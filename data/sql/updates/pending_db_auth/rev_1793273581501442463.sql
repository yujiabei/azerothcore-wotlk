-- Player command for listing mastered classes. Linked from the player-command group (197).
-- Permission id 1005 follows the retained-spell commands (1001-1004).

DELETE FROM `rbac_linked_permissions` WHERE `id` = 197 AND `linkedId` = 1005;
DELETE FROM `rbac_permissions` WHERE `id` = 1005;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1005, 'Command: multiclass mastery');
DELETE FROM `rbac_linked_permissions` WHERE `id` = 197 AND `linkedId` = 1005;
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 1005);
