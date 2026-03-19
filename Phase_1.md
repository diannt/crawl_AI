# Phase 1: Backend Refactor — Ollama → `claude -p`

**Started:** 2026-03-19 07:31 PDT
**Completed:** 2026-03-19 07:35 PDT
**Duration:** ~4 minutes

## Planned Actions
1. Create `claude_orchestrator.h` — C++ class wrapping `claude -p` via `popen()`
2. Archive Ollama/Qdrant code behind `#ifdef QDRANT_ENABLED` in `ai_client.h`
3. Rewire `run_companion_pipeline()` in `ai_companion.h` to use new orchestrator
4. Embed static lore context in system prompt
5. Test: verify `claude -p` prompts execute from repo root and return valid output

## Status
- [x] claude_orchestrator.h created
- [x] ai_client.h archived (Ollama/Qdrant behind `#ifdef QDRANT_ENABLED`)
- [x] ai_companion.h rewired (pipeline uses ClaudeOrchestrator directly)
- [x] Prompts verified working

## Files Changed
- **NEW** `crawl-ref/source/claude_orchestrator.h` — ClaudeOrchestrator class with:
  - `query(prompt)` → raw string via `popen("claude -p '...' 2>/dev/null")`
  - `query_json(prompt)` → parsed JSON with fallback handling
  - Shell escaping for single quotes
  - JSON extraction from mixed text responses
  - Test mode via `CLAUDE_ORCH_TEST` define
- **MODIFIED** `crawl-ref/source/ai_client.h` — Two-path architecture:
  - Default: `AIClient` wraps `ClaudeOrchestrator` (Embed/SearchLore become no-ops)
  - `#ifdef QDRANT_ENABLED`: Full Ollama+Qdrant pipeline preserved intact
- **MODIFIED** `crawl-ref/source/ai_companion.h` — Pipeline simplified:
  - Removed embed→RAG→Ollama steps
  - Direct: build_prompt → ClaudeOrchestrator::query_json → parse → dispatch

## Testing Results
### Test 1: Basic prompt
```
Input: "Say hello to the adventurer"
Output: {"chat":"Greetings, adventurer!...","action":"CHAT","payload":""}
Result: PASS — Valid JSON, correct schema
```

### Test 2: Game state with threats
```
Input: Game state with HP=25/100, visible_threats=["orc warrior","troll"]
Output: {"chat":"You are grievously wounded...Fall back...","action":"WARN_THREAT","payload":""}
Result: PASS — Correct action (WARN_THREAT), contextually aware response
```

## Notes
- `claude -p` executes from repo root as required
- Response time ~3-5s per prompt (vs ~1-2s for local Ollama)
- JSON parsing handles claude's markdown code fences via extract_json()
- Lore context embedded as static string in claude_orchestrator.h
