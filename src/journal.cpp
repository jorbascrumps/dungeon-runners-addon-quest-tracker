#include "journal.h"
#include "game.h"
#include <cstdlib>
#include <string>

namespace {
    namespace w = game::widget;
    const uintptr_t image = game::Image();
    constexpr unsigned DepthLimit = 32, WidgetLimit = 4000, ChildLimit = 512;   // bounds against a bad tree
    constexpr unsigned PageDepth = 3;           // journal window -> page -> "top" -> buttons (log 13)
    constexpr int32_t CoordinateLimit = 16384;  // same sanity bound as upstream's Geometry
    constexpr int32_t RowHeight = 30, AbandonWidth = 160, BackWidth = 120;   // the Quest Summary page's bottom row (log 13)
    constexpr int32_t AbandonFit = 130, BackFit = 82, RowGap = 8;            // ours; Track gets the freed 82 px

    struct Rect { int32_t x = 0, y = 0, width = 0, height = 0; };   // a widget's +0xf0..+0xfc

    bool Visible(uintptr_t widget) {
        uint32_t flags = 0;
        return mem::Read(widget + w::Flags, flags) && (flags & w::Visible);
    }

    // The first visible widget under `root`, at most `depth` levels below its children, that `match`
    // accepts; hidden subtrees are skipped. Stops after `budget` widgets.
    template<class Match> uintptr_t Find(uintptr_t root, unsigned depth, unsigned& budget, Match match) {
        unsigned children = 0;
        for (uintptr_t child = mem::Ptr(root + w::FirstChild); child && children++ < ChildLimit && budget; child = mem::Ptr(child + w::Next)) {
            if (mem::Ptr(child + w::Parent) != root) return 0;   // not a child list after all
            if (!Visible(child)) continue;
            --budget;
            if (match(child)) return child;
            if (const uintptr_t found = depth ? Find(child, depth - 1, budget, match) : 0) return found;
        }
        return 0;
    }

    // Screen position of `widget` in UI units, if it and every parent up to the root are visible (upstream's Geometry walk).
    bool Origin(uintptr_t widget, int32_t& x, int32_t& y) {
        const uintptr_t root = mem::Ptr(image + game::UiRoot);
        x = y = 0;
        for (unsigned depth = 0; widget && depth < DepthLimit; ++depth, widget = mem::Ptr(widget + w::Parent)) {
            if (!Visible(widget)) return false;
            if (widget == root) return true;
            int32_t position[2]{};
            if (!mem::Read(widget + w::X, position, sizeof(position))) return false;
            if (std::abs(position[0]) > CoordinateLimit || std::abs(position[1]) > CoordinateLimit) return false;
            x += position[0]; y += position[1];
        }
        return false;
    }

    using Setter = void (__thiscall*)(void*, const int32_t*, bool);

    // Upstream's check before calling the setters: the widget's class uses the game's own.
    bool UsesSetters(uintptr_t widget) {
        const uintptr_t type = mem::Ptr(widget);
        return mem::Ptr(type + w::SetPositionSlot) == image + game::SetPosition && mem::Ptr(type + w::SetSizeSlot) == image + game::SetSize;
    }

    void Set(uintptr_t function, uintptr_t widget, int32_t a, int32_t b) {
        const int32_t value[2] = {a, b};
        reinterpret_cast<Setter>(image + function)(reinterpret_cast<void*>(widget), value, false);
    }

    // Makes room for Track: Abandon Quest shrinks, Back shrinks and moves right (`b` follows). Only from the game's own layout.
    bool Fit(uintptr_t abandon, uintptr_t back, const Rect& a, Rect& b) {
        if (a.width == AbandonFit && b.width == BackFit) return true;   // done earlier
        if (a.width != AbandonWidth || b.width != BackWidth || a.height != RowHeight || b.height != RowHeight || a.y != b.y) return false;
        if (!UsesSetters(abandon) || !UsesSetters(back)) return false;
        b.x += BackWidth - BackFit;
        Set(game::SetSize, abandon, AbandonFit, RowHeight);
        Set(game::SetPosition, back, b.x, b.y);
        Set(game::SetSize, back, BackFit, RowHeight);
        return true;
    }
}

namespace journal {
    uintptr_t Locate() {
        const uintptr_t window = mem::Ptr(mem::Ptr(image + game::UiRoot) + game::JournalSlot);
        return mem::Ptr(window) == image + game::JournalVtable ? window : 0;
    }

    bool TrackButton(uintptr_t window, Button& button) {
        if (!window || mem::Ptr(window) != image + game::JournalVtable || !Visible(window)) return false;   // closed: one read per frame
        const uintptr_t quest = mem::Ptr(window + game::JournalQuest);
        if (mem::Ptr(quest) != image + game::QuestVtable) return false;
        unsigned budget = WidgetLimit;
        const auto named = [](const char* name) { return [name](uintptr_t widget) { return mem::String(widget + w::Name, 96) == name; }; };
        const uintptr_t abandon = Find(window, PageDepth, budget, named("AbandonQuest"));
        const uintptr_t back = abandon ? Find(mem::Ptr(abandon + w::Parent), 0, budget, named("Back")) : 0;   // a sibling
        Rect a, b;
        int32_t x = 0, y = 0;   // Abandon Quest's screen position
        if (!back || !mem::Read(abandon + w::X, &a, sizeof(a)) || !mem::Read(back + w::X, &b, sizeof(b)) ||
            !Origin(abandon, x, y) || !Fit(abandon, back, a, b)) return false;
        button = {quest, x + AbandonFit + RowGap, y, b.x - a.x - AbandonFit - 2 * RowGap, RowHeight};
        return true;
    }
}
