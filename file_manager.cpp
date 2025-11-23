/**
 * Yazi-like File Manager (C++)
 * * Features:
 * - 3-Column Layout
 * - Vim-style navigation
 * - Nerd Fonts Icons (Requires libncursesw)
 * - Kitty Terminal Image/Video Preview Protocol
 * - File Operations: Copy, Cut, Paste, Rename, New File/Folder, Zip
 * - Multi-select: Space/v to toggle, Esc to clear, 'a' for all
 * - Hidden Files: Toggle with '.'
 * * * * Dependencies (Install these first!):
 * - libncursesw (Wide char support): sudo apt install libncursesw5-dev
 * - ffmpeg (REQUIRED for previews):  sudo apt install ffmpeg
 * - zip (REQUIRED for zipping):      sudo apt install zip
 * * * * Compile (Note the -lncursesw flag):
 * g++ -std=c++17 -O3 file_manager.cpp -o fm -lncursesw
 */

#define _XOPEN_SOURCE_EXTENDED // Required for ncurses wide char support
#include <algorithm>
#include <array>
#include <clocale>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ncurses.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Configuration
const std::set<std::string> VIDEO_EXTS = {".mp4", ".mkv", ".avi", ".mov",
                                          ".flv", ".wmv", ".webm"};
const std::set<std::string> IMAGE_EXTS = {".png", ".jpg",  ".jpeg", ".gif",
                                          ".bmp", ".webp", ".svg",  ".tiff"};
const std::set<std::string> CODE_EXTS = {
    ".cpp", ".h",    ".py",  ".js",   ".ts", ".rs",  ".c",    ".txt",
    ".md",  ".json", ".css", ".html", ".sh", ".lua", ".conf", ".yaml"};

// Nerd Font Icons (Wide Strings)
const char *ICON_DIR = " ";   // nf-fa-folder
const char *ICON_VIDEO = " "; // nf-fa-film
const char *ICON_IMAGE = " "; // nf-fa-file_image_o
const char *ICON_CODE = " ";  // nf-fa-code
const char *ICON_FILE = " ";  // nf-fa-file_o
const char *ICON_MUSIC = " "; // nf-fa-music

// Temp file for preview generation (Matches Kitty f=100 expectation)
const std::string PREVIEW_TEMP = "/tmp/fm_preview_thumb.png";

struct Clipboard {
  std::vector<fs::path> paths; // Support multiple files
  bool isCut = false;
};

struct FileEntry {
  fs::path path;
  std::string name;
  bool is_directory;
  uintmax_t size;
  std::string extension;

  FileEntry(const fs::path &p) : path(p) {
    name = p.filename().string();
    is_directory = fs::is_directory(p);
    extension = p.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   ::tolower);
    try {
      size = is_directory ? 0 : fs::file_size(p);
    } catch (...) {
      size = 0;
    }
  }
};

// Base64 Encoder for Kitty Protocol
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789+/";

std::string base64_encode(const unsigned char *bytes, size_t len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (len--) {
    char_array_3[i++] = *(bytes++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] =
          ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] =
          ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;
      for (i = 0; (i < 4); i++)
        ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }
  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';
    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] =
        ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] =
        ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;
    for (j = 0; (j < i + 1); j++)
      ret += base64_chars[char_array_4[j]];
    while ((i++ < 3))
      ret += '=';
  }
  return ret;
}

std::string formatSize(uintmax_t size) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int i = 0;
  double dSize = static_cast<double>(size);
  while (dSize > 1024 && i < 4) {
    dSize /= 1024;
    i++;
  }
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.1f %s", dSize, units[i]);
  return std::string(buffer);
}

class FileManager {
private:
  fs::path currentPath;
  std::vector<FileEntry> currentFiles;
  std::vector<FileEntry> parentFiles;
  std::set<fs::path> multiSelection; // Track selected files
  size_t selectedIndex;
  size_t scrollOffset;

  WINDOW *winParent, *winCurrent, *winPreview;
  int width, height;
  bool lastWasImage = false; // Track if we need to clear graphics

  Clipboard clipboard;
  std::string statusMessage;
  bool showHidden = false; // Default: hidden files are hidden

public:
  FileManager()
      : selectedIndex(0), scrollOffset(0), winParent(nullptr),
        winCurrent(nullptr), winPreview(nullptr) {
    setlocale(LC_ALL, "");

    currentPath = fs::current_path();
    loadDirectory(currentPath, currentFiles);
    loadParent();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();

    // Define Colors
    init_pair(1, COLOR_BLUE, -1);          // Dir
    init_pair(2, COLOR_WHITE, -1);         // File
    init_pair(3, COLOR_BLACK, COLOR_CYAN); // Cursor selection
    init_pair(4, COLOR_YELLOW, -1);        // Video
    init_pair(5, COLOR_MAGENTA, -1);       // Image
    init_pair(6, COLOR_GREEN, -1);         // Border
    init_pair(7, COLOR_CYAN, -1);          // Info
    init_pair(8, COLOR_RED, -1);           // Error
    init_pair(9, COLOR_YELLOW, -1); // Multi-selected item (Bright Yellow)

    refresh();
  }

  void clearKittyImage() {
    std::cout << "\033_Ga=d,q=2\033\\" << std::flush;
    lastWasImage = false;
  }

  ~FileManager() {
    clearKittyImage();
    endwin();
  }

  const char *getIcon(const FileEntry &f) {
    if (f.is_directory)
      return ICON_DIR;
    if (VIDEO_EXTS.count(f.extension))
      return ICON_VIDEO;
    if (IMAGE_EXTS.count(f.extension))
      return ICON_IMAGE;
    if (CODE_EXTS.count(f.extension))
      return ICON_CODE;
    return ICON_FILE;
  }

  void loadDirectory(const fs::path &path, std::vector<FileEntry> &target) {
    target.clear();
    multiSelection
        .clear(); // Clear selection when changing folders for simplicity
    try {
      // Filter and add Directories
      for (const auto &entry : fs::directory_iterator(path)) {
        // Hidden file check
        if (!showHidden && entry.path().filename().string().front() == '.')
          continue;

        if (fs::is_directory(entry))
          target.emplace_back(entry);
      }

      std::sort(target.begin(), target.end(),
                [](const FileEntry &a, const FileEntry &b) {
                  return a.name < b.name;
                });
      size_t split = target.size();

      // Filter and add Files
      for (const auto &entry : fs::directory_iterator(path)) {
        // Hidden file check
        if (!showHidden && entry.path().filename().string().front() == '.')
          continue;

        if (!fs::is_directory(entry))
          target.emplace_back(entry);
      }

      std::sort(target.begin() + split, target.end(),
                [](const FileEntry &a, const FileEntry &b) {
                  return a.name < b.name;
                });
    } catch (...) {
    }
  }

  void loadParent() {
    if (currentPath.has_parent_path() &&
        currentPath != currentPath.parent_path()) {
      loadDirectory(currentPath.parent_path(), parentFiles);
    } else {
      parentFiles.clear();
    }
  }

  void updateLayout() {
    getmaxyx(stdscr, height, width);
    int w1 = width * 0.2;
    int w2 = width * 0.4;
    int w3 = width - w1 - w2;

    if (winParent)
      delwin(winParent);
    if (winCurrent)
      delwin(winCurrent);
    if (winPreview)
      delwin(winPreview);

    winParent = newwin(height - 1, w1, 0, 0);
    winCurrent = newwin(height - 1, w2, 0, w1);
    winPreview = newwin(height - 1, w3, 0, w1 + w2);

    refresh();
  }

  // --- Kitty Image Protocol Logic ---

  void drawKittyImage(const std::string &path, bool isVideo) {
    int pW, pH, pX, pY;
    getmaxyx(winPreview, pH, pW);
    getbegyx(winPreview, pY, pX);

    std::string cmd;
    if (isVideo) {
      cmd = "ffmpeg -y -v error -i \"" + path +
            "\" -vf \"thumbnail,scale=400:-1\" -frames:v 1 -f image2 " +
            PREVIEW_TEMP;
    } else {
      cmd = "ffmpeg -y -v error -i \"" + path +
            "\" -vf \"scale=400:-1\" -f image2 " + PREVIEW_TEMP;
    }

    int res = system(cmd.c_str());
    if (res != 0)
      return;

    std::ifstream file(PREVIEW_TEMP, std::ios::binary);
    if (!file)
      return;
    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
    std::string b64 = base64_encode(buffer.data(), buffer.size());

    // FIX: Moved from pY+3 to pY+7 to avoid overlapping "Name" and "Size" text
    std::cout << "\033[" << (pY + 7) << ";" << (pX + 2) << "H";

    const size_t chunk_size = 4096;
    size_t total = b64.length();
    size_t offset = 0;

    std::cout << "\033_Gf=100,a=T,t=d,m=1,q=2;";

    while (offset < total) {
      size_t chunk = std::min(chunk_size, total - offset);
      if (offset + chunk >= total) {
        std::cout << "\033_Gm=0;" << b64.substr(offset, chunk) << "\033\\";
      } else {
        std::cout << "\033_Gm=1;" << b64.substr(offset, chunk) << "\033\\";
      }
      offset += chunk;
    }
    std::cout << std::flush;
    lastWasImage = true;
  }

  // --- Operations ---

  void setStatus(const std::string &msg) { statusMessage = msg; }

  std::string promptInput(const std::string &prompt) {
    move(height - 1, 0);
    clrtoeol();
    attron(COLOR_PAIR(7) | A_BOLD);
    mvprintw(height - 1, 0, "%s: ", prompt.c_str());
    attroff(COLOR_PAIR(7) | A_BOLD);
    refresh();

    echo();
    curs_set(1);
    char buf[256];
    getnstr(buf, 255);
    noecho();
    curs_set(0);
    return std::string(buf);
  }

  // --- Multi-Select Logic ---
  void toggleSelection() {
    if (currentFiles.empty())
      return;
    fs::path p = currentFiles[selectedIndex].path;
    if (multiSelection.count(p)) {
      multiSelection.erase(p);
    } else {
      multiSelection.insert(p);
    }
    // Auto advance
    if (selectedIndex < currentFiles.size() - 1)
      selectedIndex++;
  }

  void selectAll() {
    for (const auto &f : currentFiles) {
      multiSelection.insert(f.path);
    }
    setStatus("Selected all files");
  }

  void clearSelection() {
    multiSelection.clear();
    setStatus("Cleared selection");
  }

  void handleCopy() {
    if (currentFiles.empty())
      return;
    clipboard.paths.clear();

    if (multiSelection.empty()) {
      clipboard.paths.push_back(currentFiles[selectedIndex].path);
    } else {
      for (const auto &p : multiSelection)
        clipboard.paths.push_back(p);
    }

    clipboard.isCut = false;
    setStatus("Yanked " + std::to_string(clipboard.paths.size()) + " items");
    multiSelection.clear();
  }

  void handleCut() {
    if (currentFiles.empty())
      return;
    clipboard.paths.clear();

    if (multiSelection.empty()) {
      clipboard.paths.push_back(currentFiles[selectedIndex].path);
    } else {
      for (const auto &p : multiSelection)
        clipboard.paths.push_back(p);
    }

    clipboard.isCut = true;
    setStatus("Cut " + std::to_string(clipboard.paths.size()) + " items");
    multiSelection.clear();
  }

  void handlePaste() {
    if (clipboard.paths.empty()) {
      setStatus("Clipboard empty");
      return;
    }

    int successCount = 0;
    std::string errorMsg;

    for (const auto &src : clipboard.paths) {
      fs::path dest = currentPath / src.filename();
      if (fs::exists(dest) && !clipboard.isCut && src != dest) {
        // Simple conflict resolution: skip
        continue;
      }

      try {
        if (clipboard.isCut) {
          fs::rename(src, dest);
        } else {
          if (fs::is_directory(src))
            fs::copy(src, dest, fs::copy_options::recursive);
          else
            fs::copy(src, dest);
        }
        successCount++;
      } catch (const std::exception &e) {
        errorMsg = e.what();
      }
    }

    if (clipboard.isCut && successCount > 0) {
      clipboard.paths.clear(); // Clear clipboard after move
    }

    setStatus(clipboard.isCut
                  ? "Moved "
                  : "Pasted " + std::to_string(successCount) + " items");
    if (!errorMsg.empty())
      setStatus("Error: " + errorMsg);

    reloadAll();
  }

  void handleRename() {
    if (currentFiles.empty())
      return;
    const auto &file = currentFiles[selectedIndex];
    std::string newName = promptInput("Rename " + file.name + " to");
    if (newName.empty())
      return;
    fs::path dest = currentPath / newName;
    try {
      fs::rename(file.path, dest);
      setStatus("Renamed to " + newName);
      reloadAll();
    } catch (std::exception &e) {
      setStatus("Rename Failed: " + std::string(e.what()));
    }
  }

  void handleNewFile() {
    std::string name = promptInput("New File Name");
    if (name.empty())
      return;
    fs::path dest = currentPath / name;
    if (fs::exists(dest)) {
      setStatus("File exists");
      return;
    }
    std::ofstream(dest).close();
    setStatus("Created file: " + name);
    reloadAll();
  }

  void handleNewFolder() {
    std::string name = promptInput("New Folder Name");
    if (name.empty())
      return;
    fs::path dest = currentPath / name;
    try {
      if (fs::create_directory(dest)) {
        setStatus("Created folder: " + name);
        reloadAll();
      } else
        setStatus("Failed to create folder");
    } catch (std::exception &e) {
      setStatus("Error: " + std::string(e.what()));
    }
  }

  void handleZip() {
    if (currentFiles.empty())
      return;

    std::vector<fs::path> targets;
    if (multiSelection.empty()) {
      targets.push_back(currentFiles[selectedIndex].path);
    } else {
      for (const auto &p : multiSelection)
        targets.push_back(p);
    }

    std::string name = promptInput("Zip Name (without .zip)");
    if (name.empty())
      return;

    // Construct command: zip -r -q "name.zip" "file1" "file2" ...
    std::string cmd = "zip -r -q \"" + name + ".zip\"";
    for (const auto &p : targets) {
      cmd += " \"" + p.filename().string() + "\"";
    }

    fs::path old = fs::current_path();
    fs::current_path(currentPath);

    int res = system(cmd.c_str());
    (void)res;

    fs::current_path(old);

    if (res == 0) {
      setStatus("Zipped " + std::to_string(targets.size()) + " items to " +
                name + ".zip");
      multiSelection.clear();
      reloadAll();
    } else {
      setStatus("Zip failed");
    }
  }

  void reloadAll() {
    loadDirectory(currentPath, currentFiles);
    loadParent();
  }

  void toggleHidden() {
    showHidden = !showHidden;
    reloadAll();
    setStatus(showHidden ? "Showing hidden files" : "Hidden files masked");
  }

  // ------------------------------------

  void drawParent() {
    werase(winParent);
    box(winParent, 0, 0);
    int maxLines = height - 2;
    int highlightIdx = -1;
    for (size_t i = 0; i < parentFiles.size(); ++i) {
      if (parentFiles[i].path == currentPath) {
        highlightIdx = i;
        break;
      }
    }
    int start = 0;
    if (highlightIdx > maxLines / 2)
      start = highlightIdx - (maxLines / 2);
    if (start + maxLines > parentFiles.size() && parentFiles.size() > maxLines)
      start = parentFiles.size() - maxLines;

    for (int i = 0; i < maxLines && (start + i) < parentFiles.size(); ++i) {
      const auto &file = parentFiles[start + i];
      bool isCurrent = (static_cast<int>(start + i) == highlightIdx);
      wmove(winParent, i + 1, 1);
      if (isCurrent)
        wattron(winParent, A_BOLD | COLOR_PAIR(7));
      else
        wattron(winParent, A_DIM);
      std::string display = file.name;
      if (display.length() > getmaxx(winParent) - 5)
        display = display.substr(0, getmaxx(winParent) - 8) + "...";
      wprintw(winParent, "%s %s", getIcon(file), display.c_str());
      if (isCurrent)
        wattroff(winParent, A_BOLD | COLOR_PAIR(7));
      else
        wattroff(winParent, A_DIM);
    }
    wrefresh(winParent);
  }

  void drawCurrent() {
    werase(winCurrent);
    wattron(winCurrent, COLOR_PAIR(6));
    box(winCurrent, 0, 0);
    wattroff(winCurrent, COLOR_PAIR(6));
    wattron(winCurrent, A_BOLD);
    mvwprintw(winCurrent, 0, 2, " %s ", currentPath.filename().c_str());
    wattroff(winCurrent, A_BOLD);

    // Selection indicator
    if (!multiSelection.empty()) {
      std::string selStr =
          "[" + std::to_string(multiSelection.size()) + " sel]";
      mvwprintw(winCurrent, 0, getmaxx(winCurrent) - selStr.length() - 2, "%s",
                selStr.c_str());
    }

    int maxLines = height - 3;
    if (selectedIndex < scrollOffset)
      scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + maxLines)
      scrollOffset = selectedIndex - maxLines + 1;

    for (int i = 0; i < maxLines && (scrollOffset + i) < currentFiles.size();
         ++i) {
      int idx = scrollOffset + i;
      const auto &file = currentFiles[idx];

      wmove(winCurrent, i + 1, 1);
      int colorPair = 2;
      bool isMultiSelected = multiSelection.count(file.path);

      // Selection Logic
      if (idx == selectedIndex) {
        wattron(winCurrent, COLOR_PAIR(3)); // Cursor highlight
      } else if (isMultiSelected) {
        wattron(winCurrent, COLOR_PAIR(9) | A_BOLD); // Multi-select highlight
      } else {
        if (file.is_directory)
          colorPair = 1;
        else if (VIDEO_EXTS.count(file.extension))
          colorPair = 4;
        else if (IMAGE_EXTS.count(file.extension))
          colorPair = 5;
        wattron(winCurrent, COLOR_PAIR(colorPair));
      }

      std::string display = file.name;
      int availWidth = getmaxx(winCurrent) - 16;
      if (display.length() > availWidth)
        display = display.substr(0, availWidth - 3) + "...";

      // Marker for multi-select
      char marker = isMultiSelected ? '*' : ' ';
      wprintw(winCurrent, " %c %s %-s", marker, getIcon(file), display.c_str());

      std::string sz = formatSize(file.size);
      mvwprintw(winCurrent, i + 1, getmaxx(winCurrent) - sz.length() - 2, "%s",
                sz.c_str());

      if (idx == selectedIndex)
        wattroff(winCurrent, COLOR_PAIR(3));
      else if (isMultiSelected)
        wattroff(winCurrent, COLOR_PAIR(9) | A_BOLD);
      else
        wattroff(winCurrent, COLOR_PAIR(colorPair));
    }
    wrefresh(winCurrent);
  }

  void drawPreview() {
    if (lastWasImage)
      clearKittyImage();
    werase(winPreview);
    box(winPreview, 0, 0);
    mvwprintw(winPreview, 0, 2, " Preview ");

    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) {
      wrefresh(winPreview);
      return;
    }

    const auto &file = currentFiles[selectedIndex];
    int maxW = getmaxx(winPreview) - 4;

    wmove(winPreview, 2, 2);
    wattron(winPreview, A_BOLD | COLOR_PAIR(7));
    wprintw(winPreview, "Details:");
    wattroff(winPreview, A_BOLD | COLOR_PAIR(7));
    mvwprintw(winPreview, 3, 2, "Name: %s", file.name.c_str());
    mvwprintw(winPreview, 4, 2, "Size: %s", formatSize(file.size).c_str());

    bool isVid = VIDEO_EXTS.count(file.extension);
    bool isImg = IMAGE_EXTS.count(file.extension);

    if (file.is_directory) {
      wattron(winPreview, A_DIM);
      mvwprintw(winPreview, 7, 2, "--- Content ---");
      try {
        int line = 8;
        for (const auto &entry : fs::directory_iterator(file.path)) {
          // Hide hidden files in directory preview too
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;

          if (line >= height - 2)
            break;
          std::string subName = entry.path().filename().string();
          if (subName.length() > maxW)
            subName = subName.substr(0, maxW - 3) + "...";
          mvwprintw(winPreview, line++, 4, "%s %s",
                    fs::is_directory(entry) ? ICON_DIR : ICON_FILE,
                    subName.c_str());
        }
      } catch (...) {
      }
      wattroff(winPreview, A_DIM);
      wrefresh(winPreview);
    } else if (isVid || isImg) {
      wrefresh(winPreview);
      drawKittyImage(file.path.string(), isVid);
    } else if (CODE_EXTS.count(file.extension)) {
      std::ifstream f(file.path);
      if (f.is_open()) {
        std::string lineStr;
        int line = 7;
        while (std::getline(f, lineStr) && line < height - 2) {
          std::replace(lineStr.begin(), lineStr.end(), '\t', ' ');
          if (lineStr.length() > maxW)
            lineStr = lineStr.substr(0, maxW);
          mvwprintw(winPreview, line++, 2, "%s", lineStr.c_str());
        }
      }
      wrefresh(winPreview);
    } else {
      wrefresh(winPreview);
    }
  }

  void openFile() {
    if (currentFiles.empty())
      return;
    const auto &file = currentFiles[selectedIndex];

    if (file.is_directory) {
      clearKittyImage();
      currentPath = file.path;
      selectedIndex = 0;
      scrollOffset = 0;
      reloadAll();
    } else {
      clearKittyImage();
      def_prog_mode();
      endwin();

      std::string cmd;
      if (VIDEO_EXTS.count(file.extension))
        cmd = "mpv \"" + file.path.string() + "\"";
      else {
#ifdef __APPLE__
        cmd = "open \"" + file.path.string() + "\"";
#else
        cmd = "xdg-open \"" + file.path.string() + "\"";
#endif
      }
      int res = system(cmd.c_str());
      (void)res;

      reset_prog_mode();
      refresh();
    }
  }

  void goUp() {
    if (currentPath.has_parent_path() &&
        currentPath != currentPath.parent_path()) {
      clearKittyImage();
      std::string oldDirName = currentPath.filename().string();
      currentPath = currentPath.parent_path();
      reloadAll();

      selectedIndex = 0;
      for (size_t i = 0; i < currentFiles.size(); ++i) {
        if (currentFiles[i].name == oldDirName) {
          selectedIndex = i;
          break;
        }
      }
      scrollOffset = (selectedIndex > 10) ? selectedIndex - 10 : 0;
    }
  }

  void run() {
    updateLayout();
    while (true) {
      if (!currentFiles.empty()) {
        if (selectedIndex >= currentFiles.size())
          selectedIndex = currentFiles.size() - 1;
      } else
        selectedIndex = 0;

      drawParent();
      drawCurrent();
      drawPreview();

      move(height - 1, 0);
      clrtoeol();
      if (!statusMessage.empty()) {
        attron(COLOR_PAIR(7));
        printw("%s", statusMessage.c_str());
        attroff(COLOR_PAIR(7));
      } else {
        attron(A_DIM);
        printw("Space/v:Select Esc:Clear a:All y:Cp x:Cut p:Pst z:Zip n:New "
               ".:Hidden q:Quit");
        attroff(A_DIM);
      }
      refresh();

      int ch = getch();
      if (ch != ERR)
        statusMessage = "";

      switch (ch) {
      case 'q':
        return;
      case 'j':
      case KEY_DOWN:
        if (!currentFiles.empty() && selectedIndex < currentFiles.size() - 1)
          selectedIndex++;
        break;
      case 'k':
      case KEY_UP:
        if (selectedIndex > 0)
          selectedIndex--;
        break;
      case 'l':
      case KEY_RIGHT:
      case 10:
        openFile();
        break;
      case 'h':
      case KEY_LEFT:
      case 127:
      case KEY_BACKSPACE:
        goUp();
        break;
      case 'g':
        selectedIndex = 0;
        scrollOffset = 0;
        break;
      case 'G':
        if (!currentFiles.empty()) {
          selectedIndex = currentFiles.size() - 1;
          if (selectedIndex > height - 5)
            scrollOffset = selectedIndex - (height - 5);
        }
        break;

      // Multi-Select
      case ' ':
      case 'v':
        toggleSelection();
        break;
      case 'a':
        selectAll();
        break;
      case 27:
        clearSelection();
        break; // ESC

      // Operations
      case 'y':
        handleCopy();
        break;
      case 'x':
        handleCut();
        break;
      case 'p':
        handlePaste();
        break;
      case 'r':
        handleRename();
        break;
      case 'n':
        handleNewFile();
        break;
      case 'N':
        handleNewFolder();
        break;
      case 'z':
        handleZip();
        break;
      case '.':
        toggleHidden();
        break;

      case KEY_RESIZE:
        clearKittyImage();
        updateLayout();
        break;
      }
    }
  }
};

int main() {
  FileManager fm;
  fm.run();
  return 0;
}
