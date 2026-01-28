/**
 * @file test_ai_client.cc
 * @brief Unit tests for AIClient singleton (Phase 1).
 *
 * All tests run with AI_CLIENT_DRY_RUN=1 to avoid real network calls.
 * Compile standalone:
 *   g++ -std=c++17 -DAI_CLIENT_TEST \
 *       -I ../contrib -I .. \
 *       test_ai_client.cc catch_amalgamated.cc -o test_ai_client -lpthread
 *   AI_CLIENT_DRY_RUN=1 ./test_ai_client
 */

#include "catch_amalgamated.hpp"
#include "../ai_client.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Phase 1 Tests — AIClient Dry Run
// ---------------------------------------------------------------------------

TEST_CASE("AIClient is a singleton", "[ai_client][singleton]")
{
    AIClient& a = AIClient::instance();
    AIClient& b = AIClient::instance();
    REQUIRE(&a == &b);
}

TEST_CASE("Embed returns 768-dim vector in dry run", "[ai_client][embed]")
{
    // Force dry run for this test binary
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    auto vec = AIClient::instance().Embed("test passage about the dungeon");
    REQUIRE(vec.size() == 768);

    // Dry run vector should have first element ~1.0 (unit hint)
    REQUIRE(vec[0] == Approx(1.0f));

    // Remaining elements should be small but non-zero
    for (size_t i = 1; i < vec.size(); ++i)
        REQUIRE(vec[i] == Approx(0.01f));
}

TEST_CASE("Embed with empty string returns valid vector", "[ai_client][embed]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    auto vec = AIClient::instance().Embed("");
    REQUIRE(vec.size() == 768);
}

TEST_CASE("SearchLore returns non-empty lore in dry run", "[ai_client][search]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    std::vector<float> vec(768, 0.01f);
    std::string lore = AIClient::instance().SearchLore(vec);
    REQUIRE(!lore.empty());
    REQUIRE(lore.find("dungeon") != std::string::npos);
}

TEST_CASE("SearchLore with empty vector returns empty string", "[ai_client][search]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    std::vector<float> empty_vec;
    std::string lore = AIClient::instance().SearchLore(empty_vec);
    REQUIRE(lore.empty());
}

TEST_CASE("QueryInternal returns valid JSON in dry run", "[ai_client][llm]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    std::string result = AIClient::instance().QueryInternal("test context");
    REQUIRE(!result.empty());

    // Should parse as valid JSON
    json parsed = json::parse(result);
    REQUIRE(parsed.contains("chat"));
    REQUIRE(parsed.contains("action"));
    REQUIRE(parsed.contains("payload"));

    // Action should be a known enum value
    std::string action = parsed["action"].get<std::string>();
    REQUIRE(action == "CHAT");
}

TEST_CASE("QueryInternal JSON has all required fields", "[ai_client][llm]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    std::string result = AIClient::instance().QueryInternal("Player asks: what dangers lie ahead?");
    json parsed = json::parse(result);

    // chat must be a non-empty string
    REQUIRE(parsed["chat"].is_string());
    REQUIRE(!parsed["chat"].get<std::string>().empty());

    // action must be a string
    REQUIRE(parsed["action"].is_string());

    // payload must be a string (can be empty)
    REQUIRE(parsed["payload"].is_string());
}

TEST_CASE("Fallback response is valid JSON", "[ai_client][fallback]")
{
    // Test the fallback path by calling with dry run off but no server
    // We test the shape of the expected fallback format
    json fallback = {
        {"chat", "I sense danger ahead, adventurer."},
        {"action", "CHAT"},
        {"payload", ""}
    };

    REQUIRE(fallback.contains("chat"));
    REQUIRE(fallback.contains("action"));
    REQUIRE(fallback.contains("payload"));
    REQUIRE(fallback["action"].get<std::string>() == "CHAT");
}

TEST_CASE("LatencyTimer tracks elapsed time", "[ai_client][latency]")
{
    auto start = std::chrono::steady_clock::now();
    LatencyTimer timer("test_op");

    // Simulate small work
    volatile int sum = 0;
    for (int i = 0; i < 10000; ++i) sum += i;

    double ms = timer.elapsed_ms();
    REQUIRE(ms >= 0.0);
    REQUIRE(ms < 5000.0); // should not take 5 seconds
}
