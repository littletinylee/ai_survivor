# Native presentation for 吃白饭

File edits only src/gameplay/ai_survivors_bootstrap.h/.cpp. Do not run shell or modify core/JSON/orchestration. Core implementation now exposes progression.passive(): active(), progress(), threshold(), proc_count(). Default character passive_id free_rice, display name 吃白饭.100 actual collected XP ->3% current maxHP heal; full health consumes procs. Existing native and simulation share advance_run; do not create any gameplay logic in scene code.

In populate_character_buttons append c.passive_description to the existing name/description/stats text, retaining icon, auto-wrap and VBox scrolling. Increase minimum row height enough for this Chinese rule at600px width (around200px, verify structurally). Do not clip other characters or hardcode only one row.

Add a small battle HUD label to the right of the existing XP label (roughlyx740,y48,width340,height24,font16). When passive.active(), show 吃白饭 plus current progress/threshold and proc count; for no passive hide/empty it. Read runtime getters only; never accumulate XP or heal in UI. Keep pause/upgrade overlays, weapons footer and main HP/time unchanged. No new visual fixture logic required; existing --visual-select and --visual-battle can be used by Codex afterward.

Report changed files and layout assumptions. Host will build/test and inspect actual screenshots before acceptance.
