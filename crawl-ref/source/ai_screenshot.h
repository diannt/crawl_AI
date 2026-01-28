/**
 * @file ai_screenshot.h
 * @brief Phase 5 — Screenshot parsing, glyph classification, and dynamic
 *        gameplay scenario observation for the LLM co-player.
 *
 * Provides:
 *   - parse_screenshot(raw): extracts structured ScreenshotFrame from ASCII dump
 *   - classify_glyph(ch): maps character to GlyphClass (MONSTER/ITEM/TERRAIN/etc.)
 *   - scenario_registry(): static list of observable gameplay scenarios
 *   - match_scenario_trigger(scenario, frame): evaluates trigger against live frame
 *   - ObservationLog: records frame sequence with labels, auto-detects active scenario
 *   - validate_companion_response(json): verifies LLM output has valid action schema
 *
 * Test mode: define AI_SCREENSHOT_TEST before including — no engine linkage.
 */

#pragma once

#include <string>
#include <vector>
#include <set>

#include "contrib/nlohmann/json.hpp"

using json = nlohmann::json;

namespace ai_companion
{

// ---------------------------------------------------------------------------
// Glyph classification
// ---------------------------------------------------------------------------

enum class GlyphClass
{
    PLAYER,     // @
    MONSTER,    // uppercase letters (most crawl monsters)
    ITEM,       // ; % $ ( [ etc.
    WALL,       // #
    FLOOR,      // .
    EMPTY,      // space / out-of-bounds
    OTHER       // unknown / unclassified
};

static inline GlyphClass classify_glyph(char ch)
{
    if (ch == '@') return GlyphClass::PLAYER;

    // Standard crawl monster glyphs — uppercase A-Z covers ogre, dragon,
    // lich, snake, troll, vampire, etc.
    if (ch >= 'A' && ch <= 'Z') return GlyphClass::MONSTER;

    // Item glyphs as defined in crawl's showsymb.h conventions
    static const std::string item_glyphs = ";%$([{+?!\"";
    if (item_glyphs.find(ch) != std::string::npos) return GlyphClass::ITEM;

    // Terrain
    if (ch == '#') return GlyphClass::WALL;
    if (ch == '.') return GlyphClass::FLOOR;
    if (ch == ' ') return GlyphClass::EMPTY;

    return GlyphClass::OTHER;
}

// ---------------------------------------------------------------------------
// Screenshot frame — structured representation of a parsed game view
// ---------------------------------------------------------------------------

struct GridPos
{
    int x = -1;
    int y = -1;
};

struct ScreenshotFrame
{
    int width  = 0;
    int height = 0;

    GridPos player_pos;                        // @ location, (-1,-1) if absent
    std::vector<GridPos> visible_monsters;     // detected monster glyph positions
    std::vector<GridPos> ground_items;         // detected item glyph positions

    int wall_count = 0;
    int open_count = 0;                        // floor + player + item cells
};

static inline ScreenshotFrame parse_screenshot(const std::string& raw)
{
    ScreenshotFrame frame;
    if (raw.empty()) return frame;

    // Split into lines
    std::vector<std::string> lines;
    std::string line;
    for (char ch : raw)
    {
        if (ch == '\n')
        {
            lines.push_back(line);
            line.clear();
        }
        else
        {
            line += ch;
        }
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) return frame;

    frame.height = static_cast<int>(lines.size());
    frame.width  = 0;
    for (const auto& l : lines)
        frame.width = std::max(frame.width, static_cast<int>(l.size()));

    // Scan each cell
    for (int y = 0; y < frame.height; ++y)
    {
        for (int x = 0; x < static_cast<int>(lines[y].size()); ++x)
        {
            char ch = lines[y][x];
            GlyphClass cls = classify_glyph(ch);

            switch (cls)
            {
                case GlyphClass::PLAYER:
                    frame.player_pos = {x, y};
                    frame.open_count++;
                    break;
                case GlyphClass::MONSTER:
                    frame.visible_monsters.push_back({x, y});
                    frame.open_count++;
                    break;
                case GlyphClass::ITEM:
                    frame.ground_items.push_back({x, y});
                    frame.open_count++;
                    break;
                case GlyphClass::WALL:
                    frame.wall_count++;
                    break;
                case GlyphClass::FLOOR:
                    frame.open_count++;
                    break;
                default:
                    break;
            }
        }
    }

    return frame;
}

// ---------------------------------------------------------------------------
// Gameplay scenario definitions — dynamic trigger registry
// ---------------------------------------------------------------------------

struct GameplayScenario
{
    std::string id;
    std::string trigger;       // human-readable trigger condition
    std::string description;   // what this scenario validates
};

static inline const std::vector<GameplayScenario>& scenario_registry()
{
    static const std::vector<GameplayScenario> scenarios = {
        {
            "spawn_companion",
            "Game starts with GOD_HEPLIAKLQANA and ancestor appears",
            "Validates auto-spawn: religion set, companion visible in first frame"
        },
        {
            "enemy_encounter",
            "Visible monsters detected on map",
            "Companion should WARN_THREAT or issue tactical advice"
        },
        {
            "xp_share_on_kill",
            "Monster killed while ancestor alive",
            "XP_SHARE event logged, narration generated, gain_exp called"
        },
        {
            "item_reward_grant",
            "GRANT_REWARD action dispatched by LLM",
            "Item resolved via registry, spawned or logged, reward_log updated"
        },
        {
            "quest_progression",
            "QUEST_LOG action dispatched with non-empty payload",
            "Quest text appended to quest_log, storyline continuity maintained"
        },
        {
            "low_hp_heal_suggest",
            "Player HP below 30% of max",
            "Companion issues HEAL_SUGGEST with potion or retreat advice"
        },
        {
            "exploration_ambient",
            "No monsters visible, player in open area",
            "Companion issues ambient CHAT — narrative flavor, lore citation, or guidance"
        }
    };
    return scenarios;
}

static inline bool match_scenario_trigger(const GameplayScenario& scenario,
                                          const ScreenshotFrame& frame)
{
    if (scenario.id == "spawn_companion")
    {
        // Frame has player — companion scenario active if player detected
        return frame.player_pos.x >= 0;
    }
    if (scenario.id == "enemy_encounter")
    {
        return !frame.visible_monsters.empty();
    }
    if (scenario.id == "xp_share_on_kill")
    {
        // Stateful — requires external kill event; frame alone can't confirm
        // In observation mode, this is detected via stderr log parsing
        return false;  // validated externally
    }
    if (scenario.id == "item_reward_grant")
    {
        return !frame.ground_items.empty();
    }
    if (scenario.id == "quest_progression")
    {
        return false;  // validated via LLM response parsing
    }
    if (scenario.id == "low_hp_heal_suggest")
    {
        return false;  // requires HP data from game state, not screenshot alone
    }
    if (scenario.id == "exploration_ambient")
    {
        return frame.visible_monsters.empty() && frame.player_pos.x >= 0;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Observation log — records frame sequence with active scenario detection
// ---------------------------------------------------------------------------

struct ObservationEntry
{
    ScreenshotFrame frame;
    std::string label;
    std::string narrative;
    std::string active_scenario;
};

class ObservationLog
{
public:
    void record(const ScreenshotFrame& frame,
                const std::string& label,
                const std::string& narrative)
    {
        ObservationEntry entry;
        entry.frame = frame;
        entry.label = label;
        entry.narrative = narrative;

        // Auto-detect active scenario
        const auto& scenarios = scenario_registry();
        for (const auto& s : scenarios)
        {
            if (match_scenario_trigger(s, frame))
            {
                entry.active_scenario = s.id;
                break;  // first match wins (priority order in registry)
            }
        }

        entries_.push_back(entry);
    }

    const std::vector<ObservationEntry>& entries() const { return entries_; }

    std::string export_json() const
    {
        json obj;
        obj["entries"] = json::array();
        for (const auto& e : entries_)
        {
            json entry;
            entry["label"] = e.label;
            entry["narrative"] = e.narrative;
            entry["active_scenario"] = e.active_scenario;
            entry["player_pos"] = {{"x", e.frame.player_pos.x},
                                   {"y", e.frame.player_pos.y}};
            entry["monsters"] = static_cast<int>(e.frame.visible_monsters.size());
            entry["items"] = static_cast<int>(e.frame.ground_items.size());
            obj["entries"].push_back(entry);
        }
        return obj.dump(2);
    }

private:
    std::vector<ObservationEntry> entries_;
};

// ---------------------------------------------------------------------------
// Companion response validation — verify LLM output schema
// ---------------------------------------------------------------------------

struct ValidationResult
{
    bool valid      = false;
    std::string action;
    bool has_chat   = false;
    std::string error;
};

static inline ValidationResult validate_companion_response(const std::string& response)
{
    ValidationResult vr;

    // Known valid actions
    static const std::set<std::string> valid_actions = {
        "CHAT", "GRANT_REWARD", "QUEST_LOG",
        "WARN_THREAT", "HEAL_SUGGEST", "LORE_CITE"
    };

    try
    {
        json parsed = json::parse(response);

        // Must have action field
        if (!parsed.contains("action") || !parsed["action"].is_string())
        {
            vr.error = "missing or non-string 'action' field";
            return vr;
        }

        vr.action = parsed["action"].get<std::string>();

        // Action must be in valid set
        if (valid_actions.find(vr.action) == valid_actions.end())
        {
            vr.error = "unknown action: " + vr.action;
            return vr;
        }

        // Check chat field presence
        vr.has_chat = parsed.contains("chat") && parsed["chat"].is_string()
                      && !parsed["chat"].get<std::string>().empty();

        vr.valid = true;
    }
    catch (const json::parse_error&)
    {
        vr.error = "invalid JSON";
    }
    catch (const std::exception& e)
    {
        vr.error = std::string("parse error: ") + e.what();
    }

    return vr;
}

} // namespace ai_companion
