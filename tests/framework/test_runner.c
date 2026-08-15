#include "test_framework.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  char *binary;
  char *id;
  char *tags;
  char *contracts;
  unsigned timeout_ms;
} Entry;

static bool valid_tokens(const char *text, bool comma_separated, bool allow_empty) {
  bool have = false, previous_comma = true;
  if (!text) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (*p == ',') {
      if (!comma_separated || previous_comma) return false;
      previous_comma = true;
    } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
               *p == '.' || *p == ':') {
      have = true; previous_comma = false;
    } else return false;
  }
  return have ? !previous_comma : allow_empty;
}

static char *copy_text(const char *start, size_t length) {
  char *copy = malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, start, length); copy[length] = '\0';
  return copy;
}

static void free_entries(Entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(entries[i].binary); free(entries[i].id); free(entries[i].tags); free(entries[i].contracts);
  }
  free(entries);
}

static bool has_tag(const char *tags, const char *wanted) {
  const char *p = tags;
  size_t wanted_length = strlen(wanted);
  while (p && *p) {
    const char *end = strchr(p, ',');
    size_t length = end ? (size_t)(end - p) : strlen(p);
    if (length == wanted_length && strncmp(p, wanted, length) == 0) return true;
    p = end ? end + 1 : NULL;
  }
  return false;
}

static int parse_listing(const char *binary, const char *text, Entry **entries,
                         size_t *count) {
  const char *line = text;
  while (line && *line) {
    const char *end = strchr(line, '\n');
    size_t length = end ? (size_t)(end - line) : strlen(line);
    if (length != 0) {
      char *record = copy_text(line, length);
      char *fields[6] = {0};
      char *cursor = record;
      size_t field_count = 0;
      if (!record) return -1;
      while (field_count < 6 && cursor) {
        fields[field_count++] = cursor;
        cursor = strchr(cursor, '|');
        if (cursor) { *cursor = '\0'; cursor++; }
      }
      if (field_count != 6 || strcmp(fields[0], "TF") != 0 || strcmp(fields[1], "LIST") != 0 ||
          !valid_tokens(fields[2], false, false) || !valid_tokens(fields[3], true, true) ||
          !valid_tokens(fields[5], true, false) || !fields[4][0]) {
        free(record); return -1;
      }
      char *tail = NULL;
      unsigned long timeout = strtoul(fields[4], &tail, 10);
      if (!tail || *tail || timeout > 3600000ul) { free(record); return -1; }
      Entry *grown = realloc(*entries, (*count + 1) * sizeof **entries);
      if (!grown) { free(record); return -1; }
      *entries = grown;
      Entry *entry = &grown[*count];
      entry->binary = strdup(binary); entry->id = strdup(fields[2]);
      entry->tags = strdup(fields[3]); entry->contracts = strdup(fields[5]);
      entry->timeout_ms = (unsigned)timeout;
      if (!entry->binary || !entry->id || !entry->tags || !entry->contracts) {
        free(entry->binary); free(entry->id); free(entry->tags); free(entry->contracts);
        free(record); return -1;
      }
      for (size_t prior = 0; prior < *count; prior++) {
        if (strcmp((*entries)[prior].id, entry->id) == 0) {
          free(entry->binary); free(entry->id); free(entry->tags); free(entry->contracts);
          free(record); return -2;
        }
      }
      if (has_tag(entry->tags, "helper")) {
        free(entry->binary); free(entry->id); free(entry->tags); free(entry->contracts);
      } else {
        (*count)++;
      }
      free(record);
    }
    line = end ? end + 1 : NULL;
  }
  return 0;
}

static bool serial_entry(const Entry *entry) {
  const char *p = entry->tags;
  while (p && *p) {
    const char *end = strchr(p, ',');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if ((n == 9 && strncmp(p, "exclusive", n) == 0) ||
        (n == 7 && strncmp(p, "network", n) == 0) ||
        (n == 9 && strncmp(p, "benchmark", n) == 0)) return true;
    p = end ? end + 1 : NULL;
  }
  return false;
}

static int run_entry(const Entry *entry) {
  char *args[] = {entry->binary, "--run", entry->id, NULL};
  TF_ProcessResult result;
  int failed;
  (void)setenv("TF_TEST_ID", entry->id, 1);
  if (tf_process_run(args, entry->timeout_ms, &result) < 0 && !result.exited && !result.signaled && !result.timed_out) {
    (void)unsetenv("TF_TEST_ID");
    return -1;
  }
  (void)unsetenv("TF_TEST_ID");
  failed = !(result.exited && result.exit_status == 0 && !result.timed_out && !result.capture_failed);
  if (failed || (getenv("TF_VERBOSE") && strcmp(getenv("TF_VERBOSE"), "0") != 0)) {
    if (result.stdout_data) (void)fwrite(result.stdout_data, 1, result.stdout_len, stderr);
    if (result.stderr_data) (void)fwrite(result.stderr_data, 1, result.stderr_len, stderr);
  }
  (void)printf("TF|RESULT|%s|%s|%s|%s\n", entry->id, failed ? "FAIL" : "PASS",
               result.timed_out ? "timeout" : (result.signaled ? "signal" : "completed"),
               entry->tags);
  tf_process_result_destroy(&result);
  return failed ? -1 : 0;
}

static void run_batch(Entry *entries, size_t first, size_t count,
                      size_t *passed, size_t *failed) {
  pid_t *workers = calloc(count, sizeof *workers);
  int *files = calloc(count, sizeof *files);
  if (!workers || !files) { free(workers); free(files); *failed += count; return; }
  for (size_t i = 0; i < count; i++) {
    char name[] = "/tmp/sin-runner-XXXXXX";
    files[i] = mkstemp(name);
    (void)unlink(name);
    if (files[i] < 0) { workers[i] = -1; continue; }
    workers[i] = fork();
    if (workers[i] == 0) {
      int rc;
      (void)dup2(files[i], STDOUT_FILENO); (void)close(files[i]);
      rc = run_entry(&entries[first + i]);
      _exit(rc == 0 ? 0 : 1);
    }
    if (workers[i] < 0) { (void)close(files[i]); files[i] = -1; }
  }
  for (size_t i = 0; i < count; i++) {
    int status = 1;
    char buffer[4096];
    ssize_t got;
    if (workers[i] < 0 || files[i] < 0 || waitpid(workers[i], &status, 0) < 0) { (*failed)++; continue; }
    if (lseek(files[i], 0, SEEK_SET) < 0) { (void)close(files[i]); (*failed)++; continue; }
    while ((got = read(files[i], buffer, sizeof buffer)) > 0) (void)fwrite(buffer, 1, (size_t)got, stdout);
    (void)close(files[i]);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) (*passed)++; else (*failed)++;
  }
  free(workers); free(files);
}

int main(int argc, char **argv) {
  Entry *entries = NULL;
  size_t count = 0, passed = 0, failed = 0;
  unsigned jobs = 1;
  const char *jobs_text = getenv("TEST_JOBS");
  int first_binary = 1;
  if (jobs_text && jobs_text[0]) {
    char *tail = NULL; unsigned long parsed = strtoul(jobs_text, &tail, 10);
    if (!tail || *tail != '\0' || parsed == 0 || parsed > 256) {
      (void)fprintf(stderr, "invalid TEST_JOBS value\n"); return 2;
    }
    jobs = (unsigned)parsed;
  }
  if (argc > 1 && strcmp(argv[1], "--jobs") == 0) {
    char *tail = NULL; unsigned long parsed;
    if (argc < 3) { (void)fprintf(stderr, "--jobs requires a positive integer\n"); return 2; }
    parsed = strtoul(argv[2], &tail, 10);
    if (!tail || *tail || parsed == 0 || parsed > 256) { (void)fprintf(stderr, "invalid --jobs value\n"); return 2; }
    jobs = (unsigned)parsed; first_binary = 3;
  }
  if (argc <= first_binary) { (void)fprintf(stderr, "usage: %s [--jobs N] TEST_EXECUTABLE...\n", argv[0]); return 2; }
  for (int i = first_binary; i < argc; i++) {
    char *args[] = {argv[i], "--list", NULL};
    TF_ProcessResult result;
    int listing_rc = tf_process_run(args, 10000, &result);
    int parse_rc = parse_listing(argv[i], result.stdout_data ? result.stdout_data : "", &entries, &count);
    if (listing_rc < 0 || !result.exited || result.exit_status != 0 || parse_rc < 0) {
      if (parse_rc == -2) (void)fprintf(stderr, "TF|ERROR|duplicate ID across executables\n");
      (void)fprintf(stderr, "TF|ERROR|discovery|%s\n", argv[i]);
      tf_process_result_destroy(&result); free_entries(entries, count); return 2;
    }
    tf_process_result_destroy(&result);
  }
  /* A serial tagged test acts as a barrier.  Non-tagged work is launched in
   * batches when TEST_JOBS is greater than one; each worker is still a
   * process, preserving the framework's isolation guarantee. */
  size_t index = 0;
  while (index < count) {
    if (jobs == 1 || serial_entry(&entries[index])) {
      if (run_entry(&entries[index]) == 0) passed++; else failed++;
      index++; continue;
    }
    size_t batch = 0;
    while (index + batch < count && batch < jobs && !serial_entry(&entries[index + batch])) batch++;
    run_batch(entries, index, batch, &passed, &failed);
    index += batch;
  }
  (void)printf("TF|TOTAL|all|%zu|%zu|%zu\n", count, passed, failed);
  free_entries(entries, count);
  return failed == 0 ? 0 : 1;
}
