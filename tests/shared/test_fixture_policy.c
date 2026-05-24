#include <stdio.h>
#include <string.h>

#include "test_assert.h"

typedef struct {
  const char *name;
  const char *path;
  const char *comment;
} FixtureEntry;

static void assert_fixture_entry(const char *class_name, const FixtureEntry *entry) {
  FILE *f = fopen(entry->path, "rb");
  ASSERT_NOT_NULL(f);
  fclose(f);

  ASSERT_TRUE(strncmp(entry->comment, "SOT:", 4) == 0);
  ASSERT_TRUE(strstr(entry->comment, "| regen:") != NULL);

  (void)class_name;
}

void test_fixture_policy_declared_goldens_exist(void) {
  static const FixtureEntry source_fixtures[] = {
      {"chat_boot_src", "examples/chat-boot.src", "SOT: examples/chat-boot.src | regen: authored source fixture"},
      {"chat_load_src", "examples/chat-load.src", "SOT: examples/chat-load.src | regen: authored source fixture"},
      {"echo_boot_src", "examples/echo-boot.src", "SOT: examples/echo-boot.src | regen: authored source fixture"},
      {"echo_load_src", "examples/echo-load.src", "SOT: examples/echo-load.src | regen: authored source fixture"},
      {"int_literal_src", "tests/fixtures/int_literal.src", "SOT: tests/fixtures/int_literal.src | regen: authored source fixture"},
  };

  static const FixtureEntry bytecode_hex_fixtures[] = {
      {"int_literal_hex", "tests/fixtures/int_literal.hex", "SOT: pipeline output for tests/fixtures/int_literal.src | regen: make regen-fixtures"},
      {"string_literal_hex", "tests/fixtures/string_literal.hex", "SOT: AST builder in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"locals_store_load_hex", "tests/fixtures/locals_store_load.hex", "SOT: pipeline/source golden tables | regen: make regen-fixtures"},
      {"arithmetic_add_hex", "tests/fixtures/arithmetic_add.hex", "SOT: pipeline/source golden tables | regen: make regen-fixtures"},
      {"boolean_compare_hex", "tests/fixtures/boolean_compare.hex", "SOT: AST builder in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"simple_if_hex", "tests/fixtures/simple_if.hex", "SOT: AST builder in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"if_elsif_else_hex", "tests/fixtures/if_elsif_else.hex", "SOT: source case in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"locals_inc_hex", "tests/fixtures/locals_inc.hex", "SOT: source case in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"locals_dec_hex", "tests/fixtures/locals_dec.hex", "SOT: source case in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"libcall_exprstmt_hex", "tests/fixtures/libcall_exprstmt.hex", "SOT: source case in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"item_numeric_layer_hex", "tests/fixtures/item_numeric_layer.hex", "SOT: source case in tests/shared/test_pipeline_cases.c | regen: make regen-fixtures"},
      {"sdiss_basic_hex", "tests/fixtures/sdiss/basic.hex", "SOT: hand-authored disassembly sample | regen: manual update plus expected sync"},
  };

  static const FixtureEntry interpret_output_fixtures[] = {
      {"chat_boot_expected", "tests/fixtures/interpret/chat-boot.expected.txt", "SOT: runtime output contract for chat-boot | regen: ./scomp examples/chat-boot.src tests/fixtures/interpret/chat-boot.generated.obj && ./sin -o tests/fixtures/interpret/chat-boot.generated.obj > tests/fixtures/interpret/chat-boot.expected.txt"},
      {"chat_load_expected", "tests/fixtures/interpret/chat-load.expected.txt", "SOT: runtime output contract for chat-load | regen: ./scomp examples/chat-load.src tests/fixtures/interpret/chat-load.generated.obj && ./sin -o tests/fixtures/interpret/chat-load.generated.obj > tests/fixtures/interpret/chat-load.expected.txt"},
      {"echo_boot_expected", "tests/fixtures/interpret/echo-boot.expected.txt", "SOT: runtime output contract for echo-boot | regen: ./scomp examples/echo-boot.src tests/fixtures/interpret/echo-boot.generated.obj && ./sin -o tests/fixtures/interpret/echo-boot.generated.obj > tests/fixtures/interpret/echo-boot.expected.txt"},
      {"echo_load_expected", "tests/fixtures/interpret/echo-load.expected.txt", "SOT: runtime output contract for echo-load | regen: ./scomp examples/echo-load.src tests/fixtures/interpret/echo-load.generated.obj && ./sin -o tests/fixtures/interpret/echo-load.generated.obj > tests/fixtures/interpret/echo-load.expected.txt"},
      {"sdiss_basic_expected", "tests/fixtures/sdiss/basic.expected.txt", "SOT: sdiss stdout for tests/fixtures/sdiss/basic.hex | regen: ./sdiss --no-header -o tests/fixtures/sdiss/basic.bin"},
  };

  for (size_t i = 0; i < sizeof(source_fixtures) / sizeof(source_fixtures[0]); i++) {
    assert_fixture_entry("source", &source_fixtures[i]);
  }
  for (size_t i = 0; i < sizeof(bytecode_hex_fixtures) / sizeof(bytecode_hex_fixtures[0]); i++) {
    assert_fixture_entry("bytecode_hex", &bytecode_hex_fixtures[i]);
  }
  for (size_t i = 0; i < sizeof(interpret_output_fixtures) / sizeof(interpret_output_fixtures[0]); i++) {
    assert_fixture_entry("interpret_output", &interpret_output_fixtures[i]);
  }
}
