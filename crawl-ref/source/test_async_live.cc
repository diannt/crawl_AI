/**
 * @file test_async_live.cc
 * @brief Live test: parallel claude -p calls via AsyncClaudeOrchestrator.
 *
 * Compile:
 *   g++ -std=c++17 -DAI_COMPANION_TEST \
 *       -I crawl-ref/source -I crawl-ref/source/contrib \
 *       crawl-ref/source/test_async_live.cc -o test_async_live
 *
 * Run:
 *   ./test_async_live
 */

#define AI_COMPANION_TEST
// NOT defining CLAUDE_ORCH_TEST — real claude -p subprocess calls
#include "ai_companion.h"
#include "async_claude_orchestrator.h"

#include <cassert>
#include <cstdio>

using namespace ai_companion;

int main()
{
    printf("========================================\n");
    printf("Async Pipeline Live Test\n");
    printf("Two PARALLEL claude -p calls\n");
    printf("========================================\n");

    auto& orch = AsyncClaudeOrchestrator::instance();

    // Set up game state
    GameState gs;
    gs.player_hp = 55;
    gs.player_hp_max = 100;
    gs.location = "Dungeon:4";
    gs.visible_threats = {"ogre", "gnoll"};
    gs.turn = 88;
    gs.ancestor_name = "Aldric the Undying";
    gs.inventory = {"ring mail", "mace", "healing potion"};

    // Build prompts
    std::string action_prompt = build_prompt(gs, std::string(LORE_CONTEXT),
        "I see ogre, gnoll nearby. What direction should I move or who should I attack?");
    std::string commentary_prompt = build_commentary_prompt(gs,
        "Visible enemies: ogre, gnoll. Location: Dungeon:4. HP: 55/100.");

    printf("\nLaunching two claude -p calls in PARALLEL...\n");
    auto t0 = std::chrono::steady_clock::now();

    // Launch BOTH at the same time
    orch.launch(action_prompt, "action");
    orch.launch(commentary_prompt, "commentary");

    printf("  Both launched. Active processes: %zu\n", orch.active_count());

    // Wait for both to complete
    printf("  Waiting for action...\n");
    orch.wait_for("action", 120000);
    printf("  Waiting for commentary...\n");
    orch.wait_for("commentary", 120000);

    auto t1 = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Get results
    json action_json = orch.get_json("action");
    json commentary_json = orch.get_json("commentary");

    printf("\n--- Action Result ---\n");
    printf("  chat: %s\n", action_json.value("chat", std::string("N/A")).c_str());
    printf("  action: %s\n", action_json.value("action", std::string("N/A")).c_str());
    printf("  direction: %s\n", action_json.value("direction", std::string("N/A")).c_str());

    printf("\n--- Commentary Result ---\n");
    std::string commentary = commentary_json.value("commentary",
        commentary_json.value("chat", std::string("N/A")));
    printf("  commentary: %s\n", commentary.c_str());

    printf("\n--- Message Log Output ---\n");
    printf("  Companion says: %s\n", action_json.value("chat", std::string("...")).c_str());
    printf("  Companion says: %s\n", commentary.c_str());
    printf("  [Direction: %s]\n", action_json.value("direction", std::string("FOLLOW")).c_str());

    printf("\n--- Performance ---\n");
    printf("  Total wall time (parallel): %.1f ms\n", total_ms);
    printf("  (vs sequential ~15-20s, parallel should be ~7-10s)\n");

    // Record to world tree
    WorldTreeEntry entry;
    entry.turn = gs.turn;
    entry.action = action_json.value("action", std::string("CHAT"));
    entry.direction = action_json.value("direction", std::string("FOLLOW"));
    entry.chat = action_json.value("chat", std::string(""));
    entry.commentary = commentary;
    entry.timestamp = std::chrono::steady_clock::now();
    orch.world_tree().record(entry);

    printf("\n--- World Tree ---\n");
    printf("%s\n", orch.world_tree().build_context_summary().c_str());

    // Cleanup
    orch.cleanup();

    bool pass = !action_json.value("chat", std::string("")).empty()
             && !commentary.empty()
             && total_ms > 0;

    printf("========================================\n");
    printf("Result: %s\n", pass ? "PASS" : "FAIL");
    printf("========================================\n");

    return pass ? 0 : 1;
}
