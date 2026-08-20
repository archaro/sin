#include "test_framework.h"

void test_error_message_table_defines_active_errors(void);
void test_error_item_preserves_compiler_diagnostic_fields(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_error_message_table_defines_active_errors",
     test_error_message_table_defines_active_errors, "exclusive", 30000,
     "api.common.errors,baseline.legacy.unified.core.test_error_message_table_defines_active_errors"},
    {"rewrite.core.test_error_item_preserves_compiler_diagnostic_fields",
     test_error_item_preserves_compiler_diagnostic_fields, "exclusive", 30000,
     "api.common.errors,api.compiler.diagnostics,baseline.legacy.unified.core.test_error_item_preserves_compiler_diagnostic_fields"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
