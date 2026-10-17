#include "quests.h"
#include "game.h"
#include <algorithm>

namespace {
    const uintptr_t image = game::Image();
    constexpr unsigned ObjectiveLimit = 16;   // quests have 1-5; stops a bad list early
    constexpr unsigned HeightLimit = 64;      // red-black tree of at most SetLimit nodes
    constexpr uint32_t SetLimit = 1000;
    // MSVC's std::set node: {left, parent, right, value, colour, isnil}. The head node's left is the
    // leftmost node, its parent the root; leaves point back at the head (log 16).
    constexpr uintptr_t Left = 0, Parent = 4, Right = 8, Value = 0xc;

    // In-order successor, as MSVC's tree iterator does it; 0 or `head` when the walk ends.
    uintptr_t Next(uintptr_t node, uintptr_t head) {
        unsigned steps = 0;
        const uintptr_t right = mem::Ptr(node + Right);
        if (right != head) {
            for (node = right; mem::Ptr(node + Left) != head && steps++ < HeightLimit;) node = mem::Ptr(node + Left);
            return node;
        }
        uintptr_t parent = mem::Ptr(node + Parent);
        while (parent != head && node == mem::Ptr(parent + Right) && steps++ < HeightLimit) { node = parent; parent = mem::Ptr(parent + Parent); }
        return parent;
    }

    std::vector<std::string> Objectives(uintptr_t quest) {
        std::vector<std::string> texts;
        const uintptr_t table = mem::Ptr(quest + game::quest::ObjectiveTexts);   // entry i = objective i
        uintptr_t objective = mem::Ptr(quest + game::quest::FirstObjective);
        for (unsigned i = 0; objective && i < ObjectiveLimit; ++i, objective = mem::Ptr(objective + game::quest::ObjectiveNext)) {
            const std::string text = mem::String(table + i * game::quest::ObjectiveTextStride);
            if (!text.empty()) texts.push_back(text);
        }
        return texts;
    }

    Quest Read(uintptr_t address) {
        Quest quest;
        quest.address = address;
        mem::Read(address + game::quest::Id, quest.id);
        quest.title = mem::String(mem::Ptr(address + game::quest::Description) + game::quest::Title);
        quest.objectives = Objectives(address);
        // Same walk as upstream's Wishing Well tracker: the first quest in the base chain with a definition path.
        uintptr_t current = address;
        for (unsigned depth = 0; current && depth < 8 && quest.path.empty(); ++depth) {
            quest.path = mem::String(mem::Ptr(current + game::quest::Definition) + game::quest::DefinitionPath);
            const uintptr_t base = mem::Ptr(current + game::quest::Base);
            if (base == current) break;
            current = base;
        }
        return quest;
    }
}

bool ScanQuests(uintptr_t player, std::vector<Quest>& quests) {
    quests.clear();
    const uintptr_t manager = mem::Ptr(player + game::PlayerManager);
    const uintptr_t head = mem::Ptr(manager + game::QuestSet);
    uint32_t size = 0;
    if (!manager || mem::Ptr(manager + game::ManagerOwner) != player || !head || !mem::Read(manager + game::QuestSetSize, size) || size > SetLimit) return false;
    uintptr_t node = mem::Ptr(head + Left);
    for (uint32_t i = 0; i < size && node && node != head; ++i, node = Next(node, head)) {
        const uintptr_t quest = mem::Ptr(node + Value);
        if (mem::Ptr(quest) == image + game::QuestVtable && mem::Ptr(quest + game::quest::Manager) == manager) quests.push_back(Read(quest));
    }
    std::sort(quests.begin(), quests.end(), [](const Quest& a, const Quest& b) { return a.id < b.id; });
    return !quests.empty();
}
