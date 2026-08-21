#include "test_framework.h"

/* Keep the complete localhost orchestration and cleanup from the chat smoke
 * test body; the framework descriptor runs that flow in its isolated child. */
#define SIN_CHAT_SMOKE_FRAMEWORK 1
#define main chat_smoke_test_main
#include "../../tests/network/test_chat_smoke.c"
#undef main
#undef SIN_CHAT_SMOKE_FRAMEWORK

static void test_chat_smoke_flow(void) {
  (void)chat_smoke_test_main();
}

static const TF_TestDescriptor tests[] = {
    {"rewrite.chat_smoke.examples_chat_flow", test_chat_smoke_flow,
     "exclusive,network", 120000,
     "executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
