#!/bin/bash
# TUI integration test for AI Companion in DCSS (console mode)
# Launches crawl in an xterm, sends keystrokes via xdotool,
# captures terminal screenshots with import/scrot.
#
# Usage: ./run_graphical_test.sh

set -e

CRAWL_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCREENSHOT_DIR="$(dirname "$0")"
CRAWL_BIN="$CRAWL_DIR/crawl"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
SAVE_DIR="/tmp/crawl_test_$TIMESTAMP"
STEP=0

cleanup() {
    if [ -n "$XTERM_PID" ] && kill -0 "$XTERM_PID" 2>/dev/null; then
        echo "[TEST] Killing xterm (pid $XTERM_PID)"
        kill "$XTERM_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

screenshot() {
    local label="$1"
    STEP=$((STEP + 1))
    local num=$(printf "%02d" $STEP)
    local filename="${SCREENSHOT_DIR}/${TIMESTAMP}_${num}_${label}.png"
    sleep 0.8
    if [ -n "$WID" ]; then
        import -window "$WID" "$filename" 2>/dev/null \
            || scrot -u "$filename" 2>/dev/null \
            || echo "[TEST] Screenshot FAILED: $label"
    else
        echo "[TEST] Screenshot SKIP (no window): $label"
        return
    fi
    echo "[TEST] Screenshot saved: $filename"
}

send_key() {
    xdotool key --window "$WID" --delay 80 "$@"
    sleep 0.3
}

send_str() {
    xdotool type --window "$WID" --clearmodifiers --delay 80 "$1"
    sleep 0.3
}

echo "========================================="
echo "AI Companion TUI Integration Test"
echo "========================================="
echo "[TEST] Crawl: $CRAWL_BIN"
echo "[TEST] Output: $SCREENSHOT_DIR/"
echo ""

mkdir -p "$SAVE_DIR"

# =========================================================================
# Launch crawl inside an xterm
# =========================================================================
echo "[TEST] Launching crawl in xterm (console mode, wizard)..."

xterm -fa "Monospace" -fs 14 -geometry 100x35 \
    -title "DCSS-AI-Test" \
    -e "$CRAWL_BIN" -wizard -dir "$SAVE_DIR" \
        -name AICompTest \
        -extra-opt-first "default_manual_training=true" \
        2>"/tmp/crawl_stderr_$TIMESTAMP.log" &
XTERM_PID=$!

echo "[TEST] xterm PID: $XTERM_PID"
echo "[TEST] Stderr:    /tmp/crawl_stderr_$TIMESTAMP.log"

# Wait for xterm window
echo "[TEST] Waiting for xterm..."
for i in $(seq 1 20); do
    WID=$(xdotool search --name "DCSS-AI-Test" 2>/dev/null | head -1) || true
    [ -n "$WID" ] && break
    sleep 1
done
if [ -z "$WID" ]; then
    echo "[TEST] ERROR: xterm window not found after 20s"; exit 1
fi
echo "[TEST] Window ID: $WID"
sleep 2

# =========================================================================
# 1. Startup screen
# =========================================================================
screenshot "startup"

# =========================================================================
# 2. Character creation — pick Human Fighter via Tab (recommended)
# =========================================================================
echo "[TEST] Selecting character..."
# Pick species: 'j' for Human (or Tab for recommended)
send_key Tab
sleep 1
screenshot "species_pick"

# If prompted for background, Tab again
send_key Tab
sleep 1
screenshot "background_pick"

# Weapon selection: 'a' for first weapon
send_key a
sleep 2
screenshot "game_start"

# =========================================================================
# 3. Wizard mode: set god to Hepliaklqana
# =========================================================================
echo "[TEST] Setting Hepliaklqana via wizard mode..."
# & opens wizard mode — must confirm with "wiz" + Enter
send_key ampersand
sleep 1
send_str "wiz"
send_key Return
sleep 1
screenshot "wizard_entered"

# Now use wizard commands: & again then 'G' for god selection
send_key ampersand
sleep 0.5
send_key shift+g
sleep 1
screenshot "god_prompt"

# Type Hepliaklqana
send_str "Hepliaklqana"
send_key Return
sleep 2
screenshot "hep_selected"

# Dismiss any prompts
send_key Return
sleep 0.5
send_key Return
sleep 1
screenshot "hep_active"

# =========================================================================
# 4. Wizard: boost piety so ancestor spawns
# =========================================================================
echo "[TEST] Boosting piety..."
send_key ampersand
sleep 0.5
send_key p
sleep 0.5
send_str "200"
send_key Return
sleep 1
screenshot "piety_maxed"

# =========================================================================
# 5. Wizard: spawn test monsters
# =========================================================================
echo "[TEST] Spawning ogre..."
send_key ampersand
sleep 0.5
send_key shift+m
sleep 1
send_str "ogre"
send_key Return
sleep 2
screenshot "ogre_spawned"

echo "[TEST] Spawning gnoll..."
send_key ampersand
sleep 0.5
send_key shift+m
sleep 1
send_str "gnoll"
send_key Return
sleep 2
screenshot "gnoll_spawned"

# =========================================================================
# 6. Move around — triggers ancestor speak → AI companion hook
# =========================================================================
echo "[TEST] Moving to trigger companion..."
# Each move may trigger mon-speak.cc ancestor hook → claude -p call
# Give extra time per move for the claude -p subprocess

send_key period   # wait (.)
sleep 5
screenshot "wait_companion"

send_key l        # east
sleep 5
screenshot "move_east"

send_key h        # west
sleep 5
screenshot "move_west"

send_key j        # south
sleep 5
screenshot "move_south"

# =========================================================================
# 7. Open message log — should show "Companion says:" lines
# =========================================================================
echo "[TEST] Checking message log (Ctrl+P)..."
send_key ctrl+p
sleep 1
screenshot "message_log"

# Scroll up for more history
send_key Page_Up
sleep 0.5
screenshot "message_log_scroll"

send_key Escape
sleep 0.5

# =========================================================================
# 8. Move toward monsters for combat
# =========================================================================
echo "[TEST] Engaging combat..."
for dir in l l j j l; do
    send_key "$dir"
    sleep 5
done
screenshot "combat_state"

# Check log after combat
send_key ctrl+p
sleep 1
screenshot "combat_messages"
send_key Escape
sleep 0.5

# =========================================================================
# 9. Final state
# =========================================================================
screenshot "final_state"

# Quit
send_key ctrl+q
sleep 1
send_key Return
sleep 1

# =========================================================================
# Results
# =========================================================================
echo ""
echo "========================================="
echo "Test Complete"
echo "========================================="
echo ""
echo "Screenshots:"
ls -1 "$SCREENSHOT_DIR"/${TIMESTAMP}_*.png 2>/dev/null | while read f; do
    echo "  $(basename "$f")"
done
echo ""
echo "AI Companion log output:"
echo "-----------------------------------------"
grep -E "\[AI_COMPANION\]|\[ASYNC_ORCH\]" "/tmp/crawl_stderr_$TIMESTAMP.log" 2>/dev/null \
    || echo "(no AI companion messages in stderr)"
echo "-----------------------------------------"
echo "Full stderr: /tmp/crawl_stderr_$TIMESTAMP.log"
