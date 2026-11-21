# PSOBB Shortcut Manager 0.5.4 — Face-button and analog trigger diagnostics

**Source package; Windows build and in-game behavior not verified.** The virtual keyboard has not been reintroduced. There is no `d3d9.dll` in this archive.

## Fixes
- Newly captured chords default to **Consume ON**. Version 0.5.3 created them with Consume OFF, so the game received the unmodified A/B/X/Y action even when the chord was matched. Previously saved chords retain their settings; manually enable Consume for those rules, or delete and recapture them.
- Analog DirectInput axes are exposed in capture as **Axis 1- to Axis 8+** (IDs 36–51). This allows triggers which are not reported as digital buttons to be captured. Detection is relative to the axis resting value and uses 25% of its DirectInput-reported range where available. Capture with triggers released at game startup; start capture only when all inputs are idle.
- Removed hardcoded L/R+A/B/X/Y preset, since controller button numbering varies by hardware. Capture each real combo from your controller.
- Keeps DPAD and Shift+F1 through Shift+F12 support and existing INI file format.

## Test procedure
1. Keep the incompatible **d3d9.dll** disabled, and back up the current `dinput8.dll` and `addons/Shortcut Manager/shortcuts.ini` before installing a new build.
2. Build using `.github/workflows/build.yml` on GitHub Actions; **not compiled here**.
3. In the addon window, verify that **Inputs** displays a change when LT/RT is pressed alone. It may display `Button N` or `Axis N+/-`. If no change appears, this particular controller/driver path isn't being captured.
4. Delete/recreate the LT+A chord, pressing LT first, then A. The rule should have **Consume [x]**.
5. Test LT+A in a safe location. If A still triggers normally while the expected keyboard shortcut also works, PSOBB may be reading it through another input path (such as buffered DirectInput or XInput), which this wrapper does not intercept. Report whether the target keyboard action fired and what Inputs displayed.

## Limitations
- DirectInput axis ranges and order vary per controller. A trigger that is not visible as a button or axis in the Inputs diagnostic cannot be captured by this build.
- Consumption only modifies controller bytes in **GetDeviceState**, and only after the whole combo is down. It cannot retroactively suppress a face button PSOBB has already received, or suppress input read through another API.
- A physically held trigger during first axis sample becomes the baseline; release the triggers before starting the game.
- The native shortcut output still uses `SendInput`. Some PSOBB configurations may not respond to that output. The in-game behavior and Windows compilation have not been verified.
