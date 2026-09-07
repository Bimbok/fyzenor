#!/bin/bash
set -e

# Fyzenor Universal Installer, Updater & Uninstaller
# This script can be run locally or via curl:
#   curl -fsSL https://raw.githubusercontent.com/Bimbok/fyzenor/main/install.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/Bimbok/fyzenor/main/install.sh | bash -s -- --beta
#   curl -fsSL https://raw.githubusercontent.com/Bimbok/fyzenor/main/install.sh | bash -s -- --uninstall

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔══╦ ╦╔═╗╔═╗╔╗╔╔═╗╦═╗${NC}"
echo -e "${GREEN}╠══╚╦╝╔═╝║╣ ║║║║ ║╠╦╝${NC}"
echo -e "${GREEN}╩   ╩ ╚═╝╚═╝╝╚╝╚═╝╩╚═${NC}"
echo -e "${CYAN}The Blazing Fast Modern C++ File Manager${NC}\n"

REPO_URL="https://github.com/Bimbok/fyzenor.git"
TARGET_BRANCH="main"
ACTION="install"
PURGE=false

for arg in "$@"; do
    case $arg in
        --beta)
            TARGET_BRANCH="beta"
            ;;
        --stable)
            TARGET_BRANCH="main"
            ;;
        --uninstall)
            ACTION="uninstall"
            ;;
        --purge)
            PURGE=true
            ;;
        -h|--help)
            echo -e "${BOLD}Fyzenor Installer & Manager${NC}"
            echo -e "Usage: ./install.sh [OPTIONS]\n"
            echo -e "Options:"
            echo -e "  ${YELLOW}--stable${NC}       Install / update Stable channel (default, v4.2.0 on main branch)"
            echo -e "  ${YELLOW}--beta${NC}         Install / update Beta channel (v4.3.0-beta.2 on beta branch)"
            echo -e "  ${YELLOW}--uninstall${NC}    Uninstall Fyzenor binary, symlink, desktop entry, and icons"
            echo -e "  ${YELLOW}--purge${NC}        When used with --uninstall, also delete ~/.config/fyzenor and ~/.fm_pins"
            echo -e "  ${YELLOW}-h, --help${NC}     Show this help message"
            exit 0
            ;;
    esac
done

# Detect Termux environment
IS_TERMUX=false
if [ -n "$TERMUX_VERSION" ] || [ -d "/data/data/com.termux/files/usr" ]; then
    IS_TERMUX=true
fi

# Determine target directories
if $IS_TERMUX; then
    PREFIX_PATH="${PREFIX:-/data/data/com.termux/files/usr}"
    INSTALL_PATH="$PREFIX_PATH/bin/fyzenor"
    SYMLINK_PATH="$PREFIX_PATH/bin/fm"
    DESKTOP_DIR=""
    PIXMAPS_DIR=""
else
    PREFIX_PATH="/usr/local"
    INSTALL_PATH="$PREFIX_PATH/bin/fyzenor"
    SYMLINK_PATH="$PREFIX_PATH/bin/fm"
    DESKTOP_DIR="/usr/share/applications"
    PIXMAPS_DIR="/usr/share/pixmaps"
fi

CONFIG_DIR="$HOME/.config/fyzenor"
PINS_FILE="$HOME/.fm_pins"

# Helper function to run commands with/without sudo
run_cmd() {
    if [ "$(id -u)" -eq 0 ] || $IS_TERMUX; then
        "$@"
    else
        if command -v sudo &>/dev/null; then
            sudo "$@"
        else
            echo -e "${RED}Error: 'sudo' command not found. Please run this script as root.${NC}"
            exit 1
        fi
    fi
}

# -------------------------------------------------------------
# UNINSTALLATION ROUTINE
# -------------------------------------------------------------
if [ "$ACTION" = "uninstall" ]; then
    echo -e "${BLUE}=== Uninstalling Fyzenor ===${NC}\n"
    REMOVED_ANY=false

    # 1. Remove binary
    if [ -f "$INSTALL_PATH" ]; then
        echo -e "Removing executable at $INSTALL_PATH..."
        run_cmd rm -f "$INSTALL_PATH"
        echo -e "${GREEN}✓ Removed $INSTALL_PATH${NC}"
        REMOVED_ANY=true
    fi

    # 2. Remove symlink 'fm'
    if [ -L "$SYMLINK_PATH" ] || [ -f "$SYMLINK_PATH" ]; then
        echo -e "Removing symlink at $SYMLINK_PATH..."
        run_cmd rm -f "$SYMLINK_PATH"
        echo -e "${GREEN}✓ Removed $SYMLINK_PATH${NC}"
        REMOVED_ANY=true
    fi

    # 3. Remove desktop file
    if [ -n "$DESKTOP_DIR" ] && [ -f "$DESKTOP_DIR/fyzenor.desktop" ]; then
        echo -e "Removing desktop entry at $DESKTOP_DIR/fyzenor.desktop..."
        run_cmd rm -f "$DESKTOP_DIR/fyzenor.desktop"
        if command -v update-desktop-database &>/dev/null; then
            run_cmd update-desktop-database "$DESKTOP_DIR" &>/dev/null || true
        fi
        echo -e "${GREEN}✓ Removed desktop entry${NC}"
        REMOVED_ANY=true
    fi

    # 4. Remove desktop icon
    if [ -n "$PIXMAPS_DIR" ] && [ -f "$PIXMAPS_DIR/fyzenor.png" ]; then
        echo -e "Removing desktop icon at $PIXMAPS_DIR/fyzenor.png..."
        run_cmd rm -f "$PIXMAPS_DIR/fyzenor.png"
        if command -v gtk-update-icon-cache &>/dev/null; then
            run_cmd gtk-update-icon-cache -f -t "$PIXMAPS_DIR" &>/dev/null || true
        fi
        echo -e "${GREEN}✓ Removed desktop icon${NC}"
        REMOVED_ANY=true
    fi

    # 5. Handle user configurations and bookmarks
    if $PURGE; then
        echo -e "${YELLOW}Purging user configuration and bookmarks...${NC}"
        rm -rf "$CONFIG_DIR" "$PINS_FILE"
        echo -e "${GREEN}✓ Removed $CONFIG_DIR and $PINS_FILE${NC}"
        REMOVED_ANY=true
    else
        if [ -d "$CONFIG_DIR" ] || [ -f "$PINS_FILE" ]; then
            echo -e "\n${CYAN}Notice: User configurations preserved in $CONFIG_DIR${NC}"
            echo -e "To completely purge configs and bookmarks, re-run with: ${YELLOW}./install.sh --uninstall --purge${NC}"
        fi
    fi

    if $REMOVED_ANY; then
        echo -e "\n${GREEN}Fyzenor has been successfully uninstalled from your system.${NC}"
    else
        echo -e "\n${YELLOW}No active Fyzenor installation found at $INSTALL_PATH.${NC}"
    fi
    exit 0
fi

# -------------------------------------------------------------
# INSTALLATION / UPDATE ROUTINE
# -------------------------------------------------------------
if [ "$TARGET_BRANCH" = "beta" ]; then
    echo -e "${YELLOW}Installing Channel: BETA (v4.3.0-beta.2 - Cutting-edge features, Disk Usage, Keybindings Modal & Lua Plugin Engine)${NC}\n"
else
    echo -e "${GREEN}Installing Channel: STABLE (v4.2.0 - Tested production release)${NC}\n"
fi

# Function to check command dependency
check_dep() {
    command -v "$1" &>/dev/null
}

# Function to check Lua development library / headers
check_lua_dev() {
    if pkg-config --exists lua 2>/dev/null || \
       pkg-config --exists lua5.4 2>/dev/null || \
       pkg-config --exists lua-5.4 2>/dev/null || \
       pkg-config --exists lua5.3 2>/dev/null || \
       pkg-config --exists lua-5.3 2>/dev/null; then
        return 0
    fi
    if [ -f /usr/include/lua.h ] || [ -f /usr/include/lua5.4/lua.h ] || [ -f /usr/include/lua5.3/lua.h ] || \
       [ -f /usr/local/include/lua.h ] || [ -f "${PREFIX_PATH}/include/lua.h" ]; then
        return 0
    fi
    return 1
}

# Function to check Ncurses development library / headers
check_ncurses_dev() {
    if pkg-config --exists ncursesw 2>/dev/null || pkg-config --exists ncurses 2>/dev/null; then
        return 0
    fi
    if [ -f /usr/include/ncursesw/ncurses.h ] || [ -f /usr/include/ncurses.h ] || \
       [ -f /usr/include/curses.h ] || [ -f "${PREFIX_PATH}/include/ncurses.h" ]; then
        return 0
    fi
    return 1
}

# Helper to print OS-specific package manager instructions
print_distro_instructions() {
    echo -e "\n${CYAN}Recommended installation command for your system:${NC}"
    if $IS_TERMUX; then
        echo -e "  ${GREEN}pkg install -y clang cmake ndk-sysroot ncurses-utils lua54 ffmpeg zip bat ripgrep fzf poppler${NC}"
    elif [ -f /etc/debian_version ] || check_dep "apt-get"; then
        echo -e "  ${GREEN}sudo apt update && sudo apt install -y build-essential cmake libncursesw5-dev liblua5.4-dev ffmpeg zip bat ripgrep fzf poppler-utils${NC}"
    elif [ -f /etc/fedora-release ] || [ -f /etc/redhat-release ] || check_dep "dnf"; then
        echo -e "  ${GREEN}sudo dnf install -y gcc-c++ cmake ncurses-devel lua-devel ffmpeg zip bat ripgrep fzf poppler-utils${NC}"
    elif [ -f /etc/arch-release ] || check_dep "pacman"; then
        echo -e "  ${GREEN}sudo pacman -S --needed base-devel cmake ncurses lua ffmpeg zip bat ripgrep fzf poppler${NC}"
    elif [ -f /etc/alpine-release ] || check_dep "apk"; then
        echo -e "  ${GREEN}apk add g++ cmake make ncurses-dev lua5.4-dev ffmpeg zip bat ripgrep fzf poppler-utils${NC}"
    elif check_dep "zypper"; then
        echo -e "  ${GREEN}sudo zypper install -y gcc-c++ cmake ncurses-devel lua54-devel ffmpeg zip bat ripgrep fzf poppler-tools${NC}"
    elif check_dep "brew"; then
        echo -e "  ${GREEN}brew install cmake ncurses lua ffmpeg zip bat ripgrep fzf poppler${NC}"
    else
        echo -e "  Please install C++17 compiler, cmake, libncursesw development files, and lua 5.3/5.4 development headers."
    fi
    echo ""
}

# 1. Handle "Run from anywhere" (curl | bash)
if [ ! -f "src/main.cpp" ]; then
    echo -e "${YELLOW}Source code not found in current directory.${NC}"
    echo -e "${BLUE}Cloning Fyzenor ($TARGET_BRANCH channel) from GitHub...${NC}"

    if ! check_dep "git"; then
        echo -e "${RED}Error: git is required to clone the repository.${NC}"
        print_distro_instructions
        exit 1
    fi

    TEMP_DIR=$(mktemp -d)
    trap 'rm -rf "$TEMP_DIR"' EXIT
    git clone --depth 1 -b "$TARGET_BRANCH" "$REPO_URL" "$TEMP_DIR"
    cd "$TEMP_DIR" || exit 1
    echo -e "${GREEN}Repository ($TARGET_BRANCH) cloned to temporary directory.${NC}"
fi

# 2. Handle "Update" if already in a git repository
if [ -d ".git" ]; then
    echo -e "${BLUE}Checking for updates on branch '$TARGET_BRANCH'...${NC}"
    git fetch origin "$TARGET_BRANCH" 2>/dev/null || true
    git checkout "$TARGET_BRANCH" 2>/dev/null || git checkout -b "$TARGET_BRANCH" "origin/$TARGET_BRANCH" 2>/dev/null || true
    git pull origin "$TARGET_BRANCH" 2>/dev/null || true
fi

# 3. Dependencies Check
echo -e "${BLUE}Checking build and runtime dependencies...${NC}"
COMPILER="g++"
if $IS_TERMUX; then
    COMPILER="clang++"
elif ! check_dep "g++" && check_dep "clang++"; then
    COMPILER="clang++"
fi

BUILD_DEPS=("$COMPILER" "cmake")
MISSING_BUILD=()

for dep in "${BUILD_DEPS[@]}"; do
    if ! check_dep "$dep"; then
        MISSING_BUILD+=("$dep")
    fi
done

if ! check_lua_dev; then
    MISSING_BUILD+=("lua development library (liblua5.4-dev / lua-devel)")
fi

if ! check_ncurses_dev; then
    MISSING_BUILD+=("ncursesw development library (libncursesw5-dev / ncurses-devel)")
fi

if [ ${#MISSING_BUILD[@]} -ne 0 ]; then
    echo -e "${RED}Error: Missing required build dependencies:${NC}"
    for dep in "${MISSING_BUILD[@]}"; do
        echo -e "  - $dep"
    done
    print_distro_instructions
    exit 1
fi

# Check optional runtime tools
OPTIONAL_TOOLS=("ffmpeg" "zip" "rg")
MISSING_OPTIONAL=()

for tool in "${OPTIONAL_TOOLS[@]}"; do
    if ! check_dep "$tool"; then
        MISSING_OPTIONAL+=("$tool")
    fi
done

if ! check_dep "bat" && ! check_dep "batcat"; then
    MISSING_OPTIONAL+=("bat (or batcat)")
fi

if ! check_dep "fzf"; then
    MISSING_OPTIONAL+=("fzf")
fi

if [ ${#MISSING_OPTIONAL[@]} -ne 0 ]; then
    echo -e "${YELLOW}Notice: The following optional tools were not found:${NC}"
    for tool in "${MISSING_OPTIONAL[@]}"; do
        echo -e "  - $tool"
    done
    echo -e "${YELLOW}Fyzenor will still build and run, but installing them unlocks full previews, search, and fuzzy finding.${NC}"
    print_distro_instructions
fi

# 4. Parallel Compilation
echo -e "${BLUE}Compiling Fyzenor...${NC}"
mkdir -p build
cd build || exit 1

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

if cmake .. && cmake --build . -j"$NPROC"; then
    echo -e "${GREEN}✓ Compilation successful using $NPROC parallel jobs!${NC}"
    cd ..
else
    echo -e "${RED}Compilation failed.${NC}"
    print_distro_instructions
    exit 1
fi

# 5. Installation/Update Binary
if [ -f "$INSTALL_PATH" ]; then
    echo -e "${YELLOW}Existing installation found at $INSTALL_PATH. Updating...${NC}"
else
    echo -e "${BLUE}Installing Fyzenor to $INSTALL_PATH...${NC}"
fi

run_cmd mkdir -p "$(dirname "$INSTALL_PATH")"

if run_cmd cp -f build/fyzenor "$INSTALL_PATH" && run_cmd chmod 755 "$INSTALL_PATH"; then
    echo -e "${GREEN}✓ Fyzenor executable installed at $INSTALL_PATH${NC}"
    
    # Symlink 'fm' for fast terminal invocation
    if run_cmd ln -sf "$INSTALL_PATH" "$SYMLINK_PATH"; then
        echo -e "${GREEN}✓ Symlink 'fm' created at $SYMLINK_PATH${NC}"
    else
        echo -e "${YELLOW}Notice: Could not create symlink '$SYMLINK_PATH'.${NC}"
    fi

    # 6. Desktop Entry & Icon
    if ! $IS_TERMUX && [ -n "$DESKTOP_DIR" ]; then
        echo -e "${BLUE}Setting up desktop application shortcut and icon...${NC}"
        ICON_SOURCE="fyzenor.png"

        if [ -f "$ICON_SOURCE" ] && [ -n "$PIXMAPS_DIR" ]; then
            run_cmd mkdir -p "$PIXMAPS_DIR"
            if run_cmd cp -f "$ICON_SOURCE" "$PIXMAPS_DIR/fyzenor.png" && run_cmd chmod 644 "$PIXMAPS_DIR/fyzenor.png"; then
                echo -e "${GREEN}✓ Icon installed at $PIXMAPS_DIR/fyzenor.png${NC}"
            fi
        fi

        run_cmd mkdir -p "$DESKTOP_DIR"
        cat <<EOF | run_cmd tee "$DESKTOP_DIR/fyzenor.desktop" > /dev/null
[Desktop Entry]
Type=Application
Name=Fyzenor
Comment=The Blazing Fast Modern C++ File Manager
Icon=fyzenor
Exec=fyzenor
Terminal=true
Categories=System;FileTools;FileManager;Utility;
Keywords=file;manager;terminal;tui;cpp;
EOF

        if [ -f "$DESKTOP_DIR/fyzenor.desktop" ]; then
            run_cmd chmod 644 "$DESKTOP_DIR/fyzenor.desktop"
            if command -v update-desktop-database &>/dev/null; then
                run_cmd update-desktop-database "$DESKTOP_DIR" &>/dev/null || true
            fi
            if command -v gtk-update-icon-cache &>/dev/null && [ -n "$PIXMAPS_DIR" ]; then
                run_cmd gtk-update-icon-cache -f -t "$PIXMAPS_DIR" &>/dev/null || true
            fi
            echo -e "${GREEN}✓ Desktop entry registered at $DESKTOP_DIR/fyzenor.desktop${NC}"
        fi
    fi

    # 7. User Configuration & Plugins Initialization
    mkdir -p "$CONFIG_DIR"
    mkdir -p "$CONFIG_DIR/plugins"

    KEYS_FILE="$CONFIG_DIR/keys.toml"
    THEME_FILE="$CONFIG_DIR/theme.toml"
    CONFIG_FILE="$CONFIG_DIR/config.toml"

    if [ ! -f "$KEYS_FILE" ]; then
        if [ -f "keys.toml" ]; then
            cp "keys.toml" "$KEYS_FILE"
        else
            cat <<EOF > "$KEYS_FILE"
# Fyzenor Custom Keys Macro Configuration
# Macros allow you to execute shell command shortcuts using single keystrokes.
#   \$f - expands to the currently highlighted file's absolute path
#   \$s - expands to space-separated paths of all selected files

[macros]
v = 'nvim "\$f"'
g = 'git status'
l = 'ls -la'
EOF
        fi
        echo -e "${GREEN}✓ Configuration initialized: $KEYS_FILE${NC}"
    fi

    if [ ! -f "$THEME_FILE" ]; then
        if [ -f "theme.toml" ]; then
            cp "theme.toml" "$THEME_FILE"
        else
            cat <<EOF > "$THEME_FILE"
# Fyzenor Theme Configuration File (Catppuccin Mocha)

[colors]
dir = "#89b4fa"
file = "#cdd6f4"
sel_bg = "#585b70"
media = "#f9e2af"
image = "#f5c2e7"
border = "#b4befe"
success = "#a6e3a1"
error = "#f38ba8"
multi = "#f5e0dc"
pin_bg = "#cba6f7"
pin_border = "#89b4fa"
sec_sel_bg = "#313244"
core = "#a6e3a1"
archive = "#eba0ac"
frontend = "#fab387"
config = "#94e2d5"
script = "#f9e2af"
docs = "#f2cdcd"
font = "#cba6f7"
EOF
        fi
        echo -e "${GREEN}✓ Theme initialized: $THEME_FILE${NC}"
    fi

    if [ ! -f "$CONFIG_FILE" ]; then
        if [ -f "config.toml" ]; then
            cp "config.toml" "$CONFIG_FILE"
        else
            cat <<EOF > "$CONFIG_FILE"
# Fyzenor Default Configuration File

[general]
show_hidden = false
sort_mode = "name"

[layout]
parent_width = 0.18
current_width = 0.32
hide_preview = false
hide_parent = false
hide_pinned = false

[icons]
dir = " "
video = " "
image = " "
core = " "
frontend = "󰖟 "
config = " "
script = " "
docs = " "
font = " "
file = " "
music = " "
pin = " "
zip = "󰿺 "
link = "󰌹 "
EOF
        fi
        echo -e "${GREEN}✓ Default preferences initialized: $CONFIG_FILE${NC}"
    fi
else
    echo -e "${RED}Error: Failed to install binary to $INSTALL_PATH.${NC}"
    exit 1
fi

echo -e "\n${GREEN}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║             Installation Completed Successfully!         ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════╝${NC}"
echo -e "🚀 Run ${BOLD}${YELLOW}fyzenor${NC} or shortcut ${BOLD}${YELLOW}fm${NC} to start."
echo -e "ℹ️  Version: ${BLUE}$("$INSTALL_PATH" --version 2>/dev/null || echo "v4.3.0-beta.2")${NC}"
echo -e "⚙️  Config directory: ${BLUE}$CONFIG_DIR${NC}"
echo -e "🗑️  To uninstall at any time: ${YELLOW}./uninstall.sh${NC} (or ./install.sh --uninstall)\n"
