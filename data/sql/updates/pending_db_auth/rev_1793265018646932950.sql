-- Player command for logout class switch. Linked from the player-command group (197),
-- same parent as character changerace. Permission id 1000 is the custom range.

DELETE FROM `rbac_linked_permissions` WHERE `id` = 197 AND `linkedId` = 1000;
DELETE FROM `rbac_permissions` WHERE `id` = 1000;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1000, 'Command: character changeclass');
DELETE FROM `rbac_linked_permissions` WHERE `id` = 197 AND `linkedId` = 1000;
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 1000);
