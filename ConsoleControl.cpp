#include "ConsoleControl.h"

#include <atomic>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace sek {

namespace {

std::atomic<bool> g_stopRequested(false);

BOOL WINAPI handleConsoleControl(const DWORD controlType) {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT ||
        controlType == CTRL_CLOSE_EVENT) {
        g_stopRequested.store(true);
        return TRUE;
    }

    return FALSE;
}

}  // namespace

void configureConsoleEncoding() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void installConsoleControlHandler() {
    g_stopRequested.store(false);
    SetConsoleCtrlHandler(handleConsoleControl, TRUE);
}

bool isStopRequested() {
    return g_stopRequested.load();
}

}  // namespace sek

#else

#include <csignal>
#include <clocale>

namespace sek {

namespace {

std::atomic<bool> g_stopRequested(false);

void handleSignal(int) {
    g_stopRequested.store(true);
}

}  // namespace

void configureConsoleEncoding() {
    std::setlocale(LC_ALL, "");
}

void installConsoleControlHandler() {
    g_stopRequested.store(false);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
}

bool isStopRequested() {
    return g_stopRequested.load();
}

}  // namespace sek

#endif
