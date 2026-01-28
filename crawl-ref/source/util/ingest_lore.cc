/**
 * @file ingest_lore.cc
 * @brief Standalone tool to ingest game lore into Qdrant for RAG.
 *
 * Reads docs/crawl_manual.rst and docs/aptitudes.txt, tokenizes into
 * overlapping chunks, embeds via Ollama nomic-embed-text, and upserts
 * into Qdrant collection "crawl_lore".
 *
 * Usage:
 *   ./ingest_lore [--docs-dir <path>]
 *
 * Defaults to ../../docs/ relative to binary location.
 * Set AI_CLIENT_DRY_RUN=1 to skip real HTTP (for testing chunker logic).
 *
 * Compile:
 *   g++ -std=c++17 -I ../contrib -I .. ingest_lore.cc -o ingest_lore -lpthread
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../ai_client.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static constexpr size_t CHUNK_MAX_CHARS  = 400;   // ~512 tokens conservatively
static constexpr size_t CHUNK_OVERLAP    = 50;    // sliding overlap in chars
static constexpr int    QDRANT_DIM       = 768;

// ---------------------------------------------------------------------------
// UUID generation (simple, no boost dependency)
// ---------------------------------------------------------------------------
static std::string generate_uuid()
{
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    char buf[37];
    snprintf(buf, sizeof(buf),
             "%08x-%04x-%04x-%04x-%012x",
             dist(rng),
             dist(rng) & 0xFFFF,
             (dist(rng) & 0x0FFF) | 0x4000,   // version 4
             (dist(rng) & 0x3FFF) | 0x8000,   // variant 1
             dist(rng) & 0xFFFFFFFF);
    // Pad to 36 chars (simplified — not RFC4122 perfect but unique enough)
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// RST markup stripping
// ---------------------------------------------------------------------------
static bool is_rst_decoration(const std::string& line)
{
    if (line.empty()) return false;
    char c = line[0];
    if (c != '*' && c != '+' && c != '-' && c != '=' && c != '#' && c != '~')
        return false;
    // Check if entire line is the same char
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
        // Skip RST decoration lines (===, ***, +++, etc.)
        if (is_rst_decoration(line)) continue;

        // Skip RST directives (.. directive::)
        if (line.size() >= 2 && line[0] == '.' && line[1] == '.') continue;

        // Strip leading/trailing whitespace but preserve paragraph structure
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
        {
            result += "\n";
            continue;
        }
        result += line.substr(start) + "\n";
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tokenization: paragraph-aware chunking with overlap
// ---------------------------------------------------------------------------
std::vector<std::string> chunk_text(const std::string& text)
{
    std::vector<std::string> chunks;
    std::string cleaned = strip_rst(text);

    // Split on double newlines (paragraph boundaries)
    std::vector<std::string> paragraphs;
    {
        std::istringstream iss(cleaned);
        std::string para;
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos)
            {
                if (!para.empty())
                {
                    paragraphs.push_back(para);
                    para.clear();
                }
            }
            else
            {
                if (!para.empty()) para += " ";
                para += line;
            }
        }
        if (!para.empty()) paragraphs.push_back(para);
    }

    // Merge paragraphs into chunks up to CHUNK_MAX_CHARS, with overlap
    std::string current_chunk;
    std::string overlap_buf; // last CHUNK_OVERLAP chars of previous chunk

    for (const auto& para : paragraphs)
    {
        // If adding this paragraph would exceed chunk size, flush first
        if (!current_chunk.empty() &&
            current_chunk.size() + 1 + para.size() > CHUNK_MAX_CHARS)
        {
            // Save tail for overlap
            if (current_chunk.size() > CHUNK_OVERLAP)
                overlap_buf = current_chunk.substr(current_chunk.size() - CHUNK_OVERLAP);
            else
                overlap_buf = current_chunk;

            chunks.push_back(current_chunk);
            current_chunk = overlap_buf + " ";
        }

        if (current_chunk.empty())
            current_chunk = para;
        else
            current_chunk += " " + para;
    }

    // Flush remaining
    if (!current_chunk.empty() &&
        current_chunk.find_first_not_of(" \t\r\n") != std::string::npos)
    {
        chunks.push_back(current_chunk);
    }

    return chunks;
}

// ---------------------------------------------------------------------------
// File reader
// ---------------------------------------------------------------------------
static std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cerr << "[INGEST] ERROR: Cannot open " << path << std::endl;
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------
// Qdrant collection creation
// ---------------------------------------------------------------------------
static bool ensure_collection(httplib::Client& qdrant)
{
    // Check if collection exists
    auto check = qdrant.Get(std::string("/collections/") + LORE_COLLECTION);
    if (check && check->status == 200) return true;

    // Create collection via PUT
    json body = {
        {"vectors", {
            {"size", QDRANT_DIM},
            {"distance", "Cosine"}
        }}
    };

    std::string payload = body.dump();
    auto res = qdrant.Put(
        std::string("/collections/") + LORE_COLLECTION,
        payload.c_str(),
        payload.size(),
        "application/json"
    );

    if (!res || res->status != 200)
    {
        std::cerr << "[INGEST] ERROR: Failed to create collection, HTTP "
                  << (res ? res->status : -1) << std::endl;
        return false;
    }

    std::cout << "[INGEST] Created collection '" << LORE_COLLECTION
              << "' (dim=" << QDRANT_DIM << ", Cosine)" << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Upsert a single point to Qdrant
// ---------------------------------------------------------------------------
static bool upsert_point(httplib::Client& qdrant,
                         const std::string& id,
                         const std::vector<float>& vec,
                         const std::string& text,
                         const std::string& source)
{
    json point = {
        {"id", id},
        {"vector", json::array()},
        {"payload", {
            {"text", text},
            {"source", source}
        }}
    };

    // Build vector array
    for (float f : vec) point["vector"].push_back(f);

    json body = {
        {"points", json::array({point})}
    };

    std::string path = std::string("/collections/") + LORE_COLLECTION + "/points";
    std::string payload = body.dump();
    auto res = qdrant.Put(path, payload.c_str(), payload.size(), "application/json");

    return (res && res->status == 200);
}

// ---------------------------------------------------------------------------
// Main ingestion loop
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::string docs_dir = "../../docs/";

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--docs-dir" && i + 1 < argc)
            docs_dir = argv[++i];
    }

    bool dry_run = (std::getenv("AI_CLIENT_DRY_RUN") != nullptr);

    std::cout << "[INGEST] Starting lore ingestion..." << std::endl;
    std::cout << "[INGEST] Docs dir: " << docs_dir << std::endl;
    std::cout << "[INGEST] Dry run: " << (dry_run ? "YES" : "NO") << std::endl;

    // Source files
    std::vector<std::pair<std::string, std::string>> sources = {
        {docs_dir + "crawl_manual.rst", "crawl_manual"},
        {docs_dir + "aptitudes.txt",    "aptitudes"}
    };

    // Setup Qdrant client (for collection management)
    httplib::Client qdrant(QDRANT_HOST);
    qdrant.set_read_timeout(30, 0);

    if (!dry_run)
    {
        if (!ensure_collection(qdrant))
            return 1;
    }

    size_t total_chunks = 0;
    size_t total_upserted = 0;

    for (const auto& [filepath, source_name] : sources)
    {
        std::cout << "[INGEST] Reading: " << filepath << std::endl;
        std::string content = read_file(filepath);
        if (content.empty())
        {
            std::cerr << "[INGEST] WARNING: Skipping empty file " << filepath << std::endl;
            continue;
        }

        auto chunks = chunk_text(content);
        std::cout << "[INGEST] " << source_name << ": "
                  << chunks.size() << " chunks generated" << std::endl;

        for (size_t i = 0; i < chunks.size(); ++i)
        {
            // Skip whitespace-only chunks
            if (chunks[i].find_first_not_of(" \t\r\n") == std::string::npos)
                continue;

            total_chunks++;

            // Embed
            auto vec = AIClient::instance().Embed(chunks[i]);
            if (vec.empty())
            {
                std::cerr << "[INGEST] WARNING: embed failed for chunk " << i << std::endl;
                continue;
            }

            if ((int)vec.size() != QDRANT_DIM)
            {
                std::cerr << "[INGEST] WARNING: unexpected dim " << vec.size()
                          << " (expected " << QDRANT_DIM << ")" << std::endl;
                continue;
            }

            // Upsert
            if (!dry_run)
            {
                std::string id = generate_uuid();
                if (upsert_point(qdrant, id, vec, chunks[i], source_name))
                    total_upserted++;
                else
                    std::cerr << "[INGEST] WARNING: upsert failed for chunk " << i << std::endl;
            }
            else
            {
                total_upserted++; // count as success in dry run
            }

            std::cout << "[INGEST] chunk " << (i + 1) << "/" << chunks.size()
                      << " embedded (vec_size=" << vec.size() << ")" << std::endl;
        }
    }

    std::cout << "[INGEST] Done. Processed " << total_chunks << " chunks, "
              << total_upserted << " upserted to Qdrant." << std::endl;

    return (total_chunks > 0) ? 0 : 1;
}
