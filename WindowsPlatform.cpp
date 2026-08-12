#include "WindowsPlatform.h"

#ifdef _WIN32

#include <chrono>
#include <cmath>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "ConsoleControl.h"
#include "Error.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace sek {

namespace {

class TimerResolutionGuard {
public:
    TimerResolutionGuard() : active_(timeBeginPeriod(1) == TIMERR_NOERROR) {}

    ~TimerResolutionGuard() {
        if (active_) {
            timeEndPeriod(1);
        }
    }

private:
    bool active_ = false;
};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }

    const int textLength = static_cast<int>(text.size());
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), textLength, nullptr, 0);
    if (required <= 0) {
        throw RuntimeError("Failed to convert text to UTF-16 for send");
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.data(), textLength, &wide[0], required) <= 0) {
        throw RuntimeError("Failed to convert text to UTF-16 for send");
    }

    return wide;
}

std::wstring mciErrorMessage(const MCIERROR error) {
    wchar_t buffer[256];
    if (mciGetErrorStringW(error, buffer, static_cast<UINT>(sizeof(buffer) / sizeof(buffer[0])))) {
        return buffer;
    }

    return L"Unknown MCI error";
}

std::wstring quoteMciArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (std::wstring::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (*it != L'"') {
            quoted.push_back(*it);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

void ensureTimerResolution() {
    static TimerResolutionGuard resolutionGuard;
    (void)resolutionGuard;
}

double performanceCounterMs() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return value;
    }();

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (static_cast<double>(counter.QuadPart) * 1000.0) /
           static_cast<double>(frequency.QuadPart);
}

void waitWithHighResolution(const double milliseconds) {
    using CreateWaitableTimerExWFn = HANDLE (WINAPI *)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);

    HANDLE timer = nullptr;
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 != nullptr) {
        const auto createWaitableTimerExW = reinterpret_cast<CreateWaitableTimerExWFn>(
            GetProcAddress(kernel32, "CreateWaitableTimerExW"));
        if (createWaitableTimerExW != nullptr) {
            timer = createWaitableTimerExW(
                nullptr,
                nullptr,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                TIMER_ALL_ACCESS);
        }
    }

    if (timer == nullptr) {
        timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    }

    if (timer == nullptr) {
        return;
    }

    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -static_cast<LONGLONG>(milliseconds * 10000.0);
    if (!SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, FALSE)) {
        CloseHandle(timer);
        return;
    }

    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

}  // namespace

void preciseSleepMs(const double milliseconds) {
    if (milliseconds <= 0.0) {
        return;
    }

    ensureTimerResolution();

    const double target = performanceCounterMs() + milliseconds;
    for (;;) {
        const double remaining = target - performanceCounterMs();
        if (remaining <= 0.0) {
            return;
        }

        if (remaining > 2.0) {
            waitWithHighResolution(remaining - 1.0);
            continue;
        }

        if (remaining > 0.25) {
            SwitchToThread();
            continue;
        }

        while (performanceCounterMs() < target) {
        }
        return;
    }
}

void showMessageBox(const std::string& text) {
    const std::wstring wideText = utf8ToWide(text);
    MessageBoxW(nullptr, wideText.c_str(), L"gum", MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
}

void playSoundFile(const std::string& path) {
    const std::wstring widePath = utf8ToWide(path);
    if (widePath.empty()) {
        throw RuntimeError("playsound expects a non-empty file path");
    }

    const std::wstring alias = L"sek_sound";
    const std::wstring closeCommand = L"close " + alias;
    mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);

    const std::wstring openCommand = L"open " + quoteMciArgument(widePath) + L" alias " + alias;

    MCIERROR error = mciSendStringW(openCommand.c_str(), nullptr, 0, nullptr);
    if (error != 0) {
        throw RuntimeError("playsound failed to open file");
    }

    const std::wstring playCommand = L"play " + alias + L" from 0";
    error = mciSendStringW(playCommand.c_str(), nullptr, 0, nullptr);
    if (error != 0) {
        mciSendStringW(closeCommand.c_str(), nullptr, 0, nullptr);
        throw RuntimeError("playsound failed to play file");
    }
}

void sendText(const std::string& text) {
    const std::wstring wideText = utf8ToWide(text);
    if (wideText.empty()) {
        return;
    }

    std::vector<INPUT> inputs;
    inputs.reserve(wideText.size() * 2);

    for (std::wstring::const_iterator it = wideText.begin(); it != wideText.end(); ++it) {
        INPUT keyDown;
        ZeroMemory(&keyDown, sizeof(INPUT));
        keyDown.type = INPUT_KEYBOARD;
        keyDown.ki.wScan = *it;
        keyDown.ki.dwFlags = KEYEVENTF_UNICODE;

        INPUT keyUp = keyDown;
        keyUp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        inputs.push_back(keyDown);
        inputs.push_back(keyUp);
    }

    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), &inputs[0], sizeof(INPUT));
    if (sent != inputs.size()) {
        throw RuntimeError("SendInput failed while executing send");
    }
}

void sendNamedKey(const std::string& keyName) {
    const unsigned int virtualKey = resolveVirtualKey(keyName);

    INPUT keyDown;
    ZeroMemory(&keyDown, sizeof(INPUT));
    keyDown.type = INPUT_KEYBOARD;
    keyDown.ki.wVk = static_cast<WORD>(virtualKey);

    INPUT keyUp = keyDown;
    keyUp.ki.dwFlags = KEYEVENTF_KEYUP;

    INPUT inputs[2];
    inputs[0] = keyDown;
    inputs[1] = keyUp;

    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        throw RuntimeError("SendInput failed while executing send key");
    }
}

void clickLeftMouseButton() {
    INPUT mouseDown;
    ZeroMemory(&mouseDown, sizeof(INPUT));
    mouseDown.type = INPUT_MOUSE;
    mouseDown.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    INPUT mouseUp = mouseDown;
    mouseUp.mi.dwFlags = MOUSEEVENTF_LEFTUP;

    INPUT inputs[2];
    inputs[0] = mouseDown;
    inputs[1] = mouseUp;

    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        throw RuntimeError("SendInput failed while executing click");
    }
}

void sendLeftMouseEvent(DWORD flags, const char* action) {
    INPUT input;
    ZeroMemory(&input, sizeof(INPUT));
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;

    const UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        throw RuntimeError(std::string("SendInput failed while executing ") + action);
    }
}

void leftMouseDown() {
    sendLeftMouseEvent(MOUSEEVENTF_LEFTDOWN, "mousedown");
}

void leftMouseUp() {
    sendLeftMouseEvent(MOUSEEVENTF_LEFTUP, "mouseup");
}

void moveMouse(int x, int y) {
    // Move the physical cursor to given screen coordinates (pixels)
    if (!SetCursorPos(x, y)) {
        throw RuntimeError("SetCursorPos failed while executing mousemove");
    }
}

unsigned int resolveVirtualKey(const std::string& trigger) {
    if (trigger.size() == 1) {
        const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(trigger[0])));
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return static_cast<unsigned int>(ch);
        }
    }

    static const std::map<std::string, unsigned int> specialKeys = {
        {"space", VK_SPACE},
        {"tab", VK_TAB},
        {"enter", VK_RETURN},
        {"esc", VK_ESCAPE},
        {"escape", VK_ESCAPE},
        {"up", VK_UP},
        {"down", VK_DOWN},
        {"left", VK_LEFT},
        {"right", VK_RIGHT},
        {"delete", VK_DELETE},
        {"backspace", VK_BACK},
        {"home", VK_HOME},
        {"end", VK_END},
        {"pgup", VK_PRIOR},
        {"pgdn", VK_NEXT},
    };

    std::string normalized;
    normalized.reserve(trigger.size());
    for (std::string::const_iterator it = trigger.begin(); it != trigger.end(); ++it) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*it))));
    }

    const std::map<std::string, unsigned int>::const_iterator special = specialKeys.find(normalized);
    if (special != specialKeys.end()) {
        return special->second;
    }

    if (normalized.size() == 2 && normalized[0] == 'f' && std::isdigit(normalized[1])) {
        return static_cast<unsigned int>(VK_F1 + (normalized[1] - '1'));
    }

    if (normalized.size() == 3 && normalized[0] == 'f' && normalized[1] == '1' &&
        normalized[2] >= '0' && normalized[2] <= '2') {
        return static_cast<unsigned int>(VK_F10 + (normalized[2] - '0'));
    }

    throw RuntimeError("Unsupported hotkey trigger: " + trigger);
}

std::vector<RegisteredHotkey> registerHotkeys(const std::vector<std::string>& triggers) {
    std::vector<RegisteredHotkey> hotkeys;
    hotkeys.reserve(triggers.size());

    for (std::size_t index = 0; index < triggers.size(); ++index) {
        const int hotkeyId = static_cast<int>(index + 1);
        const unsigned int key = resolveVirtualKey(triggers[index]);
        if (!RegisterHotKey(nullptr, hotkeyId, MOD_NOREPEAT, key)) {
            unregisterHotkeys(hotkeys);
            throw RuntimeError("Failed to register hotkey: " + triggers[index]);
        }

        RegisteredHotkey hotkey;
        hotkey.id = hotkeyId;
        hotkey.trigger = triggers[index];
        hotkeys.push_back(hotkey);
    }

    return hotkeys;
}

int waitForHotkey() {
    MSG message;
    while (!isStopRequested()) {
        const BOOL result = PeekMessage(&message, nullptr, 0, 0, PM_REMOVE);
        if (!result) {
            Sleep(25);
            continue;
        }

        if (message.message == WM_HOTKEY) {
            return static_cast<int>(message.wParam);
        }

        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return -1;
}

int pollHotkey() {
    MSG message;

    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_HOTKEY) {
            return static_cast<int>(message.wParam);
        }

        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return -1;
}

void unregisterHotkeys(const std::vector<RegisteredHotkey>& hotkeys) {
    for (std::vector<RegisteredHotkey>::const_iterator it = hotkeys.begin(); it != hotkeys.end(); ++it) {
        UnregisterHotKey(nullptr, it->id);
    }
}

}  // namespace sek

#else

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include "ConsoleControl.h"
#include "Error.h"

namespace sek {

namespace {

Display* g_display = nullptr;
std::map<KeyCode, int> g_hotkeyKeycodeToId;
std::vector<unsigned int> g_grabbedModifierMasks;
std::vector<KeyCode> g_grabbedKeycodes;
int g_xErrorCode = 0;

int handleXError(Display*, XErrorEvent* event) {
    g_xErrorCode = event->error_code;
    return 0;
}

Display* getDisplay() {
    if (g_display == nullptr) {
        g_display = XOpenDisplay(nullptr);
        if (g_display == nullptr) {
            throw RuntimeError(
                "Cannot open X display. Make sure DISPLAY is set and an X11 session is running.");
        }

        XSetErrorHandler(handleXError);
    }

    return g_display;
}

double performanceCounterMs() {
    struct timespec now;
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) * 1000.0 +
           static_cast<double>(now.tv_nsec) / 1000000.0;
}

KeySym keysymForName(const std::string& trigger) {
    if (trigger.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(trigger[0]);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            char single[2];
            single[0] = static_cast<char>(std::tolower(ch));
            single[1] = '\0';
            return XStringToKeysym(single);
        }

        if (ch >= 'a' && ch <= 'z') {
            char single[2];
            single[0] = static_cast<char>(ch);
            single[1] = '\0';
            return XStringToKeysym(single);
        }
    }

    static const std::map<std::string, KeySym> specialKeys = {
        {"space", XK_space},
        {"tab", XK_Tab},
        {"enter", XK_Return},
        {"esc", XK_Escape},
        {"escape", XK_Escape},
        {"up", XK_Up},
        {"down", XK_Down},
        {"left", XK_Left},
        {"right", XK_Right},
        {"delete", XK_Delete},
        {"backspace", XK_BackSpace},
        {"home", XK_Home},
        {"end", XK_End},
        {"pgup", XK_Page_Up},
        {"pgdn", XK_Page_Down},
    };

    std::string normalized;
    normalized.reserve(trigger.size());
    for (std::string::const_iterator it = trigger.begin(); it != trigger.end(); ++it) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*it))));
    }

    const std::map<std::string, KeySym>::const_iterator special = specialKeys.find(normalized);
    if (special != specialKeys.end()) {
        return special->second;
    }

    if (normalized.size() == 2 && normalized[0] == 'f' && std::isdigit(normalized[1])) {
        return static_cast<KeySym>(XK_F1 + (normalized[1] - '1'));
    }

    if (normalized.size() == 3 && normalized[0] == 'f' && normalized[1] == '1' &&
        normalized[2] >= '0' && normalized[2] <= '2') {
        return static_cast<KeySym>(XK_F10 + (normalized[2] - '0'));
    }

    throw RuntimeError("Unsupported hotkey trigger: " + trigger);
}

std::vector<unsigned int> hotkeyModifierMasks() {
    std::vector<unsigned int> masks;
    masks.push_back(0U);
    masks.push_back(LockMask);
    masks.push_back(Mod2Mask);
    masks.push_back(LockMask | Mod2Mask);
    return masks;
}

int processHotkeyEvent(const XEvent& event) {
    if (event.type != KeyPress) {
        return -1;
    }

    const std::map<KeyCode, int>::const_iterator found =
        g_hotkeyKeycodeToId.find(event.xkey.keycode);
    if (found == g_hotkeyKeycodeToId.end()) {
        return -1;
    }

    return found->second;
}

std::vector<unsigned long> utf8ToCodePoints(const std::string& text) {
    std::vector<unsigned long> codePoints;

    for (std::size_t index = 0; index < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        unsigned long codePoint = 0;
        std::size_t width = 0;

        if (ch < 0x80) {
            codePoint = ch;
            width = 1;
        } else if ((ch & 0xE0) == 0xC0) {
            codePoint = ch & 0x1F;
            width = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            codePoint = ch & 0x0F;
            width = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            codePoint = ch & 0x07;
            width = 4;
        } else {
            ++index;
            continue;
        }

        if (index + width > text.size()) {
            break;
        }

        for (std::size_t offset = 1; offset < width; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                width = 0;
                break;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3F);
        }

        if (width == 0) {
            ++index;
            continue;
        }

        codePoints.push_back(codePoint);
        index += width;
    }

    return codePoints;
}

void fakeKeyPressRelease(Display* display, KeyCode keycode) {
    XTestFakeKeyEvent(display, keycode, True, CurrentTime);
    XTestFakeKeyEvent(display, keycode, False, CurrentTime);
}

}  // namespace

void preciseSleepMs(const double milliseconds) {
    if (milliseconds <= 0.0) {
        return;
    }

    const double target = performanceCounterMs() + milliseconds;
    for (;;) {
        const double remaining = target - performanceCounterMs();
        if (remaining <= 0.0) {
            return;
        }

        if (remaining > 2.0) {
            struct timespec request;
            request.tv_sec = 0;
            request.tv_nsec = static_cast<long>((remaining - 1.0) * 1000000.0);
            ::clock_nanosleep(CLOCK_MONOTONIC, 0, &request, nullptr);
            continue;
        }

        if (remaining > 0.25) {
            ::sched_yield();
            continue;
        }

        while (performanceCounterMs() < target) {
        }
        return;
    }
}

void showMessageBox(const std::string& text) {
    const std::string option = "--text=" + text;
    const pid_t child = ::fork();
    if (child == 0) {
        ::execlp("zenity", "zenity", "--info", "--title=gum", option.c_str(),
                 static_cast<char*>(nullptr));
        ::execlp("xmessage", "xmessage", text.c_str(), static_cast<char*>(nullptr));
        ::_exit(1);
    }

    if (child > 0) {
        ::waitpid(child, nullptr, 0);
    }
}

void playSoundFile(const std::string& path) {
    const pid_t child = ::fork();
    if (child == 0) {
        ::execlp("aplay", "aplay", "-q", path.c_str(), static_cast<char*>(nullptr));
        ::execlp("paplay", "paplay", path.c_str(), static_cast<char*>(nullptr));
        ::execlp("ffplay", "ffplay", "-nodisp", "-autoexit", path.c_str(),
                 static_cast<char*>(nullptr));
        ::_exit(1);
    }

    if (child > 0) {
        ::waitpid(child, nullptr, 0);
    }
}

void sendText(const std::string& text) {
    Display* display = getDisplay();
    const std::vector<unsigned long> codePoints = utf8ToCodePoints(text);

    KeyCode shiftKeycode = XKeysymToKeycode(display, XK_Shift_L);

    for (std::vector<unsigned long>::const_iterator it = codePoints.begin();
         it != codePoints.end(); ++it) {
        const unsigned long codePoint = *it;

        KeySym target = static_cast<KeySym>(codePoint);
        if (codePoint >= 128) {
            target = static_cast<KeySym>(0x01000000UL | codePoint);
        }

        KeyCode keycode = XKeysymToKeycode(display, target);
        if (keycode == 0) {
            continue;
        }

        if (codePoint < 128) {
            const KeySym baseKeysym = XkbKeycodeToKeysym(display, keycode, 0, 0);
            const KeySym shiftedKeysym = XkbKeycodeToKeysym(display, keycode, 0, 1);

            bool needsShift = false;
            if (target != baseKeysym && target == shiftedKeysym) {
                needsShift = true;
            } else if (target != baseKeysym && target != shiftedKeysym) {
                continue;
            }

            if (needsShift && shiftKeycode != 0) {
                XTestFakeKeyEvent(display, shiftKeycode, True, CurrentTime);
            }

            fakeKeyPressRelease(display, keycode);

            if (needsShift && shiftKeycode != 0) {
                XTestFakeKeyEvent(display, shiftKeycode, False, CurrentTime);
            }
        } else {
            fakeKeyPressRelease(display, keycode);
        }
    }

    XFlush(display);
}

void sendNamedKey(const std::string& keyName) {
    Display* display = getDisplay();
    const KeySym keysym = keysymForName(keyName);
    const KeyCode keycode = XKeysymToKeycode(display, keysym);
    if (keycode == 0) {
        throw RuntimeError("Unsupported send key: " + keyName);
    }

    fakeKeyPressRelease(display, keycode);
    XFlush(display);
}

void clickLeftMouseButton() {
    Display* display = getDisplay();
    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    XFlush(display);
}

void leftMouseDown() {
    Display* display = getDisplay();
    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XFlush(display);
}

void leftMouseUp() {
    Display* display = getDisplay();
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    XFlush(display);
}

void moveMouse(int x, int y) {
    Display* display = getDisplay();
    XWarpPointer(display, None, DefaultRootWindow(display), 0, 0, 0, 0, x, y);
    XFlush(display);
}

unsigned int resolveVirtualKey(const std::string& trigger) {
    Display* display = getDisplay();
    const KeySym keysym = keysymForName(trigger);
    const KeyCode keycode = XKeysymToKeycode(display, keysym);
    if (keycode == 0) {
        throw RuntimeError("Unsupported hotkey trigger: " + trigger);
    }

    return static_cast<unsigned int>(keycode);
}

std::vector<RegisteredHotkey> registerHotkeys(const std::vector<std::string>& triggers) {
    Display* display = getDisplay();
    const Window root = DefaultRootWindow(display);
    const std::vector<unsigned int> modifierMasks = hotkeyModifierMasks();

    std::vector<KeyCode> keycodes;
    keycodes.reserve(triggers.size());
    for (std::size_t index = 0; index < triggers.size(); ++index) {
        keycodes.push_back(static_cast<KeyCode>(resolveVirtualKey(triggers[index])));
    }

    std::vector<RegisteredHotkey> hotkeys;
    hotkeys.reserve(triggers.size());

    g_xErrorCode = 0;

    for (std::size_t index = 0; index < keycodes.size(); ++index) {
        const int hotkeyId = static_cast<int>(index + 1);
        const KeyCode keycode = keycodes[index];

        for (std::vector<unsigned int>::const_iterator it = modifierMasks.begin();
             it != modifierMasks.end(); ++it) {
            XGrabKey(display, static_cast<int>(keycode), *it, root, True,
                     GrabModeAsync, GrabModeAsync);
        }

        g_hotkeyKeycodeToId[keycode] = hotkeyId;
        g_grabbedKeycodes.push_back(keycode);
        g_grabbedModifierMasks = modifierMasks;

        RegisteredHotkey hotkey;
        hotkey.id = hotkeyId;
        hotkey.trigger = triggers[index];
        hotkeys.push_back(hotkey);
    }

    XSync(display, False);

    if (g_xErrorCode != 0) {
        unregisterHotkeys(hotkeys);
        g_hotkeyKeycodeToId.clear();
        g_grabbedKeycodes.clear();
        g_grabbedModifierMasks.clear();
        throw RuntimeError("Failed to register hotkey (already grabbed by another application)");
    }

    return hotkeys;
}

int waitForHotkey() {
    Display* display = getDisplay();

    while (!isStopRequested()) {
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);

            const int hotkeyId = processHotkeyEvent(event);
            if (hotkeyId > 0) {
                return hotkeyId;
            }
        }

        ::usleep(20000);
    }

    return -1;
}

int pollHotkey() {
    Display* display = getDisplay();

    while (XPending(display) > 0) {
        XEvent event;
        XNextEvent(display, &event);

        const int hotkeyId = processHotkeyEvent(event);
        if (hotkeyId > 0) {
            return hotkeyId;
        }
    }

    return -1;
}

void unregisterHotkeys(const std::vector<RegisteredHotkey>& hotkeys) {
    Display* display = g_display;
    if (display == nullptr) {
        return;
    }

    const Window root = DefaultRootWindow(display);
    const std::vector<unsigned int> modifierMasks = g_grabbedModifierMasks;

    for (std::size_t index = 0; index < g_grabbedKeycodes.size(); ++index) {
        const KeyCode keycode = g_grabbedKeycodes[index];
        for (std::vector<unsigned int>::const_iterator it = modifierMasks.begin();
             it != modifierMasks.end(); ++it) {
            XUngrabKey(display, static_cast<int>(keycode), *it, root);
        }
    }

    XSync(display, False);
    g_hotkeyKeycodeToId.clear();
    g_grabbedKeycodes.clear();
    g_grabbedModifierMasks.clear();

    (void)hotkeys;
}

}  // namespace sek

#endif
