-- Remember the equipped item guids for each class. Rows stay until the next time
-- that class is switched out; a switch-in does not delete them.

CREATE TABLE IF NOT EXISTS `character_class_equipment` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `item` int unsigned NOT NULL,
  PRIMARY KEY (`guid`, `class`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
