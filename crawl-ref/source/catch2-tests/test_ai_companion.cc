/**
 * @file test_ai_companion.cc
 * @brief Unit tests for AI companion action dispatch, state capture,
 *        prompt construction, and pipeline logic (Phase 3).
 *
 * Compiled with AI_COMPANION_TEST defined so all engine calls are stubbed.
 * All AIClient calls use AI_CLIENT_DRY_RUN=1 (no network).
 *
 * Composable: link with test_ai_client.cc and test_ingest_lore.cc into a
 * single test binary via test_combined.cc (see build instructions).
 *
 * Standalone compile:
 *   g++ -std=c++17 -DCATCH_CONFIG_MAIN -DAI_COMPANION_TEST \
 *       -I ../contrib -I .. \
 *       test_ai_companion.cc catch_amalgamated.cc -o test_ai_companion -lpthread
 *   AI_CLIENT_DRY_RUN=1 ./test_ai_companion
 */

#include "catch_amalgamated.hpp"

// Define test mode BEFORE including the companion header — stubs engine calls
#ifndef AI_COMPANION_TEST
#define AI_COMPANION_TEST
#endif
#include "../ai_companion.h"

using namespace ai_companion;
using Catch::Approx;

// ===========================================================================
// Action enum tests
// ===========================================================================

TEST_CASE("parse_action maps all known action strings", "[companion][action_parse]")
{
    REQUIRE(parse_action("CHAT")         == AncestorAction::CHAT);
    REQUIRE(parse_action("GRANT_REWARD") == AncestorAction::GRANT_REWARD);
    REQUIRE(parse_action("QUEST_LOG")    == AncestorAction::QUEST_LOG);
    REQUIRE(parse_action("WARN_THREAT")  == AncestorAction::WARN_THREAT);
    REQUIRE(parse_action("HEAL_SUGGEST") == AncestorAction::HEAL_SUGGEST);
    REQUIRE(parse_action("LORE_CITE")    == AncestorAction::LORE_CITE);
}

TEST_CASE("parse_action defaults unknown strings to CHAT", "[companion][action_parse]")
{
    REQUIRE(parse_action("")              == AncestorAction::CHAT);
    REQUIRE(parse_action("INVALID")       == AncestorAction::CHAT);
    REQUIRE(parse_action("grant_reward")  == AncestorAction::CHAT); // case sensitive
    REQUIRE(parse_action("SUMMON_DRAGON") == AncestorAction::CHAT);
}

TEST_CASE("action_to_string roundtrips all actions", "[companion][action_string]")
{
    for (int i = 0; i < static_cast<int>(AncestorAction::NUM_ACTIONS); ++i)
    {
        auto action = static_cast<AncestorAction>(i);
        std::string s = action_to_string(action);
        REQUIRE(!s.empty());
        REQUIRE(parse_action(s) == action);
    }
}

// ===========================================================================
// GameState capture tests
// ===========================================================================

TEST_CASE("capture_game_state returns populated struct (stub)", "[companion][state]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs = capture_game_state();

    // Stub returns known values — verify they're present
    REQUIRE(gs.player_hp      == 45);
    REQUIRE(gs.player_hp_max  == 100);
    REQUIRE(gs.location       == "Dungeon:3");
    REQUIRE(gs.turn           == 42);
    REQUIRE(!gs.ancestor_name.empty());
    REQUIRE(gs.inventory.size() == 3);
    REQUIRE(gs.visible_threats.size() == 2);
}

TEST_CASE("GameState inventory contains expected items", "[companion][state]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs = capture_game_state();

    // Verify the test stub provides realistic inventory
    bool has_potion = false;
    for (const auto& item : gs.inventory)
        if (item.find("potion") != std::string::npos)
            has_potion = true;
    REQUIRE(has_potion);
}

TEST_CASE("GameState visible_threats lists enemies", "[companion][state]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs = capture_game_state();

    // Threats should be non-empty strings
    for (const auto& t : gs.visible_threats)
        REQUIRE(!t.empty());
}

// ===========================================================================
// State → JSON serialisation
// ===========================================================================

TEST_CASE("state_to_json produces valid JSON with all fields", "[companion][json]")
{
    GameState gs;
    gs.player_hp      = 72;
    gs.player_hp_max  = 100;
    gs.location       = "The Abyss:2";
    gs.inventory      = {"staff of destruction", "elven cloak"};
    gs.visible_threats = {"abyssal fiend", "tentacled thing"};
    gs.turn           = 156;
    gs.ancestor_name  = "Sigrid the Bold";

    json j = state_to_json(gs);

    REQUIRE(j["player_health"]["current"].get<int>() == 72);
    REQUIRE(j["player_health"]["max"].get<int>()     == 100);
    REQUIRE(j["location"].get<std::string>()         == "The Abyss:2");
    REQUIRE(j["turn"].get<int>()                     == 156);
    REQUIRE(j["ancestor_name"].get<std::string>()    == "Sigrid the Bold");

    // Inventory array
    REQUIRE(j["inventory"].is_array());
    REQUIRE(j["inventory"].size() == 2);
    REQUIRE(j["inventory"][0].get<std::string>() == "staff of destruction");

    // Threats array
    REQUIRE(j["visible_threats"].is_array());
    REQUIRE(j["visible_threats"].size() == 2);
}

TEST_CASE("state_to_json handles empty collections", "[companion][json]")
{
    GameState gs;
    // Leave inventory and threats empty

    json j = state_to_json(gs);

    REQUIRE(j["inventory"].is_array());
    REQUIRE(j["inventory"].empty());
    REQUIRE(j["visible_threats"].is_array());
    REQUIRE(j["visible_threats"].empty());
}

// ===========================================================================
// Prompt builder
// ===========================================================================

TEST_CASE("build_prompt includes system instruction", "[companion][prompt]")
{
    GameState gs = capture_game_state();
    std::string prompt = build_prompt(gs, "Some lore text.", "Hello ancestor");

    REQUIRE(prompt.find("System:") != std::string::npos);
    REQUIRE(prompt.find("Hepliaklqana") != std::string::npos);
    REQUIRE(prompt.find(gs.ancestor_name) != std::string::npos);
}

TEST_CASE("build_prompt embeds lore context", "[companion][prompt]")
{
    GameState gs = capture_game_state();
    std::string lore = "The Orb of Zot lies deep in the dungeon.";
    std::string prompt = build_prompt(gs, lore, "Where is the Orb?");

    REQUIRE(prompt.find("Context (Lore") != std::string::npos);
    REQUIRE(prompt.find(lore) != std::string::npos);
}

TEST_CASE("build_prompt includes game state JSON", "[companion][prompt]")
{
    GameState gs;
    gs.player_hp     = 33;
    gs.location      = "Zot:5";
    gs.turn          = 999;

    std::string prompt = build_prompt(gs, "", "test");

    REQUIRE(prompt.find("Game State:") != std::string::npos);
    REQUIRE(prompt.find("\"current\": 33") != std::string::npos);
    REQUIRE(prompt.find("Zot:5") != std::string::npos);
    REQUIRE(prompt.find("999") != std::string::npos);
}

TEST_CASE("build_prompt includes player input", "[companion][prompt]")
{
    GameState gs = capture_game_state();
    std::string input = "What dangers await me in the depths?";
    std::string prompt = build_prompt(gs, "", input);

    REQUIRE(prompt.find("Player Input:") != std::string::npos);
    REQUIRE(prompt.find(input) != std::string::npos);
}

TEST_CASE("build_prompt specifies JSON response schema", "[companion][prompt]")
{
    GameState gs = capture_game_state();
    std::string prompt = build_prompt(gs, "", "test");

    REQUIRE(prompt.find("\"chat\"") != std::string::npos);
    REQUIRE(prompt.find("\"action\"") != std::string::npos);
    REQUIRE(prompt.find("\"payload\"") != std::string::npos);
    REQUIRE(prompt.find("GRANT_REWARD") != std::string::npos);
    REQUIRE(prompt.find("WARN_THREAT") != std::string::npos);
    REQUIRE(prompt.find("HEAL_SUGGEST") != std::string::npos);
}

TEST_CASE("build_prompt truncates overly long lore", "[companion][prompt]")
{
    GameState gs = capture_game_state();
    // Create lore longer than 2048 chars
    std::string long_lore(4000, 'X');
    long_lore += "UNIQUE_TAIL_MARKER";

    std::string prompt = build_prompt(gs, long_lore, "test");

    // The unique tail marker at position >2048 should be truncated away
    REQUIRE(prompt.find("UNIQUE_TAIL_MARKER") == std::string::npos);
    // But the first 2048 chars of X should be present
    REQUIRE(prompt.find(std::string(100, 'X')) != std::string::npos);
}

TEST_CASE("build_prompt with empty lore omits context section", "[companion][prompt]")
{
    GameState gs = capture_game_state();
    std::string prompt = build_prompt(gs, "", "test");

    // Empty lore should skip the "Context (Lore" section entirely
    REQUIRE(prompt.find("Context (Lore") == std::string::npos);
}

// ===========================================================================
// Dispatch tests (stub mode — records actions to log)
// ===========================================================================

TEST_CASE("dispatch_action logs CHAT correctly", "[companion][dispatch]")
{
    std::vector<std::string> log;
    dispatch_action(AncestorAction::CHAT,
                    "Beware the shadows!", "", log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].find("[CHAT]") != std::string::npos);
    REQUIRE(log[0].find("Beware the shadows!") != std::string::npos);
}

TEST_CASE("dispatch_action logs GRANT_REWARD with payload", "[companion][dispatch]")
{
    std::vector<std::string> log;
    dispatch_action(AncestorAction::GRANT_REWARD,
                    "Here, take this.", "healing_potion", log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].find("[GRANT_REWARD]") != std::string::npos);
    REQUIRE(log[0].find("healing_potion") != std::string::npos);
}

TEST_CASE("dispatch_action logs WARN_THREAT", "[companion][dispatch]")
{
    std::vector<std::string> log;
    dispatch_action(AncestorAction::WARN_THREAT,
                    "Dragon ahead!", "fire_dragon", log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].find("[WARN_THREAT]") != std::string::npos);
    REQUIRE(log[0].find("Dragon ahead!") != std::string::npos);
}

TEST_CASE("dispatch_action logs QUEST_LOG", "[companion][dispatch]")
{
    std::vector<std::string> log;
    dispatch_action(AncestorAction::QUEST_LOG,
                    "A new quest begins.", "Retrieve the ancient key", log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].find("[QUEST_LOG]") != std::string::npos);
    REQUIRE(log[0].find("ancient key") != std::string::npos);
}

TEST_CASE("dispatch_action logs HEAL_SUGGEST", "[companion][dispatch]")
{
    std::vector<std::string> log;
    dispatch_action(AncestorAction::HEAL_SUGGEST,
                    "Use your healing potion now!", "", log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].find("[HEAL_SUGGEST]") != std::string::npos);
}

TEST_CASE("dispatch_action logs LORE_CITE", "[companion][dispatch]")
{
    std::vector<std::string> log;
    dispatch_action(AncestorAction::LORE_CITE,
                    "The manual says beware of traps.", "crawl_manual:trap_section", log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].find("[LORE_CITE]") != std::string::npos);
    REQUIRE(log[0].find("crawl_manual") != std::string::npos);
}

// ===========================================================================
// Full pipeline integration (dry run — tests composition of all layers)
// ===========================================================================

TEST_CASE("run_companion_pipeline returns valid CompanionResponse", "[companion][pipeline]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs = capture_game_state();
    auto resp = run_companion_pipeline("What dangers lie ahead?", gs);

    // Should have a non-empty chat
    REQUIRE(!resp.chat.empty());

    // Action should be a valid enum
    REQUIRE(static_cast<int>(resp.action) >= 0);
    REQUIRE(static_cast<int>(resp.action) < static_cast<int>(AncestorAction::NUM_ACTIONS));

    // Latency field should be non-negative
    REQUIRE(resp.llm_ms    >= 0.0);

    // Direction field should be populated (default FOLLOW)
    REQUIRE(!resp.direction.empty());
}

TEST_CASE("pipeline produces JSON-valid action field", "[companion][pipeline]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs = capture_game_state();
    auto resp = run_companion_pipeline("Tell me about this area", gs);

    // The dry-run LLM returns CHAT action
    std::string act_str = action_to_string(resp.action);
    REQUIRE(!act_str.empty());
    // Round-trip must preserve
    REQUIRE(parse_action(act_str) == resp.action);
}

TEST_CASE("pipeline handles threat-aware input construction", "[companion][pipeline]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    // State with visible threats
    GameState gs;
    gs.visible_threats = {"giant spider", "ogre"};
    gs.ancestor_name = "Test Ancestor";

    // Build the trigger input the same way mon-speak.cc does
    std::string trigger_input;
    if (!gs.visible_threats.empty())
    {
        trigger_input = "I see ";
        for (size_t i = 0; i < gs.visible_threats.size(); ++i)
        {
            if (i > 0) trigger_input += ", ";
            trigger_input += gs.visible_threats[i];
        }
        trigger_input += " nearby.";
    }

    REQUIRE(trigger_input == "I see giant spider, ogre nearby.");

    auto resp = run_companion_pipeline(trigger_input, gs);
    REQUIRE(!resp.chat.empty());
}

TEST_CASE("pipeline handles quiet dungeon input", "[companion][pipeline]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs;
    gs.visible_threats.clear(); // No threats
    gs.ancestor_name = "The Ancient One";

    std::string trigger_input = "The dungeon is quiet. What should I do next?";
    auto resp = run_companion_pipeline(trigger_input, gs);
    REQUIRE(!resp.chat.empty());
}

TEST_CASE("pipeline latency and direction fields are populated", "[companion][pipeline]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    GameState gs = capture_game_state();
    auto resp = run_companion_pipeline("test", gs);

    // claude -p latency should be >= 0 (test mode is near-instant)
    REQUIRE(resp.llm_ms >= 0.0);

    // Direction should default to FOLLOW in test mode
    REQUIRE(!resp.direction.empty());
}

TEST_CASE("direction_to_key maps correctly", "[companion][direction]")
{
    REQUIRE(direction_to_key("N")  == 'k');
    REQUIRE(direction_to_key("S")  == 'j');
    REQUIRE(direction_to_key("E")  == 'l');
    REQUIRE(direction_to_key("W")  == 'h');
    REQUIRE(direction_to_key("NE") == 'u');
    REQUIRE(direction_to_key("NW") == 'y');
    REQUIRE(direction_to_key("SE") == 'n');
    REQUIRE(direction_to_key("SW") == 'b');
    REQUIRE(direction_to_key("STAY") == '.');
}

TEST_CASE("attack direction parsing", "[companion][direction]")
{
    REQUIRE(is_attack_direction("ATTACK_orc") == true);
    REQUIRE(is_attack_direction("ATTACK_giant_spider") == true);
    REQUIRE(is_attack_direction("N") == false);
    REQUIRE(is_attack_direction("FOLLOW") == false);
    REQUIRE(attack_target("ATTACK_orc") == "orc");
    REQUIRE(attack_target("ATTACK_giant_spider") == "giant_spider");
    REQUIRE(attack_target("N") == "");
}
