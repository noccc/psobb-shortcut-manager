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
#include <array>
#include <cstring>
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
static std::atomic<unsigned long> gAxisMask(0);
static std::atomic<int> gStatus(0);
static std::atomic<int> gCaptureEnabled(1);

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

static void press_rule(Rule& r) {
    if (r.syntheticDown || r.key == 0) return;

    if (r.ctrl)  send_scan(DIK_LCONTROL, false);
    if (r.shift) send_scan(DIK_LSHIFT, false);
    if (r.alt)   send_scan(DIK_LMENU, false);

    send_scan(r.key, false);

    r.syntheticDown = true;
    r.releaseAt = GetTickCount() + 70;
}

static void release_rule(Rule& r) {
    if (!r.syntheticDown) {
        r.releaseAt = 0;
        return;
    }

    // Release main key first, then modifiers.
    send_scan(r.key, true);

    if (r.alt)   send_scan(DIK_LMENU, true);
    if (r.shift) send_scan(DIK_LSHIFT, true);
    if (r.ctrl)  send_scan(DIK_LCONTROL, true);

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
                if (b >= 0 && b < 52) {
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

static bool combo_down(const Rule& r, unsigned long buttonMask, unsigned long dpadMask, unsigned long axisMask) {
    if (r.buttons.empty()) return false;

    for (int b : r.buttons) {
        if (b >= 0 && b < 32) {
            if ((buttonMask & (1UL << b)) == 0) return false;
        } else if (b >= 32 && b < 36) {
            const int d = b - 32;
            if ((dpadMask & (1UL << d)) == 0) return false;
        } else if (b >= 36 && b < 52) {
            if ((axisMask & (1UL << (b - 36))) == 0) return false;
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

extern "C" __declspec(dllexport) unsigned long __cdecl SM_GetButtonMask() {
    return gButtonMask.load();
}

extern "C" __declspec(dllexport) unsigned long __cdecl SM_GetDPadMask() {
    return gDpadMask.load();
}

extern "C" __declspec(dllexport) unsigned long __cdecl SM_GetAxisMask() {
    return gAxisMask.load();
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

extern "C" __declspec(dllexport) void __cdecl SM_ReloadConfig() {
    std::lock_guard<std::mutex> lock(gMutex);
    load_config_locked();
}

#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:SM_GetButtonMask=_SM_GetButtonMask")
#pragma comment(linker, "/EXPORT:SM_GetDPadMask=_SM_GetDPadMask")
#pragma comment(linker, "/EXPORT:SM_GetAxisMask=_SM_GetAxisMask")
#pragma comment(linker, "/EXPORT:SM_GetStatus=_SM_GetStatus")
#pragma comment(linker, "/EXPORT:SM_SetCaptureEnabled=_SM_SetCaptureEnabled")
#pragma comment(linker, "/EXPORT:SM_ReloadConfig=_SM_ReloadConfig")
#endif

class ControllerDeviceWrapper : public IDirectInputDevice8A {
public:
    explicit ControllerDeviceWrapper(IDirectInputDevice8A* inner)
        : inner_(inner) {
        gStatus.fetch_or(1); // wrapper active
        gStatus.fetch_or(4); // controller seen
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

        // Analog axes, relative to their calibrated idle value. The physical
        // device may expose LT/RT as separate axes or opposite ends of one axis.
        // IDs 36..51 represent axis 1..8 (- / +), learned by manual capture.
        unsigned long axisMask = 0;
        for (size_t i = 0; i < axisOffsets_.size() && i < 8; ++i) {
            const DWORD off = axisOffsets_[i];
            if (off + sizeof(LONG) > cbData) continue;
            LONG value = 0;
            memcpy(&value, data + off, sizeof(value));
            if (!axisReady_[i]) {
                axisBaseline_[i] = value;
                axisReady_[i] = true;
            }
            // Most DirectInput axes default to 0..65535 or -32768..32767.
            // 10000 units filters deadzone noise. If a controller reports a
            // smaller range, adjust that range in its Windows game controller setup.
            const long long delta = static_cast<long long>(value) - axisBaseline_[i];
            const long long threshold = axisThreshold_[i];
            if (delta < -threshold) axisMask |= 1UL << (2*i);
            if (delta >  threshold) axisMask |= 1UL << (2*i+1);
        }
        gButtonMask.store(mask);
        gDpadMask.store(dpadMask);
        gAxisMask.store(axisMask);

        const DWORD now = GetTickCount();
        const bool foreground =
            gameWindow_ != nullptr &&
            GetForegroundWindow() == gameWindow_;

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

        // First state after acquire/focus change = baseline only.
        if (syncNextControllerState_) {
            for (Rule& r : gRules) {
                r.latched = combo_down(r, mask, dpadMask, axisMask);
            }

            syncNextControllerState_ = false;
            return hr;
        }

        for (Rule& r : gRules) {
            const bool down = combo_down(r, mask, dpadMask, axisMask);

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

        if (SUCCEEDED(hr) && fmt && fmt->rgodf) {
            buttonOffsets_.clear();
            povOffsets_.clear();
            axisOffsets_.clear();
            axisReady_.fill(false);
            axisThreshold_.fill(10000);

            for (DWORD i = 0; i < fmt->dwNumObjs; ++i) {
                const DIOBJECTDATAFORMAT& o = fmt->rgodf[i];
                const DWORD t = DIDFT_GETTYPE(o.dwType);

                if ((t & DIDFT_BUTTON) != 0) {
                    buttonOffsets_.push_back(o.dwOfs);
                } else if ((t & DIDFT_POV) != 0) {
                    povOffsets_.push_back(o.dwOfs);
                } else if ((t & DIDFT_AXIS) != 0 && axisOffsets_.size() < 8) {
                    axisOffsets_.push_back(o.dwOfs);
                    DIPROPRANGE range = {};
                    range.diph.dwSize = sizeof(range);
                    range.diph.dwHeaderSize = sizeof(range.diph);
                    range.diph.dwObj = o.dwOfs;
                    range.diph.dwHow = DIPH_BYOFFSET;
                    if (SUCCEEDED(inner_->GetProperty(DIPROP_RANGE, &range.diph))) {
                        const long long extent =
                            static_cast<long long>(range.lMax) - range.lMin;
                        if (extent > 0) {
                            // Require a quarter of the reported physical axis range.
                            axisThreshold_[axisOffsets_.size()-1] =
                                static_cast<LONG>(std::max(1LL, extent / 4));
                        }
                    }
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
    HWND gameWindow_ = nullptr;
    bool syncNextControllerState_ = true;
    std::vector<DWORD> buttonOffsets_;
    std::vector<DWORD> povOffsets_;
    std::vector<DWORD> axisOffsets_;
    std::array<LONG, 8> axisThreshold_{};
    std::array<LONG, 8> axisBaseline_{};
    std::array<bool, 8> axisReady_{};
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

            if (keyboard) {
                // IMPORTANT:
                // The keyboard is deliberately NOT wrapped.
                // PSO receives its native DirectInput keyboard device untouched.
                gStatus.fetch_or(2);
            } else if (!mouse) {
                *out = new ControllerDeviceWrapper(*out);
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
