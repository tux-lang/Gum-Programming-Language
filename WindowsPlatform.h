#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sek {

struct RegisteredHotkey {
    int id = 0;
    std::string trigger;
};

void preciseSleepMs(double milliseconds);
void showMessageBox(const std::string& text);
void playSoundFile(const std::string& path);
void sendText(const std::string& text);
void sendNamedKey(const std::string& keyName);
void clickLeftMouseButton();
void leftMouseDown();
void leftMouseUp();
void moveMouse(int x, int y);
unsigned int resolveVirtualKey(const std::string& trigger);
std::vector<RegisteredHotkey> registerHotkeys(const std::vector<std::string>& triggers);
int waitForHotkey();
int pollHotkey();
void unregisterHotkeys(const std::vector<RegisteredHotkey>& hotkeys);

}  // namespace sek
