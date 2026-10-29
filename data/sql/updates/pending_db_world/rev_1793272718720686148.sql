-- Cross-class retained spells. Security 0: access is granted by RBAC 1001-1004.

DELETE FROM `command` WHERE `name` LIKE 'multiclass%';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('multiclass', 0, 'Syntax: .multiclass retain ...'),
('multiclass retain', 0, 'Syntax: .multiclass retain list|add|remove|clear'),
('multiclass retain list', 0, 'Syntax: .multiclass retain list'),
('multiclass retain add', 0, 'Syntax: .multiclass retain add <spellId> [slot|synergy] [slot]'),
('multiclass retain remove', 0, 'Syntax: .multiclass retain remove <spellId>'),
('multiclass retain clear', 0, 'Syntax: .multiclass retain clear [class]');
