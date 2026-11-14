# PSOBB Shortcut Manager v0.4.3 + Virtual Keyboard v0.2.2

This is a combined **source/build package**. Virtual Keyboard is provided as the original
user-supplied compiled distribution. Shortcut Manager is source code and **must be compiled**
with GitHub Actions. The resulting GitHub Actions artifact contains both runtimes.

## Build
Upload the contents of this ZIP to your GitHub repository, then run
**Actions > Build PSOBB Shortcut Manager > Run workflow**. Download the artifact
`PSOBB-Shortcut-Manager-Virtual-Keyboard-BUNDLE-v0.4.3`.

## Install
Back up any existing `dinput8.dll`, `d3d9.dll`, `shared_utils.dll`, `lua51.dll`,
`mod/`, and `addons/Shortcut Manager` in your Ephinea folder. Extract the built
artifact into the Ephinea PSOBB game folder and preserve folder names.
Do not overwrite another installed `d3d9.dll` proxy without checking its dependencies.

The official Virtual Keyboard uses `d3d9.dll` and XInput/DirectInput hooks;
Shortcut Manager uses Soly's `dinput8.dll`. Two DLLs can load side by side,
but in-game coexistence is **unverified**. No official VK source/API was included
in the supplied v0.2.2 archive, only compiled DLLs. Its exported `_LoadMod@4`
function is not a public visibility callback.

## Usage / limitation
- Shortcut rules default to Consume=off; turn it off on older rules as needed.
- Shortcut Manager menu: **Pause shortcuts (VK open)** before interacting with the
  virtual keyboard, then **Resume shortcuts (VK closed)** afterwards.
- Native `SM_SetVirtualKeyboardActive()` and `SM_GetVirtualKeyboardActive()` are
  retained for future automatic integration with the VK source.
- No automatic visibility detection or in-game validation is claimed.
- This build does not patch or reverse-engineer the Virtual Keyboard DLL.

See `VIRTUAL_KEYBOARD_BRIDGE.md` for native API notes.
