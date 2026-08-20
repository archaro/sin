#include "test_framework.h"

void test_pipeline_negative_matrix(void);
void test_pipeline_ast_budget_subprocess(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_pipeline_negative_matrix", test_pipeline_negative_matrix, "exclusive", 30000,
     "api.compiler.lowering,api.compiler.pipeline,baseline.legacy.unified.compiler.test_pipeline_negative_matrix,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence,language.diagnostic.compiler-error,language.diagnostic.overflow,language.diagnostic.parser-error,language.diagnostic.semantic-error,language.diagnostic.source-span,language.diagnostic.unknown-libcall,language.operator.add,language.operator.boolean,language.operator.comparison,language.operator.divide,language.operator.modulo,language.operator.multiply,language.operator.subtract,language.semantic-rule.break-context,language.semantic-rule.call-arity,language.semantic-rule.continue-context,language.semantic-rule.item-name,language.semantic-rule.libcall-resolution,language.semantic-rule.loop-variable,language.token.tunknownchar"},
    {"rewrite.compiler.test_pipeline_ast_budget_subprocess", test_pipeline_ast_budget_subprocess, "exclusive", 30000,
     "api.compiler.pipeline,baseline.legacy.unified.compiler.test_pipeline_ast_budget_subprocess,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
