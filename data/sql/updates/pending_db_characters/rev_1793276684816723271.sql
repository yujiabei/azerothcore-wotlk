-- Stable slots parked for another class.
-- character_pet.slot stays 1-4 until the switch transaction moves that class, so a hunter
-- save cannot delete another class's pets. Slot is the stable index (1-4).

CREATE TABLE IF NOT EXISTS `character_class_pet` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `id` int unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  PRIMARY KEY (`guid`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
