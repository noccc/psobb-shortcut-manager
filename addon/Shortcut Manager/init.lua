local core_mainmenu = require("core_mainmenu")
local ffi = require("ffi")

ffi.cdef[[
unsigned long SM_GetButtonMask(void);
unsigned long SM_GetDPadMask(void);
unsigned long SM_GetAxisMask(void);
int SM_GetStatus(void);
void SM_SetCaptureEnabled(int enabled);
void SM_ReloadConfig(void);
]]

local native = ffi.load("dinput8")
local optionsPath = "addons/Shortcut Manager/shortcuts.ini"

local windowOpen = false
local captureMode = false
local captureStart = 0
-- Chords are captured in the actual press order, not numeric button order.
-- In particular, holding a trigger first and pressing A/B/X/Y next must
-- display and store "trigger + face button" rather than sorting by index.
local capturePrevious = {}
local captureOrder = {}
local captureSeen = {}
local captureStableSince = 0
local captureReady = false
local captureLastSignature = ""
local rules = {}
local shortcutEnabled = true

local keyChoices = {
    {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06},
    {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B},
    {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E},
    {"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42},
    {"F9", 0x43}, {"F10", 0x44}, {"F11", 0x57}, {"F12", 0x58},
}

-- Explicit Shift+Fn target selection. Existing INI format stays compatible:
-- Key=<DirectInput scan code>, Shift=1.
local shiftFunctionKeys = {
    {"Shift+F1", 0x3B}, {"Shift+F2", 0x3C}, {"Shift+F3", 0x3D},
    {"Shift+F4", 0x3E}, {"Shift+F5", 0x3F}, {"Shift+F6", 0x40},
    {"Shift+F7", 0x41}, {"Shift+F8", 0x42}, {"Shift+F9", 0x43},
    {"Shift+F10", 0x44}, {"Shift+F11", 0x57}, {"Shift+F12", 0x58},
}

local function nextShiftFunctionKey(rule)
    local pos = 0
    if rule.shift and not rule.ctrl and not rule.alt then
        for i, entry in ipairs(shiftFunctionKeys) do
            if entry[2] == rule.key then
                pos = i
                break
            end
        end
    end
    local nextPos = (pos % #shiftFunctionKeys) + 1
    rule.key = shiftFunctionKeys[nextPos][2]
    rule.shift = true
    rule.ctrl = false
    rule.alt = false
end

local function keyName(code)
    for _, k in ipairs(keyChoices) do
        if k[2] == code then return k[1] end
    end
    return string.format("DIK 0x%02X", code)
end

local function maskToButtons(mask, dpadMask, axisMask)
    local out = {}
    local m = tonumber(mask) or 0
    local d = tonumber(dpadMask) or 0

    for i = 0, 31 do
        local p = 2 ^ i
        if math.floor(m / p) % 2 == 1 then
            table.insert(out, i)
        end
    end

    -- Pseudo button IDs used by the native wrapper:
    -- 32=Up, 33=Right, 34=Down, 35=Left.
    for i = 0, 3 do
        local p = 2 ^ i
        if math.floor(d / p) % 2 == 1 then
            table.insert(out, 32 + i)
        end
    end

    local a = tonumber(axisMask) or 0
    for i = 0, 15 do
        local p = 2 ^ i
        if math.floor(a / p) % 2 == 1 then
            table.insert(out, 36 + i)
        end
    end
    return out
end

local function buttonsToString(t)
    local s = {}

    for _, b in ipairs(t) do
        table.insert(s, tostring(b))
    end

    return table.concat(s, ",")
end

local function buttonLabel(t)
    local s = {}
    local dpadNames = {
        [32] = "D-Pad Up",
        [33] = "D-Pad Right",
        [34] = "D-Pad Down",
        [35] = "D-Pad Left",
    }

    for _, b in ipairs(t) do
        local name = dpadNames[b]
        if not name and b >= 36 and b < 52 then
            local axis = math.floor((b - 36) / 2) + 1
            local sign = (b % 2 == 0) and "-" or "+"
            name = "Axis " .. tostring(axis) .. sign
        end
        table.insert(s, name or ("Button " .. tostring(b + 1)))
    end

    return table.concat(s, " + ")
end

local function loadRules()
    rules = {}
    shortcutEnabled = true

    local f = io.open(optionsPath, "r")
    if not f then
        native.SM_SetCaptureEnabled(1)
        return
    end

    local current = nil

    for line in f:lines() do
        line = line:gsub("\r", "")

        local generalKey, generalValue =
            line:match("^([^=]+)=(.*)$")

        if not current and generalKey == "Enabled" then
            shortcutEnabled = tonumber(generalValue) ~= 0
        end

        local idx = line:match("^%[Shortcut(%d+)%]$")

        if idx then
            current = {
                buttons = {},
                key = 0x02,
                ctrl = false,
                shift = false,
                alt = false,
                consume = false,
            }

            table.insert(rules, current)
        elseif current then
            local k, v = line:match("^([^=]+)=(.*)$")

            if k == "Buttons" then
                current.buttons = {}

                for n in v:gmatch("[^,]+") do
                    table.insert(current.buttons, tonumber(n))
                end
            elseif k == "Key" then
                current.key = tonumber(v) or 0x02
            elseif k == "Ctrl" then
                current.ctrl = tonumber(v) == 1
            elseif k == "Shift" then
                current.shift = tonumber(v) == 1
            elseif k == "Alt" then
                current.alt = tonumber(v) == 1
            elseif k == "Consume" then
                current.consume = tonumber(v) ~= 0
            end
        end
    end

    f:close()
    native.SM_SetCaptureEnabled(shortcutEnabled and 1 or 0)
end

local function saveRules()
    local f = assert(io.open(optionsPath, "w"))

    f:write("[General]\n")
    f:write("Enabled=" .. (shortcutEnabled and "1" or "0") .. "\n")
    f:write("Count=" .. tostring(#rules) .. "\n\n")

    for i, r in ipairs(rules) do
        f:write("[Shortcut" .. tostring(i - 1) .. "]\n")
        f:write("Buttons=" .. buttonsToString(r.buttons) .. "\n")
        f:write("Key=" .. tostring(r.key) .. "\n")
        f:write("Ctrl=" .. (r.ctrl and "1" or "0") .. "\n")
        f:write("Shift=" .. (r.shift and "1" or "0") .. "\n")
        f:write("Alt=" .. (r.alt and "1" or "0") .. "\n")
        f:write("Consume=" .. (r.consume and "1" or "0") .. "\n\n")
    end

    f:close()

    native.SM_ReloadConfig()
    native.SM_SetCaptureEnabled(shortcutEnabled and 1 or 0)
end

local function addDefaultEight()
    -- Controller button indices are device-specific. Fake presets silently
    -- map face buttons as triggers and result in broken shortcuts.
    -- Keep existing bindings; prompt the user to capture each chord instead.
end

local function draw()
    if not windowOpen then return end

    local status = native.SM_GetStatus()

    imgui.Begin("Shortcut Manager")

    imgui.Text(
        "Native wrapper: " ..
        ((bit.band(status, 1) ~= 0) and "OK" or "waiting")
    )

    imgui.Text(
        "Keyboard: " ..
        ((bit.band(status, 2) ~= 0) and "PASSTHROUGH" or "waiting")
    )

    imgui.Text(
        "Controller seen: " ..
        ((bit.band(status, 4) ~= 0) and "YES" or "NO")
    )

    imgui.Text(
        "Shortcuts: " ..
        (shortcutEnabled and "ON" or "OFF")
    )

    if imgui.Button(
        shortcutEnabled
            and "Disable shortcuts"
            or "Enable shortcuts"
    ) then
        shortcutEnabled = not shortcutEnabled
        saveRules()
    end

    local mask = native.SM_GetButtonMask()
    local dpadMask = native.SM_GetDPadMask()
    local axisMask = native.SM_GetAxisMask()

    imgui.Text("Inputs: " .. buttonLabel(maskToButtons(mask, dpadMask, axisMask)))
    imgui.Text("Axis +/- identifiers indicate analog inputs, including some triggers.")

    if imgui.Button("Add shortcut") then
        captureMode = true
        captureStart = pso.get_tick_count()
        capturePrevious = {}
        captureOrder = {}
        captureSeen = {}
        captureStableSince = captureStart
        captureReady = false
        captureLastSignature = ""
        native.SM_SetCaptureEnabled(0)
    end

    imgui.SameLine()

    imgui.Text("Capture LT/RT and A/B/X/Y individually (controller IDs vary).")

    if captureMode then
        imgui.Text("Release all inputs, then hold LT/RT FIRST + A/B/X/Y.")
        imgui.Text("If LT/RT appears as Axis N +/- it is an analog trigger.")

        local now = pso.get_tick_count()
        local held = maskToButtons(mask, dpadMask, axisMask)
        local heldSet = {}
        for _, b in ipairs(held) do heldSet[b] = true end

        -- Arm only when the controller is at rest. This avoids accidentally
        -- capturing the face button used to click the Add shortcut control.
        if not captureReady then
            if #held == 0 then
                captureReady = true
                capturePrevious = {}
                captureOrder = {}
                captureSeen = {}
                captureLastSignature = ""
                captureStableSince = now
            else
                imgui.Text("Waiting for all buttons to be released...")
            end
        else
            -- Store rising edges in chronological order; never re-sort by
            -- their numeric DirectInput button IDs.
            for _, b in ipairs(held) do
                if not capturePrevious[b] and not captureSeen[b] then
                    table.insert(captureOrder, b)
                    captureSeen[b] = true
                end
            end

            -- Restart after full release without a completed chord.
            if #held == 0 then
                captureOrder = {}
                captureSeen = {}
                captureLastSignature = ""
                captureStableSince = now
            else
                local active = {}
                for _, b in ipairs(captureOrder) do
                    if heldSet[b] then table.insert(active, b) end
                end
                local signature = table.concat(active, ",")
                if signature ~= captureLastSignature then
                    captureStableSince = now
                    captureLastSignature = signature
                end

                imgui.Text("Detected (press order): " .. buttonLabel(active))
                -- Wait until BOTH buttons have remained down, instead of
                -- saving as soon as the second face button appears.
                if #active >= 2 and now - captureStableSince >= 180 then
                    table.insert(rules, {
                        buttons = active,
                        key = 0x02,
                        ctrl = false,
                        shift = false,
                        alt = false,
                        consume = true,
                    })
                    captureMode = false
                    native.SM_SetCaptureEnabled(shortcutEnabled and 1 or 0)
                    saveRules()
                end
            end
            capturePrevious = heldSet
        end

        if imgui.Button("Cancel capture") then
            captureMode = false
            native.SM_SetCaptureEnabled(shortcutEnabled and 1 or 0)
        end
    end

    imgui.Separator()

    local remove = nil

    for i, r in ipairs(rules) do
        imgui.Text(
            buttonLabel(r.buttons) ..
            "  ->  " ..
            (r.ctrl and "Ctrl+" or "") ..
            (r.shift and "Shift+" or "") ..
            (r.alt and "Alt+" or "") ..
            keyName(r.key)
        )

        if imgui.Button("Next key##" .. tostring(i)) then
            local pos = 1

            for j, k in ipairs(keyChoices) do
                if k[2] == r.key then
                    pos = j
                    break
                end
            end

            pos = pos + 1

            if pos > #keyChoices then
                pos = 1
            end

            r.key = keyChoices[pos][2]
            saveRules()
        end

        imgui.SameLine()

        if imgui.Button("Next Shift+F1-F12##" .. tostring(i)) then
            nextShiftFunctionKey(r)
            saveRules()
        end

        if imgui.Button(
            (r.ctrl and "[x]" or "[ ]") ..
            " Ctrl##" .. tostring(i)
        ) then
            r.ctrl = not r.ctrl
            saveRules()
        end

        imgui.SameLine()

        if imgui.Button(
            (r.shift and "[x]" or "[ ]") ..
            " Shift##" .. tostring(i)
        ) then
            r.shift = not r.shift
            saveRules()
        end

        imgui.SameLine()

        if imgui.Button(
            (r.alt and "[x]" or "[ ]") ..
            " Alt##" .. tostring(i)
        ) then
            r.alt = not r.alt
            saveRules()
        end

        imgui.SameLine()

        if imgui.Button(
            (r.consume and "[x]" or "[ ]") ..
            " Consume##" .. tostring(i)
        ) then
            r.consume = not r.consume
            saveRules()
        end

        imgui.SameLine()

        if imgui.Button("Delete##" .. tostring(i)) then
            remove = i
        end

        imgui.Separator()
    end

    if remove then
        table.remove(rules, remove)
        saveRules()
    end

    imgui.End()
end

local function init()
    core_mainmenu.add_button("Shortcut Manager", function()
        windowOpen = not windowOpen
    end)

    loadRules()

    return {
        name = "Shortcut Manager",
        version = "0.5.4",
        author = "OpenAI",
        description = "DirectInput controller shortcuts (including Shift+F1-F12); no virtual keyboard.",
        present = draw
    }
end

return {
    __addon = {
        init = init
    }
}
