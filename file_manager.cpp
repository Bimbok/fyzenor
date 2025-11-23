/**
 * Yazi-like File Manager (C++)
 * * Features:
 * - 3-Column Layout with Split Parent/Pinned View
 * - Vim-style navigation
 * - Nerd Fonts Icons
 * - Kitty Terminal Image/Video Preview Protocol
 * - Syntax Highlighting using 'bat' (Async)
 * - File Operations: Copy, Cut, Paste, Rename, New File/Folder, Zip, Delete
 * - Multi-select & Pins
 * * * * Dependencies:
 * - libncursesw, ffmpeg, zip
 * - bat (or batcat) for syntax highlighting
 * * * * Compile:
 * g++ -std=c++17 -O3 file_manager.cpp -o fm -lncursesw -lpthread
 */

#define _XOPEN_SOURCE_EXTENDED
#include <algorithm>
#include <array>
#include <atomic>
#include <clocale>
#include <cstdio> // Required for popen/pclose
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ncurses.h>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Configuration
const std::set<std::string> VIDEO_EXTS = {".mp4", ".mkv", ".avi", ".mov",
                                          ".flv", ".wmv", ".webm"};
const std::set<std::string> IMAGE_EXTS = {".png", ".jpg",  ".jpeg", ".gif",
                                          ".bmp", ".webp", ".svg",  ".tiff"};
const std::set<std::string> CODE_EXTS = {
    ".cpp",  ".h",    ".hpp",   ".c",    ".cc",    ".py",   ".js",
    ".ts",   ".rs",   ".go",    ".java", ".rb",    ".php",  ".html",
    ".css",  ".scss", ".json",  ".xml",  ".yaml",  ".yml",  ".toml",
    ".ini",  ".sh",   ".bash",  ".zsh",  ".lua",   ".md",   ".txt",
    ".conf", ".diff", ".patch", ".sql",  ".cmake", ".make", ".dockerfile"};
const std::set<std::string> AUDIO_EXTS = {".mp3", ".wav", ".flac", ".m4a",
                                          ".aac", ".ogg", ".wma",  ".opus"};

const char *ICON_DIR = " ";
const char *ICON_VIDEO = " ";
const char *ICON_IMAGE = " ";
const char *ICON_CODE = " ";
const char *ICON_FILE = " ";
const char *ICON_MUSIC = " ";
const char *ICON_PIN = " ";

const std::string PREVIEW_TEMP = "/tmp/fm_preview_thumb.png";

struct Clipboard {
  std::vector<fs::path> paths;
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

// Helper to check for binary files (simple heuristic: look for null byte)
bool is_binary_file(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return false;
  char buffer[512];
  file.read(buffer, sizeof(buffer));
  size_t n = file.gcount();
  for (size_t i = 0; i < n; ++i) {
    if (buffer[i] == '\0')
      return true;
  }
  return false;
}

enum class PreviewType { NONE, IMAGE, TEXT };

class FileManager {
private:
  fs::path currentPath;
  std::vector<FileEntry> currentFiles;
  std::vector<FileEntry> parentFiles;
  std::set<fs::path> multiSelection;
  std::vector<fs::path> pinnedPaths;
  size_t pinnedIndex = 0;
  bool focusPinned = false;
  size_t selectedIndex;
  size_t scrollOffset;

  WINDOW *winPinned, *winParent, *winCurrent, *winPreview;
  int width, height;

  Clipboard clipboard;
  std::string statusMessage;
  bool showHidden = false;

  // Async Preview State
  std::mutex previewMutex;
  std::atomic<bool> imageReady{false};
  std::string cachedBase64;
  std::vector<std::string> cachedTextLines;
  std::string cachedPath;
  std::string requestedPath;
  long long requestID = 0;
  bool lastWasDirectRender = false;

public:
  FileManager()
      : selectedIndex(0), scrollOffset(0), winPinned(nullptr),
        winParent(nullptr), winCurrent(nullptr), winPreview(nullptr) {
    setlocale(LC_ALL, "");
    loadPins();
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
    timeout(50);

    init_pair(1, COLOR_BLUE, -1);
    init_pair(2, COLOR_WHITE, -1);
    init_pair(3, COLOR_BLACK, COLOR_CYAN);
    init_pair(4, COLOR_YELLOW, -1);
    init_pair(5, COLOR_MAGENTA, -1);
    init_pair(6, COLOR_GREEN, -1);
    init_pair(7, COLOR_CYAN, -1);
    init_pair(8, COLOR_RED, -1);
    init_pair(9, COLOR_YELLOW, -1);
    init_pair(10, COLOR_WHITE, COLOR_BLUE);

    refresh();
  }

  void clearDirectRender() {
    // q=2 suppresses errors
    std::cout << "\033_Ga=d,q=2\033\\" << std::flush;
    lastWasDirectRender = false;
  }

  ~FileManager() {
    clearDirectRender();
    endwin();
  }

  // --- Pin Management ---
  std::string getPinFile() {
    const char *home = getenv("HOME");
    if (home)
      return std::string(home) + "/.fm_pins";
    return ".fm_pins";
  }
  void loadPins() {
    pinnedPaths.clear();
    std::ifstream f(getPinFile());
    std::string line;
    while (std::getline(f, line)) {
      if (!line.empty() && fs::exists(line))
        pinnedPaths.push_back(line);
    }
  }
  void savePins() {
    std::ofstream f(getPinFile());
    for (const auto &p : pinnedPaths)
      f << p.string() << "\n";
  }
  void handlePin() {
    for (const auto &p : pinnedPaths) {
      if (p == currentPath) {
        setStatus("Already pinned");
        return;
      }
    }
    pinnedPaths.push_back(currentPath);
    savePins();
    setStatus("Pinned");
  }
  void handleUnpin() {
    if (pinnedPaths.empty())
      return;
    if (pinnedIndex < pinnedPaths.size()) {
      pinnedPaths.erase(pinnedPaths.begin() + pinnedIndex);
      if (pinnedIndex >= pinnedPaths.size() && pinnedIndex > 0)
        pinnedIndex--;
      savePins();
      setStatus("Unpinned");
    }
  }
  void jumpToPin() {
    if (pinnedPaths.empty())
      return;
    if (pinnedIndex < pinnedPaths.size()) {
      currentPath = pinnedPaths[pinnedIndex];
      reloadAll();
      focusPinned = false;
      setStatus("Jumped to pin");
    }
  }

  const char *getIcon(const FileEntry &f) {
    if (f.is_directory)
      return ICON_DIR;
    if (VIDEO_EXTS.count(f.extension))
      return ICON_VIDEO;
    if (IMAGE_EXTS.count(f.extension))
      return ICON_IMAGE;
    if (AUDIO_EXTS.count(f.extension))
      return ICON_MUSIC;
    if (CODE_EXTS.count(f.extension))
      return ICON_CODE;
    return ICON_FILE;
  }

  void loadDirectory(const fs::path &path, std::vector<FileEntry> &target) {
    target.clear();
    multiSelection.clear();
    try {
      for (const auto &entry : fs::directory_iterator(path)) {
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
      for (const auto &entry : fs::directory_iterator(path)) {
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

    if (winPinned)
      delwin(winPinned);
    if (winParent)
      delwin(winParent);
    if (winCurrent)
      delwin(winCurrent);
    if (winPreview)
      delwin(winPreview);

    int hPinned = height / 3;
    int hParent = (height - 1) - hPinned;

    winPinned = newwin(hPinned, w1, 0, 0);
    winParent = newwin(hParent, w1, hPinned, 0);
    winCurrent = newwin(height - 1, w2, 0, w1);
    winPreview = newwin(height - 1, w3, 0, w1 + w2);

    refresh();
  }

  // --- Async Preview Logic (Image & Bat Text) ---
  void startAsyncPreview(const std::string &path, PreviewType type,
                         int previewHeight, int previewWidth) {
    requestID++;
    requestedPath = path;
    imageReady = false;

    std::thread([this, path, type, previewHeight, previewWidth,
                 reqId = requestID]() {
      std::string b64;
      std::vector<std::string> lines;

      if (type == PreviewType::IMAGE) {
        try {
          fs::remove(PREVIEW_TEMP);
        } catch (...) {
        }
        std::string cmd;
        std::string fileCmd = "\"" + path + "\"";
        if (path.find(".mp4") != std::string::npos ||
            path.find(".mkv") != std::string::npos ||
            path.find(".webm") != std::string::npos) {
          cmd = "ffmpeg -y -v error -i " + fileCmd +
                " -vf \"thumbnail,scale=400:-1\" -frames:v 1 -f image2 " +
                PREVIEW_TEMP + " > /dev/null 2>&1";
        } else {
          cmd = "ffmpeg -y -v error -i " + fileCmd +
                " -vf \"scale=400:-1\" -f image2 " + PREVIEW_TEMP +
                " > /dev/null 2>&1";
        }
        int res = system(cmd.c_str());
        (void)res;
        std::ifstream file(PREVIEW_TEMP, std::ios::binary);
        if (file) {
          std::vector<unsigned char> buffer(
              std::istreambuf_iterator<char>(file), {});
          b64 = base64_encode(buffer.data(), buffer.size());
        }
      } else if (type == PreviewType::TEXT) {
        // Check binary first to avoid catting binaries
        if (is_binary_file(path)) {
          lines.push_back("\033[1;31m[Binary File]\033[0m");
        } else {
          // 1. Try 'bat'
          std::string cmd = "bat --color=always --style=plain --paging=never "
                            "--wrap=never --line-range=:" +
                            std::to_string(previewHeight) + " \"" + path +
                            "\" 2>/dev/null";
          FILE *pipe = popen(cmd.c_str(), "r");
          bool gotOutput = false;

          if (pipe) {
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
              std::string line(buffer);
              if (!line.empty() && line.back() == '\n')
                line.pop_back();
              lines.push_back(line);
              gotOutput = true;
            }
            pclose(pipe);
          }

          // 2. Fallback to 'batcat'
          if (!gotOutput) {
            lines.clear();
            cmd = "batcat --color=always --style=plain --paging=never "
                  "--wrap=never --line-range=:" +
                  std::to_string(previewHeight) + " \"" + path +
                  "\" 2>/dev/null";
            pipe = popen(cmd.c_str(), "r");
            if (pipe) {
              char buffer[1024];
              while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line(buffer);
                if (!line.empty() && line.back() == '\n')
                  line.pop_back();
                lines.push_back(line);
                gotOutput = true;
              }
              pclose(pipe);
            }
          }

          // 3. Final Fallback: Plain Text
          if (!gotOutput) {
            lines.clear();
            std::ifstream f(path);
            if (f.is_open()) {
              std::string lineStr;
              int count = 0;
              while (std::getline(f, lineStr) && count < previewHeight) {
                std::string clean;
                for (char c : lineStr) {
                  if (c == '\t')
                    clean += "    ";
                  else
                    clean += c;
                }
                if (clean.length() > (size_t)previewWidth)
                  clean = clean.substr(0, previewWidth);
                lines.push_back(clean);
                count++;
              }
            }
          }
        }
      }

      {
        std::lock_guard<std::mutex> lock(previewMutex);
        if (reqId == requestID) {
          if (type == PreviewType::IMAGE)
            cachedBase64 = b64;
          else
            cachedTextLines = lines;
          cachedPath = path;
        }
      }
      if (reqId == requestID)
        imageReady = true;
    }).detach();
  }

  void sendKittyGraphics(const std::string &b64Data, int pY, int pX) {
    std::cout << "\033[" << (pY + 7) << ";" << (pX + 2) << "H";
    const size_t chunk_size = 4096;
    size_t total = b64Data.length();
    size_t offset = 0;
    while (offset < total) {
      size_t chunkLen = std::min(chunk_size, total - offset);
      bool isLast = (offset + chunkLen >= total);
      std::cout << "\033_G";
      if (offset == 0)
        std::cout << "a=T,f=100,t=d,q=2,";
      std::cout << "m=" << (isLast ? "0" : "1") << ";";
      std::cout << b64Data.substr(offset, chunkLen);
      std::cout << "\033\\";
      offset += chunkLen;
    }
    std::cout << std::flush;
  }

  void drawFromCache(PreviewType type) {
    std::lock_guard<std::mutex> lock(previewMutex);
    int pW, pH, pX, pY;
    getmaxyx(winPreview, pH, pW);
    getbegyx(winPreview, pY, pX);

    if (type == PreviewType::IMAGE && !cachedBase64.empty()) {
      sendKittyGraphics(cachedBase64, pY, pX);
      lastWasDirectRender = true;
    } else if (type == PreviewType::TEXT && !cachedTextLines.empty()) {
      std::cout << "\033[?7l";

      int lineLimit = std::min((int)cachedTextLines.size(), pH - 8);
      for (int i = 0; i < lineLimit; ++i) {
        std::cout << "\033[" << (pY + 7 + i) << ";" << (pX + 2) << "H";
        std::cout << cachedTextLines[i];
      }

      std::cout << "\033[?7h";
      std::cout << std::flush;
      lastWasDirectRender = true;
    }
  }
  // ----------------------------

  void setStatus(const std::string &msg) { statusMessage = msg; }

  std::string promptInput(const std::string &prompt) {
    move(height - 1, 0);
    clrtoeol();
    attron(COLOR_PAIR(7) | A_BOLD);
    mvprintw(height - 1, 0, "%s: ", prompt.c_str());
    attroff(COLOR_PAIR(7) | A_BOLD);
    refresh();
    timeout(-1);
    echo();
    curs_set(1);
    char buf[256];
    getnstr(buf, 255);
    noecho();
    curs_set(0);
    timeout(50);
    return std::string(buf);
  }

  void toggleSelection() {
    if (currentFiles.empty())
      return;
    fs::path p = currentFiles[selectedIndex].path;
    if (multiSelection.count(p))
      multiSelection.erase(p);
    else
      multiSelection.insert(p);
    if (selectedIndex < currentFiles.size() - 1)
      selectedIndex++;
  }
  void selectAll() {
    for (const auto &f : currentFiles)
      multiSelection.insert(f.path);
    setStatus("Selected all");
  }
  void clearSelection() {
    multiSelection.clear();
    setStatus("Cleared selection");
  }

  void handleCopy() {
    if (currentFiles.empty())
      return;
    clipboard.paths.clear();
    if (multiSelection.empty())
      clipboard.paths.push_back(currentFiles[selectedIndex].path);
    else
      for (const auto &p : multiSelection)
        clipboard.paths.push_back(p);
    clipboard.isCut = false;
    setStatus("Yanked items");
    multiSelection.clear();
  }
  void handleCut() {
    if (currentFiles.empty())
      return;
    clipboard.paths.clear();
    if (multiSelection.empty())
      clipboard.paths.push_back(currentFiles[selectedIndex].path);
    else
      for (const auto &p : multiSelection)
        clipboard.paths.push_back(p);
    clipboard.isCut = true;
    setStatus("Cut items");
    multiSelection.clear();
  }

  void handlePaste() {
    if (clipboard.paths.empty()) {
      setStatus("Clipboard empty");
      return;
    }
    int successCount = 0;
    for (const auto &src : clipboard.paths) {
      fs::path dest = currentPath / src.filename();

      // Prevent overwrite on copy unless it is a move (rename usually handles
      // overwrite)
      if (fs::exists(dest) && !clipboard.isCut && src != dest)
        continue;

      try {
        if (clipboard.isCut) {
          // FIX: Try efficient rename (move) first
          try {
            fs::rename(src, dest);
          } catch (const fs::filesystem_error &e) {
            // Fallback for cross-device moves: Copy then Delete
            if (fs::is_directory(src))
              fs::copy(src, dest, fs::copy_options::recursive);
            else
              fs::copy(src, dest);
            fs::remove_all(src);
          }
        } else {
          // Copy Operation
          if (fs::is_directory(src))
            fs::copy(src, dest, fs::copy_options::recursive);
          else
            fs::copy(src, dest);
        }
        successCount++;
      } catch (...) {
      }
    }
    if (clipboard.isCut && successCount > 0)
      clipboard.paths.clear();
    setStatus(clipboard.isCut ? "Moved items" : "Pasted items");
    reloadAll();
  }

  void handleRename() {
    if (currentFiles.empty())
      return;
    const auto &file = currentFiles[selectedIndex];
    std::string newName = promptInput("Rename " + file.name + " to");
    if (newName.empty())
      return;
    try {
      fs::rename(file.path, currentPath / newName);
      setStatus("Renamed");
      reloadAll();
    } catch (...) {
      setStatus("Rename Failed");
    }
  }
  void handleNewFile() {
    std::string name = promptInput("New File Name");
    if (name.empty())
      return;
    std::ofstream(currentPath / name).close();
    setStatus("Created file");
    reloadAll();
  }
  void handleNewFolder() {
    std::string name = promptInput("New Folder Name");
    if (name.empty())
      return;
    try {
      fs::create_directory(currentPath / name);
      setStatus("Created folder");
      reloadAll();
    } catch (...) {
    }
  }
  void handleZip() {
    if (currentFiles.empty())
      return;
    std::vector<fs::path> targets;
    if (multiSelection.empty())
      targets.push_back(currentFiles[selectedIndex].path);
    else
      for (const auto &p : multiSelection)
        targets.push_back(p);
    std::string name = promptInput("Zip Name");
    if (name.empty())
      return;
    std::string cmd = "zip -r -q \"" + name + ".zip\"";
    for (const auto &p : targets)
      cmd += " \"" + p.filename().string() + "\"";
    cmd += " > /dev/null 2>&1";
    fs::path old = fs::current_path();
    fs::current_path(currentPath);
    int res = system(cmd.c_str());
    (void)res;
    fs::current_path(old);
    setStatus("Zipped");
    reloadAll();
  }
  void handleDelete() {
    if (currentFiles.empty())
      return;
    std::vector<fs::path> targets;
    if (multiSelection.empty())
      targets.push_back(currentFiles[selectedIndex].path);
    else
      for (const auto &p : multiSelection)
        targets.push_back(p);
    std::string countStr = (targets.size() > 1)
                               ? std::to_string(targets.size()) + " items"
                               : targets[0].filename().string();
    std::string confirm = promptInput("Delete " + countStr + "? (y/n)");
    if (confirm != "y" && confirm != "Y")
      return;
    for (const auto &p : targets) {
      try {
        fs::remove_all(p);
      } catch (...) {
      }
    }
    multiSelection.clear();
    setStatus("Deleted items");
    reloadAll();
  }

  void reloadAll() {
    loadDirectory(currentPath, currentFiles);
    loadParent();
  }
  void toggleHidden() {
    showHidden = !showHidden;
    reloadAll();
    setStatus(showHidden ? "Showing hidden" : "Hidden masked");
  }

  // --- Drawing ---
  void drawPinned() {
    werase(winPinned);
    if (focusPinned)
      wattron(winPinned, COLOR_PAIR(10));
    box(winPinned, 0, 0);
    if (focusPinned)
      wattroff(winPinned, COLOR_PAIR(10));
    mvwprintw(winPinned, 0, 2, " Pinned ");
    for (size_t i = 0; i < pinnedPaths.size() && i < getmaxy(winPinned) - 2;
         ++i) {
      wmove(winPinned, i + 1, 1);
      if (focusPinned && i == pinnedIndex)
        wattron(winPinned, COLOR_PAIR(3));
      std::string name = pinnedPaths[i].filename().string();
      if (name.empty())
        name = pinnedPaths[i].string();
      if (name.length() > getmaxx(winPinned) - 4)
        name = name.substr(0, getmaxx(winPinned) - 4);
      wprintw(winPinned, " %s %s", ICON_PIN, name.c_str());
      if (focusPinned && i == pinnedIndex)
        wattroff(winPinned, COLOR_PAIR(3));
    }
    wrefresh(winPinned);
  }

  void drawParent() {
    werase(winParent);
    box(winParent, 0, 0);
    int maxLines = getmaxy(winParent) - 2;
    int highlightIdx = -1;
    for (size_t i = 0; i < parentFiles.size(); ++i)
      if (parentFiles[i].path == currentPath) {
        highlightIdx = i;
        break;
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
    if (!focusPinned)
      wattron(winCurrent, COLOR_PAIR(6));
    box(winCurrent, 0, 0);
    if (!focusPinned)
      wattroff(winCurrent, COLOR_PAIR(6));
    wattron(winCurrent, A_BOLD);
    mvwprintw(winCurrent, 0, 2, " %s ", currentPath.filename().c_str());
    wattroff(winCurrent, A_BOLD);
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
      if (!focusPinned && idx == selectedIndex)
        wattron(winCurrent, COLOR_PAIR(3));
      else if (isMultiSelected)
        wattron(winCurrent, COLOR_PAIR(9) | A_BOLD);
      else {
        if (file.is_directory)
          colorPair = 1;
        else if (VIDEO_EXTS.count(file.extension))
          colorPair = 4;
        else if (IMAGE_EXTS.count(file.extension))
          colorPair = 5;
        else if (AUDIO_EXTS.count(file.extension))
          colorPair = 4;
        wattron(winCurrent, COLOR_PAIR(colorPair));
      }
      std::string display = file.name;
      int availWidth = getmaxx(winCurrent) - 16;
      if (display.length() > availWidth)
        display = display.substr(0, availWidth - 3) + "...";
      char marker = isMultiSelected ? '*' : ' ';
      wprintw(winCurrent, " %c %s %-s", marker, getIcon(file), display.c_str());
      std::string sz = formatSize(file.size);
      mvwprintw(winCurrent, i + 1, getmaxx(winCurrent) - sz.length() - 2, "%s",
                sz.c_str());
      if (!focusPinned && idx == selectedIndex)
        wattroff(winCurrent, COLOR_PAIR(3));
      else if (isMultiSelected)
        wattroff(winCurrent, COLOR_PAIR(9) | A_BOLD);
      else
        wattroff(winCurrent, COLOR_PAIR(colorPair));
    }
    wrefresh(winCurrent);
  }

  void drawPreview() {
    if (lastWasDirectRender)
      clearDirectRender();
    // wclear Forces a hard redraw/scrub of the window, preventing overlap
    // artifacts from direct rendering
    wclear(winPreview);
    box(winPreview, 0, 0);
    mvwprintw(winPreview, 0, 2, " Preview ");

    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) {
      wrefresh(winPreview);
      return;
    }
    const auto &file = currentFiles[selectedIndex];
    int maxW = getmaxx(winPreview) - 4;
    int maxH = getmaxy(winPreview) - 2;

    wmove(winPreview, 2, 2);
    wattron(winPreview, A_BOLD | COLOR_PAIR(7));
    wprintw(winPreview, "Details:");
    wattroff(winPreview, A_BOLD | COLOR_PAIR(7));
    mvwprintw(winPreview, 3, 2, "Name: %s", file.name.c_str());
    mvwprintw(winPreview, 4, 2, "Size: %s", formatSize(file.size).c_str());

    bool isVid = VIDEO_EXTS.count(file.extension);
    bool isImg = IMAGE_EXTS.count(file.extension);
    bool isCode = CODE_EXTS.count(file.extension);

    if (file.is_directory) {
      wattron(winPreview, A_DIM);
      mvwprintw(winPreview, 7, 2, "--- Content ---");
      try {
        int line = 8;
        for (const auto &entry : fs::directory_iterator(file.path)) {
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
    } else if (isVid || isImg || isCode) {
      wrefresh(winPreview);

      bool match = false;
      {
        std::lock_guard<std::mutex> lock(previewMutex);
        if (cachedPath == file.path.string())
          match = true;
      }

      if (match) {
        if (isCode)
          drawFromCache(PreviewType::TEXT);
        else
          drawFromCache(PreviewType::IMAGE);
      } else if (requestedPath != file.path.string()) {
        mvwprintw(winPreview, 10, 4, "Loading...");
        wrefresh(winPreview);
        PreviewType type = isCode ? PreviewType::TEXT : PreviewType::IMAGE;
        startAsyncPreview(file.path.string(), type, maxH - 8,
                          maxW); // Reserve space for header
      }
    } else {
      // Default text preview with binary check
      if (is_binary_file(file.path.string())) {
        mvwprintw(winPreview, 7, 2, "[Binary File]");
      } else {
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
      }
      wrefresh(winPreview);
    }
  }

  void openFile() {
    if (currentFiles.empty())
      return;
    const auto &file = currentFiles[selectedIndex];
    if (file.is_directory) {
      clearDirectRender();
      currentPath = file.path;
      selectedIndex = 0;
      scrollOffset = 0;
      reloadAll();
    } else {
      clearDirectRender();
      def_prog_mode();
      endwin();
      std::string cmd;
      if (VIDEO_EXTS.count(file.extension) ||
          AUDIO_EXTS.count(file.extension)) {
        cmd = "mpv \"" + file.path.string() + "\" 2> /dev/null";
      } else {
#ifdef __APPLE__
        cmd = "open \"" + file.path.string() + "\"";
#else
        cmd = "xdg-open \"" + file.path.string() + "\"";
#endif
        cmd += " > /dev/null 2>&1";
      }
      int res = system(cmd.c_str());
      (void)res;
      reset_prog_mode();
      refresh();
      timeout(50);
    }
  }

  void goUp() {
    if (currentPath.has_parent_path() &&
        currentPath != currentPath.parent_path()) {
      clearDirectRender();
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
    bool needsRedraw = true;

    while (true) {
      if (needsRedraw) {
        if (!currentFiles.empty()) {
          if (selectedIndex >= currentFiles.size())
            selectedIndex = currentFiles.size() - 1;
        } else
          selectedIndex = 0;

        drawPinned();
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
          if (focusPinned)
            printw("[PINNED] j/k:Nav Enter:Jump d:Unpin Tab:Files");
          else
            printw(
                "Tab:Pins P:Pin Space:Sel y:Cp x:Cut p:Pst d:Del z:Zip .:Hide");
          attroff(A_DIM);
        }
        refresh();
        needsRedraw = false;
      }

      int ch = getch();
      if (ch == ERR) {
        if (imageReady) {
          needsRedraw = true;
          imageReady = false;
        }
        continue;
      }
      needsRedraw = true;
      if (ch != ERR)
        statusMessage = "";

      if (ch == 'q')
        return;
      if (ch == KEY_RESIZE) {
        clearDirectRender();
        updateLayout();
        continue;
      }
      if (ch == '\t') {
        focusPinned = !focusPinned;
        continue;
      }

      if (focusPinned) {
        switch (ch) {
        case 'j':
        case KEY_DOWN:
          if (!pinnedPaths.empty() && pinnedIndex < pinnedPaths.size() - 1)
            pinnedIndex++;
          break;
        case 'k':
        case KEY_UP:
          if (pinnedIndex > 0)
            pinnedIndex--;
          break;
        case 10:
          jumpToPin();
          break;
        case 'd':
          handleUnpin();
          break;
        }
      } else {
        switch (ch) {
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
        case 'P':
          handlePin();
          break;
        case ' ':
        case 'v':
          toggleSelection();
          break;
        case 'a':
          selectAll();
          break;
        case 27:
          clearSelection();
          break;
        case 'y':
          handleCopy();
          break;
        case 'x':
          handleCut();
          break;
        case 'p':
          handlePaste();
          break;
        case 'd':
        case KEY_DC:
          handleDelete();
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
        }
      }
    }
  }
};

int main() {
  FileManager fm;
  fm.run();
  return 0;
}
