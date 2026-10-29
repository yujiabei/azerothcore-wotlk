-- Per-class snapshots for logout class switch.
-- character_class_switch is kept after the swap: target_class 0 means nothing is pending,
-- and last_switch_at is the cooldown anchor.

CREATE TABLE IF NOT EXISTS `character_class_progress` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `level` tinyint unsigned NOT NULL DEFAULT '1',
  `xp` int unsigned NOT NULL DEFAULT '0',
  `rest_bonus` float NOT NULL DEFAULT '0',
  `active_spec` tinyint unsigned NOT NULL DEFAULT '0',
  `level_play_time` int unsigned NOT NULL DEFAULT '0',
  `current_pet_id` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_class_spell` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `spell` int unsigned NOT NULL,
  `specMask` tinyint unsigned NOT NULL DEFAULT '1',
  PRIMARY KEY (`guid`, `class`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_class_talent` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `spell` int unsigned NOT NULL,
  `specMask` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `class`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_class_glyph` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `talentGroup` tinyint unsigned NOT NULL DEFAULT '0',
  `glyph1` smallint unsigned NOT NULL DEFAULT '0',
  `glyph2` smallint unsigned NOT NULL DEFAULT '0',
  `glyph3` smallint unsigned NOT NULL DEFAULT '0',
  `glyph4` smallint unsigned NOT NULL DEFAULT '0',
  `glyph5` smallint unsigned NOT NULL DEFAULT '0',
  `glyph6` smallint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `class`, `talentGroup`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_class_action` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `spec` tinyint unsigned NOT NULL DEFAULT '0',
  `button` tinyint unsigned NOT NULL DEFAULT '0',
  `action` int unsigned NOT NULL DEFAULT '0',
  `type` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `class`, `spec`, `button`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_class_skill` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `skill` smallint unsigned NOT NULL,
  `value` smallint unsigned NOT NULL,
  `max` smallint unsigned NOT NULL,
  PRIMARY KEY (`guid`, `class`, `skill`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `character_class_switch` (
  `guid` int unsigned NOT NULL,
  `target_class` tinyint unsigned NOT NULL DEFAULT '0',
  `requested_at` int unsigned NOT NULL DEFAULT '0',
  `last_switch_at` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- timer is remaining milliseconds, not a unix timestamp.
CREATE TABLE IF NOT EXISTS `character_class_quest` (
  `guid` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  `quest` int unsigned NOT NULL,
  `status` tinyint unsigned NOT NULL DEFAULT '0',
  `explored` tinyint unsigned NOT NULL DEFAULT '0',
  `timer` int unsigned NOT NULL DEFAULT '0',
  `mobcount1` smallint unsigned NOT NULL DEFAULT '0',
  `mobcount2` smallint unsigned NOT NULL DEFAULT '0',
  `mobcount3` smallint unsigned NOT NULL DEFAULT '0',
  `mobcount4` smallint unsigned NOT NULL DEFAULT '0',
  `itemcount1` smallint unsigned NOT NULL DEFAULT '0',
  `itemcount2` smallint unsigned NOT NULL DEFAULT '0',
  `itemcount3` smallint unsigned NOT NULL DEFAULT '0',
  `itemcount4` smallint unsigned NOT NULL DEFAULT '0',
  `itemcount5` smallint unsigned NOT NULL DEFAULT '0',
  `itemcount6` smallint unsigned NOT NULL DEFAULT '0',
  `playercount` smallint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `class`, `quest`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

ALTER TABLE `character_pet`
  ADD COLUMN `class` tinyint unsigned NOT NULL DEFAULT '0' AFTER `owner`;
