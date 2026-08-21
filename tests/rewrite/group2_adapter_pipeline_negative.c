#include "test_framework.h"

void test_pipeline_negative_matrix(void);
void test_pipeline_ast_budget_subprocess(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_pipeline_negative_matrix", test_pipeline_negative_matrix, "exclusive", 30000,
     "language.token.tunknownchar,language.operator.add,language.operator.subtract,language.operator.multiply,language.operator.divide,language.operator.modulo,language.operator.comparison,language.operator.boolean,language.semantic-rule.break-context,language.semantic-rule.continue-context,language.semantic-rule.call-arity,language.semantic-rule.libcall-resolution,language.semantic-rule.item-name,language.semantic-rule.loop-variable,language.diagnostic.parser-error,language.diagnostic.semantic-error,language.diagnostic.compiler-error,language.diagnostic.source-span,language.diagnostic.overflow,language.diagnostic.unknown-libcall,api.compiler.lowering,api.compiler.pipeline,executable.scomp.command-line,executable.scomp.input-output,executable.scomp.exit-status,executable.scomp.persistence,executable.scomp.errors"},
    {"rewrite.compiler.test_pipeline_ast_budget_subprocess", test_pipeline_ast_budget_subprocess, "exclusive", 30000,
     "api.compiler.pipeline,executable.scomp.command-line,executable.scomp.input-output,executable.scomp.exit-status,executable.scomp.persistence,executable.scomp.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
