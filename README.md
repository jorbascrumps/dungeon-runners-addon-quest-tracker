> [!WARNING]
> This addon is in **early development** and should be considered **experimental**. Expect frequent changes and breaking updates.
>
> Feedback is welcome. Track progress and open issues here: [GitHub Issues](https://github.com/jorbascrumps/dungeon-runners-addon-quest-tracker/issues)

# Dungeon Runners Quest Tracker

Keeps pinned quests and their progress in view while you play.

<img width="1280" height="705" alt="A sample screen of the Quest Tracker addon" src="https://github.com/user-attachments/assets/fec02158-678f-4207-b2d0-4eb1fa914a1e" />

## Installation

1. Install [Dungeon-Runners-Addons](https://github.com/MaxmilianBaron/Dungeon-Runners-Addons) if you haven't already. Quest Tracker runs on its loader.
2. Download the [latest release](https://github.com/jorbascrumps/dungeon-runners-addon-quest-tracker/releases/latest).
3. Extract it into your game's `Addons` folder, so you end up with `Addons/QuestTracker/Addon.dll`.
4. Start the game.

## Usage

### Tracking a quest

Open your journal, select a quest and click **Track**. The quest and its objectives appear in a panel on the right of your screen. Click **Untrack** to remove it.

Tracked quests are remembered between sessions.

### Settings

Open **ESC > Addons > Quest Tracker**.

| Setting | Default | Description |
|---|---|---|
| Enabled | On | Show the quest panel and the journal's Track button. Off: the addon reads and draws nothing. |
| Panel background | On | Draw the game-style frame behind the panel. Turn off to show only the text. |

## Building from source

Requires CMake and a 32-bit MinGW toolchain.

```bash
git clone --recurse-submodules https://github.com/jorbascrumps/dungeon-runners-addon-quest-tracker.git
cd dungeon-runners-addon-quest-tracker
cmake -B build && cmake --build build
```

The installable folder is written to `dist/QuestTracker`.

## Credits

Built on [Dungeon-Runners-Addons](https://github.com/MaxmilianBaron/Dungeon-Runners-Addons) by [@MaxmilianBaron](https://github.com/MaxmilianBaron), whose UI code and art loader are compiled into this addon under the MIT license.
