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
     "language.token.tfloat,language.literal.float"},
    {"rewrite.core.test_parser_float_literals_integer_still_int", test_parser_float_literals_integer_still_int, "exclusive", 30000,
     "language.token.tinteger"},
    {"rewrite.core.test_parser_float_literals_unary_minus_preserves_float_literals", test_parser_float_literals_unary_minus_preserves_float_literals, "exclusive", 30000,
     "language.token.tminus"},
    {"rewrite.core.test_parser_float_literals_item_layers_unchanged", test_parser_float_literals_item_layers_unchanged, "exclusive", 30000,
     "test.core.test_parser_float_literals_item_layers_unchanged"},
    {"rewrite.core.test_parser_float_literals_malformed_rejected", test_parser_float_literals_malformed_rejected, "exclusive", 30000,
     "language.token.tfloat,language.literal.float,language.diagnostic.lexer-error"},
    {"rewrite.core.test_parser_integer_literals_range", test_parser_integer_literals_range, "exclusive", 30000,
     "language.token.tinteger,language.literal.integer"},
    {"rewrite.core.test_floatconv_binary64_edge_cases",
     test_floatconv_binary64_edge_cases, "", 30000,
     "api.common.float-format"},
    {"rewrite.core.test_floatconv_binary64_formatting",
     test_floatconv_binary64_formatting, "", 30000,
     "api.common.float-format"},
    {"rewrite.core.test_floatconv_binary64_format_roundtrip",
     test_floatconv_binary64_format_roundtrip, "", 30000,
     "api.common.float-format"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
