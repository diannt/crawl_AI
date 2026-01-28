/**
 * @file test_combined.cc
 * @brief Combined test entry point — links Phase 1 + Phase 2 + Phase 3 tests
 *        into a single binary. Future phases add their test files here.
 *
 * This file defines CATCH_CONFIG_MAIN so individual test files must NOT define it.
 * Each test file is included directly (they are self-contained with no ODR conflicts
 * because all Phase headers use inline/static functions).
 *
 * Compile (all phases together):
 *   g++ -std=c++17 -DCATCH_CONFIG_MAIN -DAI_COMPANION_TEST \
 *       -I ../contrib -I .. \
 *       test_combined.cc catch_amalgamated.cc -o test_combined -lpthread
 *
 *   AI_CLIENT_DRY_RUN=1 ./test_combined
 *   AI_CLIENT_DRY_RUN=1 ./test_combined --list-tests        # enumerate all
 *   AI_CLIENT_DRY_RUN=1 ./test_combined [ai_client]          # filter by tag
 *   AI_CLIENT_DRY_RUN=1 ./test_combined [companion]          # Phase 3 only
 *   AI_CLIENT_DRY_RUN=1 ./test_combined [ingest]             # Phase 2 only
 *
 * Tags per phase:
 *   Phase 1: [ai_client] [singleton] [embed] [search] [llm] [fallback] [latency]
 *   Phase 2: [ingest] [rst_strip] [chunker]
 *   Phase 3: [companion] [action_parse] [action_string] [state] [json] [prompt] [dispatch] [pipeline]
 *
 * Adding Phase 4:
 *   1. Create test_ai_rewards.cc with [rewards] tags
 *   2. Add #include "test_ai_rewards.cc" below
 *   3. Recompile
 */

#ifndef CATCH_CONFIG_MAIN
#define CATCH_CONFIG_MAIN
#endif
#ifndef AI_COMPANION_TEST
#define AI_COMPANION_TEST
#endif
#include "catch_amalgamated.hpp"

// ---------------------------------------------------------------------------
// Phase 1 — AIClient networking tests
// ---------------------------------------------------------------------------
#include "test_ai_client.cc"

// ---------------------------------------------------------------------------
// Phase 2 — Lore ingestion chunker tests
// ---------------------------------------------------------------------------
#include "test_ingest_lore.cc"

// ---------------------------------------------------------------------------
// Phase 3 — AI companion dispatch + pipeline tests
// ---------------------------------------------------------------------------
#include "test_ai_companion.cc"

// ---------------------------------------------------------------------------
// Phase 4 — AI rewards, XP sharing, item grant mechanics
// ---------------------------------------------------------------------------
#ifndef AI_REWARDS_TEST
#define AI_REWARDS_TEST
#endif
#include "test_ai_rewards.cc"
