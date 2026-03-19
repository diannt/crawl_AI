/**
 * @file claude_orchestrator.h
 * @brief C++ orchestrator that calls `claude -p` as a subprocess for AI
 *        companion responses. Replaces the Ollama HTTP pipeline.
 *
 * All prompts are single-message strings executed from WORK_FOLDER (repo root).
 * Uses popen() for synchronous calls; Phase V upgrades to async fork+pipe.
 *
 * Test mode: define CLAUDE_ORCH_TEST before including to get stub responses.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "contrib/nlohmann/json.hpp"
using json = nlohmann::json;

// Work folder — repo root where `claude -p` must execute
static constexpr const char* CLAUDE_WORK_FOLDER = "/home/maatru/dev/code_pref/model_a";
static constexpr int CLAUDE_TIMEOUT_SEC = 60;

// ---------------------------------------------------------------------------
// Static lore excerpt (replaces RAG pipeline for this sprint)
// ---------------------------------------------------------------------------
static const char* LORE_CONTEXT =
    "Dungeon Crawl Stone Soup: A roguelike game of dungeon exploration. "
    "Hepliaklqana is a god of ancestral memory who grants an ancestor companion. "
    "The ancestor fights alongside the player and can be a hexer, knight, or battlemage. "
    "Key threats: ogres hit hard, hydras have multiple attacks, unique monsters are dangerous. "
    "Healing potions restore HP. Scrolls of teleportation provide escape. "
    "The Orb of Zot is the ultimate goal, found in the deepest level of the dungeon. "
    "Players must manage resources carefully - food, potions, scrolls are limited.";

// ---------------------------------------------------------------------------
// ClaudeOrchestrator — subprocess wrapper for `claude -p`
// ---------------------------------------------------------------------------
class ClaudeOrchestrator
{
public:
    static ClaudeOrchestrator& instance()
    {
        static ClaudeOrchestrator orch;
        return orch;
    }

    ClaudeOrchestrator(const ClaudeOrchestrator&) = delete;
    ClaudeOrchestrator& operator=(const ClaudeOrchestrator&) = delete;

    struct QueryResult
    {
        std::string output;
        double elapsed_ms = 0;
        bool success = false;
        std::string error;
    };

    /**
     * Execute a `claude -p` prompt and return the raw output.
     * Runs from WORK_FOLDER. Single-message prompt only.
     */
    QueryResult query(const std::string& prompt)
    {
#ifdef CLAUDE_ORCH_TEST
        return test_query(prompt);
#else
        return exec_claude(prompt);
#endif
    }

    /**
     * Query claude -p and parse the output as JSON.
     * Returns fallback JSON on parse failure.
     */
    json query_json(const std::string& prompt)
    {
        QueryResult result = query(prompt);
        if (!result.success)
        {
            fprintf(stderr, "[CLAUDE_ORCH] query failed: %s\n", result.error.c_str());
            return fallback_json();
        }

        // Try to extract JSON from the response (claude may wrap it in text)
        std::string json_str = extract_json(result.output);
        try
        {
            json parsed = json::parse(json_str);
            if (!parsed.contains("chat"))   parsed["chat"]   = "The dungeon stirs...";
            if (!parsed.contains("action")) parsed["action"] = "CHAT";
            if (!parsed.contains("payload")) parsed["payload"] = "";
            return parsed;
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "[CLAUDE_ORCH] JSON parse error: %s\nRaw: %.200s\n",
                    e.what(), result.output.c_str());
            // If we can't parse JSON, use the raw output as chat text
            json resp = fallback_json();
            if (!result.output.empty())
                resp["chat"] = result.output.substr(0, 200);
            return resp;
        }
    }

private:
    ClaudeOrchestrator() = default;

    QueryResult exec_claude(const std::string& prompt)
    {
        QueryResult result;
        auto t0 = std::chrono::steady_clock::now();

        // Escape single quotes in prompt for shell safety
        std::string escaped = escape_for_shell(prompt);

        // Build command: cd to WORK_FOLDER and run claude -p
        std::string cmd = "cd " + std::string(CLAUDE_WORK_FOLDER)
                        + " && claude -p '" + escaped + "' 2>/dev/null";

        fprintf(stderr, "[CLAUDE_ORCH] Executing claude -p (prompt length: %zu)\n",
                prompt.size());

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe)
        {
            result.error = "popen() failed";
            return result;
        }

        // Read output
        std::array<char, 4096> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
            result.output += buffer.data();

        int status = pclose(pipe);
        auto t1 = std::chrono::steady_clock::now();
        result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (status == 0)
        {
            result.success = true;
            fprintf(stderr, "[CLAUDE_ORCH] Success: %.1fms, output=%zu bytes\n",
                    result.elapsed_ms, result.output.size());
        }
        else
        {
            result.error = "claude -p exited with status " + std::to_string(status);
            fprintf(stderr, "[CLAUDE_ORCH] Failed: %s\n", result.error.c_str());
            // Still set success if we got output (non-zero exit but useful text)
            if (!result.output.empty())
                result.success = true;
        }

        return result;
    }

    // Escape single quotes for bash: replace ' with '\''
    static std::string escape_for_shell(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 32);
        for (char c : s)
        {
            if (c == '\'')
                out += "'\\''";
            else
                out += c;
        }
        return out;
    }

    // Extract first JSON object from text (claude may wrap in markdown/prose)
    static std::string extract_json(const std::string& text)
    {
        // Find first { and last }
        auto first = text.find('{');
        auto last = text.rfind('}');
        if (first != std::string::npos && last != std::string::npos && last > first)
            return text.substr(first, last - first + 1);
        return text;
    }

    static json fallback_json()
    {
        return {
            {"chat", "I sense danger ahead, adventurer."},
            {"action", "CHAT"},
            {"payload", ""}
        };
    }

#ifdef CLAUDE_ORCH_TEST
    static QueryResult test_query(const std::string& prompt)
    {
        QueryResult result;
        result.success = true;
        result.elapsed_ms = 1.0;

        json resp = {
            {"chat", "Test: I shall guide you through these halls."},
            {"action", "CHAT"},
            {"payload", ""}
        };

        // Context-aware test responses
        if (prompt.find("threat") != std::string::npos ||
            prompt.find("monster") != std::string::npos)
        {
            resp["chat"] = "Test: Beware, I sense hostile creatures nearby!";
            resp["action"] = "WARN_THREAT";
        }
        else if (prompt.find("health") != std::string::npos ||
                 prompt.find("HP") != std::string::npos)
        {
            resp["chat"] = "Test: You should heal before proceeding.";
            resp["action"] = "HEAL_SUGGEST";
        }

        result.output = resp.dump();
        fprintf(stderr, "[CLAUDE_ORCH] TEST: action=%s\n",
                resp["action"].get<std::string>().c_str());
        return result;
    }
#endif
};
