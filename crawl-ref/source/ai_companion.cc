/**
 * @file ai_companion.cc
 * @brief Production engine-linked implementations for ai_companion functions.
 *        Compiled only as part of the full crawl build (not in test mode).
 */

#include "AppHdr.h"

#include "ai_companion.h"

#include "branch.h"
#include "message.h"
#include "monster.h"
#include "nearby-danger.h"
#include "place.h"
#include "player.h"
#include "religion.h"
#include "view.h"

namespace ai_companion
{

// Helper: cached ancestor name for dispatch messages
static const std::string& cached_ancestor_name()
{
    static std::string name;
    name = hepliaklqana_ally_name();
    if (name.empty()) name = "Ancestor";
    return name;
}

GameState capture_game_state()
{
    GameState gs;

    gs.player_hp     = you.hp;
    gs.player_hp_max = you.hp_max;
    level_id cur = level_id::current();
    gs.location  = branches[cur.branch].shortname
                   + std::string(":") + to_string(cur.depth);
    gs.turn      = static_cast<int>(you.num_turns);
    gs.ancestor_name = cached_ancestor_name();

    // Inventory: top-level item names
    for (int i = 0; i < ENDOFPACK; ++i)
    {
        if (you.inv[i].defined())
            gs.inventory.push_back(you.inv[i].name(DESC_A));
    }

    // Visible threats: hostile monsters in range
    vector<monster*> nearby = get_nearby_monsters(true);
    for (monster* m : nearby)
    {
        if (m && m->temp_attitude() == ATT_HOSTILE)
            gs.visible_threats.push_back(m->name(DESC_PLAIN));
    }

    return gs;
}

void dispatch_action(AncestorAction action,
                     const std::string& chat,
                     const std::string& payload,
                     std::vector<std::string>& log_out)
{
    // Log for observation harness
    std::string entry = "[" + action_to_string(action) + "] " + chat;
    if (!payload.empty())
        entry += " {payload=" + payload + "}";
    log_out.push_back(entry);

    const std::string& name = cached_ancestor_name();

    // Production dispatch: print companion speech to game log
    switch (action)
    {
    case AncestorAction::WARN_THREAT:
        mprf(MSGCH_WARN, "[%s] %s", name.c_str(), chat.c_str());
        break;
    case AncestorAction::CHAT:
    case AncestorAction::HEAL_SUGGEST:
    case AncestorAction::GRANT_REWARD:
    case AncestorAction::QUEST_LOG:
    case AncestorAction::LORE_CITE:
    default:
        mprf(MSGCH_PLAIN, "[%s] %s", name.c_str(), chat.c_str());
        break;
    }

    fprintf(stderr, "[AI_COMPANION] DISPATCH:[%s] %s\n",
            action_to_string(action).c_str(), chat.c_str());
}

} // namespace ai_companion
