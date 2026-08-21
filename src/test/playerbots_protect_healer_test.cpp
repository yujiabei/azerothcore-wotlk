#include "gtest/gtest.h"
#include "../../modules/mod-playerbots/src/Ai/Base/Value/PlayerbotsProtectHealer.h"

TEST(PlayerbotsProtectHealer, ClassifyPrefersHealerOverTankAndDps)
{
    EXPECT_EQ(PlayerbotsClassifyVictim(true, false, true, true, false), PlayerbotsProtectVictimKind::Healer);
    EXPECT_EQ(PlayerbotsClassifyVictim(true, false, true, false, true), PlayerbotsProtectVictimKind::Tank);
    EXPECT_EQ(PlayerbotsClassifyVictim(true, false, true, false, false), PlayerbotsProtectVictimKind::Other);
    EXPECT_EQ(PlayerbotsClassifyVictim(true, true, true, false, true), PlayerbotsProtectVictimKind::SelfTank);
    EXPECT_EQ(PlayerbotsClassifyVictim(false, false, false, false, false), PlayerbotsProtectVictimKind::None);
}

TEST(PlayerbotsProtectHealer, TankPicksBossOnHealerOverAddInMelee)
{
    PlayerbotsTankTargetInfo bossOnHealer;
    bossOnHealer.victim = PlayerbotsProtectVictimKind::Healer;
    bossOnHealer.hasAggro = false;
    bossOnHealer.inMelee = false;
    bossOnHealer.distance = 25.0f;
    bossOnHealer.creatureRank = 3;

    PlayerbotsTankTargetInfo addInMelee;
    addInMelee.victim = PlayerbotsProtectVictimKind::SelfTank;
    addInMelee.hasAggro = true;
    addInMelee.inMelee = true;
    addInMelee.isCurrentTarget = true;
    addInMelee.distance = 3.0f;

    EXPECT_GT(PlayerbotsTankTargetTier(bossOnHealer), PlayerbotsTankTargetTier(addInMelee));
    EXPECT_TRUE(PlayerbotsTankTargetIsBetter(bossOnHealer, addInMelee, false));
}

TEST(PlayerbotsProtectHealer, TankPicksAddOnHealerOverBossOnTank)
{
    PlayerbotsTankTargetInfo addOnHealer;
    addOnHealer.victim = PlayerbotsProtectVictimKind::Healer;
    addOnHealer.distance = 8.0f;
    addOnHealer.creatureRank = 0;

    PlayerbotsTankTargetInfo bossOnTank;
    bossOnTank.victim = PlayerbotsProtectVictimKind::SelfTank;
    bossOnTank.hasAggro = true;
    bossOnTank.inMelee = true;
    bossOnTank.isCurrentTarget = true;
    bossOnTank.creatureRank = 3;

    EXPECT_TRUE(PlayerbotsTankTargetIsBetter(addOnHealer, bossOnTank, false));
}

TEST(PlayerbotsProtectHealer, AmongHealerAttackersPreferBossOverAdd)
{
    PlayerbotsTankTargetInfo bossOnHealer;
    bossOnHealer.victim = PlayerbotsProtectVictimKind::Healer;
    bossOnHealer.distance = 20.0f;
    bossOnHealer.creatureRank = 3;

    PlayerbotsTankTargetInfo addOnHealer;
    addOnHealer.victim = PlayerbotsProtectVictimKind::Healer;
    addOnHealer.distance = 5.0f;
    addOnHealer.creatureRank = 0;

    EXPECT_TRUE(PlayerbotsTankTargetIsBetter(bossOnHealer, addOnHealer, false));
}

TEST(PlayerbotsProtectHealer, NoAggroStillPicksCloserAdd)
{
    PlayerbotsTankTargetInfo closer;
    closer.hasAggro = false;
    closer.distance = 5.0f;

    PlayerbotsTankTargetInfo farther;
    farther.hasAggro = false;
    farther.distance = 20.0f;

    EXPECT_TRUE(PlayerbotsTankTargetIsBetter(closer, farther, false));
}

TEST(PlayerbotsProtectHealer, HealerVictimOverridesMainTankStickiness)
{
    PlayerbotsTankTargetInfo bossOnHealer;
    bossOnHealer.victim = PlayerbotsProtectVictimKind::Healer;

    PlayerbotsTankTargetInfo currentAdd;
    currentAdd.hasAggro = true;
    currentAdd.inMelee = true;
    currentAdd.isCurrentTarget = true;

    EXPECT_TRUE(PlayerbotsTankTargetIsBetter(bossOnHealer, currentAdd, true));
}

TEST(PlayerbotsProtectHealer, SwitchToHealerAttackerWithoutCurrentAggro)
{
    EXPECT_TRUE(PlayerbotsShouldSwitchTankTarget(true, false, false, true, false));
    EXPECT_FALSE(PlayerbotsShouldSwitchTankTarget(true, true, false, true, false));
    EXPECT_FALSE(PlayerbotsShouldSwitchTankTarget(true, false, false, false, false));
    EXPECT_TRUE(PlayerbotsShouldSwitchTankTarget(true, false, true, false, false));
    EXPECT_TRUE(PlayerbotsShouldSwitchTankTarget(false, false, false, false, false));
}

TEST(PlayerbotsProtectHealer, SkipSkullWhenHealerIsAttackedElsewhere)
{
    EXPECT_TRUE(PlayerbotsShouldSkipRtiForHealer(true, false));
    EXPECT_FALSE(PlayerbotsShouldSkipRtiForHealer(true, true));
    EXPECT_FALSE(PlayerbotsShouldSkipRtiForHealer(false, false));
}

TEST(PlayerbotsProtectHealer, DpsOnlyBoostsHealerAttackers)
{
    EXPECT_EQ(PlayerbotsDpsVictimPriority(PlayerbotsProtectVictimKind::Healer), 2);
    EXPECT_EQ(PlayerbotsDpsVictimPriority(PlayerbotsProtectVictimKind::Other), 0);
    EXPECT_EQ(PlayerbotsDpsVictimPriority(PlayerbotsProtectVictimKind::Tank), 0);
}

TEST(PlayerbotsProtectHealer, ProtectHealerWheneverAttacked)
{
    EXPECT_TRUE(PlayerbotsNeedsProtection(PlayerbotsProtectVictimKind::Healer, true, 95.0f, 5.0f));
    EXPECT_FALSE(PlayerbotsNeedsProtection(PlayerbotsProtectVictimKind::Healer, true, 95.0f, 40.0f));
    EXPECT_FALSE(PlayerbotsNeedsProtection(PlayerbotsProtectVictimKind::Other, true, 50.0f, 5.0f));
    EXPECT_TRUE(PlayerbotsNeedsProtection(PlayerbotsProtectVictimKind::Other, true, 30.0f, 5.0f));
    EXPECT_FALSE(PlayerbotsNeedsProtection(PlayerbotsProtectVictimKind::Tank, true, 20.0f, 5.0f));
    EXPECT_TRUE(PlayerbotsNeedsProtection(PlayerbotsProtectVictimKind::Tank, true, 8.0f, 5.0f));
}

TEST(PlayerbotsProtectHealer, ProtectScorePrefersLowHpHealer)
{
    EXPECT_GT(PlayerbotsProtectScore(PlayerbotsProtectVictimKind::Healer, 40.0f),
              PlayerbotsProtectScore(PlayerbotsProtectVictimKind::Healer, 80.0f));
    EXPECT_GT(PlayerbotsProtectScore(PlayerbotsProtectVictimKind::Healer, 90.0f),
              PlayerbotsProtectScore(PlayerbotsProtectVictimKind::Other, 10.0f));
}
