# PSOBB Shortcut Manager v0.5.5 — DirectInput + XInput (experimental)

This source package adds **native XInput polling** while retaining the original DirectInput wrapper and existing `shortcuts.ini` format. **No virtual keyboard** and **no `d3d9.dll`** are included.

## What changed

- Dynamically discovers `XInputGetState` from `xinput1_4.dll`, `xinput1_3.dll`, or `xinput9_1_0.dll`. No static XInput library linking required.
- Polls the **first connected XInput controller** every addon frame, even while the configuration window is closed; displays its index (1–4).
- Introduces XInput-specific capture IDs **52–67** (distinct from existing DirectInput IDs 0–51).
- Reads **LT and RT independently** using the XInput analog trigger values with a threshold of 42/255, and reads A/B/X/Y and D-Pad directly.
- Adds a **Capture source: DirectInput / XInput** selector in the addon window. Select XInput before recording XInput combinations; default remains DirectInput for backward compatibility.
- Existing DirectInput rules, Shift+F1–F12 and other modifier output remain intact. XInput bindings and DirectInput bindings can coexist in `shortcuts.ini`.

## IMPORTANT limitation: button consumption

XInput is **read-only** in this build. `Consume` in an XInput rule **does not intercept or block** A/B/X/Y as seen by the PSOBB client when the game reads XInput directly. The target keyboard shortcut may fire, but the game's native A/B/X/Y action may ALSO fire. Unlike the DirectInput wrapper, this build does not hook the game's `XInputGetState` calls. This is a deliberately scoped diagnostic XInput implementation; **do not assume the face-button conflict is solved**. XInput must not be forcibly enabled in the Ephinea launcher to use this addon, but the controller/driver must expose a functioning XInput device.

The output still relies on Windows `SendInput`, which may not be recognized by some PSOBB configurations. Testing in-game is required.

## Build / install

1. Back up the Ephinea `dinput8.dll` and `addons/Shortcut Manager/shortcuts.ini`.
2. **Keep the old third-party `d3d9.dll` disabled** since it prevented PSOBB from launching in your prior test.
3. Upload the source folder to a GitHub repository; run **Actions → Build PSOBB Shortcut Manager**.
4. After a successful CI build, install its artifact as described in the workflow output. Do not replace the original addon loader with an unrelated `dinput8.dll`.
5. The shipped ZIP is **source only**. Neither a Windows compilation nor an Ephinea runtime test has been performed here.

## Test steps

1. Launch PSOBB with `d3d9.dll` disabled. Open the Shortcut Manager addon window.
2. Check **XInput: connected, pad 1** (or another index). If not detected, check your controller connection/driver.
3. Click **Capture source: DirectInput** to select **XInput**.
4. Click **Add shortcut**, release all buttons, press **LT** first, then **A**; wait for stable capture.
5. Choose target `F1` or `Shift+F1`, then repeat with `RT+B` if desired.
6. Test whether the assigned keyboard action fires **and separately** whether PSOBB still performs the normal A/B action. Report both results.
7. To record an existing DirectInput combination, switch capture source back to DirectInput.

## XInput IDs

| ID | Key | ID | Key |
|---|---|---|---|
| 52 | A | 53 | B |
| 54 | X | 55 | Y |
| 56 | LB | 57 | RB |
| 58 | Back | 59 | Start |
| 60 | L3 | 61 | R3 |
| 62 | LT | 63 | RT |
| 64 | D-Pad Up | 65 | D-Pad Right |
| 66 | D-Pad Down | 67 | D-Pad Left |

**Known limitations:** Multi-controller selection is not yet configurable (first connected controller is used); UI reads XInput once per drawn frame; `Consume` is not supported for native XInput; no Windows/game runtime verification.
