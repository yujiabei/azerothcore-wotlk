-- Cliff hop (900001) and the hold aura (900002).
-- Server range is 3x collision height. RangeIndex 4 is only the client click limit.

DELETE FROM `spell_script_names` WHERE `spell_id` IN (900001, 900002);
DELETE FROM `spell_dbc` WHERE `ID` IN (900001, 900002);

INSERT INTO `spell_dbc` (
  `ID`, `Attributes`, `AttributesEx2`, `Targets`, `CastingTimeIndex`, `RangeIndex`, `EquippedItemClass`, `SchoolMask`,
  `Effect_1`, `ImplicitTargetA_1`, `SpellIconID`,
  `Name_Lang_enUS`, `Name_Lang_zhCN`, `Description_Lang_enUS`, `Description_Lang_zhCN`)
VALUES (
  900001, 16, 4, 64, 1, 4, -1, 1,
  3, 87, 4,
  'Cliff Hop', '峭壁短跳',
  'Jump to a point within three body lengths. Steep landings hold you in place.',
  '跳向三个身位以内的落点。落在陡坡上会固定住。');

INSERT INTO `spell_dbc` (
  `ID`, `Attributes`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`, `EquippedItemClass`, `SchoolMask`,
  `Effect_1`, `ImplicitTargetA_1`, `EffectAura_1`, `SpellIconID`,
  `Name_Lang_enUS`, `Name_Lang_zhCN`, `AuraDescription_Lang_enUS`, `AuraDescription_Lang_zhCN`)
VALUES (
  900002, 16, 1, 21, 1, -1, 1,
  6, 1, 4, 4,
  'Cliff Hold', '峭壁立足',
  'Right-click to let go.',
  '右键取消，从落点松开。');

INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(900001, 'spell_cliff_hop'),
(900002, 'spell_cliff_hold');
