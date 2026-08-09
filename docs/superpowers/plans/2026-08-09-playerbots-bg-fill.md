# Playerbots BG Fill (Level-Near Player) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a real player queues a battleground, random bots fill so the match can start, with bot level ≤ queue max real-player level + N, green-tier gear, and enemy team size never exceeding the real players' side.

**Architecture:** Extend `BattlegroundInfo` in `CheckBgQueue` with per-bracket real-player max level and side flags. Pure helpers (unit-tested) decide level window, target level, and faction balance. `BGJoinAction` applies those rules in the BG branch of `shouldJoinBg` / `isUseful`, and releveis idle bots once via `PlayerbotFactory` when the window has no eligible bot. Config adds `AiPlayerbot.BgJoinMaxLevelAbovePlayer`; runtime conf lowers `RandomGearQualityLimit` and `Battleground.Override.LowLevels.MinPlayers`.

**Tech Stack:** AzerothCore C++20, `mod-playerbots`, Google Test (`src/test`), existing `PlayerbotFactory` / `BattlegroundData` / `BGJoinAction` path.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-09-playerbots-bg-fill-design.md`
- Do not enable `AiPlayerbot.RandomBotAutoJoinBG` (keep `0`)
- Do not change Arena join logic beyond leaving it untouched
- Do not change in-BG tactical AI
- Do not edit `battleground_template` SQL
- Level proximity applies only when the bracket has at least one real player (`maxPlayerLevel > 0`)
- Faction balance applies only when the bracket has at least one real player
- Single-side real players: after join, `enemy <= our`; both sides: after join must `a == h` (only top up the smaller side)
- Code style: Allman braces, 4-space indent, `{}` format strings, `auto const&`, `Type const*`
- Do not configure/build/install unless the user explicitly asks
- Do not commit unless the user explicitly asks (skip git commit steps if not requested)
- `modules/mod-playerbots` is gitignored by the root repo; still edit those files on disk — use absolute paths / `--no-ignore` when searching

## File Structure

| File | Responsibility |
|------|----------------|
| `modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsBgFill.h` | Header-only pure helpers: level window, target level clamp, faction balance |
| `src/test/playerbots_bg_fill_test.cpp` | Google Test for those helpers |
| `modules/mod-playerbots/src/PlayerbotAIConfig.h` / `.cpp` | `bgJoinMaxLevelAbovePlayer` member + `GetOption` load |
| `modules/mod-playerbots/conf/playerbots.conf.dist` | Document new config; default gear quality → 2 |
| `env/dist/etc/modules/playerbots.conf` | Runtime: gear quality 2, new config = 1 |
| `env/dist/etc/worldserver.conf` | Runtime: `Battleground.Override.LowLevels.MinPlayers = 1` |
| `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.h` | Extend `BattlegroundInfo` |
| `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp` | Record max level / side in `CheckBgQueue`; log new fields |
| `modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.h` / `.cpp` | Level window, faction balance, relevel-then-join; keep `FreeBGJoinAction` in sync |

---

### Task 1: Pure fill helpers + unit tests

**Files:**
- Create: `modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsBgFill.h`
- Create: `src/test/playerbots_bg_fill_test.cpp`

**Interfaces:**
- Consumes: nothing (no WoW runtime types beyond `<cstdint>`)
- Produces:
  - `enum : uint8 { PLAYERBOTS_BG_REAL_NONE = 0, PLAYERBOTS_BG_REAL_ALLIANCE = 1, PLAYERBOTS_BG_REAL_HORDE = 2, PLAYERBOTS_BG_REAL_BOTH = 3 };`
  - `bool PlayerbotsBgHasRealPlayers(uint8 realPlayerSide);`
  - `bool PlayerbotsBgBotLevelInWindow(uint32 botLevel, uint32 maxPlayerLevel, uint32 maxAbove, uint32 bracketMin, uint32 bracketMax);`
  - `uint32 PlayerbotsBgRollTargetLevel(uint32 maxPlayerLevel, uint32 maxAbove, uint32 bracketMin, uint32 bracketMax, uint32 serverMaxLevel, uint32 rolledInclusive);` — `rolledInclusive` is a pre-rolled value in `[maxPlayerLevel, maxPlayerLevel + maxAbove]` so tests stay deterministic; implementation clamps to bracket and server max
  - `bool PlayerbotsBgFactionAllowsJoin(uint32 allianceCount, uint32 hordeCount, uint8 botTeam /*0=Alliance,1=Horde*/, uint8 realPlayerSide);`

- [ ] **Step 1: Write the failing tests**

Create `src/test/playerbots_bg_fill_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Run tests to verify they fail to compile / link**

Run (from an existing build dir with `-DBUILD_TESTING=ON` if available):

```powershell
# from build tree
ctest -R playerbots_bg_fill -V
# or
.\src\test\unit_tests.exe --gtest_filter=PlayerbotsBgFill.*
```

Expected: FAIL (header / symbols missing). If `BUILD_TESTING` is off, skip run and proceed — compile failure of the new test file is enough signal when tests are next configured.

- [ ] **Step 3: Implement the header**

Create `modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsBgFill.h`:

```cpp
#ifndef PLAYERBOTS_BG_FILL_H
#define PLAYERBOTS_BG_FILL_H

#include <algorithm>
#include <cstdint>

enum : uint8_t
{
    PLAYERBOTS_BG_REAL_NONE = 0,
    PLAYERBOTS_BG_REAL_ALLIANCE = 1,
    PLAYERBOTS_BG_REAL_HORDE = 2,
    PLAYERBOTS_BG_REAL_BOTH = 3
};

inline bool PlayerbotsBgHasRealPlayers(uint8_t realPlayerSide)
{
    return realPlayerSide != PLAYERBOTS_BG_REAL_NONE;
}

inline bool PlayerbotsBgBotLevelInWindow(uint32_t botLevel, uint32_t maxPlayerLevel, uint32_t maxAbove,
                                         uint32_t bracketMin, uint32_t bracketMax)
{
    if (maxPlayerLevel == 0)
        return true;

    if (botLevel < bracketMin || botLevel > bracketMax)
        return false;

    return botLevel <= maxPlayerLevel + maxAbove;
}

inline uint32_t PlayerbotsBgRollTargetLevel(uint32_t maxPlayerLevel, uint32_t maxAbove, uint32_t bracketMin,
                                            uint32_t bracketMax, uint32_t serverMaxLevel, uint32_t rolledInclusive)
{
    uint32_t level = rolledInclusive;
    uint32_t const windowLo = maxPlayerLevel;
    uint32_t const windowHi = maxPlayerLevel + maxAbove;
    if (level < windowLo)
        level = windowLo;
    if (level > windowHi)
        level = windowHi;
    if (level < bracketMin)
        level = bracketMin;
    if (level > bracketMax)
        level = bracketMax;
    if (level > serverMaxLevel)
        level = serverMaxLevel;
    return level;
}

// botTeam: 0 = Alliance, 1 = Horde (matches TeamId)
inline bool PlayerbotsBgFactionAllowsJoin(uint32_t allianceCount, uint32_t hordeCount, uint8_t botTeam,
                                          uint8_t realPlayerSide)
{
    if (realPlayerSide == PLAYERBOTS_BG_REAL_NONE)
        return true;

    if (realPlayerSide == PLAYERBOTS_BG_REAL_BOTH)
    {
        if (botTeam == 0)
            return (allianceCount + 1u) == hordeCount;
        return (hordeCount + 1u) == allianceCount;
    }

    uint32_t our = 0;
    uint32_t enemy = 0;
    if (realPlayerSide == PLAYERBOTS_BG_REAL_ALLIANCE)
    {
        our = allianceCount;
        enemy = hordeCount;
    }
    else
    {
        our = hordeCount;
        enemy = allianceCount;
    }

    bool const joiningOurs =
        (realPlayerSide == PLAYERBOTS_BG_REAL_ALLIANCE && botTeam == 0) ||
        (realPlayerSide == PLAYERBOTS_BG_REAL_HORDE && botTeam == 1);

    if (joiningOurs)
        return true;

    return (enemy + 1u) <= our;
}

#endif
```

- [ ] **Step 4: Run tests to verify they pass**

```powershell
.\src\test\unit_tests.exe --gtest_filter=PlayerbotsBgFill.*
```

Expected: PASS (all `PlayerbotsBgFill` tests).

- [ ] **Step 5: Commit (only if user asked)**

```bash
git add modules/mod-playerbots/src/Ai/Base/Actions/PlayerbotsBgFill.h \
  src/test/playerbots_bg_fill_test.cpp
git commit -m "$(cat <<'EOF'
feat(playerbots): add pure BG fill level/faction helpers

EOF
)"
```

---

### Task 2: Config — `BgJoinMaxLevelAbovePlayer`, gear cap, low-level MinPlayers

**Files:**
- Modify: `modules/mod-playerbots/src/PlayerbotAIConfig.h` (near `randomBotJoinBG`)
- Modify: `modules/mod-playerbots/src/PlayerbotAIConfig.cpp` (near `randomBotJoinBG` load)
- Modify: `modules/mod-playerbots/conf/playerbots.conf.dist` (BG section + gear default)
- Modify: `env/dist/etc/modules/playerbots.conf`
- Modify: `env/dist/etc/worldserver.conf` (LowLevels MinPlayers only — do not change `.dist` defaults unless product policy says so; this install's runtime conf is enough for acceptance)

**Interfaces:**
- Consumes: Task 1 helpers (none yet at runtime)
- Produces: `sPlayerbotAIConfig.bgJoinMaxLevelAbovePlayer` (`uint32`, default `1`)

- [ ] **Step 1: Add member + load**

In `PlayerbotAIConfig.h` after `bool randomBotAutoJoinBG;`:

```cpp
    uint32 bgJoinMaxLevelAbovePlayer;
```

In `PlayerbotAIConfig.cpp` after `randomBotAutoJoinBG = ...`:

```cpp
    bgJoinMaxLevelAbovePlayer = sConfigMgr->GetOption<uint32>("AiPlayerbot.BgJoinMaxLevelAbovePlayer", 1);
```

- [ ] **Step 2: Document in `playerbots.conf.dist`**

After `AiPlayerbot.RandomBotAutoJoinBG = 0` block (~line 1270), add:

```conf
# When real players are in a BG queue bracket, random bots may only join if
# botLevel <= (highest real player level in that bracket) + this value.
# Bots outside the window may be temporarily re-leveled to fill.
# Default: 1
AiPlayerbot.BgJoinMaxLevelAbovePlayer = 1
```

Change gear default in the same dist file:

```conf
# Default: 2 (uncommon / green)
AiPlayerbot.RandomGearQualityLimit = 2
```

(keep the existing quality comment list; only change the Default line and value from `3` to `2`)

- [ ] **Step 3: Apply runtime conf for this install**

In `env/dist/etc/modules/playerbots.conf`:

```conf
AiPlayerbot.RandomGearQualityLimit = 2
AiPlayerbot.RandomBotJoinBG = 1
AiPlayerbot.RandomBotAutoJoinBG = 0
AiPlayerbot.BgJoinMaxLevelAbovePlayer = 1
```

In `env/dist/etc/worldserver.conf`:

```conf
Battleground.Override.LowLevels.MinPlayers = 1
```

- [ ] **Step 4: Note on existing bot gear**

Existing random bots keep previously rolled blues until the next `Randomize` / BG relevel path. No mass re-roll command is required for acceptance if BG relevel uses `InitEquipment(false, true)` (Task 5). Optional operator step if open-world blues matter: wait for periodic randomize, or GM `.playerbots` init on specific bots.

- [ ] **Step 5: Commit (only if user asked)**

```bash
git add modules/mod-playerbots/src/PlayerbotAIConfig.h \
  modules/mod-playerbots/src/PlayerbotAIConfig.cpp \
  modules/mod-playerbots/conf/playerbots.conf.dist
git commit -m "$(cat <<'EOF'
feat(playerbots): add BgJoinMaxLevelAbovePlayer and green gear default

EOF
)"
```

(`env/dist/etc/*` is typically local runtime — commit only if this repo tracks it and the user wants it.)

---

### Task 3: Record `maxPlayerLevel` + `realPlayerSide` in `CheckBgQueue`

**Files:**
- Modify: `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.h` (`BattlegroundInfo`)
- Modify: `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp` (`CheckBgQueue` real-player BG branch + `LogBattlegroundInfo`)

**Interfaces:**
- Consumes: none from Task 1 at write time (side constants should match `PlayerbotsBgFill.h`)
- Produces: per `BattlegroundData[queueTypeId][bracketId]`:
  - `uint32 maxPlayerLevel = 0;`
  - `uint8 realPlayerSide = 0;` — `PLAYERBOTS_BG_REAL_*`

- [ ] **Step 1: Extend struct**

In `BattlegroundInfo` after `activeBgQueue`:

```cpp
    uint32 maxPlayerLevel = 0;   // highest real player level in this bracket queue; 0 = none
    uint8 realPlayerSide = 0;    // PLAYERBOTS_BG_REAL_* bit flags / BOTH
```

Include the fill header from the cpp (not required in the mgr header) when assigning constants, or duplicate the numeric values `1/2/3` with a comment pointing at `PlayerbotsBgFill.h`. Prefer `#include "PlayerbotsBgFill.h"` in `RandomPlayerbotMgr.cpp`.

- [ ] **Step 2: Update real-player BG branch in `CheckBgQueue`**

Inside the `else` BG block (~lines 975–1009), when counting a real player for Alliance/Horde, also:

```cpp
            else
            {
                if (teamId == TEAM_ALLIANCE)
                {
                    BattlegroundData[queueTypeId][bracketId].bgAlliancePlayerCount++;
                    if (BattlegroundData[queueTypeId][bracketId].realPlayerSide == PLAYERBOTS_BG_REAL_NONE)
                        BattlegroundData[queueTypeId][bracketId].realPlayerSide = PLAYERBOTS_BG_REAL_ALLIANCE;
                    else if (BattlegroundData[queueTypeId][bracketId].realPlayerSide == PLAYERBOTS_BG_REAL_HORDE)
                        BattlegroundData[queueTypeId][bracketId].realPlayerSide = PLAYERBOTS_BG_REAL_BOTH;
                }
                else
                {
                    BattlegroundData[queueTypeId][bracketId].bgHordePlayerCount++;
                    if (BattlegroundData[queueTypeId][bracketId].realPlayerSide == PLAYERBOTS_BG_REAL_NONE)
                        BattlegroundData[queueTypeId][bracketId].realPlayerSide = PLAYERBOTS_BG_REAL_HORDE;
                    else if (BattlegroundData[queueTypeId][bracketId].realPlayerSide == PLAYERBOTS_BG_REAL_ALLIANCE)
                        BattlegroundData[queueTypeId][bracketId].realPlayerSide = PLAYERBOTS_BG_REAL_BOTH;
                }

                uint32 const playerLevel = player->GetLevel();
                if (playerLevel > BattlegroundData[queueTypeId][bracketId].maxPlayerLevel)
                    BattlegroundData[queueTypeId][bracketId].maxPlayerLevel = playerLevel;

                // existing InBattleground instance bookkeeping unchanged...
```

Keep existing `activeBgQueue = 1` when `!IsInvitedForBattlegroundInstance() && !InBattleground()`.

Do **not** set `maxPlayerLevel` / `realPlayerSide` from bots. AutoJoin block stays as-is (still gated by `randomBotAutoJoinBG`).

- [ ] **Step 3: Extend `LogBattlegroundInfo` BG line**

Append max level + side to the existing `LOG_INFO` for BGs, e.g.:

```cpp
            LOG_INFO("playerbots",
                     "BG:{} {}: Player ({}:{}) Bot ({}:{}) Total (A:{} H:{}), Instances {}, Active Queue: {}, "
                     "MaxPlayerLevel: {}, RealSide: {}",
                     _bgType,
                     std::to_string(bgInfo.minLevel) + "-" + std::to_string(bgInfo.maxLevel),
                     bgInfo.bgAlliancePlayerCount, bgInfo.bgHordePlayerCount, bgInfo.bgAllianceBotCount,
                     bgInfo.bgHordeBotCount, bgInfo.bgAlliancePlayerCount + bgInfo.bgAllianceBotCount,
                     bgInfo.bgHordePlayerCount + bgInfo.bgHordeBotCount, bgInfo.bgInstanceCount, bgInfo.activeBgQueue,
                     bgInfo.maxPlayerLevel, uint32(bgInfo.realPlayerSide));
```

- [ ] **Step 4: Manual smoke (optional without rebuild if not asked)**

After rebuild+restart (only when user asks): queue a real player for WSG; within ~35s `Playerbots.log` / server log should show `MaxPlayerLevel` matching the character and `RealSide` 1 or 2.

- [ ] **Step 5: Commit (only if user asked)**

```bash
git add modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.h \
  modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp
git commit -m "$(cat <<'EOF'
feat(playerbots): track BG queue real-player level and side

EOF
)"
```

---

### Task 4: Faction balance in `shouldJoinBg` (BG branch only)

**Files:**
- Modify: `modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.cpp` (`BGJoinAction::shouldJoinBg` and `FreeBGJoinAction::shouldJoinBg`)

**Interfaces:**
- Consumes: `PlayerbotsBgFactionAllowsJoin`, `BattlegroundInfo.realPlayerSide`, existing `a/h` counts
- Produces: BG join returns true only if slot capacity **and** faction helper allow

- [ ] **Step 1: Patch `BGJoinAction::shouldJoinBg` BG branch**

Replace the BG branch (~290–309) with:

```cpp
    // Check if bots should join Battleground
    auto const& bgInfo = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
    uint32 const bgAllianceBotCount = bgInfo.bgAllianceBotCount;
    uint32 const bgAlliancePlayerCount = bgInfo.bgAlliancePlayerCount;
    uint32 const bgHordeBotCount = bgInfo.bgHordeBotCount;
    uint32 const bgHordePlayerCount = bgInfo.bgHordePlayerCount;
    uint32 const activeBgQueue = bgInfo.activeBgQueue;
    uint32 const bgInstanceCount = bgInfo.bgInstanceCount;

    uint32 const allianceTotal = bgAllianceBotCount + bgAlliancePlayerCount;
    uint32 const hordeTotal = bgHordeBotCount + bgHordePlayerCount;

    bool hasSlot = false;
    if (teamId == TEAM_ALLIANCE)
        hasSlot = allianceTotal < TeamSize * (activeBgQueue + bgInstanceCount);
    else
        hasSlot = hordeTotal < TeamSize * (activeBgQueue + bgInstanceCount);

    if (!hasSlot)
        return false;

    uint8_t const botTeam = (teamId == TEAM_ALLIANCE) ? 0 : 1;
    if (!PlayerbotsBgFactionAllowsJoin(allianceTotal, hordeTotal, botTeam, bgInfo.realPlayerSide))
        return false;

    return true;
```

Add `#include "PlayerbotsBgFill.h"` at the top of the cpp.

Leave the entire Arena branch (`type != ARENA_TYPE_NONE`) unchanged.

- [ ] **Step 2: Apply the same BG-branch logic to `FreeBGJoinAction::shouldJoinBg`**

Copy the same replacement into the FreeBG BG branch (~621–640). Do not refactor into a virtual unless the duplication is painful — if extracting, add a protected helper on `BGJoinAction`:

```cpp
bool BGJoinAction::passesBgSlotAndFaction(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId,
                                          uint32 TeamSize) const;
```

and call it from both overrides' BG branches. Prefer the helper if touching both sites.

- [ ] **Step 3: Sanity check with helpers (already unit-tested in Task 1)**

No new gtest required here — runtime wiring only.

- [ ] **Step 4: Commit (only if user asked)**

```bash
git add modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.cpp \
  modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.h
git commit -m "$(cat <<'EOF'
feat(playerbots): enforce BG fill faction balance vs real players

EOF
)"
```

---

### Task 5: Level window + relevel-then-join in `BGJoinAction`

**Files:**
- Modify: `modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.h`
- Modify: `modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.cpp`
- Uses: `PlayerbotFactory.h`, `PlayerbotsBgFill.h`, `PlayerbotAIConfig`, `Random.h` (`urand`)

**Interfaces:**
- Consumes: Task 1–4 (`maxPlayerLevel`, `realPlayerSide`, `bgJoinMaxLevelAbovePlayer`, faction helper)
- Produces:
  - `std::vector<uint32> bgBracketList;` — parallel to `bgList` (same index → `BattlegroundBracketId`)
  - `bool BGJoinAction::canRelevelForBgFill(BattlegroundQueueTypeId, BattlegroundBracketId) const;`
  - `bool BGJoinAction::relevelForBgFill(BattlegroundQueueTypeId, BattlegroundBracketId);`
  - Updated `isUseful` / `Execute` behavior below

- [ ] **Step 1: Extend header members / methods**

In `BGJoinAction` protected section:

```cpp
    bool JoinQueue(uint32 type);
    bool canRelevelForBgFill(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId) const;
    bool relevelForBgFill(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId);
    std::vector<uint32> bgList;
    std::vector<uint32> bgBracketList;
    std::vector<uint32> ratedList;
```

- [ ] **Step 2: Implement `canRelevelForBgFill`**

```cpp
bool BGJoinAction::canRelevelForBgFill(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId) const
{
    auto const& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
    if (!PlayerbotsBgHasRealPlayers(info.realPlayerSide) || info.maxPlayerLevel == 0)
        return false;

    if (!info.activeBgQueue)
        return false;

    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    if (botAI->HasActivePlayerMaster())
        return false;

    if (bot->IsInCombat() || bot->InBattleground() || bot->InBattlegroundQueue())
        return false;

    uint32 const maxAbove = sPlayerbotAIConfig.bgJoinMaxLevelAbovePlayer;
    uint32 const serverMax = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 preview = PlayerbotsBgRollTargetLevel(info.maxPlayerLevel, maxAbove, info.minLevel, info.maxLevel,
                                                 serverMax, info.maxPlayerLevel);
    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        uint32 const dkMin = sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);
        if (preview < dkMin)
            preview = dkMin;
        // After DK minimum clamp, must still fit bracket + proximity window
        if (!PlayerbotsBgBotLevelInWindow(preview, info.maxPlayerLevel, maxAbove, info.minLevel, info.maxLevel))
            return false;
    }

    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return false;

    if (preview < bg->GetMinLevel() || preview > bg->GetMaxLevel())
        return false;

    return true;
}
```

- [ ] **Step 3: Implement `relevelForBgFill`**

```cpp
bool BGJoinAction::relevelForBgFill(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    auto const& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
    uint32 const maxAbove = sPlayerbotAIConfig.bgJoinMaxLevelAbovePlayer;
    uint32 const serverMax = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    uint32 const windowLo = info.maxPlayerLevel;
    uint32 const windowHi = info.maxPlayerLevel + maxAbove;
    uint32 const rolled = urand(windowLo, windowHi);
    uint32 targetLevel =
        PlayerbotsBgRollTargetLevel(info.maxPlayerLevel, maxAbove, info.minLevel, info.maxLevel, serverMax, rolled);

    if (bot->getClass() == CLASS_DEATH_KNIGHT)
    {
        uint32 const dkMin = sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL);
        if (targetLevel < dkMin)
            targetLevel = dkMin;
        if (targetLevel > info.maxLevel)
            return false;
    }

    LOG_INFO("playerbots", "Bot {} <{}> BG fill relevel {} -> {} (queue {} bracket maxPlayer {})",
             bot->GetGUID().ToString().c_str(), bot->GetName().c_str(), bot->GetLevel(), targetLevel,
             uint32(queueTypeId), info.maxPlayerLevel);

    PlayerbotFactory factory(bot, targetLevel);
    factory.Randomize(false);
    // EquipAndSpecPersistence may keep old gear/talents; force green-cap kit at the new level
    bot->resetTalents(true);
    factory.InitTalentsTree(false, true, true);
    factory.InitEquipment(false, true);

    return PlayerbotsBgBotLevelInWindow(bot->GetLevel(), info.maxPlayerLevel, maxAbove, info.minLevel, info.maxLevel);
}
```

Include `"PlayerbotFactory.h"` / `"World.h"` as needed.

- [ ] **Step 4: Update `isUseful` loop**

Clear both lists; for each bracket/queue:

```cpp
    bgList.clear();
    bgBracketList.clear();
    ratedList.clear();

    for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
    {
        for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
        {
            BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(queueType);
            BattlegroundBracketId bracketId = BattlegroundBracketId(bracket);

            ArenaType const arenaType = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
            if (arenaType != ARENA_TYPE_NONE)
            {
                if (!canJoinBg(queueTypeId, bracketId))
                    continue;
                if (shouldJoinBg(queueTypeId, bracketId))
                {
                    bgList.push_back(queueTypeId);
                    bgBracketList.push_back(bracketId);
                }
                continue;
            }

            auto const& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
            if (!info.activeBgQueue && !info.bgInstanceCount)
                continue;

            if (!shouldJoinBg(queueTypeId, bracketId))
                continue;

            uint32 const maxAbove = sPlayerbotAIConfig.bgJoinMaxLevelAbovePlayer;
            bool const inWindow = PlayerbotsBgBotLevelInWindow(bot->GetLevel(), info.maxPlayerLevel, maxAbove,
                                                               info.minLevel, info.maxLevel);

            if (canJoinBg(queueTypeId, bracketId) && inWindow)
            {
                bgList.push_back(queueTypeId);
                bgBracketList.push_back(bracketId);
                continue;
            }

            // Prefer in-window bots: only consider relevel when this bot is outside the window
            // (or outside the bracket) and the bracket has real players needing fill.
            if (!inWindow && canRelevelForBgFill(queueTypeId, bracketId))
            {
                bgList.push_back(queueTypeId);
                bgBracketList.push_back(bracketId);
            }
        }
    }
```

Note: when `maxPlayerLevel == 0` (AutoJoin / no real players), `PlayerbotsBgBotLevelInWindow` returns true for any level in bracket via `canJoinBg` path only — relevel branch requires real players inside `canRelevelForBgFill`. That matches the spec.

- [ ] **Step 5: Update `Execute` to relevel before `JoinQueue`**

When selecting from `bgList`, also take the matching `bgBracketList` entry. Before `JoinQueue`:

```cpp
bool BGJoinAction::Execute(Event /*event*/)
{
    uint32 queueType = AI_VALUE(uint32, "bg type");
    BattlegroundBracketId chosenBracket = BG_BRACKET_ID_FIRST;
    bool haveChosenBracket = false;

    if (!queueType)
    {
        if (bgList.empty() || bgBracketList.size() != bgList.size())
            return false;

        uint32 const idx = urand(0, bgList.size() - 1);
        BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(bgList[idx]);
        chosenBracket = BattlegroundBracketId(bgBracketList[idx]);
        haveChosenBracket = true;

        // ... keep existing arena gatherArenaTeam / isRated block using queueTypeId ...

        botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(queueTypeId);
        queueType = queueTypeId;
    }

    BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(queueType);
    if (ArenaType(BattlegroundMgr::BGArenaType(queueTypeId)) == ARENA_TYPE_NONE)
    {
        auto const& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][
            haveChosenBracket ? chosenBracket
                              : GetBattlegroundBracketByLevel(
                                    sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundMgr::BGTemplateId(queueTypeId))
                                        ->GetMapId(),
                                    bot->GetLevel())
                                    ->GetBracketId()];

        BattlegroundBracketId bracketId = haveChosenBracket
                                              ? chosenBracket
                                              : GetBattlegroundBracketByLevel(
                                                    sBattlegroundMgr->GetBattlegroundTemplate(
                                                                        BattlegroundMgr::BGTemplateId(queueTypeId))
                                                        ->GetMapId(),
                                                    bot->GetLevel())
                                                    ->GetBracketId();

        // Re-validate after other bots may have queued
        if (!shouldJoinBg(queueTypeId, bracketId))
            return false;

        uint32 const maxAbove = sPlayerbotAIConfig.bgJoinMaxLevelAbovePlayer;
        if (PlayerbotsBgHasRealPlayers(info.realPlayerSide) &&
            !PlayerbotsBgBotLevelInWindow(bot->GetLevel(), info.maxPlayerLevel, maxAbove, info.minLevel, info.maxLevel))
        {
            if (!canRelevelForBgFill(queueTypeId, bracketId))
                return false;
            if (!relevelForBgFill(queueTypeId, bracketId))
                return false;
            if (!shouldJoinBg(queueTypeId, bracketId))
                return false;
        }
    }

    return JoinQueue(queueType);
}
```

Keep null-checks on `GetBattlegroundTemplate` / `GetBattlegroundBracketByLevel` — the sketch above is logic-complete; match existing null-guard style in the file when implementing (early `return false` if template/bracket missing).

`JoinQueue` already derives `bracketId` from the bot's (possibly new) level — after relevel that must match `chosenBracket`.

- [ ] **Step 6: Commit (only if user asked)**

```bash
git add modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.h \
  modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.cpp
git commit -m "$(cat <<'EOF'
feat(playerbots): level-cap BG joins and relevel idle bots to fill

EOF
)"
```

---

### Task 6: End-to-end verification (manual)

**Files:** none (manual + configs from Task 2)

**Interfaces:**
- Consumes: Tasks 1–5, rebuilt `worldserver` / playerbots module when user asks to build

- [ ] **Step 1: Rebuild only when user asks**

```powershell
# from the existing out-of-source build directory used on this machine
cmake --build . --target worldserver -j
# install/copy binaries the way this env already deploys to env/dist
```

- [ ] **Step 2: Confirm runtime config**

`env/dist/etc/modules/playerbots.conf`:

```conf
AiPlayerbot.RandomBotJoinBG = 1
AiPlayerbot.RandomBotAutoJoinBG = 0
AiPlayerbot.RandomGearQualityLimit = 2
AiPlayerbot.BgJoinMaxLevelAbovePlayer = 1
```

`env/dist/etc/worldserver.conf`:

```conf
Battleground.Override.LowLevels.MinPlayers = 1
```

- [ ] **Step 3: In-game / log checklist (spec acceptance)**

1. ~10 level Alliance character queues Warsong Gulch → bots join; match starts; visible bot levels ≤ player+1; gear mostly green/white.
2. Repeat on another legal bracket (e.g. 30–39 or 70–79) → same constraints relative to that bracket's real player.
3. Solo Alliance queue → after start, Alliance count ≥ Horde (`2v1` / `2v2` OK, `1v2` not OK). Solo Horde is symmetric.
4. Set `RandomBotJoinBG = 0`, reload/restart → no auto fill.
5. Keep `RandomBotAutoJoinBG = 0` with nobody queuing → no spam of empty bracket BGs.
6. Log lines from `CheckBgQueue` show `MaxPlayerLevel` / `RealSide` for the active bracket; relevel lines appear when no in-window bot was available.

- [ ] **Step 4: Commit leftovers only if user asked**

```bash
git add docs/superpowers/specs/2026-08-09-playerbots-bg-fill-design.md \
  docs/superpowers/plans/2026-08-09-playerbots-bg-fill.md
git commit -m "$(cat <<'EOF'
docs: add playerbots BG fill spec and implementation plan

EOF
)"
```

---

## Spec coverage check

| Spec requirement | Task |
|------------------|------|
| Cover any legal level bracket (not only 10–19) | Task 3–5 (bracket-generic) |
| `botLevel <= maxPlayerLevel + N` | Task 1, 2, 5 |
| Prefer in-window bots; relevel when short | Task 5 |
| Green gear cap (`RandomGearQualityLimit = 2`) | Task 2; relevel `InitEquipment` in Task 5 |
| Faction: enemy ≤ our (single side); both sides equalize only | Task 1, 4 |
| `LowLevels.MinPlayers = 1` via conf, not SQL | Task 2 |
| Keep `RandomBotJoinBG=1`, `AutoJoinBG=0` | Task 2, 6 |
| No Arena / tactical AI / template SQL changes | Tasks 3–5 touch BG branch only |
| Acceptance checklist | Task 6 |

## Self-review notes

- No TBD placeholders.
- `realPlayerSide` / `maxPlayerLevel` names match across Tasks 3–5.
- `PlayerbotsBgRollTargetLevel` takes an injected `rolledInclusive` so unit tests stay deterministic; production passes `urand(windowLo, windowHi)`.
- Counting stays on the existing `InBattlegroundQueue()` population path (queued players/bots, including those already in a BG instance while still counted by that loop). No separate in-BG-only scan unless acceptance shows under-count.
- `EquipAndSpecPersistence=1` is handled by explicit `resetTalents` + `InitTalentsTree` + `InitEquipment(false, true)` after `Randomize(false)`.
- Optional “re-gear in-window bots on join” from the spec is **not** in scope unless open-world blues still crush in acceptance — then add a single `InitEquipment(false, true)` call for in-window joiners behind a short comment.
