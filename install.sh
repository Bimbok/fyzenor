#!/bin/bash

# Fyzenor Installer Script

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔══╦ ╦╔═╗╔═╗╔╗╔╔═╗╦═╗${NC}"
echo -e "${GREEN}╠══╚╦╝╔═╝║╣ ║║║║ ║╠╦╝${NC}"
echo -e "${GREEN}╩   ╩ ╚═╝╚═╝╝╚╝╚═╝╩╚═${NC}"
echo -e "${YELLOW}Fyzenor Installer${NC}\n"

# Check if running on Linux or macOS
OS="$(uname -s)"
if [ "$OS" != "Linux" ] && [ "$OS" != "Darwin" ]; then
    echo -e "${RED}Error: Fyzenor is only supported on Linux and macOS.${NC}"
    exit 1
fi

# Function to check dependency
check_dep() {
    if ! command -v $1 &> /dev/null; then
        return 1
    else
        return 0
    fi
}

# Dependencies list
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

# Check for ncurses development files
if [ "$OS" == "Linux" ]; then
    if ! ld -lncursesw 2>/dev/null; then
        MISSING_DEPS+=("libncursesw-dev")
    fi
fi

if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo -e "${YELLOW}The following dependencies are missing:${NC}"
    for dep in "${MISSING_DEPS[@]}"; do
        echo -e " - $dep"
    done
    echo ""
    echo -e "On Debian/Ubuntu, you can install them with:"
    echo -e "${GREEN}sudo apt update && sudo apt install build-essential libncursesw5-dev ffmpeg zip bat${NC}"
    echo ""
    read -p "Do you want to continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo -e "${YELLOW}Compiling Fyzenor...${NC}"
g++ -std=c++17 -O3 file_manager.cpp -o fyzenor -lncursesw -lpthread

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Compilation successful!${NC}"
else
    echo -e "${RED}Compilation failed.${NC}"
    exit 1
fi

echo ""
read -p "Do you want to install Fyzenor to /usr/local/bin/? (requires sudo) (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo mv fyzenor /usr/local/bin/
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Fyzenor installed to /usr/local/bin/fyzenor${NC}"
    else
        echo -e "${RED}Failed to move binary to /usr/local/bin/.${NC}"
        echo -e "You can find the binary in the current directory as './fyzenor'."
    fi
else
    echo -e "Skipping installation. Binary is available as './fyzenor'."
fi

echo -e "\n${GREEN}Installation complete!${NC}"
echo -e "Make sure you have a ${YELLOW}Nerd Font${NC} installed for the best experience."
echo -e "Run ${GREEN}fyzenor --help${NC} for more information."
