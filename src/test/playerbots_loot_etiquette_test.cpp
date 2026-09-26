#include "gtest/gtest.h"
#include "../../modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsLootEtiquette.h"

#include <array>

namespace
{
uint8_t const kWarrior = 1;
uint8_t const kRogue = 4;
uint8_t const kShaman = 7;
uint8_t const kMage = 8;
uint8_t const kDruid = 11;

std::array<float, PLAYERBOTS_LOOT_STAT_COUNT> EmptyStats()
{
    return {};
}

bool CanNeed(std::array<float, PLAYERBOTS_LOOT_STAT_COUNT> const& stats, uint8_t cls, bool tank, bool heal, bool caster,
             bool melee)
{
    return PlayerbotsLootCanNeed(PlayerbotsLootItemFamilies(stats.data()),
                                 PlayerbotsLootSpecFamilies(cls, tank, heal, caster, melee));
}
}

TEST(PlayerbotsLootEtiquette, SpellPowerSpiritMaceIsHealerAndCasterLoot)
{
    auto stats = EmptyStats();
    stats[PLAYERBOTS_LOOT_STAT_SPIRIT] = 1.0f;
    stats[PLAYERBOTS_LOOT_STAT_SPELL_POWER] = 9.0f;
    stats[PLAYERBOTS_LOOT_STAT_HEAL_POWER] = 9.0f;
    stats[PLAYERBOTS_LOOT_STAT_MELEE_DPS] = 20.0f;

    uint32_t const families = PlayerbotsLootItemFamilies(stats.data());
    EXPECT_TRUE(families & PLAYERBOTS_LOOT_FAMILY_CASTER);
    EXPECT_TRUE(families & PLAYERBOTS_LOOT_FAMILY_HEALER);
    EXPECT_FALSE(families & PLAYERBOTS_LOOT_FAMILY_MELEE_STR);
    EXPECT_FALSE(families & PLAYERBOTS_LOOT_FAMILY_MELEE_AGI);

    EXPECT_FALSE(CanNeed(stats, kRogue, false, false, false, true));
    EXPECT_TRUE(CanNeed(stats, kShaman, false, true, false, false));
    EXPECT_TRUE(CanNeed(stats, kMage, false, false, true, false));
    EXPECT_FALSE(CanNeed(stats, kShaman, false, false, false, true));
}

TEST(PlayerbotsLootEtiquette, AgilityDaggerIsRogueLoot)
{
    auto stats = EmptyStats();
    stats[PLAYERBOTS_LOOT_STAT_AGILITY] = 12.0f;
    stats[PLAYERBOTS_LOOT_STAT_MELEE_DPS] = 15.0f;

    EXPECT_TRUE(CanNeed(stats, kRogue, false, false, false, true));
    EXPECT_FALSE(CanNeed(stats, kMage, false, false, true, false));
}

TEST(PlayerbotsLootEtiquette, StrengthMaceIsWarriorLoot)
{
    auto stats = EmptyStats();
    stats[PLAYERBOTS_LOOT_STAT_STRENGTH] = 15.0f;

    EXPECT_TRUE(CanNeed(stats, kWarrior, false, false, false, true));
    EXPECT_FALSE(CanNeed(stats, kRogue, false, false, false, true));
}

TEST(PlayerbotsLootEtiquette, TraceSpiritOnStrengthGearIsNotHealerLoot)
{
    auto stats = EmptyStats();
    stats[PLAYERBOTS_LOOT_STAT_STRENGTH] = 20.0f;
    stats[PLAYERBOTS_LOOT_STAT_SPIRIT] = 1.0f;

    EXPECT_TRUE(CanNeed(stats, kWarrior, false, false, false, true));
    EXPECT_FALSE(CanNeed(stats, kShaman, false, true, false, false));
}

TEST(PlayerbotsLootEtiquette, NeutralDpsOnlyAllowsNeed)
{
    auto stats = EmptyStats();
    stats[PLAYERBOTS_LOOT_STAT_MELEE_DPS] = 18.0f;
    stats[PLAYERBOTS_LOOT_STAT_STAMINA] = 5.0f;

    EXPECT_EQ(PlayerbotsLootItemFamilies(stats.data()), PLAYERBOTS_LOOT_FAMILY_NONE);
    EXPECT_TRUE(CanNeed(stats, kRogue, false, false, false, true));
    EXPECT_TRUE(CanNeed(stats, kMage, false, false, true, false));
}

TEST(PlayerbotsLootEtiquette, BearTankNeedsAgiAndTankFamilies)
{
    uint32_t const families = PlayerbotsLootSpecFamilies(kDruid, true, false, false, true);
    EXPECT_TRUE(families & PLAYERBOTS_LOOT_FAMILY_TANK);
    EXPECT_TRUE(families & PLAYERBOTS_LOOT_FAMILY_MELEE_AGI);
    EXPECT_FALSE(families & PLAYERBOTS_LOOT_FAMILY_MELEE_STR);
}

TEST(PlayerbotsLootEtiquette, PlateTankNeedsStrAndTankFamilies)
{
    uint32_t const families = PlayerbotsLootSpecFamilies(kWarrior, true, false, false, true);
    EXPECT_TRUE(families & PLAYERBOTS_LOOT_FAMILY_TANK);
    EXPECT_TRUE(families & PLAYERBOTS_LOOT_FAMILY_MELEE_STR);
    EXPECT_FALSE(families & PLAYERBOTS_LOOT_FAMILY_MELEE_AGI);
}
