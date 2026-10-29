-- Unix time when this class was mastered. 0 means not mastered.
-- Login fills a 0 when the live level or this row's level is already at the server cap.
-- The progress upsert does not write this column, so a switch keeps it.

ALTER TABLE `character_class_progress`
  ADD COLUMN `mastered_at` int unsigned NOT NULL DEFAULT '0' AFTER `last_daily_bonus_at`;
