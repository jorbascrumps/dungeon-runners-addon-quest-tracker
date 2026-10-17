#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Quest {
    uintptr_t address = 0;
    uint32_t id = 0;
    std::string path;                     // definition path; the key under [Tracked] in settings.ini
    std::string title;                    // the game's title (Description.Label)
    std::vector<std::string> objectives;  // the game's own tracker text per objective, "Green Paint: 1 / 7"
};

// The local player's quests, sorted by id, read from the quest manager's std::set (log 16);
// false when none were found.
bool ScanQuests(uintptr_t player, std::vector<Quest>& quests);
