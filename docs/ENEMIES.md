# Enemies

| Kind | Radius | HP | Speed | Contact | XP | Behaviour |
|---|---|---|---|---|---|---|
| Chaser (coral circle) | 18 (config) | 1x | 1x | 1x | 1x | walks at the player |
| Charger (amber arrow) | 14 | 0.75x | 1.1x | 1.3x | 1.25x | see below |
| Brute (purple block) | 28 | 3x | 0.55x | 1.6x | 2.4x | chases, bulky |

Multipliers apply to the time-scaled chaser stats once, in `WaveDirector`.
Chargers appear from 45 combat seconds (~20% of spawns), brutes from 90 s
(~15%, chargers ~20%). Before 45 s the RNG stream is unchanged. Spawn interval
and count curve are unchanged. Hand-spawned enemies use their `EnemySpawn`
stats verbatim; `kind` only selects behaviour, `radius` 0 means config radius.

Charger cycle: approach; within 220 units (and rearmed) a 0.7 s stationary
windup locks the aim at the player; 0.3 s dash at 3x speed along that vector
(clamped to the arena); 0.8 s stationary recovery; then at least 1 s of
approach before the next windup. State is only advanced by
`CombatWorld::tick` while Playing; per-call delta is capped at 0.25 s.
Rendering reads `phase`, `phase_remaining` and `aim` and never mutates them.
`--visual-enemies` freezes a fixture mid-windup for screenshots.

Local Windows validation:117 GoogleTest, native phase/texture integration and2 fresh cold starts PASS; screenshot reviewed. Normal JSON character remains Lost around616/623seconds for kite seeds1/2; no20minute balance claim. New tests account for time already consumed on windup entry and verify exact dash/recovery/rearm durations. Cloud CI still pending.
