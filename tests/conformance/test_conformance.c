#include "test_framework.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

#define MANIFEST_PATH "tests/fixtures/conformance/conformance.manifest"
#define MAX_MANIFEST_LINE 16384u

static char *read_text(const char *path);
static char *expectation_block(const char *text, const char *name);

typedef struct {
  char *id;
  char *intent;
  char *source;
  char *contracts;
  char *compile_stdout;
  char *compile_stderr;
  char *disassembly_stdout;
  char *disassembly_stderr;
  char *runtime_expectation;
  char *notes;
  int compile_status;
  int disassembly_status;
  int runtime_status;
  unsigned runtime_runs;
  char compile_match[16];
  char disassembly_match[16];
  char runtime_match[16];
  char runtime_mode[16];
} ConformanceCase;

typedef struct {
  char *contract;
  char *positive_case;
  char *positive_witness;
  char *negative_case;
  char *negative_witness;
  char *runtime;
} Coverage;

typedef struct {
  char *contract;
  char *reason;
} Exclusion;

typedef struct {
  ConformanceCase *cases;
  size_t case_count;
  Coverage *coverage;
  size_t coverage_count;
  Exclusion *exclusions;
  size_t exclusion_count;
} Manifest;

static char validation_detail[512];

static void validation_error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  (void)vsnprintf(validation_detail, sizeof validation_detail, format, ap);
  va_end(ap);
}

static char *copy_text(const char *text) {
  size_t length;
  char *copy;
  if (!text) return NULL;
  length = strlen(text);
  copy = malloc(length + 1u);
  if (!copy) return NULL;
  memcpy(copy, text, length + 1u);
  return copy;
}

static void free_manifest(Manifest *manifest) {
  if (!manifest) return;
  for (size_t i = 0; i < manifest->case_count; i++) {
    ConformanceCase *item = &manifest->cases[i];
    free(item->id); free(item->intent); free(item->source); free(item->contracts);
    free(item->compile_stdout); free(item->compile_stderr);
    free(item->disassembly_stdout); free(item->disassembly_stderr);
    free(item->runtime_expectation); free(item->notes);
  }
  for (size_t i = 0; i < manifest->coverage_count; i++) {
    free(manifest->coverage[i].contract);
    free(manifest->coverage[i].positive_case);
    free(manifest->coverage[i].positive_witness);
    free(manifest->coverage[i].negative_case);
    free(manifest->coverage[i].negative_witness);
    free(manifest->coverage[i].runtime);
  }
  for (size_t i = 0; i < manifest->exclusion_count; i++) {
    free(manifest->exclusions[i].contract);
    free(manifest->exclusions[i].reason);
  }
  free(manifest->cases); free(manifest->coverage); free(manifest->exclusions);
  memset(manifest, 0, sizeof *manifest);
}

static bool token_valid(const char *text, bool comma_separated) {
  bool have = false, previous_separator = true;
  if (!text) return false;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (*p == (comma_separated ? ';' : '\0')) {
      if (previous_separator) return false;
      previous_separator = true;
      continue;
    }
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
          *p == '.' || *p == ':')) return false;
    have = true;
    previous_separator = false;
  }
  return have && !previous_separator;
}

static bool id_valid(const char *text) { return token_valid(text, false); }

static bool path_valid(const char *path) {
  const char *component;
  if (!path || !path[0] || path[0] == '/') return false;
  component = path;
  while (*component) {
    const char *end = strchr(component, '/');
    size_t length = end ? (size_t)(end - component) : strlen(component);
    if (length == 0 || (length == 1 && component[0] == '.') ||
        (length == 2 && component[0] == '.' && component[1] == '.')) return false;
    component = end ? end + 1 : component + length;
  }
  return true;
}

static int copy_bounded(char *destination, size_t destination_size,
                        const char *source) {
  size_t length;
  if (!destination || !source) return -1;
  length = strlen(source);
  if (length >= destination_size) return -1;
  memcpy(destination, source, length + 1u);
  return 0;
}

static int join_path(char *destination, size_t destination_size,
                     const char *left, const char *right) {
  size_t left_length, right_length, separator = 0u;
  if (!destination || !left || !right || destination_size == 0u) return -1;
  left_length = strlen(left);
  right_length = strlen(right);
  if (right_length != 0u && left_length != 0u) separator = 1u;
  if (left_length > destination_size - 1u ||
      separator > destination_size - 1u - left_length ||
      right_length > destination_size - 1u - left_length - separator) return -1;
  memcpy(destination, left, left_length);
  if (separator != 0u) destination[left_length++] = '/';
  memcpy(destination + left_length, right, right_length);
  destination[left_length + right_length] = '\0';
  return 0;
}

static int make_marker(char *destination, size_t destination_size,
                       const char *name) {
  size_t name_length;
  if (!destination || !name || destination_size < 9u) return -1;
  name_length = strlen(name);
  if (name_length > destination_size - 8u) return -1;
  memcpy(destination, "===", 3u);
  memcpy(destination + 3u, name, name_length);
  memcpy(destination + 3u + name_length, "===\n", 5u);
  destination[8u + name_length - 1u] = '\0';
  return 0;
}

static bool is_empty_ref(const char *text) { return text && strcmp(text, "-") == 0; }

static int parse_status(const char *text, int *status) {
  char *end = NULL;
  long value;
  if (strcmp(text, "skip") == 0) { *status = -1; return 0; }
  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || !end || *end || value < 0 || value > 255) return -1;
  *status = (int)value;
  return 0;
}

static int parse_expected_status(const char *text, int *status) {
  char *end = NULL;
  long value;
  if (!text || !status) return -1;
  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < 0 || value > 255)
    return -1;
  *status = (int)value;
  return 0;
}

static bool field_match_valid(const char *text) {
  return strcmp(text, "exact") == 0 || strcmp(text, "contains") == 0 ||
         strcmp(text, "skip") == 0;
}

static bool disassembly_match_valid(const char *text) {
  return strcmp(text, "signature") == 0 || strcmp(text, "skip") == 0;
}

static char **split_fields(char *line, size_t *count) {
  size_t capacity = 24u, used = 0;
  char **fields = calloc(capacity, sizeof *fields);
  char *cursor = line;
  if (!fields) return NULL;
  while (cursor) {
    char *separator = strchr(cursor, '|');
    if (used == capacity) {
      char **grown;
      capacity *= 2u;
      grown = realloc(fields, capacity * sizeof *fields);
      if (!grown) { free(fields); return NULL; }
      fields = grown;
    }
    fields[used++] = cursor;
    if (!separator) break;
    *separator = '\0';
    cursor = separator + 1;
  }
  *count = used;
  return fields;
}

static int add_case(Manifest *manifest, char **fields, size_t count) {
  ConformanceCase item = {0};
  ConformanceCase *grown;
  if (count != 19u || strcmp(fields[0], "case") != 0 ||
      !id_valid(fields[1]) || (strcmp(fields[2], "positive") != 0 &&
                               strcmp(fields[2], "negative") != 0) ||
      !path_valid(fields[3]) || !token_valid(fields[4], true) ||
      parse_status(fields[5], &item.compile_status) < 0 ||
      parse_status(fields[9], &item.disassembly_status) < 0 ||
      parse_status(fields[15], &item.runtime_status) < 0 ||
      !field_match_valid(fields[8]) || !disassembly_match_valid(fields[12]) ||
      !field_match_valid(fields[17]) ||
      (strcmp(fields[13], "skip") != 0 && strcmp(fields[13], "loadonly") != 0)) return -1;
  if (fields[6][0] == '\0' || fields[7][0] == '\0' || fields[10][0] == '\0' ||
      fields[11][0] == '\0' || fields[16][0] == '\0') return -1;
  if (!is_empty_ref(fields[6]) && !path_valid(fields[6])) return -1;
  if (!is_empty_ref(fields[7]) && !path_valid(fields[7])) return -1;
  if (!is_empty_ref(fields[10]) && !path_valid(fields[10])) return -1;
  if (!is_empty_ref(fields[11]) && !path_valid(fields[11])) return -1;
  if (!is_empty_ref(fields[16]) && !path_valid(fields[16])) return -1;
  if (strcmp(fields[13], "skip") == 0) {
    if (item.runtime_status != -1 || strcmp(fields[17], "skip") != 0) return -1;
    item.runtime_runs = 0;
  } else {
    char *end = NULL;
    unsigned long runs = strtoul(fields[14], &end, 10);
    if (!end || *end || runs == 0 || runs > 16u || item.runtime_status < 0 ||
        strcmp(fields[17], "skip") == 0) return -1;
    item.runtime_runs = (unsigned)runs;
  }
  if (strcmp(fields[2], "positive") == 0) {
    if (item.compile_status != 0 || item.disassembly_status != 0 ||
        strcmp(fields[8], "skip") == 0 || item.runtime_status < 0 ||
        strcmp(fields[12], "skip") == 0 || is_empty_ref(fields[10]) ||
        is_empty_ref(fields[16])) return -1;
  } else if (item.compile_status == 0 || item.disassembly_status != -1 ||
             item.runtime_status != -1 || strcmp(fields[12], "skip") != 0 ||
             strcmp(fields[17], "skip") != 0) return -1;
  item.id = copy_text(fields[1]); item.intent = copy_text(fields[2]);
  item.source = copy_text(fields[3]); item.contracts = copy_text(fields[4]);
  item.compile_stdout = copy_text(fields[6]); item.compile_stderr = copy_text(fields[7]);
  item.disassembly_stdout = copy_text(fields[10]); item.disassembly_stderr = copy_text(fields[11]);
  item.runtime_expectation = copy_text(fields[16]); item.notes = copy_text(fields[18]);
  if (copy_bounded(item.compile_match, sizeof item.compile_match, fields[8]) < 0 ||
      copy_bounded(item.disassembly_match, sizeof item.disassembly_match, fields[12]) < 0 ||
      copy_bounded(item.runtime_match, sizeof item.runtime_match, fields[17]) < 0 ||
      copy_bounded(item.runtime_mode, sizeof item.runtime_mode, fields[13]) < 0) goto failure;
  if (!item.id || !item.intent || !item.source || !item.contracts ||
      !item.compile_stdout || !item.compile_stderr || !item.disassembly_stdout ||
      !item.disassembly_stderr || !item.runtime_expectation || !item.notes) goto failure;
  for (size_t i = 0; i < manifest->case_count; i++) {
    if (strcmp(manifest->cases[i].id, item.id) == 0 ||
        strcmp(manifest->cases[i].source, item.source) == 0) goto failure;
  }
  grown = realloc(manifest->cases, (manifest->case_count + 1u) * sizeof *grown);
  if (!grown) goto failure;
  manifest->cases = grown;
  manifest->cases[manifest->case_count++] = item;
  return 0;
failure:
  free(item.id); free(item.intent); free(item.source); free(item.contracts);
  free(item.compile_stdout); free(item.compile_stderr); free(item.disassembly_stdout);
  free(item.disassembly_stderr); free(item.runtime_expectation); free(item.notes);
  return -1;
}

static int add_coverage(Manifest *manifest, char **fields, size_t count) {
  Coverage item = {0};
  Coverage *grown;
  if (count != 7u || strcmp(fields[0], "coverage") != 0 ||
      !id_valid(fields[1]) || !id_valid(fields[2]) || !path_valid(fields[3]) ||
      (!id_valid(fields[4]) && strcmp(fields[4], "native") != 0) ||
      (!path_valid(fields[5]) && strcmp(fields[5], "-") != 0) ||
      (!id_valid(fields[6]) && strcmp(fields[6], "-") != 0) ||
      (strcmp(fields[6], "yes") != 0 && strcmp(fields[6], "no") != 0)) return -1;
  item.contract = copy_text(fields[1]); item.positive_case = copy_text(fields[2]);
  item.positive_witness = copy_text(fields[3]); item.negative_case = copy_text(fields[4]);
  item.negative_witness = copy_text(fields[5]); item.runtime = copy_text(fields[6]);
  if (!item.contract || !item.positive_case || !item.positive_witness ||
      !item.negative_case || !item.negative_witness || !item.runtime) goto failure;
  for (size_t i = 0; i < manifest->coverage_count; i++) {
    if (strcmp(manifest->coverage[i].contract, item.contract) == 0) goto failure;
  }
  grown = realloc(manifest->coverage, (manifest->coverage_count + 1u) * sizeof *grown);
  if (!grown) goto failure;
  manifest->coverage = grown;
  manifest->coverage[manifest->coverage_count++] = item;
  return 0;
failure:
  free(item.contract); free(item.positive_case); free(item.positive_witness);
  free(item.negative_case); free(item.negative_witness); free(item.runtime); return -1;
}

static int add_exclusion(Manifest *manifest, char **fields, size_t count) {
  Exclusion item = {0};
  Exclusion *grown;
  if (count != 3u || strcmp(fields[0], "exclude") != 0 ||
      !id_valid(fields[1]) || fields[2][0] == '\0') return -1;
  item.contract = copy_text(fields[1]); item.reason = copy_text(fields[2]);
  if (!item.contract || !item.reason) goto failure;
  for (size_t i = 0; i < manifest->exclusion_count; i++) {
    if (strcmp(manifest->exclusions[i].contract, item.contract) == 0) goto failure;
  }
  grown = realloc(manifest->exclusions, (manifest->exclusion_count + 1u) * sizeof *grown);
  if (!grown) goto failure;
  manifest->exclusions = grown;
  manifest->exclusions[manifest->exclusion_count++] = item;
  return 0;
failure:
  free(item.contract); free(item.reason); return -1;
}

static bool file_exists(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool listed_reference(const Manifest *manifest, const char *relative) {
  /* This checked-in golden predates the declarative runtime schema and remains
   * owned by the legacy interpreter suite.  Keep its exemption exact so new
   * conformance drift still fails closed. */
  if (strcmp(relative, "tests/fixtures/conformance/positive-core.expected.txt") == 0)
    return true;
  for (size_t i = 0; i < manifest->case_count; i++) {
    const ConformanceCase *item = &manifest->cases[i];
    if ((!is_empty_ref(item->compile_stdout) && strcmp(item->compile_stdout, relative) == 0) ||
        (!is_empty_ref(item->compile_stderr) && strcmp(item->compile_stderr, relative) == 0) ||
        (!is_empty_ref(item->disassembly_stdout) && strcmp(item->disassembly_stdout, relative) == 0) ||
        (!is_empty_ref(item->disassembly_stderr) && strcmp(item->disassembly_stderr, relative) == 0) ||
        (!is_empty_ref(item->runtime_expectation) && strcmp(item->runtime_expectation, relative) == 0) ||
        strcmp(item->source, relative) == 0) return true;
  }
  return false;
}

static int scan_fixture_tree(const Manifest *manifest, const char *root,
                             const char *relative) {
  char directory[PATH_MAX];
  DIR *dir;
  struct dirent *entry;
  int result = 0;
  if (join_path(directory, sizeof directory, root, relative) < 0) return -1;
  dir = opendir(directory);
  if (!dir) return -1;
  while ((entry = readdir(dir)) != NULL && result == 0) {
    char child[PATH_MAX];
    char child_relative[PATH_MAX];
    struct stat st;
    if (entry->d_name[0] == '.') continue;
    if (join_path(child, sizeof child, directory, entry->d_name) < 0 ||
        join_path(child_relative, sizeof child_relative, relative, entry->d_name) < 0 ||
        stat(child, &st) < 0) { result = -1; break; }
    if (S_ISDIR(st.st_mode)) result = scan_fixture_tree(manifest, root, child_relative);
    else if (S_ISREG(st.st_mode) && (strstr(entry->d_name, ".src") || strstr(entry->d_name, ".txt"))) {
      char reference[PATH_MAX];
      if (join_path(reference, sizeof reference, "tests/fixtures/conformance", child_relative) < 0 ||
          !listed_reference(manifest, reference)) result = -1;
    }
  }
  (void)closedir(dir);
  return result;
}

static int read_manifest(const char *path, Manifest *manifest) {
  FILE *file = fopen(path, "r");
  char line[MAX_MANIFEST_LINE];
  bool header = false;
  if (!file) return -1;
  while (fgets(line, sizeof line, file)) {
    size_t length = strlen(line);
    char **fields;
    size_t count;
    if (length == sizeof line - 1u && line[length - 1u] != '\n') { fclose(file); return -1; }
    while (length && (line[length - 1u] == '\n' || line[length - 1u] == '\r')) line[--length] = '\0';
    if (!line[0] || line[0] == '#') continue;
    if (!header) {
      header = strcmp(line, "SINISTRA-CONFORMANCE|1|case-v1|coverage-v1|exclude-v1") == 0;
      if (!header) { fclose(file); return -1; }
      continue;
    }
    fields = split_fields(line, &count);
    if (!fields || count == 0 ||
        (strcmp(fields[0], "case") == 0 ? add_case(manifest, fields, count) :
         strcmp(fields[0], "coverage") == 0 ? add_coverage(manifest, fields, count) :
         strcmp(fields[0], "exclude") == 0 ? add_exclusion(manifest, fields, count) : -1) < 0) {
      free(fields); fclose(file); return -1;
    }
    free(fields);
  }
  if (ferror(file) || !header || manifest->case_count == 0) { fclose(file); return -1; }
  fclose(file);
  return 0;
}

static bool coverage_for(const Manifest *manifest, const char *contract) {
  for (size_t i = 0; i < manifest->coverage_count; i++)
    if (strcmp(manifest->coverage[i].contract, contract) == 0) return true;
  for (size_t i = 0; i < manifest->exclusion_count; i++)
    if (strcmp(manifest->exclusions[i].contract, contract) == 0) return true;
  return false;
}

static const Coverage *coverage_entry(const Manifest *manifest, const char *contract) {
  for (size_t i = 0; i < manifest->coverage_count; i++)
    if (strcmp(manifest->coverage[i].contract, contract) == 0) return &manifest->coverage[i];
  return NULL;
}

static bool catalog_contract_exists(const char *root, const char *contract) {
  char path[PATH_MAX];
  char line[MAX_MANIFEST_LINE];
  FILE *file;
  if (join_path(path, sizeof path, root, "tests/inventory/contracts.csv") < 0) return false;
  file = fopen(path, "r");
  if (!file) return false;
  (void)fgets(line, sizeof line, file);
  while (fgets(line, sizeof line, file)) {
    char *comma = strchr(line, ',');
    if (comma) {
      *comma = '\0';
      if (strcmp(line, contract) == 0) {
        fclose(file);
        return true;
      }
    }
  }
  fclose(file);
  return false;
}

static bool case_contracts_valid(const char *root, const char *contracts) {
  const char *start = contracts;
  while (*start) {
    const char *end = strchr(start, ';');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    char contract[128];
    if (length == 0u || length >= sizeof contract) return false;
    memcpy(contract, start, length);
    contract[length] = '\0';
    if (!catalog_contract_exists(root, contract)) return false;
    start = end ? end + 1 : start + length;
  }
  return true;
}

static bool source_witness_has_contract(const char *root, const char *contract,
                                       const char *relative) {
  static const struct { const char *contract; const char *marker; } markers[] = {
    {"language.token.tinteger", "1"}, {"language.token.tfloat", "1.5"},
    {"language.token.tstringlit", "\""}, {"language.token.tlocal", "@"},
    {"language.token.tlayer", ".relative"}, {"language.token.tlibname", "sys."},
    {"language.token.tcodebody", "code"}, {"language.token.tunknownchar", "^"},
    {"language.token.ttrue", "true"}, {"language.token.tfalse", "false"},
    {"language.token.tnil", "nil"}, {"language.token.tliststart", "#["},
    {"language.token.titemref", "&missing"}, {"language.token.tbreak", "break"},
    {"language.token.tcontinue", "continue"}, {"language.token.tsemi", ";"},
    {"language.token.twhile", "while"}, {"language.token.tdo", "DO"},
    {"language.token.tendwhile", "endwhile"}, {"language.token.tif", "if"},
    {"language.token.tthen", "then"}, {"language.token.telse", "else"},
    {"language.token.telsif", "elsif"}, {"language.token.tendif", "endif"},
    {"language.token.treturn", "return"}, {"language.token.tforeach", "foreach"},
    {"language.token.tin", " in "}, {"language.token.tendfor", "endfor"},
    {"language.token.tassign", " = "}, {"language.token.tinc", "++"},
    {"language.token.tdec", "--"}, {"language.token.tlayersep", "foo.12"},
    {"language.token.tderefstart", "[foo]"}, {"language.token.tcode", "code"},
    {"language.token.tderefend", "[foo]"}, {"language.token.tlparen", "("},
    {"language.token.trparen", ")"}, {"language.token.tlbrace", "{"},
    {"language.token.trbrace", "}"}, {"language.token.tcomma", ","},
    {"language.token.tor", " or "}, {"language.token.tand", " and "},
    {"language.token.tequal", "=="}, {"language.token.tnotequal", "!="},
    {"language.token.tlt", " < "}, {"language.token.tgt", " > "},
    {"language.token.tlteq", "<="}, {"language.token.tgteq", ">="},
    {"language.token.tplus", " + "}, {"language.token.tminus", " - "},
    {"language.token.tmult", " * "}, {"language.token.tdiv", " / "},
    {"language.token.tmod", " % "}, {"language.token.tnot", "!"},
    {"language.production.input", "sys."}, {"language.production.stmtlist", ";\n"},
    {"language.production.stmtsemi", ";"}, {"language.production.stmt", " = "},
    {"language.production.expr", "1"}, {"language.production.libcall", "sys."},
    {"language.production.elsif_else_opt", "elsif"},
    {"language.production.params", "code {"},
    {"language.production.param_list", "code {@a, @b}"},
    {"language.production.param_local", "code {"},
    {"language.production.args", "{"},
    {"language.production.arg_list", "pair{1, 2}"},
    {"language.production.item_assignment", "conformance_assignment ="},
    {"language.production.list", "#["}, {"language.production.list_elems", "#[1, 2]"},
    {"language.production.itemref", "&missing"},
    {"language.production.item", "conformance_assignment"},
    {"language.production.first_layer", ".relative"},
    {"language.production.subsequent_layers", "foo.12"},
    {"language.production.layer", "foo.12"},
    {"language.production.dereference", "[foo]"},
    {"language.production.deref_content", "[foo]"},
    {"language.operator.add", " + "}, {"language.operator.subtract", " - "},
    {"language.operator.multiply", " * "}, {"language.operator.divide", " / "},
    {"language.operator.modulo", " % "}, {"language.operator.comparison", " == "},
    {"language.operator.boolean", " and "}, {"language.literal.integer", "1"},
    {"language.literal.float", "1.5"}, {"language.literal.string", "\""},
    {"language.literal.boolean", "true"}, {"language.literal.nil", "nil"},
    {"language.statement.assignment", " = "},
    {"language.statement.expression", "sys."},
    {"language.statement.return", "return"}, {"language.statement.while", "while"},
    {"language.statement.do-while", "DO"}, {"language.statement.if", "if"},
    {"language.statement.foreach", "foreach"}, {"language.statement.break", "break"},
    {"language.statement.continue", "continue"},
    {"language.expression.binary", " + "}, {"language.expression.unary", "!"},
    {"language.expression.local", "@"}, {"language.expression.call", "pair{1, 2}"},
    {"language.expression.libcall", "sys.itemname{"},
    {"language.expression.item", "conformance_assignment"},
    {"language.expression.item-reference", "&missing"},
    {"language.expression.list", "#["},
    {"language.item-syntax.absolute-layer", "foo.12"},
    {"language.item-syntax.relative-layer", ".relative"},
    {"language.item-syntax.dereference", "[foo]"},
    {"language.item-syntax.layer-chain", "foo.12"},
    {"language.item-syntax.item-save", "sys.save"},
    {"language.semantic-rule.local-definition", "@conformance_local ="},
    {"language.semantic-rule.local-before-definition", "@x"},
    {"language.semantic-rule.break-context", "break"},
    {"language.semantic-rule.continue-context", "continue"},
    {"language.semantic-rule.call-arity", "pair{1, 2}"},
    {"language.semantic-rule.libcall-resolution", "sys.itemname{"},
    {"language.semantic-rule.item-name", "23foo.baz.43bar43.2"},
    {"language.semantic-rule.loop-variable", "foreach @element"},
    {"language.diagnostic.lexer-error", "^"},
    {"language.diagnostic.parser-error", ",]"},
    {"language.diagnostic.semantic-error", "break"},
    {"language.diagnostic.compiler-error", "continue;"},
    {"language.diagnostic.source-span", "continue;"},
    {"language.diagnostic.overflow", "9223372036854775808"},
    {"language.diagnostic.unknown-libcall", "sys.unknown"},
  };
  char path[PATH_MAX];
  char *source;
  char libcall_marker[128];
  const char *marker = NULL;
  if (join_path(path, sizeof path, root, relative) < 0) return false;
  source = read_text(path);
  if (!source) return false;
  for (size_t i = 0; i < sizeof markers / sizeof markers[0]; i++) {
    if (strcmp(contract, markers[i].contract) == 0) marker = markers[i].marker;
  }
  if (!marker && strncmp(contract, "libcall.", 8u) == 0) {
    const char *library = contract + 8u;
    const char *separator = strchr(library, '.');
    if (separator) {
      int written = snprintf(libcall_marker, sizeof libcall_marker, "%.*s.%s",
                             (int)(separator - library), library, separator + 1u);
      if (written > 0 && (size_t)written < sizeof libcall_marker) marker = libcall_marker;
    }
  }
  bool found = marker && strstr(source, marker) != NULL;
  free(source);
  return found;
}

static const ConformanceCase *case_with_intent(const Manifest *manifest,
                                               const char *id,
                                               const char *intent) {
  for (size_t i = 0; i < manifest->case_count; i++)
    if (strcmp(manifest->cases[i].id, id) == 0 &&
        strcmp(manifest->cases[i].intent, intent) == 0) return &manifest->cases[i];
  return NULL;
}

static bool inventory_contract_exists(const char *root, const char *contract) {
  const char *paths[] = {"tests/inventory/language.csv", "tests/inventory/libcalls.csv"};
  char path[PATH_MAX];
  char line[MAX_MANIFEST_LINE];
  for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
    FILE *file;
    if (join_path(path, sizeof path, root, paths[i]) < 0) return false;
    file = fopen(path, "r");
    if (!file) return false;
    (void)fgets(line, sizeof line, file);
    while (fgets(line, sizeof line, file)) {
      char *comma = strchr(line, ',');
      if (comma) {
        *comma = '\0';
        if (strcmp(line, contract) == 0) {
          fclose(file);
          return true;
        }
      }
    }
    fclose(file);
  }
  return false;
}

static int validate_inventory(const Manifest *manifest, const char *root) {
  const char *inventory_paths[] = {"tests/inventory/language.csv", "tests/inventory/libcalls.csv"};
  for (size_t i = 0; i < manifest->coverage_count; i++) {
    if (!inventory_contract_exists(root, manifest->coverage[i].contract)) {
      validation_error("unknown coverage contract %s", manifest->coverage[i].contract);
      return -1;
    }
    for (size_t j = 0; j < manifest->exclusion_count; j++) {
      if (strcmp(manifest->coverage[i].contract, manifest->exclusions[j].contract) == 0) {
        validation_error("coverage/exclusion overlap for %s", manifest->coverage[i].contract);
        return -1;
      }
    }
  }
  for (size_t i = 0; i < manifest->exclusion_count; i++) {
    if (!inventory_contract_exists(root, manifest->exclusions[i].contract)) {
      validation_error("unknown exclusion contract %s", manifest->exclusions[i].contract);
      return -1;
    }
  }
  for (size_t path_index = 0; path_index < sizeof inventory_paths / sizeof inventory_paths[0]; path_index++) {
    char path[PATH_MAX];
    FILE *file;
    char line[MAX_MANIFEST_LINE];
    if (join_path(path, sizeof path, root, inventory_paths[path_index]) < 0) return -1;
    file = fopen(path, "r");
    if (!file) return -1;
    (void)fgets(line, sizeof line, file);
    while (fgets(line, sizeof line, file)) {
      char *comma = strchr(line, ',');
      char *kind;
      char *kind_end;
      if (!comma) { fclose(file); return -1; }
      *comma = '\0';
      kind = comma + 1;
      kind_end = strchr(kind, ',');
      if (!kind_end) { fclose(file); return -1; }
      *kind_end = '\0';
      if (!id_valid(line) || !coverage_for(manifest, line)) {
        validation_error("inventory %s is unclassified", line);
        fclose(file); return -1;
      }
      if (path_index == 0u) {
        const Coverage *coverage = coverage_entry(manifest, line);
        if (!coverage) continue;
        bool negative_only = strcmp(kind, "diagnostic") == 0 ||
                             strcmp(line, "language.token.tunknownchar") == 0 ||
                             (strcmp(kind, "semantic-rule") == 0 &&
                              case_with_intent(manifest, coverage ? coverage->positive_case : "", "negative") != NULL);
        if (!coverage || !case_with_intent(manifest, coverage->positive_case,
                                           negative_only ? "negative" : "positive") ||
            !case_with_intent(manifest, coverage->negative_case, "negative") ||
            strcmp(coverage->negative_case, "native") == 0 ||
            strcmp(coverage->negative_witness, "-") == 0 ||
            (negative_only ? strcmp(coverage->runtime, "no") != 0 :
                             strcmp(coverage->runtime, "yes") != 0)) {
          validation_error("invalid %s coverage for %s", kind, line);
          fclose(file); return -1;
        }
      } else if (coverage_entry(manifest, line) != NULL) {
        const Coverage *coverage = coverage_entry(manifest, line);
        if (!case_with_intent(manifest, coverage->positive_case, "positive") ||
            strcmp(coverage->runtime, "yes") != 0) {
          validation_error("invalid libcall coverage for %s", line);
          fclose(file); return -1;
        }
      }
    }
    if (ferror(file)) { fclose(file); return -1; }
    fclose(file);
  }
  for (size_t i = 0; i < manifest->coverage_count; i++) {
    char witness[PATH_MAX];
    const ConformanceCase *witness_case = NULL;
    const char *positive_intent = case_with_intent(manifest, manifest->coverage[i].positive_case,
                                                   "positive") ? "positive" : "negative";
    if (!case_with_intent(manifest, manifest->coverage[i].positive_case, positive_intent) ||
        join_path(witness, sizeof witness, root, manifest->coverage[i].positive_witness) < 0 ||
        !file_exists(witness)) {
      validation_error("missing positive witness %s for %s", manifest->coverage[i].positive_witness,
                       manifest->coverage[i].contract);
      return -1;
    }
    for (size_t j = 0; j < manifest->case_count; j++) {
      if (strcmp(manifest->cases[j].id, manifest->coverage[i].positive_case) == 0) {
        witness_case = &manifest->cases[j];
        break;
      }
    }
    if (!witness_case || strcmp(witness_case->source, manifest->coverage[i].positive_witness) != 0 ||
        !source_witness_has_contract(root, manifest->coverage[i].contract,
                                     manifest->coverage[i].positive_witness)) {
      validation_error("positive witness mismatch for %s", manifest->coverage[i].contract);
      return -1;
    }
    if (strcmp(manifest->coverage[i].negative_case, "native") != 0) {
      if (join_path(witness, sizeof witness, root, manifest->coverage[i].negative_witness) < 0 ||
          !file_exists(witness)) {
        validation_error("missing negative witness %s for %s", manifest->coverage[i].negative_witness,
                         manifest->coverage[i].contract);
        return -1;
      }
      witness_case = NULL;
      for (size_t j = 0; j < manifest->case_count; j++) {
        if (strcmp(manifest->cases[j].id, manifest->coverage[i].negative_case) == 0 &&
            strcmp(manifest->cases[j].intent, "negative") == 0) {
          witness_case = &manifest->cases[j];
          break;
        }
      }
      if (!witness_case || strcmp(witness_case->source, manifest->coverage[i].negative_witness) != 0 ||
          !source_witness_has_contract(root, manifest->coverage[i].contract,
                                       manifest->coverage[i].negative_witness)) {
        validation_error("negative witness mismatch for %s", manifest->coverage[i].contract);
        return -1;
      }
    }
  }
  return 0;
}

static int validate_manifest(const char *root, const Manifest *manifest) {
  char fixture_root[PATH_MAX];
  validation_detail[0] = '\0';
  for (size_t i = 0; i < manifest->case_count; i++) {
    if (!case_contracts_valid(root, manifest->cases[i].contracts)) {
      validation_error("unknown contract tag(s) for case %s", manifest->cases[i].id);
      return -1;
    }
  }
  if (join_path(fixture_root, sizeof fixture_root, root, "tests/fixtures/conformance") < 0 ||
      scan_fixture_tree(manifest, fixture_root, "") < 0) {
    validation_error("fixture tree contains an undeclared source or expectation");
    return -1;
  }
  if (validate_inventory(manifest, root) < 0) return -1;
  for (size_t i = 0; i < manifest->case_count; i++) {
    const ConformanceCase *item = &manifest->cases[i];
    char path[PATH_MAX];
    if (join_path(path, sizeof path, root, item->source) < 0 || !file_exists(path)) {
      validation_error("missing source %s for case %s", item->source, item->id);
      return -1;
    }
    const char *refs[] = {item->compile_stdout, item->compile_stderr, item->disassembly_stdout,
                          item->disassembly_stderr, item->runtime_expectation};
    for (size_t j = 0; j < sizeof refs / sizeof refs[0]; j++) {
      if (!is_empty_ref(refs[j])) {
        if (join_path(path, sizeof path, root, refs[j]) < 0 || !file_exists(path)) {
          validation_error("missing reference %s for case %s", refs[j], item->id);
          return -1;
        }
      }
    }
    if (item->runtime_status >= 0) {
      char *expectation = NULL;
      char *exit_block = NULL;
      int expected_status;
      if (join_path(path, sizeof path, root, item->runtime_expectation) < 0 ||
          !(expectation = read_text(path)) ||
          !(exit_block = expectation_block(expectation, "exit"))) {
        free(expectation);
        validation_error("invalid runtime expectation for case %s", item->id);
        return -1;
      }
      if (parse_expected_status(exit_block, &expected_status) < 0) {
        free(exit_block);
        free(expectation);
        validation_error("invalid runtime exit for case %s", item->id);
        return -1;
      }
      free(exit_block);
      free(expectation);
      if (expected_status != item->runtime_status) {
        validation_error("runtime status mismatch for case %s", item->id);
        return -1;
      }
    }
  }
  return 0;
}

static char *read_text(const char *path) {
  FILE *file = fopen(path, "rb");
  char *data = NULL;
  size_t length = 0;
  if (!file) return NULL;
  for (;;) {
    char buffer[4096];
    size_t got = fread(buffer, 1, sizeof buffer, file);
    if (got) {
      char *grown = realloc(data, length + got + 1u);
      if (!grown) { free(data); fclose(file); return NULL; }
      data = grown; memcpy(data + length, buffer, got); length += got; data[length] = '\0';
    }
    if (got < sizeof buffer) break;
  }
  if (ferror(file)) { free(data); data = NULL; }
  fclose(file);
  if (!data) data = copy_text("");
  return data;
}

static char *normalize_output(const char *text) {
  size_t length = text ? strlen(text) : 0u;
  char *copy = malloc(length + 1u);
  if (!copy) return NULL;
  size_t used = 0;
  for (size_t i = 0; i < length; i++) if (text[i] != '\r') copy[used++] = text[i];
  while (used && copy[used - 1u] == '\n') used--;
  copy[used] = '\0';
  return copy;
}

static char *expectation_block(const char *text, const char *name) {
  char marker[64];
  const char *start, *end;
  size_t length;
  if (make_marker(marker, sizeof marker, name) < 0) return NULL;
  start = strstr(text, marker);
  if (!start) return NULL;
  start += strlen(marker);
  end = strstr(start, "\n===");
  if (!end) end = text + strlen(text);
  length = (size_t)(end - start);
  while (length && start[length - 1u] == '\n') length--;
  char *copy = malloc(length + 1u);
  if (!copy) return NULL;
  memcpy(copy, start, length); copy[length] = '\0';
  return copy;
}

static bool expectation_matches(const char *actual, const char *expected,
                                 const char *mode) {
  if (strcmp(mode, "contains") == 0) return strstr(actual, expected) != NULL;
  return strcmp(actual, expected) == 0;
}

static void fail_phase(const char *id, const char *phase, const char *reason,
                       const char *expected, const char *actual) {
  char detail[512];
  size_t used = 0u;
  const char *parts[] = {id, " ", phase, ": ", reason};
  for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
    size_t length = strlen(parts[i]);
    if (length > sizeof detail - 1u - used) length = sizeof detail - 1u - used;
    memcpy(detail + used, parts[i], length);
    used += length;
  }
  detail[used] = '\0';
  tf_fail(__FILE__, __LINE__, "conformance phase", expected, actual, detail);
}

static void assert_phase(const char *id, const char *phase, const TF_ProcessResult *result,
                         int status, const char *stdout_expected, const char *stderr_expected,
                         const char *match) {
  char *actual_stdout = normalize_output(result->stdout_data ? result->stdout_data : "");
  char *actual_stderr = normalize_output(result->stderr_data ? result->stderr_data : "");
  char *normalized_stdout_expected = normalize_output(stdout_expected ? stdout_expected : "");
  char *normalized_stderr_expected = normalize_output(stderr_expected ? stderr_expected : "");
  char status_expected[32];
  char status_actual[32];
  if (!result->exited || result->signaled || result->timed_out || result->capture_failed)
    fail_phase(id, phase, "process did not exit normally", "normal exit", result->stderr_data ? result->stderr_data : "");
  (void)snprintf(status_expected, sizeof status_expected, "%d", status);
  (void)snprintf(status_actual, sizeof status_actual, "%d", result->exit_status);
  if (result->exit_status != status)
    fail_phase(id, phase, "exit status mismatch", status_expected, status_actual);
  if (!actual_stdout || !actual_stderr || !normalized_stdout_expected || !normalized_stderr_expected)
    fail_phase(id, phase, "output allocation failed", "allocated output", "allocation failure");
  if ((normalized_stdout_expected[0] != '\0' &&
       !expectation_matches(actual_stdout, normalized_stdout_expected, match)) ||
      (normalized_stdout_expected[0] == '\0' && actual_stdout[0] != '\0'))
    fail_phase(id, phase, "stdout mismatch", normalized_stdout_expected, actual_stdout);
  if ((normalized_stderr_expected[0] != '\0' &&
       !expectation_matches(actual_stderr, normalized_stderr_expected, match)) ||
      (normalized_stderr_expected[0] == '\0' && actual_stderr[0] != '\0'))
    fail_phase(id, phase, "stderr mismatch", normalized_stderr_expected, actual_stderr);
  free(actual_stdout); free(actual_stderr);
  free(normalized_stdout_expected); free(normalized_stderr_expected);
}

static void assert_disassembly(const ConformanceCase *item,
                               const TF_ProcessResult *result,
                               const char *expected_text) {
  char *actual = normalize_output(result->stdout_data ? result->stdout_data : "");
  char *expected = normalize_output(expected_text ? expected_text : "");
  const char *cursor;
  const char *search;
  bool have_signature = false;
  bool have_summary = false;
  if (!result->exited || result->signaled || result->timed_out || result->capture_failed)
    fail_phase(item->id, "disassembly", "process did not exit normally", "normal exit",
               result->stderr_data ? result->stderr_data : "");
  if (result->exit_status != item->disassembly_status)
    fail_phase(item->id, "disassembly", "exit status mismatch", "declared status", "actual status");
  if (strcmp(item->disassembly_match, "signature") != 0)
    fail_phase(item->id, "disassembly", "disassembly expectations must be ordered signatures",
               "signature", item->disassembly_match);
  if (!actual || !expected) fail_phase(item->id, "disassembly", "output allocation failed", "allocated output", "allocation failure");
  cursor = expected;
  search = actual;
  while (*cursor) {
    const char *end = strchr(cursor, '\n');
    size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
    char *needle = malloc(length + 1u);
    if (!needle) fail_phase(item->id, "disassembly", "expectation allocation failed", "allocated expectation", "allocation failure");
    memcpy(needle, cursor, length); needle[length] = '\0';
    if (needle[0] != '\0') {
      have_signature = true;
      if (strncmp(needle, "Summary:", 8u) == 0) {
        const char *remaining = end ? end + 1 : cursor + length;
        while (*remaining == '\n' || *remaining == '\r') remaining++;
        if (*remaining != '\0')
          fail_phase(item->id, "disassembly", "summary signature must be last",
                     "Summary as final expectation", expected);
        have_summary = true;
      }
      bool found = false;
      const char *line = search;
      while (*line) {
        const char *line_end = strchr(line, '\n');
        size_t line_length = line_end ? (size_t)(line_end - line) : strlen(line);
        const char *candidate = line;
        size_t candidate_length = line_length;
        if (line_length >= 12u && strncmp(line, "Byte ", 5u) == 0) {
          const char *prefix_end = strstr(line, ": ");
          if (prefix_end && prefix_end + 2 <= line + line_length) {
            candidate = prefix_end + 2;
            candidate_length = (size_t)((line + line_length) - candidate);
          }
        }
        if ((length <= candidate_length && strncmp(candidate, needle, length) == 0) &&
            (length == candidate_length || needle[length - 1u] == '=' ||
             candidate[length] == ' ' || candidate[length] == '\t' ||
             needle[length - 1u] == ':' )) {
          found = true;
          search = line_end ? line_end + 1 : line + line_length;
          if (strncmp(needle, "Summary:", 8u) == 0) {
            const char *remaining = search;
            while (*remaining == '\n' || *remaining == '\r') remaining++;
            if (*remaining != '\0')
              fail_phase(item->id, "disassembly", "summary is not final output",
                         "Summary as final output", actual);
          }
          break;
        }
        line = line_end ? line_end + 1 : line + line_length;
      }
      if (!found)
        fail_phase(item->id, "disassembly", "declared instruction/summary missing", needle, actual);
    }
    free(needle);
    cursor = end ? end + 1 : cursor + length;
  }
  if (!have_signature || !have_summary)
    fail_phase(item->id, "disassembly", "signature expectation is incomplete",
               "at least one signature and Summary:", expected);
  free(actual); free(expected);
}

static const ConformanceCase *find_case(const Manifest *manifest, const char *id) {
  for (size_t i = 0; i < manifest->case_count; i++)
    if (strcmp(manifest->cases[i].id, id) == 0) return &manifest->cases[i];
  return NULL;
}

static void run_case(const char *case_id) {
  Manifest manifest = {0};
  const ConformanceCase *item;
  char root[PATH_MAX], source[PATH_MAX], compile_stdout_path[PATH_MAX], compile_stderr_path[PATH_MAX];
  char dis_stdout_path[PATH_MAX], dis_stderr_path[PATH_MAX], runtime_path[PATH_MAX];
  char compiler[PATH_MAX], disassembler[PATH_MAX], interpreter[PATH_MAX], object[PATH_MAX];
  TF_Fixture fixture;
  if (!getcwd(root, sizeof root) || read_manifest(MANIFEST_PATH, &manifest) < 0) {
    free_manifest(&manifest);
    tf_fail(__FILE__, __LINE__, "conformance manifest", "readable manifest", "read failure", case_id);
    return;
  }
  if (validate_manifest(root, &manifest) < 0) {
    free_manifest(&manifest);
    tf_fail(__FILE__, __LINE__, "conformance manifest", "valid manifest",
            validation_detail[0] ? validation_detail : "validation failure", case_id);
    return;
  }
  item = find_case(&manifest, case_id);
  TF_ASSERT_TRUE(item != NULL);
  TF_ASSERT_TRUE(realpath(item->source, source) != NULL);
  TF_ASSERT_TRUE(realpath("scomp", compiler) != NULL);
  TF_ASSERT_TRUE(realpath("sdiss", disassembler) != NULL);
  TF_ASSERT_TRUE(realpath("sin", interpreter) != NULL);
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_path(&fixture) != NULL);
  TF_ASSERT_TRUE(join_path(object, sizeof object, tf_fixture_path(&fixture), "object.bin") == 0);
  TF_ASSERT_TRUE(join_path(compile_stdout_path, sizeof compile_stdout_path, root, item->compile_stdout) == 0);
  TF_ASSERT_TRUE(join_path(compile_stderr_path, sizeof compile_stderr_path, root, item->compile_stderr) == 0);
  TF_ASSERT_TRUE(join_path(dis_stdout_path, sizeof dis_stdout_path, root, item->disassembly_stdout) == 0);
  TF_ASSERT_TRUE(join_path(dis_stderr_path, sizeof dis_stderr_path, root, item->disassembly_stderr) == 0);
  TF_ASSERT_TRUE(join_path(runtime_path, sizeof runtime_path, root, item->runtime_expectation) == 0);
  {
    char *args[] = {compiler, "-q", source, object, NULL};
    TF_ProcessResult result;
    TF_ASSERT_TRUE(tf_process_run(args, 10000, &result) == 0);
    char *expected_stdout = is_empty_ref(item->compile_stdout) ? copy_text("") : read_text(compile_stdout_path);
    char *expected_stderr = is_empty_ref(item->compile_stderr) ? copy_text("") : read_text(compile_stderr_path);
    TF_ASSERT_TRUE(expected_stdout && expected_stderr);
    assert_phase(item->id, "compile", &result, item->compile_status, expected_stdout, expected_stderr, item->compile_match);
    free(expected_stdout); free(expected_stderr); tf_process_result_destroy(&result);
  }
  if (item->disassembly_status >= 0) {
    char *args[] = {disassembler, "-q", "--no-header", "-o", object, NULL};
    TF_ProcessResult result;
    TF_ASSERT_TRUE(tf_process_run(args, 10000, &result) == 0);
    char *expected_stdout = is_empty_ref(item->disassembly_stdout) ? copy_text("") : read_text(dis_stdout_path);
    char *expected_stderr = is_empty_ref(item->disassembly_stderr) ? copy_text("") : read_text(dis_stderr_path);
    TF_ASSERT_TRUE(expected_stdout && expected_stderr);
    assert_disassembly(item, &result, expected_stdout);
    {
      char *actual_stderr = normalize_output(result.stderr_data ? result.stderr_data : "");
      char *expected_stderr_normalized = normalize_output(expected_stderr);
      if (!actual_stderr || !expected_stderr_normalized ||
          strcmp(actual_stderr, expected_stderr_normalized) != 0)
        fail_phase(item->id, "disassembly", "stderr mismatch", expected_stderr_normalized, actual_stderr);
      free(actual_stderr); free(expected_stderr_normalized);
    }
    free(expected_stdout); free(expected_stderr); tf_process_result_destroy(&result);
  }
  if (item->runtime_status >= 0) {
    char *expectation = read_text(runtime_path);
    TF_ASSERT_TRUE(expectation != NULL);
    TF_ASSERT_TRUE(chdir(tf_fixture_path(&fixture)) == 0);
    if (mkdir("srcroot", 0700) < 0) TF_ASSERT_I64(0, errno == EEXIST ? 0 : -1);
    for (unsigned run = 0; run < item->runtime_runs; run++) {
      char stdout_name[32];
      const char *block_name = "stdout";
      if (run != 0) {
        size_t run_number = (size_t)run + 1u;
        size_t digits = run_number >= 10u ? 2u : 1u;
        memcpy(stdout_name, "stdout_run", 10u);
        if (digits == 1u) {
          stdout_name[10] = (char)('0' + run_number);
        } else {
          stdout_name[10] = (char)('0' + run_number / 10u);
          stdout_name[11] = (char)('0' + run_number % 10u);
        }
        stdout_name[10u + digits] = '\0';
        block_name = stdout_name;
      }
      char *expected_stdout = expectation_block(expectation, block_name);
      char *expected_stderr = expectation_block(expectation, "stderr");
      char *expected_exit = expectation_block(expectation, "exit");
      int exit_status;
      TF_ProcessResult result;
      char *args[] = {interpreter, "--loadonly", "--strict-validation",
                      "-i", "items.dat", "-s", "srcroot", "-o", object, NULL};
      TF_ASSERT_TRUE(expected_stdout && expected_stderr && expected_exit);
      TF_ASSERT_TRUE(parse_expected_status(expected_exit, &exit_status) == 0);
      TF_ASSERT_I64(item->runtime_status, exit_status);
      TF_ASSERT_TRUE(tf_process_run(args, 15000, &result) == 0);
      assert_phase(item->id, "runtime", &result, item->runtime_status,
                   expected_stdout, expected_stderr, item->runtime_match);
      free(expected_stdout); free(expected_stderr); free(expected_exit); tf_process_result_destroy(&result);
    }
    free(expectation);
  }
  TF_ASSERT_TRUE(chdir(root) == 0);
  free_manifest(&manifest);
}

static void manifest_validation(void) {
  Manifest manifest = {0};
  char root[PATH_MAX];
  TF_ASSERT_TRUE(getcwd(root, sizeof root) != NULL);
  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  if (validate_manifest(root, &manifest) < 0) {
    free_manifest(&manifest);
    tf_fail(__FILE__, __LINE__, "validate_manifest", "valid manifest",
            validation_detail[0] ? validation_detail : "validation failure", MANIFEST_PATH);
    return;
  }
  free_manifest(&manifest);
}

static void expect_manifest_rejected(TF_Fixture *fixture, const char *name,
                                     const char *text) {
  char path[PATH_MAX];
  Manifest manifest = {0};
  FILE *file;
  TF_ASSERT_TRUE(tf_fixture_file(fixture, name, path, sizeof path) == 0);
  file = fopen(path, "w");
  TF_ASSERT_TRUE(file != NULL);
  TF_ASSERT_TRUE(fputs(text, file) >= 0);
  TF_ASSERT_TRUE(fclose(file) == 0);
  TF_ASSERT_TRUE(read_manifest(path, &manifest) < 0);
  free_manifest(&manifest);
}

static void expect_manifest_body_rejected(TF_Fixture *fixture, const char *name,
                                          const char *body) {
  char text[MAX_MANIFEST_LINE];
  const char header[] = "SINISTRA-CONFORMANCE|1|case-v1|coverage-v1|exclude-v1\n";
  size_t header_length = sizeof header - 1u;
  size_t body_length = strlen(body);
  TF_ASSERT_TRUE(header_length + body_length + 1u <= sizeof text);
  memcpy(text, header, header_length);
  memcpy(text + header_length, body, body_length + 1u);
  expect_manifest_rejected(fixture, name, text);
}

static void malformed_manifest_fails_closed(void) {
  TF_Fixture fixture;
  Manifest manifest = {0};
  char root[PATH_MAX];
  char stray[PATH_MAX];
  const char *valid_case = "case|valid|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|exact|0|-|-|signature|loadonly|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|valid\n";
  tf_fixture_init(&fixture);
  expect_manifest_rejected(&fixture, "bad-header.manifest",
                           "SINISTRA-CONFORMANCE|2|case-v1|coverage-v1|exclude-v1\n");
  expect_manifest_body_rejected(&fixture, "bad-fields.manifest", "case|broken\n");
  {
    char body[MAX_MANIFEST_LINE];
    TF_ASSERT_TRUE(copy_bounded(body, sizeof body, valid_case) == 0);
    TF_ASSERT_TRUE(strlen(valid_case) * 2u + 1u <= sizeof body);
    memcpy(body + strlen(valid_case), valid_case, strlen(valid_case) + 1u);
    expect_manifest_body_rejected(&fixture, "duplicate-id.manifest", body);
  }
  expect_manifest_body_rejected(&fixture, "duplicate-source.manifest",
                           "case|one|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|exact|0|-|-|signature|loadonly|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|one\n"
                           "case|two|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|exact|0|-|-|signature|loadonly|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|two\n");
  expect_manifest_body_rejected(&fixture, "bad-status.manifest",
                           "case|bad-status|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|bogus|-|-|exact|0|-|-|signature|loadonly|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|bad\n");
  expect_manifest_body_rejected(&fixture, "bad-mode.manifest",
                           "case|bad-mode|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|exact|0|-|-|signature|invalid|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|bad\n");
  expect_manifest_body_rejected(&fixture, "bad-match.manifest",
                           "case|bad-match|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|invalid|0|-|-|signature|loadonly|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|bad\n");
  expect_manifest_body_rejected(&fixture, "missing-disassembly-reference.manifest",
                           "case|missing-disassembly-reference|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|exact|0|-|-|signature|loadonly|1|0|tests/fixtures/conformance/positive-core.runtime.expected.txt|exact|bad\n");
  expect_manifest_body_rejected(&fixture, "missing-runtime-reference.manifest",
                           "case|missing-runtime-reference|positive|tests/fixtures/conformance/positive-core.src|conformance.framework.pipeline|0|-|-|exact|0|tests/fixtures/conformance/positive-core.disassembly.expected.txt|-|signature|loadonly|1|0|-|exact|bad\n");
  {
    int status;
    TF_ASSERT_TRUE(parse_expected_status("0junk", &status) < 0);
    TF_ASSERT_TRUE(parse_expected_status("", &status) < 0);
  }

  TF_ASSERT_TRUE(getcwd(root, sizeof root) != NULL);
  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  free(manifest.cases[0].source);
  manifest.cases[0].source = copy_text("tests/fixtures/conformance/missing.src");
  TF_ASSERT_TRUE(manifest.cases[0].source != NULL);
  TF_ASSERT_TRUE(validate_manifest(root, &manifest) < 0);
  free_manifest(&manifest);

  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  manifest.cases[0].runtime_status = 1;
  TF_ASSERT_TRUE(validate_manifest(root, &manifest) < 0);
  free_manifest(&manifest);
  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  free(manifest.cases[0].runtime_expectation);
  manifest.cases[0].runtime_expectation = copy_text("tests/fixtures/conformance/missing.expected.txt");
  TF_ASSERT_TRUE(manifest.cases[0].runtime_expectation != NULL);
  TF_ASSERT_TRUE(validate_manifest(root, &manifest) < 0);
  free_manifest(&manifest);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "stray.src", stray, sizeof stray) == 0);
  {
    FILE *file = fopen(stray, "w");
    TF_ASSERT_TRUE(file != NULL);
    TF_ASSERT_TRUE(fputs("1;\n", file) >= 0);
    TF_ASSERT_TRUE(fclose(file) == 0);
  }
  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  TF_ASSERT_TRUE(scan_fixture_tree(&manifest, tf_fixture_path(&fixture), "") < 0);
  free_manifest(&manifest);

  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  TF_ASSERT_TRUE(manifest.coverage_count > 0u);
  manifest.coverage_count--;
  TF_ASSERT_TRUE(validate_inventory(&manifest, root) < 0);
  manifest.coverage_count++;
  free_manifest(&manifest);

  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  free(manifest.coverage[0].contract);
  manifest.coverage[0].contract = copy_text("language.unknown-conformance-contract");
  TF_ASSERT_TRUE(manifest.coverage[0].contract != NULL);
  TF_ASSERT_TRUE(validate_inventory(&manifest, root) < 0);
  free_manifest(&manifest);

  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  for (size_t i = 0; i < manifest.coverage_count; i++) {
    if (strcmp(manifest.coverage[i].contract, "language.expression.call") == 0) {
      free(manifest.coverage[i].negative_case);
      free(manifest.coverage[i].negative_witness);
      manifest.coverage[i].negative_case = copy_text("negative-parser-unknown");
      manifest.coverage[i].negative_witness =
        copy_text("tests/fixtures/conformance/negative/parser-unknown-character.src");
      TF_ASSERT_TRUE(manifest.coverage[i].negative_case != NULL);
      TF_ASSERT_TRUE(manifest.coverage[i].negative_witness != NULL);
      break;
    }
  }
  TF_ASSERT_TRUE(validate_inventory(&manifest, root) < 0);
  free_manifest(&manifest);

  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  free(manifest.coverage[0].contract);
  manifest.coverage[0].contract = copy_text("language.diagnostic.allocation-error");
  TF_ASSERT_TRUE(manifest.coverage[0].contract != NULL);
  TF_ASSERT_TRUE(validate_inventory(&manifest, root) < 0);
  free_manifest(&manifest);
  tf_fixture_cleanup(&fixture);
}

static void conformance_positive_core(void) { run_case("positive-core"); }
static void conformance_positive_structures(void) { run_case("positive-structures"); }
static void conformance_libcall_sys(void) { run_case("libcall-sys"); }
static void conformance_libcall_list(void) { run_case("libcall-list"); }
static void conformance_libcall_str(void) { run_case("libcall-str"); }
static void conformance_libcall_task(void) { run_case("libcall-task"); }
static void conformance_persistence(void) { run_case("persistence-list-itemref"); }
static void conformance_negative_cases(void) {
  Manifest manifest = {0};
  TF_ASSERT_TRUE(read_manifest(MANIFEST_PATH, &manifest) == 0);
  for (size_t i = 0; i < manifest.case_count; i++) {
    if (strcmp(manifest.cases[i].intent, "negative") == 0) run_case(manifest.cases[i].id);
  }
  free_manifest(&manifest);
}

static const TF_TestDescriptor tests[] = {
  {"conformance.manifest", manifest_validation, "conformance", 5000, "conformance.framework.manifest"},
  {"conformance.malformed-manifest", malformed_manifest_fails_closed, "conformance", 5000, "conformance.framework.manifest"},
  {"conformance.positive-core", conformance_positive_core, "conformance", 30000, "conformance.framework.pipeline"},
  {"conformance.positive-structures", conformance_positive_structures, "conformance", 30000, "conformance.framework.pipeline"},
  {"conformance.libcall-sys", conformance_libcall_sys, "conformance", 30000, "conformance.framework.libcall"},
  {"conformance.libcall-list", conformance_libcall_list, "conformance", 30000, "conformance.framework.libcall"},
  {"conformance.libcall-str", conformance_libcall_str, "conformance", 30000, "conformance.framework.libcall"},
  {"conformance.libcall-task", conformance_libcall_task, "conformance", 30000, "conformance.framework.libcall"},
  {"conformance.persistence", conformance_persistence, "conformance,exclusive", 30000, "conformance.framework.persistence"},
  {"conformance.negative", conformance_negative_cases, "conformance", 30000, "conformance.framework.negative"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
