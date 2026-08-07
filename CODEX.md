# OPENAI.md

Platform-specific guidance for OpenAI coding agents working on Sinistra.
Read this file alongside the repository-root `AGENTS.md`.

## Special instructions for OpenAI models

In Code Mode, within each bounded stage:

- Run independent, functions.exec-available tool calls concurrently in one
  functions.exec call.
- Use `await Promise.allSettled([...])` when partial results are useful, and
  inspect every result; use `await Promise.all([...])` only when any failure
  should abort the batch.
- Keep dependencies, waits/resumes, approvals, conflicting or interdependent
  mutations, and adaptive investigations where each result may change the next
  step sequential.
- Do not split otherwise batchable inspections across outer tool calls.