#include "test_framework.h"

void test_sdiss_writer_failure_stops_output(void);
void test_sdiss_summary_writer_failure_propagates(void);
void test_sdiss_summary_writer_failure_preserves_verifier_error(void);
void test_sdiss_cli_reports_output_failure(void);
void test_sdiss_fixture_basic(void);
void test_sdiss_malformed_fixture_reports_verifier_diagnostic(void);
void test_sdiss_reads_compiler_operand_widths(void);
void test_sdiss_lists_and_itemrefs_show_full_operands(void);
void test_sdiss_jump_display_offsets_and_range(void);
void test_sdiss_legacy_and_v1_headers_report_absolute_offsets(void);
void test_sdiss_missing_or_unreadable_input(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_sdiss_writer_failure_stops_output", test_sdiss_writer_failure_stops_output, "exclusive", 30000,
     "api.bytecode.disassembly"},
    {"rewrite.compiler.test_sdiss_summary_writer_failure_propagates", test_sdiss_summary_writer_failure_propagates, "exclusive", 30000,
     "test.compiler.test_sdiss_summary_writer_failure_propagates"},
    {"rewrite.compiler.test_sdiss_summary_writer_failure_preserves_verifier_error", test_sdiss_summary_writer_failure_preserves_verifier_error, "exclusive", 30000,
     "executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_cli_reports_output_failure", test_sdiss_cli_reports_output_failure, "exclusive", 30000,
     "api.entrypoint.sdiss,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_fixture_basic", test_sdiss_fixture_basic, "exclusive", 30000,
     "bytecode.disassembly.mnemonic,bytecode.disassembly.operand,bytecode.disassembly.header,bytecode.disassembly.item-expression,bytecode.disassembly.malformed,bytecode.disassembly.options,api.bytecode.disassembly,api.entrypoint.sdiss,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_malformed_fixture_reports_verifier_diagnostic", test_sdiss_malformed_fixture_reports_verifier_diagnostic, "exclusive", 30000,
     "api.bytecode.disassembly,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_reads_compiler_operand_widths", test_sdiss_reads_compiler_operand_widths, "exclusive", 30000,
     "executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_lists_and_itemrefs_show_full_operands", test_sdiss_lists_and_itemrefs_show_full_operands, "exclusive", 30000,
     "executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_jump_display_offsets_and_range", test_sdiss_jump_display_offsets_and_range, "exclusive", 30000,
     "api.entrypoint.sdiss,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_legacy_and_v1_headers_report_absolute_offsets", test_sdiss_legacy_and_v1_headers_report_absolute_offsets, "exclusive", 30000,
     "api.entrypoint.sdiss,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.compiler.test_sdiss_missing_or_unreadable_input", test_sdiss_missing_or_unreadable_input, "exclusive", 30000,
     "api.entrypoint.sdiss,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
