local core_mainmenu = require("core_mainmenu")
local ffi = require("ffi")

ffi.cdef[[
unsigned long SM_GetButtonMask(void);
unsigned long SM_GetDPadMask(void);
int SM_GetStatus(void);
void SM_SetCaptureEnabled(int enabled);
void SM_ReloadConfig(void);
void SM_SetVirtualKeyboardActive(int active);
int SM_GetVirtualKeyboardActive(void);
]]

local native = ffi.load("dinput8")
local optionsPath = "addons/Shortcut Manager/shortcuts.ini"

local windowOpen = false
local captureMode = false
local captureStart = 0
local rules = {}
local shortcutEnabled = true
local manualVKMode = false

local keyChoices = {
    {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06},
    {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B},
    {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E},
    {"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42},
    {"F9", 0x43}, {"F10", 0x44}, {"F11", 0x57}, {"F12", 0x58},
}

local function keyName(code)
    for _, k in ipairs(keyChoices) do
        if k[2] == code then return k[1] end
    end
    return string.format("DIK 0x%02X", code)
end

local function maskToButtons(mask, dpadMask)
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
        table.insert(s, dpadNames[b] or ("Button " .. tostring(b + 1)))
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
    rules = {
        {buttons={0,1}, key=0x02, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={0,2}, key=0x03, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={0,3}, key=0x04, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={0,4}, key=0x05, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={1,1}, key=0x06, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={1,2}, key=0x07, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={1,3}, key=0x08, ctrl=false, shift=false, alt=false, consume=false},
        {buttons={1,4}, key=0x09, ctrl=false, shift=false, alt=false, consume=false},
    }

    saveRules()
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

    imgui.Text("Virtual Keyboard 0.2.2: " .. (native.SM_GetVirtualKeyboardActive() ~= 0 and "SHORTCUTS PAUSED" or "shortcuts permitted"))
    if imgui.Button(manualVKMode and "Resume shortcuts (VK closed)" or "Pause shortcuts (VK open)") then
        manualVKMode = not manualVKMode
        native.SM_SetVirtualKeyboardActive(manualVKMode and 1 or 0)
    end
    imgui.Text("Note: VK 0.2.2 has no exported visibility status; set this manually.")

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

    imgui.Text("Inputs: " .. buttonLabel(maskToButtons(mask, dpadMask)))

    if imgui.Button("Add shortcut") then
        captureMode = true
        captureStart = pso.get_tick_count()
        native.SM_SetCaptureEnabled(0)
    end

    imgui.SameLine()

    if imgui.Button("Preset L/R + A/B/X/Y") then
        addDefaultEight()
    end

    if captureMode then
        imgui.Text("Press the desired controller combination...")

        local buttons = maskToButtons(mask, dpadMask)

        if
            #buttons >= 2 and
            pso.get_tick_count() - captureStart > 250
        then
            table.insert(rules, {
                buttons = buttons,
                key = 0x02,
                ctrl = false,
                shift = false,
                alt = false,
                consume = false,
            })

            captureMode = false

            native.SM_SetCaptureEnabled(
                shortcutEnabled and 1 or 0
            )

            saveRules()
        end

        if imgui.Button("Cancel capture") then
            captureMode = false

            native.SM_SetCaptureEnabled(
                shortcutEnabled and 1 or 0
            )
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
    native.SM_SetVirtualKeyboardActive(0)

    return {
        name = "Shortcut Manager",
        version = "0.4.3",
        author = "OpenAI",
        description = "Controller-only chord shortcuts for PSOBB; keyboard passthrough is untouched.",
        present = draw
    }
end

return {
    __addon = {
        init = init
    }
}
