-- Class mentor. There is no default spawn.
-- Add one with: .npc add 550000
-- The entry must match Multiclass.MentorNpcEntry (default 550000).
-- creature_template is replaced in place. The SQL linter rejects DELETE on that table.
-- Display 3167 is the Stormwind human male already used by creature 1756.

REPLACE INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `type`, `HealthModifier`, `ScriptName`) VALUES
(550000, 'Class Mentor', 'Multi-class', 80, 80, 2, 35, 1, 1, 1.14286, 2000, 2000, 1, 7, 1, 'npc_class_mentor');

DELETE FROM `creature_template_model` WHERE `CreatureID` = 550000;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(550000, 0, 3167, 1, 1);

DELETE FROM `creature_template_locale` WHERE `entry` = 550000;
INSERT INTO `creature_template_locale` (`entry`, `locale`, `Name`, `Title`) VALUES
(550000, 'zhCN', '职业导师', '多职业');

DELETE FROM `npc_text` WHERE `ID` = 800000;
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `Probability0`) VALUES
(800000, 'Choose a class, or pick spells kept from another class.', 'Choose a class, or pick spells kept from another class.', 1);

DELETE FROM `npc_text_locale` WHERE `ID` = 800000;
INSERT INTO `npc_text_locale` (`ID`, `Locale`, `Text0_0`, `Text0_1`) VALUES
(800000, 'zhCN', '选择一个职业，或挑选从其他职业留下的技能。', '选择一个职业，或挑选从其他职业留下的技能。');

DELETE FROM `acore_string` WHERE `entry` BETWEEN 35500 AND 35538;
INSERT INTO `acore_string` (`entry`, `content_default`, `locale_zhCN`) VALUES
(35500, '{}: level {}, {} XP', '{}：{} 级，经验 {}'),
(35501, '{}: not started, starts at level {}', '{}：未开档，从 {} 级开始'),
(35502, ' (current)', '（当前）'),
(35503, 'Cost: {} copper.', '费用：{} 铜币。'),
(35504, 'Cost: free.', '费用：免费。'),
(35505, 'Cooldown: ready.', '冷却：可以切换。'),
(35506, 'Cooldown: {} seconds.', '冷却：还需 {} 秒。'),
(35507, 'A class change is already waiting for logout.', '已有一次切换在等待登出。'),
(35508, 'Retained spells', '保留技能'),
(35509, 'Back', '返回'),
(35510, 'Confirm switch to {}', '确认切换为{}'),
(35511, 'Synergy passives', '连携被动'),
(35512, 'Synergy is locked until two classes reach level {}.', '至少两个职业达到 {} 级才解锁连携。'),
(35513, 'Next page', '下一页'),
(35514, 'Previous page', '上一页'),
(35515, 'Spells ({}/{})', '法术（{}/{}）'),
(35516, 'Talents ({}/{})', '天赋（{}/{}）'),
(35517, 'Keep: {}', '保留：{}'),
(35518, 'Remove: {}', '移除：{}'),
(35519, 'Bots cannot use the class mentor.', '机器人不能使用职业导师。'),
(35520, 'Class change cancelled', '职业切换已取消'),
(35521, 'The pending class change was cancelled because this character was not in a capital city.', '角色不在主城，等待中的职业切换已取消。'),
(35522, 'Class change to {} is queued. Logging out to apply it.', '切换为{}已排队，即将登出并生效。'),
(35523, 'Class change is disabled.', '职业切换未启用。'),
(35524, 'That character is already this class.', '角色已经是这个职业。'),
(35525, 'This race cannot be that class.', '这个种族不能成为该职业。'),
(35526, 'Death Knight class changes are disabled.', '死亡骑士职业切换未启用。'),
(35527, 'Bots cannot change class.', '机器人不能切换职业。'),
(35528, 'A class change is already pending. Log out to finish it.', '已有一次职业切换在等待，请登出完成。'),
(35529, 'You must wait {} seconds before changing class again.', '还需等待 {} 秒才能再次切换职业。'),
(35530, 'You cannot change class while in combat.', '战斗中不能切换职业。'),
(35531, 'You cannot change class in a battleground or arena.', '战场或竞技场中不能切换职业。'),
(35532, 'Class change is only allowed in a capital city.', '只能在主城切换职业。'),
(35533, 'You do not have enough money to change class.', '没有足够的钱切换职业。'),
(35534, 'That class cannot be selected.', '不能切换到这个职业。'),
(35535, '{}: this race cannot be that class.', '{}：这个种族不能成为该职业。'),
(35536, 'The class mentor is disabled.', '职业导师未启用。'),
(35537, 'Spell slots {}/{}. Talent slots {}/{}.', '法术位 {}/{}。天赋位 {}/{}。'),
(35538, 'No saved spells on this page.', '这一页没有可选技能。');
