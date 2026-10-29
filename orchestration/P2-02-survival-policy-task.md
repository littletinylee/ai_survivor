Task: improve the deterministic kite policy's survival through upgrade-card choices, using current real reports as evidence.

Baseline on branch codex/enemy-variety with unchanged data/characters/deepseek_big_fish.json:
- seed 1 kite: Lost at 615.683345647s, 2077 kills, level 71, 70 cards, 3 weapons.
- seed 2 kite: Lost at 623.033345794s, 2132 kills, level 72, 71 cards, 3 weapons.
- seed 2 stationary: Lost at 25.133333836s; preserve this weak-policy baseline.

Implement a narrowly scoped, deterministic improvement to KiteBot card ranking in src/core/run_simulation.cpp. Prefer defensive/health choices when sustained incoming damage makes survival the limiting factor; retain valid new-weapon acquisition and useful weapon upgrades, and do not change combat stats, enemy curves, character JSON, simulation time, or terminal state. Add a meaningful GoogleTest regression that checks the intended ranking behavior and exercises at least one full real-character fixed-seed run without fixture overrides. Do not assert victory unless the unchanged real JSON character actually reaches 1200 seconds. Preserve stationary behavior and existing weapon/capacity/frozen-time invariants. Do not edit orchestration state files.

Acceptance: focused regression passes; seed 1 and 2 deterministic reports improve over baseline survival time without errors, invalid selections, caps, or invariant violations; no claim of 1200-second balance unless observed. Keep changes small and report exact files and test commands. File edits only; Codex runs build/tests/Headless afterward.
