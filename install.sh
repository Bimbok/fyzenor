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
INSTALL_PATH="/usr/local/bin/fyzenor"

# Function to check dependency
check_dep() {
    if ! command -v $1 &> /dev/null; then
        return 1
    else
        return 0
    fi
}

detect_platform() {
    case "$(uname -s 2>/dev/null)" in
        MINGW*|MSYS*|CYGWIN*) echo "windows";;
        Darwin*) echo "macos";;
        Linux*) echo "linux";;
        *) echo "unknown";;
    esac
}

install_hint() {
    local platform="$1"
    case "$platform" in
        windows)
            if check_dep "pacman"; then
                echo -e "This looks like an MSYS2-style shell. Install the MinGW packages, then rerun this installer:"
                echo -e "  ${GREEN}pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-ncurses mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-zip mingw-w64-x86_64-bat${NC}"
                echo -e "If this is Git Bash rather than MSYS2, use WSL/Ubuntu instead."
                return
            fi
            echo -e "This looks like Windows Git Bash. Git Bash does not include ncurses development headers."
            echo -e "Do not run the apt command in this Git Bash window."
            echo -e "Recommended: install and run Fyzenor inside WSL/Ubuntu:"
            echo -e "  ${GREEN}wsl --install -d Ubuntu${NC}"
            echo -e "Then open Ubuntu and run:"
            echo -e "  ${GREEN}cd /mnt/c/projects/fyzenor${NC}"
            echo -e "  ${GREEN}sudo apt update${NC}"
            echo -e "  ${GREEN}sudo apt install build-essential libncursesw5-dev ffmpeg zip bat${NC}"
            echo -e "  ${GREEN}./install.sh${NC}"
            echo -e "Alternative: install MSYS2, open its MINGW64 shell, then run:"
            echo -e "  ${GREEN}pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-ncurses mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-zip mingw-w64-x86_64-bat${NC}"
            ;;
        macos)
            echo -e "On macOS:"
            echo -e "  ${GREEN}brew install ncurses ffmpeg zip bat${NC}"
            ;;
        *)
            echo -e "On Debian/Ubuntu:"
            echo -e "  ${GREEN}sudo apt install build-essential libncursesw5-dev ffmpeg zip bat${NC}"
            echo -e "On Fedora:"
            echo -e "  ${GREEN}sudo dnf install gcc gcc-c++ make ncurses-devel ffmpeg zip bat${NC}"
            ;;
    esac
}

check_ncurses_header() {
    check_dep "g++" || return 1
    printf '#include <ncurses.h>\nint main(){return 0;}\n' | g++ -x c++ -fsyntax-only - >/dev/null 2>&1
}

# 1. Handle "Run from anywhere" (curl | bash)
if [ ! -f "file_manager.cpp" ]; then
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
PLATFORM="$(detect_platform)"
REQUIRED_DEPS=("g++")
OPTIONAL_DEPS=("ffmpeg" "zip")
MISSING_REQUIRED=()
MISSING_OPTIONAL=()

for dep in "${REQUIRED_DEPS[@]}"; do
    if ! check_dep "$dep"; then
        MISSING_REQUIRED+=("$dep")
    fi
done

if ! check_ncurses_header; then
    MISSING_REQUIRED+=("ncurses development headers (ncurses.h)")
fi

for dep in "${OPTIONAL_DEPS[@]}"; do
    if ! check_dep "$dep"; then
        MISSING_OPTIONAL+=("$dep")
    fi
done

# Special check for bat/batcat
if ! check_dep "bat" && ! check_dep "batcat"; then
    MISSING_OPTIONAL+=("bat (or batcat)")
fi

if [ ${#MISSING_REQUIRED[@]} -ne 0 ]; then
    echo -e "${RED}Error: The following required build dependencies are missing:${NC}"
    for dep in "${MISSING_REQUIRED[@]}"; do
        echo -e " - $dep"
    done
    install_hint "$PLATFORM"
    exit 1
fi

if [ ${#MISSING_OPTIONAL[@]} -ne 0 ]; then
    echo -e "${YELLOW}Warning: The following optional dependencies are missing:${NC}"
    for dep in "${MISSING_OPTIONAL[@]}"; do
        echo -e " - $dep"
    done
    echo -e "${YELLOW}Some features (previews, zip) might not work until installed.${NC}"
    install_hint "$PLATFORM"
    echo
fi

# 4. Compilation
echo -e "${BLUE}Compiling Fyzenor...${NC}"
if g++ -std=c++17 -O3 file_manager.cpp -o fyzenor -lncursesw -lpthread; then
    echo -e "${GREEN}Compilation successful!${NC}"
else
    echo -e "${RED}Compilation failed. Please check if 'libncursesw-dev' and 'g++' are installed.${NC}"
    exit 1
fi

# 5. Installation/Update
if [ -f "$INSTALL_PATH" ]; then
    echo -e "${YELLOW}Existing installation found at $INSTALL_PATH. Updating...${NC}"
else
    echo -e "${BLUE}Installing Fyzenor...${NC}"
fi

if sudo mv fyzenor "$INSTALL_PATH"; then
    echo -e "${GREEN}Fyzenor is now installed/updated at $INSTALL_PATH${NC}"
    # Create symlink 'fm'
    if sudo ln -sf "$INSTALL_PATH" "$(dirname "$INSTALL_PATH")/fm"; then
        echo -e "${GREEN}Symlink 'fm' created successfully.${NC}"
    else
        echo -e "${YELLOW}Failed to create symlink 'fm'. You can do it manually: sudo ln -sf $INSTALL_PATH /usr/local/bin/fm${NC}"
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
