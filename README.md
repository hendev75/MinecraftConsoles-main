# MinecraftConsoles

![Tutorial World](.github/TutorialWorld.png)

## Introduction

This project contains the source code of Minecraft Legacy Console Edition v1.6.0560.0 (TU19) from https://archive.org/details/minecraft-legacy-console-edition-source-code, with some fixes and improvements applied.


## Platform Support

- **Windows**: Supported for building and running the project
- **macOS / Linux**: The Windows nightly build may run through Wine or CrossOver based on community reports, but this is unofficial and not currently tested by the maintainers

## Features
- Fixed FOV sprinting bug.
- Working on PS4 native support
- Added Hard core game mode.
## Multiplayer

Basic LAN multiplayer is available on the Windows build

- Hosting a multiplayer world automatically advertises it on the local network
- Other players on the same LAN can discover the session from the in-game Join Game menu
- Game connections use TCP port `25565` by default
- LAN discovery uses UDP port `25566`
- Add servers to your server list with `servers.txt` (temp solution)
- Rename yourself without losing data by keeping your `uid.dat`

Parts of this feature are based on code from [LCEMP](https://github.com/LCEMP/LCEMP) (thanks!)

### servers.txt

To add a server to your game, create the `servers.txt` file in the same directory as you have `Minecraft.Client.exe`. Inside, follow this format:
```
serverip.example.com
25565
The name of your server in UI!
```

For example, here's a valid servers.txt
```
1.1.1.1
25565
Cloudflare's Very Own LCE Server
127.0.0.1
25565
Localhost Test Crap
```

### Launch Arguments

| Argument           | Description                                                                                         |
|--------------------|-----------------------------------------------------------------------------------------------------|
| `-name <username>` | Sets your in-game username.                                                                         |
| `-fullscreen`      | Launches the game in Fullscreen mode                                                                |

Example:
```
Minecraft.Client.exe -name Steve -fullscreen
```

## Controls (Keyboard & Mouse)

- **Movement**: `W` `A` `S` `D`
- **Jump / Fly (Up)**: `Space`
- **Sneak / Fly (Down)**: `Shift` (Hold)
- **Sprint**: `Ctrl` (Hold) or Double-tap `W`
- **Inventory**: `E`
- **Chat**: `T`
- **Drop Item**: `Q`
- **Crafting**: `C` Use `Q` and `E` to move through tabs (cycles Left/Right)
- **Toggle View (FPS/TPS)**: `F5`
- **Fullscreen**: `F11`
- **Pause Menu**: `Esc`
- **Attack / Destroy**: `Left Click`
- **Use / Place**: `Right Click`
- **Select Item**: `Mouse Wheel` or keys `1` to `9`
- **Accept or Decline Tutorial hints**: `Enter` to accept and `B` to decline
- **Game Info (Player list and Host Options)**: `TAB`
- **Toggle HUD**: `F1`
- **Toggle Debug Info**: `F3`
- **Open Debug Overlay**: `F4`
- **Toggle Debug Console**: `F6`

## Build & Run

1. Install [Visual Studio 2022](https://aka.ms/vs/17/release/vs_community.exe).
2. Clone the repository.
3. Open the project by double-clicking `MinecraftConsoles.sln`.
4. Make sure `Minecraft.Client` is set as the Startup Project.
5. Set the build configuration to **Debug** (Release is also OK but has some bugs) and the target platform to **Windows64**, then build and run.

## Known Issues

- Native builds for platforms other than Windows have not been tested and are most likely non-functional. The Windows nightly build may still run on macOS and Linux through Wine or CrossOver, but that path is unofficial and not currently supported
