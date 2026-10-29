-- Unix time when this class first reached the server max level through GiveLevel.
-- 0 means not mastered. The progress upsert does not write this column, so a switch keeps it.

ALTER TABLE `character_class_progress`
  ADD COLUMN `mastered_at` int unsigned NOT NULL DEFAULT '0' AFTER `last_daily_bonus_at`;
