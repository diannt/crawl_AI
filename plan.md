# AI Companion Refactor: Ollama → `claude -p` Orchestrator

**Created:** 2026-03-19 07:31 PDT
**Total budget:** 60-75 minutes
**Time tracking:** Each Phase_{X}.md has start/end timestamps

## Architecture Overview

Replace the Ollama HTTP client (`ai_client.h`) with a C++ subprocess orchestrator
that calls `claude -p "<prompt>"` from the repo root. Qdrant/Ollama code archived
(not deleted) behind `#ifdef QDRANT_ENABLED`.

### Key Files
- `crawl-ref/source/ai_client.h` — Current Ollama/Qdrant HTTP client (to archive)
- `crawl-ref/source/ai_companion.h` — Companion pipeline (to rewire)
- `crawl-ref/source/ai_companion.cc` — Engine-linked dispatch (to extend)
- `crawl-ref/source/ai_screenshot.h` — Screenshot parser (used in Phase II)
- `crawl-ref/source/ai_rewards.h` — Rewards/XP (unchanged)
- `crawl-ref/source/mon-speak.cc:356` — Integration hook
- **NEW:** `crawl-ref/source/claude_orchestrator.h` — `claude -p` subprocess wrapper

### Execution Rules
- `claude -p` MUST run from WORK_FOLDER (repo root: `/home/maatru/dev/code_pref/model_a/`)
- Single-message prompts only
- Prompts must be verified to execute and return output

---

## Phase I — Backend Refactor (12-15 min)
**Goal:** Replace Ollama HTTP with `claude -p` subprocess calls

1. Create `claude_orchestrator.h` with `ClaudeOrchestrator` class
   - `query(prompt) → string` via `popen("claude -p '...' 2>/dev/null")`
   - Working directory set to repo root
   - Timeout handling (30s default)
2. Archive Ollama/Qdrant in `ai_client.h` behind `#ifdef QDRANT_ENABLED`
3. Rewire `run_companion_pipeline()` to use `ClaudeOrchestrator::query()`
4. Embed static lore excerpt in system prompt (replaces RAG)
5. **Test:** Verify `claude -p` prompts go through and return valid responses

## Phase II — On-Screen Directional Commands (12-15 min)
**Goal:** Companion outputs movement/attack directions based on environment

1. Extend prompt to include parsed screen state (from `ai_screenshot.h`)
2. Add `"direction"` field to Claude output schema: `N/S/E/W/NE/NW/SE/SW/ATTACK_<target>`
3. New `dispatch_movement()` — translates direction → DCSS key commands
4. Replace auto-explore with environment-driven directional decisions
5. **Test:** Companion moves/attacks based on Claude's directional output

## Phase III — Conversational Context (10-12 min)
**Goal:** Claude outputs flavor commentary about screen context

1. Secondary `claude -p` prompt for conversational text
2. Prompt includes: visible monsters, quests, items, player HP, dungeon level
3. Output: `"commentary"` field (1-2 sentences)
4. Display via `mprf()` in message log
5. **Test:** Commentary appears in game log, contextually relevant

## Phase IV — Message Log + Player Commands (10-12 min)
**Goal:** Two-way communication: "Companion says:" format + player input

1. Format all companion text as `"Companion says: <text>"` in DCSS messages
2. Detect player commands: `@companion <msg>` or `/c <msg>`
3. Route player text to `claude -p` with full conversation context
4. New `player_to_companion()` function in `ai_companion.cc`
5. **Test:** Player can write to companion, companion responds in log

## Phase V — Async I/O + Parallel + World Tree (12-15 min)
**Goal:** Non-blocking subprocess, parallel commands, context preservation

1. Replace `popen()` with `fork()`+`pipe()` + non-blocking `poll()`
2. `AsyncClaudeOrchestrator` manages multiple in-flight processes
3. World tree: rolling context buffer of recent decisions/observations
4. Companion maintains context across turns (prompt window)
5. **Test:** Multiple `claude -p` calls in flight, no terminal blocking
