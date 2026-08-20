#include "test_framework.h"

/* Keep the legacy executable's complete localhost orchestration and cleanup;
 * the framework descriptor runs that same flow in its isolated child. */
#define SIN_CHAT_SMOKE_FRAMEWORK 1
#define main legacy_chat_smoke_main
#include "../../tests/network/test_chat_smoke.c"
#undef main
#undef SIN_CHAT_SMOKE_FRAMEWORK

static void test_chat_smoke_flow(void) {
  (void)legacy_chat_smoke_main();
}

static const TF_TestDescriptor tests[] = {
    {"rewrite.chat_smoke.examples_chat_flow", test_chat_smoke_flow,
     "exclusive,network", 120000,
     "baseline.legacy.chat_smoke.examples_chat_flow,executable.sin.command-line,executable.sin.errors,executable.sin.exit-status,executable.sin.input-output"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
