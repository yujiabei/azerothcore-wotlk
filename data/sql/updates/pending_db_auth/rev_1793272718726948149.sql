-- Retained-spell commands. Linked from the player-command group (197).

DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 1001 AND 1004;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(1001, 'Command: multiclass retain list'),
(1002, 'Command: multiclass retain add'),
(1003, 'Command: multiclass retain remove'),
(1004, 'Command: multiclass retain clear');
DELETE FROM `rbac_linked_permissions` WHERE `id` = 197 AND `linkedId` BETWEEN 1001 AND 1004;
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(197, 1001),
(197, 1002),
(197, 1003),
(197, 1004);
