#include "test_framework.h"

void test_floatconv_binary64_edge_cases(void);
void test_floatconv_binary64_formatting(void);
void test_floatconv_binary64_format_roundtrip(void);
void test_parser_float_literals_decimal_forms(void);
void test_parser_float_literals_integer_still_int(void);
void test_parser_float_literals_unary_minus_preserves_float_literals(void);
void test_parser_float_literals_item_layers_unchanged(void);
void test_parser_float_literals_malformed_rejected(void);
void test_parser_integer_literals_range(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_parser_float_literals_decimal_forms", test_parser_float_literals_decimal_forms, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_float_literals_decimal_forms,language.literal.float,language.token.tfloat"},
    {"rewrite.core.test_parser_float_literals_integer_still_int", test_parser_float_literals_integer_still_int, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_float_literals_integer_still_int,language.token.tinteger"},
    {"rewrite.core.test_parser_float_literals_unary_minus_preserves_float_literals", test_parser_float_literals_unary_minus_preserves_float_literals, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_float_literals_unary_minus_preserves_float_literals,language.token.tminus"},
    {"rewrite.core.test_parser_float_literals_item_layers_unchanged", test_parser_float_literals_item_layers_unchanged, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_float_literals_item_layers_unchanged"},
    {"rewrite.core.test_parser_float_literals_malformed_rejected", test_parser_float_literals_malformed_rejected, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_float_literals_malformed_rejected,language.diagnostic.lexer-error,language.literal.float,language.token.tfloat"},
    {"rewrite.core.test_parser_integer_literals_range", test_parser_integer_literals_range, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_integer_literals_range,language.literal.integer,language.token.tinteger"},
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
