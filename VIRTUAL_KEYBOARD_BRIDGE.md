# Integration notes

Inspecting the user-supplied v0.2.2 archive found only PE32 DLL binaries, notably
`d3d9.dll`, `shared_utils.dll` and `mod/virtual_keyboard.dll`, plus dependencies.
`virtual_keyboard.dll` exports only `_LoadMod@4`; it has no documented exported
GetVisible/SetVisible API. Strings indicate an XInputGetState hook, DirectInput
hook and D3D9 EndScene hook; they do **not** establish a safe way to read visibility.

Shortcut Manager's Soly `dinput8.dll` exports:

- `SM_SetVirtualKeyboardActive(int)`
- `SM_GetVirtualKeyboardActive(void)`

The Lua addon now supplies a manual pause/resume control that calls the bridge.
The existing Set function suppresses synthetic shortcut keypresses and controller
consumption, and requires release/repress on resume.

**Future automatic integration requires VK source, an official callback, or a
verified public API.** Avoid memory offsets or blind binary patching, which are
fragile across versions.
