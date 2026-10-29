-- Self morphs into Sylvanas, Jaina, and Tyrande. Right-click the buff to restore.

DELETE FROM `spell_dbc` WHERE `ID` IN (900003, 900004, 900005);
INSERT INTO `spell_dbc` (
  `ID`, `Attributes`, `CastingTimeIndex`, `DurationIndex`, `PowerType`, `RangeIndex`, `EquippedItemClass`, `SchoolMask`,
  `Effect_1`, `ImplicitTargetA_1`, `EffectAura_1`, `EffectMiscValue_1`, `SpellVisualID_1`, `SpellIconID`,
  `Name_Lang_enUS`, `Name_Lang_zhCN`, `Description_Lang_enUS`, `Description_Lang_zhCN`,
  `AuraDescription_Lang_enUS`, `AuraDescription_Lang_zhCN`)
VALUES
(
  900003, 16, 1, 21, -1, 1, -1, 1,
  6, 1, 56, 10181, 7099, 1939,
  'Sylvanas Form', '变身：希尔瓦娜斯',
  'Take the form of Sylvanas Windrunner. Right-click the buff to restore your form.',
  '化身为希尔瓦娜斯·风行者。右键点击增益可以恢复原形。',
  'Right-click to restore your form.',
  '右键点击可以恢复原形。'),
(
  900004, 16, 1, 21, -1, 1, -1, 1,
  6, 1, 56, 4968, 7099, 188,
  'Jaina Form', '变身：吉安娜',
  'Take the form of Jaina Proudmoore. Right-click the buff to restore your form.',
  '化身为吉安娜·普罗德摩尔。右键点击增益可以恢复原形。',
  'Right-click to restore your form.',
  '右键点击可以恢复原形。'),
(
  900005, 16, 1, 21, -1, 1, -1, 1,
  6, 1, 56, 7999, 7099, 225,
  'Tyrande Form', '变身：泰兰德',
  'Take the form of Tyrande Whisperwind. Right-click the buff to restore your form.',
  '化身为泰兰德·语风。右键点击增益可以恢复原形。',
  'Right-click to restore your form.',
  '右键点击可以恢复原形。');

DELETE FROM `spell_group` WHERE `id` = 1900;
INSERT INTO `spell_group` (`id`, `spell_id`) VALUES
(1900, 900003),
(1900, 900004),
(1900, 900005);

DELETE FROM `spell_group_stack_rules` WHERE `group_id` = 1900;
INSERT INTO `spell_group_stack_rules` (`group_id`, `stack_rule`, `description`) VALUES
(1900, 1, 'Leader morphs');
