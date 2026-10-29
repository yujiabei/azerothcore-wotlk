-- Character-wide time of the last daily off-class XP bonus, unix time.
-- Copied onto the active class row so a snapshot replace does not clear it.
-- 0 means it has never been granted. Compared with the daily quest reset window.

ALTER TABLE `character_class_progress`
  ADD COLUMN `last_daily_bonus_at` int unsigned NOT NULL DEFAULT '0' AFTER `current_pet_id`;
