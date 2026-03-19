/**
 * @file ai_client.h
 * @brief AI client interface — routes to ClaudeOrchestrator (default) or
 *        legacy Ollama+Qdrant (behind QDRANT_ENABLED flag).
 *
 * Current sprint: `claude -p` via ClaudeOrchestrator is the sole backend.
 * To restore Ollama/Qdrant, compile with -DQDRANT_ENABLED.
 */

#pragma once

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "contrib/nlohmann/json.hpp"
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Latency tracking helper (shared by both backends)
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

// ===========================================================================
// ACTIVE BACKEND: ClaudeOrchestrator (claude -p subprocess)
// ===========================================================================
#ifndef QDRANT_ENABLED

#include "claude_orchestrator.h"

class AIClient
{
public:
    static AIClient& instance()
    {
        static AIClient client;
        return client;
    }

    AIClient(const AIClient&) = delete;
    AIClient& operator=(const AIClient&) = delete;

    // Embed is a no-op in claude -p mode (no vector DB)
    std::vector<float> Embed(const std::string&)
    {
        return {};
    }

    // SearchLore returns static lore context (no Qdrant)
    std::string SearchLore(const std::vector<float>&)
    {
        return std::string(LORE_CONTEXT);
    }

    // QueryInternal routes to claude -p
    std::string QueryInternal(const std::string& context)
    {
        LatencyTimer timer("CLAUDE_P");
        auto result = ClaudeOrchestrator::instance().query_json(context);
        return result.dump();
    }

private:
    AIClient() = default;
};

#else
// ===========================================================================
// ARCHIVED BACKEND: Ollama + Qdrant (compile with -DQDRANT_ENABLED)
// ===========================================================================

#include <mutex>
#include "contrib/httplib.h"

// ---------------------------------------------------------------------------
// Configuration constants (Ollama + Qdrant)
// ---------------------------------------------------------------------------
static constexpr const char* OLLAMA_HOST  = "http://172.29.64.1:11434";
static constexpr const char* QDRANT_HOST  = "http://localhost:6333";
static constexpr const char* EMBED_MODEL  = "nomic-embed-text";
static constexpr const char* LLM_MODEL    = "PetrosStav/gemma3-tools:4b";
static constexpr const char* LORE_COLLECTION = "crawl_lore";
static constexpr int         EMBED_DIM    = 768;
static constexpr int         SEARCH_LIMIT = 5;
static constexpr int         HTTP_TIMEOUT = 30; // seconds

class AIClient
{
public:
    static AIClient& instance()
    {
        static AIClient client;
        return client;
    }

    AIClient(const AIClient&)            = delete;
    AIClient& operator=(const AIClient&) = delete;

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
            json parsed = json::parse(raw);
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

    static bool is_dry_run()
    {
        static const bool dry = (std::getenv("AI_CLIENT_DRY_RUN") != nullptr);
        return dry;
    }

    static std::vector<float> canned_vector()
    {
        std::vector<float> v(EMBED_DIM, 0.01f);
        v[0] = 1.0f;
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

#endif // QDRANT_ENABLED
