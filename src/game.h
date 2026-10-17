#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

// Safe reads into our own process: ReadProcessMemory fails cleanly on bad addresses, so no SEH.
namespace mem {
    inline bool Read(uintptr_t address, void* out, size_t size) {
        if (address < 0x10000) return false;
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
    }
    template<class T> bool Read(uintptr_t address, T& value) { return Read(address, &value, sizeof(value)); }
    inline uintptr_t Ptr(uintptr_t address) { uint32_t value = 0; Read(address, value); return value; }

    // Game strings: the field holds a pointer to { uint32 length; char text[length]; }.
    inline std::string String(uintptr_t field, uint32_t limit = 256) {
        const uintptr_t block = Ptr(field);
        uint32_t size = 0;
        if (!Read(block, size) || !size || size > limit) return {};
        std::string text(size, '\0');
        if (!Read(block + 4, &text[0], size)) return {};
        for (unsigned char c : text) if (c < 32 || c > 126) return {};
        return text;
    }
}

// RVAs and offsets for the DungeonRunners.exe build upstream targets.
namespace game {
    constexpr uintptr_t UiRoot = 0x5314b0, Graphics = 0x533a44;
    constexpr uintptr_t ZoneVtable = 0x4ab1b8, PlayerVtable = 0x49b468, QuestVtable = 0x49cbe0, JournalVtable = 0x44f8f8;
    constexpr uintptr_t SetPosition = 0x27fc60, SetSize = 0x27fed0;   // the game's widget setters; render thread only
    constexpr uintptr_t PlayerManager = 0x18, ManagerOwner = 0x14, QuestSet = 0xc8, QuestSetSize = 0xcc;   // manager: std::set<Quest*> head, size
    constexpr uintptr_t JournalSlot = 0x254, JournalQuest = 0x198;   // UI root -> journal window; journal -> the quest its summary page shows

    namespace quest {
        constexpr uintptr_t Manager = 0x14, FirstObjective = 0x18, Base = 0x54, Definition = 0x58, Description = 0x5c, Id = 0x68, ObjectiveTexts = 0x7c;
        constexpr uintptr_t DefinitionPath = 0x18, Title = 0xb4, ObjectiveNext = 0x20;   // on the definition / description / objective
        constexpr uint32_t ObjectiveTextStride = 8;
    }

    namespace widget {
        constexpr uintptr_t Name = 0x10, Parent = 0x14, FirstChild = 0x18, Next = 0x20, Flags = 0xb4;
        constexpr uint32_t Visible = 8;                                  // bit in Flags
        constexpr uintptr_t X = 0xf0, Width = 0xf8, Height = 0xfc;       // x, y, w, h; x, y relative to the parent
        constexpr uintptr_t SetPositionSlot = 0xa4, SetSizeSlot = 0xac;  // vtable slots that hold the setters above
    }

    inline uintptr_t Image() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }
    inline uintptr_t Device(uintptr_t image) { return mem::Ptr(mem::Ptr(image + Graphics) + 0x1c); }

    // The two setter prologues, which the official addons also verify and never patch.
    inline bool Supported(uintptr_t image) {
        static const uint8_t a[] = {0x8b,0x91,0xfc,0x00,0x00,0x00,0x8b,0x44,0x24,0x04,0x3b,0x50,0x04};
        static const uint8_t b[] = {0x8b,0x91,0xf0,0x00,0x00,0x00,0x8b,0x44,0x24,0x04,0x3b,0x10};
        uint8_t buffer[16]{};
        return mem::Read(image + SetSize, buffer, sizeof(a)) && !memcmp(buffer, a, sizeof(a)) &&
               mem::Read(image + SetPosition, buffer, sizeof(b)) && !memcmp(buffer, b, sizeof(b));
    }

    // The local player object, or 0 when not in a zone (upstream's InWorld checks).
    inline uintptr_t LocalPlayer(uintptr_t image) {
        const uintptr_t ui = mem::Ptr(image + UiRoot);
        uint32_t flags = 0, id = 0;
        if (!ui || !mem::Read(ui + widget::Flags, flags) || !(flags & widget::Visible)) return 0;
        const uintptr_t zone = mem::Ptr(ui + 0x1b4);
        if (!zone || mem::Ptr(zone) != image + ZoneVtable || !mem::Ptr(zone + 0xf0)) return 0;
        const uintptr_t player = mem::Ptr(zone + 0xf8);
        if (!player || mem::Ptr(player) != image + PlayerVtable || !mem::Ptr(player + 0x80)) return 0;
        return mem::Read(player + 0x98, id) && id ? player : 0;
    }

    // The game's logical UI size; upstream divides the screen size by it to scale its panels.
    inline bool UiSize(uintptr_t image, int32_t& width, int32_t& height) {
        const uintptr_t ui = mem::Ptr(image + UiRoot);
        return ui && mem::Read(ui + widget::Width, width) && mem::Read(ui + widget::Height, height) && width > 0 && height > 0;
    }
}
