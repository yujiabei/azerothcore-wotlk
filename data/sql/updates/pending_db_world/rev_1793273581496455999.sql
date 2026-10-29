-- Lists classes that have reached the server level cap. Security 0: access is granted by RBAC 1001.

DELETE FROM `command` WHERE `name` = 'multiclass mastery';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('multiclass mastery', 0, 'Syntax: .multiclass mastery [player]\nList classes that have reached the server level cap.');
