# OPENAI.md
## Special instructions for OpenAI models

Platform-specific guidance for OpenAI coding agents working on Sinistra.
Read this file alongside the repository-root `AGENTS.md`.

## Agent roles

- Terra (Medium) is the orchestrator.
- Use the Astra (Medium) agent to create work-packages for the less-capable
  models.
- When Astra has produced bounded work-packages, the orchestrator hands them
  to Luna (High) agent for writing code.
- Tests are run with the Luna (Low) agent and relevant output from the test is
  fed back to the calling agent.
- Astra is called again by the orchestrator to review the work done against the
  work-package, and to recommend changes where appropriate and necessary.

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
