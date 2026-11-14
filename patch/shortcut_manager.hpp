#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <windows.h>
#include <dinput.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>

namespace sm {

struct Rule {
    std::vector<int> buttons;
    BYTE key = 0; // DIK / scan code
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool consume = true;

    bool latched = false;
    bool syntheticDown = false;
    DWORD releaseAt = 0;
};

static std::mutex gMutex;
static std::vector<Rule> gRules;

static std::atomic<unsigned long> gButtonMask(0);
static std::atomic<unsigned long> gDpadMask(0);
static std::atomic<int> gStatus(0);
static std::atomic<int> gCaptureEnabled(1);
// Set by the virtual keyboard bridge while its overlay owns controller input.
static std::atomic<int> gVirtualKeyboardActive(0);
// Independent DirectInput virtual keyboard. Pressed scan codes are merged into
// the game's DirectInput keyboard state, never into XInput.
static DWORD gVKScanExpiry[256] = {};
static DWORD gVKShiftExpiry = 0;
static std::atomic<unsigned long> gVKToggleMask((1UL << 6) | (1UL << 7));


static std::string module_dir() {
    HMODULE hm = nullptr;
    char path[MAX_PATH] = {};

    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&module_dir),
        &hm
    );

    if (!hm || !GetModuleFileNameA(hm, path, MAX_PATH)) {
        return ".";
    }

    std::string s(path);
    const size_t p = s.find_last_of("\\/");
    return (p == std::string::npos) ? "." : s.substr(0, p);
}

static std::string config_path() {
    return module_dir() + "\\addons\\Shortcut Manager\\shortcuts.ini";
}

static INPUT make_scan_input(WORD scan, bool keyUp) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
    return in;
}

static void send_scan(WORD scan, bool keyUp) {
    INPUT in = make_scan_input(scan, keyUp);
    SendInput(1, &in, sizeof(INPUT));
}

// Emit each chord as one ordered SendInput batch. The modifier key-down
// precedes the function-key down; releases are sent in reverse order.
// This is still SendInput injection (not DirectInput keyboard-state patching),
// and must be verified against the actual PSOBB client input path.
static bool send_rule_chord(const Rule& r, bool keyUp) {
    INPUT inputs[4] = {};
    UINT count = 0;
    auto append = [&](WORD scan) {
        inputs[count++] = make_scan_input(scan, keyUp);
    };

    if (!keyUp) {
        if (r.ctrl) append(DIK_LCONTROL);
        if (r.shift) append(DIK_LSHIFT);
        if (r.alt) append(DIK_LMENU);
        append(r.key);
    } else {
        append(r.key);
        if (r.alt) append(DIK_LMENU);
        if (r.shift) append(DIK_LSHIFT);
        if (r.ctrl) append(DIK_LCONTROL);
    }
    return SendInput(count, inputs, sizeof(INPUT)) == count;
}

static void press_rule(Rule& r) {
    if (r.syntheticDown || r.key == 0) return;
    if (!send_rule_chord(r, false)) return;
    r.syntheticDown = true;
    r.releaseAt = GetTickCount() + 100;
}

static void release_rule(Rule& r) {
    if (!r.syntheticDown) {
        r.releaseAt = 0;
        return;
    }
    send_rule_chord(r, true);
    r.syntheticDown = false;
    r.releaseAt = 0;
}

static void release_all_locked() {
    for (Rule& r : gRules) {
        release_rule(r);
    }
}

static void load_config_locked() {
    release_all_locked();
    gRules.clear();

    const std::string path = config_path();

    int count = GetPrivateProfileIntA("General", "Count", 0, path.c_str());
    if (count < 0) count = 0;
    if (count > 64) count = 64;

    for (int i = 0; i < count; ++i) {
        char section[32] = {};
        wsprintfA(section, "Shortcut%d", i);

        Rule r;

        char buttons[256] = {};
        GetPrivateProfileStringA(
            section, "Buttons", "",
            buttons, sizeof(buttons),
            path.c_str()
        );

        std::string bstr(buttons);
        size_t start = 0;

        while (start < bstr.size()) {
            const size_t comma = bstr.find(',', start);
            const std::string token = bstr.substr(
                start,
                comma == std::string::npos
                    ? std::string::npos
                    : comma - start
            );

            if (!token.empty()) {
                const int b = atoi(token.c_str());
                if (b >= 0 && b < 36) {
                    r.buttons.push_back(b);
                }
            }

            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        r.key = static_cast<BYTE>(
            GetPrivateProfileIntA(section, "Key", 0, path.c_str())
        );
        r.ctrl = GetPrivateProfileIntA(section, "Ctrl", 0, path.c_str()) != 0;
        r.shift = GetPrivateProfileIntA(section, "Shift", 0, path.c_str()) != 0;
        r.alt = GetPrivateProfileIntA(section, "Alt", 0, path.c_str()) != 0;
        r.consume = GetPrivateProfileIntA(section, "Consume", 0, path.c_str()) != 0;

        if (!r.buttons.empty() && r.key != 0) {
            gRules.push_back(r);
        }
    }
}

static void ensure_loaded() {
    static bool loaded = false;

    if (!loaded) {
        std::lock_guard<std::mutex> lock(gMutex);

        if (!loaded) {
            load_config_locked();
            loaded = true;
        }
    }
}

static bool combo_down(const Rule& r, unsigned long buttonMask, unsigned long dpadMask) {
    if (r.buttons.empty()) return false;

    for (int b : r.buttons) {
        if (b >= 0 && b < 32) {
            if ((buttonMask & (1UL << b)) == 0) return false;
        } else if (b >= 32 && b < 36) {
            const int d = b - 32;
            if ((dpadMask & (1UL << d)) == 0) return false;
        } else {
            return false;
        }
    }

    return true;
}

static unsigned long pov_to_dpad_mask(DWORD pov) {
    if ((pov & 0xFFFF) == 0xFFFF) return 0;

    const DWORD angle = pov % 36000;
    unsigned long mask = 0;

    // Bits: 0=Up, 1=Right, 2=Down, 3=Left.
    // Diagonals intentionally set both adjacent directions.
    if (angle >= 31500 || angle <= 4500)  mask |= (1UL << 0);
    if (angle >= 4500  && angle <= 13500) mask |= (1UL << 1);
    if (angle >= 13500 && angle <= 22500) mask |= (1UL << 2);
    if (angle >= 22500 && angle <= 31500) mask |= (1UL << 3);

    return mask;
}

// Pulse a DirectInput scan code for 110 ms; the game's next keyboard poll
// receives the key. Out-of-range values cannot write outside the state array.
extern "C" __declspec(dllexport) void __cdecl SM_VKPressKey(int scan, int shift) {
    if (scan <= 0 || scan >= 256 || !gVirtualKeyboardActive.load()) return;
    std::lock_guard<std::mutex> lock(gMutex);
    const DWORD expiry = GetTickCount() + 110;
    gVKScanExpiry[scan] = expiry;
    if (shift) gVKShiftExpiry = expiry;
}

extern "C" __declspec(dllexport) void __cdecl SM_SetVKToggleMask(unsigned long mask) {
    // Two distinct controller buttons are strongly recommended for toggle.
    gVKToggleMask.store(mask);
}

extern "C" __declspec(dllexport) unsigned long __cdecl SM_GetVKToggleMask() {
    return gVKToggleMask.load();
}

extern "C" __declspec(dllexport) unsigned long __cdecl SM_GetButtonMask() {
    return gButtonMask.load();
}

extern "C" __declspec(dllexport) unsigned long __cdecl SM_GetDPadMask() {
    return gDpadMask.load();
}

extern "C" __declspec(dllexport) int __cdecl SM_GetStatus() {
    return gStatus.load();
}

extern "C" __declspec(dllexport) void __cdecl SM_SetCaptureEnabled(int enabled) {
    const int newValue = enabled ? 1 : 0;
    gCaptureEnabled.store(newValue);

    if (!newValue) {
        std::lock_guard<std::mutex> lock(gMutex);
        release_all_locked();

        for (Rule& r : gRules) {
            r.latched = false;
        }
    }
}

// Public, process-local coexistence API for pso-bb-virtual-keyboard.
// 1 = overlay active: never synthesize shortcuts or consume controller state.
// 0 = overlay closed: normal shortcut behaviour resumes after release/repress.
extern "C" __declspec(dllexport) void __cdecl SM_SetVirtualKeyboardActive(int active) {
    const int value = active ? 1 : 0;
    if (gVirtualKeyboardActive.exchange(value) != value && value) {
        std::lock_guard<std::mutex> lock(gMutex);
        release_all_locked();
        for (Rule& r : gRules) r.latched = true;
    }
}

extern "C" __declspec(dllexport) int __cdecl SM_GetVirtualKeyboardActive() {
    return gVirtualKeyboardActive.load();
}

extern "C" __declspec(dllexport) void __cdecl SM_ReloadConfig() {
    std::lock_guard<std::mutex> lock(gMutex);
    load_config_locked();
}

#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:SM_VKPressKey=_SM_VKPressKey")
#pragma comment(linker, "/EXPORT:SM_SetVKToggleMask=_SM_SetVKToggleMask")
#pragma comment(linker, "/EXPORT:SM_GetVKToggleMask=_SM_GetVKToggleMask")
#pragma comment(linker, "/EXPORT:SM_GetButtonMask=_SM_GetButtonMask")
#pragma comment(linker, "/EXPORT:SM_GetDPadMask=_SM_GetDPadMask")
#pragma comment(linker, "/EXPORT:SM_GetStatus=_SM_GetStatus")
#pragma comment(linker, "/EXPORT:SM_SetCaptureEnabled=_SM_SetCaptureEnabled")
#pragma comment(linker, "/EXPORT:SM_ReloadConfig=_SM_ReloadConfig")
#pragma comment(linker, "/EXPORT:SM_SetVirtualKeyboardActive=_SM_SetVirtualKeyboardActive")
#pragma comment(linker, "/EXPORT:SM_GetVirtualKeyboardActive=_SM_GetVirtualKeyboardActive")
#endif

class ControllerDeviceWrapper : public IDirectInputDevice8A {
public:
    explicit ControllerDeviceWrapper(IDirectInputDevice8A* inner, bool keyboard = false)
        : inner_(inner), keyboard_(keyboard) {
        gStatus.fetch_or(1); // wrapper active
        if (keyboard_) gStatus.fetch_or(2);
        else gStatus.fetch_or(4); // controller seen
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppvObj) override {
        if (!ppvObj) return E_POINTER;

        if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8A) {
            *ppvObj = static_cast<IDirectInputDevice8A*>(this);
            AddRef();
            return S_OK;
        }

        return inner_->QueryInterface(riid, ppvObj);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;

        if (r == 0) {
            {
                std::lock_guard<std::mutex> lock(gMutex);
                release_all_locked();
            }

            inner_->Release();
            delete this;
        }

        return r;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS p) override {
        return inner_->GetCapabilities(p);
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(
        LPDIENUMDEVICEOBJECTSCALLBACKA cb,
        LPVOID ref,
        DWORD flags
    ) override {
        return inner_->EnumObjects(cb, ref, flags);
    }

    HRESULT STDMETHODCALLTYPE GetProperty(
        REFGUID g,
        LPDIPROPHEADER h
    ) override {
        return inner_->GetProperty(g, h);
    }

    HRESULT STDMETHODCALLTYPE SetProperty(
        REFGUID g,
        LPCDIPROPHEADER h
    ) override {
        return inner_->SetProperty(g, h);
    }

    HRESULT STDMETHODCALLTYPE Acquire() override {
        const HRESULT hr = inner_->Acquire();

        if (SUCCEEDED(hr)) {
            syncNextControllerState_ = true;

            std::lock_guard<std::mutex> lock(gMutex);
            release_all_locked();

            for (Rule& r : gRules) {
                r.latched = false;
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE Unacquire() override {
        syncNextControllerState_ = true;

        {
            std::lock_guard<std::mutex> lock(gMutex);
            release_all_locked();

            for (Rule& r : gRules) {
                r.latched = false;
            }
        }

        return inner_->Unacquire();
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(
        DWORD cbData,
        LPVOID lpvData
    ) override {
        const HRESULT hr = inner_->GetDeviceState(cbData, lpvData);

        if (FAILED(hr) || !lpvData) {
            return hr;
        }

        ensure_loaded();

        // Only standard DirectInput keyboard format (256 scan-code bytes) is
        // modified. Preserve the physical keyboard: virtual keys are OR-ed.
        if (keyboard_) {
            if (cbData >= 256) {
                std::lock_guard<std::mutex> lock(gMutex);
                BYTE* keys = static_cast<BYTE*>(lpvData);
                const DWORD now = GetTickCount();
                for (int i = 1; i < 256; ++i) {
                    if (gVKScanExpiry[i] && static_cast<LONG>(now - gVKScanExpiry[i]) < 0)
                        keys[i] |= 0x80;
                    else gVKScanExpiry[i] = 0;
                }
                if (gVKShiftExpiry && static_cast<LONG>(now - gVKShiftExpiry) < 0)
                    keys[DIK_LSHIFT] |= 0x80;
                else gVKShiftExpiry = 0;
            }
            return hr;
        }

        unsigned char* data = static_cast<unsigned char*>(lpvData);
        unsigned long mask = 0;

        for (size_t i = 0; i < buttonOffsets_.size() && i < 32; ++i) {
            const DWORD off = buttonOffsets_[i];

            if (off < cbData && (data[off] & 0x80)) {
                mask |= (1UL << static_cast<unsigned long>(i));
            }
        }

        unsigned long dpadMask = 0;
        for (DWORD off : povOffsets_) {
            if (off + sizeof(DWORD) <= cbData) {
                const DWORD pov = *reinterpret_cast<DWORD*>(data + off);
                dpadMask |= pov_to_dpad_mask(pov);
            }
        }

        gButtonMask.store(mask);
        gDpadMask.store(dpadMask);

        const DWORD now = GetTickCount();
        const bool foreground =
            gameWindow_ != nullptr &&
            GetForegroundWindow() == gameWindow_;

        // Hardware button chord for VK visibility: works with DirectInput
        // controllers regardless of XInput support or launcher settings.
        const unsigned long toggleMask = gVKToggleMask.load();
        const bool toggleDown = toggleMask && (mask & toggleMask) == toggleMask;
        if (!toggleDown) vkToggleLatched_ = false;
        if (foreground && toggleDown && !vkToggleLatched_ && !syncNextControllerState_) {
            vkToggleLatched_ = true;
            SM_SetVirtualKeyboardActive(!gVirtualKeyboardActive.load());
        }

        std::lock_guard<std::mutex> lock(gMutex);

        // Release synthetic keys after their short pulse.
        for (Rule& r : gRules) {
            if (
                r.syntheticDown &&
                static_cast<LONG>(now - r.releaseAt) >= 0
            ) {
                release_rule(r);
            }
        }

        // The virtual keyboard owns controller input while visible.
        // Prevent game actions from the navigation/selection inputs.
        if (gVirtualKeyboardActive.load()) {
            for (Rule& r : gRules) {
                if (r.syntheticDown) release_rule(r);
                r.latched = combo_down(r, mask, dpadMask);
            }
            for (DWORD off : buttonOffsets_) {
                if (off < cbData) data[off] = 0;
            }
            for (DWORD off : povOffsets_) {
                if (off + sizeof(DWORD) <= cbData) {
                    const DWORD centered = 0xFFFFFFFF;
                    memcpy(data + off, &centered, sizeof(centered));
                }
            }
            syncNextControllerState_ = true;
            return hr;
        }

        // First state after acquire/focus change = baseline only.
        if (syncNextControllerState_) {
            for (Rule& r : gRules) {
                r.latched = combo_down(r, mask, dpadMask);
            }

            syncNextControllerState_ = false;
            return hr;
        }

        for (Rule& r : gRules) {
            const bool down = combo_down(r, mask, dpadMask);

            if (!down) {
                r.latched = false;
            }

            if (
                gCaptureEnabled.load() &&
                foreground &&
                down &&
                !r.latched
            ) {
                press_rule(r);
                r.latched = true;
            }

            // Consumption affects controller bytes ONLY.
            // Keyboard state is never wrapped or modified.
            if (
                r.consume &&
                foreground &&
                down &&
                gCaptureEnabled.load()
            ) {
                bool consumePov = false;
                for (int b : r.buttons) {
                    if (b >= 0 && b < 32 && static_cast<size_t>(b) < buttonOffsets_.size()) {
                        const DWORD off = buttonOffsets_[b];
                        if (off < cbData) data[off] = 0;
                    } else if (b >= 32 && b < 36) {
                        consumePov = true;
                    }
                }

                if (consumePov) {
                    for (DWORD off : povOffsets_) {
                        if (off + sizeof(DWORD) <= cbData) {
                            *reinterpret_cast<DWORD*>(data + off) = 0xFFFFFFFF;
                        }
                    }
                }
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(
        DWORD a,
        LPDIDEVICEOBJECTDATA b,
        LPDWORD c,
        DWORD d
    ) override {
        return inner_->GetDeviceData(a, b, c, d);
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(
        LPCDIDATAFORMAT fmt
    ) override {
        const HRESULT hr = inner_->SetDataFormat(fmt);

        if (SUCCEEDED(hr) && fmt && fmt->rgodf && !keyboard_) {
            buttonOffsets_.clear();
            povOffsets_.clear();

            for (DWORD i = 0; i < fmt->dwNumObjs; ++i) {
                const DIOBJECTDATAFORMAT& o = fmt->rgodf[i];
                const DWORD t = DIDFT_GETTYPE(o.dwType);

                if ((t & DIDFT_BUTTON) != 0) {
                    buttonOffsets_.push_back(o.dwOfs);
                } else if ((t & DIDFT_POV) != 0) {
                    povOffsets_.push_back(o.dwOfs);
                }
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE h) override {
        return inner_->SetEventNotification(h);
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND h, DWORD d) override {
        gameWindow_ = h;
        return inner_->SetCooperativeLevel(h, d);
    }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(
        LPDIDEVICEOBJECTINSTANCEA a,
        DWORD b,
        DWORD c
    ) override {
        return inner_->GetObjectInfo(a, b, c);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(
        LPDIDEVICEINSTANCEA a
    ) override {
        return inner_->GetDeviceInfo(a);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD d) override {
        return inner_->RunControlPanel(h, d);
    }

    HRESULT STDMETHODCALLTYPE Initialize(
        HINSTANCE h,
        DWORD d,
        REFGUID g
    ) override {
        return inner_->Initialize(h, d, g);
    }

    HRESULT STDMETHODCALLTYPE CreateEffect(
        REFGUID g,
        LPCDIEFFECT e,
        LPDIRECTINPUTEFFECT* o,
        LPUNKNOWN u
    ) override {
        return inner_->CreateEffect(g, e, o, u);
    }

    HRESULT STDMETHODCALLTYPE EnumEffects(
        LPDIENUMEFFECTSCALLBACKA cb,
        LPVOID r,
        DWORD d
    ) override {
        return inner_->EnumEffects(cb, r, d);
    }

    HRESULT STDMETHODCALLTYPE GetEffectInfo(
        LPDIEFFECTINFOA i,
        REFGUID g
    ) override {
        return inner_->GetEffectInfo(i, g);
    }

    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD d) override {
        return inner_->GetForceFeedbackState(d);
    }

    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD d) override {
        return inner_->SendForceFeedbackCommand(d);
    }

    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(
        LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb,
        LPVOID r,
        DWORD d
    ) override {
        return inner_->EnumCreatedEffectObjects(cb, r, d);
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE e) override {
        return inner_->Escape(e);
    }

    HRESULT STDMETHODCALLTYPE Poll() override {
        return inner_->Poll();
    }

    HRESULT STDMETHODCALLTYPE SendDeviceData(
        DWORD a,
        LPCDIDEVICEOBJECTDATA b,
        LPDWORD c,
        DWORD d
    ) override {
        return inner_->SendDeviceData(a, b, c, d);
    }

    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(
        LPCSTR f,
        LPDIENUMEFFECTSINFILECALLBACK cb,
        LPVOID r,
        DWORD d
    ) override {
        return inner_->EnumEffectsInFile(f, cb, r, d);
    }

    HRESULT STDMETHODCALLTYPE WriteEffectToFile(
        LPCSTR f,
        DWORD a,
        LPDIFILEEFFECT b,
        DWORD c
    ) override {
        return inner_->WriteEffectToFile(f, a, b, c);
    }

    HRESULT STDMETHODCALLTYPE BuildActionMap(
        LPDIACTIONFORMATA a,
        LPCSTR n,
        DWORD d
    ) override {
        return inner_->BuildActionMap(a, n, d);
    }

    HRESULT STDMETHODCALLTYPE SetActionMap(
        LPDIACTIONFORMATA a,
        LPCSTR n,
        DWORD d
    ) override {
        return inner_->SetActionMap(a, n, d);
    }

    HRESULT STDMETHODCALLTYPE GetImageInfo(
        LPDIDEVICEIMAGEINFOHEADERA h
    ) override {
        return inner_->GetImageInfo(h);
    }

private:
    std::atomic<ULONG> refs_{1};
    IDirectInputDevice8A* inner_;
    bool keyboard_ = false;
    bool vkToggleLatched_ = false;
    HWND gameWindow_ = nullptr;
    bool syncNextControllerState_ = true;
    std::vector<DWORD> buttonOffsets_;
    std::vector<DWORD> povOffsets_;
};

class DirectInputWrapper : public IDirectInput8A {
public:
    explicit DirectInputWrapper(IDirectInput8A* inner)
        : inner_(inner) {
        gStatus.fetch_or(1);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        LPVOID* ppvObj
    ) override {
        if (!ppvObj) return E_POINTER;

        if (riid == IID_IUnknown || riid == IID_IDirectInput8A) {
            *ppvObj = static_cast<IDirectInput8A*>(this);
            AddRef();
            return S_OK;
        }

        return inner_->QueryInterface(riid, ppvObj);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refs_;

        if (r == 0) {
            inner_->Release();
            delete this;
        }

        return r;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        REFGUID rguid,
        LPDIRECTINPUTDEVICE8A* out,
        LPUNKNOWN unk
    ) override {
        const HRESULT hr = inner_->CreateDevice(rguid, out, unk);

        if (SUCCEEDED(hr) && out && *out) {
            const bool keyboard =
                IsEqualGUID(rguid, GUID_SysKeyboard) != FALSE;
            const bool mouse =
                IsEqualGUID(rguid, GUID_SysMouse) != FALSE;

            if (!mouse) {
                // Wrap keyboard only to OR virtual scan-code pulses onto the
                // original game state. All physical input stays untouched.
                *out = new ControllerDeviceWrapper(*out, keyboard);
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(
        DWORD a,
        LPDIENUMDEVICESCALLBACKA b,
        LPVOID c,
        DWORD d
    ) override {
        return inner_->EnumDevices(a, b, c, d);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g) override {
        return inner_->GetDeviceStatus(g);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD d) override {
        return inner_->RunControlPanel(h, d);
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD d) override {
        return inner_->Initialize(h, d);
    }

    HRESULT STDMETHODCALLTYPE FindDevice(
        REFGUID a,
        LPCSTR b,
        LPGUID c
    ) override {
        return inner_->FindDevice(a, b, c);
    }

    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
        LPCSTR u,
        LPDIACTIONFORMATA a,
        LPDIENUMDEVICESBYSEMANTICSCBA cb,
        LPVOID r,
        DWORD d
    ) override {
        return inner_->EnumDevicesBySemantics(u, a, cb, r, d);
    }

    HRESULT STDMETHODCALLTYPE ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK cb,
        LPDICONFIGUREDEVICESPARAMSA p,
        DWORD d,
        LPVOID r
    ) override {
        return inner_->ConfigureDevices(cb, p, d, r);
    }

private:
    std::atomic<ULONG> refs_{1};
    IDirectInput8A* inner_;
};

} // namespace sm

static IDirectInput8A* SM_WrapDirectInput(IDirectInput8A* inner) {
    sm::ensure_loaded();
    return new sm::DirectInputWrapper(inner);
}
