-- Fix Chinese UI text overlap / truncation on zhCN clients (especially 4K + UI scale).
-- Covers: UnitPopup / FriendsMenuXP menus, quest reward money vs XP, quest buttons,
-- EN_UnitFrames HP/MP text crowding, and PaperDoll stat rows.
--
-- Three kinds of 叠字:
-- 1) Layout: leftover width / height 0 / CJK wrap. Fix by measuring and
--    re-anchoring after each content update, never a per-word pixel offset.
-- 2) Font shadow: zhCN GameFont* inherit SystemFont_Shadow_* at 15px with a
--    1px shadow. On parchment and at non-integer UI scale the shadow reads as
--    a second copy of every glyph. FontStrings copy that shadow at creation.
--    Strip the shared objects and existing strings, keep it off after SetFont,
--    and run measure-based layout on the next frames so CJK width is valid.
-- 3) Quest typewriter: SetAlphaGradient(start, 30) is a 30px fade. When the
--    fade ends the API returns nil and leaves that window on. QuestFont is
--    ~14-15px, so the last two CJK glyphs stay in the gradient pass, which
--    does not advance CJK width and stacks them (e.g. 后 on 悔). Clear with
--    SetAlphaGradient(0, 0). This is not a wrap: the line still has unused
--    width after those two glyphs.

local BUTTON_HEIGHT = 22
local MIN_MENU_WIDTH = 140
local QUEST_BUTTON_MIN_WIDTH = 90
local QUEST_BUTTON_HEIGHT = 24
local FitOneLineButtonLabel

UIDROPDOWNMENU_BUTTON_HEIGHT = BUTTON_HEIGHT

-- Shared UI fonts. Combat text / subtitles keep their shadow so they stay readable
-- on the 3D world.
local SHADOWLESS_FONTS = {
    "SystemFont_Tiny",
    "SystemFont_Small",
    "SystemFont_Med1",
    "SystemFont_Med2",
    "SystemFont_Med3",
    "SystemFont_Large",
    "SystemFont_Shadow_Small",
    "SystemFont_Shadow_Med1",
    "SystemFont_Shadow_Med2",
    "SystemFont_Shadow_Med3",
    "SystemFont_Shadow_Large",
    "SystemFont_Shadow_Huge",
    "GameFontNormal",
    "GameFontHighlight",
    "GameFontNormalSmall",
    "GameFontHighlightSmall",
    "GameFontDisable",
    "GameFontDisableSmall",
    "GameFontNormalLarge",
    "GameFontHighlightLarge",
    "GameFontWhite",
    "GameFontBlack",
    "QuestTitleFont",
    "QuestFont",
    "QuestFontNormalSmall",
    "QuestFont_Large",
    "QuestFont_Shadow_Huge",
    "GameTooltipText",
    "GameTooltipTextSmall",
    "GameTooltipHeaderText",
    "Tooltip_Med",
    "Tooltip_Small",
    "NumberFontNormal",
    "NumberFontNormalSmall",
    "NumberFont_Shadow_Small",
    "NumberFont_Shadow_Med",
    "NumberFont_Shadow_Large",
    "ChatFontNormal",
    "ChatFontSmall",
    "FriendsFont_Normal",
    "FriendsFont_Small",
    "FriendsFont_Large",
    "FriendsFont_UserText",
    "InvoiceTextFontNormal",
    "InvoiceTextFontSmall",
    "MailTextFontNormal",
    "ReputationDetailFont",
    "SpellFont_Small",
}

local function StripOneFont(font)
    if not font then
        return
    end
    if font._FixDropDownMenuStripping then
        return
    end
    font._FixDropDownMenuStripping = true
    if font.SetShadowOffset then
        font:SetShadowOffset(0, 0)
    end
    if font.SetShadowColor then
        font:SetShadowColor(0, 0, 0, 0)
    end
    font._FixDropDownMenuStripping = nil
end

local function HookFontKeepShadowless(font)
    if not font or font._FixDropDownMenuFontHooked then
        return
    end
    font._FixDropDownMenuFontHooked = true
    if font.SetFont then
        hooksecurefunc(font, "SetFont", function(self)
            StripOneFont(self)
        end)
    end
    if font.SetShadowOffset then
        hooksecurefunc(font, "SetShadowOffset", function(self, x, y)
            if self._FixDropDownMenuStripping then
                return
            end
            if (x or 0) ~= 0 or (y or 0) ~= 0 then
                StripOneFont(self)
            end
        end)
    end
end

local function StripFontShadows()
    for i = 1, #SHADOWLESS_FONTS do
        local font = _G[SHADOWLESS_FONTS[i]]
        if font then
            StripOneFont(font)
            HookFontKeepShadowless(font)
        end
    end
end

local function ShouldSkipShadowWalk(frame)
    local name = frame.GetName and frame:GetName()
    if not name then
        return false
    end
    if name == "WorldFrame" or name == "MovieFrame" then
        return true
    end
    if name:find("CombatText", 1, true) or name:find("NamePlate", 1, true) then
        return true
    end
    return false
end

local function StripFontStringShadow(fs)
    if not fs or not fs.SetShadowOffset then
        return
    end
    fs:SetShadowOffset(0, 0)
    if fs.SetShadowColor then
        fs:SetShadowColor(0, 0, 0, 0)
    end
end

local shadowWalkCount = 0
local SHADOW_WALK_LIMIT = 25000

local function WalkStripFontStringShadows(frame)
    if not frame or shadowWalkCount > SHADOW_WALK_LIMIT then
        return
    end
    shadowWalkCount = shadowWalkCount + 1
    if ShouldSkipShadowWalk(frame) then
        return
    end

    local okRegions, regions = pcall(function()
        return { frame:GetRegions() }
    end)
    if okRegions then
        for i = 1, #regions do
            local region = regions[i]
            if region and region.GetObjectType and region:GetObjectType() == "FontString" then
                StripFontStringShadow(region)
            end
        end
    end

    local okChildren, children = pcall(function()
        return { frame:GetChildren() }
    end)
    if okChildren then
        for i = 1, #children do
            WalkStripFontStringShadows(children[i])
        end
    end
end

local function StripAllFontStringShadows()
    shadowWalkCount = 0
    WalkStripFontStringShadows(UIParent)
end

-- Missing zhCN strings in FriendsMenuXP (present in enUS / zhTW only).
if not FMXP_BUTTON_PROMOTE then
    FMXP_BUTTON_PROMOTE = "提升为队长"
end
if not FMXP_BUTTON_LOOT_PROMOTE then
    FMXP_BUTTON_LOOT_PROMOTE = "设为拾取者"
end
if not FMXP_BUTTON_ACHIEVEMENTS then
    FMXP_BUTTON_ACHIEVEMENTS = "对比成就"
end

if FriendsMenuXP_Buttons then
    if FriendsMenuXP_Buttons["PROMOTE"] then
        FriendsMenuXP_Buttons["PROMOTE"].text = FMXP_BUTTON_PROMOTE
    end
    if FriendsMenuXP_Buttons["LOOT_PROMOTE"] then
        FriendsMenuXP_Buttons["LOOT_PROMOTE"].text = FMXP_BUTTON_LOOT_PROMOTE
    end
    if FriendsMenuXP_Buttons["ACHIEVEMENTS"] then
        FriendsMenuXP_Buttons["ACHIEVEMENTS"].text = FMXP_BUTTON_ACHIEVEMENTS
    end
end

local function TextWidth(fontString)
    if fontString.GetStringWidth then
        return fontString:GetStringWidth()
    end
    return fontString:GetWidth()
end

local function ResizeDropDownButtons(listFrame)
    if not listFrame then
        return
    end

    local i = 1
    while true do
        local button = _G[listFrame:GetName() .. "Button" .. i]
        if not button then
            break
        end
        button:SetHeight(BUTTON_HEIGHT)
        i = i + 1
    end
end

local function MenuButtonText(button)
    if not button then
        return nil
    end
    local name = button:GetName()
    if name then
        return _G[name .. "NormalText"] or button:GetFontString()
    end
    return button:GetFontString()
end

-- ButtonText leftover width is one CJK glyph. 悄悄话 wraps (悄悄 / 话),
-- 邀请 clips to "...", and 离开队伍 wraps 伍. Measure in a wide box;
-- SetWidth(0) clips to a leftover glyph and the last CJK starts a new line.
local function UnlockMenuButtonText(fs)
    if not fs then
        return 0
    end
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    fs:SetJustifyH("LEFT")
    fs:SetWidth(400)
    fs:SetHeight(16)
    return TextWidth(fs)
end

local function FitMenuButtonLabel(button, innerWidth)
    local fs = MenuButtonText(button)
    if not fs then
        return
    end
    UnlockMenuButtonText(fs)
    if innerWidth and innerWidth > 0 then
        fs:SetWidth(innerWidth)
    end
    fs:SetHeight(16)
end

local function FixListFrameWidth(listFrame)
    if not listFrame or not listFrame.numButtons or listFrame.numButtons == 0 then
        return
    end

    local maxWidth = MIN_MENU_WIDTH
    for i = 1, listFrame.numButtons do
        local button = _G[listFrame:GetName() .. "Button" .. i]
        if button and button:IsShown() then
            button:SetWidth(400)
            local width = UnlockMenuButtonText(MenuButtonText(button)) + 40
            if width > maxWidth then
                maxWidth = width
            end
        end
    end

    listFrame.maxWidth = maxWidth
    local inner = maxWidth - 16
    for i = 1, (FRIENDSMENU_MAXBUTTONS or listFrame.numButtons) do
        local button = _G[listFrame:GetName() .. "Button" .. i]
        if button then
            button:SetWidth(maxWidth)
            button:SetHeight(BUTTON_HEIGHT)
            FitMenuButtonLabel(button, inner)
        end
    end
    listFrame:SetWidth(maxWidth + 15)
    listFrame:SetHeight((listFrame.numButtons * BUTTON_HEIGHT) + (UIDROPDOWNMENU_BORDER_HEIGHT * 2))
end

local function FixOpenDropDownLists()
    local level = 1
    while true do
        local listFrame = _G["DropDownList" .. level]
        if not listFrame then
            break
        end
        if listFrame:IsShown() then
            UIDROPDOWNMENU_BUTTON_HEIGHT = BUTTON_HEIGHT
            FixListFrameWidth(listFrame)
        end
        level = level + 1
    end
    for _, name in ipairs({ "FriendsMenuXP", "FriendsMenuXPSecure" }) do
        local frame = _G[name]
        if frame and frame:IsShown() then
            FixListFrameWidth(frame)
        end
    end
end

local dropDownFixFrame = CreateFrame("Frame")
dropDownFixFrame:Hide()
dropDownFixFrame.elapsed = 0
dropDownFixFrame:SetScript("OnUpdate", function(self, elapsed)
    self.elapsed = self.elapsed + elapsed
    FixOpenDropDownLists()
    if self.elapsed > 0.15 then
        self:Hide()
        self.elapsed = 0
    end
end)

local function ScheduleDropDownFix()
    dropDownFixFrame.elapsed = 0
    dropDownFixFrame:Show()
    FixOpenDropDownLists()
end

hooksecurefunc("ToggleDropDownMenu", function()
    ScheduleDropDownFix()
end)

if UIDropDownMenu_Refresh then
    hooksecurefunc("UIDropDownMenu_Refresh", function()
        ScheduleDropDownFix()
    end)
end

local function ResizeAllDropDownLists()
    local level = 1
    while true do
        local listFrame = _G["DropDownList" .. level]
        if not listFrame then
            break
        end
        ResizeDropDownButtons(listFrame)
        level = level + 1
    end

    for _, name in ipairs({ "FriendsMenuXP", "FriendsMenuXPSecure" }) do
        local frame = _G[name]
        if frame then
            ResizeDropDownButtons(frame)
        end
    end
end

hooksecurefunc("UIDropDownMenu_AddButton", function(_, level)
    level = level or 1
    local listFrame = _G["DropDownList" .. level]
    if not listFrame then
        return
    end

    local index = listFrame.numButtons
    local button = _G[listFrame:GetName() .. "Button" .. index]
    if not button then
        return
    end

    button:SetHeight(BUTTON_HEIGHT)

    local oldWidth = button:GetWidth()
    button:SetWidth(400)
    local width = UnlockMenuButtonText(MenuButtonText(button)) + 40
    button:SetWidth(oldWidth)
    if width > (listFrame.maxWidth or 0) then
        listFrame.maxWidth = width
    end
    if (listFrame.maxWidth or 0) < MIN_MENU_WIDTH then
        listFrame.maxWidth = MIN_MENU_WIDTH
    end
end)

local function HookFriendsMenuAddButton()
    if not FriendsMenuXP_AddButton or HookFriendsMenuAddButton._hooked then
        return
    end
    HookFriendsMenuAddButton._hooked = true
    hooksecurefunc("FriendsMenuXP_AddButton", function(listFrame)
        if not listFrame or not listFrame.numButtons then
            return
        end
        local button = _G[listFrame:GetName() .. "Button" .. listFrame.numButtons]
        if not button then
            return
        end
        local width = UnlockMenuButtonText(MenuButtonText(button)) + 40
        if width > (listFrame.maxWidth or 0) then
            listFrame.maxWidth = width
        end
        if (listFrame.maxWidth or 0) < MIN_MENU_WIDTH then
            listFrame.maxWidth = MIN_MENU_WIDTH
        end
    end)
end

HookFriendsMenuAddButton()

local function HookFriendsMenu(frame)
    if not frame or frame._FixDropDownMenuHooked then
        return
    end
    frame._FixDropDownMenuHooked = true

    frame:HookScript("OnShow", function(self)
        UIDROPDOWNMENU_BUTTON_HEIGHT = BUTTON_HEIGHT
        FixListFrameWidth(self)
        ScheduleDropDownFix()
    end)
end

local function LayoutInlineLabelValue(label, value)
    if not label then
        return
    end
    if label.SetNonSpaceWrap then
        label:SetNonSpaceWrap(true)
    end
    label:SetJustifyH("LEFT")
    label:SetWidth(0)
    local labelWidth = TextWidth(label)
    if labelWidth > 0 then
        label:SetWidth(labelWidth + 2)
    end

    if not value then
        return
    end
    if value.SetNonSpaceWrap then
        value:SetNonSpaceWrap(true)
    end
    value:SetJustifyH("LEFT")

    local valueText = value:GetText() or ""
    local labelText = label:GetText() or ""
    if valueText ~= "" and labelText:find(valueText, 1, true) then
        value:SetText("")
    end

    value:SetWidth(0)
    local valueWidth = TextWidth(value)
    if valueWidth > 0 then
        value:SetWidth(valueWidth + 2)
    end
    value:ClearAllPoints()
    value:SetPoint("LEFT", label, "RIGHT", 6, 0)
end

-- Quest rewards: Chinese "你将得到:" is much shorter than "You will receive:",
-- so QuestInfoMoneyFrame (anchored to the right of that label) sits on top of the
-- XP line. Keep coins after the XP value, and keep "经验值" / the number on one line.
local function FixQuestRewardMoneyOverlap()
    local xp = QuestInfoXPFrame
    local points = QuestInfoXPFramePoints
    local xpLabel = QuestInfoXPFrameReceiveText
    if xp and xp:IsShown() then
        LayoutInlineLabelValue(xpLabel, points)
    end

    local money = QuestInfoMoneyFrame
    if not money or not money:IsShown() then
        return
    end

    local receive = QuestInfoItemReceiveText
    money:ClearAllPoints()
    if xp and xp:IsShown() and points and (points:GetText() or "") ~= "" then
        money:SetPoint("LEFT", points, "RIGHT", 12, 0)
    elseif xp and xp:IsShown() and xpLabel then
        money:SetPoint("LEFT", xpLabel, "RIGHT", 12, 0)
    elseif receive and receive:IsShown() then
        money:SetPoint("TOPLEFT", receive, "BOTTOMLEFT", 0, -6)
    end
end

local QUEST_BUTTONS = {
    "QuestFrameAcceptButton",
    "QuestFrameDeclineButton",
    "QuestFrameCompleteQuestButton",
    "QuestFrameCompleteButton",
    "QuestFrameGoodbyeButton",
    "QuestFrameCancelButton",
    "QuestFrameGreetingGoodbyeButton",
    "QuestLogFrameAbandonButton",
    "QuestLogFrameTrackButton",
    "QuestLogFramePushQuestButton",
    "QuestLogFrameCancelButton",
}

-- Share sits between Abandon and Track (LEFT+RIGHT). Do not break that stretch.
local QUEST_BUTTON_KEEP_WIDTH = {
    ["QuestLogFramePushQuestButton"] = true,
}

local function FixQuestButton(button)
    if not button then
        return
    end

    local fs = button:GetFontString()
    if fs then
        -- CJK wraps per glyph. An unlocked leftover width stacks "放" on "弃".
        if fs.SetNonSpaceWrap then
            fs:SetNonSpaceWrap(true)
        end
        if fs.SetShadowOffset then
            fs:SetShadowOffset(0, 0)
        end
        fs:SetJustifyH("CENTER")
        fs:SetWidth(0)
        local textWidth = TextWidth(fs)
        local width = textWidth + 28
        if width < QUEST_BUTTON_MIN_WIDTH then
            width = QUEST_BUTTON_MIN_WIDTH
        end
        if width > button:GetWidth() and not QUEST_BUTTON_KEEP_WIDTH[button:GetName()] then
            button:SetWidth(width)
        end
        local inner = button:GetWidth() - 8
        if inner < textWidth + 2 then
            inner = textWidth + 2
        end
        fs:SetWidth(inner)
    elseif button:GetWidth() < QUEST_BUTTON_MIN_WIDTH and not QUEST_BUTTON_KEEP_WIDTH[button:GetName()] then
        button:SetWidth(QUEST_BUTTON_MIN_WIDTH)
    end

    if button:GetHeight() < QUEST_BUTTON_HEIGHT then
        button:SetHeight(QUEST_BUTTON_HEIGHT)
    end
end

-- Greeting / gossip quest rows are 300x16 with ButtonText 275x0. Height 0 plus
-- CJK wrap paints 捕 on 捉 ("捕捉矿道老鼠").
local function FixQuestListTitleButton(button)
    if not button or not button:IsShown() then
        return
    end
    local fs = button:GetFontString()
    if not fs then
        return
    end
    if fs.SetNonSpaceWrap then
        fs:SetNonSpaceWrap(true)
    end
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    fs:SetJustifyH("LEFT")
    fs:SetWidth(275)
    fs:SetHeight(16)
end

local function FixQuestListTitleButtons()
    for i = 1, 32 do
        FixQuestListTitleButton(_G["QuestTitleButton" .. i])
        FixQuestListTitleButton(_G["GossipTitleButton" .. i])
    end
end

local function FixQuestButtons()
    for _, name in ipairs(QUEST_BUTTONS) do
        FixQuestButton(_G[name])
    end
    FixQuestListTitleButtons()
end

local function FitFontStringHeight(fs, width, wrapLeftover)
    if not fs then
        return
    end
    -- CJK has no spaces. Without this, a long paragraph is one "word" and the
    -- last glyphs that do not fit are painted on the previous ones instead of
    -- starting a new line.
    if fs.SetNonSpaceWrap then
        fs:SetNonSpaceWrap(true)
    end
    if wrapLeftover and width then
        local sw = fs:GetStringWidth()
        if sw and sw > width then
            local leftover = sw - math.floor(sw / width) * width
            local em = 14
            local _, size = fs:GetFont()
            if size and size > 0 then
                em = size
            end
            -- 1-3 leftover CJK glyphs stack on the same cell instead of wrapping.
            if leftover > 0 and leftover <= em * 3 + 2 then
                width = width - leftover - 2
            end
        end
    end
    if width then
        fs:SetWidth(width)
    end
    if fs.SetSpacing then
        fs:SetSpacing(3)
    end
    -- Unlock first. A short box makes GetStringHeight return only the clipped region.
    fs:SetHeight(2000)
    local height = fs:GetStringHeight()
    if height and height > 0 then
        fs:SetHeight(height)
    end
end

-- Parchment quest bodies are ~250-300 wide and height 0. CJK leftover of 1-3
-- glyphs at a line end stacks those glyphs (e.g. 英 on 勇). Walk every
-- FontString under the quest frames and fit leftover width plus height.
local QUEST_BODY_WIDTH = 285
local questPanelWalkSeen = {}

local function ClearQuestTextGradient(fs)
    if fs and fs.SetAlphaGradient then
        fs:SetAlphaGradient(0, 0)
    end
end

local function ClearQuestDescriptionGradient()
    local fading = QuestInfoFadingFrame
    if not (fading and fading.fading) then
        ClearQuestTextGradient(QuestInfoDescriptionText)
    end
    -- Typewriter fade is only on QuestInfoDescriptionText. Other parchment
    -- strings keep the 30px window and the last two CJK glyphs stack.
    ClearQuestTextGradient(QuestInfoObjectivesText)
    ClearQuestTextGradient(QuestInfoRewardText)
    ClearQuestTextGradient(QuestProgressText)
    ClearQuestTextGradient(GreetingText)
end

local function IsQuestPanelButtonText(fs)
    local parent = fs.GetParent and fs:GetParent()
    if not parent or not parent.GetObjectType then
        return false
    end
    local otype = parent:GetObjectType()
    return otype == "Button" or otype == "CheckButton"
end

local function IsQuestPanelMoneyText(fs)
    local parent = fs.GetParent and fs:GetParent()
    while parent do
        local name = parent.GetName and parent:GetName()
        if name and string.find(name, "Money") then
            return true
        end
        parent = parent.GetParent and parent:GetParent()
    end
    return false
end

local function FitQuestPanelFontString(fs)
    if not fs or not fs.GetText or not fs.GetObjectType then
        return
    end
    if fs:GetObjectType() ~= "FontString" then
        return
    end
    if fs.IsShown and not fs:IsShown() then
        return
    end
    local text = fs:GetText()
    if not text or text == "" then
        return
    end
    if IsQuestPanelButtonText(fs) or IsQuestPanelMoneyText(fs) then
        return
    end

    local fading = QuestInfoFadingFrame
    if not (fading and fading.fading and fs == QuestInfoDescriptionText) then
        ClearQuestTextGradient(fs)
    end

    local width = fs._FixChineseUIQuestWidth
    if not width then
        width = fs:GetWidth()
        if not width or width < 20 then
            width = QUEST_BODY_WIDTH
        end
        fs._FixChineseUIQuestWidth = width
    end

    local height = fs:GetHeight()
    local isBody = fs._FixChineseUIQuestBody
        or (not height or height < 1)
        or width >= 250
    if not isBody then
        if fs.SetNonSpaceWrap then
            fs:SetNonSpaceWrap(true)
        end
        return
    end
    fs._FixChineseUIQuestBody = true

    FitFontStringHeight(fs, width, true)
    if fs:GetHeight() < 16 then
        fs:SetHeight(16)
    end
end

local function WalkQuestPanelFontStrings(frame)
    if not frame or questPanelWalkSeen[frame] then
        return
    end
    questPanelWalkSeen[frame] = true
    if frame.GetRegions then
        local regions = { frame:GetRegions() }
        for i = 1, #regions do
            local region = regions[i]
            if region then
                FitQuestPanelFontString(region)
            end
        end
    end
    if frame.GetChildren then
        local children = { frame:GetChildren() }
        for i = 1, #children do
            WalkQuestPanelFontStrings(children[i])
        end
    end
end

local function FixQuestInfoBodyText()
    for k in pairs(questPanelWalkSeen) do
        questPanelWalkSeen[k] = nil
    end
    WalkQuestPanelFontStrings(QuestFrame)
    WalkQuestPanelFontStrings(QuestInfoFrame)
    WalkQuestPanelFontStrings(QuestInfoRewardsFrame)
    WalkQuestPanelFontStrings(QuestLogFrame)
    WalkQuestPanelFontStrings(QuestLogDetailFrame)
    ClearQuestDescriptionGradient()
end

-- QuestItemTemplate name box is 100x36. Chinese names wrap to a second line
-- and paint over the first (e.g. "修桥工的手套"). Same treatment as merchant names.
local function FixQuestItemName(name)
    if not name then
        return
    end
    if name.SetNonSpaceWrap then
        name:SetNonSpaceWrap(true)
    end
    name:SetJustifyH("LEFT")
    FitFontStringHeight(name, 124)
    if name:GetHeight() < 16 then
        name:SetHeight(16)
    elseif name:GetHeight() > 36 then
        name:SetHeight(36)
    end
    if name.SetShadowOffset then
        name:SetShadowOffset(0, 0)
    end
end

local function FixQuestItemNames()
    for i = 1, 10 do
        FixQuestItemName(_G["QuestInfoItem" .. i .. "Name"])
    end
    for i = 1, 6 do
        FixQuestItemName(_G["QuestProgressItem" .. i .. "Name"])
    end
end

-- Chinese progress text wraps taller than English. QuestProgressText keeps height 0,
-- so "需要物品" / item buttons stay too high and the last line (e.g. "凶猛") paints
-- on top of the item name (e.g. "雪怪的鬃毛").
local function FixQuestProgressLayout()
    local title = QuestProgressTitleText
    local text = QuestProgressText
    local req = QuestProgressRequiredItemsText
    local moneyText = QuestProgressRequiredMoneyText
    local item1 = QuestProgressItem1

    if not text then
        return
    end

    FitFontStringHeight(title, 285)
    if title and title:GetHeight() < 16 then
        title:SetHeight(16)
    end
    FitFontStringHeight(text, 275)
    ClearQuestTextGradient(text)
    local textHeight = text:GetHeight()
    if textHeight and textHeight > 0 then
        text:SetHeight(textHeight + 6)
    end

    if req and req:IsShown() then
        FitFontStringHeight(req, 295)
        req:ClearAllPoints()
        req:SetPoint("TOPLEFT", text, "BOTTOMLEFT", 0, -10)
    end

    if moneyText and moneyText:IsShown() then
        moneyText:ClearAllPoints()
        if req and req:IsShown() then
            moneyText:SetPoint("TOPLEFT", req, "BOTTOMLEFT", 0, -10)
        else
            moneyText:SetPoint("TOPLEFT", text, "BOTTOMLEFT", 0, -10)
        end
    end

    if item1 and item1:IsShown() then
        item1:ClearAllPoints()
        if moneyText and moneyText:IsShown() then
            item1:SetPoint("TOPLEFT", moneyText, "BOTTOMLEFT", 0, -10)
        elseif req and req:IsShown() then
            item1:SetPoint("TOPLEFT", req, "BOTTOMLEFT", -3, -5)
        else
            item1:SetPoint("TOPLEFT", text, "BOTTOMLEFT", -3, -10)
        end
    end
end

local progressLayoutFrame = CreateFrame("Frame")
progressLayoutFrame:Hide()
progressLayoutFrame.elapsed = 0
progressLayoutFrame:SetScript("OnUpdate", function(self, elapsed)
    self.elapsed = self.elapsed + elapsed
    FixQuestProgressLayout()
    FixQuestInfoBodyText()
    FixQuestItemNames()
    FixQuestRewardMoneyOverlap()
    if self.elapsed > 0.35 then
        self:Hide()
        self.elapsed = 0
    end
end)

local function ScheduleQuestProgressLayout()
    progressLayoutFrame.elapsed = 0
    progressLayoutFrame:Show()
    FixQuestProgressLayout()
end

local function HookQuestFrame(frame)
    if not frame or frame._FixChineseUIQuestHooked then
        return
    end
    frame._FixChineseUIQuestHooked = true
    frame:HookScript("OnShow", function()
        FixQuestButtons()
        FixQuestRewardMoneyOverlap()
        FixQuestInfoBodyText()
        FixQuestItemNames()
        ScheduleQuestProgressLayout()
    end)
end

local GAME_MENU_BUTTONS = {
    "DuowanMenuButtonOptions",
    "GameMenuButtonOptions",
    "GameMenuButtonSoundOptions",
    "GameMenuButtonMacOptions",
    "GameMenuButtonUIOptions",
    "GameMenuButtonKeybindings",
    "GameMenuButtonMacros",
    "GameMenuButtonAddOns",
    "GameMenuButtonRatings",
    "GameMenuButtonLogout",
    "GameMenuButtonQuit",
    "GameMenuButtonContinue",
}
local GAME_MENU_BUTTON_HEIGHT = 24
local GAME_MENU_GAP = 2
local GAME_MENU_CONTINUE_GAP = 16

local function FixGameMenuButton(button)
    if not button then
        return
    end
    local fs = button:GetFontString()
    if fs then
        if fs.SetNonSpaceWrap then
            fs:SetNonSpaceWrap(true)
        end
        fs:SetJustifyH("CENTER")
        fs:SetWidth(0)
        local width = TextWidth(fs) + 24
        if width > button:GetWidth() then
            button:SetWidth(width)
        else
            fs:SetWidth(button:GetWidth() - 16)
        end
    end
    if button:GetHeight() < GAME_MENU_BUTTON_HEIGHT then
        button:SetHeight(GAME_MENU_BUTTON_HEIGHT)
    end
end

-- Duowan + ACP insert extra ESC buttons then lock GameMenuFrame to 255px.
-- Short labels wrap ("多玩魔盒" → two stacked lines) and the locked height
-- crushes the rest. Restack visible buttons and size the frame from the stack.
local function FixGameMenuLayout()
    if not GameMenuFrame then
        return
    end

    local last
    local count = 0
    for i = 1, #GAME_MENU_BUTTONS do
        local button = _G[GAME_MENU_BUTTONS[i]]
        if button and button:IsShown() then
            FixGameMenuButton(button)
            button:ClearAllPoints()
            if last then
                local gap = GAME_MENU_GAP
                if GAME_MENU_BUTTONS[i] == "GameMenuButtonContinue" then
                    gap = GAME_MENU_CONTINUE_GAP
                end
                button:SetPoint("TOP", last, "BOTTOM", 0, -gap)
            else
                button:SetPoint("TOP", GameMenuFrame, "TOP", 0, -36)
            end
            last = button
            count = count + 1
        end
    end
    if count == 0 then
        return
    end

    local extra = 0
    if GameMenuButtonContinue and GameMenuButtonContinue:IsShown() then
        extra = GAME_MENU_CONTINUE_GAP - GAME_MENU_GAP
    end
    GameMenuFrame:SetHeight(48 + extra + count * GAME_MENU_BUTTON_HEIGHT + (count - 1) * GAME_MENU_GAP)
end

local function HookGameMenuFrame()
    if not GameMenuFrame or GameMenuFrame._FixChineseUIGameMenuHooked then
        return
    end
    GameMenuFrame._FixChineseUIGameMenuHooked = true
    GameMenuFrame:HookScript("OnShow", FixGameMenuLayout)
    if GameMenuButtonAddOns then
        GameMenuButtonAddOns:HookScript("OnShow", FixGameMenuLayout)
    end
    if DuowanMenuButtonOptions then
        DuowanMenuButtonOptions:HookScript("OnShow", FixGameMenuLayout)
    end
end

local function FixStaticPopup(popup)
    if not popup or not popup:IsShown() then
        return
    end

    local name = popup:GetName()
    local text = _G[name .. "Text"]
    if text then
        if text.SetNonSpaceWrap then
            text:SetNonSpaceWrap(true)
        end
        if text.SetShadowOffset then
            text:SetShadowOffset(0, 0)
        end
        local width = text:GetWidth()
        if not width or width < 10 then
            width = 290
        end
        FitFontStringHeight(text, width)
        local btn = _G[name .. "Button1"]
        local needed = 40 + (text:GetHeight() or 0) + ((btn and btn:GetHeight()) or 21)
        if popup:GetHeight() < needed then
            popup:SetHeight(needed)
        end
    end

    for i = 1, 3 do
        local btn = _G[name .. "Button" .. i]
        FixQuestButton(btn)
        FitOneLineButtonLabel(btn)
    end
end

local function FixStaticPopups()
    for i = 1, (STATICPOPUP_NUMDIALOGS or 4) do
        FixStaticPopup(_G["StaticPopup" .. i])
    end
end

local function HookGossipFrame()
    if GossipFrame and not GossipFrame._FixChineseUIGossipHooked then
        GossipFrame._FixChineseUIGossipHooked = true
        GossipFrame:HookScript("OnShow", FixQuestListTitleButtons)
    end
    if GossipFrameUpdate and not HookGossipFrame._updateHooked then
        HookGossipFrame._updateHooked = true
        hooksecurefunc("GossipFrameUpdate", FixQuestListTitleButtons)
    end
    if QuestFrameGreetingPanel_OnShow and not HookGossipFrame._greetingHooked then
        HookGossipFrame._greetingHooked = true
        hooksecurefunc("QuestFrameGreetingPanel_OnShow", FixQuestListTitleButtons)
    end
end

local function HookQuestLogFrame()
    if QuestLogFrame and not QuestLogFrame._FixChineseUIQuestLogHooked then
        QuestLogFrame._FixChineseUIQuestLogHooked = true
        QuestLogFrame:HookScript("OnShow", function()
            FixQuestButtons()
            FixQuestItemNames()
            FixQuestInfoBodyText()
            FixQuestRewardMoneyOverlap()
        end)
    end
    if QuestLogControlPanel and not QuestLogControlPanel._FixChineseUIQuestLogHooked then
        QuestLogControlPanel._FixChineseUIQuestLogHooked = true
        QuestLogControlPanel:HookScript("OnShow", FixQuestButtons)
    end
    if QuestLog_UpdateQuestDetails and not HookQuestLogFrame._detailsHooked then
        HookQuestLogFrame._detailsHooked = true
        hooksecurefunc("QuestLog_UpdateQuestDetails", function()
            FixQuestButtons()
            FixQuestItemNames()
            FixQuestInfoBodyText()
            FixQuestRewardMoneyOverlap()
        end)
    end
    if QuestLog_Update and not HookQuestLogFrame._updateHooked then
        HookQuestLogFrame._updateHooked = true
        hooksecurefunc("QuestLog_Update", FixQuestButtons)
    end
end

local function HookStaticPopups()
    if HookStaticPopups._hooked then
        return
    end
    HookStaticPopups._hooked = true
    if StaticPopup_Show then
        hooksecurefunc("StaticPopup_Show", FixStaticPopups)
    end
    if StaticPopup_Resize then
        hooksecurefunc("StaticPopup_Resize", function(dialog)
            FixStaticPopup(dialog)
        end)
    end
    -- DEATH rewrites "%d%s后释放灵魂" every tick. Height 0 leftover stacks 灵 on 魂.
    if StaticPopup_OnUpdate then
        hooksecurefunc("StaticPopup_OnUpdate", function(dialog)
            if dialog and dialog:IsShown() then
                FixStaticPopup(dialog)
            end
        end)
    end
end

-- TargetFrameTextureFrameName is GameFontNormalSmall in a 100x10 box.
-- Font is 15px; leftover width stacks the next two CJK glyphs (碎 on 尸).
local function FitUnitNamePlate(fs, justify)
    if not fs then
        return
    end
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    if fs.SetNonSpaceWrap then
        fs:SetNonSpaceWrap(true)
    end
    fs:SetJustifyH(justify or "CENTER")
    local width = fs:GetWidth()
    if not width or width < 80 then
        width = 100
    elseif width > 140 then
        width = 140
    end
    fs:SetWidth(width)
    fs:SetHeight(16)
end

local function FixUnitFrameTexts()
    FitUnitNamePlate(TargetFrameTextureFrameName, "CENTER")
    FitUnitNamePlate(FocusFrameTextureFrameName, "CENTER")
    FitUnitNamePlate(TargetFrameToTTextureFrameName, "LEFT")
    FitUnitNamePlate(FocusFrameToTTextureFrameName, "LEFT")

    if EUF_PlayerFrameHP and EUF_PlayerFrameHPPercent then
        EUF_PlayerFrameHP:ClearAllPoints()
        EUF_PlayerFrameHP:SetPoint("LEFT", PlayerFrameHealthBar, "RIGHT", 8, 0)
        EUF_PlayerFrameHP:SetJustifyH("LEFT")

        EUF_PlayerFrameHPPercent:ClearAllPoints()
        EUF_PlayerFrameHPPercent:SetPoint("LEFT", EUF_PlayerFrameHP, "RIGHT", 8, 0)
        EUF_PlayerFrameHPPercent:SetJustifyH("LEFT")
    end

    if EUF_PlayerFrameMP then
        EUF_PlayerFrameMP:ClearAllPoints()
        EUF_PlayerFrameMP:SetPoint("LEFT", PlayerFrameManaBar, "RIGHT", 8, 0)
        EUF_PlayerFrameMP:SetJustifyH("LEFT")
    end

    -- Target numbers stay on the target bars; percent sits by the portrait.
    if EUF_TargetFrameHP then
        EUF_TargetFrameHP:ClearAllPoints()
        EUF_TargetFrameHP:SetPoint("CENTER", TargetFrameHealthBar, "CENTER", 0, 0)
    end
    if EUF_TargetFrameMP then
        EUF_TargetFrameMP:ClearAllPoints()
        EUF_TargetFrameMP:SetPoint("CENTER", TargetFrameManaBar, "CENTER", 0, 0)
    end
    if EUF_TargetFrameHPPercent then
        EUF_TargetFrameHPPercent:ClearAllPoints()
        EUF_TargetFrameHPPercent:SetPoint("LEFT", TargetFrameHealthBar, "RIGHT", 4, 0)
    end

    if EUF_HideBlizzardStatusTexts then
        EUF_HideBlizzardStatusTexts()
    end
end

local function FindChildByName(parent, name)
    if not parent then
        return nil
    end
    local children = { parent:GetChildren() }
    for _, child in ipairs(children) do
        if child:GetName() == name then
            return child
        end
    end
    return _G[name]
end

-- Accountant Classic ("收支统计") + MerchantAssist ("交易助手<<") both sit at y=-45,
-- right on top of the first merchant item row, so their labels stack on item names
-- like "棕马缰绳". Move them into the title strip and keep auto-buy "+" on the icon.
local function FixMerchantOverlays()
    if not MerchantFrame then
        return
    end

    local acc = FindChildByName(MerchantFrame, "AccMerchantToggleButton")
    if acc then
        acc:ClearAllPoints()
        acc:SetPoint("TOPLEFT", MerchantFrame, "TOPLEFT", 70, -28)
        acc:SetWidth(100)
        acc:SetHeight(22)
        acc:SetFrameLevel((MerchantFrame:GetFrameLevel() or 1) + 5)
        FitOneLineButtonLabel(acc)
    end

    local assist = FindChildByName(MerchantFrame, "MerchantShowFrameButton")
    if assist then
        assist:ClearAllPoints()
        assist:SetPoint("TOPRIGHT", MerchantFrame, "TOPRIGHT", -40, -28)
        assist:SetWidth(90)
        assist:SetHeight(20)
        assist:SetFrameLevel((MerchantFrame:GetFrameLevel() or 1) + 5)
        FitOneLineButtonLabel(assist)
    end

    for i = 1, 12 do
        local btn = _G["MerchantAssistAutoBuyAddItemButton" .. i]
        local itemBtn = _G["MerchantItem" .. i .. "ItemButton"]
        if btn and itemBtn then
            btn:ClearAllPoints()
            btn:SetPoint("TOPRIGHT", itemBtn, "TOPRIGHT", 4, 4)
            btn:SetWidth(16)
            btn:SetHeight(16)
            btn:SetFrameLevel((itemBtn:GetFrameLevel() or 1) + 2)
        end

        -- Name box is 100x30, GameFontNormalSmall is 15px. CJK wraps per glyph, so a
        -- two-character name like "铁锤" stacks if the box is shorter than the font.
        local name = _G["MerchantItem" .. i .. "Name"]
        if name then
            if name.SetNonSpaceWrap then
                name:SetNonSpaceWrap(true)
            end
            if name.SetShadowOffset then
                name:SetShadowOffset(0, 0)
            end
            name:SetJustifyH("LEFT")
            name:SetWidth(0)
            local width = TextWidth(name) + 6
            if width < 80 then
                width = 80
            elseif width > 120 then
                width = 120
            end
            FitFontStringHeight(name, width)
            if name:GetHeight() < 16 then
                name:SetHeight(16)
            end
        end
    end
end

local function HookMerchantFrame()
    if not MerchantFrame or MerchantFrame._FixChineseUIMerchantHooked then
        return
    end
    MerchantFrame._FixChineseUIMerchantHooked = true
    MerchantFrame:HookScript("OnShow", FixMerchantOverlays)
    if MerchantFrame_Update then
        hooksecurefunc("MerchantFrame_Update", FixMerchantOverlays)
    end
end

-- LootButtonTemplate name is GameFontNormal in a 93x38 box. CJK wraps per glyph;
-- leftover width stacks the next two (e.g. 鲜 on 的 in "新鲜的红苹果").
local function FixLootButtonNames()
    local n = LOOTFRAME_NUMBUTTONS or 4
    for i = 1, n do
        local name = _G["LootButton" .. i .. "Text"]
        if name and name:IsShown() then
            if name.SetNonSpaceWrap then
                name:SetNonSpaceWrap(true)
            end
            if name.SetShadowOffset then
                name:SetShadowOffset(0, 0)
            end
            name:SetJustifyH("LEFT")
            FitFontStringHeight(name, 110)
            if name:GetHeight() < 16 then
                name:SetHeight(16)
            elseif name:GetHeight() > 36 then
                name:SetHeight(36)
            end
        end
    end
end

local function HookLootFrame()
    if LootFrame and not LootFrame._FixChineseUILootHooked then
        LootFrame._FixChineseUILootHooked = true
        LootFrame:HookScript("OnShow", FixLootButtonNames)
    end
    if LootFrame_Update and not HookLootFrame._updateHooked then
        HookLootFrame._updateHooked = true
        hooksecurefunc("LootFrame_Update", FixLootButtonNames)
    end
    if LootFrame_UpdateButton and not HookLootFrame._buttonHooked then
        HookLootFrame._buttonHooked = true
        hooksecurefunc("LootFrame_UpdateButton", FixLootButtonNames)
    end
end

-- PaperDoll left/right columns reuse the same 6 rows for 基础属性 / 近战 / 远程 / 法术 / 防御.
-- Labels inherit GameFontNormalSmall (center, 15px, wraps between CJK glyphs) and have no
-- width. After a long label, leftover width + wrap stacks "敏" over "捷"; a short leftover
-- plus a later long label truncates "法术爆击" to "法...". Hardcoded offsets only match
-- one leftover width, so they break as soon as the dropdown changes.
local STAT_VALUE_MIN = 28
local STAT_VALUE_MAX = 48
local STAT_ROW_HEIGHT = 15

local function FixPaperDollStatRow(statFrame)
    if not statFrame then
        return
    end

    local name = statFrame:GetName()
    if not name then
        return
    end

    local label = _G[name .. "Label"]
    local stat = _G[name .. "Stat"]
    local text = _G[name .. "StatText"]
    if not label then
        return
    end

    if label.SetNonSpaceWrap then
        label:SetNonSpaceWrap(true)
    end
    label:SetJustifyH("LEFT")

    local valueWidth = STAT_VALUE_MIN
    if text then
        if text.SetNonSpaceWrap then
            text:SetNonSpaceWrap(true)
        end
        text:SetJustifyH("RIGHT")
        local width = TextWidth(text) + 6
        if width < STAT_VALUE_MIN then
            width = STAT_VALUE_MIN
        elseif width > STAT_VALUE_MAX then
            width = STAT_VALUE_MAX
        end
        valueWidth = width
    end

    if stat then
        stat:ClearAllPoints()
        stat:SetPoint("RIGHT", statFrame, "RIGHT", -2, 0)
        stat:SetWidth(valueWidth)
        stat:SetHeight(STAT_ROW_HEIGHT)
    end

    if text then
        text:ClearAllPoints()
        if stat then
            text:SetPoint("RIGHT", stat, "RIGHT", 0, 0)
            text:SetPoint("LEFT", stat, "LEFT", 0, 0)
        else
            text:SetPoint("RIGHT", statFrame, "RIGHT", -2, 0)
            text:SetWidth(valueWidth)
        end
    end

    label:ClearAllPoints()
    label:SetPoint("LEFT", statFrame, "LEFT", 4, 0)
    if stat then
        label:SetPoint("RIGHT", stat, "LEFT", -4, 0)
    else
        label:SetPoint("RIGHT", statFrame, "RIGHT", -(valueWidth + 6), 0)
    end
    label:SetHeight(STAT_ROW_HEIGHT)
end

local function FixPaperDollStatRows()
    local prefixes = { "PlayerStatFrameLeft", "PlayerStatFrameRight" }
    for i = 1, #prefixes do
        for row = 1, 6 do
            FixPaperDollStatRow(_G[prefixes[i] .. row])
        end
    end
end

local function HookPaperDollFrame()
    if not PaperDollFrame or PaperDollFrame._FixChineseUIPaperDollHooked then
        return
    end
    PaperDollFrame._FixChineseUIPaperDollHooked = true
    PaperDollFrame:HookScript("OnShow", FixPaperDollStatRows)
    if PaperDollFrame_UpdateStats then
        hooksecurefunc("PaperDollFrame_UpdateStats", FixPaperDollStatRows)
    end
    if UpdatePaperdollStats then
        hooksecurefunc("UpdatePaperdollStats", FixPaperDollStatRows)
    end
end

-- GearManagerDialog Save/Equip/Delete are 78x22 UIPanelButtons. ButtonText has
-- no size; leftover width is one CJK glyph, so 保 wraps onto the next line.
local GEAR_MANAGER_BUTTONS = {
    "GearManagerDialogSaveSet",
    "GearManagerDialogEquipSet",
    "GearManagerDialogDeleteSet",
    "GearManagerDialogPopupOkay",
    "GearManagerDialogPopupCancel",
}

function FitOneLineButtonLabel(button)
    if not button then
        return
    end
    local fs = button:GetFontString()
    if not fs then
        local name = button:GetName()
        if name then
            fs = _G[name .. "Text"]
        end
    end
    if not fs then
        return
    end
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    fs:SetJustifyH("CENTER")
    fs:SetWidth(0)
    local inner = button:GetWidth() - 8
    local textWidth = TextWidth(fs)
    if inner < textWidth + 2 then
        inner = textWidth + 2
    end
    if inner < 36 then
        inner = 36
    end
    fs:SetWidth(inner)
    fs:SetHeight(16)
end

local function FixGearManagerButtons()
    for i = 1, #GEAR_MANAGER_BUTTONS do
        FitOneLineButtonLabel(_G[GEAR_MANAGER_BUTTONS[i]])
    end
end

local function HookGearManagerDialog()
    if GearManagerDialog and not GearManagerDialog._FixChineseUIGearHooked then
        GearManagerDialog._FixChineseUIGearHooked = true
        GearManagerDialog:HookScript("OnShow", FixGearManagerButtons)
    end
    if GearManagerDialogPopup and not GearManagerDialogPopup._FixChineseUIGearHooked then
        GearManagerDialogPopup._FixChineseUIGearHooked = true
        GearManagerDialogPopup:HookScript("OnShow", FixGearManagerButtons)
    end
end

-- GuildMemberDetailFrame Remove / Group Invite are 96x22. ButtonText leftover
-- width is one CJK glyph, so 移除 wraps onto two lines.
local GUILD_DETAIL_BUTTONS = {
    "GuildMemberRemoveButton",
    "GuildMemberGroupInviteButton",
}

local function FixGuildMemberDetailButtons()
    for i = 1, #GUILD_DETAIL_BUTTONS do
        FitOneLineButtonLabel(_G[GUILD_DETAIL_BUTTONS[i]])
    end
end

local function HookGuildMemberDetailFrame()
    if GuildMemberDetailFrame and not GuildMemberDetailFrame._FixChineseUIGuildHooked then
        GuildMemberDetailFrame._FixChineseUIGuildHooked = true
        GuildMemberDetailFrame:HookScript("OnShow", FixGuildMemberDetailButtons)
    end
end

-- Browse/Bid/Auctions Name is GameFontNormal in a short row (167x32 / 18px).
-- CJK wraps per glyph; leftover height paints 入 on 口 ("入口之七").
local function FixAuctionNameFontString(name)
    if not name then
        return
    end
    if name.SetNonSpaceWrap then
        name:SetNonSpaceWrap(true)
    end
    name:SetJustifyH("LEFT")
    if name.SetShadowOffset then
        name:SetShadowOffset(0, 0)
    end
    local width = name:GetWidth()
    if not width or width < 40 then
        name:SetWidth(160)
    end
    local text = name:GetText() or ""
    if text:find("\n", 1, true) then
        name:SetHeight(32)
    else
        name:SetHeight(16)
    end
end

local function FixAuctionSearchNames()
    local browseN = NUM_BROWSE_TO_DISPLAY or 8
    for i = 1, browseN do
        FixAuctionNameFontString(_G["BrowseButton" .. i .. "Name"])
    end
    local bidN = NUM_BIDS_TO_DISPLAY or 9
    for i = 1, bidN do
        FixAuctionNameFontString(_G["BidButton" .. i .. "Name"])
    end
    local aucN = NUM_AUCTIONS_TO_DISPLAY or 9
    for i = 1, aucN do
        FixAuctionNameFontString(_G["AuctionsButton" .. i .. "Name"])
    end
    for i = 1, 16 do
        FixAuctionNameFontString(_G["BuyButton" .. i .. "DetailName"])
        FixAuctionNameFontString(_G["BuyButton" .. i .. "SummaryName"])
        FixAuctionNameFontString(_G["SellButton" .. i .. "Name"])
    end
    -- Sell-slot name is GameFontNormal in a 124x30 box. CJK leftover width
    -- stacks the last two glyphs (矿 on 石 in "银矿石").
    FixAuctionNameFontString(AuctionsItemButtonName)
    FixAuctionNameFontString(SellItemButtonName)
end

local auctionLayoutFrame = CreateFrame("Frame")
auctionLayoutFrame:Hide()
auctionLayoutFrame.elapsed = 0
auctionLayoutFrame:SetScript("OnUpdate", function(self, elapsed)
    self.elapsed = self.elapsed + elapsed
    FixAuctionSearchNames()
    if self.elapsed > 0.2 then
        self:Hide()
        self.elapsed = 0
    end
end)

local function ScheduleAuctionNameFix()
    auctionLayoutFrame.elapsed = 0
    auctionLayoutFrame:Show()
    FixAuctionSearchNames()
end

local function HookNamedFrameShow(frame)
    if not frame or frame._FixChineseUIAuctionHooked then
        return
    end
    frame._FixChineseUIAuctionHooked = true
    frame:HookScript("OnShow", ScheduleAuctionNameFix)
end

local auctionBlizzardHooked = false

local function HookAuctionLite()
    if not AuctionLite or AuctionLite._FixChineseUIAuctionHooked then
        return
    end
    AuctionLite._FixChineseUIAuctionHooked = true
    if AuctionLite.AuctionFrameBuy_Update then
        hooksecurefunc(AuctionLite, "AuctionFrameBuy_Update", ScheduleAuctionNameFix)
    end
    if AuctionLite.AuctionFrameBuy_UpdateDetail then
        hooksecurefunc(AuctionLite, "AuctionFrameBuy_UpdateDetail", ScheduleAuctionNameFix)
    end
    if AuctionLite.AuctionFrameBuy_UpdateSummary then
        hooksecurefunc(AuctionLite, "AuctionFrameBuy_UpdateSummary", ScheduleAuctionNameFix)
    end
    if AuctionLite.AuctionFrameSell_Update then
        hooksecurefunc(AuctionLite, "AuctionFrameSell_Update", ScheduleAuctionNameFix)
    end
end

local function HookAuctionFrame()
    HookNamedFrameShow(AuctionFrame)
    HookNamedFrameShow(AuctionFrameBrowse)
    HookNamedFrameShow(AuctionFrameBid)
    HookNamedFrameShow(AuctionFrameAuctions)
    HookNamedFrameShow(AuctionFrameBuy)
    HookNamedFrameShow(AuctionFrameSell)
    if not auctionBlizzardHooked and AuctionFrameBrowse_Update then
        auctionBlizzardHooked = true
        hooksecurefunc("AuctionFrameBrowse_Update", ScheduleAuctionNameFix)
        if AuctionFrameBid_Update then
            hooksecurefunc("AuctionFrameBid_Update", ScheduleAuctionNameFix)
        end
        if AuctionFrameAuctions_Update then
            hooksecurefunc("AuctionFrameAuctions_Update", ScheduleAuctionNameFix)
        end
        if AuctionSellItemButton_OnEvent then
            hooksecurefunc("AuctionSellItemButton_OnEvent", ScheduleAuctionNameFix)
        end
    end
    HookAuctionLite()
end

-- ClassTrainerSkillButtonTemplate ButtonText is 270x13, GameFontNormal is 15px.
-- CJK wraps into that short box and stacks the last two glyphs.
-- TradeSkillSkillName / ClassTrainerSkillName are 244x0 and do the same on the
-- selected recipe or trainer skill. Class trainers list 11 rows.
local function FixTradeSkillListButton(button)
    if not button or not button:IsShown() then
        return
    end
    local name = button:GetName()
    local fs = button:GetFontString()
    if not fs and name then
        fs = _G[name .. "Text"]
    end
    if not fs then
        return
    end
    if fs.SetNonSpaceWrap then
        fs:SetNonSpaceWrap(true)
    end
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    fs:SetJustifyH("LEFT")
    fs:SetWidth(270)
    fs:SetHeight(16)
end

local function FitSkillTitle(fs)
    if not fs then
        return
    end
    if fs.SetNonSpaceWrap then
        fs:SetNonSpaceWrap(true)
    end
    if fs.SetShadowOffset then
        fs:SetShadowOffset(0, 0)
    end
    fs:SetJustifyH("LEFT")
    fs:SetWidth(244)
    fs:SetHeight(16)
end

local function FixTradeSkillNames()
    local tradeRows = TRADE_SKILLS_DISPLAYED or 8
    for i = 1, tradeRows do
        FixTradeSkillListButton(_G["TradeSkillSkill" .. i])
    end
    for i = tradeRows + 1, 16 do
        local btn = _G["TradeSkillSkill" .. i]
        if not btn then
            break
        end
        FixTradeSkillListButton(btn)
    end

    for i = 1, 11 do
        FixTradeSkillListButton(_G["ClassTrainerSkill" .. i])
    end

    FitSkillTitle(TradeSkillSkillName)
    FitSkillTitle(ClassTrainerSkillName)

    if TradeSkillDescription and TradeSkillDescription:IsShown() then
        FitFontStringHeight(TradeSkillDescription, 290)
    end
    if ClassTrainerSkillDescription and ClassTrainerSkillDescription:IsShown() then
        FitFontStringHeight(ClassTrainerSkillDescription, 290)
    end

    for i = 1, 8 do
        FixQuestItemName(_G["TradeSkillReagent" .. i .. "Name"])
    end

    -- UICheckButtonTemplate Text has no size. Leftover width wraps 材料齐备.
    local makeable = TradeSkillFrameAvailableFilterCheckButtonText
    if makeable then
        if makeable.SetShadowOffset then
            makeable:SetShadowOffset(0, 0)
        end
        makeable:SetJustifyH("LEFT")
        makeable:SetWidth(0)
        local width = TextWidth(makeable) + 4
        if width < 56 then
            width = 56
        end
        makeable:SetWidth(width)
        makeable:SetHeight(16)
    end
end

local function HookTradeSkillFrame()
    if TradeSkillFrame and not TradeSkillFrame._FixChineseUITradeHooked then
        TradeSkillFrame._FixChineseUITradeHooked = true
        TradeSkillFrame:HookScript("OnShow", FixTradeSkillNames)
    end
    if TradeSkillFrame_Update and not HookTradeSkillFrame._updateHooked then
        HookTradeSkillFrame._updateHooked = true
        hooksecurefunc("TradeSkillFrame_Update", FixTradeSkillNames)
    end
    if TradeSkillFrame_SetSelection and not HookTradeSkillFrame._selHooked then
        HookTradeSkillFrame._selHooked = true
        hooksecurefunc("TradeSkillFrame_SetSelection", FixTradeSkillNames)
    end
    if ClassTrainerFrame and not ClassTrainerFrame._FixChineseUITradeHooked then
        ClassTrainerFrame._FixChineseUITradeHooked = true
        ClassTrainerFrame:HookScript("OnShow", FixTradeSkillNames)
    end
    if ClassTrainerFrame_Update and not HookTradeSkillFrame._trainerHooked then
        HookTradeSkillFrame._trainerHooked = true
        hooksecurefunc("ClassTrainerFrame_Update", FixTradeSkillNames)
    end
    if ClassTrainer_SetSelection and not HookTradeSkillFrame._trainerSelHooked then
        HookTradeSkillFrame._trainerSelHooked = true
        hooksecurefunc("ClassTrainer_SetSelection", FixTradeSkillNames)
    end
end

-- Chat lines wrap CJK as one word. A loot line that ends on an item name
-- ("皇家霜鬃束带") paints 束 on 带 instead of starting a new line.
local function FixChatFrameLines(frame)
    if not frame then
        return
    end
    if frame.SetNonSpaceWrap then
        frame:SetNonSpaceWrap(true)
    end
    local ok, regions = pcall(function()
        return { frame:GetRegions() }
    end)
    if not ok then
        return
    end
    for i = 1, #regions do
        local fs = regions[i]
        if fs and fs.GetObjectType and fs:GetObjectType() == "FontString" then
            if fs.SetNonSpaceWrap then
                fs:SetNonSpaceWrap(true)
            end
            if fs.SetShadowOffset then
                fs:SetShadowOffset(0, 0)
            end
        end
    end
end

local function FixChatFrames()
    local n = NUM_CHAT_WINDOWS or 10
    for i = 1, n do
        FixChatFrameLines(_G["ChatFrame" .. i])
    end
    FixChatFrameLines(UIErrorsFrame)
end

local chatLayoutFrame = CreateFrame("Frame")
chatLayoutFrame:Hide()
chatLayoutFrame.elapsed = 0
chatLayoutFrame:SetScript("OnUpdate", function(self, elapsed)
    self.elapsed = self.elapsed + elapsed
    FixChatFrames()
    if self.elapsed > 0.2 then
        self:Hide()
        self.elapsed = 0
    end
end)

local function ScheduleChatLineFix()
    chatLayoutFrame.elapsed = 0
    chatLayoutFrame:Show()
    FixChatFrames()
end

local function HookChatFrames()
    local n = NUM_CHAT_WINDOWS or 10
    for i = 1, n do
        local frame = _G["ChatFrame" .. i]
        if frame and not frame._FixChineseUIChatHooked then
            frame._FixChineseUIChatHooked = true
            hooksecurefunc(frame, "AddMessage", ScheduleChatLineFix)
        end
    end
    if UIErrorsFrame and not UIErrorsFrame._FixChineseUIChatHooked then
        UIErrorsFrame._FixChineseUIChatHooked = true
        hooksecurefunc(UIErrorsFrame, "AddMessage", ScheduleChatLineFix)
    end
end

-- ItemTextTitleText is 224x14. zhCN GameFontNormal is 15px, so a dense glyph
-- like "便" clips and stacks inside the title strip (e.g. "威利的?笺").
local function FixItemTextFrame()
    local title = ItemTextTitleText
    if not title then
        return
    end
    if title.SetNonSpaceWrap then
        title:SetNonSpaceWrap(true)
    end
    if title.SetShadowOffset then
        title:SetShadowOffset(0, 0)
    end
    title:SetJustifyH("CENTER")
    FitFontStringHeight(title, 240)
    if title:GetHeight() < 16 then
        title:SetHeight(16)
    end
end

local function HookItemTextFrame()
    if not ItemTextFrame or ItemTextFrame._FixChineseUIItemTextHooked then
        return
    end
    ItemTextFrame._FixChineseUIItemTextHooked = true
    ItemTextFrame:HookScript("OnShow", FixItemTextFrame)
    ItemTextFrame:HookScript("OnEvent", function(_, event)
        if event == "ITEM_TEXT_BEGIN" or event == "ITEM_TEXT_READY" then
            FixItemTextFrame()
        end
    end)
end

-- BarberShopUI is LoadOnDemand. Okay is 80x24 / "确定"; leftover ButtonText width
-- wraps the two glyphs into one cell and reads as "...".
local function FixBarberShopFrame()
    if BarberShopFrameOkayButton then
        local fs = BarberShopFrameOkayButton:GetFontString()
        local text = fs and fs:GetText()
        if not text or text == "" or text == "..." then
            BarberShopFrameOkayButton:SetText(OKAY)
        end
    end
    FixQuestButton(BarberShopFrameOkayButton)
    FixQuestButton(BarberShopFrameCancelButton)
    FixQuestButton(BarberShopFrameResetButton)

    for i = 1, 3 do
        local cat = _G["BarberShopFrameSelector" .. i .. "Category"]
        if cat then
            if cat.SetNonSpaceWrap then
                cat:SetNonSpaceWrap(true)
            end
            if cat.SetShadowOffset then
                cat:SetShadowOffset(0, 0)
            end
            cat:SetJustifyH("CENTER")
            FitFontStringHeight(cat, 100)
            if cat:GetHeight() < 16 then
                cat:SetHeight(16)
            end
        end
    end
end

local function HookBarberShopFrame()
    if not BarberShopFrame or BarberShopFrame._FixChineseUIBarberHooked then
        return
    end
    BarberShopFrame._FixChineseUIBarberHooked = true
    BarberShopFrame:HookScript("OnShow", FixBarberShopFrame)
    if BarberShop_UpdateCost then
        hooksecurefunc("BarberShop_UpdateCost", FixBarberShopFrame)
    end
end

if QuestInfo_ShowRewards then
    hooksecurefunc("QuestInfo_ShowRewards", function()
        FixQuestRewardMoneyOverlap()
        FixQuestInfoBodyText()
        FixQuestItemNames()
        FixQuestButtons()
    end)
end

if QuestInfo_Display then
    hooksecurefunc("QuestInfo_Display", function()
        FixQuestRewardMoneyOverlap()
        FixQuestInfoBodyText()
        FixQuestItemNames()
        FixQuestButtons()
        ScheduleQuestProgressLayout()
    end)
end

if QuestInfo_ShowDescriptionText then
    hooksecurefunc("QuestInfo_ShowDescriptionText", function()
        FixQuestInfoBodyText()
        ScheduleQuestProgressLayout()
    end)
end

if QuestInfo_ShowRewardText then
    hooksecurefunc("QuestInfo_ShowRewardText", function()
        FixQuestInfoBodyText()
        ScheduleQuestProgressLayout()
    end)
end

if QuestInfoFadingFrame_OnUpdate then
    hooksecurefunc("QuestInfoFadingFrame_OnUpdate", function(self)
        if not self.fading then
            ClearQuestDescriptionGradient()
        end
    end)
end

if QuestFrameProgressItems_Update then
    hooksecurefunc("QuestFrameProgressItems_Update", ScheduleQuestProgressLayout)
end

if TextStatusBar_UpdateTextString then
    hooksecurefunc("TextStatusBar_UpdateTextString", function()
        if EUF_HideBlizzardStatusTexts then
            EUF_HideBlizzardStatusTexts()
        end
    end)
end

if TargetFrame_Update then
    hooksecurefunc("TargetFrame_Update", FixUnitFrameTexts)
end
if UnitFrame_Update then
    hooksecurefunc("UnitFrame_Update", function(frame)
        if not frame or not frame.name then
            return
        end
        local name = frame.name:GetName()
        if name == "TargetFrameTextureFrameName" or name == "FocusFrameTextureFrameName" then
            FitUnitNamePlate(frame.name, "CENTER")
        elseif name == "TargetFrameToTTextureFrameName" or name == "FocusFrameToTTextureFrameName" then
            FitUnitNamePlate(frame.name, "LEFT")
        end
    end)
end

local WATCHED_ADDONS = {
    FriendsMenuXP = true,
    FixDropDownMenu = true,
    EN_UnitFrames = true,
    Duowan = true,
    DuowanChat = true,
    ACP = true,
    MerchantAssist = true,
    Accountant_Classic = true,
    Blizzard_BarberShopUI = true,
    Blizzard_AuctionUI = true,
    Fence = true,
    AuctionLite = true,
    Blizzard_BindingUI = true,
    Blizzard_MacroUI = true,
    Blizzard_TalentUI = true,
    Blizzard_GlyphUI = true,
    Blizzard_TradeSkillUI = true,
    Blizzard_TrainerUI = true,
    Blizzard_AchievementUI = true,
    Blizzard_InspectUI = true,
    Blizzard_Calendar_GUI = true,
    Blizzard_GuildBankUI = true,
    Blizzard_ItemSocketingUI = true,
    Blizzard_TokenUI = true,
    Blizzard_TimeManager = true,
}

local function ApplyFriendsMenuStrings()
    if not FriendsMenuXP_Buttons then
        return
    end
    if FriendsMenuXP_Buttons["PROMOTE"] then
        FriendsMenuXP_Buttons["PROMOTE"].text = FMXP_BUTTON_PROMOTE
    end
    if FriendsMenuXP_Buttons["LOOT_PROMOTE"] then
        FriendsMenuXP_Buttons["LOOT_PROMOTE"].text = FMXP_BUTTON_LOOT_PROMOTE
    end
    if FriendsMenuXP_Buttons["ACHIEVEMENTS"] then
        FriendsMenuXP_Buttons["ACHIEVEMENTS"].text = FMXP_BUTTON_ACHIEVEMENTS
    end
end

local function ApplyHooks()
    UIDROPDOWNMENU_BUTTON_HEIGHT = BUTTON_HEIGHT
    ApplyFriendsMenuStrings()
    HookFriendsMenuAddButton()
    HookFriendsMenu(FriendsMenuXP)
    HookFriendsMenu(FriendsMenuXPSecure)

    for level = 1, 3 do
        local listFrame = _G["DropDownList" .. level]
        if listFrame and not listFrame._FixDropDownMenuHooked then
            listFrame._FixDropDownMenuHooked = true
            listFrame:HookScript("OnShow", function(self)
                UIDROPDOWNMENU_BUTTON_HEIGHT = BUTTON_HEIGHT
                FixListFrameWidth(self)
                ScheduleDropDownFix()
            end)
        end
    end

    HookQuestFrame(QuestFrame)
    HookQuestFrame(QuestFrameDetailPanel)
    HookQuestFrame(QuestFrameRewardPanel)
    HookQuestFrame(QuestFrameProgressPanel)
    HookQuestFrame(QuestFrameGreetingPanel)
    HookGossipFrame()
    HookMerchantFrame()
    HookLootFrame()
    HookPaperDollFrame()
    HookGearManagerDialog()
    HookGuildMemberDetailFrame()
    HookAuctionFrame()
    HookTradeSkillFrame()
    HookChatFrames()
    HookGameMenuFrame()
    HookQuestLogFrame()
    HookItemTextFrame()
    HookBarberShopFrame()
    HookStaticPopups()
end

local function ApplyLayoutFixes()
    UIDROPDOWNMENU_BUTTON_HEIGHT = BUTTON_HEIGHT
    StripFontShadows()
    StripAllFontStringShadows()
    ResizeAllDropDownLists()
    FixItemTextFrame()
    FixBarberShopFrame()
    FixQuestButtons()
    FixStaticPopups()
    FixUnitFrameTexts()
    FixMerchantOverlays()
    FixLootButtonNames()
    FixPaperDollStatRows()
    FixGearManagerButtons()
    FixGuildMemberDetailButtons()
    FixAuctionSearchNames()
    FixTradeSkillNames()
    FixChatFrames()
    FixQuestInfoBodyText()
    FixQuestItemNames()
    FixGameMenuLayout()
    ScheduleQuestProgressLayout()
end

local fullFixFrame = CreateFrame("Frame")
fullFixFrame:Hide()
fullFixFrame.elapsed = 0
fullFixFrame.passes = 0
fullFixFrame:SetScript("OnUpdate", function(self, elapsed)
    self.elapsed = self.elapsed + elapsed
    local due = false
    if self.passes == 0 then
        due = true
    elseif self.passes == 1 and self.elapsed >= 0.15 then
        due = true
    elseif self.passes == 2 and self.elapsed >= 0.6 then
        due = true
    elseif self.passes == 3 and self.elapsed >= 1.5 then
        due = true
    end
    if not due then
        return
    end
    self.passes = self.passes + 1
    ApplyLayoutFixes()
    if self.passes >= 4 then
        self:Hide()
        self.elapsed = 0
        self.passes = 0
    end
end)

local function ScheduleFullFix()
    StripFontShadows()
    fullFixFrame.elapsed = 0
    fullFixFrame.passes = 0
    fullFixFrame:Show()
end

if UIParent and not UIParent._FixDropDownMenuScaleHooked then
    UIParent._FixDropDownMenuScaleHooked = true
    UIParent:HookScript("OnSizeChanged", function()
        if fullFixFrame:IsShown() then
            return
        end
        ScheduleFullFix()
    end)
end

local enteredWorld = false
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_LOGIN")
f:RegisterEvent("ADDON_LOADED")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:RegisterEvent("PLAYER_TARGET_CHANGED")
f:RegisterEvent("PLAYER_FOCUS_CHANGED")
f:RegisterEvent("PLAYER_DEAD")
f:RegisterEvent("PLAYER_ALIVE")
f:RegisterEvent("DISPLAY_SIZE_CHANGED")
f:RegisterEvent("CHAT_MSG_LOOT")
f:SetScript("OnEvent", function(_, event, addon)
    if event == "PLAYER_TARGET_CHANGED" or event == "PLAYER_FOCUS_CHANGED" then
        FixUnitFrameTexts()
        return
    end

    if event == "PLAYER_DEAD" or event == "PLAYER_ALIVE" then
        FixStaticPopups()
        return
    end

    if event == "CHAT_MSG_LOOT" then
        ScheduleChatLineFix()
        return
    end

    if event == "DISPLAY_SIZE_CHANGED" then
        ScheduleFullFix()
        return
    end

    if event == "PLAYER_ENTERING_WORLD" then
        ApplyHooks()
        if not enteredWorld then
            enteredWorld = true
            ScheduleFullFix()
        else
            StripFontShadows()
        end
        return
    end

    if event == "ADDON_LOADED" then
        StripFontShadows()
        if not WATCHED_ADDONS[addon] then
            return
        end
    end

    ApplyHooks()
    ScheduleFullFix()
end)
