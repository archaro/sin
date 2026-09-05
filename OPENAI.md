# OPENAI.md
## Special instructions for OpenAI models

Platform-specific guidance for OpenAI coding agents working on Sinistra.
Read this file alongside the repository-root `AGENTS.md`.

## Agent roles

Orchestration Agent: Terra (Medium)
Planning, work-packaging, and critical review agent: Astra (High)
Code-writing agent: Luna (High)
Compilation and test-execution agent: Luna (Low)
All other work should use the same agent as for code-writing.

Disable history forking and provide a self-contained prompt with only the
bounded task context.

Keep every handoff isolated and minimal. Do not pass conversation history.
Provide only the bounded question or task, acceptance criteria, relevant paths
and decisions, necessary tree state, and any specific failure the recipient
needs to act on.

Within each bounded stage:
- Run independent, functions.exec-available tool calls concurrently in one
  functions.exec call.
- Use `await Promise.allSettled([...])` when partial results are useful, and
  inspect every result; use `await Promise.all([...])` only when any failure
  should abort the batch.
- Keep dependencies, waits/resumes, approvals, conflicting or interdependent
  mutations, and adaptive investigations where each result may change the next
  step sequential.
- Do not split otherwise batchable inspections across outer tool calls.
