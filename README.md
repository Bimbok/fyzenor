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
- **⚡ Asynchronous Folder Sizing:** Directory sizes are calculated in the background, showing `...` during calculation and updating live.
- **🧭 Vim-Like Navigation:** Feel right at home with `h`, `j`, `k`, `l` and `g`/`G` movements.
- **✨ Nerd Fonts Support:** Beautiful icons for directories, archives, media, and various code files.
- **🎨 Custom Theming:** Fully customizable color schemes via simple configuration files.
- **📌 Persistent Pins:** Pin your favorite directories and jump between them instantly. Pins are saved across sessions.
- **⚡ Flicker-Free Rendering:** Efficient state management ensures the UI only redraws when necessary.
- **📝 Essential Operations:** Copy, Cut, Paste, Rename, Delete, New File/Folder, and Zip compression.
- **✅ Robust Multi-Selection:** Easily select varying groups of files for bulk operations.

## 🛠️ Prerequisites

To unleash the full power of Fyzenor (especially image previews), your system needs a few things:

1.  **A Terminal Supporting the Kitty Graphics Protocol:**
    - Recommended: [Kitty](https://sw.kovidgoyal.net/kitty/)
    - _Note: Other terminals like WezTerm or Konsole might work but Kitty is the primary target._
2.  **Dependencies (Debian/Ubuntu based):**

```bash
sudo apt update
sudo apt install build-essential libncursesw5-dev ffmpeg zip bat
```

- `libncursesw`: For wide-character (Unicode/Icon) TUI support.
- `ffmpeg`: Required to generate thumbnails for images and videos.
- `zip`: Required for archive operations.
- `bat`: (Optional) For high-performance syntax highlighting in text previews.

## ⚙️ Installation & Update

The easiest way to install or update Fyzenor is using the universal installation script.

### One-Liner (Recommended)

Run this command in your terminal to automatically download, compile, and install (or update) Fyzenor:

```bash
curl -fsSL https://raw.githubusercontent.com/Bimbok/fyzenor/main/install.sh | bash
```

### Manual Installation & Updates

If you already have the repository cloned, you can run the installer locally:

```bash
# 1. Enter the repository
cd fyzenor

# 2. Run the installer (it will also pull the latest changes)
./install.sh
```

## 🎨 Customization & Theming

Fyzenor supports custom themes via a simple configuration file located at `~/.config/fyzenor/colors.fz`. The default theme is **Catppuccin Mocha**.

### Configuration Format

Create `~/.config/fyzenor/colors.fz` and define colors using hex codes:

```text
DIR: #89b4fa
FILE: #cdd6f4
SEL_BG: #585b70
MEDIA: #f9e2af
IMAGE: #f5c2e7
BORDER: #b4befe
SUCCESS: #a6e3a1
ERROR: #f38ba8
MULTI: #fab387
PIN_BG: #cba6f7
PIN_BORDER: #89b4fa
SEC_SEL_BG: #313244
CODE: #a6e3a1
ARCHIVE: #eba0ac
```

### Matugen works using **templates**. Instead of manually writing `colors.fz`,

#### Step 1: Create the Matugen Template

Create a new file for your template. You can put this in your Matugen config directory (e.g., `~/.config/matugen/templates/fyzenor-colors.template`).

Add the following Material Design variable mappings to it. I've mapped the Material 3 color tokens to best fit the UI elements in Fyzenor:

```text
# Fyzenor Theme: Matugen Generated
DIR: {{colors.primary.default.hex}}
FILE: {{colors.on_surface.default.hex}}
SEL_BG: {{colors.surface_variant.default.hex}}
MEDIA: {{colors.tertiary.default.hex}}
IMAGE: {{colors.secondary.default.hex}}
BORDER: {{colors.outline.default.hex}}
SUCCESS: {{colors.primary_fixed.default.hex}}
ERROR: {{colors.error.default.hex}}
MULTI: {{colors.tertiary_container.default.hex}}
PIN_BG: {{colors.secondary_container.default.hex}}
PIN_BORDER: {{colors.primary.default.hex}}
SEC_SEL_BG: {{colors.surface_dim.default.hex}}
```

#### Step 2: Update your Matugen Config

Now, tell Matugen about this new template so it knows where to write the final configuration file.

Open your Matugen config file (usually `~/.config/matugen/config.toml`) and add a new `[templates]` block at the bottom:

```toml
[templates.fyzenor]
# The path to the template file we just created
input_path = "~/.config/matugen/templates/fyzenor-colors.template"
# The path where Fyzenor expects to find its colors
output_path = "~/.config/fyzenor/colors.fz"
```

### Step 3: Generate the Colors

Run Matugen against your wallpaper as you normally would:

```bash
matugen image /path/to/your/wallpaper.jpg
```

Matugen will read the image, calculate the Material colors, fill in your template, and overwrite `~/.config/fyzenor/colors.fz` with the generated hex codes.

## 🛠️ CLI Usage

Fyzenor supports a few command-line arguments:

| Option            | Description                             |
| :---------------- | :-------------------------------------- |
| `-v`, `--version` | Display the current version of Fyzenor. |
| `-h`, `--help`    | Show the help message and exit.         |

```bash
# Check version
fyzenor --version

# Show help
fyzenor --help
```

## ⌨️ Controls

Navigate your filesystem with the speed and precision of Vim bindings.

### Navigation

| Key                   | Action                      |
| :-------------------- | :-------------------------- |
| `k` or `↑`            | Move selection up           |
| `j` or `↓`            | Move selection down         |
| `h` or `←` or `BS`    | Go to parent directory      |
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
| `z`             | **Zip** selected items into an archive        |
| `c`             | **Copy Path** to system clipboard             |

### Selection, View & Pins

| Key            | Action                                    |
| :------------- | :---------------------------------------- |
| `Space` or `v` | Toggle selection of current file          |
| `a`            | Select **All** files in current directory |
| `Esc`          | Clear all selections                      |
| `.`            | Toggle hidden files (dotfiles)            |
| `s`            | Toggle sort by size                       |
| `P`            | Pin current directory (Persistent)        |
| `Tab`          | Toggle focus between Files and Pins       |
| `q`            | Quit Fyzenor                              |

### Pin Mode Controls

When focused on the **Pins** column (using `Tab`):

- `j`/`k`: Navigate pins.
- `Enter`: Jump to pinned directory.
- `d`: Remove pin.

## 🎨 Visuals & Protocols

### Kitty Graphics Protocol

Fyzenor utilizes the [Kitty Graphics Protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/) for high-resolution image and video previews. This allows for smooth, asynchronous rendering of thumbnails without freezing the terminal UI. It automatically handles aspect ratios and scales previews to fit your terminal window.

### Nerd Fonts

Icons are rendered using [Nerd Fonts](https://www.nerdfonts.com/). Ensure your terminal is using a Nerd Font for icons to display correctly. Supported categories include media, code, archives, and system folders.

### Syntax Highlighting

Text file previews benefit from syntax highlighting powered by `bat` (or `batcat`). If `bat` is not installed, Fyzenor falls back to a plain text preview. Binary files are automatically detected and skipped to prevent terminal corruption.

## 🤝 Contributing

Contributions are welcome! Whether it's reporting a bug, suggesting a feature, or submitting a pull request.

1.  Fork the repository.
2.  Create your feature branch (`git checkout -b feature/AmazingFeature`).
3.  Commit your changes (`git commit -m 'Add some AmazingFeature'`).
4.  Push to the branch (`git push origin feature/AmazingFeature`).
5.  Open a pull request.

## ⚖️ License

Distributed under the MIT License. See `LICENSE` for more information.
