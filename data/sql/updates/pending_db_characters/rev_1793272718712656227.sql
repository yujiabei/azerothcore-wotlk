-- Cross-class retained spells. Rows stay across a class switch.
-- kind: 0 spell, 1 talent, 2 synergy. Slot is per class for 0 and 1, and shared for 2.
-- character_retained_change keeps the edit cooldown after a clear deletes every pick.

CREATE TABLE IF NOT EXISTS `character_retained_spell` (
  `guid` int unsigned NOT NULL,
  `source_class` tinyint unsigned NOT NULL,
  `spell` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL COMMENT '0 spell, 1 talent, 2 synergy',
  `slot` tinyint unsigned NOT NULL,
  PRIMARY KEY (`guid`, `source_class`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_retained_change` (
  `guid` int unsigned NOT NULL,
  `changed_at` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
