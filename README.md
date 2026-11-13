# Naruto Shippuden: Ultimate Ninja Storm 2 — PS Vita Port

Port of **Naruto Shippuden: Ultimate Ninja Storm 2** (Xbox 360 version) to **PlayStation Vita** / **PS TV** via **HENkaku** / **Vita3K**.

> ⚠️ **This repository contains only the port source code and build scripts.**  
> The original game assets are **not included** — you must own a legal copy of the game.

---

## 📥 Download

**Latest release:** [v1.0.0](../../releases/tag/v1.0.0)  
→ Download `storm2_vita.vpk` from the Assets section.

Install the `.vpk` on your Vita using **VitaShell** or on Vita3K via **Install VPK**.

---

## 🎮 Features

- Full story mode port
- Battle system adapted for Vita controls
- Optimized textures and shaders for Vita GPU (GXM)
- Runs on **PS Vita (HENkaku/Ensō)** and **Vita3K** emulator
- Supports **PS TV** (Vita TV)

---

## 🛠 Building from Source

### Requirements

- **VitaSDK** (vitasdk / vdpm)
- **CMake** ≥ 3.20
- **Ninja** build system
- Original game assets (extracted from Xbox 360 version)

### Build Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/Kruvyy/naruto-storm2-vita.git
cd naruto-storm2-vita

# Configure (adjust paths to your extracted game assets)
cmake -B vita/storm2/build-vita -S vita/storm2 \
  -DCMAKE_TOOLCHAIN_FILE=${VITASDK}/share/vita.toolchain.cmake \
  -DGAME_DATA_PATH=/path/to/extracted/storm2/game

# Build
cmake --build vita/storm2/build-vita --config Release

# Output: vita/storm2/build-vita/storm2_vita.vpk
```

---

## 📁 Project Structure

```
├── storm2/              # Game port source (reverse-engineered)
│   ├── src/             # Engine & gameplay code
│   ├── vita/            # Vita-specific backend (GXM, input, audio)
│   └── CMakeLists.txt
├── vita/                # VitaSDK tooling & runtime
│   ├── common/          # Shared Vita utilities
│   ├── runtime/         # vitaGL, SDL2, etc.
│   └── storm2/          # Storm 2 Vita build config
├── rexglue/             # PowerPC → ARM translation layer (submodule)
├── tools/               # Asset conversion & build tools
└── docs/                # Technical documentation & research logs
```

---

## 🎯 Controls (Vita)

| Action | Button |
|--------|--------|
| Move | Left Stick |
| Camera | Right Stick |
| Light Attack | □ |
| Heavy Attack | △ |
| Jump | ✕ |
| Guard / Block | ○ |
| Substitution / Dash | L / R |
| Chakra Charge | Hold △ |
| Ultimate Jutsu | L + R |
| Pause / Menu | START |

---

## 📜 Credits

- **Original Game**: CyberConnect2 / Bandai Namco
- **Port Authors**: [Kruvyy](https://github.com/Kruvyy)
- **VitaSDK / vitaGL / vdpm**: Vita homebrew community
- **ReXGlue**: PPC dynamic recompilation

---

## ⚖️ Legal

This is a **fan-made port** for educational and preservation purposes.  
No copyrighted game assets are distributed in this repository.  
You must own a legitimate copy of *Naruto Shippuden: Ultimate Ninja Storm 2* to play.

---

## 📄 License

Source code: **MIT License** — see [LICENSE](LICENSE)  
Game assets: **Property of their respective owners** (not included)