#include "test_framework.h"

void test_floatconv_binary64_edge_cases(void);
void test_floatconv_binary64_formatting(void);
void test_floatconv_binary64_format_roundtrip(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_floatconv_binary64_edge_cases",
     test_floatconv_binary64_edge_cases, "", 30000,
     "api.common.float-format,baseline.legacy.unified.core.test_floatconv_binary64_edge_cases"},
    {"rewrite.core.test_floatconv_binary64_formatting",
     test_floatconv_binary64_formatting, "", 30000,
     "api.common.float-format,baseline.legacy.unified.core.test_floatconv_binary64_formatting"},
    {"rewrite.core.test_floatconv_binary64_format_roundtrip",
     test_floatconv_binary64_format_roundtrip, "", 30000,
     "api.common.float-format,baseline.legacy.unified.core.test_floatconv_binary64_format_roundtrip"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
