# Phase 5: Async I/O + Parallel Commands + World Tree + Full Engine Integration

**Started:** 2026-03-19 08:04 PDT
**Completed:** 2026-03-19 09:05 PDT
**Duration:** ~61 minutes (includes engine build fix + live gameplay testing)

## Planned Actions
1. Replace popen() with fork()+pipe() + non-blocking poll()
2. AsyncClaudeOrchestrator manages multiple in-flight processes
3. World tree: rolling context buffer of recent decisions/observations
4. Companion preserves context across turns via prompt window
5. Test: parallel claude -p calls, verify no terminal blocking
6. Fix DCSS engine build (struct monster namespace issue)
7. Full in-engine gameplay verification with live claude -p
8. Wire player-to-companion chat into DCSS note input system

## Status
- [x] AsyncClaudeOrchestrator created (async_claude_orchestrator.h)
- [x] Non-blocking pipe reads via fork()+pipe()+poll()+fcntl(O_NONBLOCK)
- [x] World tree context buffer (deque<WorldTreeEntry>, max 20 turns)
- [x] Parallel pipeline (action + commentary concurrent via launch/wait_for)
- [x] Live async test passes (test_async_live.cc)
- [x] DCSS engine build fixed (struct monster namespace resolution)
- [x] Full in-engine gameplay verification with real claude -p
- [x] Player-to-companion chat hooked into notes.cc (:  then /c or @companion)

## Files Changed
- **NEW** `crawl-ref/source/async_claude_orchestrator.h`:
  - `AsyncClaudeOrchestrator`: singleton managing parallel claude -p via fork()+pipe()
  - `AsyncProcess` struct: pid, read_fd, output buffer, tag, finished status
  - `launch()`, `poll_completions()`, `wait_for()`, `get_json()`, `cleanup()`
  - Non-blocking reads via `fcntl(O_NONBLOCK)` + `poll()`
  - `WorldTree` class: rolling `deque<WorldTreeEntry>` (max 20 entries)
  - `build_context_summary()` for injecting recent decisions into prompts
  - Test mode via `CLAUDE_ORCH_TEST` define
- **NEW** `crawl-ref/source/test_async_live.cc`:
  - Standalone binary exercising REAL parallel claude -p calls
  - Launches action + commentary simultaneously, verifies parallel completion
- **MODIFIED** `crawl-ref/source/ai_companion.h`:
  - Moved `struct monster;` forward declaration to global scope (before namespace)
  - Changed production `dispatch_companion_movement()` to use `::monster*`
- **MODIFIED** `crawl-ref/source/ai_companion.cc`:
  - Changed `dispatch_companion_movement()` signature to `::monster*`
- **MODIFIED** `crawl-ref/source/notes.cc`:
  - Added `#include "ai_companion.h"`
  - `make_user_note()` intercepts `/c` and `@companion` prefixed input
  - Routes intercepted input to `ai_companion::player_to_companion()`
- **NEW** `crawl-ref/source/test_screenshots/`:
  - 17 TUI text captures from live gameplay sessions
  - PNG screenshots from automated xterm capture runs
  - AI companion stderr logs (149 lines of debug output)

## Engine Build Fix
The DCSS build failed because `struct monster;` was forward-declared inside
`namespace ai_companion` (line 166 of ai_companion.h), creating type
`ai_companion::monster` instead of the global `::monster` from `monster.h`.

**Fix**: Moved forward declaration before the namespace, used `::monster*` in
production function signatures. Build succeeds, all .o files compile, crawl
binary links (19MB).

## Live Gameplay Test Results (real claude -p, in-engine)

### Game 1: Ogre Combat (wizard-spawned)
```
Companion says: That ogre hits like a cave-in — let me engage it first
  with my buckler raised while you flank from the side.
Companion says: An ogre lurks nearby — those brutes hit hard enough to
  fell the unwary in just a few blows.
Cuahtemoc barely misses the ogre.
Cuahtemoc hits the ogre but does no damage.
Action: WARN_THREAT, Direction: ATTACK_ogre
Movement: mons->target set to ogre position (78,35)
```

### Game 2: Natural Exploration + Hobgoblin Encounter
```
Exploration (no enemies):
  Action: CHAT, Direction: E/S (varies by environment)
  "The air stirs faintly from the south — I sense open passages that way."
  "The halls are quiet for now, a good time to get our bearings."

Combat (hobgoblin encountered naturally):
  Action: WARN_THREAT, Direction: ATTACK_hobgoblin
  "A mere hobgoblin dares challenge our line? Stand firm — I'll engage it directly."
  Kaya hits the hobgoblin. (ancestor fights alongside player)
  Hobgoblin killed. XP shared.
```

### Game 3: Player-to-Companion Chat
```
Player: /c Hello ancestor! What do you think of this dungeon?
Companion says: Greetings, descendant. These halls echo with memories
  older than stone — tread carefully, for the dungeon tests the unwary.

Player: /c Should we go north or south?
Companion says: The southern passage carries an older draft — let us
  head south, descendant.

Player: @companion What should I do if we encounter a tough monster?
Companion says: Trust in our bloodline's strength — quaff a potion of
  might to bolster your blows, and if the beast still overwhelms us,
  read a scroll of teleportation rather than die a fool's death.
```

## Performance
- Single claude -p call: 7-10 seconds
- Two calls per turn (action + commentary): ~8-10s parallel (vs ~15-20s sequential)
- 35+ claude -p calls during live gameplay session
- Average response time: ~8.5ms per call
- Zero crashes during gameplay (one graceful fallback on claude -p exit status 1)

## Phase 5 Summary
All 5 planned features implemented and verified in-engine with real claude -p
subprocess calls. The AI companion:
1. Speaks contextually during exploration and combat
2. Moves the ancestor monster toward threats or exploration directions
3. Provides separate commentary about the environment
4. Responds to player questions via /c and @companion commands
5. Handles claude -p failures gracefully with fallback responses
