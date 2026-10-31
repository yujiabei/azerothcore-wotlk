-- Bonus already honored for this class: floor(sum of other class levels / 3), after the cap.
-- A lower recalc keeps spent talents and leaves this value in place until the new budget covers them.

ALTER TABLE `character_class_progress`
  ADD COLUMN `talent_bonus` int unsigned NOT NULL DEFAULT '0' AFTER `mastered_at`;
