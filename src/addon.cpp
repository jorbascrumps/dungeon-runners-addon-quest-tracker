#include <windows.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include "Source/extension_api.h"   // upstream (MIT); the loader validates these structs strictly
#include "game.h"
#include "journal.h"
#include "overlay.h"
#include "quests.h"

#define ADDON_EXPORT extern "C" __declspec(dllexport)

namespace {
    enum Option : uint32_t { Enabled, PanelBackground, OptionCount };
    constexpr const wchar_t* Section = L"QuestTracker";
    constexpr const wchar_t* TrackedSection = L"Tracked";
    constexpr const wchar_t* Keys[OptionCount] = {L"Enabled", L"PanelBackground"};

    volatile LONG options[OptionCount] = {1, 1};
    std::set<std::string> trackedPaths;      // definition paths of tracked quests; worker thread only
    std::wstring settingsPath;
    uintptr_t image = 0;

    void LoadSettings() {
        for (uint32_t i = 0; i < OptionCount; ++i) options[i] = GetPrivateProfileIntW(Section, Keys[i], 1, settingsPath.c_str()) ? 1 : 0;
    }

    void SaveSettings() {
        for (uint32_t i = 0; i < OptionCount; ++i) WritePrivateProfileStringW(Section, Keys[i], options[i] ? L"1" : L"0", settingsPath.c_str());
    }

    void LoadTracked() {
        std::wstring buffer(32768, L'\0');
        const DWORD size = GetPrivateProfileSectionW(TrackedSection, &buffer[0], DWORD(buffer.size()), settingsPath.c_str());
        // "key=value\0key=value\0\0"
        for (const wchar_t* entry = buffer.c_str(); entry < buffer.c_str() + size && *entry; entry += wcslen(entry) + 1) {
            const std::wstring line(entry);
            const std::wstring key = line.substr(0, line.find(L'='));
            trackedPaths.insert(std::string(key.begin(), key.end()));   // definition paths are ASCII
        }
    }

    void SaveTracked(const std::string& path, bool on) {
        const std::wstring key(path.begin(), path.end());
        WritePrivateProfileStringW(TrackedSection, key.c_str(), on ? L"1" : nullptr, settingsPath.c_str());   // null deletes
    }

    // Track clicks from the overlay: quest address -> definition path, saved at once.
    void ApplyToggles(const std::vector<Quest>& quests) {
        for (uintptr_t address : overlay::TakeToggles()) {
            const auto quest = std::find_if(quests.begin(), quests.end(), [&](const Quest& q) { return q.address == address; });
            if (quest == quests.end() || quest->path.empty()) continue;
            const bool on = trackedPaths.insert(quest->path).second;
            if (!on) trackedPaths.erase(quest->path);
            SaveTracked(quest->path, on);
        }
    }

    DWORD WINAPI Worker(void*) {
        std::vector<Quest> quests;
        for (;;) {
            Sleep(1000);
            overlay::View view;
            view.background = options[PanelBackground] != 0;
            const uintptr_t player = options[Enabled] ? game::LocalPlayer(image) : 0;   // off: no reads, nothing drawn
            if (player) {
                view.visible = true;
                view.journal = journal::Locate();
                if (ScanQuests(player, quests)) {
                    ApplyToggles(quests);
                    for (const auto& quest : quests) {
                        if (!trackedPaths.count(quest.path)) continue;
                        view.tracked.push_back(quest.address);
                        view.lines.push_back({quest.title.empty() ? quest.path : quest.title, quest.objectives});
                    }
                }
            }
            overlay::Hook(reinterpret_cast<IDirect3DDevice9*>(game::Device(image)));   // a no-op once hooked
            overlay::Publish(std::move(view));
        }
    }

    int WINAPI ReadOption(void*, uint32_t index) { return index < OptionCount ? int(options[index]) : -1; }

    BOOL WINAPI ApplyOptions(void*, const int* values, uint32_t count) {
        if (!values || count != OptionCount) return FALSE;
        for (uint32_t i = 0; i < count; ++i) InterlockedExchange(&options[i], values[i] ? 1 : 0);
        SaveSettings();
        return TRUE;
    }

    template<size_t N> void Copy(char (&target)[N], const char* text) { std::strncpy(target, text, N - 1); target[N - 1] = 0; }
}

ADDON_EXPORT BOOL WINAPI DungeonRunnersAddonInitialize(const ExtensionHost* host, ExtensionDefinition* definition) {
    if (!host || !definition || host->size < sizeof(ExtensionHost) || host->version != ExtensionApiVersion ||
        definition->size != sizeof(ExtensionDefinition) || !host->addonDirectory) return FALSE;

    Copy(definition->id, "quest-tracker");
    Copy(definition->name, "Quest Tracker");
    Copy(definition->description, "Keeps pinned quests and their progress in view while you play.");
    definition->optionCount = OptionCount;
    Copy(definition->options[Enabled].label, "Enabled");
    Copy(definition->options[Enabled].help, "Show the quest panel and the journal's Track button. Off: the addon reads and draws nothing.");
    Copy(definition->options[PanelBackground].label, "Panel background");
    Copy(definition->options[PanelBackground].help, "Draw the game-style frame behind the quest list. Turn off to show only the text.");
    definition->context = nullptr;
    definition->read = ReadOption;
    definition->apply = ApplyOptions;

    static bool started = false;   // the loader may rediscover addons; start once
    if (started) return TRUE;
    started = true;

    settingsPath = std::wstring(host->addonDirectory) + L"\\settings.ini";
    image = game::Image();
    LoadSettings();
    LoadTracked();
    if (!game::Supported(image)) return TRUE;   // another game build: stay registered, draw nothing
    // ui.bin is built by the official installer from the player's game files; without it nothing is drawn.
    if (!overlay::LoadSkin(std::wstring(host->gameDirectory ? host->gameDirectory : L"") + L"\\Addons\\Runtime\\ui.bin")) return TRUE;
    if (HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(thread);
    return TRUE;
}
