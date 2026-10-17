#include "overlay.h"
#include <windows.h>
#include <d3d9.h>
#include "Source/AardvarkUI/ui.h"   // upstream (MIT): UiDrawList and its D3D9 renderer
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Source/native_skin.h"     // upstream (MIT): the game's frame, button and font art from ui.bin
#include "game.h"
#include "journal.h"

// d3d9's Present is hot-patched (`mov edi, edi` + 5 bytes of padding); a vtable patch does not hold on this game's device.

namespace overlay {
namespace {
    using PresentFn = HRESULT (WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    constexpr int PresentSlot = 17;
    constexpr float AtlasPixels = 20;   // the skin font's own size: a `size` px line is NativeSkin::Text at size / 20
    constexpr UiColor Gold = UI_COLOR(251, 187, 6, 255), Body = UI_COLOR(244, 238, 216, 255), Muted = UI_COLOR(185, 172, 143, 255);

    const uintptr_t image = game::Image();
    PresentFn original = nullptr;         // d3d9's Present, past its `mov edi, edi`
    NativeSkin skin;
    IDirect3DDevice9* device = nullptr;   // the device `context` and the skin textures belong to
    UiContext* context = nullptr;
    bool wasDown = false, pressing = false;   // Track button mouse state (render thread)

    SRWLOCK lock = SRWLOCK_INIT;
    View published;
    std::vector<uintptr_t> toggles;       // Track clicks not yet taken by the worker

    // `jmp hook` into the padding, then swap `mov edi, edi` for `jmp $-5` in one 2-byte write.
    bool HotPatch(unsigned char* code, void* hook) {
        DWORD protection = 0;
        if (!VirtualProtect(code - 5, 7, PAGE_EXECUTE_READWRITE, &protection)) return false;
        code[-5] = 0xe9;
        const int32_t relative = int32_t(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(code));
        std::memcpy(code - 4, &relative, 4);
        *reinterpret_cast<volatile uint16_t*>(code) = 0xf9eb;
        VirtualProtect(code - 5, 7, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), code - 5, 7);
        return true;
    }

    // Upstream's GameScale(): screen / the game's logical UI size, else height / 1080 clamped to [1, 2].
    float Scale(UiPoint display) {
        int32_t width = 0, height = 0;
        if (game::UiSize(image, width, height)) return std::min(display.x / width, display.y / height);
        return std::clamp(display.y / 1080.0f, 1.0f, 2.0f);
    }

    void Text(UiDrawList* draw, const std::string& text, float x, float y, float size, UiColor color) {
        const float s = size / AtlasPixels;
        skin.Text(draw, text.c_str(), UiPoint(std::round(x), std::round(y)), UiPoint(), UiPoint(s, s), false, color);
    }
    float Width(const std::string& text, float size) { return NativeSkin::TextWidth(text.c_str(), size / AtlasPixels); }

    // Top right of the screen. Spaced like upstream's damage meter: 23 x 17 px of padding inside the
    // frame, a 20 px heading, then 14 px lines every 20 px; objectives indented 12 px.
    void Panel(UiDrawList* draw, const View& view, UiPoint display, float scale) {
        struct Row { std::string text; UiColor color; float indent; };
        std::vector<Row> rows;
        if (view.lines.empty()) rows.push_back({"Click Track on a quest in your journal", Muted, 0});
        for (const auto& line : view.lines) {
            rows.push_back({line.title, Body, 0});
            for (const auto& detail : line.details) rows.push_back({detail, Muted, 12});
        }
        float width = Width("Quests", 20);
        for (const auto& row : rows) width = std::max(width, row.indent + Width(row.text, 14));
        const UiPoint size((width + 46) * scale, (60 + 20 * rows.size()) * scale);
        const UiPoint at(display.x - 24 - size.x, display.y * 0.22f);
        if (view.background) skin.Frame(draw, at, size, UiPoint(scale, scale));
        float y = at.y + 17 * scale;
        Text(draw, "Quests", at.x + 23 * scale, y, 20 * scale, Gold);
        y += 32 * scale;
        for (const auto& row : rows) {
            Text(draw, row.text, at.x + (23 + row.indent) * scale, y, 14 * scale, row.color);
            y += 20 * scale;
        }
    }

    // The game's mouse in back-buffer pixels, only while a window of this process is in front.
    bool Mouse(UiPoint display, UiPoint& cursor) {
        const HWND window = GetForegroundWindow();
        DWORD process = 0;
        POINT point{};
        RECT client{};
        if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId() ||
            !GetCursorPos(&point) || !ScreenToClient(window, &point) || !GetClientRect(window, &client) ||
            client.right <= 0 || client.bottom <= 0) return false;
        cursor = UiPoint(point.x * display.x / client.right, point.y * display.y / client.bottom);
        return true;
    }

    // Flips the quest in the published view at once, so the label changes this frame, and
    // queues it for the worker, which saves it and publishes the real set.
    void Toggle(uintptr_t quest) {
        AcquireSRWLockExclusive(&lock);
        auto& tracked = published.tracked;
        const auto found = std::find(tracked.begin(), tracked.end(), quest);
        if (found == tracked.end()) tracked.push_back(quest);
        else tracked.erase(found);
        toggles.push_back(quest);
        ReleaseSRWLockExclusive(&lock);
    }

    // The Track button over the journal's Quest Summary page. The mouse is polled, never taken
    // from the game: a click is a press that started on the button and was released on it.
    void TrackButton(UiDrawList* draw, const View& view, UiPoint display, float scale) {
        UiPoint cursor;
        const bool mouse = Mouse(display, cursor);
        const bool down = mouse && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        journal::Button button;
        const bool shown = view.journal && journal::TrackButton(view.journal, button);
        const UiPoint at(button.x * scale, button.y * scale), size(button.width * scale, button.height * scale);
        const bool hovered = shown && mouse && cursor.x >= at.x && cursor.y >= at.y && cursor.x < at.x + size.x && cursor.y < at.y + size.y;
        if (down && !wasDown) pressing = hovered;
        const bool clicked = wasDown && !down && pressing && hovered;
        if (!down) pressing = false;
        wasDown = down;
        if (!shown) return;
        const bool on = std::find(view.tracked.begin(), view.tracked.end(), button.quest) != view.tracked.end();
        skin.CompactButton(draw, at, size, UiPoint(scale, scale), hovered, pressing, on ? "Untrack" : "Track");
        if (clicked) Toggle(button.quest);
    }

    void Draw(IDirect3DDevice9* dev) {
        View view;
        AcquireSRWLockShared(&lock); view = published; ReleaseSRWLockShared(&lock);
        if (SkinFont.empty() || (!view.visible && !view.journal)) return;   // SkinFont: filled by LoadSkinData
        if (device != dev) {   // first frame, or the game recreated its device
            skin.Release();
            if (context) Ui::DestroyContext(context);
            context = Ui::CreateContext(nullptr, dev, nullptr);
            device = dev;
        }
        Ui::SetCurrentContext(context);
        if (!skin.Load(dev) || FAILED(dev->BeginScene())) return;
        Ui::NewFrame();
        const UiPoint display = Ui::GetIO().DisplaySize;
        const float scale = Scale(display);
        UiDrawList* draw = Ui::GetWindowDrawList();
        if (view.visible) Panel(draw, view, display, scale);
        TrackButton(draw, view, display, scale);
        Ui::Render();
        dev->EndScene();
    }

    HRESULT WINAPI HookPresent(IDirect3DDevice9* dev, const RECT* source, const RECT* dest, HWND window, const RGNDATA* dirty) {
        Draw(dev);
        return original(dev, source, dest, window, dirty);
    }
}

bool LoadSkin(const std::wstring& path) { return LoadSkinData(path); }

bool Hook(IDirect3DDevice9* dev) {
    if (original) return true;
    uintptr_t table = 0, present = 0;
    unsigned char bytes[7]{};   // 5 bytes of padding, then `mov edi, edi`
    if (!mem::Read(reinterpret_cast<uintptr_t>(dev), table) || !mem::Read(table + PresentSlot * sizeof(void*), present) ||
        !mem::Read(present - 5, bytes, sizeof(bytes)) || bytes[5] != 0x8b || bytes[6] != 0xff) return false;
    for (int i = 0; i < 5; ++i) if (bytes[i] != 0xcc && bytes[i] != 0x90) return false;
    original = reinterpret_cast<PresentFn>(present + 2);   // set before the patch: the hook can run at once
    if (HotPatch(reinterpret_cast<unsigned char*>(present), reinterpret_cast<void*>(HookPresent))) return true;
    original = nullptr;
    return false;
}

void Publish(View view) {
    AcquireSRWLockExclusive(&lock);
    published = std::move(view);
    ReleaseSRWLockExclusive(&lock);
}

std::vector<uintptr_t> TakeToggles() {
    AcquireSRWLockExclusive(&lock);
    std::vector<uintptr_t> taken = std::move(toggles);
    toggles.clear();
    ReleaseSRWLockExclusive(&lock);
    return taken;
}
}
