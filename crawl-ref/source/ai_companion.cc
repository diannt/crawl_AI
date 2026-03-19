/**
 * @file ai_companion.cc
 * @brief Production engine-linked implementations for ai_companion functions.
 *        Compiled only as part of the full crawl build (not in test mode).
 */

#include "AppHdr.h"

#include "ai_companion.h"

#include "branch.h"
#include "coord.h"
#include "god-companions.h"
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

    // Production dispatch: all companion output formatted as "Companion says:"
    switch (action)
    {
    case AncestorAction::WARN_THREAT:
        mprf(MSGCH_WARN, "Companion says: %s", chat.c_str());
        break;
    case AncestorAction::HEAL_SUGGEST:
        mprf(MSGCH_PLAIN, "Companion says: %s", chat.c_str());
        break;
    case AncestorAction::CHAT:
    case AncestorAction::GRANT_REWARD:
    case AncestorAction::QUEST_LOG:
    case AncestorAction::LORE_CITE:
    default:
        mprf(MSGCH_PLAIN, "Companion says: %s", chat.c_str());
        break;
    }

    fprintf(stderr, "[AI_COMPANION] DISPATCH:[%s] %s\n",
            action_to_string(action).c_str(), chat.c_str());
}

// ---------------------------------------------------------------------------
// Commentary dispatch — outputs conversational text to DCSS message log
// ---------------------------------------------------------------------------
void dispatch_commentary(const std::string& commentary)
{
    if (commentary.empty()) return;
    const std::string& name = cached_ancestor_name();
    mprf(MSGCH_PLAIN, "Companion says: %s", commentary.c_str());
    fprintf(stderr, "[AI_COMPANION] COMMENTARY: %s\n", commentary.c_str());
}

// ---------------------------------------------------------------------------
// Direction → coord_def delta mapping
// ---------------------------------------------------------------------------
static coord_def direction_to_delta(const std::string& dir)
{
    if (dir == "N")  return coord_def( 0, -1);
    if (dir == "S")  return coord_def( 0,  1);
    if (dir == "E")  return coord_def( 1,  0);
    if (dir == "W")  return coord_def(-1,  0);
    if (dir == "NE") return coord_def( 1, -1);
    if (dir == "NW") return coord_def(-1, -1);
    if (dir == "SE") return coord_def( 1,  1);
    if (dir == "SW") return coord_def(-1,  1);
    return coord_def(0, 0); // STAY
}

// ---------------------------------------------------------------------------
// dispatch_companion_movement — sets ancestor monster target based on
// claude -p directional output.
//
// For ATTACK_<target>: finds the named monster and targets it.
// For directional (N/S/E/W/etc): sets target to adjacent cell.
// For FOLLOW: targets the player position.
// For STAY: no target change.
// ---------------------------------------------------------------------------
void dispatch_companion_movement(::monster* mons, const std::string& direction)
{
    if (!mons) return;

    if (direction == "FOLLOW")
    {
        // Move toward the player
        mons->target = you.pos();
        mons->foe = MHITYOU; // friendly following player
        fprintf(stderr, "[AI_COMPANION] MOVE:FOLLOW -> player at (%d,%d)\n",
                you.pos().x, you.pos().y);
        return;
    }

    if (direction == "STAY")
    {
        mons->target = mons->pos();
        fprintf(stderr, "[AI_COMPANION] MOVE:STAY at (%d,%d)\n",
                mons->pos().x, mons->pos().y);
        return;
    }

    // ATTACK_<target> — find the named monster and target it
    if (direction.substr(0, 7) == "ATTACK_")
    {
        std::string target_name = direction.substr(7);
        // Lowercase for comparison
        std::string lower_target = target_name;
        for (auto& c : lower_target) c = std::tolower(c);

        vector<monster*> nearby = get_nearby_monsters(true);
        for (monster* m : nearby)
        {
            if (!m || m->temp_attitude() != ATT_HOSTILE) continue;
            std::string mname = m->name(DESC_PLAIN);
            std::string lower_mname = mname;
            for (auto& c : lower_mname) c = std::tolower(c);

            if (lower_mname.find(lower_target) != std::string::npos)
            {
                mons->target = m->pos();
                mons->foe = m->mindex();
                fprintf(stderr, "[AI_COMPANION] MOVE:ATTACK_%s -> (%d,%d)\n",
                        mname.c_str(), m->pos().x, m->pos().y);
                return;
            }
        }
        // Target not found, fallback to follow
        fprintf(stderr, "[AI_COMPANION] MOVE:ATTACK_%s target not found, following player\n",
                target_name.c_str());
        mons->target = you.pos();
        return;
    }

    // Directional movement (N/S/E/W/NE/NW/SE/SW)
    coord_def delta = direction_to_delta(direction);
    if (delta.origin())
    {
        // Unknown direction, stay put
        fprintf(stderr, "[AI_COMPANION] MOVE:unknown dir '%s', staying\n",
                direction.c_str());
        return;
    }

    coord_def new_target = mons->pos() + delta;
    mons->target = new_target;
    fprintf(stderr, "[AI_COMPANION] MOVE:%s -> (%d,%d)\n",
            direction.c_str(), new_target.x, new_target.y);
}

// ---------------------------------------------------------------------------
// Player-to-companion command processing
// Detects "@companion <msg>" or "/c <msg>" in player input.
// Routes to claude -p with full game context.
// ---------------------------------------------------------------------------
bool is_companion_command(const std::string& input)
{
    if (input.size() < 3) return false;
    // Check for "@companion " or "/c " prefix
    if (input.substr(0, 11) == "@companion ") return true;
    if (input.substr(0, 3) == "/c ") return true;
    return false;
}

std::string extract_companion_message(const std::string& input)
{
    if (input.substr(0, 11) == "@companion ")
        return input.substr(11);
    if (input.substr(0, 3) == "/c ")
        return input.substr(3);
    return input;
}

void player_to_companion(const std::string& player_message)
{
    GameState gs = capture_game_state();
    std::string message = extract_companion_message(player_message);

    // Run the full pipeline with the player's actual message
    auto response = run_companion_pipeline(message, gs);

    // Dispatch response formatted as "Companion says:"
    std::vector<std::string> action_log;
    dispatch_action(response.action, response.chat, response.payload, action_log);

    fprintf(stderr, "[AI_COMPANION] PLAYER_CMD:'%s' -> action=%s dir=%s\n",
            message.c_str(), action_to_string(response.action).c_str(),
            response.direction.c_str());
}

} // namespace ai_companion
