#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Era,
    [switch]$WriteDb,
    [switch]$Force,
    [string]$RepoRoot,
    [string]$MysqlContainer = 'ac-database',
    [string]$MysqlUser = 'root',
    [string]$MysqlPassword
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

if (-not $MysqlPassword) {
    $envFile = Join-Path $RepoRoot '.env'
    if (Test-Path -LiteralPath $envFile) {
        foreach ($line in Get-Content -LiteralPath $envFile) {
            if ($line -match '^\s*DOCKER_DB_ROOT_PASSWORD=(.+)$') {
                $MysqlPassword = $Matches[1].Trim()
            }
        }
    }
    if (-not $MysqlPassword) {
        $MysqlPassword = 'acore'
    }
}

function Get-EraSettings {
    param([Parameter(Mandatory = $true)][string]$Name)

    $table = @{
        vanilla = [ordered]@{
            RandomBotMaxLevel          = 60
            RandomBotMaps              = '0,1'
            UseFlyMountAtMinLevel      = 80
            DisableItemsAboveReqLevel  = 60
            DisableTGsAboveReqLevel    = 60
            DisableItemsAboveLevel     = 92
            DisableTGsAboveLevel       = 92
            DisableDKItems             = 1
            DisableDeathKnightLogin    = 1
            DisableRDF                 = 1
            RandomBotJoinLfg           = 0
            RaceMask                   = 1536
            ClassMask                  = 32
            BotAccountsMaxLevel        = 60
            ProgressionTier            = 0
            DisabledItemMinExclusive   = 23000
        }
        tbc = [ordered]@{
            RandomBotMaxLevel          = 70
            RandomBotMaps              = '0,1,530'
            UseFlyMountAtMinLevel      = 70
            DisableItemsAboveReqLevel  = 70
            DisableTGsAboveReqLevel    = 70
            DisableItemsAboveLevel     = 164
            DisableTGsAboveLevel       = 164
            DisableDKItems             = 1
            DisableDeathKnightLogin    = 1
            DisableRDF                 = 1
            RandomBotJoinLfg           = 0
            RaceMask                   = 0
            ClassMask                  = 32
            BotAccountsMaxLevel        = 70
            ProgressionTier            = 8
            DisabledItemMinExclusive   = 35000
        }
        wotlk = [ordered]@{
            RandomBotMaxLevel          = 80
            RandomBotMaps              = '0,1,530,571'
            UseFlyMountAtMinLevel      = 60
            DisableItemsAboveReqLevel  = 0
            DisableTGsAboveReqLevel    = 0
            DisableItemsAboveLevel     = 0
            DisableTGsAboveLevel       = 0
            DisableDKItems             = 0
            DisableDeathKnightLogin    = 0
            DisableRDF                 = 0
            RandomBotJoinLfg           = 1
            RaceMask                   = 0
            ClassMask                  = 0
            BotAccountsMaxLevel        = 80
            ProgressionTier            = 13
            DisabledItemMinExclusive   = 0
        }
    }

    $key = $Name.ToLowerInvariant()
    if (-not $table.ContainsKey($key)) {
        throw "unknown era '$Name' (expected vanilla|tbc|wotlk)"
    }
    return $table[$key]
}

function Get-ProgressionQuestIds {
    param([Parameter(Mandatory = $true)][int]$Tier)
    $ids = New-Object 'System.Collections.Generic.List[int]'
    if ($Tier -gt 0) {
        foreach ($id in 66001..(66000 + $Tier)) {
            $ids.Add($id)
        }
    }
    return , $ids
}

function Set-ConfValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)]$Value
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "conf file missing: $Path"
    }

    $raw = [IO.File]::ReadAllText($Path)
    $newline = "`n"
    if ($raw.Contains("`r`n")) {
        $newline = "`r`n"
    }
    $lines = $raw -split "`r?`n", -1
    if ($lines.Count -gt 0 -and $lines[-1] -eq '') {
        $lines = $lines[0..($lines.Count - 2)]
    }

    $pattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    $replaced = $false
    $updated = foreach ($line in $lines) {
        if (-not $replaced -and $line -match $pattern) {
            $replaced = $true
            "$Key = $Value"
        }
        else {
            $line
        }
    }

    if (-not $replaced) {
        throw "key '$Key' not found in $Path"
    }

    $text = ($updated -join $newline) + $newline
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [IO.File]::WriteAllText($Path, $text, $utf8)
}

function Get-DisabledItemsSql {
    param([Parameter(Mandatory = $true)][string]$Name)

    switch ($Name.ToLowerInvariant()) {
        'vanilla' {
            return @(
                'INSERT IGNORE INTO acore_world.mod_auctionhousebot_disabled_items (item)',
                'SELECT entry FROM acore_world.item_template WHERE entry > 23000;'
            ) -join "`n"
        }
        'tbc' {
            return @(
                'DELETE FROM acore_world.mod_auctionhousebot_disabled_items WHERE item > 23000 AND item <= 35000;',
                'INSERT IGNORE INTO acore_world.mod_auctionhousebot_disabled_items (item)',
                'SELECT entry FROM acore_world.item_template WHERE entry > 35000;'
            ) -join "`n"
        }
        'wotlk' {
            return 'DELETE FROM acore_world.mod_auctionhousebot_disabled_items WHERE item > 23000;'
        }
        default {
            throw "unknown era '$Name' (expected vanilla|tbc|wotlk)"
        }
    }
}

function Get-RndbotBackfillSql {
    param([Parameter(Mandatory = $true)][int]$Tier)

    $ids = Get-ProgressionQuestIds $Tier
    if ($ids.Count -eq 0) {
        return ''
    }

    $unions = ($ids | ForEach-Object {
            if ($_ -eq $ids[0]) {
                "SELECT $_ AS id"
            }
            else {
                "UNION SELECT $_"
            }
        }) -join ' '
    return @"
INSERT IGNORE INTO acore_characters.character_queststatus_rewarded (guid, quest)
SELECT c.guid, q.id
FROM acore_characters.characters c
JOIN acore_auth.account a ON a.id = c.account
JOIN ($unions) q
WHERE a.username LIKE 'RNDBOT%';
"@
}

function Get-EraConfFiles {
    param([Parameter(Mandatory = $true)][string]$Root)
    return [ordered]@{
        worldserver            = Join-Path $Root 'env\dist\etc\worldserver.conf'
        playerbots             = Join-Path $Root 'env\dist\etc\modules\playerbots.conf'
        ahbot                  = Join-Path $Root 'env\dist\etc\modules\mod_ahbot.conf'
        individualProgression  = Join-Path $Root 'env\dist\etc\modules\individualProgression.conf'
    }
}

function Get-EraConfAssignments {
    param([Parameter(Mandatory = $true)]$Settings)
    return @(
        @{ File = 'worldserver'; Key = 'CharacterCreating.Disabled.RaceMask'; Value = $Settings.RaceMask }
        @{ File = 'worldserver'; Key = 'CharacterCreating.Disabled.ClassMask'; Value = $Settings.ClassMask }
        @{ File = 'playerbots'; Key = 'AiPlayerbot.RandomBotMaxLevel'; Value = $Settings.RandomBotMaxLevel }
        @{ File = 'playerbots'; Key = 'AiPlayerbot.RandomBotMaps'; Value = $Settings.RandomBotMaps }
        @{ File = 'playerbots'; Key = 'AiPlayerbot.UseFlyMountAtMinLevel'; Value = $Settings.UseFlyMountAtMinLevel }
        @{ File = 'playerbots'; Key = 'AiPlayerbot.DisableDeathKnightLogin'; Value = $Settings.DisableDeathKnightLogin }
        @{ File = 'playerbots'; Key = 'AiPlayerbot.RandomBotJoinLfg'; Value = $Settings.RandomBotJoinLfg }
        @{ File = 'ahbot'; Key = 'AuctionHouseBot.DisableItemsAboveReqLevel'; Value = $Settings.DisableItemsAboveReqLevel }
        @{ File = 'ahbot'; Key = 'AuctionHouseBot.DisableTGsAboveReqLevel'; Value = $Settings.DisableTGsAboveReqLevel }
        @{ File = 'ahbot'; Key = 'AuctionHouseBot.DisableItemsAboveLevel'; Value = $Settings.DisableItemsAboveLevel }
        @{ File = 'ahbot'; Key = 'AuctionHouseBot.DisableTGsAboveLevel'; Value = $Settings.DisableTGsAboveLevel }
        @{ File = 'ahbot'; Key = 'AuctionHouseBot.DisableDKItems'; Value = $Settings.DisableDKItems }
        @{ File = 'individualProgression'; Key = 'IndividualProgression.DisableRDF'; Value = $Settings.DisableRDF }
        @{ File = 'individualProgression'; Key = 'IndividualProgression.BotAccountsMaxLevel'; Value = $Settings.BotAccountsMaxLevel }
    )
}

function Invoke-EraMysql {
    param(
        [Parameter(Mandatory = $true)][string]$Sql,
        [switch]$Scalar
    )

    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = $Sql | & docker exec -i $MysqlContainer mysql "-u$MysqlUser" "-p$MysqlPassword" --batch --raw -N 2>&1
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    $lines = @($output | ForEach-Object { "$_" } | Where-Object { $_ -notmatch 'Using a password on the command line' })
    if ($exit -ne 0) {
        throw "mysql failed: $($lines -join '`n')"
    }
    if ($Scalar) {
        if ($lines.Count -eq 0 -or [string]::IsNullOrWhiteSpace($lines[-1])) {
            return 0
        }
        return [int]$lines[-1]
    }
    return ($lines -join "`n")
}

function Get-EraWritePreview {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Settings
    )

    $rndbots = Invoke-EraMysql -Sql @"
SELECT COUNT(*) FROM acore_characters.characters c
JOIN acore_auth.account a ON a.id = c.account
WHERE a.username LIKE 'RNDBOT%';
"@ -Scalar

    $questIds = Get-ProgressionQuestIds $Settings.ProgressionTier
    $questRows = $rndbots * $questIds.Count

    $disableCount = 0
    $enableCount = 0
    if ($Name -eq 'vanilla') {
        $disableCount = Invoke-EraMysql -Sql 'SELECT COUNT(*) FROM acore_world.item_template WHERE entry > 23000;' -Scalar
    }
    elseif ($Name -eq 'tbc') {
        $enableCount = Invoke-EraMysql -Sql 'SELECT COUNT(*) FROM acore_world.mod_auctionhousebot_disabled_items WHERE item > 23000 AND item <= 35000;' -Scalar
        $disableCount = Invoke-EraMysql -Sql 'SELECT COUNT(*) FROM acore_world.item_template WHERE entry > 35000;' -Scalar
    }
    else {
        $enableCount = Invoke-EraMysql -Sql 'SELECT COUNT(*) FROM acore_world.mod_auctionhousebot_disabled_items WHERE item > 23000;' -Scalar
    }

    return [pscustomobject]@{
        RndbotCharacters = $rndbots
        BackfillQuestRows = $questRows
        DisabledItemsToInsert = $disableCount
        DisabledItemsToDelete = $enableCount
    }
}

function Write-EraConfigs {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)]$Settings
    )

    $files = Get-EraConfFiles -Root $Root
    $written = @()
    foreach ($row in Get-EraConfAssignments -Settings $Settings) {
        $path = $files[$row.File]
        Set-ConfValue -Path $path -Key $row.Key -Value $row.Value
        $written += [pscustomobject]@{ File = $row.File; Key = $row.Key; Value = $row.Value }
    }
    return $written
}

function Invoke-SetEra {
    param(
        [Parameter(Mandatory = $true)][string]$Era,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [switch]$WriteDb,
        [switch]$Force
    )

    if (-not $WriteDb) {
        throw '-WriteDb is required'
    }

    $name = $Era.ToLowerInvariant()
    $settings = Get-EraSettings $name
    $preview = Get-EraWritePreview -Name $name -Settings $settings

    Write-Host "Era: $name"
    Write-Host ("RNDBOT characters: {0}" -f $preview.RndbotCharacters)
    Write-Host ("RNDBOT hidden-quest rows to insert: {0}" -f $preview.BackfillQuestRows)
    Write-Host ("AH disabled_items inserts: {0}" -f $preview.DisabledItemsToInsert)
    Write-Host ("AH disabled_items deletes: {0}" -f $preview.DisabledItemsToDelete)

    if (-not $Force) {
        $answer = Read-Host 'Apply database writes? [y/N]'
        if ($answer -notmatch '^[yY]') {
            throw 'aborted'
        }
    }

    $written = Write-EraConfigs -Root $RepoRoot -Settings $settings
    foreach ($row in $written) {
        Write-Host ("{0}: {1} = {2}" -f $row.File, $row.Key, $row.Value)
    }

    $itemSql = Get-DisabledItemsSql $name
    Invoke-EraMysql -Sql $itemSql | Out-Null

    $backfill = Get-RndbotBackfillSql $settings.ProgressionTier
    if ($backfill) {
        Invoke-EraMysql -Sql $backfill | Out-Null
    }

    Write-Host ''
    Write-Host 'AiPlayerbot.* changes require a worldserver restart.'
    Write-Host 'AH keys can be applied with .reload config (existing auctions expire or seller restart).'
    Write-Host 'Existing level-60 random bots wait for periodic randomize or a bot re-init.'
}

if ($MyInvocation.InvocationName -ne '.') {
    if (-not $Era) {
        throw 'usage: set-era.ps1 vanilla|tbc|wotlk -WriteDb [-Force]'
    }
    Invoke-SetEra -Era $Era -RepoRoot $RepoRoot -WriteDb:$WriteDb -Force:$Force
}
