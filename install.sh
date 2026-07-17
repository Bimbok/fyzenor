#!/bin/bash

# Fyzenor Universal Installer & Updater
# This script can be run locally or via curl:
# curl -fsSL https://raw.githubusercontent.com/Bimbok/fyzenor/main/install.sh | bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔══╦ ╦╔═╗╔═╗╔╗╔╔═╗╦═╗${NC}"
echo -e "${GREEN}╠══╚╦╝╔═╝║╣ ║║║║ ║╠╦╝${NC}"
echo -e "${GREEN}╩   ╩ ╚═╝╚═╝╝╚╝╚═╝╩╚═${NC}"
echo -e "${BLUE}The Blazing Fast Modern C++ File Manager${NC}\n"

REPO_URL="https://github.com/Bimbok/fyzenor.git"

# Detect Termux environment
IS_TERMUX=false
if [ -n "$TERMUX_VERSION" ] || [ -d "/data/data/com.termux/files/usr" ]; then
    IS_TERMUX=true
fi

if $IS_TERMUX; then
    PREFIX_PATH="${PREFIX:-/data/data/com.termux/files/usr}"
    INSTALL_PATH="$PREFIX_PATH/bin/fyzenor"
else
    INSTALL_PATH="/usr/local/bin/fyzenor"
fi

# Helper function to run commands with/without sudo
run_cmd() {
    if $IS_TERMUX; then
        "$@"
    else
        sudo "$@"
    fi
}

# Function to check dependency
check_dep() {
    if ! command -v $1 &> /dev/null; then
        return 1
    else
        return 0
    fi
}

# 1. Handle "Run from anywhere" (curl | bash)
if [ ! -f "src/main.cpp" ]; then
    echo -e "${YELLOW}Source code not found in current directory.${NC}"
    echo -e "${BLUE}Cloning Fyzenor from GitHub...${NC}"
    
    if ! check_dep "git"; then
        echo -e "${RED}Error: git is required to clone the repository.${NC}"
        exit 1
    fi

    TEMP_DIR=$(mktemp -d)
    git clone --depth 1 "$REPO_URL" "$TEMP_DIR"
    cd "$TEMP_DIR" || exit 1
    echo -e "${GREEN}Repository cloned to temporary directory.${NC}"
fi

# 2. Handle "Update" if in a git repo
if [ -d ".git" ]; then
    echo -e "${BLUE}Checking for updates...${NC}"
    git pull origin main
fi

# 3. Dependencies Check
echo -e "${BLUE}Checking dependencies...${NC}"
COMPILER="g++"
if $IS_TERMUX; then
    COMPILER="clang++"
fi
DEPS=("$COMPILER" "cmake" "ffmpeg" "zip" "rg")
MISSING_DEPS=()

for dep in "${DEPS[@]}"; do
    if ! check_dep "$dep"; then
        MISSING_DEPS+=("$dep")
    fi
done

# Special check for bat/batcat
if ! check_dep "bat" && ! check_dep "batcat"; then
    MISSING_DEPS+=("bat (or batcat)")
fi

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo -e "${YELLOW}Warning: The following dependencies are missing:${NC}"
    for dep in "${MISSING_DEPS[@]}"; do
        echo -e " - $dep"
    done
    echo -e "${YELLOW}Some features (previews, zip, search, fuzzy find) might not work until installed.${NC}"
    if $IS_TERMUX; then
        echo -e "On Termux: ${GREEN}pkg install clang cmake ndk-sysroot ncurses-utils ffmpeg zip bat ripgrep${NC}\n"
    else
        echo -e "On Debian/Ubuntu: ${GREEN}sudo apt install libncursesw5-dev ffmpeg zip bat ripgrep${NC}\n"
    fi
fi

# 4. Compilation
echo -e "${BLUE}Compiling Fyzenor...${NC}"
mkdir -p build
cd build || exit 1
if cmake .. && make; then
    echo -e "${GREEN}Compilation successful!${NC}"
    cd ..
else
    echo -e "${RED}Compilation failed. Please check if 'libncursesw-dev' (or 'ncurses-utils'/'ncurses-devel'), 'cmake', and your C++ compiler are installed.${NC}"
    exit 1
fi

# 5. Installation/Update
if [ -f "$INSTALL_PATH" ]; then
    echo -e "${YELLOW}Existing installation found at $INSTALL_PATH. Updating...${NC}"
else
    echo -e "${BLUE}Installing Fyzenor...${NC}"
fi

if run_cmd mv build/fyzenor "$INSTALL_PATH" && run_cmd chmod 755 "$INSTALL_PATH"; then
    echo -e "${GREEN}Fyzenor is now installed/updated at $INSTALL_PATH${NC}"
    # Create symlink 'fm'
    if run_cmd ln -sf "$INSTALL_PATH" "$(dirname "$INSTALL_PATH")/fm"; then
        echo -e "${GREEN}Symlink 'fm' created successfully.${NC}"
    else
        echo -e "${YELLOW}Failed to create symlink 'fm'. You can do it manually: ln -sf $INSTALL_PATH $(dirname "$INSTALL_PATH")/fm${NC}"
    fi

    # 6. Create Desktop Entry
    if ! $IS_TERMUX; then
        echo -e "${BLUE}Creating desktop entry...${NC}"
        DESKTOP_FILE="/usr/share/applications/fyzenor.desktop"
        ICON_SOURCE="fyzenor.png"
        ICON_DEST="/usr/share/pixmaps/fyzenor.png"

        # Copy icon if it exists in the current directory
        if [ -f "$ICON_SOURCE" ]; then
            if run_cmd cp "$ICON_SOURCE" "$ICON_DEST"; then
                echo -e "${GREEN}Icon installed at $ICON_DEST${NC}"
            else
                echo -e "${YELLOW}Failed to install icon at $ICON_DEST${NC}"
            fi
        fi

        # Create the .desktop file
        cat <<EOF | run_cmd tee "$DESKTOP_FILE" > /dev/null
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

        if [ -f "$DESKTOP_FILE" ]; then
            run_cmd chmod 644 "$DESKTOP_FILE"
            echo -e "${GREEN}Desktop entry created at $DESKTOP_FILE${NC}"
        else
            echo -e "${YELLOW}Failed to create desktop entry.${NC}"
        fi
    fi

    # 7. Create default keys.fz macro config if not existing
    CONFIG_DIR="$HOME/.config/fyzenor"
    KEYS_FILE="$CONFIG_DIR/keys.fz"
    CONFIG_FILE="$CONFIG_DIR/config.toml"
    mkdir -p "$CONFIG_DIR"
    if [ ! -f "$KEYS_FILE" ]; then
        echo -e "${BLUE}Generating default keyboard macros config in $KEYS_FILE...${NC}"
        cat <<EOF > "$KEYS_FILE"
# Fyzenor Custom Keys Macro Configuration
# Format: single_key=command
# Macros:
#   \$f - expands to the currently highlighted file's absolute path
#   \$s - expands to space-separated paths of all selected files
# Examples:
#   v=nvim "\$f"
#   g=git status
#   l=ls -la
EOF
        echo -e "${GREEN}Default keys.fz generated!${NC}"
    fi

    # 8. Create default config.toml if not existing
    if [ ! -f "$CONFIG_FILE" ]; then
        if [ -f "config.toml" ]; then
            cp "config.toml" "$CONFIG_FILE"
            echo -e "${GREEN}Default config.toml copied to $CONFIG_FILE!${NC}"
        else
            echo -e "${BLUE}Generating default config.toml in $CONFIG_FILE...${NC}"
            cat <<EOF > "$CONFIG_FILE"
# Fyzenor Default Configuration File
# This file is automatically copied to ~/.config/fyzenor/config.toml on launch if missing.

[general]
# Show hidden files by default
show_hidden = false

# Default sorting mode: "name", "size" (descending), or "date" (descending)
sort_mode = "name"

[layout]
# Width percentages for the parent and current columns in normal mode (must sum to < 1.0)
parent_width = 0.18
current_width = 0.32

# Set to true to hide the preview pane or the parent directory listings by default
hide_preview = false
hide_parent = false

[icons]
# Glyph icons used for different file categories and states (Nerd Fonts required)
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

[categories]
# Associate file extensions with styling and behavior groups
video = [".mp4", ".mkv", ".avi", ".mov", ".flv", ".wmv", ".webm", ".m4v", ".mpg", ".mpeg"]
image = [".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".tiff", ".ico", ".psd", ".ai"]
frontend = [".js", ".jsx", ".ts", ".tsx", ".css", ".scss", ".sass", ".less", ".styl", ".vue", ".html", ".svelte", ".htm", ".astro", ".mjx", ".dart", ".swift"]
scripts = [".sh", ".bash", ".zsh", ".fish", ".ksh", ".command", ".pl", ".pm", ".t", ".awk", ".ps1", ".psm1", ".bat", ".cmd", ".vbs", ".wsf"]
config = [".json", ".json5", ".jsonc", ".xml", ".xsd", ".xsl", ".gpx", ".yaml", ".yml", ".toml", ".ini", ".conf", ".cfg", ".prefs", ".properties", ".lock", ".env", ".dockerfile", ".gitignore", ".gitconfig", ".gitattributes", ".gitmodules"]
documentation = [".md", ".markdown", ".txt", ".text", ".log", ".pdf", ".doc", ".docx", ".odt", ".rtf", ".ppt", ".pptx", ".odp", ".xls", ".xlsx", ".ods", ".csv"]
core = [".py", ".pyw", ".ipynb", ".pyc", ".pyd", ".rb", ".ru", ".gemspec", ".php", ".cpp", ".cxx", ".cc", ".hpp", ".hxx", ".ixx", ".c", ".h", ".rs", ".java", ".class", ".jar", ".war", ".go", ".lua", ".sql", ".db", ".sqlite", ".sqlite3", ".db3", ".mdb", ".accdb", ".cmake", ".make", ".diff", ".patch", ".kt", ".kts", ".cs", ".csx", ".scala", ".sc", ".hs", ".lhs", ".clj", ".cljs", ".cljc", ".edn", ".r", ".rmd", ".jl", ".fs", ".fsi", ".fsx"]
font = [".woff", ".woff2", ".ttf", ".eot", ".otf"]
audio = [".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".wma", ".opus", ".mid", ".midi"]
archive = [".zip", ".tar", ".gz", ".tgz", ".7z", ".rar", ".xz", ".bz2", ".tbz2", ".lzma", ".cab"]
EOF
            echo -e "${GREEN}Default config.toml generated!${NC}"
        fi
    fi
else
    echo -e "${RED}Failed to move binary to $INSTALL_PATH. You may need to move it manually.${NC}"
    exit 1
fi


# 7. Cleanup if we used a temp dir
if [[ "$PWD" == "/tmp/"* ]]; then
    cd - > /dev/null
fi

echo -e "\n${GREEN}Done!${NC} Run ${YELLOW}fyzenor${NC} to start."
echo -e "Tip: Use ${YELLOW}fyzenor --version${NC} to check the current version."
