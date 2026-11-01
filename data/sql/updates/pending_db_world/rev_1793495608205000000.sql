-- Item enhancement gems. Entries must match ItemEnhance.Gem.Low/Mid/High.
-- Not bound, stack of 200. Right-click uses client spell 836 like the class mentor token.
-- The script opens the enhancement menu and does not cast it.
-- Flags 8388672 = ITEM_FLAG_PLAYERCAST | ITEM_FLAG_USE_WHEN_SHAPESHIFTED.
-- Displays: 35403 Large Brilliant Shard, 56459 Dream Shard, 56465 Abyss Crystal.
-- The client needs matching Item.dbc rows or it shows a question mark.
-- item_template is replaced in place. The SQL linter rejects DELETE on that table.

REPLACE INTO `item_template` (
  `entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`,
  `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `maxcount`,
  `stackable`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`,
  `spellcategorycooldown_1`, `bonding`, `description`, `Material`, `ScriptName`,
  `RequiredDisenchantSkill`, `area`, `Map`, `VerifiedBuild`)
VALUES
(550010, 15, 0, -1, 'Lesser Enhancement Stone', 35403, 2, 8388672, 0, -1, -1, 1, 0, 0, 200,
  836, 0, 0, 0, -1, 0, 'Right-click to enhance equipment. Used for +1 to +5.',
  -1, 'item_enhance_gem', -1, 0, 0, 12340),
(550011, 15, 0, -1, 'Greater Enhancement Stone', 56459, 3, 8388672, 0, -1, -1, 1, 0, 0, 200,
  836, 0, 0, 0, -1, 0, 'Right-click to enhance equipment. Used for +6 to +10.',
  -1, 'item_enhance_gem', -1, 0, 0, 12340),
(550012, 15, 0, -1, 'Superior Enhancement Stone', 56465, 4, 8388672, 0, -1, -1, 1, 0, 0, 200,
  836, 0, 0, 0, -1, 0, 'Right-click to enhance equipment. Used for +11 and above.',
  -1, 'item_enhance_gem', -1, 0, 0, 12340);

DELETE FROM `item_template_locale` WHERE `ID` IN (550010, 550011, 550012);
INSERT INTO `item_template_locale` (`ID`, `locale`, `Name`, `Description`, `VerifiedBuild`) VALUES
(550010, 'zhCN', '低阶强化石', '右键打开装备强化菜单。用于 +1 到 +5。', 12340),
(550011, 'zhCN', '中阶强化石', '右键打开装备强化菜单。用于 +6 到 +10。', 12340),
(550012, 'zhCN', '高阶强化石', '右键打开装备强化菜单。用于 +11 及以上。', 12340);

-- Enhancement master. There is no default spawn. Add one with: .npc add 550002
-- The entry must match ItemEnhance.NpcEntry. Display 3307 is Therum Deepforge (creature 5511).
REPLACE INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `type`, `HealthModifier`, `ScriptName`) VALUES
(550002, 'Enhancement Master', 'Equipment Enhancement', 80, 80, 2, 35, 1, 1, 1.14286, 2000, 2000, 1, 7, 1, 'npc_enhance_master');

DELETE FROM `creature_template_model` WHERE `CreatureID` = 550002;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(550002, 0, 3307, 1, 1);

DELETE FROM `creature_template_locale` WHERE `entry` = 550002;
INSERT INTO `creature_template_locale` (`entry`, `locale`, `Name`, `Title`) VALUES
(550002, 'zhCN', '强化师', '装备强化');

DELETE FROM `npc_text` WHERE `ID` = 800001;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(800001, 'Enhance worn equipment with enhancement stones. +1 to +3 never fail. Higher levels are harder, and a failure from +8 up loses one level, but never below +10, +15, +20 and so on.', 'Enhance worn equipment with enhancement stones. +1 to +3 never fail. Higher levels are harder, and a failure from +8 up loses one level, but never below +10, +15, +20 and so on.', 1);

DELETE FROM `npc_text_locale` WHERE `ID` = 800001;
INSERT INTO `npc_text_locale` (`ID`, `Locale`, `Text0_0`, `Text0_1`) VALUES
(800001, 'zhCN', '用强化石强化身上的装备。+1 到 +3 必定成功，越往上越难。+8 起失败会掉 1 级，但不会跌破 +10、+15、+20 这些锚点。', '用强化石强化身上的装备。+1 到 +3 必定成功，越往上越难。+8 起失败会掉 1 级，但不会跌破 +10、+15、+20 这些锚点。');
