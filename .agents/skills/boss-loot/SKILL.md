---
name: boss-loot
description: >-
  Look up one creature's loot table and drop rates in this AzerothCore repo.
  Use when the user asks what a boss or NPC drops, a drop list, drop rate,
  掉落, 掉落列表, or 掉率.
---

# Boss loot

Run the script. Do not grep loot or item SQL, do not search for MySQL, and do not open the individual-progression item files.

## Resolve the creature

English name:

```powershell
powershell -NoProfile -File .agents/skills/boss-loot/scripts/boss-loot.ps1 -Name "Arcanist Doan"
```

Chinese or partial name: one Grep on `data/sql/base/db_world/creature_template_locale.sql` for `'zhCN','[^']*<name>`. Take `entry` from the matching line. If several names match, list them and stop. Then:

```powershell
powershell -NoProfile -File .agents/skills/boss-loot/scripts/boss-loot.ps1 -Entry <id>
```

`AMBIGUOUS` lists entries. Re-run with the entry the user meant. `NOT_FOUND` / `NO_LOOT` means stop.

Read `REPORT=` with the Read tool. Console text can garble Chinese; the report file is UTF-8.

`-All` includes white and grey junk. References of 20 or fewer direct items are expanded. Larger or nested references stay one `REF` line unless `-Expand` is passed.

A dungeon is one run per boss. Do not scan the map.

## Answer

Reply in 中文. Use the report numbers. Do not recompute them.

Quality words: grey 灰, white 白, green 绿, blue 蓝, purple 紫, orange 橙.

- Same `group` and the chances in that group sum to about 100: one kill drops exactly one of those rows.
- `group` 0: each row rolls on its own.
- `quest` 1: quest item.
- A `quality ... -> ... by individual-progression` note means the client shows the new quality. Say that in one clause. Do not recount patch history unless asked.
- `REF` lines are a reference table, not a single item. Say the chance to roll that table. Do not invent its contents.
- `OMITTED_JUNK` is cloth, food, and other white or grey items. Mention the count only.
- Chances are the template. `Rate.Drop.Item` is not applied. Pickpocket and skinning are not included.
