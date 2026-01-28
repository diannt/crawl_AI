/**
 * @file test_ai_rewards.cc
 * @brief Unit tests for Phase 4 mechanics — item rewards, XP sharing,
 *        companion enable gate, and event narration.
 *
 * Compiled with AI_REWARDS_TEST defined so all engine calls are stubbed.
 * Tags: [rewards] [grant] [xp] [gate] [narrate] [resolve]
 *
 * Standalone:
 *   g++ -std=c++17 -DCATCH_CONFIG_MAIN -DAI_REWARDS_TEST -DAI_COMPANION_TEST \
 *       -I ../contrib -I .. \
 *       test_ai_rewards.cc catch_amalgamated.cc -o test_ai_rewards -lpthread
 *   AI_CLIENT_DRY_RUN=1 ./test_ai_rewards
 */

#ifndef AI_REWARDS_TEST
#define AI_REWARDS_TEST
#endif
#ifndef AI_COMPANION_TEST
#define AI_COMPANION_TEST
#endif

#include "catch_amalgamated.hpp"
#include "../ai_rewards.h"

using namespace ai_companion;

// ===========================================================================
// Companion enable gate
// ===========================================================================

TEST_CASE("ai_companion_enabled is true by default", "[rewards][gate]")
{
    unsetenv("AI_COMPANION");
    REQUIRE(ai_companion_enabled() == true);
}

TEST_CASE("ai_companion_enabled respects AI_COMPANION=0", "[rewards][gate]")
{
    setenv("AI_COMPANION", "0", 1);
    REQUIRE(ai_companion_enabled() == false);
    unsetenv("AI_COMPANION");
}

TEST_CASE("ai_companion_enabled treats non-zero as enabled", "[rewards][gate]")
{
    setenv("AI_COMPANION", "1", 1);
    REQUIRE(ai_companion_enabled() == true);

    setenv("AI_COMPANION", "yes", 1);
    REQUIRE(ai_companion_enabled() == true);
    unsetenv("AI_COMPANION");
}

// ===========================================================================
// Item resolution
// ===========================================================================

TEST_CASE("resolve_item finds healing potion", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    const ItemSpec* spec = resolve_item("healing potion");
    REQUIRE(spec != nullptr);
    REQUIRE(spec->base_type == 7); // OBJ_POTIONS
    REQUIRE(spec->name == "healing potion");
}

TEST_CASE("resolve_item finds weapons by name", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    const ItemSpec* sword = resolve_item("short sword");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->base_type == 0); // OBJ_WEAPONS

    const ItemSpec* dagger = resolve_item("dagger");
    REQUIRE(dagger != nullptr);
    REQUIRE(dagger->base_type == 0);
}

TEST_CASE("resolve_item finds armour", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    const ItemSpec* chain = resolve_item("chain mail");
    REQUIRE(chain != nullptr);
    REQUIRE(chain->base_type == 2); // OBJ_ARMOUR

    const ItemSpec* cloak = resolve_item("elven cloak");
    REQUIRE(cloak != nullptr);
    REQUIRE(cloak->base_type == 2);
}

TEST_CASE("resolve_item finds scrolls", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    const ItemSpec* scroll = resolve_item("scroll of identify");
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->base_type == 5); // OBJ_SCROLLS
}

TEST_CASE("resolve_item finds gold", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    const ItemSpec* gold = resolve_item("gold");
    REQUIRE(gold != nullptr);
    REQUIRE(gold->base_type == 13); // OBJ_GOLD
}

TEST_CASE("resolve_item is case insensitive", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    REQUIRE(resolve_item("Healing Potion") != nullptr);
    REQUIRE(resolve_item("DAGGER") != nullptr);
    REQUIRE(resolve_item("Chain Mail") != nullptr);
}

TEST_CASE("resolve_item returns nullptr for unknown items", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    REQUIRE(resolve_item("dragon scales of doom") == nullptr);
    REQUIRE(resolve_item("") == nullptr);
    REQUIRE(resolve_item("xyzzy_nonexistent") == nullptr);
}

TEST_CASE("resolve_item handles partial match", "[rewards][resolve]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    // "potion" should match "healing potion" or similar
    const ItemSpec* spec = resolve_item("potion");
    REQUIRE(spec != nullptr);
    REQUIRE(spec->base_type == 7); // some potion type
}

// ===========================================================================
// GrantReward (stub mode — logs to reward_log)
// ===========================================================================

TEST_CASE("GrantReward logs resolved items", "[rewards][grant]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);
    reward_log().clear();

    GrantReward("healing potion");

    REQUIRE(reward_log().size() == 1);
    REQUIRE(reward_log()[0].item_name == "healing potion");
    REQUIRE(reward_log()[0].resolved == true);
    REQUIRE(reward_log()[0].base_type == 7);
}

TEST_CASE("GrantReward logs unresolved items gracefully", "[rewards][grant]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);
    reward_log().clear();

    GrantReward("dragon egg of prophecy");

    REQUIRE(reward_log().size() == 1);
    REQUIRE(reward_log()[0].resolved == false);
    REQUIRE(reward_log()[0].base_type == -1);
}

TEST_CASE("GrantReward accumulates multiple rewards", "[rewards][grant]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);
    reward_log().clear();

    GrantReward("short sword");
    GrantReward("healing potion");
    GrantReward("gold");

    REQUIRE(reward_log().size() == 3);
    REQUIRE(reward_log()[0].resolved == true);
    REQUIRE(reward_log()[1].resolved == true);
    REQUIRE(reward_log()[2].resolved == true);
}

// ===========================================================================
// XP sharing
// ===========================================================================

TEST_CASE("share_xp_with_ancestor splits XP 50/50", "[rewards][xp]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    auto res = share_xp_with_ancestor(100, "orc", 5);
    REQUIRE(res.shared_xp == 50);
    REQUIRE(res.monster_name == "orc");
    REQUIRE(res.turn == 5);
    REQUIRE(res.logged == true);
}

TEST_CASE("share_xp_with_ancestor handles odd XP (integer division)", "[rewards][xp]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    auto res = share_xp_with_ancestor(101, "goblin", 10);
    REQUIRE(res.shared_xp == 50); // 101/2 = 50 (truncated)
}

TEST_CASE("share_xp_with_ancestor handles zero XP", "[rewards][xp]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    auto res = share_xp_with_ancestor(0, "slime", 1);
    REQUIRE(res.shared_xp == 0);
    REQUIRE(res.logged == true);
}

TEST_CASE("share_xp_with_ancestor preserves monster name", "[rewards][xp]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    auto res = share_xp_with_ancestor(200, "fire dragon", 99);
    REQUIRE(res.monster_name == "fire dragon");
    REQUIRE(res.shared_xp == 100);
}

// ===========================================================================
// Quest log
// ===========================================================================

TEST_CASE("WritePlayerLogs records quest entries", "[rewards][quest]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);
    quest_log().clear();

    WritePlayerLogs("Retrieve the ancient key from the crypt.");

    REQUIRE(quest_log().size() == 1);
    REQUIRE(quest_log()[0].find("ancient key") != std::string::npos);
}

TEST_CASE("WritePlayerLogs accumulates multiple quests", "[rewards][quest]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);
    quest_log().clear();

    WritePlayerLogs("Find the missing sword.");
    WritePlayerLogs("Defeat the lich lord.");

    REQUIRE(quest_log().size() == 2);
}

// ===========================================================================
// Event narration (dry run — LLM returns canned response)
// ===========================================================================

TEST_CASE("narrate_event returns non-empty string", "[rewards][narrate]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    std::string narr = narrate_event("XP_SHARE", "Defeated orc gaining 50 XP",
                                      "Aldric the Undying");
    REQUIRE(!narr.empty());
}

TEST_CASE("narrate_event handles empty details", "[rewards][narrate]")
{
    setenv("AI_CLIENT_DRY_RUN", "1", 1);

    std::string narr = narrate_event("REWARD", "", "The Ancestor");
    REQUIRE(!narr.empty());
}

// ===========================================================================
// Item registry completeness
// ===========================================================================

TEST_CASE("item_registry covers all major object classes", "[rewards][resolve]")
{
    const auto& reg = item_registry();

    bool has_weapon   = false;
    bool has_armour   = false;
    bool has_potion   = false;
    bool has_scroll   = false;
    bool has_gold     = false;

    for (const auto& spec : reg)
    {
        if (spec.base_type == 0)  has_weapon = true;   // OBJ_WEAPONS
        if (spec.base_type == 2)  has_armour = true;   // OBJ_ARMOUR
        if (spec.base_type == 7)  has_potion = true;   // OBJ_POTIONS
        if (spec.base_type == 5)  has_scroll = true;   // OBJ_SCROLLS
        if (spec.base_type == 13) has_gold   = true;   // OBJ_GOLD
    }

    REQUIRE(has_weapon);
    REQUIRE(has_armour);
    REQUIRE(has_potion);
    REQUIRE(has_scroll);
    REQUIRE(has_gold);
}

TEST_CASE("item_registry has no duplicate names", "[rewards][resolve]")
{
    const auto& reg = item_registry();
    std::vector<std::string> names;
    for (const auto& spec : reg)
        names.push_back(spec.name);

    // Sort and check for adjacent duplicates
    std::sort(names.begin(), names.end());
    for (size_t i = 1; i < names.size(); ++i)
        REQUIRE(names[i] != names[i-1]);
}
