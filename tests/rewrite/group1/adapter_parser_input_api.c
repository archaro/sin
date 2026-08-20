#include "test_framework.h"

void test_parser_scanner_setup_allocation_failures(void);
void test_parser_cleanup_allocation_failures(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_parser_scanner_setup_allocation_failures",
     test_parser_scanner_setup_allocation_failures, "exclusive", 30000,
     "api.common.memory,baseline.legacy.unified.core.test_parser_scanner_setup_allocation_failures"},
    {"rewrite.core.test_parser_cleanup_allocation_failures",
     test_parser_cleanup_allocation_failures, "exclusive", 30000,
     "api.common.memory,api.compiler.parser-lifecycle,baseline.legacy.unified.core.test_parser_cleanup_allocation_failures,language.diagnostic.allocation-error"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
