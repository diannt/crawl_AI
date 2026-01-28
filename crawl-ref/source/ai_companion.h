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

#include "ai_client.h"   // AIClient singleton, json

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

#else
// ---- Production: real engine calls (compiled only with game engine) ----

// Forward declarations — these link against the crawl engine
// They are defined in ai_companion.cc (see Phase 4 / engine linkage)
GameState capture_game_state();
void dispatch_action(AncestorAction action,
                     const std::string& chat,
                     const std::string& payload,
                     std::vector<std::string>& log_out);

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
              "You are a wise guide who helps the player navigate the dungeon. "
              "Respond ONLY in valid JSON matching the schema below. "
              "Be concise (max 2 sentences for chat). Stay in character.\n\n";

    if (!lore_context.empty())
    {
        prompt += "Context (Lore from game manual):\n";
        // Truncate lore to avoid overwhelming the 4b model context window
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
              "  \"payload\": \"<string: action-specific data, empty for CHAT>\"\n"
              "}\n";

    return prompt;
}

// ---------------------------------------------------------------------------
// Full pipeline: input → embed → RAG → prompt → LLM → parse → dispatch
// ---------------------------------------------------------------------------
struct CompanionResponse
{
    std::string chat;
    AncestorAction action;
    std::string payload;
    double embed_ms   = 0;
    double search_ms  = 0;
    double llm_ms     = 0;
};

static inline CompanionResponse run_companion_pipeline(const std::string& player_input,
                                                        const GameState& state)
{
    CompanionResponse resp;
    resp.action = AncestorAction::CHAT;
    resp.chat   = "I sense the dungeon stirs...";

    // --- Step 1: Embed player input ---
    auto t0 = std::chrono::steady_clock::now();
    auto vec = AIClient::instance().Embed(player_input);
    auto t1 = std::chrono::steady_clock::now();
    resp.embed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (vec.empty())
    {
        fprintf(stderr, "[AI_COMPANION] embed failed — using fallback dialogue\n");
        return resp;
    }

    // --- Step 2: RAG search ---
    auto t2 = std::chrono::steady_clock::now();
    std::string lore = AIClient::instance().SearchLore(vec);
    auto t3 = std::chrono::steady_clock::now();
    resp.search_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // --- Step 3: Build prompt ---
    std::string prompt = build_prompt(state, lore, player_input);

    // --- Step 4: LLM query ---
    auto t4 = std::chrono::steady_clock::now();
    std::string llm_raw = AIClient::instance().QueryInternal(prompt);
    auto t5 = std::chrono::steady_clock::now();
    resp.llm_ms = std::chrono::duration<double, std::milli>(t5 - t4).count();

    // --- Step 5: Parse response ---
    try
    {
        json parsed = json::parse(llm_raw);
        resp.chat    = parsed.value("chat", resp.chat);
        std::string act_str = parsed.value("action", std::string("CHAT"));
        resp.action  = parse_action(act_str);
        resp.payload = parsed.value("payload", std::string(""));
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "[AI_COMPANION] parse error in pipeline: %s\n", e.what());
        // Keep defaults: CHAT with fallback message
    }

    fprintf(stderr, "[AI_COMPANION] pipeline: embed=%.1fms search=%.1fms llm=%.1fms action=%s\n",
            resp.embed_ms, resp.search_ms, resp.llm_ms, action_to_string(resp.action).c_str());

    return resp;
}

} // namespace ai_companion
