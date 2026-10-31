-- Class mentor token. Entry 550001 must match Multiclass.MentorItemEntry.
-- Soulbound (bonding 1) and AllowableClass -1, so a class change leaves it in the bag.
-- Map and area are 0, so it is not destroyed when entering an instance.
-- Right-click uses client spell 836 (LOGINEFFECT): self, instant, no cooldown, no
-- level or class requirement, usable while mounted or sitting. It is not limited
-- to the outdoors or to raids. The script opens the mentor menu and does not cast
-- it. Casting it would run the Spawn effect. Combat is refused in the script.
-- Give one with: .additem 550001
-- Flags 8388672 = ITEM_FLAG_PLAYERCAST | ITEM_FLAG_USE_WHEN_SHAPESHIFTED.
-- item_template is replaced in place. The SQL linter rejects DELETE on that table.

REPLACE INTO `item_template` (
  `entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`, `Flags`,
  `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `maxcount`,
  `stackable`, `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`,
  `spellcategorycooldown_1`, `bonding`, `description`, `Material`, `ScriptName`,
  `RequiredDisenchantSkill`, `area`, `Map`, `VerifiedBuild`)
VALUES (
  550001, 15, 0, -1, 'Class Mentor Token', 54562, 3, 8388672, 0, -1, -1, 1, 0, 1, 1,
  836, 0, 0, 0, -1, 1,
  'Right-click to open the class mentor menu. Usable in instances. Not usable in combat.',
  -1, 'item_class_mentor', -1, 0, 0, 12340);

DELETE FROM `item_template_locale` WHERE `ID` = 550001;
INSERT INTO `item_template_locale` (`ID`, `locale`, `Name`, `Description`, `VerifiedBuild`) VALUES
(550001, 'zhCN', '职业导师徽记', '右键打开职业导师菜单。副本内可用。战斗中不可用。', 12340);
