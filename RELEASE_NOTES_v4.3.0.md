# 🚀 Fyzenor v4.3.0 — Official Stable Release

> **The modern, blazing-fast terminal file manager built in C++17 with live previews, Lua extensibility, native Neovim integration, and asynchronous workflows.**

---

## 🌟 Overview & Release Highlights

We are thrilled to announce the official stable release of **Fyzenor v4.3.0**! 

This landmark milestone consolidates months of active development, incorporating all features, architectural upgrades, and optimizations from the `v4.3.0-beta` cycle into an ultra-reliable, production-grade release. Fyzenor v4.3.0 bridges terminal efficiency and modern desktop ergonomics, bringing native Neovim integration, an embedded Lua plugin runtime, interactive visual disk usage inspection, flicker-free GPU-accelerated image previews across modern terminals, dynamic Yazi-inspired cursor tracking, and complete 256-color palette fidelity.

### ⚡ Highlights at a Glance
- 🖼️ **2D Grid View Mode (`Shift+V` / `V`)**: Switch seamlessly from 3-column Miller mode to an adaptive 2D card grid with high-resolution aspect-ratio letterboxed thumbnails, directional 2D navigation, and ANSI TrueColor fallback.
- 🌌 **Native Neovim Plugin (`fyzenor.nvim`)**: Floating modal TUI, netrw directory hijacking, multi-buffer placement, split modes (`Ctrl+V`, `Ctrl+X`), and automatic CWD synchronization.
- 🧩 **Embedded Lua Plugin Engine**: Sandboxed Lua runtime, custom keybindings, official plugin repository (`zoxide`, `git`, `json`), and C++ API bindings.
- 📊 **Visual Disk Usage Analyzer (`U`)**: Single-key interactive `ncdu`/`gdu` mode with proportional UTF-8 bar graphs, non-blocking background scanner, and symlink recursion guards.
- 🖼️ **Kitty Graphics Protocol & Multi-Terminal Engine**: Crisp media previews with DEC Mode 2026 atomic frame sync, GPU memory-safe texture pruning, and LRU session caching across **Kitty**, **Ghostty**, and **WezTerm**.
- 🎯 **Yazi-Style Cursor Tracking Across Sorting (`s`)**: Cursor stays locked onto your selected item when cycling sort modes, even while background size calculations run.
- 🧭 **Per-Directory Cursor Memory (`dirCursorHistory`)**: Remembers cursor positions and scroll offsets across directory jumps, parent navigations, and history stacks (`Ctrl+O` / `Ctrl+P` / `H`).
- 🎨 **Universal 256-Color Engine (`hexTo256`)**: Euclidean RGB color matching ensures consistent theme rendering across `tmux`, Neovim terminal (`:terminal`), and lightweight emulators without washed-out palettes.
- ✨ **Unified Item Creation (`n`)**: Dynamic trailing-slash detection (`name` vs `name/`), centered modal dialog, and real-time extension-based Nerd Font icons & syntax coloring.
- 🖱️ **Context-Aware Mouse Scrolling**: Pane-aware wheel hovering over file lists, parent panes, pinned bookmarks, and preview viewports.
- 󰊢 **Git & Lazygit Integration (`Ctrl+G`)**: Native shortcut with centered 85% floating popup overlay when running inside `tmux`.
- 📋 **Modal Clipboard Pasting & Input Hardening**: System clipboard paste (`Ctrl+V` / bracketed paste), Readline shortcuts (`Ctrl+U`, `Ctrl+W`), and UTF-8 multibyte boundary safety.
- 🛠️ **Modernized Tooling**: Updated `install.sh` with automated dependency verification and a dedicated standalone `uninstall.sh`.

---

## 🔍 What's New in v4.3.0

### 1. 🖼️ 2D Grid View & Visual Media Explorer (`Shift+V` / `V`)
Fyzenor introduces a native **2D Grid View** designed for fast, beautiful visual exploration of photos, wallpapers, video clips, and directories:
* **Dynamic Card Grid Layout**: Press <kbd>V</kbd> (Shift+V) or click the `[󰕰 Grid: V]` header badge to instantly switch between 3-column Miller mode and an adaptive 2D thumbnail card grid that automatically packs cards into rows and columns based on your terminal width (`cardW = 16`, `cardH = 7`).
* **True Aspect-Ratio Letterboxing**: Generates high-resolution 280x160 RGBA letterboxed Kitty image thumbnails with automatic padding. Eliminates stretched or squished thumbnails across portrait, landscape (16:9, 4:3), and square media.
* **Universal 24-Bit TrueColor ANSI Fallback**: In terminals without Kitty graphics support, Fyzenor generates a 4-row Unicode half-block (`▀`) fallback with TrueColor fidelity (`\033[38;2;...;48;2;...m`).
* **High-Visibility Selection Prominence**: Active cards feature bold double-line borders (`╔═◆═╗`), a centered selection diamond (`◆`), and a full-width filename pill (`▸ name ◂`) with high-contrast background highlights.
* **Zero-Flicker Focus Transitions**: Direct Kitty graphics placements are tracked per terminal cell, eliminating premature clearing or blanking when jumping focus between the Pinned menu and Grid View.
* **Safe In-Memory Generation for Cache & Trash**: Viewing `~/.cache/fyzenor/previews` or `~/.local/share/Trash` generates and renders thumbnails completely in memory (or directly reads existing cache files) without creating recursive disk cache files or triggering inotify reload loops.
* **2D Directional Navigation**: Intuitive navigation with <kbd>h</kbd> (left), <kbd>j</kbd> (down), <kbd>k</kbd> (up), <kbd>l</kbd> (right), arrow keys, <kbd>Home</kbd>/<kbd>End</kbd>, <kbd>PgUp</kbd>/<kbd>PgDn</kbd>, and mouse clicks.

---

### 2. 🌌 Native Neovim Integration (`fyzenor.nvim`)
Fyzenor now includes a first-class Neovim plugin inspired by *yazi.nvim*, allowing you to use Fyzenor directly inside Neovim as an ultra-fast file picker, floating manager, and complete `netrw` replacement:

* **Floating Modal Window**: Spawns in a centered floating window with customizable dimensions, borders (`rounded`, `single`, `double`), and background dimming.
* **Netrw Directory Hijacking**: Set `open_for_directories = true` to automatically replace netrw when opening directories (`nvim .` or `:edit dir/`).
* **Multi-Buffer & Split Loading**: Select multiple files in Fyzenor with <kbd>Tab</kbd> or <kbd>Space</kbd> and open them across splits (<kbd>Ctrl+V</kbd> vertical, <kbd>Ctrl+X</kbd> horizontal), new tabs (<kbd>Ctrl+T</kbd>), or Neovim's quickfix list (<kbd>Ctrl+Q</kbd>).
* **Automatic CWD Synchronization**: Optionally updates Neovim's working directory (`:cd`) to match your Fyzenor navigation upon exit.
* **Quick Neovim Setup (`lazy.nvim`)**:
  ```lua
  {
    "Bimbok/fyzenor",
    event = "VeryLazy",
    opts = {
      open_for_directories = true,
      change_neovim_cwd_on_close = true,
      floating_window_scaling_factor = 0.9,
      border = "rounded",
    },
    keys = {
      { "<leader>e", "<cmd>Fyzenor<cr>", desc = "Open Fyzenor (current file)" },
      { "<leader>E", "<cmd>Fyzenor cwd<cr>", desc = "Open Fyzenor (project root)" },
      { "<leader>fe", "<cmd>FyzenorToggle<cr>", desc = "Toggle Fyzenor floating modal" },
    },
  }
  ```

---

### 2. 🧩 Embedded Lua Plugin Engine & Official Plugins
Fyzenor features an embedded Lua scripting engine, enabling users to extend file manager behavior, bind custom key shortcuts, query shell commands, and interact with the TUI without recompiling C++ code.

* **Sandboxed Runtime**: Plugins are loaded automatically on boot from `~/.config/fyzenor/plugins/*/init.lua`.
* **Exposed C++ API**:
  - `fyzenor.prompt(title, default)`: Prompts the user with an interactive centered modal dialog.
  - `fyzenor.change_directory(path)`: Immediately updates the active tab path and reloads the file tree.
  - `fyzenor.shell_output(cmd)`: Executes a shell command synchronously and captures stdout.
  - `fyzenor.set_status(msg, color)`: Displays transient or persistent status bar messages.
  - `fyzenor.get_version()`: Returns current Fyzenor semantic version string.
* **Official Plugins Repository**:
  Install plugins with a single command:
  ```bash
  git clone https://github.com/Bimbok/fyzenor-plugins.git ~/.config/fyzenor/plugins
  ```
  - **`zoxide`**: Interactive modal fast-jump navigation (<kbd>z</kbd> / <kbd>Alt+Z</kbd>).
  - **`git`**: Git branch status summary (<kbd>Ctrl+B</kbd>), staging toggle (<kbd>Ctrl+S</kbd>), and diff statistics (<kbd>Ctrl+K</kbd>).
  - **`json_previewer`**: Custom syntax formatting and validation for `.json` files.

---

### 3. 📊 Visual Disk Usage & Storage Visualizer (`U`)
Transform any directory listing into an interactive storage visualizer (inspired by `ncdu` and `gdu`):

* **Single-Key Toggle**: Press <kbd>U</kbd> in normal mode to activate disk usage analysis for the current directory.
* **Proportional UTF-8 Bar Graphs**: Displays live unicode meters (`[██████░░░░] 64.2%`) alongside human-readable byte sizes.
* **Non-Blocking Background Traversal**: Recursive folder sizing runs on isolated C++ worker threads; the interface remains completely responsive during scans.
* **Loop & Cycle Protection**: Employs inode-level tracking and skips symlinks during deep scans to prevent infinite recursion hangs on complex filesystems.
* **Inotify Safe**: Automatic filesystem reload triggers are decoupled from scanner writes, preventing reload feedback loops.

---

### 4. 🖼️ Kitty Graphics Protocol & Multi-Terminal Media Engine
High-resolution media previewing is now hardened across top modern terminal emulators:

* **Supported Terminals**: First-class zero-flicker previews on **Kitty**, **Ghostty**, and **WezTerm**.
* **Atomic Frame Synchronization**: Leverages DEC Mode 2026 (`\033[?2026h` / `\033[?2026l`) to batch terminal draw calls into single atomic updates, completely eliminating screen tearing and cursor jumping.
* **GPU Memory-Safe Lifecycle**: Employs targeted Kitty graphics commands (`a=T,i=1`) to swap images in-place and ID-based pruning (`d=A`) to release GPU textures, preventing terminal memory growth and freezing.
* **LRU Session Cache**: Extracted thumbnails are cached with an LRU eviction strategy for instantaneous recall when navigating back and forth.
* **Graceful Fallbacks**: In terminals without graphics support (e.g. Alacritty, Foot, xterm), Fyzenor gracefully falls back to detailed metadata inspection (`mediainfo`/`ffprobe`) and syntax-highlighted text.

---

### 5. 🎯 Yazi-Style Cursor Tracking & Navigation Memory
* **Locked Cursor Tracking Across Sorting (<kbd>s</kbd>)**:
  When cycling sort modes (Alphabetical $\rightarrow$ Size $\rightarrow$ Date Modified), your active selection **stays locked onto that exact file or folder**, automatically recalibrating viewport scroll offsets instead of resetting to the top of the list.
* **Asynchronous Reorder Stability**:
  As background worker threads calculate directory sizes and re-sort lists dynamically, your highlighted item never flickers or jumps.
* **Per-Directory Navigation Memory (`dirCursorHistory`)**:
  - Entering a subdirectory and returning (<kbd>h</kbd> / <kbd>Left</kbd> / <kbd>Backspace</kbd>) restores focus to the directory you just exited.
  - Every visited directory preserves its exact cursor position and viewport scroll state across history back/forward jumps (<kbd>Ctrl+O</kbd> / <kbd>Ctrl+P</kbd> / <kbd>H</kbd>).

---

### 6. 🎨 Universal 256-Color Engine (`hexTo256`)
* **Truecolor-to-256 Translation**:
  Implements Euclidean RGB distance matching to map hex colors defined in `~/.config/fyzenor/theme.toml` into standard 256-color ANSI space.
* **Terminal Consistency**:
  Eliminates washed-out palettes and crude 8-color fallbacks when running inside `tmux`, Neovim terminal buffers (`:terminal`), or SSH sessions.
* **Dynamic Palette Immunity**:
  Theme indices are stable against dynamic terminal palette overrides (such as Matugen wallpaper theming). Press <kbd>F5</kbd> or <kbd>Ctrl+R</kbd> to reload `theme.toml` live.

---

### 7. ✨ Unified Item Creation (`n`) & Dynamic Type Indicators
* **Single Shortcut Workflow**:
  File and folder creation are unified under <kbd>n</kbd> (replacing legacy `N`). Typing a trailing slash (e.g. `src/components/`) automatically creates a directory; omitting it creates a file.
* **Dynamic Real-Time Nerd Font Glyph Detection**:
  As you type in the centered input dialog, the bottom-right indicator dynamically updates its icon and syntax accent color in real time:
  - Language extensions: `hello.c` $\rightarrow$ ``, `main.cpp` $\rightarrow$ ``, `app.py` $\rightarrow$ ``, `lib.rs` $\rightarrow$ ``, `server.go` $\rightarrow$ ``, `config.json` $\rightarrow$ ``, `script.sh` $\rightarrow$ ``, `doc.md` $\rightarrow$ ``.
  - Dotfiles and configs: `Makefile` $\rightarrow$ ``, `Dockerfile` $\rightarrow$ `󰡨`, `.gitignore` $\rightarrow$ ``.
  - Directories: Appending `/` or `\` immediately switches to the directory glyph (``).
* **Nested Path Creation & Smart Focus**:
  Enter recursive paths (e.g. `nested/deep/directory/` or `components/ui/Button.tsx`) to create missing parent folders automatically and immediately focus the created item in the listing.

---

### 8. 🖱️ Context-Aware Mouse Scrolling & Preview Navigation
* **Hover-Aware Pane Scrolling**:
  Simply hover the mouse pointer over any pane and rotate the wheel:
  - Middle pane: scrolls file entries.
  - Preview pane: smoothly scrolls long code, text, or archive contents with live position indicators (`[1-40/350]`).
  - Parent / Pinned panes: scrolls directory trees and bookmarks independently.
* **Keyboard Preview Scrolling**:
  Scroll previews without mouse input using <kbd>Ctrl+E</kbd> (down) and <kbd>Ctrl+Y</kbd> (up) while maintaining cursor focus on your active file.

---

### 9. 📋 Modal Clipboard Pasting & Input Hardening
* **Pasting in Input Modals**:
  Supports <kbd>Ctrl+V</kbd>, <kbd>Ctrl+Shift+V</kbd>, and terminal bracketed paste sequences (`\033[200~`) inside all text prompts (Rename <kbd>r</kbd>, Create Item <kbd>n</kbd>, Search `/`, and Zip <kbd>z</kbd>).
* **Readline Line-Editing**:
  Full support for <kbd>Ctrl+U</kbd> (clear entire line) and <kbd>Ctrl+W</kbd> (delete preceding word).
* **Multibyte UTF-8 Boundary Safety**:
  Backspace, Delete, and arrow keys properly respect UTF-8 codepoints, preventing character truncation or garbled display.
* **Dynamic Terminal Resize**:
  All modal overlays automatically re-center and adapt geometry when the terminal window is resized (`KEY_RESIZE`).

---

### 10. 󰊢 Git & Lazygit Integration (`Ctrl+G`)
* **Instant Lazygit Launch**:
  Press <kbd>Ctrl+G</kbd> in normal mode to launch `lazygit` inside the currently browsed directory.
* **Tmux Centered Popup Modal**:
  When running inside `tmux`, Fyzenor leverages `tmux display-popup` to spawn `lazygit` as a floating 85% overlay modal, preserving your TUI view underneath.
* **Clean Fallback & State Reloading**:
  In standalone terminals, Fyzenor suspends its UI cleanly and immediately invokes `reloadAll()` upon exiting `lazygit`, ensuring branch switches, checkouts, and staged files are reflected instantaneously.

---

### 11. 🚀 Drag & Drop Integration
* **Drag In via Terminal**:
  Drag files directly from your desktop file manager or web browser into the Fyzenor terminal window to trigger copy/move dialogs via bracketed paste.
* **Drag Out (<kbd>Ctrl+D</kbd>)**:
  Press <kbd>Ctrl+D</kbd> on any highlighted or multi-selected items to drag them out into GUI applications (email clients, browsers, chat apps) using `ripdrag` or `dragon`.

---

### 12. 🛠️ Modernized Installation & Uninstallation Scripts
* **Smart Channel Installer (`install.sh`)**:
  Automated prerequisite checks for C++17 compilers (GCC 8+, Clang 7+), CMake, ncursesw, and Lua runtime. Supports `--stable` and `--beta` channels.
* **Dedicated Standalone Uninstaller (`uninstall.sh`)**:
  Cleanly removes the binary, desktop entries, icons, and `fm` symlinks. Includes `--purge` to remove configuration directories and bookmarks.

---

## 🐛 Bug Fixes, Concurrency & Stability Hardening

v4.3.0 includes an extensive security, memory, and concurrency audit:

* **POSIX Stream Lifecycle**: Replaced `fclose` with `pclose` on `popen` command streams, preventing zombie process leaks and securing PID tracking.
* **UTF-8 Truncation Underflow**: Fixed critical out-of-bounds read and index underflow in `utf8_safe_truncate_left` when formatting long file paths.
* **Scroll Arithmetic Underflow**: Guarded signed-to-unsigned integer conversion in <kbd>G</kbd> (jump to bottom) scroll calculations.
* **Async Search Data Race**: Fixed a race condition on `currentPath` inside asynchronous search worker threads by capturing search paths strictly by value.
* **Large Directory Sizing (12GB+)**: Re-engineered recursive directory calculation with non-throwing error handling, symlink loop avoidance, and graceful handling of unreadable entries.
* **Active Directory Deletion Recovery**: If the active directory is deleted externally while Fyzenor is open, Fyzenor safely catches the error and navigates up to the nearest valid parent directory.
* **Inotify Feedback Loop**: Suppressed inotify reload triggers generated during recursive size scanning, eliminating UI stutter on massive directory trees.
* **Preview Thread Hang**: Fixed worker thread deadlocks and ensured Kitty/Ghostty/WezTerm image overlays are immediately cleared when pressing <kbd>Esc</kbd> or switching files quickly.

---

## ⚠️ Breaking Changes & Deprecations

| Change | Details | Migration |
| :--- | :--- | :--- |
| **Unified Create Hotkey** | Legacy <kbd>N</kbd> (folder creation) has been removed. | Use <kbd>n</kbd> for both files and directories. Append `/` to create a directory (e.g. `my-folder/`). |
| **Disk Usage Hotkey** | Legacy <kbd>Space+u</kbd> key sequence has been removed. | Press single-key <kbd>U</kbd> to toggle Visual Disk Usage Mode. |
| **Neovim Plugin Branch** | Neovim plugin users on beta branch. | Point plugin spec to `main` (or tag `v4.3.0`). |

---

## ⌨️ Comprehensive Keybindings Cheatsheet

### Navigation
| Key | Action |
| :--- | :--- |
| `h` / `←` / `Backspace` | Navigate to parent directory / Clear active search |
| `j` / `↓` | Move selection down |
| `k` / `↑` | Move selection up |
| `l` / `→` / `Enter` | Open file / Enter directory |
| `g` / `G` | Jump to top / Jump to bottom of list |
| `/` | Live content search via `ripgrep` |
| `f` | Fast fuzzy-find files in current directory |
| `Ctrl+O` / `Ctrl+P` | Jump backward / Jump forward in directory navigation history |
| `H` | Open recent directory navigation history overlay |
| `Ctrl+E` / `Ctrl+Y` | Scroll preview pane content down / up |
| `Mouse Wheel` | Hover-aware pane scrolling (files, preview, pins) |

### File Operations
| Key | Action |
| :--- | :--- |
| `n` | **Create file or folder** (`name` for file, `name/` for folder) |
| `r` | **Rename** item (in Trash Manager: **Restore** item) |
| `y` / `x` / `p` | **Yank** (copy) / **Cut** / **Paste** |
| `Y` | **Paste as absolute symlinks** |
| `d` / `Delete` | **Move to Trash** (in Trash Manager: permanently delete) |
| `D` | **Delete permanently** (skips trash) |
| `T` | **Toggle Trash Manager** |
| `u` | **Undo** last move-to-trash action |
| `z` / `e` | **Zip** items / **Extract** archive |
| `c` | **Copy absolute path** to system clipboard |
| `Ctrl+D` | **Drag out files** to external GUI apps (`ripdrag` / `dragon`) |
| `I` | **Interactive Permissions & Ownership Editor** (`chmod`/`chown`) |

### Selection, View & Modes
| Key | Action |
| :--- | :--- |
| `Tab` / `Shift+Tab` | Toggle selection and advance / reverse cursor |
| `Space` / `v` | Toggle selection (`Space` advances, `v` stays on current item) |
| `a` / `Esc` | Select **All** / **Clear** all selections |
| `.` | Toggle hidden files |
| `s` | Cycle sorting mode (**Name** $\rightarrow$ **Size** $\rightarrow$ **Date Modified**) |
| `V` / `Shift+V` | Toggle **2D Grid View Mode** (switch between 3-column Miller mode and 2D visual thumbnail card grid) |
| `U` | Toggle **Visual Disk Usage & Bar Graph Mode** (`ncdu` style) |
| `P` | Pin current directory to bookmarks |
| `F2` | Toggle **Dual-Pane Mode** (split-screen browsing) |
| `F3` / `F4` / `F6` | Toggle **Preview** / **Parent** / **Pinned Bookmarks** pane visibility |
| `F5` / `Ctrl+R` | Invalidate caches and **Refresh** view |
| `Ctrl+G` | Open **Lazygit** (or expand pane in Dual-Pane mode) |
| `w` | Open **Background Tasks Manager** (pause, resume, cancel tasks) |
| `m` | Open **Devices & Mounts Manager** (USB drives & Android MTP) |
| `:` | Execute interactive shell command |
| `?` | Open **Keyboard Shortcuts Help Modal** |
| `q` | Quit Fyzenor |

### Tab Management
| Key | Action |
| :--- | :--- |
| `t` | Open new tab |
| `W` / `Ctrl+W` | Close current tab |
| `[` / `]` | Switch to previous / next tab |
| `1` - `9`, `0` | Switch directly to tab 1 through 10 (`0` = tab 10) |

---

## 📦 Installation & Upgrades

### 1. Smart Installer (Recommended)
Install or upgrade to the stable release with one command:
```bash
curl -fsSL https://raw.githubusercontent.com/Bimbok/fyzenor/main/install.sh | bash
```

### 2. Manual Build from Source
```bash
# Clone the repository
git clone https://github.com/Bimbok/fyzenor.git
cd fyzenor

# Build using CMake
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Install system-wide
sudo make install
```

### 3. Neovim Plugin Setup
Add Fyzenor to your Neovim plugin manager:

#### `lazy.nvim`
```lua
{
  "Bimbok/fyzenor",
  event = "VeryLazy",
  opts = {
    open_for_directories = true,
    change_neovim_cwd_on_close = true,
  },
  keys = {
    { "<leader>e", "<cmd>Fyzenor<cr>", desc = "Open Fyzenor at file" },
    { "<leader>fe", "<cmd>FyzenorToggle<cr>", desc = "Toggle Fyzenor" },
  },
}
```

---

## 💬 Community & Links
- 🌐 **Documentation**: [fyzenor.vercel.app](https://fyzenor.vercel.app/)
- 💻 **Source Code**: [github.com/Bimbok/fyzenor](https://github.com/Bimbok/fyzenor)
- 🔌 **Official Plugins**: [github.com/Bimbok/fyzenor-plugins](https://github.com/Bimbok/fyzenor-plugins)
- 🐛 **Issue Tracker**: [github.com/Bimbok/fyzenor/issues](https://github.com/Bimbok/fyzenor/issues)
- 👤 **Maintainer**: [@Bimbok](https://github.com/Bimbok)

---
*Thank you to everyone who tested the beta builds, provided feedback, and helped make Fyzenor v4.3.0 our fastest, most versatile release yet!*
