<p align="center">
  <img src="The_Simpsons_Hit_%26_Run_Logo.webp" alt="The Simpsons: Hit & Run" width="500">
</p>

# 🍩 D'oh! The Simpsons: Hit & Run for PS Vita

Welcome, and thanks for stopping by! 👋

This is a PS Vita port of *The Simpsons: Hit & Run*, built from the leaked source code. 
This fork is all about making the game run smoother and more reliably on the Vita: better frame rates, fewer hitches while driving, and a few new options you can change right from the in-game Options menu.

> This is a fork of [ZenoArrows' port](https://github.com/ZenoArrows/The-Simpsons-Hit-and-Run). Please read the big **thank-you** section at the bottom, because this project would not exist without them!

---

## 📋 What you need

- 🎮 A PS Vita (or PS TV) with custom firmware
- 💿 The **PC version** of the game (official release, not the assets from the source leak)
- 🔑 `libshacccg.suprx` installed on your console. It is set up during the first run of the [VitaDB Downloader](https://vitadb.rinnegatamante.it/#/info/877), or you can install it separately with [ShaRKBR33D](https://vitadb.rinnegatamante.it/#/info/997).

## 📦 Installation

1. 💻 Install the PC version of the game on your computer.
2. 📂 Copy the contents of the game's installation folder to `ux0:/data/simpsons` on your Vita.
   - Use the original `.rmv` movie files in the `movies` folder (not converted `.bk2` files).
3. ⬇️ Download the `.vpk` from the [Releases](https://github.com/Osaul4ik/The-Simpsons-Hit-and-Run/releases) page of this repository, copy it to your Vita and install it with [VitaShell](https://github.com/TheOfficialFloW/VitaShell).
4. 🎉 Launch the game and have fun!

## 🌍 Languages

The PAL version supports several languages and uses the system language of your console. If your language is not supported, a menu lets you pick one of the supported languages.

No official release contains the dialog files for all four languages, so make sure the game assets you use are localized in the language you want to play. If you just want English, use the NTSC version.

---

## ⚙️ Options menu

Open **Options** in the game to change these. They are remembered between launches.

| Option | Choices | Notes |
|---|---|---|
| **Frame rate** | 30 / 50 / 60 fps | The game always starts at 30 fps and switches to your choice when the intro movie ends. 30 and 60 are synced to the screen. 50 is not, so you may notice some tearing. |
| **MSAA** | Off / 2x | Smoother edges. Takes effect after restarting the game. |
| **Filtering** | Low / Medium / High | How sharp textures stay in the distance (roads especially). High is the default. Takes effect after restarting the game. |
| **Animation** | Low / Medium / High | How far from you pedestrians get full-rate animation: 25 m / 38 m / 50 m. Applies immediately. |
| **Compile shaders** | | Shows the shader status and lets you compile the optimized shaders. |

## 🧪 Advanced settings (`vita.cfg`)

Want to fine tune things? Create a text file at `ux0:/data/simpsons/vita.cfg` with one `key=value` per line. Missing file or missing key means the default. The most useful keys:

| Key | Default | What it does |
|---|---|---|
| `fps` | `30` | `30`, `50` or `60` (other values go to the nearest). |
| `msaa` | `2` | `0` = off, `2` = 2x. |
| `filtering` | `2` | `0` low, `1` medium, `2` high. |
| `npcdist` | `38` | Metres within which pedestrians are fully animated (the original game uses 25). |
| `smalldist` | `80` | Distance in metres up to which small props (hydrants and the like) are drawn. |
| `zonecache` | `32` | How many zones stay in memory after you leave them (`0` = off). |
| `zonepreload` | `1` | Load all outside zones of a level behind the loading screen, so nothing streams while you drive. `0` = faster level start, streaming on the way. |
| `pedlimit` | `7` | Most pedestrians at once (0 to 7). |
| `log` | `0` | `1` writes `ux0:/data/simpsons/log.txt`. Handy for bug reports. 🐛 |

There are more keys (culling margins, particle budget, sound thread and others). The full list with explanations is in the comment at the top of [`code/main/win32main.cpp`](code/main/win32main.cpp).

---

## 🔨 Building

The project uses CMake and the VitaSDK. See the options documented in `CMakeLists.txt` (for example `SRR2_REGION` to pick `NTSC` or `PAL`).

---

## 💛 Huge thanks

This fork stands on the shoulders of two amazing people. 

### 🥇 ZenoArrows, author of the original port

Bringing *The Simpsons: Hit & Run* to the PS Vita (and the Nintendo Switch) from a pile of leaked source code is an enormous achievement. ZenoArrows wrote the **GXM renderer** from scratch, built the Vita file and memory layers, rebuilt the build system and fixed countless issues along the way. All of that was released openly for everyone to enjoy and build on. 🌟

Every single thing in this fork sits on top of that foundation. 

👉 Original project: [ZenoArrows/The-Simpsons-Hit-and-Run](https://github.com/ZenoArrows/The-Simpsons-Hit-and-Run)

### 🥈 Strict-Top6935, author of the Old 3DS port

Making a game like this run on an Old 3DS takes serious skill and patience. The cheaper pedestrian handling in this fork was inspired by their Old 3DS port, and the lessons from squeezing the game onto such small hardware were a great help in thinking about how to make the Vita version lighter too. A big thank-you for the clever work and for sharing it! 🙌

👉 [u/Strict-Top6935](https://www.reddit.com/user/Strict-Top6935/)

---

*The Simpsons: Hit & Run* and all related content belong to their respective owners. This is a non-commercial fan project and you need your own copy of the game to use it. 🍩
