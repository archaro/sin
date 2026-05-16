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
      {"string_literal_hex", "tests/fixtures/string_literal.hex", "SOT: AST builder in tests/test_pipeline_golden.c | regen: make regen-fixtures"},
      {"locals_store_load_hex", "tests/fixtures/locals_store_load.hex", "SOT: pipeline/source golden tables | regen: make regen-fixtures"},
      {"arithmetic_add_hex", "tests/fixtures/arithmetic_add.hex", "SOT: pipeline/source golden tables | regen: make regen-fixtures"},
      {"boolean_compare_hex", "tests/fixtures/boolean_compare.hex", "SOT: AST builder in tests/test_pipeline_golden.c | regen: make regen-fixtures"},
      {"simple_if_hex", "tests/fixtures/simple_if.hex", "SOT: AST builder in tests/test_pipeline_golden.c | regen: make regen-fixtures"},
      {"if_elsif_else_hex", "tests/fixtures/if_elsif_else.hex", "SOT: inline source in tests/test_pipeline_source_golden.c | regen: make regen-fixtures"},
      {"locals_inc_hex", "tests/fixtures/locals_inc.hex", "SOT: inline source in tests/test_pipeline_source_golden.c | regen: make regen-fixtures"},
      {"locals_dec_hex", "tests/fixtures/locals_dec.hex", "SOT: inline source in tests/test_pipeline_source_golden.c | regen: make regen-fixtures"},
      {"sdiss_basic_hex", "tests/fixtures/sdiss/basic.hex", "SOT: hand-authored disassembly sample | regen: manual update plus expected sync"},
  };

  static const FixtureEntry object_fixtures[] = {
      {"chat_boot_obj", "examples/chat-boot.obj", "SOT: compiler output from examples/chat-boot.src | regen: ./scomp -i examples/chat-boot.src -o examples/chat-boot.obj"},
      {"chat_load_obj", "examples/chat-load.obj", "SOT: compiler output from examples/chat-load.src | regen: ./scomp -i examples/chat-load.src -o examples/chat-load.obj"},
      {"echo_boot_obj", "examples/echo-boot.obj", "SOT: compiler output from examples/echo-boot.src | regen: ./scomp -i examples/echo-boot.src -o examples/echo-boot.obj"},
      {"echo_load_obj", "examples/echo-load.obj", "SOT: compiler output from examples/echo-load.src | regen: ./scomp -i examples/echo-load.src -o examples/echo-load.obj"},
  };

  static const FixtureEntry interpret_output_fixtures[] = {
      {"chat_boot_expected", "tests/fixtures/interpret/chat-boot.expected.txt", "SOT: runtime output contract for chat-boot | regen: ./sin -f examples/chat-boot.obj > tests/fixtures/interpret/chat-boot.expected.txt"},
      {"chat_load_expected", "tests/fixtures/interpret/chat-load.expected.txt", "SOT: runtime output contract for chat-load | regen: ./sin -f examples/chat-load.obj > tests/fixtures/interpret/chat-load.expected.txt"},
      {"echo_boot_expected", "tests/fixtures/interpret/echo-boot.expected.txt", "SOT: runtime output contract for echo-boot | regen: ./sin -f examples/echo-boot.obj > tests/fixtures/interpret/echo-boot.expected.txt"},
      {"echo_load_expected", "tests/fixtures/interpret/echo-load.expected.txt", "SOT: runtime output contract for echo-load | regen: ./sin -f examples/echo-load.obj > tests/fixtures/interpret/echo-load.expected.txt"},
      {"sdiss_basic_expected", "tests/fixtures/sdiss/basic.expected.txt", "SOT: sdiss stdout for tests/fixtures/sdiss/basic.hex | regen: ./sdiss --no-header -o tests/fixtures/sdiss/basic.bin"},
  };

  for (size_t i = 0; i < sizeof(source_fixtures) / sizeof(source_fixtures[0]); i++) {
    assert_fixture_entry("source", &source_fixtures[i]);
  }
  for (size_t i = 0; i < sizeof(bytecode_hex_fixtures) / sizeof(bytecode_hex_fixtures[0]); i++) {
    assert_fixture_entry("bytecode_hex", &bytecode_hex_fixtures[i]);
  }
  for (size_t i = 0; i < sizeof(object_fixtures) / sizeof(object_fixtures[0]); i++) {
    assert_fixture_entry("object", &object_fixtures[i]);
  }
  for (size_t i = 0; i < sizeof(interpret_output_fixtures) / sizeof(interpret_output_fixtures[0]); i++) {
    assert_fixture_entry("interpret_output", &interpret_output_fixtures[i]);
  }
}
