/**
 * @file ai_rewards.h
 * @brief Phase 4 mechanics — item rewards, XP sharing, and companion spawn gate.
 *
 * Provides:
 *   - ai_companion_enabled(): runtime gate (env AI_COMPANION=1, default on)
 *   - GrantReward(item_name): resolves string → object_class_type, spawns item
 *   - share_xp_with_ancestor(mons, killer): 50/50 XP split on kill
 *   - WritePlayerLogs(quest): appends quest note
 *
 * Test mode: define AI_REWARDS_TEST before including — stubs all engine calls.
 */

#pragma once

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "ai_client.h"   // for AIClient + json

// ---------------------------------------------------------------------------
// Companion enable gate
// ---------------------------------------------------------------------------
namespace ai_companion
{

static inline bool ai_companion_enabled()
{
    // Default ON. Disable via env AI_COMPANION=0 (e.g. in .crawlrc wrapper)
    const char* val = std::getenv("AI_COMPANION");
    if (val == nullptr) return true;          // not set → enabled
    return (val[0] != '0');                   // "0" → disabled, anything else → enabled
}

// ---------------------------------------------------------------------------
// Item name → object class resolution table
// ---------------------------------------------------------------------------
struct ItemSpec
{
    std::string name;
    int base_type;   // maps to object_class_type enum value
    int sub_type;    // -1 means random within class
};

static inline const std::vector<ItemSpec>& item_registry()
{
    static const std::vector<ItemSpec> registry = {
        // Weapons (OBJ_WEAPONS = 0)
        {"short sword",       0,  -1},
        {"long sword",        0,  -1},
        {"dagger",            0,  -1},
        {"axe",               0,  -1},
        {"mace",              0,  -1},
        {"spear",             0,  -1},
        {"staff",             8,  -1},  // OBJ_STAVES = 8

        // Armour (OBJ_ARMOUR = 2)
        {"leather armour",    2,  -1},
        {"ring mail",         2,  -1},
        {"chain mail",        2,  -1},
        {"elven cloak",       2,  -1},
        {"shield",            2,  -1},

        // Potions (OBJ_POTIONS = 7)
        {"healing potion",    7,  -1},
        {"greater healing potion", 7, -1},
        {"mana potion",       7,  -1},
        {"strength potion",   7,  -1},
        {"speed potion",      7,  -1},

        // Scrolls (OBJ_SCROLLS = 5)
        {"scroll of identify", 5, -1},
        {"scroll of teleport", 5, -1},
        {"scroll of blinking", 5, -1},

        // Gold (OBJ_GOLD = 13)
        {"gold",             13,  -1},
    };
    return registry;
}

static inline const ItemSpec* resolve_item(const std::string& name)
{
    if (name.empty()) return nullptr;

    // Case-insensitive substring match (bidirectional)
    std::string lower_name = name;
    for (auto& c : lower_name) c = std::tolower(c);

    for (const auto& spec : item_registry())
    {
        std::string lower_spec = spec.name;
        for (auto& c : lower_spec) c = std::tolower(c);

        if (lower_name.find(lower_spec) != std::string::npos ||
            lower_spec.find(lower_name) != std::string::npos)
            return &spec;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// XP calculation helper
// ---------------------------------------------------------------------------
struct XPShareResult
{
    int shared_xp = 0;
    std::string monster_name;
    int turn = 0;
    bool logged = false;
};

// ---------------------------------------------------------------------------
// Test-mode stubs vs production declarations
// ---------------------------------------------------------------------------
#ifdef AI_REWARDS_TEST

// --- Stub: GrantReward records to a log instead of spawning ---
struct RewardLog
{
    std::string item_name;
    int base_type;
    bool resolved;
};

static inline std::vector<RewardLog>& reward_log()
{
    static std::vector<RewardLog> log;
    return log;
}

static inline void GrantReward(const std::string& item_name)
{
    const ItemSpec* spec = resolve_item(item_name);
    RewardLog entry;
    entry.item_name  = item_name;
    entry.base_type  = spec ? spec->base_type : -1;
    entry.resolved   = (spec != nullptr);
    reward_log().push_back(entry);

    fprintf(stderr, "[AI_COMPANION] REWARD:%s resolved=%s base_type=%d\n",
            item_name.c_str(), entry.resolved ? "yes" : "no", entry.base_type);
}

// --- Stub: XP share calculates but doesn't call gain_exp ---
static inline XPShareResult share_xp_with_ancestor(int raw_exp,
                                                     const std::string& mon_name,
                                                     int turn)
{
    XPShareResult res;
    res.shared_xp    = raw_exp / 2;
    res.monster_name = mon_name;
    res.turn         = turn;
    res.logged       = true;

    fprintf(stderr, "[AI_COMPANION] XP_SHARE:+%d xp from %s (turn %d)\n",
            res.shared_xp, mon_name.c_str(), turn);
    return res;
}

// --- Stub: WritePlayerLogs records quest text ---
static inline std::vector<std::string>& quest_log()
{
    static std::vector<std::string> log;
    return log;
}

static inline void WritePlayerLogs(const std::string& quest_text)
{
    quest_log().push_back(quest_text);
    fprintf(stderr, "[AI_COMPANION] QUEST_LOG: %s\n", quest_text.c_str());
}

#else
// --- Production: engine-linked implementations (compiled with crawl) ---

void GrantReward(const std::string& item_name);

XPShareResult share_xp_with_ancestor(int raw_exp,
                                      const std::string& mon_name,
                                      int turn);

void WritePlayerLogs(const std::string& quest_text);

#endif

// ---------------------------------------------------------------------------
// LLM event narration helper — generates roleplay log for XP/reward events
// ---------------------------------------------------------------------------
static inline std::string narrate_event(const std::string& event_type,
                                         const std::string& details,
                                         const std::string& ancestor_name)
{
    std::string prompt = "You are " + ancestor_name +
        ", a Hepliaklqana ancestor companion. "
        "The following event occurred: " + event_type + " — " + details + ". "
        "Respond in JSON: {\"chat\":\"<1 sentence roleplay>\","
        "\"action\":\"CHAT\",\"payload\":\"\"}";

    std::string raw = AIClient::instance().QueryInternal(prompt);

    try
    {
        json parsed = json::parse(raw);
        return parsed.value("chat", std::string("The journey continues..."));
    }
    catch (...)
    {
        return "The journey continues...";
    }
}

} // namespace ai_companion
