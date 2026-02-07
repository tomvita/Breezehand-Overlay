# Breezehand Overlay

<img width="480" height="1024" alt="image" src="https://github.com/user-attachments/assets/0377e731-0f4c-4c19-8cd6-2d719c49da4b" />

<p align="center">
  <a href="https://gbatemp.net/forums/nintendo-switch.283/?prefix_id=44"><img src="https://img.shields.io/badge/platform-Switch-898c8c?logo=C++.svg" alt="platform"></a>
  <a href="https://github.com/topics/cpp"><img src="https://img.shields.io/badge/language-C++-ba1632?logo=C++.svg" alt="language"></a>
  <a href="https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html"><img src="https://img.shields.io/badge/license-GPLv2-189c11.svg" alt="GPLv2 License"></a>
  <a href="https://github.com/tomvita/Breezehand-Overlay/releases/latest"><img src="https://img.shields.io/github/v/release/tomvita/Breezehand-Overlay?label=latest&color=blue" alt="Latest Version"></a>
</p>

<p align="center">
  <strong>A powerful Nintendo Switch overlay for managing cheats with an enhanced UI, built on libbreezehand.</strong>
</p>

## Features

### 🎨 Enhanced UI
Unlike traditional overlays that display only a single scrolling line (making long descriptions hard to read), Breezehand provides:
- **Variable-Size Display Boxes**: Automatically adjusts box height based on content
- **Multi-line Text Support**: Properly formatted text wrapping for long cheat descriptions
- **Variable Font Sizes**: Customize font size per game for optimal readability
- **Additional Notes Support**: Display detailed cheat notes alongside descriptions
- **Per-Game Settings**: Each game can have custom UI configurations:
  - Font size (some games need to display many cheats, others have long descriptions)
  - Box sizing (adapts to the amount of content)
  - Notes visibility (toggle notes on/off depending on user's needs)

### 🎮 Advanced Cheat Management

#### Automatic Cheat Downloads with Versioning
Breezehand can automatically download cheats from popular cheat repositories. The versioning system works as follows:

1. **Base URL Configuration**: Add cheat source URLs to `sdmc:/config/breezehand/cheat_url_txt`
   - URLs can include `{TITLE_ID}` placeholder which will be replaced with the game's title ID
   - The `{TITLE_ID}` placeholder can appear anywhere in the URL path
2. **Automatic Detection**: Detects the running game's Title ID and Build ID
3. **Smart Version Checking**: 
   - Replaces `{TITLE_ID}` placeholder in the configured URL
   - Attempts to download the base version: `{BUILD_ID}.txt`
   - If successful, checks for versioned cheats: `{BUILD_ID}.v1.txt`, `{BUILD_ID}.v2.txt`, etc.
   - Continues checking up to `.v15.txt` until a version is not found
   - Downloads the highest available version automatically
4. **Multi-Source Support**: Cycle through multiple configured URLs by pressing "Download from URL" repeatedly

This allows cheat authors to publish updated cheat versions without breaking compatibility, and users always get the latest available version.

#### Organization & Usability
- **Folder Support**: Organize cheats into folders when dealing with many cheats
- **Nested Folders**: Support for multi-level folder hierarchies to keep large cheat collections organized
- **Conditional Key Programming**: Program conditional keys for cheat activation (advanced cheat control)
- **Toggle Cheats**: Enable/disable individual cheats on the fly
- **Cheat Notes**: View detailed notes from `notes.txt` files for documentation and usage instructions

## Installation

1. **Prerequisites**:
   - Nintendo Switch running custom firmware (Atmosphere recommended)
   - [nx-ovlloader](https://github.com/ppkantorski/nx-ovlloader) installed

2. **Choose Your Installation Method**:

   With Ultrahand Overlay Manager** :
   - Install [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) (`ovlmenu.ovl`) for overlay management
   - Download `breezehand.ovl` from [Releases](https://github.com/tomvita/Breezehand-Overlay/releases/latest)
   - Place `breezehand.ovl` in `/switch/.overlays/` on your SD card
   - Access Breezehand through the Ultrahand overlay menu

3. **Configuration** (Optional):
   - Create `sdmc:/config/breezehand/cheat_url_txt` to configure cheat download sources
   - Add one URL per line pointing to cheat repositories

4. **Launch**:
   - Press the overlay hotkey (default: `ZL+ZR+DDOWN`)
   - Breezehand will appear as your overlay menu

## Usage

### Cheat Menu
1. Launch a game
2. Open Breezehand overlay
3. Navigate to the Cheat Menu
4. Press **A** to toggle individual cheats or enter folders
5. Press **X** to open Cheat Settings:
   - **Download from URL**: Download cheats from configured sources (press multiple times to cycle through URLs)
   - **Font Size**: Adjust font size for this game
   - **Box Height**: Customize display box height
   - **Toggle Notes**: Show/hide additional cheat notes

### Cheat URL Configuration
Create `sdmc:/config/breezehand/cheat_url_txt` with your cheat sources (one URL per line):

> **Note**: A template file with example URLs is included in the release. See the included `cheat_url_txt` template for reference.
```
https://example.com/cheats/{TITLE_ID}/
https://cdn.example.com/{TITLE_ID}/atmosphere/
https://github.com/user/repo/raw/main/
```

**How Versioning Works:**
When you press "Download from URL", the overlay:
1. Detects your game's title ID and build ID (e.g., title: `0100ABC001234000`, build: `A1B2C3D4E5F60708`)
2. Replaces `{TITLE_ID}` in your configured URL (can appear anywhere in the URL)
3. Tries to download the base version: `{BUILD_ID}.txt` (e.g., `A1B2C3D4E5F60708.txt`)
4. If successful, checks for newer versions: `{BUILD_ID}.v1.txt`, `{BUILD_ID}.v2.txt`, `{BUILD_ID}.v3.txt`... up to `.v15.txt`
5. Downloads the highest version found
6. Saves to `sdmc:/switch/breeze/cheats/{TITLE_ID}/`

**Directory Structure:**
- **Breeze Directory**: `sdmc:/switch/breeze/cheats/{TITLE_ID}/` - Where cheats are downloaded and managed
  - This is where Breezehand stores all cheats, notes, and folders
  - Less crowded than Atmosphere's contents folder
  - Notes (`notes.txt`) only work in this directory
  - **Important**: Cheats stored here are preserved during Atmosphere updates (when contents directory needs to be wiped)
- **Atmosphere Directory**: `sdmc:/atmosphere/contents/{TITLE_ID}/cheats/` - Where cheats auto-load at game startup
  - Cheats here are automatically loaded by Atmosphere when the game starts
  - No user interaction needed for cheats to activate
  - **Note**: This directory is often recommended to be wiped during major Atmosphere updates

**Save Buttons:**
Breezehand provides two save options:
1. **Save** - Saves your cheat toggles to the Breeze directory
   - Creates a toggle file that remembers which cheats are enabled
   - Cheats are automatically toggled on when you open the menu
2. **Save to AMS** - Copies enabled cheats to Atmosphere directory
   - Saves enabled cheats to `sdmc:/atmosphere/contents/{TITLE_ID}/cheats/`
   - Also creates a toggle file for automatic activation
   - Use this when you want cheats to auto-load at game startup without opening the overlay

**Example URL Configuration:**
```
https://example.com/cheats/{TITLE_ID}/
https://cdn.example.com/{TITLE_ID}/atmosphere/
https://github.com/user/repo/raw/main/
```

### Organizing Cheats with Folders (For Cheat Authors)
For games with many cheats, cheat authors can organize them into folders using special folder markers within the cheat file:

```
[Infinite Health]
04000000 XXXXXXXX YYYYYYYY

[Gameplay Cheats]20000000

[Max Stats]
04000000 XXXXXXXX YYYYYYYY

[God Mode]
04000000 XXXXXXXX YYYYYYYY

[End Gameplay]20000001

[Item Cheats]20000000

[All Weapons]
04000000 XXXXXXXX YYYYYYYY

[Consumables]20000000

[Infinite Potions]
04000000 XXXXXXXX YYYYYYYY

[End Consumables]20000001

[End Items]20000001
```

**Folder Syntax:**
- `[FolderName]20000000` - Starts a folder with the specified name
- `[AnyText]20000001` - Ends the current folder (the text in brackets doesn't matter)
- Folders can be nested by using multiple folder start markers before closing them
- Regular cheats `[CheatName]` followed by cheat codes appear within folders or at the root level

Folders appear in the Breezehand overlay menu and can be nested for complex cheat collections.

## For Cheat Creators

> [!IMPORTANT]
> **Memory Usage Considerations**: Ultrahand/Breezehand consumes significant memory. When developing cheats with dmnt (gen1 and gen2) sysmodules loaded, many games may experience memory-related issues or instability.
> 
> **Recommendation**: During cheat development, use [Breeze](https://github.com/tomvita/Breeze-Beta) without Breezehand/Ultrahand overlay to ensure maximum available memory for the game and cheat engine. Once your cheats are finalized, you can use Breezehand for convenient cheat management during normal gameplay.

## Building from Source

### Prerequisites
- [devkitPro](https://devkitpro.org/) with devkitA64
- libnx
- switch-curl
- switch-zlib
- switch-mbedtls
- [libultrahand](https://github.com/ppkantorski/libultrahand) (included as submodule)

### Compilation
```bash
git clone --recursive https://github.com/tomvita/Breezehand-Overlay.git
cd Breezehand-Overlay
make
```

The compiled `ovlmenu.ovl` will be in the project directory.

## Project Structure
```
Breezehand-Overlay/
├── source/
│   └── main.cpp          # Main overlay implementation
├── lib/
│   └── libultrahand/     # Core library (submodule)
├── Makefile
└── README.md
```

## Credits

- **Original Ultrahand Overlay**: [ppkantorski](https://github.com/ppkantorski/Ultrahand-Overlay)
- **libtesla**: [WerWolv](https://github.com/WerWolv/libtesla)
- **libultrahand**: [ppkantorski](https://github.com/ppkantorski/libultrahand)

## Contributing

Contributions are welcome! Feel free to:
- Report bugs via [Issues](https://github.com/tomvita/Breezehand-Overlay/issues)
- Submit pull requests with improvements
- Suggest new features

## License

This project is licensed under GPLv2. See [LICENSE](LICENSE) for details.

Copyright (c) 2026 tomvita

---

**Note**: This software is for educational and research purposes. Use at your own risk. Running homebrew software may void your warranty.
