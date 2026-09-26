#include "gtest/gtest.h"
#include "../../modules/mod-playerbots/src/Mgr/Guild/PlayerbotsUnguildedPool.h"

namespace
{
uint8_t const kAlliance = 0;
uint8_t const kHorde = 1;
uint8_t const kWarrior = 1;
uint8_t const kPaladin = 2;
uint8_t const kDeathKnight = 6;
uint8_t const kDruid = 11;
uint8_t const kUnknownClass = 10;

PlayerbotsUnguildedPoolSnap MakeSnap(uint8_t team, uint8_t cls, bool unguilded, bool isRandomBot,
                                     bool hasActivePlayerMaster)
{
    PlayerbotsUnguildedPoolSnap snap;
    snap.team = team;
    snap.cls = cls;
    snap.unguilded = unguilded;
    snap.isRandomBot = isRandomBot;
    snap.hasActivePlayerMaster = hasActivePlayerMaster;
    return snap;
}
}

TEST(PlayerbotsUnguildedPool, NeedsHolderWhenEmpty)
{
    std::vector<PlayerbotsUnguildedPoolSnap> snaps;
    EXPECT_TRUE(PlayerbotsPoolNeedsHolder(snaps, kAlliance, kWarrior, false));
    EXPECT_TRUE(PlayerbotsPoolNeedsHolder(snaps, kHorde, kDruid, false));
}

TEST(PlayerbotsUnguildedPool, FirstOfClassKeepsUnguilded)
{
    std::vector<PlayerbotsUnguildedPoolSnap> snaps;
    snaps.push_back(MakeSnap(kAlliance, kWarrior, true, true, false));

    EXPECT_EQ(PlayerbotsPoolCountHolders(snaps, kAlliance, kWarrior), 1u);
    EXPECT_TRUE(PlayerbotsPoolShouldKeepUnguilded(true, snaps, kAlliance, kWarrior, false));
    EXPECT_FALSE(PlayerbotsPoolNeedsHolder(snaps, kAlliance, kWarrior, false));
}

TEST(PlayerbotsUnguildedPool, SecondOfClassDoesNotKeepUnguilded)
{
    std::vector<PlayerbotsUnguildedPoolSnap> snaps;
    snaps.push_back(MakeSnap(kAlliance, kWarrior, true, true, false));
    snaps.push_back(MakeSnap(kAlliance, kWarrior, true, true, false));

    EXPECT_EQ(PlayerbotsPoolCountHolders(snaps, kAlliance, kWarrior), 2u);
    EXPECT_FALSE(PlayerbotsPoolShouldKeepUnguilded(true, snaps, kAlliance, kWarrior, false));
}

TEST(PlayerbotsUnguildedPool, DeathKnightExcludedWhenDisabled)
{
    EXPECT_TRUE(PlayerbotsPoolIsPlayableClass(kDeathKnight, false));
    EXPECT_FALSE(PlayerbotsPoolIsPlayableClass(kDeathKnight, true));
    EXPECT_FALSE(PlayerbotsPoolIsPlayableClass(kUnknownClass, false));

    std::vector<PlayerbotsUnguildedPoolSnap> snaps;
    EXPECT_FALSE(PlayerbotsPoolNeedsHolder(snaps, kAlliance, kDeathKnight, true));
    EXPECT_TRUE(PlayerbotsPoolNeedsHolder(snaps, kAlliance, kDeathKnight, false));
}

TEST(PlayerbotsUnguildedPool, HolderIgnoresGuildedAltsAndOtherTeams)
{
    std::vector<PlayerbotsUnguildedPoolSnap> snaps;
    snaps.push_back(MakeSnap(kAlliance, kWarrior, false, true, false));
    snaps.push_back(MakeSnap(kAlliance, kWarrior, true, true, true));
    snaps.push_back(MakeSnap(kAlliance, kWarrior, true, false, false));
    snaps.push_back(MakeSnap(kHorde, kWarrior, true, true, false));
    snaps.push_back(MakeSnap(kAlliance, kPaladin, true, true, false));

    EXPECT_EQ(PlayerbotsPoolCountHolders(snaps, kAlliance, kWarrior), 0u);
    EXPECT_TRUE(PlayerbotsPoolNeedsHolder(snaps, kAlliance, kWarrior, false));
}

TEST(PlayerbotsUnguildedPool, CannotUnguildLeaderLastPlayerGuildOrCombat)
{
    EXPECT_FALSE(PlayerbotsPoolCanUnguildMember(true, 10, false, false));
    EXPECT_FALSE(PlayerbotsPoolCanUnguildMember(false, 1, false, false));
    EXPECT_FALSE(PlayerbotsPoolCanUnguildMember(false, 10, true, false));
    EXPECT_FALSE(PlayerbotsPoolCanUnguildMember(false, 10, false, true));
}

TEST(PlayerbotsUnguildedPool, CanUnguildNormalMember)
{
    EXPECT_TRUE(PlayerbotsPoolCanUnguildMember(false, 10, false, false));
}

TEST(PlayerbotsUnguildedPool, DisabledConfigDoesNotKeepUnguilded)
{
    std::vector<PlayerbotsUnguildedPoolSnap> snaps;
    snaps.push_back(MakeSnap(kAlliance, kWarrior, true, true, false));
    EXPECT_FALSE(PlayerbotsPoolShouldKeepUnguilded(false, snaps, kAlliance, kWarrior, false));
}
