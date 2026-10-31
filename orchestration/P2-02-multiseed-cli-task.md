# P2-02 deterministic simulation seed selection

Add a bounded, explicit way to select a simulation seed and policy for additional evidence runs. This is test/diagnostic plumbing only; do not tune gameplay, bot policy, cards, waves, enemies, weapons, or passive values.

Acceptance:
- Preserve the existing default fixed-seed reports and the runner's current seed1/seed2 kite plus seed2 stationary invocations.
- Add validated optional seed/policy selection to the existing simulation entry point, with clear rejection of malformed seeds or unsupported policies.
- Keep output schema, deterministic behavior, trace-hash algorithm, and report bounds unchanged.
- Add focused tests for defaults, explicit options, invalid options, and repeatability.
- Windows build, unit tests, native Headless integration, cold starts, and the runner's standard fixed-seed reports pass with the existing three trace hashes unchanged.
- No claim of 1200-second victory or balance acceptance; no extra simulations are required in this task.
