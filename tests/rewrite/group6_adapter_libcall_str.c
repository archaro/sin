#include "test_framework.h"

void test_str_libcalls_float_returns_invalidargs_nil(void);
void test_str_len_returns_string_byte_length(void);
void test_str_valtostr_converts_values_to_strings(void);
void test_str_case_libcalls_mutate_strings_in_place(void);
void test_str_trim_libcalls_return_trimmed_strings(void);
void test_str_substr_returns_requested_byte_range(void);
void test_str_substr_invalid_args_return_nil(void);
void test_str_find_and_contains_return_expected_results(void);
void test_str_find_and_contains_invalid_args_return_contracts(void);
void test_str_startswith_and_endswith_return_expected_results(void);
void test_str_startswith_and_endswith_invalid_args_return_contracts(void);
void test_str_eqcasei_returns_expected_results(void);
void test_str_eqcasei_invalid_args_return_contracts(void);
void test_str_replace_returns_expected_results(void);
void test_str_replace_invalid_args_return_nil(void);
void test_str_repeat_returns_expected_results(void);
void test_str_repeat_invalid_args_return_nil(void);
void test_str_growth_libcalls_enforce_string_limit(void);
void test_str_padleft_and_padright_return_expected_results(void);
void test_str_padleft_and_padright_invalid_args_return_nil(void);
void test_str_libcall_invalidargs_uses_context_itemroot(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_str_libcalls_float_returns_invalidargs_nil", test_str_libcalls_float_returns_invalidargs_nil, "exclusive", 30000, "test.runtime.test_str_libcalls_float_returns_invalidargs_nil"},
    {"rewrite.runtime.test_str_len_returns_string_byte_length", test_str_len_returns_string_byte_length, "exclusive", 30000, "api.libcall.str,libcall.str.len"},
    {"rewrite.runtime.test_str_valtostr_converts_values_to_strings", test_str_valtostr_converts_values_to_strings, "exclusive", 30000, "libcall.str.valtostr"},
    {"rewrite.runtime.test_str_case_libcalls_mutate_strings_in_place", test_str_case_libcalls_mutate_strings_in_place, "exclusive", 30000, "libcall.str.capitalise,libcall.str.upper,libcall.str.lower"},
    {"rewrite.runtime.test_str_trim_libcalls_return_trimmed_strings", test_str_trim_libcalls_return_trimmed_strings, "exclusive", 30000, "libcall.str.trim,libcall.str.ltrim,libcall.str.rtrim"},
    {"rewrite.runtime.test_str_substr_returns_requested_byte_range", test_str_substr_returns_requested_byte_range, "exclusive", 30000, "libcall.str.substr"},
    {"rewrite.runtime.test_str_substr_invalid_args_return_nil", test_str_substr_invalid_args_return_nil, "exclusive", 30000, "libcall.str.substr"},
    {"rewrite.runtime.test_str_find_and_contains_return_expected_results", test_str_find_and_contains_return_expected_results, "exclusive", 30000, "libcall.str.find,libcall.str.contains"},
    {"rewrite.runtime.test_str_find_and_contains_invalid_args_return_contracts", test_str_find_and_contains_invalid_args_return_contracts, "exclusive", 30000, "api.libcall.str,libcall.str.find,libcall.str.contains"},
    {"rewrite.runtime.test_str_startswith_and_endswith_return_expected_results", test_str_startswith_and_endswith_return_expected_results, "exclusive", 30000, "libcall.str.startswith,libcall.str.endswith"},
    {"rewrite.runtime.test_str_startswith_and_endswith_invalid_args_return_contracts", test_str_startswith_and_endswith_invalid_args_return_contracts, "exclusive", 30000, "libcall.str.startswith,libcall.str.endswith"},
    {"rewrite.runtime.test_str_eqcasei_returns_expected_results", test_str_eqcasei_returns_expected_results, "exclusive", 30000, "libcall.str.eqcasei"},
    {"rewrite.runtime.test_str_eqcasei_invalid_args_return_contracts", test_str_eqcasei_invalid_args_return_contracts, "exclusive", 30000, "libcall.str.eqcasei"},
    {"rewrite.runtime.test_str_replace_returns_expected_results", test_str_replace_returns_expected_results, "exclusive", 30000, "libcall.str.replace"},
    {"rewrite.runtime.test_str_replace_invalid_args_return_nil", test_str_replace_invalid_args_return_nil, "exclusive", 30000, "libcall.str.replace"},
    {"rewrite.runtime.test_str_repeat_returns_expected_results", test_str_repeat_returns_expected_results, "exclusive", 30000, "libcall.str.repeat"},
    {"rewrite.runtime.test_str_repeat_invalid_args_return_nil", test_str_repeat_invalid_args_return_nil, "exclusive", 30000, "libcall.str.repeat"},
    {"rewrite.runtime.test_str_growth_libcalls_enforce_string_limit", test_str_growth_libcalls_enforce_string_limit, "exclusive", 30000, "api.libcall.str"},
    {"rewrite.runtime.test_str_padleft_and_padright_return_expected_results", test_str_padleft_and_padright_return_expected_results, "exclusive", 30000, "libcall.str.padleft,libcall.str.padright"},
    {"rewrite.runtime.test_str_padleft_and_padright_invalid_args_return_nil", test_str_padleft_and_padright_invalid_args_return_nil, "exclusive", 30000, "libcall.str.padleft,libcall.str.padright"},
    {"rewrite.runtime.test_str_libcall_invalidargs_uses_context_itemroot", test_str_libcall_invalidargs_uses_context_itemroot, "exclusive", 30000, "test.runtime.test_str_libcall_invalidargs_uses_context_itemroot"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
