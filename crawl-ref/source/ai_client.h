/**
 * @file ai_client.h
 * @brief Thread-safe singleton HTTP client for LLM + vector DB access.
 *
 * Provides:
 *   - Embed(): text → 768-dim embedding via nomic-embed-text on Ollama
 *   - SearchLore(): embedding → concatenated lore text from Qdrant
 *   - QueryInternal(): prompt → JSON-structured LLM response via gemma3-tools
 *
 * Set env AI_CLIENT_DRY_RUN=1 to skip real HTTP calls (returns canned data).
 * All methods are thread-safe via internal mutex.
 */

#pragma once

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "contrib/httplib.h"
#include "contrib/nlohmann/json.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------
static constexpr const char* OLLAMA_HOST  = "http://172.29.64.1:11434";
static constexpr const char* QDRANT_HOST  = "http://localhost:6333";
static constexpr const char* EMBED_MODEL  = "nomic-embed-text";
static constexpr const char* LLM_MODEL    = "PetrosStav/gemma3-tools:4b";
static constexpr const char* LORE_COLLECTION = "crawl_lore";
static constexpr int         EMBED_DIM    = 768;
static constexpr int         SEARCH_LIMIT = 5;
static constexpr int         HTTP_TIMEOUT = 30; // seconds

// ---------------------------------------------------------------------------
// Latency tracking helper
// ---------------------------------------------------------------------------
struct LatencyTimer
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_;
    const char*       label_;

    explicit LatencyTimer(const char* label) : start_(Clock::now()), label_(label) {}

    double elapsed_ms() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }

    ~LatencyTimer()
    {
        fprintf(stderr, "[AI_COMPANION] %s: %.1f ms\n", label_, elapsed_ms());
    }
};

// ---------------------------------------------------------------------------
// AIClient — singleton
// ---------------------------------------------------------------------------
class AIClient
{
public:
    static AIClient& instance()
    {
        // C++11 guaranteed thread-safe local static initialization (Meyer's singleton)
        static AIClient client;
        return client;
    }

    // Disallow copy/move
    AIClient(const AIClient&)            = delete;
    AIClient& operator=(const AIClient&) = delete;

    /**
     * Embed a text string into a 768-dimensional vector.
     * Returns empty vector on failure.
     */
    std::vector<float> Embed(const std::string& text)
    {
        if (is_dry_run()) return canned_vector();

        LatencyTimer timer("EMBED");
        std::lock_guard<std::mutex> lock(mtx_);

        json body = {
            {"model", EMBED_MODEL},
            {"prompt", text}
        };

        auto res = ollama_.Post("/api/embeddings",
                                body.dump(),
                                "application/json");
        if (!res || res->status != 200)
        {
            fprintf(stderr, "[AI_CLIENT] embed_error: HTTP %d\n",
                    res ? res->status : -1);
            return {};
        }

        try
        {
            json resp = json::parse(res->body);
            auto& emb = resp.at("embedding");
            std::vector<float> vec;
            vec.reserve(emb.size());
            for (auto& v : emb)
                vec.push_back(static_cast<float>(v.get<double>()));
            return vec;
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "[AI_CLIENT] embed_parse_error: %s\n", e.what());
            return {};
        }
    }

    /**
     * Search Qdrant crawl_lore collection with a query vector.
     * Returns concatenated payload.text from top-k results.
     */
    std::string SearchLore(const std::vector<float>& vec)
    {
        if (vec.empty()) return "";
        if (is_dry_run()) return canned_lore();

        LatencyTimer timer("RAG");
        std::lock_guard<std::mutex> lock(mtx_);

        json vec_arr = json::array();
        for (float f : vec) vec_arr.push_back(f);

        json body = {
            {"vector", vec_arr},
            {"limit", SEARCH_LIMIT},
            {"with_payload", true}
        };

        std::string path = "/collections/" + std::string(LORE_COLLECTION)
                           + "/points/search";
        auto res = qdrant_.Post(path, body.dump(), "application/json");

        if (!res || res->status != 200)
        {
            fprintf(stderr, "[AI_CLIENT] search_error: HTTP %d\n",
                    res ? res->status : -1);
            return "";
        }

        try
        {
            json resp   = json::parse(res->body);
            auto& hits  = resp.at("result");
            std::string merged;
            for (size_t i = 0; i < hits.size(); ++i)
            {
                if (i > 0) merged += "\n---\n";
                merged += hits[i].at("payload").at("text").get<std::string>();
            }
            fprintf(stderr, "[AI_COMPANION] RAG: %zu results\n", hits.size());
            return merged;
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "[AI_CLIENT] search_parse_error: %s\n", e.what());
            return "";
        }
    }

    /**
     * Send a structured prompt to the LLM and get a JSON response.
     * On failure or parse error, returns a safe fallback JSON.
     */
    std::string QueryInternal(const std::string& context)
    {
        if (is_dry_run()) return canned_llm_response();

        LatencyTimer timer("LLM");
        std::lock_guard<std::mutex> lock(mtx_);

        json body = {
            {"model", LLM_MODEL},
            {"prompt", context},
            {"stream", false},
            {"format", "json"}
        };

        auto res = ollama_.Post("/api/generate",
                                body.dump(),
                                "application/json");
        if (!res || res->status != 200)
        {
            fprintf(stderr, "[AI_CLIENT] llm_error: HTTP %d\n",
                    res ? res->status : -1);
            return fallback_response();
        }

        try
        {
            json resp = json::parse(res->body);
            std::string raw = resp.at("response").get<std::string>();

            // Validate it's parseable JSON
            json parsed = json::parse(raw);
            // Ensure required fields exist; default missing ones
            if (!parsed.contains("chat"))   parsed["chat"]    = "I sense the dungeon stirs...";
            if (!parsed.contains("action")) parsed["action"]  = "CHAT";
            if (!parsed.contains("payload")) parsed["payload"] = "";

            fprintf(stderr, "[AI_COMPANION] LLM action=%s\n",
                    parsed["action"].get<std::string>().c_str());
            return parsed.dump();
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "[AI_CLIENT] llm_parse_error: %s\n", e.what());
            return fallback_response();
        }
    }

private:
    AIClient()
        : ollama_(OLLAMA_HOST)
        , qdrant_(QDRANT_HOST)
    {
        ollama_.set_read_timeout(HTTP_TIMEOUT, 0);
        qdrant_.set_read_timeout(HTTP_TIMEOUT, 0);
    }

    std::mutex       mtx_;
    httplib::Client  ollama_;
    httplib::Client  qdrant_;

    // -----------------------------------------------------------------------
    // Dry-run helpers (for testing without network)
    // -----------------------------------------------------------------------
    static bool is_dry_run()
    {
        static const bool dry = (std::getenv("AI_CLIENT_DRY_RUN") != nullptr);
        return dry;
    }

    static std::vector<float> canned_vector()
    {
        std::vector<float> v(EMBED_DIM, 0.01f);
        v[0] = 1.0f; // unit-ish for cosine compat
        fprintf(stderr, "[AI_COMPANION] EMBED: DRY_RUN %d dims\n", EMBED_DIM);
        return v;
    }

    static std::string canned_lore()
    {
        fprintf(stderr, "[AI_COMPANION] RAG: DRY_RUN 1 result\n");
        return "The dungeon holds many secrets. Beware of traps and hostile creatures.";
    }

    static std::string canned_llm_response()
    {
        json resp = {
            {"chat", "I shall guide you through these treacherous halls, adventurer."},
            {"action", "CHAT"},
            {"payload", ""}
        };
        fprintf(stderr, "[AI_COMPANION] LLM: DRY_RUN action=CHAT\n");
        return resp.dump();
    }

    static std::string fallback_response()
    {
        json resp = {
            {"chat", "I sense danger ahead, adventurer."},
            {"action", "CHAT"},
            {"payload", ""}
        };
        return resp.dump();
    }
};
