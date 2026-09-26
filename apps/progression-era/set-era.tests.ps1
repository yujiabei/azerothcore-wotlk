$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
. (Join-Path $here 'set-era.ps1')

$failed = 0
$passed = 0

function Assert-True {
    param([bool]$Condition, [string]$Name)
    if ($Condition) {
        $script:passed++
        Write-Host "PASS $Name"
    }
    else {
        $script:failed++
        Write-Host "FAIL $Name"
    }
}

function Assert-Eq {
    param($Actual, $Expected, [string]$Name)
    if ("$Actual" -eq "$Expected") {
        $script:passed++
        Write-Host "PASS $Name"
    }
    else {
        $script:failed++
        Write-Host "FAIL $Name expected=[$Expected] actual=[$Actual]"
    }
}

$vanilla = Get-EraSettings 'vanilla'
Assert-Eq $vanilla.RandomBotMaxLevel 60 'vanilla RandomBotMaxLevel'
Assert-Eq $vanilla.RandomBotMaps '0,1' 'vanilla RandomBotMaps'
Assert-Eq $vanilla.UseFlyMountAtMinLevel 80 'vanilla UseFlyMountAtMinLevel'
Assert-Eq $vanilla.DisableItemsAboveReqLevel 60 'vanilla DisableItemsAboveReqLevel'
Assert-Eq $vanilla.DisableItemsAboveLevel 92 'vanilla DisableItemsAboveLevel'
Assert-Eq $vanilla.DisableTGsAboveReqLevel 60 'vanilla DisableTGsAboveReqLevel'
Assert-Eq $vanilla.DisableTGsAboveLevel 92 'vanilla DisableTGsAboveLevel'
Assert-Eq $vanilla.DisableDKItems 1 'vanilla DisableDKItems'
Assert-Eq $vanilla.DisableDeathKnightLogin 1 'vanilla DisableDeathKnightLogin'
Assert-Eq $vanilla.DisableRDF 1 'vanilla DisableRDF'
Assert-Eq $vanilla.RandomBotJoinLfg 0 'vanilla RandomBotJoinLfg'
Assert-Eq $vanilla.RaceMask 1536 'vanilla RaceMask'
Assert-Eq $vanilla.ClassMask 32 'vanilla ClassMask'
Assert-Eq $vanilla.BotAccountsMaxLevel 60 'vanilla BotAccountsMaxLevel'
Assert-Eq $vanilla.ProgressionTier 0 'vanilla ProgressionTier'
Assert-Eq $vanilla.DisabledItemMinExclusive 23000 'vanilla DisabledItemMinExclusive'

$tbc = Get-EraSettings 'tbc'
Assert-Eq $tbc.RandomBotMaxLevel 70 'tbc RandomBotMaxLevel'
Assert-Eq $tbc.RandomBotMaps '0,1,530' 'tbc RandomBotMaps'
Assert-Eq $tbc.UseFlyMountAtMinLevel 70 'tbc UseFlyMountAtMinLevel'
Assert-Eq $tbc.DisableItemsAboveReqLevel 70 'tbc DisableItemsAboveReqLevel'
Assert-Eq $tbc.DisableItemsAboveLevel 164 'tbc DisableItemsAboveLevel'
Assert-Eq $tbc.DisableDKItems 1 'tbc DisableDKItems'
Assert-Eq $tbc.DisableDeathKnightLogin 1 'tbc DisableDeathKnightLogin'
Assert-Eq $tbc.DisableRDF 1 'tbc DisableRDF'
Assert-Eq $tbc.RandomBotJoinLfg 0 'tbc RandomBotJoinLfg'
Assert-Eq $tbc.RaceMask 0 'tbc RaceMask'
Assert-Eq $tbc.ClassMask 32 'tbc ClassMask'
Assert-Eq $tbc.BotAccountsMaxLevel 70 'tbc BotAccountsMaxLevel'
Assert-Eq $tbc.ProgressionTier 8 'tbc ProgressionTier'
Assert-Eq $tbc.DisabledItemMinExclusive 35000 'tbc DisabledItemMinExclusive'

$wotlk = Get-EraSettings 'wotlk'
Assert-Eq $wotlk.RandomBotMaxLevel 80 'wotlk RandomBotMaxLevel'
Assert-Eq $wotlk.RandomBotMaps '0,1,530,571' 'wotlk RandomBotMaps'
Assert-Eq $wotlk.UseFlyMountAtMinLevel 60 'wotlk UseFlyMountAtMinLevel'
Assert-Eq $wotlk.DisableItemsAboveReqLevel 0 'wotlk DisableItemsAboveReqLevel'
Assert-Eq $wotlk.DisableItemsAboveLevel 0 'wotlk DisableItemsAboveLevel'
Assert-Eq $wotlk.DisableDKItems 0 'wotlk DisableDKItems'
Assert-Eq $wotlk.DisableDeathKnightLogin 0 'wotlk DisableDeathKnightLogin'
Assert-Eq $wotlk.DisableRDF 0 'wotlk DisableRDF'
Assert-Eq $wotlk.RandomBotJoinLfg 1 'wotlk RandomBotJoinLfg'
Assert-Eq $wotlk.RaceMask 0 'wotlk RaceMask'
Assert-Eq $wotlk.ClassMask 0 'wotlk ClassMask'
Assert-Eq $wotlk.BotAccountsMaxLevel 80 'wotlk BotAccountsMaxLevel'
Assert-Eq $wotlk.ProgressionTier 13 'wotlk ProgressionTier'
Assert-Eq $wotlk.DisabledItemMinExclusive 0 'wotlk DisabledItemMinExclusive'

$unknownFailed = $false
try {
    Get-EraSettings 'classic' | Out-Null
}
catch {
    $unknownFailed = $true
}
Assert-True $unknownFailed 'unknown era throws'

$vanillaQuests = Get-ProgressionQuestIds 0
Assert-Eq $vanillaQuests.Count 0 'vanilla quest ids empty'
$tbcQuests = Get-ProgressionQuestIds 8
Assert-Eq $tbcQuests.Count 8 'tbc quest count'
Assert-Eq $tbcQuests[0] 66001 'tbc first quest'
Assert-Eq $tbcQuests[7] 66008 'tbc last quest'
$wotlkQuests = Get-ProgressionQuestIds 13
Assert-Eq $wotlkQuests.Count 13 'wotlk quest count'
Assert-Eq $wotlkQuests[12] 66013 'wotlk last quest'

$tmp = Join-Path $env:TEMP ("set-era-test-" + [guid]::NewGuid().ToString('N') + ".conf")
@"
#    AiPlayerbot.RandomBotMaxLevel
#        Description: keep this comment
AiPlayerbot.RandomBotMaxLevel = 80
Other.Key = 1
"@ | Set-Content -LiteralPath $tmp -Encoding utf8
Set-ConfValue -Path $tmp -Key 'AiPlayerbot.RandomBotMaxLevel' -Value '60'
$after = Get-Content -LiteralPath $tmp
Assert-True ($after -contains 'AiPlayerbot.RandomBotMaxLevel = 60') 'conf writes target key'
Assert-True ($after -contains '#    AiPlayerbot.RandomBotMaxLevel') 'conf keeps commented key'
Assert-True ($after -contains '#        Description: keep this comment') 'conf keeps comments'
Assert-True ($after -contains 'Other.Key = 1') 'conf keeps other keys'
Remove-Item -LiteralPath $tmp -Force

$vanillaSql = Get-DisabledItemsSql 'vanilla'
Assert-True ($vanillaSql -match 'INSERT IGNORE INTO acore_world.mod_auctionhousebot_disabled_items') 'vanilla inserts disabled items'
Assert-True ($vanillaSql -match 'entry > 23000') 'vanilla disables entry > 23000'
Assert-True ($vanillaSql -notmatch 'DELETE FROM acore_world.mod_auctionhousebot_disabled_items') 'vanilla does not delete era items'

$tbcSql = Get-DisabledItemsSql 'tbc'
Assert-True ($tbcSql -match 'DELETE FROM acore_world.mod_auctionhousebot_disabled_items WHERE item > 23000 AND item <= 35000') 'tbc re-enables TBC entries'
Assert-True ($tbcSql -match 'entry > 35000') 'tbc disables entry > 35000'

$wotlkSql = Get-DisabledItemsSql 'wotlk'
Assert-True ($wotlkSql -match 'DELETE FROM acore_world.mod_auctionhousebot_disabled_items WHERE item > 23000') 'wotlk clears era disables'
Assert-True ($wotlkSql -notmatch 'INSERT IGNORE INTO acore_world.mod_auctionhousebot_disabled_items') 'wotlk does not add era disables'

$backfillNone = Get-RndbotBackfillSql 0
Assert-Eq $backfillNone '' 'vanilla has no rndbot backfill'
$backfillTbc = Get-RndbotBackfillSql 8
Assert-True ($backfillTbc -match 'INSERT IGNORE INTO acore_characters.character_queststatus_rewarded') 'tbc backfill inserts rewarded quests'
Assert-True ($backfillTbc -match "username LIKE 'RNDBOT%'") 'tbc backfill only RNDBOT'
Assert-True ($backfillTbc -match '66008') 'tbc backfill includes 66008'
Assert-True ($backfillTbc -notmatch '66009') 'tbc backfill stops at 66008'
$backfillWotlk = Get-RndbotBackfillSql 13
Assert-True ($backfillWotlk -match '66013') 'wotlk backfill includes 66013'
Assert-True ($backfillWotlk -notmatch '66014') 'wotlk backfill stops at 66013'

$writeDbRequired = $false
try {
    Invoke-SetEra -Era 'vanilla' -RepoRoot $here -WriteDb:$false
}
catch {
    $writeDbRequired = $_.Exception.Message -match 'WriteDb'
}
Assert-True $writeDbRequired 'WriteDb is required'

Write-Host ""
Write-Host "Passed: $passed  Failed: $failed"
if ($failed -gt 0) {
    exit 1
}
