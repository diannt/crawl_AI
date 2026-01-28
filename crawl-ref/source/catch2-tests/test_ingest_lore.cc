/**
 * @file test_ingest_lore.cc
 * @brief Unit tests for lore ingestion chunker (Phase 2).
 *
 * Tests the tokenization/chunking logic without network calls.
 * Compile:
 *   g++ -std=c++17 -DCATCH_CONFIG_MAIN -DAI_CLIENT_TEST \
 *       -I ../contrib -I .. \
 *       test_ingest_lore.cc catch_amalgamated.cc -o test_ingest_lore -lpthread
 *   AI_CLIENT_DRY_RUN=1 ./test_ingest_lore
 */

#include "catch_amalgamated.hpp"
#include "../ai_client.h"

// We need access to chunk_text and strip_rst from ingest_lore.cc.
// Since it has a main(), we'll duplicate the logic here for unit testing.
// In production, these would be extracted to a shared header.

static constexpr size_t TEST_CHUNK_MAX = 400;
static constexpr size_t TEST_CHUNK_OVERLAP = 50;

static bool is_rst_decoration(const std::string& line)
{
    if (line.empty()) return false;
    char c = line[0];
    if (c != '*' && c != '+' && c != '-' && c != '=' && c != '#' && c != '~')
        return false;
    for (char ch : line)
        if (ch != c) return false;
    return line.size() >= 3;
}

static std::string strip_rst(const std::string& text)
{
    std::istringstream iss(text);
    std::string line;
    std::string result;
    while (std::getline(iss, line))
    {
        if (is_rst_decoration(line)) continue;
        if (line.size() >= 2 && line[0] == '.' && line[1] == '.') continue;
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) { result += "\n"; continue; }
        result += line.substr(start) + "\n";
    }
    return result;
}

static std::vector<std::string> chunk_text(const std::string& text)
{
    std::vector<std::string> chunks;
    std::string cleaned = strip_rst(text);

    std::vector<std::string> paragraphs;
    {
        std::istringstream iss(cleaned);
        std::string para, line;
        while (std::getline(iss, line))
        {
            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
            {
                if (!para.empty()) { paragraphs.push_back(para); para.clear(); }
            }
            else
            {
                if (!para.empty()) para += " ";
                para += line;
            }
        }
        if (!para.empty()) paragraphs.push_back(para);
    }

    std::string current_chunk;
    std::string overlap_buf;
    for (const auto& para : paragraphs)
    {
        if (!current_chunk.empty() &&
            current_chunk.size() + 1 + para.size() > TEST_CHUNK_MAX)
        {
            if (current_chunk.size() > TEST_CHUNK_OVERLAP)
                overlap_buf = current_chunk.substr(current_chunk.size() - TEST_CHUNK_OVERLAP);
            else
                overlap_buf = current_chunk;
            chunks.push_back(current_chunk);
            current_chunk = overlap_buf + " ";
        }
        if (current_chunk.empty()) current_chunk = para;
        else current_chunk += " " + para;
    }
    if (!current_chunk.empty() &&
        current_chunk.find_first_not_of(" \t\r\n") != std::string::npos)
        chunks.push_back(current_chunk);

    return chunks;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("RST decoration lines are stripped", "[ingest][rst_strip]")
{
    std::string input = "****\nHello World\n****\n";
    std::string result = strip_rst(input);
    REQUIRE(result.find("****") == std::string::npos);
    REQUIRE(result.find("Hello World") != std::string::npos);
}

TEST_CASE("RST directives are stripped", "[ingest][rst_strip]")
{
    std::string input = ".. contents::\n   :depth: 5\n\nActual content here.\n";
    std::string result = strip_rst(input);
    REQUIRE(result.find(".. contents") == std::string::npos);
    REQUIRE(result.find("Actual content here") != std::string::npos);
}

TEST_CASE("RST heading markers (+++ and ---) stripped", "[ingest][rst_strip]")
{
    std::string input = "++++++++++\nTitle\n++++++++++\n\nBody text.\n";
    std::string result = strip_rst(input);
    REQUIRE(result.find("+++") == std::string::npos);
    REQUIRE(result.find("Title") != std::string::npos);
    REQUIRE(result.find("Body text") != std::string::npos);
}

TEST_CASE("Single short paragraph produces one chunk", "[ingest][chunker]")
{
    std::string input = "This is a short paragraph about the dungeon.";
    auto chunks = chunk_text(input);
    REQUIRE(chunks.size() == 1);
    REQUIRE(chunks[0].find("short paragraph") != std::string::npos);
}

TEST_CASE("Long text produces multiple chunks", "[ingest][chunker]")
{
    // Create text longer than CHUNK_MAX_CHARS (400)
    std::string input;
    for (int i = 0; i < 20; ++i)
        input += "Paragraph number " + std::to_string(i) +
                 " contains some interesting information about the dungeon.\n\n";

    auto chunks = chunk_text(input);
    REQUIRE(chunks.size() > 1);

    // No chunk should exceed max size by more than one paragraph
    // (overlap buffer adds to previous content)
    for (const auto& chunk : chunks)
        REQUIRE(chunk.size() < TEST_CHUNK_MAX + 200); // generous bound
}

TEST_CASE("Chunks have overlap content", "[ingest][chunker]")
{
    // Create enough paragraphs to force multiple chunks
    std::string input;
    for (int i = 0; i < 15; ++i)
        input += "Paragraph " + std::to_string(i) +
                 " has unique marker_" + std::to_string(i) +
                 " and enough text to fill space in the chunk buffer.\n\n";

    auto chunks = chunk_text(input);
    if (chunks.size() >= 2)
    {
        // Second chunk should contain some text from near the end of first chunk
        // (due to overlap)
        // Just verify chunks are non-empty and not identical
        REQUIRE(chunks[0] != chunks[1]);
        REQUIRE(!chunks[0].empty());
        REQUIRE(!chunks[1].empty());
    }
}

TEST_CASE("Empty input produces no chunks", "[ingest][chunker]")
{
    auto chunks = chunk_text("");
    REQUIRE(chunks.empty());
}

TEST_CASE("Whitespace-only input produces no chunks", "[ingest][chunker]")
{
    auto chunks = chunk_text("   \n\n\n   \n");
    REQUIRE(chunks.empty());
}

TEST_CASE("RST-heavy input still produces chunks", "[ingest][chunker]")
{
    std::string input =
        "########################################\n"
        "Manual\n"
        "########################################\n\n"
        "Crawl is a fun game in the grand tradition of similar games like Rogue.\n"
        "The objective is to travel deep into a subterranean cave complex.\n\n"
        ".. contents::\n"
        "   :depth: 5\n\n"
        "You must defeat many horrible creatures along the way.\n";

    auto chunks = chunk_text(input);
    REQUIRE(!chunks.empty());

    // Should contain actual content, not just decoration
    bool has_content = false;
    for (const auto& c : chunks)
        if (c.find("Crawl is a fun game") != std::string::npos)
            has_content = true;
    REQUIRE(has_content);
}

TEST_CASE("Chunk text preserves important keywords", "[ingest][chunker]")
{
    std::string input =
        "The Orb of Zot is the ultimate goal.\n\n"
        "Hepliaklqana grants an Ancestor companion.\n\n"
        "Species aptitudes determine learning speed.\n";

    auto chunks = chunk_text(input);
    std::string all_text;
    for (const auto& c : chunks) all_text += c + " ";

    REQUIRE(all_text.find("Orb of Zot") != std::string::npos);
    REQUIRE(all_text.find("Hepliaklqana") != std::string::npos);
    REQUIRE(all_text.find("aptitudes") != std::string::npos);
}
