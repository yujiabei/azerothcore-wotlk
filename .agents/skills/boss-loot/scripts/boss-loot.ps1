# Prints one creature's loot from the SQL dumps, using LootMgr group rolls.
# Chances are template values. Rate.Drop.Item.* is not applied.
param(
    [int]$Entry = 0,
    [string]$Name = "",
    [switch]$All,
    [switch]$Expand
)

$ErrorActionPreference = "Stop"
$utf8 = New-Object System.Text.UTF8Encoding $false
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$db = Join-Path $repo "data\sql\base\db_world"
$ipp = Join-Path $repo "modules\mod-individual-progression\data\sql\world\base"

function Split-SqlTuple([string]$line) {
    $s = $line.Trim()
    if ($s.StartsWith("(")) { $s = $s.Substring(1) }
    if ($s.EndsWith(",")) { $s = $s.Substring(0, $s.Length - 1) }
    if ($s.EndsWith(")")) { $s = $s.Substring(0, $s.Length - 1) }
    $fields = New-Object System.Collections.Generic.List[string]
    $cur = New-Object System.Text.StringBuilder
    $inQ = $false
    for ($i = 0; $i -lt $s.Length; $i++) {
        $c = $s[$i]
        if ($inQ) {
            if ($c -eq "'" -and ($i + 1) -lt $s.Length -and $s[$i + 1] -eq "'") {
                [void]$cur.Append("'")
                $i++
                continue
            }
            if ($c -eq "\" -and ($i + 1) -lt $s.Length -and $s[$i + 1] -eq "'") {
                [void]$cur.Append("'")
                $i++
                continue
            }
            if ($c -eq "'") { $inQ = $false; continue }
            [void]$cur.Append($c)
            continue
        }
        if ($c -eq "'") { $inQ = $true; continue }
        if ($c -eq ",") {
            $fields.Add($cur.ToString().Trim())
            [void]$cur.Clear()
            continue
        }
        [void]$cur.Append($c)
    }
    $fields.Add($cur.ToString().Trim())
    return $fields
}

function Invoke-RgLines([string]$pattern, [string]$path) {
    if (-not (Test-Path $path)) { return @() }
    $out = & rg -N --no-heading -e $pattern -- $path 2>$null
    if (-not $out) { return @() }
    return @($out)
}

function Get-BossRow([int]$id) {
    $lines = Invoke-RgLines ("^\({0}," -f $id) (Join-Path $db "creature_template.sql")
    foreach ($line in $lines) {
        $f = Split-SqlTuple $line
        if ([int]$f[0] -eq $id) { return $f }
    }
    return $null
}

function Get-ZhName([int]$id, [string]$file) {
    $lines = Invoke-RgLines ("^\({0},'zhCN'," -f $id) $file
    foreach ($line in $lines) {
        $f = Split-SqlTuple $line
        if ([int]$f[0] -eq $id -and $f[1] -eq "zhCN") { return $f[2] }
    }
    return ""
}

if ($Entry -le 0 -and $Name) {
    $hits = New-Object System.Collections.Generic.List[object]
    $nameLines = & rg -N --no-heading -F -e $Name -- (Join-Path $db "creature_template.sql") 2>$null
    foreach ($line in @($nameLines)) {
        if (-not $line -or $line[0] -ne "(") { continue }
        $f = Split-SqlTuple $line
        if ($f.Count -lt 7) { continue }
        if ($f[6].IndexOf($Name, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $hits.Add([pscustomobject]@{ Entry = [int]$f[0]; Name = $f[6] })
        }
    }
    $exact = @($hits | Where-Object { $_.Name -eq $Name })
    if ($exact.Count -ge 1) {
        $hits = New-Object System.Collections.Generic.List[object]
        foreach ($h in $exact) { $hits.Add($h) }
    }
    if ($hits.Count -gt 1) {
        $withLoot = New-Object System.Collections.Generic.List[object]
        foreach ($h in $hits) {
            $row = Get-BossRow $h.Entry
            if ($row -and [int]$row[34] -ne 0) { $withLoot.Add($h) }
        }
        if ($withLoot.Count -ge 1) { $hits = $withLoot }
    }
    if ($hits.Count -eq 1) { $Entry = $hits[0].Entry }
    elseif ($hits.Count -gt 1) {
        Write-Output "AMBIGUOUS"
        $hits | Select-Object -First 20 | ForEach-Object { Write-Output ("{0}`t{1}" -f $_.Entry, $_.Name) }
        exit 2
    }
}

if ($Entry -le 0) {
    Write-Output "NOT_FOUND"
    exit 3
}

$boss = Get-BossRow $Entry
if (-not $boss) {
    Write-Output "NOT_FOUND entry=$Entry"
    exit 3
}

$lootId = [int]$boss[34]
$minLv = $boss[10]
$maxLv = $boss[11]
$enName = $boss[6]
$zhName = Get-ZhName $Entry (Join-Path $db "creature_template_locale.sql")

$lootLines = Invoke-RgLines ("^\({0}," -f $lootId) (Join-Path $db "creature_loot_template.sql")
$rows = New-Object System.Collections.Generic.List[object]
foreach ($line in $lootLines) {
    $f = Split-SqlTuple $line
    if ([int]$f[0] -ne $lootId) { continue }
    $rows.Add([pscustomobject]@{
        Idx = $rows.Count
        Item = [int]$f[1]
        Reference = [int]$f[2]
        Chance = [double]$f[3]
        Quest = [int]$f[4]
        LootMode = [int]$f[5]
        GroupId = [int]$f[6]
        Min = [int]$f[7]
        Max = [int]$f[8]
        Comment = $f[9]
        Eff = 0.0
    })
}

if ($rows.Count -eq 0 -or $lootId -eq 0) {
    Write-Output "NO_LOOT entry=$Entry lootid=$lootId name=$enName zh=$zhName"
    exit 4
}

# Same order as LootGroup::Roll: explicit chances consume a 0-100 roll, then one equal-chance item.
$groups = $rows | Group-Object GroupId
foreach ($g in $groups) {
    $members = @($g.Group)
    if ([int]$g.Name -eq 0) {
        foreach ($m in $members) { $m.Eff = $m.Chance }
        continue
    }
    $explicit = @($members | Where-Object { $_.Chance -ne 0 })
    $equal = @($members | Where-Object { $_.Chance -eq 0 })
    $used = 0.0
    foreach ($m in $explicit) {
        $room = 100.0 - $used
        if ($room -lt 0) { $room = 0 }
        $take = $m.Chance
        if ($take -gt $room) { $take = $room }
        $m.Eff = $take
        $used += $take
    }
    $left = 100.0 - $used
    if ($left -lt 0) { $left = 0 }
    if ($equal.Count -gt 0) {
        $share = $left / $equal.Count
        foreach ($m in $equal) { $m.Eff = $share }
    }
}

$itemIds = New-Object "System.Collections.Generic.HashSet[int]"
$refIds = New-Object "System.Collections.Generic.HashSet[int]"
foreach ($m in $rows) {
    if ($m.Reference -ne 0) { [void]$refIds.Add([Math]::Abs($m.Reference)) }
    elseif ($m.Item -gt 0) { [void]$itemIds.Add($m.Item) }
}

$refRows = @{}
if ($refIds.Count -gt 0) {
    $refAlt = ($refIds | ForEach-Object { '^\(' + $_ + ',' }) -join "|"
    foreach ($line in (Invoke-RgLines $refAlt (Join-Path $db "reference_loot_template.sql"))) {
        $f = Split-SqlTuple $line
        $id = [int]$f[0]
        if (-not $refIds.Contains($id)) { continue }
        if (-not $refRows.ContainsKey($id)) {
            $refRows[$id] = New-Object System.Collections.Generic.List[object]
        }
        $refRows[$id].Add([pscustomobject]@{
            Item = [int]$f[1]
            Reference = [int]$f[2]
            Chance = [double]$f[3]
            Quest = [int]$f[4]
            GroupId = [int]$f[6]
            Min = [int]$f[7]
            Max = [int]$f[8]
            Comment = $f[9]
        })
        if ([int]$f[2] -eq 0 -and [int]$f[1] -gt 0) { [void]$itemIds.Add([int]$f[1]) }
    }
}

$items = @{}
if ($itemIds.Count -gt 0) {
    $itemAlt = ($itemIds | ForEach-Object { '^\(' + $_ + ',' }) -join "|"
    foreach ($line in (Invoke-RgLines $itemAlt (Join-Path $db "item_template.sql"))) {
        $f = Split-SqlTuple $line
        $id = [int]$f[0]
        if (-not $itemIds.Contains($id)) { continue }
        $items[$id] = [pscustomobject]@{
            Name = $f[4]
            Quality = [int]$f[6]
            BaseQuality = [int]$f[6]
            Zh = ""
        }
    }
    $localeAlt = ($itemIds | ForEach-Object { '^\(' + $_ + ",'zhCN'," }) -join "|"
    foreach ($line in (Invoke-RgLines $localeAlt (Join-Path $db "item_template_locale.sql"))) {
        $f = Split-SqlTuple $line
        $id = [int]$f[0]
        if ($f[1] -eq "zhCN" -and $items.ContainsKey($id)) { $items[$id].Zh = $f[2] }
    }

    $idAlt = ($itemIds | ForEach-Object { "\b$_\b" }) -join "|"
    $qualityFiles = @(
        (Join-Path $ipp "tbc_item_changes.sql"),
        (Join-Path $ipp "vanilla_item_changes.sql")
    )
    foreach ($qf in $qualityFiles) {
        foreach ($line in (Invoke-RgLines $idAlt $qf)) {
            if ($line.IndexOf("Quality") -lt 0) { continue }
            $qm = [regex]::Match($line, "Quality``?\s*=\s*(\d+)")
            if (-not $qm.Success) { continue }
            $q = [int]$qm.Groups[1].Value
            $ids = New-Object System.Collections.Generic.List[int]
            $em = [regex]::Match($line, "entry``?\s*=\s*(\d+)")
            if ($em.Success) { $ids.Add([int]$em.Groups[1].Value) }
            $im = [regex]::Match($line, "entry\s+IN\s*\(([^)]*)\)")
            if ($im.Success) {
                foreach ($part in $im.Groups[1].Value.Split(",")) {
                    $n = 0
                    if ([int]::TryParse($part.Trim(), [ref]$n)) { $ids.Add($n) }
                }
            }
            foreach ($id in $ids) {
                if ($items.ContainsKey($id)) { $items[$id].Quality = $q }
            }
        }
    }
}

$qZh = @{
    0 = "grey"
    1 = "white"
    2 = "green"
    3 = "blue"
    4 = "purple"
    5 = "orange"
    6 = "artifact"
    7 = "heirloom"
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("BOSS`t$Entry`t$lootId`t$minLv`t$maxLv`t$enName`t$zhName")
[void]$sb.AppendLine("CHANCE_MODEL`texplicit roll consumes 0-100 in file order; leftover is split across Chance=0 in that group")
[void]$sb.AppendLine("COLUMNS`tsection`tgroup`teffective_pct`tquality`tbase_quality`titem`tzh`ten`tmin`tmax`tquest`tnote")

$omitted = 0
function Emit-Item($section, $group, $pct, $itemId, $min, $max, $quest, $note) {
    $it = $items[$itemId]
    $quality = -1
    $baseQ = -1
    $zh = ""
    $en = ""
    if ($it) {
        $quality = $it.Quality
        $baseQ = $it.BaseQuality
        $zh = $it.Zh
        $en = $it.Name
    }
    if (-not $All -and $quest -eq 0 -and $quality -ge 0 -and $quality -le 1) {
        $script:omitted++
        return
    }
    $qLabel = "$quality"
    if ($qZh.ContainsKey($quality)) { $qLabel = $qZh[$quality] }
    $qNote = $note
    if ($baseQ -ge 0 -and $baseQ -ne $quality) {
        $from = $qZh[$baseQ]
        if (-not $from) { $from = "$baseQ" }
        if ($qNote) { $qNote = "$qNote; " }
        $qNote = "${qNote}quality $from -> $qLabel by individual-progression item_changes"
    }
    [void]$sb.AppendLine(("ITEM`t{0}`t{1}`t{2:0.####}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8}`t{9}`t{10}`t{11}" -f `
        $section, $group, $pct, $qLabel, $baseQ, $itemId, $zh, $en, $min, $max, $quest, $qNote))
}

foreach ($m in $rows) {
    $pct = $m.Eff
    if ($m.Reference -ne 0) {
        $rid = [Math]::Abs($m.Reference)
        $kids = @()
        if ($refRows.ContainsKey($rid)) { $kids = @($refRows[$rid]) }
        $nested = @($kids | Where-Object { $_.Reference -ne 0 }).Count -gt 0
        $directKids = @($kids | Where-Object { $_.Reference -eq 0 })
        $doExpand = $Expand -or ((-not $nested) -and $directKids.Count -gt 0 -and $directKids.Count -le 20)
        if (-not $doExpand) {
            [void]$sb.AppendLine(("REF`tref`t{0}`t{1:0.####}`t`t`t{2}`t`t{3}`t{4}`t{5}`t0`tnested_or_large count={6}" -f `
                $m.GroupId, $pct, $rid, $m.Comment, $m.Min, $m.Max, $kids.Count))
            continue
        }
        $childEff = @{}
        $cg = $directKids | Group-Object GroupId
        foreach ($g in $cg) {
            $members = @($g.Group)
            if ([int]$g.Name -eq 0) {
                foreach ($c in $members) { $childEff[$c] = $c.Chance }
                continue
            }
            $explicit = @($members | Where-Object { $_.Chance -ne 0 })
            $equal = @($members | Where-Object { $_.Chance -eq 0 })
            $used = 0.0
            foreach ($c in $explicit) {
                $room = 100.0 - $used
                if ($room -lt 0) { $room = 0 }
                $take = $c.Chance
                if ($take -gt $room) { $take = $room }
                $childEff[$c] = $take
                $used += $take
            }
            $left = 100.0 - $used
            if ($left -lt 0) { $left = 0 }
            if ($equal.Count -gt 0) {
                $share = $left / $equal.Count
                foreach ($c in $equal) { $childEff[$c] = $share }
            }
        }
        foreach ($c in $directKids) {
            $childPct = ($pct / 100.0) * $childEff[$c]
            Emit-Item "ref-$rid" $m.GroupId $childPct $c.Item $c.Min $c.Max $c.Quest $m.Comment
        }
        continue
    }
    Emit-Item "direct" $m.GroupId $pct $m.Item $m.Min $m.Max $m.Quest ""
}

[void]$sb.AppendLine("OMITTED_JUNK`t$omitted")
[void]$sb.AppendLine("NOTE`ttemplate chances only; Rate.Drop.Item.* is not applied. Pickpocket and skinning are not included.")

$text = $sb.ToString()
$report = Join-Path $env:TEMP ("boss-loot-{0}.txt" -f $Entry)
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($report, $text, $utf8)
Write-Output "REPORT=$report"
Write-Output $text
exit 0
