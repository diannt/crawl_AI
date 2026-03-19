/**
 * @file test_live_pipeline.cc
 * @brief Standalone live integration test for the AI companion pipeline.
 *
 * Exercises the REAL claude -p subprocess (not test stubs).
 * Tests:
 *   1. Action + direction pipeline (combat scenario)
 *   2. Action + direction pipeline (exploration scenario)
 *   3. Commentary pipeline
 *   4. Player-to-companion command routing
 *   5. Full combined flow (simulates mon-speak.cc trigger)
 *
 * Compile:
 *   cd /home/maatru/dev/code_pref/model_a
 *   g++ -std=c++17 -DAI_COMPANION_TEST \
 *       -I crawl-ref/source -I crawl-ref/source/contrib \
 *       crawl-ref/source/test_live_pipeline.cc -o test_live_pipeline
 *
 * Run (uses REAL claude -p, takes ~30-60s):
 *   ./test_live_pipeline
 */

// Use test mode for game state stubs, but NOT for claude orchestrator
// (we want real claude -p calls)
#define AI_COMPANION_TEST
// Do NOT define CLAUDE_ORCH_TEST — we want real subprocess calls
#include "ai_companion.h"
#include "ai_screenshot.h"

#include <cassert>
#include <cstdio>

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name) \
    printf("\n=== TEST: %s ===\n", name); \
    tests_total++;

#define PASS() \
    printf("  PASS\n"); \
    tests_passed++;

#define CHECK(cond, msg) \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        return; \
    }

using namespace ai_companion;

// ---------------------------------------------------------------------------
// Test 1: Combat scenario — action + direction
// ---------------------------------------------------------------------------
void test_combat_pipeline()
{
    TEST("Combat action+direction pipeline (real claude -p)")

    GameState gs;
    gs.player_hp = 55;
    gs.player_hp_max = 100;
    gs.location = "Dungeon:5";
    gs.visible_threats = {"ogre", "gnoll"};
    gs.turn = 120;
    gs.ancestor_name = "Aldric the Undying";

    std::string input = "I see ogre, gnoll nearby. What direction should I move or who should I attack?";
    auto resp = run_companion_pipeline(input, gs);

    printf("  chat: %s\n", resp.chat.c_str());
    printf("  action: %s\n", action_to_string(resp.action).c_str());
    printf("  direction: %s\n", resp.direction.c_str());
    printf("  llm_ms: %.1f\n", resp.llm_ms);

    CHECK(!resp.chat.empty(), "chat should not be empty")
    CHECK(!resp.direction.empty(), "direction should not be empty")
    CHECK(resp.llm_ms > 0, "should have non-zero latency")

    // Direction should be attack-related or directional when enemies visible
    bool valid_dir = (resp.direction.substr(0, 7) == "ATTACK_" ||
                      resp.direction == "N" || resp.direction == "S" ||
                      resp.direction == "E" || resp.direction == "W" ||
                      resp.direction == "NE" || resp.direction == "NW" ||
                      resp.direction == "SE" || resp.direction == "SW" ||
                      resp.direction == "STAY" || resp.direction == "FOLLOW");
    CHECK(valid_dir, "direction should be valid enum value")

    PASS()
}

// ---------------------------------------------------------------------------
// Test 2: Exploration scenario — no enemies
// ---------------------------------------------------------------------------
void test_exploration_pipeline()
{
    TEST("Exploration pipeline (real claude -p)")

    GameState gs;
    gs.player_hp = 100;
    gs.player_hp_max = 100;
    gs.location = "Dungeon:1";
    gs.visible_threats = {};
    gs.turn = 10;
    gs.ancestor_name = "Aldric the Undying";

    std::string input = "The dungeon is quiet. Which direction should I explore?";
    auto resp = run_companion_pipeline(input, gs);

    printf("  chat: %s\n", resp.chat.c_str());
    printf("  direction: %s\n", resp.direction.c_str());

    CHECK(!resp.chat.empty(), "chat should not be empty")
    CHECK(!resp.direction.empty(), "direction should not be empty")

    PASS()
}

// ---------------------------------------------------------------------------
// Test 3: Commentary pipeline
// ---------------------------------------------------------------------------
void test_commentary_pipeline()
{
    TEST("Commentary pipeline (real claude -p)")

    GameState gs;
    gs.player_hp = 72;
    gs.player_hp_max = 100;
    gs.location = "Lair:2";
    gs.visible_threats = {"death yak"};
    gs.turn = 500;
    gs.ancestor_name = "Aldric the Undying";
    gs.inventory = {"ring mail", "mace", "healing potion"};

    auto resp = run_commentary_pipeline(gs);

    printf("  commentary: %s\n", resp.commentary.c_str());
    printf("  success: %s\n", resp.success ? "yes" : "no");
    printf("  llm_ms: %.1f\n", resp.llm_ms);

    CHECK(resp.success, "commentary should succeed")
    CHECK(!resp.commentary.empty(), "commentary should not be empty")
    CHECK(resp.llm_ms > 0, "should have non-zero latency")

    PASS()
}

// ---------------------------------------------------------------------------
// Test 4: Player command detection and routing
// ---------------------------------------------------------------------------
void test_player_commands()
{
    TEST("Player command detection")

    CHECK(is_companion_command("@companion hello there"), "@companion should be detected")
    CHECK(is_companion_command("/c what should I do?"), "/c should be detected")
    CHECK(!is_companion_command("just a normal message"), "normal text should not trigger")
    CHECK(!is_companion_command("ab"), "short text should not trigger")

    CHECK(extract_companion_message("@companion hello") == "hello",
          "@companion prefix should be stripped")
    CHECK(extract_companion_message("/c what now") == "what now",
          "/c prefix should be stripped")

    PASS()
}

// ---------------------------------------------------------------------------
// Test 5: Player-to-companion with real claude -p
// ---------------------------------------------------------------------------
void test_player_to_companion_live()
{
    TEST("Player-to-companion live (real claude -p)")

    // This calls the real pipeline
    player_to_companion("@companion Should I go deeper into the dungeon?");

    // If we got here without crashing, the pipeline works
    PASS()
}

// ---------------------------------------------------------------------------
// Test 6: Full combined flow (simulates mon-speak.cc)
// ---------------------------------------------------------------------------
void test_full_combined_flow()
{
    TEST("Full combined flow: action + direction + commentary (real claude -p)")

    GameState gs;
    gs.player_hp = 45;
    gs.player_hp_max = 100;
    gs.location = "Dungeon:3";
    gs.visible_threats = {"orc", "troll"};
    gs.turn = 42;
    gs.ancestor_name = "Aldric the Undying";
    gs.inventory = {"iron ration", "healing potion", "short sword"};

    // Step 1: Action + direction (like mon-speak.cc does)
    printf("\n  --- Step 1: Action + Direction ---\n");
    std::string trigger = "I see orc, troll nearby. What direction should I move or who should I attack?";
    auto response = run_companion_pipeline(trigger, gs);

    printf("  chat: %s\n", response.chat.c_str());
    printf("  action: %s\n", action_to_string(response.action).c_str());
    printf("  direction: %s\n", response.direction.c_str());

    // Dispatch (test mode just logs)
    std::vector<std::string> action_log;
    dispatch_action(response.action, response.chat, response.payload, action_log);
    dispatch_companion_movement(nullptr, response.direction);

    CHECK(!response.chat.empty(), "action chat should not be empty")
    CHECK(!response.direction.empty(), "direction should not be empty")

    // Step 2: Commentary
    printf("\n  --- Step 2: Commentary ---\n");
    auto commentary = run_commentary_pipeline(gs);
    if (commentary.success)
    {
        dispatch_commentary(commentary.commentary);
        printf("  commentary: %s\n", commentary.commentary.c_str());
    }

    CHECK(!action_log.empty(), "action log should have entries")

    printf("\n  --- Message Log Output ---\n");
    printf("  Companion says: %s\n", response.chat.c_str());
    if (commentary.success)
        printf("  Companion says: %s\n", commentary.commentary.c_str());
    printf("  [Direction: %s]\n", response.direction.c_str());

    PASS()
}

// ---------------------------------------------------------------------------
int main()
{
    printf("========================================\n");
    printf("AI Companion Live Pipeline Test\n");
    printf("Using REAL claude -p (not test stubs)\n");
    printf("========================================\n");

    // Quick tests first (no claude -p)
    test_player_commands();

    // Live claude -p tests
    test_combat_pipeline();
    test_exploration_pipeline();
    test_commentary_pipeline();
    test_player_to_companion_live();
    test_full_combined_flow();

    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_total);
    printf("========================================\n");

    return (tests_passed == tests_total) ? 0 : 1;
}
