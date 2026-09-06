#include "test_framework.h"

void test_time_year_registry_contract(void);
void test_time_year_utc_calendar_boundaries(void);
void test_time_year_negative_millisecond_flooring(void);
void test_time_calendar_components_are_utc_integers(void);
void test_time_year_rejects_invalid_type_and_publishes_error(void);
void test_time_year_unrepresentable_timestamp_publishes_error(void);
void test_time_year_source_integration_and_arity(void);
void test_time_calendar_source_integration_and_arity(void);
void test_time_formatted_utc_boundaries_and_negative_flooring(void);
void test_time_fulldate_ordinal_suffixes_and_month_names(void);
void test_time_formatted_rejects_invalid_types_and_publishes_details(void);
void test_time_formatted_unrepresentable_timestamp_publishes_error(void);
void test_time_formatted_source_integration_and_arity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_time_year_registry_contract", test_time_year_registry_contract, "exclusive", 30000, "api.libcall.time,api.libcall.table,libcall.time.year,libcall.time.month,libcall.time.day,libcall.time.hour,libcall.time.minute,libcall.time.second,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_year_utc_calendar_boundaries", test_time_year_utc_calendar_boundaries, "exclusive", 30000, "api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_year_negative_millisecond_flooring", test_time_year_negative_millisecond_flooring, "exclusive", 30000, "api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_calendar_components_are_utc_integers", test_time_calendar_components_are_utc_integers, "exclusive", 30000, "api.libcall.time,libcall.time.month,libcall.time.day,libcall.time.hour,libcall.time.minute,libcall.time.second"},
    {"rewrite.runtime.test_time_year_rejects_invalid_type_and_publishes_error", test_time_year_rejects_invalid_type_and_publishes_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_year_unrepresentable_timestamp_publishes_error", test_time_year_unrepresentable_timestamp_publishes_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_year_source_integration_and_arity", test_time_year_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.year"},
    {"rewrite.runtime.test_time_calendar_source_integration_and_arity", test_time_calendar_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.month,libcall.time.day,libcall.time.hour,libcall.time.minute,libcall.time.second"},
    {"rewrite.runtime.test_time_formatted_utc_boundaries_and_negative_flooring", test_time_formatted_utc_boundaries_and_negative_flooring, "exclusive", 30000, "api.libcall.time,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_fulldate_ordinal_suffixes_and_month_names", test_time_fulldate_ordinal_suffixes_and_month_names, "exclusive", 30000, "api.libcall.time,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_formatted_rejects_invalid_types_and_publishes_details", test_time_formatted_rejects_invalid_types_and_publishes_details, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_formatted_unrepresentable_timestamp_publishes_error", test_time_formatted_unrepresentable_timestamp_publishes_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.timestamp"},
    {"rewrite.runtime.test_time_formatted_source_integration_and_arity", test_time_formatted_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
