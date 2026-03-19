/**
 * @file async_claude_orchestrator.h
 * @brief Phase 5 — Async, non-blocking orchestrator for `claude -p` calls.
 *
 * Provides:
 *   - AsyncClaudeOrchestrator: manages multiple in-flight claude -p processes
 *   - Non-blocking reads via fork() + pipe() + poll()
 *   - WorldTree: rolling context buffer for decision continuity
 *   - Parallel pipeline execution (action + commentary concurrent)
 *
 * Does NOT block the terminal/game loop while waiting for claude -p output.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <map>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "claude_orchestrator.h"  // For CLAUDE_WORK_FOLDER, escape, extract_json, etc.

// ---------------------------------------------------------------------------
// WorldTree — rolling context buffer for decision continuity
// ---------------------------------------------------------------------------
struct WorldTreeEntry
{
    int turn = 0;
    std::string action;
    std::string direction;
    std::string chat;
    std::string commentary;
    std::string player_input;
    std::chrono::steady_clock::time_point timestamp;
};

class WorldTree
{
public:
    static constexpr size_t MAX_ENTRIES = 20;  // Rolling window of recent turns

    void record(const WorldTreeEntry& entry)
    {
        entries_.push_back(entry);
        if (entries_.size() > MAX_ENTRIES)
            entries_.pop_front();
    }

    // Build a context summary for the next prompt
    std::string build_context_summary() const
    {
        if (entries_.empty()) return "";

        std::string summary = "Recent decisions (last " +
            std::to_string(entries_.size()) + " turns):\n";

        for (const auto& e : entries_)
        {
            summary += "- Turn " + std::to_string(e.turn) + ": ";
            if (!e.action.empty())
                summary += "[" + e.action + "] ";
            if (!e.direction.empty())
                summary += "dir=" + e.direction + " ";
            if (!e.chat.empty())
            {
                // Truncate long chat for context window
                std::string short_chat = e.chat.substr(0, 80);
                if (e.chat.size() > 80) short_chat += "...";
                summary += "\"" + short_chat + "\" ";
            }
            if (!e.player_input.empty())
                summary += "(player: " + e.player_input.substr(0, 40) + ") ";
            summary += "\n";
        }

        return summary;
    }

    const std::deque<WorldTreeEntry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    std::deque<WorldTreeEntry> entries_;
};

// ---------------------------------------------------------------------------
// AsyncProcess — represents a single in-flight claude -p subprocess
// ---------------------------------------------------------------------------
struct AsyncProcess
{
    pid_t pid = -1;
    int read_fd = -1;          // pipe read end (non-blocking)
    std::string output;        // accumulated output
    std::string tag;           // identifies this process ("action", "commentary", etc.)
    bool finished = false;
    int exit_status = -1;
    std::chrono::steady_clock::time_point start_time;
};

// ---------------------------------------------------------------------------
// AsyncClaudeOrchestrator — manages parallel claude -p processes
// ---------------------------------------------------------------------------
class AsyncClaudeOrchestrator
{
public:
    static AsyncClaudeOrchestrator& instance()
    {
        static AsyncClaudeOrchestrator orch;
        return orch;
    }

    AsyncClaudeOrchestrator(const AsyncClaudeOrchestrator&) = delete;
    AsyncClaudeOrchestrator& operator=(const AsyncClaudeOrchestrator&) = delete;

    WorldTree& world_tree() { return world_tree_; }

    /**
     * Launch a claude -p process asynchronously.
     * Returns a tag string to identify the process later.
     */
    std::string launch(const std::string& prompt, const std::string& tag)
    {
#ifdef CLAUDE_ORCH_TEST
        return launch_test(prompt, tag);
#else
        return launch_real(prompt, tag);
#endif
    }

    /**
     * Poll all in-flight processes. Non-blocking.
     * Returns list of tags for processes that completed since last poll.
     */
    std::vector<std::string> poll_completions()
    {
        std::vector<std::string> completed;

        for (auto& [tag, proc] : processes_)
        {
            if (proc.finished)
            {
                // Already finished (e.g. test mode) — report once
                if (proc.exit_status >= 0)
                {
                    completed.push_back(tag);
                    proc.exit_status = -2; // Mark as already reported
                }
                continue;
            }

#ifdef CLAUDE_ORCH_TEST
            proc.finished = true;
            proc.exit_status = 0;
            completed.push_back(tag);
            continue;
#endif

            // Try to read available data (non-blocking)
            char buf[4096];
            ssize_t n;
            while ((n = read(proc.read_fd, buf, sizeof(buf) - 1)) > 0)
            {
                buf[n] = '\0';
                proc.output += buf;
            }

            // Check if process has exited
            int status;
            pid_t result = waitpid(proc.pid, &status, WNOHANG);
            if (result > 0)
            {
                // Read any remaining data
                while ((n = read(proc.read_fd, buf, sizeof(buf) - 1)) > 0)
                {
                    buf[n] = '\0';
                    proc.output += buf;
                }

                close(proc.read_fd);
                proc.read_fd = -1;
                proc.finished = true;
                proc.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

                auto elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - proc.start_time).count();

                fprintf(stderr, "[ASYNC_ORCH] '%s' completed: %d bytes, %.1fms, exit=%d\n",
                        tag.c_str(), (int)proc.output.size(), elapsed, proc.exit_status);

                completed.push_back(tag);
            }
        }

        return completed;
    }

    /**
     * Block until a specific tagged process completes.
     * Uses poll() to avoid busy-waiting.
     */
    bool wait_for(const std::string& tag, int timeout_ms = 120000)
    {
        auto it = processes_.find(tag);
        if (it == processes_.end()) return false;
        if (it->second.finished) return true;

#ifdef CLAUDE_ORCH_TEST
        it->second.finished = true;
        it->second.exit_status = 0;
        return true;
#endif

        auto& proc = it->second;
        struct pollfd pfd;
        pfd.fd = proc.read_fd;
        pfd.events = POLLIN | POLLHUP;

        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (!proc.finished)
        {
            int remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) break;

            int ret = ::poll(&pfd, 1, std::min(remaining, 1000));
            if (ret > 0)
            {
                char buf[4096];
                ssize_t n = read(proc.read_fd, buf, sizeof(buf) - 1);
                if (n > 0)
                {
                    buf[n] = '\0';
                    proc.output += buf;
                }
                else if (n == 0)
                {
                    // EOF — process done
                    int status;
                    waitpid(proc.pid, &status, 0);
                    close(proc.read_fd);
                    proc.read_fd = -1;
                    proc.finished = true;
                    proc.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                }
            }

            // Also check waitpid
            int status;
            pid_t result = waitpid(proc.pid, &status, WNOHANG);
            if (result > 0)
            {
                // Drain remaining data
                char buf[4096];
                ssize_t n;
                while ((n = read(proc.read_fd, buf, sizeof(buf) - 1)) > 0)
                {
                    buf[n] = '\0';
                    proc.output += buf;
                }
                close(proc.read_fd);
                proc.read_fd = -1;
                proc.finished = true;
                proc.exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
        }

        if (proc.finished)
        {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - proc.start_time).count();
            fprintf(stderr, "[ASYNC_ORCH] '%s' completed (waited): %d bytes, %.1fms\n",
                    tag.c_str(), (int)proc.output.size(), elapsed);
        }

        return proc.finished;
    }

    /**
     * Get the output of a completed process.
     * Returns empty string if not found or not finished.
     */
    std::string get_output(const std::string& tag)
    {
        auto it = processes_.find(tag);
        if (it == processes_.end() || !it->second.finished)
            return "";
        return it->second.output;
    }

    /**
     * Get parsed JSON output of a completed process.
     */
    json get_json(const std::string& tag)
    {
        std::string raw = get_output(tag);
        if (raw.empty()) return json({{"chat", "..."}, {"action", "CHAT"}, {"payload", ""}});

        std::string json_str = extract_json_from_text(raw);
        try
        {
            return json::parse(json_str);
        }
        catch (...)
        {
            return json({{"chat", raw.substr(0, 200)}, {"action", "CHAT"}, {"payload", ""}});
        }
    }

    /**
     * Check if a process is still running.
     */
    bool is_running(const std::string& tag)
    {
        auto it = processes_.find(tag);
        if (it == processes_.end()) return false;
        return !it->second.finished;
    }

    /**
     * Clean up completed processes.
     */
    void cleanup()
    {
        for (auto it = processes_.begin(); it != processes_.end();)
        {
            if (it->second.finished)
                it = processes_.erase(it);
            else
                ++it;
        }
    }

    /**
     * Number of in-flight processes.
     */
    size_t active_count() const
    {
        size_t count = 0;
        for (const auto& [tag, proc] : processes_)
            if (!proc.finished) count++;
        return count;
    }

private:
    AsyncClaudeOrchestrator() = default;
    ~AsyncClaudeOrchestrator()
    {
        // Kill any remaining processes
        for (auto& [tag, proc] : processes_)
        {
            if (!proc.finished && proc.pid > 0)
            {
                kill(proc.pid, SIGTERM);
                waitpid(proc.pid, nullptr, 0);
                if (proc.read_fd >= 0) close(proc.read_fd);
            }
        }
    }

    std::map<std::string, AsyncProcess> processes_;
    WorldTree world_tree_;

    std::string launch_real(const std::string& prompt, const std::string& tag)
    {
        // Create pipe
        int pipefd[2];
        if (pipe(pipefd) == -1)
        {
            fprintf(stderr, "[ASYNC_ORCH] pipe() failed for '%s'\n", tag.c_str());
            return "";
        }

        pid_t pid = fork();
        if (pid == -1)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            fprintf(stderr, "[ASYNC_ORCH] fork() failed for '%s'\n", tag.c_str());
            return "";
        }

        if (pid == 0)
        {
            // Child process
            close(pipefd[0]);  // Close read end
            dup2(pipefd[1], STDOUT_FILENO);  // Redirect stdout to pipe
            close(pipefd[1]);

            // Redirect stderr to /dev/null
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0)
            {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }

            // cd to work folder and exec claude
            chdir(CLAUDE_WORK_FOLDER);

            // Escape prompt for shell
            std::string escaped;
            for (char c : prompt)
            {
                if (c == '\'')
                    escaped += "'\\''";
                else
                    escaped += c;
            }
            std::string cmd = "claude -p '" + escaped + "'";

            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(127);  // exec failed
        }

        // Parent process
        close(pipefd[1]);  // Close write end

        // Set read end to non-blocking
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        AsyncProcess proc;
        proc.pid = pid;
        proc.read_fd = pipefd[0];
        proc.tag = tag;
        proc.start_time = std::chrono::steady_clock::now();

        processes_[tag] = proc;

        fprintf(stderr, "[ASYNC_ORCH] launched '%s': pid=%d, prompt=%zu bytes\n",
                tag.c_str(), pid, prompt.size());

        return tag;
    }

#ifdef CLAUDE_ORCH_TEST
    std::string launch_test(const std::string& prompt, const std::string& tag)
    {
        AsyncProcess proc;
        proc.pid = 0;
        proc.tag = tag;
        proc.start_time = std::chrono::steady_clock::now();

        // Generate test response based on tag
        json resp;
        if (tag == "action")
        {
            resp = {{"chat", "Test: I shall strike the foe!"},
                    {"action", "WARN_THREAT"},
                    {"direction", "ATTACK_orc"},
                    {"payload", ""}};
        }
        else if (tag == "commentary")
        {
            resp = {{"commentary", "Test: The dungeon feels ominous today."}};
        }
        else
        {
            resp = {{"chat", "Test response for " + tag},
                    {"action", "CHAT"},
                    {"payload", ""}};
        }

        proc.output = resp.dump();
        proc.finished = true;
        proc.exit_status = 0;

        processes_[tag] = proc;

        fprintf(stderr, "[ASYNC_ORCH] TEST launched '%s'\n", tag.c_str());
        return tag;
    }
#endif

    // Extract JSON from potentially wrapped text
    static std::string extract_json_from_text(const std::string& text)
    {
        auto first = text.find('{');
        auto last = text.rfind('}');
        if (first != std::string::npos && last != std::string::npos && last > first)
            return text.substr(first, last - first + 1);
        return text;
    }
};
