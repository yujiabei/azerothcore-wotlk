#include "gtest/gtest.h"
#include "../../modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsLfgFill.h"

namespace
{
uint8_t const kWarrior = 1;
uint8_t const kPaladin = 2;
uint8_t const kHunter = 3;
uint8_t const kRogue = 4;
uint8_t const kPriest = 5;
uint8_t const kDeathKnight = 6;
uint8_t const kShaman = 7;
uint8_t const kMage = 8;
uint8_t const kWarlock = 9;
uint8_t const kDruid = 11;
}

TEST(PlayerbotsLfgFill, ClothCasterWantsMeleeDps)
{
    bool preferPhysicalDps = false;
    bool avoidPlate = false;
    PlayerbotsLfgAddPlayerLootConstraints(kMage, PLAYERBOTS_LFG_ROLE_DPS, preferPhysicalDps, avoidPlate);

    EXPECT_TRUE(preferPhysicalDps);
    EXPECT_FALSE(avoidPlate);
    EXPECT_TRUE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kMage, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_TRUE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kWarlock, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_TRUE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kPriest, PLAYERBOTS_LFG_ROLE_HEAL));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kHunter, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kRogue, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kWarrior, PLAYERBOTS_LFG_ROLE_TANK));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kShaman, PLAYERBOTS_LFG_ROLE_HEAL));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kDruid, PLAYERBOTS_LFG_ROLE_HEAL));
}

TEST(PlayerbotsLfgFill, PaladinAvoidsPlate)
{
    bool preferPhysicalDps = false;
    bool avoidPlate = false;
    PlayerbotsLfgAddPlayerLootConstraints(kPaladin, PLAYERBOTS_LFG_ROLE_DPS, preferPhysicalDps, avoidPlate);

    EXPECT_FALSE(preferPhysicalDps);
    EXPECT_TRUE(avoidPlate);
    EXPECT_TRUE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kWarrior, PLAYERBOTS_LFG_ROLE_TANK));
    EXPECT_TRUE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kDeathKnight, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_TRUE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kPaladin, PLAYERBOTS_LFG_ROLE_HEAL));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kDruid, PLAYERBOTS_LFG_ROLE_TANK));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kHunter, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kPriest, PLAYERBOTS_LFG_ROLE_HEAL));
}

TEST(PlayerbotsLfgFill, HunterDoesNotForceMeleeOrAvoidPlate)
{
    bool preferPhysicalDps = false;
    bool avoidPlate = false;
    PlayerbotsLfgAddPlayerLootConstraints(kHunter, PLAYERBOTS_LFG_ROLE_DPS, preferPhysicalDps, avoidPlate);

    EXPECT_FALSE(preferPhysicalDps);
    EXPECT_FALSE(avoidPlate);
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kMage, PLAYERBOTS_LFG_ROLE_DPS));
    EXPECT_FALSE(PlayerbotsLfgBotLootConflicts(preferPhysicalDps, avoidPlate, kWarrior, PLAYERBOTS_LFG_ROLE_TANK));
}

TEST(PlayerbotsLfgFill, PhysicalDpsSpecsForMeleeTeam)
{
    EXPECT_EQ(PlayerbotsLfgPreferredSpecTab(kShaman, PLAYERBOTS_LFG_ROLE_DPS, true), 1);
    EXPECT_EQ(PlayerbotsLfgPreferredSpecTab(kDruid, PLAYERBOTS_LFG_ROLE_DPS, true), 1);
    EXPECT_EQ(PlayerbotsLfgPreferredSpecTab(kShaman, PLAYERBOTS_LFG_ROLE_DPS, false), 0);
    EXPECT_EQ(PlayerbotsLfgPreferredSpecTab(kDruid, PLAYERBOTS_LFG_ROLE_DPS, false), 0);
}

TEST(PlayerbotsLfgFill, LootConflictFallbackAfterWait)
{
    EXPECT_FALSE(PlayerbotsLfgAllowLootConflict(100, 129, 30));
    EXPECT_TRUE(PlayerbotsLfgAllowLootConflict(100, 130, 30));
    EXPECT_FALSE(PlayerbotsLfgAllowLootConflict(0, 200, 30));
}
