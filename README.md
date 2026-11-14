# PSOBB Shortcut Manager v0.5.1 — Native Virtual Keyboard (source)

**Standalone integrated virtual keyboard**, built into Soly's existing `dinput8.dll` DirectInput wrapper + Lua/ImGui addon. No XInput requirement, no `d3d9.dll`, no external virtual keyboard installation. This ZIP provides build sources, not precompiled native binaries.

## Features
- Controller shortcuts and Shift+F1…F12 from v0.4.4.
- New on-screen keyboard with digits, QWERTY letters, punctuation, arrows, function keys F1–F12, Tab, Enter, Backspace, Escape, Space, Shift.
- Toggle via controller buttons 7+8 (internal indices 6+7), or click **Open virtual keyboard** in the Shortcut Manager addon menu.
- Reassign the toggle by clicking **Capture VK toggle** and holding exactly two controller buttons. The selection is saved to `addons/Shortcut Manager/virtual_keyboard.ini`.
- While the keyboard is open, controller buttons and D-Pad used for navigation are hidden from PSOBB; shortcut triggers are suspended. Physical keyboard input passes through.
- Navigate: D-Pad; confirm: controller button 1 (index 0); Backspace: button 2; Shift: button 3; close: button 4. All button numbers are the actual DirectInput order, not XInput labels.
- Keypresses are merged into the 256-byte DirectInput keyboard state read by the client, rather than relying solely on Windows SendInput.

## Build and install
1. Put the extracted source at the root of your GitHub repository.
2. Run **Actions → Build PSOBB Shortcut Manager → Run workflow**.
3. Download artifact `PSOBB-Shortcut-Manager-Native-VK-v0.5.1`.
4. Back up existing Ephinea `dinput8.dll`, `lua51.dll` and `addons/Shortcut Manager` before extracting the artifact into the game folder. **Do not install the old virtual keyboard `d3d9.dll`/`mod/` alongside this build** to avoid input conflicts.

## Engineering notes / known limits
- Uses the same Soly `dinput8.dll` build patch as v0.4.4. Game must create DirectInput8A controller and standard 256-byte DI keyboard devices through this wrapper. Controllers only exposed through XInput will not be seen when XInput is disabled: use a controller with a DirectInput-capable driver.
- Only the standard 256-byte keyboard state is overlaid. Applications polling Raw Input, window messages, or nonstandard DirectInput layouts will not see those synthetic keys.
- Keys are pulsed for 110ms. Controller polling, addon rendering and client input must remain active. Scan-code navigation and international layouts may need refinement.
- The native build and actual Ephinea gameplay **have not been verified** here. Back up your files first.

## Fix v0.5.1 — ordered trigger + face-button capture
- The shortcut capture waits for all controller buttons to be released before arming.
- Hold L/R trigger first, then press A/B/X/Y. Rising edges are stored in press order rather than sorted by DirectInput index.
- The capture requires at least two buttons stable for 180 ms; premature capture from the UI button is avoided.
- Trigger + face-button runtime matching is still order-independent; only the capture/display order changes.
- Analog-only triggers that do not register as DirectInput buttons remain unsupported by this capture; they need axis mapping.
