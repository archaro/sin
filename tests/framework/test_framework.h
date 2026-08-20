#ifndef SIN_TEST_FRAMEWORK_H
#define SIN_TEST_FRAMEWORK_H

/* A deliberately small, dependency-free C17/POSIX test framework.  Test
 * executables own a descriptor array and pass it to tf_main(). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

typedef void (*TF_TestFn)(void);

typedef struct {
  const char *id;
  TF_TestFn fn;
  const char *tags;       /* comma-separated; may be empty */
  unsigned timeout_ms;
  const char *contracts;  /* comma-separated, at least one */
} TF_TestDescriptor;

typedef struct {
  char *stdout_data;
  size_t stdout_len;
  char *stderr_data;
  size_t stderr_len;
  int exit_status;
  bool exited;
  bool signaled;
  int signal_number;
  bool timed_out;
  bool capture_failed;
} TF_ProcessResult;

#if defined(__GNUC__) || defined(__clang__)
#define TF_PRINTF_FORMAT(format_index, argument_index) \
  __attribute__((format(printf, format_index, argument_index)))
#else
#define TF_PRINTF_FORMAT(format_index, argument_index)
#endif

typedef struct {
  char path[4096];
  bool active;
} TF_Fixture;

int tf_validate_descriptors(const TF_TestDescriptor *tests, size_t count,
                            char *detail, size_t detail_size);
int tf_main(int argc, char **argv, const TF_TestDescriptor *tests,
            size_t count);
const char *tf_program_path(void);

int tf_process_run(char *const argv[], unsigned timeout_ms,
                   TF_ProcessResult *result);
void tf_process_result_destroy(TF_ProcessResult *result);

void tf_fixture_init(TF_Fixture *fixture);
const char *tf_fixture_path(const TF_Fixture *fixture);
int tf_fixture_file(const TF_Fixture *fixture, const char *name, char *path,
                    size_t path_size);
void tf_fixture_cleanup(TF_Fixture *fixture);

void tf_reset_hooks(void);
void tf_alloc_fail_after(long allocation);
void tf_io_failures(bool write_failure, bool close_failure,
                    bool sync_failure);

void tf_fail(const char *file, int line, const char *expression,
             const char *expected, const char *actual, const char *detail);
void tf_legacy_failf(const char *file, int line, const char *format, ...)
    TF_PRINTF_FORMAT(3, 4);
void tf_assert_bytes(const char *file, int line, const char *expression,
                     const void *expected, size_t expected_len,
                     const void *actual, size_t actual_len);
void tf_assert_process(const char *file, int line, const char *expression,
                       const TF_ProcessResult *result, int expected_status);

#undef TF_PRINTF_FORMAT

#define TF_ASSERT_TRUE(value) do { \
  bool tf_actual__ = (value); \
  if (!tf_actual__) tf_fail(__FILE__, __LINE__, #value, "true", "false", NULL); \
} while (0)
#define TF_ASSERT_FALSE(value) do { \
  bool tf_actual__ = (value); \
  if (tf_actual__) tf_fail(__FILE__, __LINE__, #value, "false", "true", NULL); \
} while (0)
#define TF_ASSERT_I64(expected, actual) do { \
  int64_t tf_e__ = (expected), tf_a__ = (actual); \
  if (tf_e__ != tf_a__) { char tf_e_buf__[64], tf_a_buf__[64]; \
    (void)snprintf(tf_e_buf__, sizeof tf_e_buf__, "%lld", (long long)tf_e__); \
    (void)snprintf(tf_a_buf__, sizeof tf_a_buf__, "%lld", (long long)tf_a__); \
    tf_fail(__FILE__, __LINE__, #actual, tf_e_buf__, tf_a_buf__, NULL); } \
} while (0)
#define TF_ASSERT_U64(expected, actual) do { \
  uint64_t tf_e__ = (expected), tf_a__ = (actual); \
  if (tf_e__ != tf_a__) { char tf_e_buf__[64], tf_a_buf__[64]; \
    (void)snprintf(tf_e_buf__, sizeof tf_e_buf__, "%llu", (unsigned long long)tf_e__); \
    (void)snprintf(tf_a_buf__, sizeof tf_a_buf__, "%llu", (unsigned long long)tf_a__); \
    tf_fail(__FILE__, __LINE__, #actual, tf_e_buf__, tf_a_buf__, NULL); } \
} while (0)
#define TF_ASSERT_STR(expected, actual) do { \
  const char *tf_e__ = (expected), *tf_a__ = (actual); \
  if (!tf_e__ || !tf_a__ || strcmp(tf_e__, tf_a__) != 0) \
    tf_fail(__FILE__, __LINE__, #actual, tf_e__ ? tf_e__ : "(null)", \
            tf_a__ ? tf_a__ : "(null)", NULL); \
} while (0)
#define TF_ASSERT_BYTES(expected, expected_len, actual, actual_len) do { \
  tf_assert_bytes(__FILE__, __LINE__, #actual, (expected), (expected_len), \
                  (actual), (actual_len)); \
} while (0)
#define TF_ASSERT_FLOAT_BITS(expected, actual) \
  TF_ASSERT_U64((uint64_t)(expected), (uint64_t)(actual))
#define TF_ASSERT_DIAGNOSTIC(needle, actual) do { \
  const char *tf_a__ = (actual), *tf_n__ = (needle); \
  if (!tf_a__ || !tf_n__ || !strstr(tf_a__, tf_n__)) \
    tf_fail(__FILE__, __LINE__, #actual, tf_n__ ? tf_n__ : "(null)", \
            tf_a__ ? tf_a__ : "(null)", "diagnostic does not contain expected text"); \
} while (0)
#define TF_ASSERT_PROCESS(result, expected_status) \
  tf_assert_process(__FILE__, __LINE__, #result, (result), (expected_status))

#endif
