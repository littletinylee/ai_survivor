# Cold-start import mitigation

`tools/test.ps1` passes `--frame-delay 1000` to every `--editor --import` run and adds cold-start fixtures.

## Problem

A fresh native-extension project (no `.godot` cache) run with `--headless --editor --import --quit` crashed at shutdown with `0xc0000005`. A second import passed, and a plain Node project cold import passed. Switching between the stable SDK and pinned master, and the reloadable flag, did not change the crash.

Upstream report: <https://github.com/godotengine/godot/issues/111048>. It describes a deferred `EditorHelp::_gen_extensions_docs` call that runs after doc cleanup when the editor shuts down quickly, and documents `--frame-delay 800` with `--import` as a way to avoid the race.

## Mitigation

This is a mitigation for an editor early-shutdown race. It does not hide the crash and does not change gameplay. The delay only gives deferred editor work time to finish. Exit codes, timeouts and `ERROR`/`SCRIPT ERROR`/`FATAL` output are still checked, and the first failure fails the test with no retry. Each invocation has a 60 s cap.

Control test (Codex, fresh `build/deferred-shutdown-check`): `--headless --editor --import --frame-delay 1000 --quit` exited 0 with no errors.

This does not claim the engine bug is fixed. Engine and dependency pins are unchanged.

## Regression

After the root unit, import and integration checks, `tools/test.ps1 -ColdStartCount N` (1-3, default 2) creates N new `build/cold-start-<guid>` fixtures. Fixtures are never deleted. Only these are copied: `project.godot`, `ai_survivors.gdextension`, `scenes/`, `data/`, `assets/` (if present) and the current Windows debug extension DLL from `bin/`. `.git`, `.godot`, `.tools`, `orchestration`, tests and credentials are not copied, and `.godot` is never written manually. Each fixture runs `cold-import-<i>` and then `cold-game-<i>` (integration marker required).
