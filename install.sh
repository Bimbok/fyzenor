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
DEPS=("g++" "ffmpeg" "zip")
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
    echo -e "${YELLOW}Some features (previews, zip) might not work until installed.${NC}"
    echo -e "On Debian/Ubuntu: ${GREEN}sudo apt install libncursesw5-dev ffmpeg zip bat${NC}\n"
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

# 6. Shell Integration (CWD on exit)
echo -e "${BLUE}Setting up shell integration for directory jumping...${NC}"

SHELL_FUNC='
# Fyzenor shell integration (jump to CWD on exit)
function f() {
    local tmp="$(mktemp -t "fyzenor-cwd.XXXXXX")" cwd
    fyzenor "$@" --cwd-file="$tmp"
    if [ -f "$tmp" ]; then
        cwd=$(cat "$tmp")
        rm -f -- "$tmp"
        if [ -n "$cwd" ] && [ "$cwd" != "$PWD" ]; then
            builtin cd -- "$cwd"
        fi
    fi
}
'

setup_shell() {
    local config_file="$1"
    if [ -f "$config_file" ]; then
        if ! grep -q "function f()" "$config_file"; then
            echo -e "$SHELL_FUNC" >> "$config_file"
            echo -e "${GREEN}Shell integration added to $config_file${NC}"
        else
            echo -e "${YELLOW}Shell integration already exists in $config_file${NC}"
        fi
    fi
}

setup_shell "$HOME/.bashrc"
setup_shell "$HOME/.zshrc"

# 7. Cleanup if we used a temp dir
if [[ "$PWD" == "/tmp/"* ]]; then
    cd - > /dev/null
fi

echo -e "\n${GREEN}Done!${NC} Run ${YELLOW}fyzenor${NC} to start."
echo -e "Tip: Use ${YELLOW}fyzenor --version${NC} to check the current version."
