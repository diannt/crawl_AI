/**
 * @file ai_companion.h
 * @brief Hepliaklqana ancestor AI companion — action dispatch, state capture,
 *        prompt construction, and RAG-triggered dialogue pipeline.
 *
 * This header is intentionally self-contained (no engine-only includes) so
 * that the logic can be unit-tested without linking the full game engine.
 * Engine-specific calls are isolated behind thin inline wrappers that the
 * test harness can stub out via compile-time flags.
 *
 * Integration point: mon-speak.cc replaces the ancestor early-return with
 *   ai_companion::maybe_ancestor_speaks(mons);
 *
 * Test mode: define AI_COMPANION_TEST before including this header to get
 *   stub implementations of every engine call.
 */

#pragma once

#include <string>
#include <vector>
#include <map>

#include "ai_client.h"          // AIClient (routes to claude_orchestrator.h)
#include "claude_orchestrator.h" // ClaudeOrchestrator singleton, LORE_CONTEXT

// ---------------------------------------------------------------------------
// Action enum — every action the LLM may request
// ---------------------------------------------------------------------------
enum class AncestorAction
{
    CHAT            = 0,    // Simple dialogue line via mpr
    GRANT_REWARD    = 1,    // Spawn an item for the player
    QUEST_LOG       = 2,    // Write a quest note to player log
    WARN_THREAT     = 3,    // Urgent warning (red channel)
    HEAL_SUGGEST    = 4,    // Suggest healing action (help channel)
    LORE_CITE       = 5,    // Cite lore from RAG results
    NUM_ACTIONS
};

// ---------------------------------------------------------------------------
// Action parsing helpers
// ---------------------------------------------------------------------------
namespace ai_companion
{

static inline AncestorAction parse_action(const std::string& s)
{
    static const std::map<std::string, AncestorAction> action_map = {
        {"CHAT",          AncestorAction::CHAT},
        {"GRANT_REWARD",  AncestorAction::GRANT_REWARD},
        {"QUEST_LOG",     AncestorAction::QUEST_LOG},
        {"WARN_THREAT",   AncestorAction::WARN_THREAT},
        {"HEAL_SUGGEST",  AncestorAction::HEAL_SUGGEST},
        {"LORE_CITE",     AncestorAction::LORE_CITE},
    };
    auto it = action_map.find(s);
    return (it != action_map.end()) ? it->second : AncestorAction::CHAT;
}

static inline std::string action_to_string(AncestorAction a)
{
    switch (a) {
        case AncestorAction::CHAT:         return "CHAT";
        case AncestorAction::GRANT_REWARD: return "GRANT_REWARD";
        case AncestorAction::QUEST_LOG:    return "QUEST_LOG";
        case AncestorAction::WARN_THREAT:  return "WARN_THREAT";
        case AncestorAction::HEAL_SUGGEST: return "HEAL_SUGGEST";
        case AncestorAction::LORE_CITE:    return "LORE_CITE";
        default:                           return "CHAT";
    }
}

// ---------------------------------------------------------------------------
// Game state snapshot — pure data, no engine dependency
// ---------------------------------------------------------------------------
struct GameState
{
    int player_hp       = 100;
    int player_hp_max   = 100;
    std::string location = "Dungeon:1";
    std::vector<std::string> inventory;
    std::vector<std::string> visible_threats;
    int turn            = 0;
    std::string ancestor_name = "The Ancestor";
};

// ---------------------------------------------------------------------------
// State capture — thin wrappers, stubbed in test mode
// ---------------------------------------------------------------------------

#ifdef AI_COMPANION_TEST
// ---- Test stubs: return controllable canned data ----

static inline GameState capture_game_state()
{
    GameState gs;
    gs.player_hp      = 45;
    gs.player_hp_max  = 100;
    gs.location       = "Dungeon:3";
    gs.inventory      = {"iron ration", "healing potion", "short sword"};
    gs.visible_threats = {"orc", "troll"};
    gs.turn           = 42;
    gs.ancestor_name  = "Aldric the Undying";
    return gs;
}

static inline void dispatch_action(AncestorAction action,
                                   const std::string& chat,
                                   const std::string& payload,
                                   std::vector<std::string>& log_out)
{
    // Test dispatch just records what would be done
    std::string entry = "[" + action_to_string(action) + "] " + chat;
    if (!payload.empty())
        entry += " {payload=" + payload + "}";
    log_out.push_back(entry);
}

// Test stub for movement dispatch — logs direction
struct monster;  // forward decl for test builds
static inline void dispatch_companion_movement(monster*, const std::string& direction)
{
    fprintf(stderr, "[AI_COMPANION] TEST_MOVE:%s\n", direction.c_str());
}

// Test stub for commentary dispatch
static inline void dispatch_commentary(const std::string& commentary)
{
    fprintf(stderr, "[AI_COMPANION] TEST_COMMENTARY:%s\n", commentary.c_str());
}

// Test stubs for player-to-companion commands
static inline bool is_companion_command(const std::string& input)
{
    if (input.size() < 3) return false;
    if (input.substr(0, 11) == "@companion ") return true;
    if (input.substr(0, 3) == "/c ") return true;
    return false;
}

static inline std::string extract_companion_message(const std::string& input)
{
    if (input.substr(0, 11) == "@companion ")
        return input.substr(11);
    if (input.substr(0, 3) == "/c ")
        return input.substr(3);
    return input;
}

// player_to_companion test stub is defined after run_companion_pipeline
// (see bottom of file)

#else
// ---- Production: real engine calls (compiled only with game engine) ----

// Forward declarations — these link against the crawl engine
// They are defined in ai_companion.cc
GameState capture_game_state();
void dispatch_action(AncestorAction action,
                     const std::string& chat,
                     const std::string& payload,
                     std::vector<std::string>& log_out);

// Phase 2: Movement dispatch — sets monster target based on claude -p direction
struct monster;  // forward declaration for engine-only builds
void dispatch_companion_movement(monster* mons, const std::string& direction);

// Phase 3: Commentary dispatch — outputs conversational text to message log
void dispatch_commentary(const std::string& commentary);

// Phase 4: Player-to-companion command processing
bool is_companion_command(const std::string& input);
std::string extract_companion_message(const std::string& input);
void player_to_companion(const std::string& player_message);

#endif

// ---------------------------------------------------------------------------
// State → JSON serialisation
// ---------------------------------------------------------------------------
static inline json state_to_json(const GameState& gs)
{
    json inv_arr = json::array();
    for (const auto& item : gs.inventory)
        inv_arr.push_back(item);

    json threats_arr = json::array();
    for (const auto& t : gs.visible_threats)
        threats_arr.push_back(t);

    return {
        {"player_health", {
            {"current", gs.player_hp},
            {"max",     gs.player_hp_max}
        }},
        {"location",       gs.location},
        {"inventory",      inv_arr},
        {"visible_threats", threats_arr},
        {"turn",           gs.turn},
        {"ancestor_name",  gs.ancestor_name}
    };
}

// ---------------------------------------------------------------------------
// Prompt builder — constructs the full LLM prompt from state + lore + input
// ---------------------------------------------------------------------------
static inline std::string build_prompt(const GameState& gs,
                                       const std::string& lore_context,
                                       const std::string& player_input)
{
    json state_json = state_to_json(gs);

    std::string prompt;
    prompt += "System: You are " + gs.ancestor_name +
              ", a Hepliaklqana ancestor companion in Dungeon Crawl Stone Soup. "
              "You are a secondary character on screen who actively fights and moves. "
              "Respond ONLY in valid JSON matching the schema below. "
              "Be concise (max 2 sentences for chat). Stay in character.\n\n";

    if (!lore_context.empty())
    {
        prompt += "Context (Lore):\n";
        std::string trimmed_lore = lore_context.substr(0, 2048);
        prompt += trimmed_lore + "\n\n";
    }

    prompt += "Game State:\n" + state_json.dump(2) + "\n\n";
    prompt += "Player Input: \"" + player_input + "\"\n\n";
    prompt += "Respond in exactly this JSON schema:\n"
              "{\n"
              "  \"chat\": \"<string: your dialogue line>\",\n"
              "  \"action\": \"<one of: CHAT, GRANT_REWARD, QUEST_LOG, "
              "WARN_THREAT, HEAL_SUGGEST, LORE_CITE>\",\n"
              "  \"direction\": \"<one of: N, S, E, W, NE, NW, SE, SW, "
              "ATTACK_<target_name>, STAY, FOLLOW>\",\n"
              "  \"payload\": \"<string: action-specific data, empty for CHAT>\"\n"
              "}\n\n"
              "DIRECTION RULES:\n"
              "- If enemies are visible, use ATTACK_<nearest_threat_name>\n"
              "- If no enemies, move toward unexplored areas (prefer directions with floor '.')\n"
              "- Use FOLLOW to stay near the player\n"
              "- Use STAY if holding position is tactically best\n";

    return prompt;
}

// ---------------------------------------------------------------------------
// Screen-aware prompt builder — includes parsed ASCII screen state
// ---------------------------------------------------------------------------
static inline std::string build_screen_prompt(const GameState& gs,
                                               const std::string& screen_dump,
                                               const std::string& player_input)
{
    json state_json = state_to_json(gs);

    std::string prompt;
    prompt += "System: You are " + gs.ancestor_name +
              ", a Hepliaklqana ancestor companion on screen in DCSS. "
              "You are a secondary character who moves and fights autonomously. "
              "Analyze the ASCII map below and choose your next move. "
              "Respond ONLY in valid JSON. Be concise.\n\n";

    prompt += "Context (Lore):\n" + std::string(LORE_CONTEXT) + "\n\n";
    prompt += "Game State:\n" + state_json.dump(2) + "\n\n";

    if (!screen_dump.empty())
    {
        prompt += "ASCII Map (@ = player, uppercase = monsters, . = floor, # = wall):\n";
        // Truncate to avoid massive prompts
        prompt += screen_dump.substr(0, 2000) + "\n\n";
    }

    prompt += "Player Input: \"" + player_input + "\"\n\n";
    prompt += "Respond in exactly this JSON schema:\n"
              "{\n"
              "  \"chat\": \"<string: your dialogue line>\",\n"
              "  \"action\": \"<one of: CHAT, WARN_THREAT, HEAL_SUGGEST, ATTACK>\",\n"
              "  \"direction\": \"<one of: N, S, E, W, NE, NW, SE, SW, "
              "ATTACK_<target>, STAY, FOLLOW>\",\n"
              "  \"payload\": \"<string: action-specific data>\"\n"
              "}\n\n"
              "DIRECTION RULES:\n"
              "- ATTACK_<name>: engage the named monster\n"
              "- N/S/E/W/NE/NW/SE/SW: move in that direction\n"
              "- FOLLOW: stay adjacent to the player @\n"
              "- STAY: hold current position\n"
              "Choose based on the map: attack if enemies are close, "
              "move toward open areas if exploring, follow player if far away.\n";

    return prompt;
}

// ---------------------------------------------------------------------------
// Commentary prompt — purely conversational, discusses screen context
// ---------------------------------------------------------------------------
static inline std::string build_commentary_prompt(const GameState& gs,
                                                    const std::string& screen_context)
{
    json state_json = state_to_json(gs);

    std::string prompt;
    prompt += "System: You are " + gs.ancestor_name +
              ", a Hepliaklqana ancestor companion in Dungeon Crawl Stone Soup. "
              "You are having a conversation with the player about what you see. "
              "Discuss the current situation: nearby monsters, quests, items, "
              "dungeon features, tactical advice, or lore. "
              "Respond ONLY in valid JSON. Be in character. Max 2 sentences.\n\n";

    prompt += "Lore Context:\n" + std::string(LORE_CONTEXT) + "\n\n";
    prompt += "Game State:\n" + state_json.dump(2) + "\n\n";

    if (!screen_context.empty())
        prompt += "What you see:\n" + screen_context + "\n\n";

    prompt += "Respond in exactly this JSON schema:\n"
              "{\n"
              "  \"commentary\": \"<string: your 1-2 sentence observation about "
              "the environment, quest, monsters, or tactical situation>\"\n"
              "}\n";

    return prompt;
}

// ---------------------------------------------------------------------------
// Commentary response struct + pipeline
// ---------------------------------------------------------------------------
struct CommentaryResponse
{
    std::string commentary;
    double llm_ms = 0;
    bool success = false;
};

static inline CommentaryResponse run_commentary_pipeline(const GameState& state)
{
    CommentaryResponse resp;
    resp.commentary = "";

    // Build context description from game state
    std::string context;
    if (!state.visible_threats.empty())
    {
        context += "Visible enemies: ";
        for (size_t i = 0; i < state.visible_threats.size(); ++i)
        {
            if (i > 0) context += ", ";
            context += state.visible_threats[i];
        }
        context += ". ";
    }
    else
    {
        context += "No enemies in sight. ";
    }

    context += "Location: " + state.location + ". ";
    context += "HP: " + std::to_string(state.player_hp) + "/"
             + std::to_string(state.player_hp_max) + ". ";
    context += "Turn: " + std::to_string(state.turn) + ". ";

    if (!state.inventory.empty())
    {
        context += "Carrying: ";
        size_t count = std::min(state.inventory.size(), (size_t)5);
        for (size_t i = 0; i < count; ++i)
        {
            if (i > 0) context += ", ";
            context += state.inventory[i];
        }
        context += ". ";
    }

    // Build and query
    std::string prompt = build_commentary_prompt(state, context);

    auto t0 = std::chrono::steady_clock::now();
    json result = ClaudeOrchestrator::instance().query_json(prompt);
    auto t1 = std::chrono::steady_clock::now();
    resp.llm_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    try
    {
        resp.commentary = result.value("commentary",
                            result.value("chat", std::string("")));
        resp.success = !resp.commentary.empty();
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "[AI_COMPANION] commentary parse error: %s\n", e.what());
    }

    fprintf(stderr, "[AI_COMPANION] commentary: %.1fms success=%s\n",
            resp.llm_ms, resp.success ? "yes" : "no");

    return resp;
}

// ---------------------------------------------------------------------------
// Full pipeline: input → prompt → claude -p → parse → dispatch
// (Replaces old embed → RAG → Ollama pipeline)
// ---------------------------------------------------------------------------
struct CompanionResponse
{
    std::string chat;
    AncestorAction action;
    std::string direction;     // N/S/E/W/NE/NW/SE/SW/ATTACK_<target>/STAY/FOLLOW
    std::string payload;
    double llm_ms = 0;
};

// ---------------------------------------------------------------------------
// Direction → DCSS key mapping
// ---------------------------------------------------------------------------
static inline char direction_to_key(const std::string& dir)
{
    // DCSS vi-key movement
    if (dir == "N")  return 'k';
    if (dir == "S")  return 'j';
    if (dir == "E")  return 'l';
    if (dir == "W")  return 'h';
    if (dir == "NE") return 'u';
    if (dir == "NW") return 'y';
    if (dir == "SE") return 'n';
    if (dir == "SW") return 'b';
    if (dir == "STAY") return '.';
    if (dir == "FOLLOW") return 'k'; // default: move toward player (north)
    return '.'; // default: wait
}

static inline bool is_attack_direction(const std::string& dir)
{
    return dir.substr(0, 7) == "ATTACK_";
}

static inline std::string attack_target(const std::string& dir)
{
    if (is_attack_direction(dir))
        return dir.substr(7);
    return "";
}

// ---------------------------------------------------------------------------
// Main pipeline: input → prompt → claude -p → parse → dispatch
// ---------------------------------------------------------------------------
static inline CompanionResponse run_companion_pipeline(const std::string& player_input,
                                                        const GameState& state)
{
    CompanionResponse resp;
    resp.action    = AncestorAction::CHAT;
    resp.direction = "FOLLOW";
    resp.chat      = "I sense the dungeon stirs...";

    // --- Step 1: Build prompt with inline lore context ---
    std::string prompt = build_prompt(state, std::string(LORE_CONTEXT), player_input);

    // --- Step 2: Query via claude -p ---
    auto t0 = std::chrono::steady_clock::now();
    json result = ClaudeOrchestrator::instance().query_json(prompt);
    auto t1 = std::chrono::steady_clock::now();
    resp.llm_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // --- Step 3: Parse response ---
    try
    {
        resp.chat      = result.value("chat", resp.chat);
        std::string act_str = result.value("action", std::string("CHAT"));
        resp.action    = parse_action(act_str);
        resp.direction = result.value("direction", std::string("FOLLOW"));
        resp.payload   = result.value("payload", std::string(""));
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "[AI_COMPANION] parse error in pipeline: %s\n", e.what());
    }

    fprintf(stderr, "[AI_COMPANION] pipeline: claude_p=%.1fms action=%s dir=%s\n",
            resp.llm_ms, action_to_string(resp.action).c_str(),
            resp.direction.c_str());

    return resp;
}

// ---------------------------------------------------------------------------
// Screen-aware pipeline: uses parsed screen dump for spatial reasoning
// ---------------------------------------------------------------------------
static inline CompanionResponse run_screen_pipeline(const std::string& screen_dump,
                                                     const std::string& player_input,
                                                     const GameState& state)
{
    CompanionResponse resp;
    resp.action    = AncestorAction::CHAT;
    resp.direction = "FOLLOW";
    resp.chat      = "I sense the dungeon stirs...";

    // Build screen-aware prompt
    std::string prompt = build_screen_prompt(state, screen_dump, player_input);

    // Query via claude -p
    auto t0 = std::chrono::steady_clock::now();
    json result = ClaudeOrchestrator::instance().query_json(prompt);
    auto t1 = std::chrono::steady_clock::now();
    resp.llm_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    try
    {
        resp.chat      = result.value("chat", resp.chat);
        std::string act_str = result.value("action", std::string("CHAT"));
        resp.action    = parse_action(act_str);
        resp.direction = result.value("direction", std::string("FOLLOW"));
        resp.payload   = result.value("payload", std::string(""));
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "[AI_COMPANION] screen parse error: %s\n", e.what());
    }

    fprintf(stderr, "[AI_COMPANION] screen_pipeline: claude_p=%.1fms action=%s dir=%s\n",
            resp.llm_ms, action_to_string(resp.action).c_str(),
            resp.direction.c_str());

    return resp;
}

// ---------------------------------------------------------------------------
// Test stub for player_to_companion (must come after run_companion_pipeline)
// ---------------------------------------------------------------------------
#ifdef AI_COMPANION_TEST
static inline void player_to_companion(const std::string& player_message)
{
    std::string message;
    if (player_message.substr(0, 11) == "@companion ")
        message = player_message.substr(11);
    else if (player_message.substr(0, 3) == "/c ")
        message = player_message.substr(3);
    else
        message = player_message;

    GameState gs = capture_game_state();
    auto response = run_companion_pipeline(message, gs);
    std::vector<std::string> action_log;
    dispatch_action(response.action, response.chat, response.payload, action_log);
    fprintf(stderr, "[AI_COMPANION] TEST_PLAYER_CMD:'%s'\n", message.c_str());
}
#endif

} // namespace ai_companion
