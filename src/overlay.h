#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct IDirect3DDevice9;

namespace overlay {
    struct Line { std::string title; std::vector<std::string> details; };   // details: one per objective
    struct View {
        bool visible = false;              // draw the panel
        bool background = true;            // the game frame behind the quest list
        std::vector<Line> lines;
        std::vector<uintptr_t> tracked;    // quests with tracking on (their button says "Untrack")
        uintptr_t journal = 0;             // the journal window (journal::Locate) while in the world: draws the Track button
    };

    bool LoadSkin(const std::wstring& path);   // upstream's Addons/Runtime/ui.bin: frame, button and font art. Once, before Hook.
    bool Hook(IDirect3DDevice9* device);       // hot-patches d3d9's Present; true once hooked, so call until it is
    void Publish(View view);                   // from the worker thread: what to draw
    std::vector<uintptr_t> TakeToggles();      // quests whose Track button was clicked since the last call
}
