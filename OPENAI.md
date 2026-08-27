# CODEX.md

Platform-specific guidance for OpenAI coding agents working on Sinistra.
Read this file alongside the repository-root `AGENTS.md`.

## Agent roles

Use Sol (Medium) as the root orchestrator. It owns planning, requirements,
architecture, acceptance criteria, direct diff review, audit, integration
decisions, and the final answer. Audit and integration do not imply rerunning
tests that already passed against the unchanged revision.

Use one Luna (High) agent as the sole code writer for a bounded task. Reuse the
same agent for implementation and corrections. Do not use it for repository-wide
discovery or routine test execution.

Use one Luna (Low) agent for targeted repository searches and all test
execution. Keep it read-only. It reports concise findings, exact commands, and
verbatim errors to the root; it does not diagnose failures, propose fixes, or
edit files. Reuse its successful test results as required by `AGENTS.md` rather
than commissioning duplicate final and integration runs.

Keep every handoff isolated and minimal. Do not pass conversation history.
Provide only the bounded question or task, acceptance criteria, relevant paths
and decisions, necessary tree state, and any specific failure the recipient
needs to act on.

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
