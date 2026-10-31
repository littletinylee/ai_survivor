# DeepSeek fallback channel

## 2026-11-01 current handoff

Claude official returned403 verification_required before edits. Its call was stopped and not retried through another Claude provider. The already-approved pickup-aware task was handed once to DeepSeek Harness in a supervised600s file-only call; CLI exit0 and only read/grep/edit tools observed. Requested model deepseek-flash; runtime events did not independently report a model ID. No user settings or security changes were made.

Candidate172 tests/native2cold passed but ten-seed acceptance failed (median596.29s,4/10 non-regressed). Patch and reports are preserved ignored locally, gameplay changes removed. Baseline168 tests/native2cold restored. No further paid task or unattended batch is being started; cost unknown. Claude account verification is pending, while DeepSeek file-edit capability itself worked. Historical note below remains a record of the original task, not the current edit scope.

## Historical bootstrap handoff

Date: 2026-10-29. Workspace: `E:/AI Survior/ai_survivor`. Branch: `feat/phase0-bootstrap`.

## Model routing (actual)

- Controller: Codex (current session; exact model not independently verified).
- Primary implementer: Claude Code, requested `claude-haiku-5-5` — BLOCKED by provider HTTP 403 (Terms Of Service). Do not retry.
- Fallback implementer: DeepSeek Harness, model `deepseek-flash` — probe READY, used for this bounded task.
- No other paid model calls were made for this patch. Cost ledger remains unavailable (`unknown`); unattended loops stay off.

## File-only implementation mode

- DeepSeek runs with direct filesystem read/edit tools only. No `pwsh`/bash, subprocesses, workflows, subagents, commits, pushes, or settings/security edits. The known shell sandbox ACL failures make command execution unreliable and it is out of scope here.
- One writer at a time: Codex is read-only while this channel is active. DeepSeek stops immediately after its edits; validation and any follow-up edits return to Codex.
- Scope is frozen and bounded: one implementation attempt, only the assigned files (`tools/test.ps1`, this note). No engine, build, CI, or gameplay changes.

## Patch intent

`tools/test.ps1` now launches Godot with `System.Diagnostics.ProcessStartInfo` + `Process`:

- `ArgumentList` passes each argument separately; no manual quote joining.
- `UseShellExecute=false`, `RedirectStandardOutput/Error=true`, `CreateNoWindow=true`.
- `ReadToEndAsync` starts on both pipes before waiting.
- 60-second `WaitForExit`; on timeout `Kill(true)`, drain both pipes, save logs, then fail.
- Existing exit-code, `(ERROR|SCRIPT ERROR|FATAL):` and `AI_SURVIVORS_INTEGRATION_OK` checks are preserved.

This targets cross-platform launch and stream-capture ambiguity only. It does not claim to fix the Linux engine/integration crash.

## Codex test handoff

1. Review the `Invoke-GodotCheck` diff for argument/text/exit-code fidelity.
2. Run `tools/test.ps1` on Windows (pwsh) and the Linux CI workflow.
3. Compare `build/test-results/import.*.log` and `integration.*.log`; confirm both pipes are captured, timeout kills the tree, and logs are written.
4. Keep P0-01 accepted only after build, GoogleTest, and Headless integration pass. Record any remaining Linux failure as engine/CI evidence, not a launch-script regression unless the logs prove otherwise.

No commit or push was made by DeepSeek; working-tree changes are left for Codex review.
