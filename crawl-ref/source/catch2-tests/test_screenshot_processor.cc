/**
 * @file test_screenshot_processor.cc
 * @brief TDD specifications for Phase 5 — screenshot parsing and gameplay
 *        scenario validation. Tests are written BEFORE implementation to
 *        define the contract.
 *
 * Tags: [screenshot] [parse] [scenario] [observation]
 *
 * Standalone:
 *   g++ -std=c++17 -DCATCH_CONFIG_MAIN -DAI_SCREENSHOT_TEST \
 *       -I ../contrib -I .. \
 *       test_screenshot_processor.cc catch_amalgamated.cc -o test_screenshot -lpthread
 *   AI_CLIENT_DRY_RUN=1 ./test_screenshot
 */

#ifndef AI_SCREENSHOT_TEST
#define AI_SCREENSHOT_TEST
#endif

#include "catch_amalgamated.hpp"
#include "../ai_screenshot.h"

using namespace ai_companion;

// ===========================================================================
// Screenshot frame parsing — dungeon map extraction
// ===========================================================================

TEST_CASE("parse_screenshot extracts dungeon grid from raw capture", "[screenshot][parse]")
{
    // Simulated raw screenshot: 5-row dungeon viewport
    std::string raw =
        "######\n"
        "#....#\n"
        "#.@.o#\n"   // @ = player, o = item
        "#..a.#\n"   // a = monster (axe-wielding orc in glyph shorthand)
        "######\n";

    ScreenshotFrame frame = parse_screenshot(raw);

    REQUIRE(frame.width == 6);
    REQUIRE(frame.height == 5);
    REQUIRE(frame.player_pos.x == 2);
    REQUIRE(frame.player_pos.y == 2);
}

TEST_CASE("parse_screenshot identifies player position via @ glyph", "[screenshot][parse]")
{
    std::string raw =
        "###\n"
        "#@#\n"
        "###\n";

    ScreenshotFrame frame = parse_screenshot(raw);
    REQUIRE(frame.player_pos.x == 1);
    REQUIRE(frame.player_pos.y == 1);
}

TEST_CASE("parse_screenshot detects visible monsters", "[screenshot][parse]")
{
    // Standard crawl monster glyphs: uppercase letters for most monsters
    std::string raw =
        "#######\n"
        "#..O..#\n"   // O = ogre
        "#.@...#\n"
        "#....G#\n"   // G = goblin (hypothetical glyph)
        "#######\n";

    ScreenshotFrame frame = parse_screenshot(raw);

    REQUIRE(frame.visible_monsters.size() >= 1);
    // At minimum the O glyph should be detected as a monster position
    bool found_O = false;
    for (const auto& m : frame.visible_monsters)
        if (m.x == 3 && m.y == 1) found_O = true;
    REQUIRE(found_O);
}

TEST_CASE("parse_screenshot detects item glyphs on ground", "[screenshot][parse]")
{
    std::string raw =
        "#####\n"
        "#.;.#\n"     // ; = scroll on ground
        "#.@.#\n"
        "#.%:##\n";   // % = potion, : = food (hypothetical)

    ScreenshotFrame frame = parse_screenshot(raw);
    REQUIRE(frame.ground_items.size() >= 1);
}

TEST_CASE("parse_screenshot handles empty/blank viewport gracefully", "[screenshot][parse]")
{
    ScreenshotFrame frame = parse_screenshot("");
    REQUIRE(frame.width == 0);
    REQUIRE(frame.height == 0);
    REQUIRE(frame.player_pos.x == -1);
    REQUIRE(frame.player_pos.y == -1);
}

TEST_CASE("parse_screenshot handles viewport with no player glyph", "[screenshot][parse]")
{
    std::string raw =
        "###\n"
        "#.#\n"
        "###\n";

    ScreenshotFrame frame = parse_screenshot(raw);
    // Player not found — sentinel values
    REQUIRE(frame.player_pos.x == -1);
    REQUIRE(frame.player_pos.y == -1);
    REQUIRE(frame.width == 3);
    REQUIRE(frame.height == 3);
}

TEST_CASE("parse_screenshot counts wall vs open cells", "[screenshot][parse]")
{
    std::string raw =
        "#####\n"
        "#...#\n"
        "#.@.#\n"
        "#...#\n"
        "#####\n";

    ScreenshotFrame frame = parse_screenshot(raw);
    REQUIRE(frame.wall_count == 16);   // perimeter of 5x5
    REQUIRE(frame.open_count == 9);    // 3x3 interior (includes player cell)
}

// ===========================================================================
// Glyph classification — monster vs item vs terrain
// ===========================================================================

TEST_CASE("classify_glyph identifies standard monster glyphs", "[screenshot][parse]")
{
    // Crawl uses uppercase for most monsters
    REQUIRE(classify_glyph('O') == GlyphClass::MONSTER);  // ogre
    REQUIRE(classify_glyph('D') == GlyphClass::MONSTER);  // dragon
    REQUIRE(classify_glyph('L') == GlyphClass::MONSTER);  // lich
    REQUIRE(classify_glyph('S') == GlyphClass::MONSTER);  // snake
}

TEST_CASE("classify_glyph identifies item glyphs", "[screenshot][parse]")
{
    REQUIRE(classify_glyph(';') == GlyphClass::ITEM);     // scroll
    REQUIRE(classify_glyph('%') == GlyphClass::ITEM);     // potion
    REQUIRE(classify_glyph('$') == GlyphClass::ITEM);     // gold
    REQUIRE(classify_glyph('(') == GlyphClass::ITEM);     // weapon
    REQUIRE(classify_glyph('[') == GlyphClass::ITEM);     // armour
}

TEST_CASE("classify_glyph identifies terrain glyphs", "[screenshot][parse]")
{
    REQUIRE(classify_glyph('#') == GlyphClass::WALL);
    REQUIRE(classify_glyph('.') == GlyphClass::FLOOR);
    REQUIRE(classify_glyph(' ') == GlyphClass::EMPTY);
    REQUIRE(classify_glyph('@') == GlyphClass::PLAYER);
}

TEST_CASE("classify_glyph handles unknown glyphs as OTHER", "[screenshot][parse]")
{
    // Control chars or symbols with no crawl mapping
    REQUIRE(classify_glyph('\0') == GlyphClass::OTHER);
    REQUIRE(classify_glyph('~') == GlyphClass::OTHER);
    // Note: '?' is an unidentified scroll in crawl — correctly classified as ITEM
    REQUIRE(classify_glyph('?') == GlyphClass::ITEM);
}

// ===========================================================================
// Gameplay scenario definitions — dynamic scenario registry
// ===========================================================================

TEST_CASE("scenario_registry contains all required gameplay scenarios", "[screenshot][scenario]")
{
    const auto& scenarios = scenario_registry();

    // Must have at minimum these 5 core scenarios
    bool has_spawn      = false;
    bool has_combat     = false;
    bool has_xp_share   = false;
    bool has_item       = false;
    bool has_quest      = false;

    for (const auto& s : scenarios)
    {
        if (s.id == "spawn_companion")     has_spawn    = true;
        if (s.id == "enemy_encounter")     has_combat   = true;
        if (s.id == "xp_share_on_kill")    has_xp_share = true;
        if (s.id == "item_reward_grant")   has_item     = true;
        if (s.id == "quest_progression")   has_quest    = true;
    }

    REQUIRE(has_spawn);
    REQUIRE(has_combat);
    REQUIRE(has_xp_share);
    REQUIRE(has_item);
    REQUIRE(has_quest);
}

TEST_CASE("each scenario has a non-empty trigger condition", "[screenshot][scenario]")
{
    const auto& scenarios = scenario_registry();
    for (const auto& s : scenarios)
    {
        REQUIRE(!s.trigger.empty());
        REQUIRE(!s.id.empty());
        REQUIRE(!s.description.empty());
    }
}

TEST_CASE("scenario trigger matching works on screenshot frames", "[screenshot][scenario]")
{
    // Build a frame that looks like enemy encounter: monsters visible
    ScreenshotFrame frame;
    frame.width = 7;
    frame.height = 5;
    frame.player_pos = {3, 2};
    frame.visible_monsters.push_back({1, 1});
    frame.visible_monsters.push_back({5, 3});
    frame.ground_items = {};

    const auto& scenarios = scenario_registry();

    // Find the enemy_encounter scenario
    bool matched = false;
    for (const auto& s : scenarios)
    {
        if (match_scenario_trigger(s, frame))
        {
            if (s.id == "enemy_encounter") matched = true;
        }
    }
    REQUIRE(matched);
}

TEST_CASE("scenario trigger does NOT match when condition absent", "[screenshot][scenario]")
{
    // Empty frame — no monsters, no items
    ScreenshotFrame frame;
    frame.width = 5;
    frame.height = 5;
    frame.player_pos = {2, 2};
    frame.visible_monsters = {};
    frame.ground_items = {};

    // enemy_encounter requires visible monsters
    const auto& scenarios = scenario_registry();
    for (const auto& s : scenarios)
    {
        if (s.id == "enemy_encounter")
            REQUIRE(!match_scenario_trigger(s, frame));
    }
}

// ===========================================================================
// Observation log accumulation
// ===========================================================================

TEST_CASE("ObservationLog records frame sequence", "[screenshot][observation]")
{
    ObservationLog log;

    ScreenshotFrame f1, f2;
    f1.player_pos = {2, 2};
    f1.visible_monsters = {};
    f2.player_pos = {3, 2};
    f2.visible_monsters = {{1, 1}};

    log.record(f1, "turn_1", "Player moved east");
    log.record(f2, "turn_2", "Orc appeared to the north");

    REQUIRE(log.entries().size() == 2);
    REQUIRE(log.entries()[0].label == "turn_1");
    REQUIRE(log.entries()[1].label == "turn_2");
}

TEST_CASE("ObservationLog tracks active scenario per frame", "[screenshot][observation]")
{
    ObservationLog log;

    ScreenshotFrame frame;
    frame.player_pos = {3, 2};
    frame.visible_monsters = {{1, 1}};

    log.record(frame, "turn_5", "Combat begins");

    // The log should auto-detect scenario from the frame
    REQUIRE(!log.entries().back().active_scenario.empty());
}

TEST_CASE("ObservationLog exports to structured format", "[screenshot][observation]")
{
    ObservationLog log;

    ScreenshotFrame f;
    f.player_pos = {1, 1};
    f.width = 10;
    f.height = 8;
    f.visible_monsters = {};
    f.ground_items = {};

    log.record(f, "exploration_start", "Beginning descent");

    std::string exported = log.export_json();

    REQUIRE(!exported.empty());
    REQUIRE(exported.find("exploration_start") != std::string::npos);
    REQUIRE(exported.find("Beginning descent") != std::string::npos);
}

TEST_CASE("ObservationLog is empty on construction", "[screenshot][observation]")
{
    ObservationLog log;
    REQUIRE(log.entries().empty());
    REQUIRE(log.export_json().find("entries") != std::string::npos);
}

// ===========================================================================
// Dynamic scenario validation — companion response verification
// ===========================================================================

TEST_CASE("validate_companion_response accepts well-formed JSON action", "[screenshot][observation]")
{
    std::string response = R"({"chat":"I see danger ahead, stay sharp!","action":"WARN_THREAT","payload":"orc"})";
    ValidationResult vr = validate_companion_response(response);

    REQUIRE(vr.valid == true);
    REQUIRE(vr.action == "WARN_THREAT");
    REQUIRE(vr.has_chat == true);
}

TEST_CASE("validate_companion_response rejects malformed JSON", "[screenshot][observation]")
{
    std::string response = "this is not json at all";
    ValidationResult vr = validate_companion_response(response);

    REQUIRE(vr.valid == false);
}

TEST_CASE("validate_companion_response rejects missing action field", "[screenshot][observation]")
{
    std::string response = R"({"chat":"Hello there","payload":""})";
    ValidationResult vr = validate_companion_response(response);

    REQUIRE(vr.valid == false);
    REQUIRE(vr.action.empty());
}

TEST_CASE("validate_companion_response accepts all valid action types", "[screenshot][observation]")
{
    const char* actions[] = {"CHAT", "GRANT_REWARD", "QUEST_LOG",
                             "WARN_THREAT", "HEAL_SUGGEST", "LORE_CITE"};

    for (const char* act : actions)
    {
        std::string response = std::string(R"({"chat":"msg","action":")")
                             + act + R"(","payload":"data"})";
        ValidationResult vr = validate_companion_response(response);
        REQUIRE(vr.valid == true);
        REQUIRE(vr.action == act);
    }
}

TEST_CASE("validate_companion_response flags unknown action as invalid", "[screenshot][observation]")
{
    std::string response = R"({"chat":"msg","action":"SUMMON_DRAGON","payload":""})";
    ValidationResult vr = validate_companion_response(response);

    REQUIRE(vr.valid == false);
}
