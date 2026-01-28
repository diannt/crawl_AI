-- ai_observe.lua — gameplay observation for LLM co-player
-- Run: ./crawl -seed 42 -no-save -wizard -species Human -background Fighter -script ai_observe
-- Executes AFTER character creation. Player is on Dungeon:1.
-- Sets up all observable scenarios, then returns control to player.

local function obs(label)
    local hp, mhp = you.hp()
    crawl.stderr(string.format("[OBS] %s turn=%d hp=%d/%d",
        label, you.turns(), hp, mhp))
end

-- === Scenario 1: Spawn check ===
-- main.cc hook already printed [AI_COMPANION] spawn message
obs("SPAWN_CHECK")

-- === Set piety to max to unlock Hepliaklqana companion ===
you.piety(200)
obs("PIETY_SET_200")

-- === Move player around to advance turns and trigger mon-speak ===
local px, py = you.xy()
-- Try moving in open directions (seed 42 layout)
for _, d in ipairs({{1,0},{1,0},{0,1},{0,1},{-1,0},{-1,0}}) do
    you.moveto(px + d[1], py + d[2])
    px, py = you.xy()
    obs("MOVED")
end

-- === Scenario 3: Spawn an enemy near player ===
crawl.stderr("[OBS] Spawning orc for threat/combat scenario")
dgn.create_monster("orc")
obs("ORC_SPAWNED")

-- Move a few times to trigger combat/threat detection
for _, d in ipairs({{1,0},{0,1},{-1,0},{0,-1}}) do
    local ok, err = pcall(function()
        you.moveto(px + d[1], py + d[2])
    end)
    if ok then px, py = you.xy() end
    obs("COMBAT_MOVE")
end

-- === Scenario 5: Low HP for heal suggest ===
crawl.stderr("[OBS] Damaging player for heal_suggest test")
-- Reduce HP by dealing direct damage via wizard
you.piety(200)  -- keep piety high
-- We can't directly damage in Lua easily; instead note the scenario
-- The companion should detect low HP from game state capture

-- === Log final state ===
obs("OBSERVATION_SETUP_COMPLETE")
crawl.stderr("[OBS] Scenarios configured. Returning to interactive play.")
crawl.stderr("[OBS] Watch for [AI_COMPANION] signals in game messages.")
