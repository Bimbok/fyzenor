```
╔══╦ ╦╔═╗╔═╗╔╗╔╔═╗╦═╗
╠══╚╦╝╔═╝║╣ ║║║║ ║╠╦╝
╩   ╩ ╚═╝╚═╝╝╚╝╚═╝╩╚═
```

<div align="center">

> \_ The Blazing Fast, Modern C++ Terminal File Manager.

[![C++](https://img.shields.io/badge/language-C++17-00599C?style=for-the-badge&logo=c%2B%2B)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey?style=for-the-badge)](https://www.linux.org/)
[![Kitty](https://img.shields.io/badge/terminal-Kitty%20Protocol-ff69b4?style=for-the-badge&logo=linux)](https://sw.kovidgoyal.net/kitty/graphics-protocol/)

<br/>

<img width="1920" height="1043" alt="Image" src="https://github.com/user-attachments/assets/7491cd62-fc34-4169-9b5b-11b0bb43dba0" />

</div>

---

## ⚡ Introduction

**Fyzenor** is a lightweight, high-performance terminal file manager built with modern C++17. It combines the speed and efficiency of the command line with visual comforts like asynchronous media previews and intuitive Vim-style navigation.

Designed for power users who want to manage files at the speed of thought without leaving their terminal.

## 🚀 Key Features

- **📁 Three-Column Architecture:** Classic Miller columns view (Parent → Current → Preview) for instant context.
- **🖼️ Asynchronous Media Previews:** High-resolution image and video thumbnails rendered directly in the terminal using the **Kitty Graphics Protocol**. Zero UI freezing while loading.
- **🧭 Vim-Like Navigation:** Feel right at home with `h`, `j`, `k`, `l` and `g`/`G` movements.
- **✨ Nerd Fonts Support:** Beautiful icons for directories and various file types.
- **⚡ Flicker-Free Rendering:** Efficient state management ensures the UI only redraws when necessary.
- **📝 Essential Operations:** Copy, Cut, Paste, Rename, Delete, New File/Folder, and Zip compression.
- **✅ Robust Multi-Selection:** Easily select varying groups of files for bulk operations.

## 🛠️ Prerequisites

To unleash the full power of Fyzenor (especially image previews), your system needs a few things:

1.  **A Terminal Supporting the Kitty Graphics Protocol:**
    - Recommended: [Kitty](https://sw.kovidgoyal.net/kitty/)
    - _Note: Other terminals like WezTerm or Konsole might work but Kitty is primary target._
2.  **Dependencies (Debian/Ubuntu based):**

```bash
sudo apt update
sudo apt install build-essential libncursesw5-dev ffmpeg zip
```

- `libncursesw`: For wide-character (Unicode/Icon) TUI support.
- `ffmpeg`: Required to generate thumbnails for images and videos.
- `zip`: Required for archive operations.

## ⚙️ Installation & Compilation

Fyzenor is distributed as source code. Compile it quickly with `g++`.

```bash
# 1. Clone the repository
git clone https://github.com/Bimbok/fyzenor.git
cd fyzenor

# 2. Compile (using O3 optimization for speed)
g++ -std=c++17 -O3 file_manager.cpp -o fyzenor -lncursesw -lpthread

# 3. Run it!
./fyzenor
```

_Tip: Move the binary to your path for global access: `sudo mv fyzenor /usr/local/bin/`_

## ⌨️ Controls

Navigate your filesystem with the speed and precision of Vim bindings.

### Navigation

| Key                   | Action                      |
| :-------------------- | :-------------------------- |
| `k` or `↑`            | Move selection up           |
| `j` or `↓`            | Move selection down         |
| `h` or `←`            | Go to parent directory      |
| `l` or `→` or `Enter` | Open file / Enter directory |
| `g`                   | Go to top of list           |
| `G`                   | Go to bottom of list        |

### File Operations

| Key             | Action                                        |
| :-------------- | :-------------------------------------------- |
| `y`             | **Yank** (Copy) selected items                |
| `x`             | **Cut** selected items                        |
| `p`             | **Paste** items from clipboard                |
| `d` or `Delete` | **Delete** selected items (with confirmation) |
| `r`             | **Rename** current item                       |
| `n`             | Create **New File**                           |
| `N`             | Create **New Folder**                         |
| `z`             | **Zip** selected items                        |

### Selection & View

| Key            | Action                                    |
| :------------- | :---------------------------------------- |
| `Space` or `v` | Toggle selection of current file          |
| `a`            | Select **All** files in current directory |
| `Esc`          | Clear all selections                      |
| `.`            | Toggle hidden files (dotfiles)            |
| `q`            | Quit Fyzenor                              |

## ⚖️ License

Distributed under the MIT License. See `LICENSE` for more information.
