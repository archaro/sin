#include "test_framework.h"

void test_time_year_registry_contract(void);
void test_time_weekday_utc_all_days_and_boundaries(void);
void test_time_weekday_is_utc_under_nonutc_timezone(void);
void test_time_weekday_rejects_invalid_types_and_preserves_error(void);
void test_time_weekday_conversion_failure_is_undefined(void);
void test_time_weekday_extreme_timestamp_follows_host_support(void);
void test_time_weekday_source_integration_and_arity(void);
void test_time_year_utc_calendar_boundaries(void);
void test_time_year_negative_millisecond_flooring(void);
void test_time_calendar_components_are_utc_integers(void);
void test_time_year_rejects_invalid_type_and_publishes_error(void);
void test_time_year_unrepresentable_timestamp_publishes_error(void);
void test_time_year_source_integration_and_arity(void);
void test_time_calendar_source_integration_and_arity(void);
void test_time_formatted_utc_boundaries_and_negative_flooring(void);
void test_time_formatted_year_boundaries(void);
void test_time_fulldate_ordinal_suffixes_and_month_names(void);
void test_time_formatted_rejects_invalid_types_and_publishes_details(void);
void test_time_formatted_unrepresentable_timestamp_publishes_error(void);
void test_time_formatted_success_preserves_existing_error(void);
void test_time_formatted_source_integration_and_arity(void);
void test_time_make_utc_calendar_and_epoch_vectors(void);
void test_time_make_validates_all_components_and_consumes_owned_values(void);
void test_time_make_overflow_error_priority_and_preserved_error(void);
void test_time_make_source_integration_and_arity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_time_year_registry_contract", test_time_year_registry_contract, "exclusive", 30000, "api.libcall.time,api.libcall.table,libcall.time.year,libcall.time.month,libcall.time.day,libcall.time.hour,libcall.time.minute,libcall.time.second,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate,libcall.time.weekday"},
    {"rewrite.runtime.test_time_weekday_utc_all_days_and_boundaries", test_time_weekday_utc_all_days_and_boundaries, "exclusive", 30000, "api.libcall.time,libcall.time.weekday"},
    {"rewrite.runtime.test_time_weekday_is_utc_under_nonutc_timezone", test_time_weekday_is_utc_under_nonutc_timezone, "exclusive", 30000, "api.libcall.time,libcall.time.weekday"},
    {"rewrite.runtime.test_time_weekday_rejects_invalid_types_and_preserves_error", test_time_weekday_rejects_invalid_types_and_preserves_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.weekday"},
    {"rewrite.runtime.test_time_weekday_conversion_failure_is_undefined", test_time_weekday_conversion_failure_is_undefined, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.weekday"},
    {"rewrite.runtime.test_time_weekday_extreme_timestamp_follows_host_support", test_time_weekday_extreme_timestamp_follows_host_support, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.weekday"},
    {"rewrite.runtime.test_time_weekday_source_integration_and_arity", test_time_weekday_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.weekday"},
    {"rewrite.runtime.test_time_year_utc_calendar_boundaries", test_time_year_utc_calendar_boundaries, "exclusive", 30000, "api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_year_negative_millisecond_flooring", test_time_year_negative_millisecond_flooring, "exclusive", 30000, "api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_calendar_components_are_utc_integers", test_time_calendar_components_are_utc_integers, "exclusive", 30000, "api.libcall.time,libcall.time.month,libcall.time.day,libcall.time.hour,libcall.time.minute,libcall.time.second"},
    {"rewrite.runtime.test_time_year_rejects_invalid_type_and_publishes_error", test_time_year_rejects_invalid_type_and_publishes_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_year_unrepresentable_timestamp_publishes_error", test_time_year_unrepresentable_timestamp_publishes_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.year"},
    {"rewrite.runtime.test_time_year_source_integration_and_arity", test_time_year_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.year"},
    {"rewrite.runtime.test_time_calendar_source_integration_and_arity", test_time_calendar_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.month,libcall.time.day,libcall.time.hour,libcall.time.minute,libcall.time.second"},
    {"rewrite.runtime.test_time_formatted_utc_boundaries_and_negative_flooring", test_time_formatted_utc_boundaries_and_negative_flooring, "exclusive", 30000, "api.libcall.time,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_formatted_year_boundaries", test_time_formatted_year_boundaries, "exclusive", 30000, "api.libcall.time,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_fulldate_ordinal_suffixes_and_month_names", test_time_fulldate_ordinal_suffixes_and_month_names, "exclusive", 30000, "api.libcall.time,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_formatted_rejects_invalid_types_and_publishes_details", test_time_formatted_rejects_invalid_types_and_publishes_details, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_formatted_unrepresentable_timestamp_publishes_error", test_time_formatted_unrepresentable_timestamp_publishes_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.timestamp"},
    {"rewrite.runtime.test_time_formatted_success_preserves_existing_error", test_time_formatted_success_preserves_existing_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.timestamp"},
    {"rewrite.runtime.test_time_formatted_source_integration_and_arity", test_time_formatted_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.timestamp,libcall.time.time,libcall.time.date,libcall.time.fulldate"},
    {"rewrite.runtime.test_time_make_utc_calendar_and_epoch_vectors", test_time_make_utc_calendar_and_epoch_vectors, "exclusive", 30000, "api.libcall.time,libcall.time.make"},
    {"rewrite.runtime.test_time_make_validates_all_components_and_consumes_owned_values", test_time_make_validates_all_components_and_consumes_owned_values, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.make"},
    {"rewrite.runtime.test_time_make_overflow_error_priority_and_preserved_error", test_time_make_overflow_error_priority_and_preserved_error, "exclusive", 30000, "api.common.errors,api.libcall.time,libcall.time.make"},
    {"rewrite.runtime.test_time_make_source_integration_and_arity", test_time_make_source_integration_and_arity, "exclusive", 30000, "api.libcall.time,language.token.tlibname,libcall.time.make"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
