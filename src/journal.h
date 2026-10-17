#pragma once
#include <cstdint>

// The game's journal window, found through the native UI widget tree (game::widget), and
// where the Track button goes on its Quest Summary page.
namespace journal {
    struct Button { uintptr_t quest = 0; int32_t x = 0, y = 0, width = 0, height = 0; };   // UI units; the quest it toggles

    // The journal window widget (it exists while closed too); 0 if not found.
    uintptr_t Locate();
    // True while `window` shows its Quest Summary page; fills `button` with the slot between Abandon Quest
    // and Back. The first call per page shrinks those two with the game's own setters: render thread only.
    bool TrackButton(uintptr_t window, Button& button);
}
