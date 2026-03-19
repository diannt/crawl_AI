# Phase 2: On-Screen Companion with Directional Commands

**Started:** 2026-03-19 07:37 PDT
**Completed:** 2026-03-19 07:40 PDT
**Duration:** ~3 minutes

## Planned Actions
1. Extend `claude -p` prompt schema to include `direction` field
2. Add screen state (from ai_screenshot.h parser) to the prompt
3. Create `dispatch_movement()` — translates direction → DCSS key commands
4. Companion makes environment-driven decisions: attack targets, move directionally
5. Test: verify directional output from claude -p given screen state

## Status
- [x] Prompt schema extended with direction field
- [x] Screen state included in prompts (build_screen_prompt() + build_prompt() updated)
- [x] direction_to_key() + is_attack_direction() + attack_target() created
- [x] Directional commands verified via claude -p tests

## Files Changed
- **MODIFIED** `crawl-ref/source/ai_companion.h`:
  - `build_prompt()` — added `direction` field to JSON schema + DIRECTION RULES
  - NEW `build_screen_prompt()` — includes ASCII map dump in prompt
  - `CompanionResponse` — added `direction` field
  - NEW `direction_to_key()` — maps N/S/E/W to vi-keys (h/j/k/l/y/u/b/n)
  - NEW `is_attack_direction()` / `attack_target()` — parse ATTACK_<target>
  - `run_companion_pipeline()` — parses direction from claude response
  - NEW `run_screen_pipeline()` — screen-aware variant with ASCII map
- **MODIFIED** `crawl-ref/source/mon-speak.cc` — Updated integration to:
  - Request directional decisions from claude -p
  - Log direction choices to stderr for observation

## Testing Results
### Test 1: Combat scenario (orc + goblin visible)
```
Input: ASCII map with O (orc) and g (goblin), HP=80/100
Output: {"direction":"ATTACK_orc","action":"ATTACK","chat":"The orc is the greater threat..."}
Result: PASS — Correct tactical decision, attacks stronger enemy
```

### Test 2: Exploration scenario (no enemies)
```
Input: ASCII map with open passage to south, HP=100/100
Output: {"direction":"S","action":"CHAT","chat":"I see a passage opening to the south..."}
Result: PASS — Correct spatial reasoning, moves toward unexplored area
```

## Notes
- Direction output is strictly environment-driven (not auto-explore)
- Claude analyzes ASCII map for spatial reasoning
- ATTACK_<target> format enables targeted combat decisions
- vi-key mapping matches DCSS defaults (h/j/k/l for cardinal directions)
