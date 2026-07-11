#include <stdio.h>
#include <string.h>

#include "shared/test_pipeline_cases.h"
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

static void assert_no_duplicate_fixture_entries(const FixtureEntry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (strcmp(entries[i].name, entries[j].name) == 0) {
        TEST_FAILF("duplicate fixture policy name: %s", entries[i].name);
      }
      if (strcmp(entries[i].path, entries[j].path) == 0) {
        TEST_FAILF("duplicate fixture policy path: %s", entries[i].path);
      }
    }
  }
}

static void assert_pipeline_golden_fixtures_present(void) {
  size_t count = 0;
  const PipelineGoldenCase *cases = pipeline_golden_cases(&count);

  for (size_t i = 0; i < count; i++) {
    ASSERT_NOT_NULL(cases[i].fixture_path);
    FILE *f = fopen(cases[i].fixture_path, "rb");
    if (f == NULL) {
      TEST_FAILF("pipeline golden fixture missing for case %s: %s", cases[i].name,
                 cases[i].fixture_path);
    }
    fclose(f);

    for (size_t j = i + 1; j < count; j++) {
      if (strcmp(cases[i].name, cases[j].name) == 0) {
        TEST_FAILF("duplicate pipeline golden case name: %s", cases[i].name);
      }
      if (strcmp(cases[i].fixture_path, cases[j].fixture_path) == 0) {
        TEST_FAILF("duplicate pipeline golden fixture path: %s",
                   cases[i].fixture_path);
      }
    }
  }
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
      {"sdiss_basic_hex", "tests/fixtures/sdiss/basic.hex", "SOT: hand-authored disassembly sample | regen: manual update plus expected sync"},
  };

  static const FixtureEntry interpret_output_fixtures[] = {
      {"chat_boot_expected", "tests/fixtures/interpret/chat-boot.expected.txt", "SOT: runtime output contract for chat-boot | regen: ./scomp examples/chat-boot.src tests/fixtures/interpret/chat-boot.generated.obj && ./sin -o tests/fixtures/interpret/chat-boot.generated.obj > tests/fixtures/interpret/chat-boot.expected.txt"},
      {"chat_load_expected", "tests/fixtures/interpret/chat-load.expected.txt", "SOT: runtime output contract for chat-load | regen: ./scomp examples/chat-load.src tests/fixtures/interpret/chat-load.generated.obj && ./sin -o tests/fixtures/interpret/chat-load.generated.obj > tests/fixtures/interpret/chat-load.expected.txt"},
      {"echo_boot_expected", "tests/fixtures/interpret/echo-boot.expected.txt", "SOT: runtime output contract for echo-boot | regen: ./scomp examples/echo-boot.src tests/fixtures/interpret/echo-boot.generated.obj && ./sin -o tests/fixtures/interpret/echo-boot.generated.obj > tests/fixtures/interpret/echo-boot.expected.txt"},
      {"echo_load_expected", "tests/fixtures/interpret/echo-load.expected.txt", "SOT: runtime output contract for echo-load | regen: ./scomp examples/echo-load.src tests/fixtures/interpret/echo-load.generated.obj && ./sin -o tests/fixtures/interpret/echo-load.generated.obj > tests/fixtures/interpret/echo-load.expected.txt"},
      {"sdiss_basic_expected", "tests/fixtures/sdiss/basic.expected.txt", "SOT: sdiss stdout for tests/fixtures/sdiss/basic.hex | regen: ./sdiss --no-header -o tests/fixtures/sdiss/basic.bin"},
  };

  assert_no_duplicate_fixture_entries(source_fixtures,
                                      sizeof(source_fixtures) / sizeof(source_fixtures[0]));
  assert_no_duplicate_fixture_entries(bytecode_hex_fixtures,
                                      sizeof(bytecode_hex_fixtures) / sizeof(bytecode_hex_fixtures[0]));
  assert_no_duplicate_fixture_entries(interpret_output_fixtures,
                                      sizeof(interpret_output_fixtures) /
                                          sizeof(interpret_output_fixtures[0]));
  assert_pipeline_golden_fixtures_present();

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
