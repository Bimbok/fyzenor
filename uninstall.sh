#!/bin/bash
set -e

# Fyzenor Universal Uninstaller
# This script removes Fyzenor and its associated files.
# Usage:
#   ./uninstall.sh           # Keeps ~/.config/fyzenor and ~/.fm_pins
#   ./uninstall.sh --purge   # Removes everything including user configs

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${RED}╔══╦ ╦╔═╗╔═╗╔╗╔╔═╗╦═╗${NC}"
echo -e "${RED}╠══╚╦╝╔═╝║╣ ║║║║ ║╠╦╝${NC}"
echo -e "${RED}╩   ╩ ╚═╝╚═╝╝╚╝╚═╝╩╚═${NC}"
echo -e "${CYAN}Fyzenor Universal Uninstaller${NC}\n"

PURGE=false
ASSUME_YES=false

for arg in "$@"; do
    case $arg in
        --purge)
            PURGE=true
            ;;
        -y|--yes)
            ASSUME_YES=true
            ;;
        -h|--help)
            echo -e "${BOLD}Fyzenor Uninstaller${NC}"
            echo -e "Usage: ./uninstall.sh [OPTIONS]\n"
            echo -e "Options:"
            echo -e "  ${YELLOW}--purge${NC}        Also remove ~/.config/fyzenor and ~/.fm_pins"
            echo -e "  ${YELLOW}-y, --yes${NC}      Skip interactive confirmation prompt"
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

# Determine target paths
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

# Prompt confirmation if interactive
if [ -t 0 ] && ! $ASSUME_YES; then
    echo -e "This will uninstall Fyzenor from: ${YELLOW}$INSTALL_PATH${NC}"
    if $PURGE; then
        echo -e "Warning: ${RED}--purge is enabled! User configs in $CONFIG_DIR will be deleted.${NC}"
    fi
    read -rp "Are you sure you want to proceed? [y/N] " confirm
    case "$confirm" in
        [yY][eE][sS]|[yY])
            ;;
        *)
            echo -e "${YELLOW}Uninstallation cancelled.${NC}"
            exit 0
            ;;
    esac
    echo ""
fi

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

# 3. Remove desktop entry
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

# 5. Handle user configurations
if $PURGE; then
    echo -e "${YELLOW}Purging user configurations and bookmarks...${NC}"
    rm -rf "$CONFIG_DIR" "$PINS_FILE"
    echo -e "${GREEN}✓ Removed $CONFIG_DIR and $PINS_FILE${NC}"
    REMOVED_ANY=true
else
    if [ -d "$CONFIG_DIR" ] || [ -f "$PINS_FILE" ]; then
        echo -e "\n${CYAN}Notice: User configurations preserved in $CONFIG_DIR${NC}"
        echo -e "To completely wipe configs and bookmarks, re-run with: ${YELLOW}./uninstall.sh --purge${NC}"
    fi
fi

if $REMOVED_ANY; then
    echo -e "\n${GREEN}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║             Uninstallation Completed!                    ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════════════════════╝${NC}"
    echo -e "Fyzenor has been successfully removed from your system.\n"
else
    echo -e "\n${YELLOW}No active Fyzenor installation found at $INSTALL_PATH.${NC}\n"
fi
