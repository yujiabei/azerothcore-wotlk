#include "gtest/gtest.h"
#include "../../modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsBgFill.h"

TEST(PlayerbotsBgFill, LevelWindowRequiresRealPlayer)
{
    // No real players → no proximity constraint
    EXPECT_TRUE(PlayerbotsBgBotLevelInWindow(19, 0, 1, 10, 19));
}

TEST(PlayerbotsBgFill, LevelWindowAllowsAtCap)
{
    EXPECT_TRUE(PlayerbotsBgBotLevelInWindow(11, 10, 1, 10, 19));
    EXPECT_FALSE(PlayerbotsBgBotLevelInWindow(12, 10, 1, 10, 19));
}

TEST(PlayerbotsBgFill, LevelWindowMustStayInBracket)
{
    EXPECT_FALSE(PlayerbotsBgBotLevelInWindow(9, 10, 1, 10, 19));
    EXPECT_FALSE(PlayerbotsBgBotLevelInWindow(20, 19, 1, 10, 19));
}

TEST(PlayerbotsBgFill, TargetLevelClampedToBracket)
{
    // rolled 10, bracket 10-19, server 80 → 10
    EXPECT_EQ(PlayerbotsBgRollTargetLevel(10, 1, 10, 19, 80, 10), 10u);
    // rolled 11 → 11
    EXPECT_EQ(PlayerbotsBgRollTargetLevel(10, 1, 10, 19, 80, 11), 11u);
    // maxPlayerLevel 18, rolled 19, bracket max 19 → 19
    EXPECT_EQ(PlayerbotsBgRollTargetLevel(18, 1, 10, 19, 80, 19), 19u);
    // clamp to server max
    EXPECT_EQ(PlayerbotsBgRollTargetLevel(80, 1, 80, 80, 80, 81), 80u);
}

TEST(PlayerbotsBgFill, FactionSingleAllianceEnemyCannotExceed)
{
    // Player side Alliance: a=1,h=1 → Horde may not join (would be 1v2)
    EXPECT_FALSE(PlayerbotsBgFactionAllowsJoin(1, 1, 1 /*Horde*/, PLAYERBOTS_BG_REAL_ALLIANCE));
    // a=2,h=1 → Horde may join → 2v2
    EXPECT_TRUE(PlayerbotsBgFactionAllowsJoin(2, 1, 1 /*Horde*/, PLAYERBOTS_BG_REAL_ALLIANCE));
    // Alliance joining always OK under single-side rule
    EXPECT_TRUE(PlayerbotsBgFactionAllowsJoin(1, 1, 0 /*Alliance*/, PLAYERBOTS_BG_REAL_ALLIANCE));
}

TEST(PlayerbotsBgFill, FactionBothSidesOnlyBalance)
{
    // a=2,h=3 → only Alliance may join to equalize
    EXPECT_TRUE(PlayerbotsBgFactionAllowsJoin(2, 3, 0 /*Alliance*/, PLAYERBOTS_BG_REAL_BOTH));
    EXPECT_FALSE(PlayerbotsBgFactionAllowsJoin(2, 3, 1 /*Horde*/, PLAYERBOTS_BG_REAL_BOTH));
    // already equal → neither
    EXPECT_FALSE(PlayerbotsBgFactionAllowsJoin(3, 3, 0, PLAYERBOTS_BG_REAL_BOTH));
    EXPECT_FALSE(PlayerbotsBgFactionAllowsJoin(3, 3, 1, PLAYERBOTS_BG_REAL_BOTH));
}

TEST(PlayerbotsBgFill, FactionNoRealPlayersUnconstrained)
{
    EXPECT_TRUE(PlayerbotsBgFactionAllowsJoin(1, 5, 1 /*Horde*/, PLAYERBOTS_BG_REAL_NONE));
}
