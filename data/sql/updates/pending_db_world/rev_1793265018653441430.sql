-- Logout class switch. Security 0: access is granted by RBAC 1000.

DELETE FROM `command` WHERE `name` = 'character changeclass';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('character changeclass', 0, 'Syntax: .character changeclass <classId|name> [player]\nQueue a class change that is applied at logout. Death Knights are rejected.');
