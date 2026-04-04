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
 * - Asynchronous Folder Size Calculation with Live Sorting
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
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
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
#include <unordered_map>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

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
const std::set<std::string> ARCHIVE_EXTS = {".zip", ".tar", ".gz", ".7z",
                                            ".rar", ".xz",  ".bz2"};

const char *ICON_DIR = " ";
const char *ICON_VIDEO = " ";
const char *ICON_IMAGE = " ";
const char *ICON_CODE = " ";
const char *ICON_FILE = " ";
const char *ICON_MUSIC = " ";
const char *ICON_PIN = " ";
const char *ICON_ZIP = "󰿺 ";

const std::string PREVIEW_TEMP = "/tmp/fm_preview_thumb.png";
const uintmax_t SIZE_CALCULATING = UINTMAX_MAX; // Sentinel value for "..."

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
      if (is_directory) {
        size = SIZE_CALCULATING; // Mark as pending calculation
      } else {
        size = fs::file_size(p);
      }
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
  if (size == SIZE_CALCULATING)
    return "...";
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

// --- Async Size Calculator ---
struct SizeJob {
  fs::path path;
  int viewId;
};

struct SizeResult {
  fs::path path;
  uintmax_t size;
  int viewId;
};

class FileManager {
private:
  fs::path currentPath;
  std::string cwdFile; // Path to write final CWD
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
  bool sortBySize = false;

  // Async Preview State
  std::mutex previewMutex;
  std::atomic<bool> imageReady{false};
  std::string cachedBase64;
  int cachedImgW = 0, cachedImgH = 0;
  std::vector<std::string> cachedTextLines;
  std::string cachedPath;
  std::string requestedPath;
  long long requestID = 0;
  bool lastWasDirectRender = false;

  // Async Size Calculation State
  std::unordered_map<std::string, uintmax_t> dirSizeCache;
  std::mutex cacheMutex;
  std::thread sizeWorker;
  std::atomic<bool> stopWorker{false};
  std::atomic<int> currentViewId{0};
  std::deque<SizeJob> sizeQueue;
  std::deque<SizeResult> resultQueue;
  std::mutex queueMutex;
  std::condition_variable queueCv;
  std::mutex resultMutex;

  void initColors() {
    start_color();
    use_default_colors();

    std::unordered_map<std::string, std::string> colors = {
        {"DIR", "#89b4fa"},        {"FILE", "#cdd6f4"},
        {"SEL_BG", "#585b70"},     {"MEDIA", "#f9e2af"},
        {"IMAGE", "#f5c2e7"},      {"BORDER", "#b4befe"},
        {"SUCCESS", "#a6e3a1"},    {"ERROR", "#f38ba8"},
        {"MULTI", "#fab387"},      {"PIN_BG", "#cba6f7"},
        {"PIN_BORDER", "#89b4fa"}, {"SEC_SEL_BG", "#313244"},
        {"CODE", "#a6e3a1"},       {"ARCHIVE", "#eba0ac"}};

    const char *home = getenv("HOME");
    if (home) {
      fs::path configDir = fs::path(home) / ".config/fyzenor";
      if (!fs::exists(configDir))
        fs::create_directories(configDir);
      fs::path colorFile = configDir / "colors.fz";
      if (fs::exists(colorFile)) {
        std::ifstream f(colorFile);
        std::string line;
        while (std::getline(f, line)) {
          if (line.empty() || line[0] == '#')
            continue;
          size_t pos = line.find(':');
          if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            if (!val.empty() && val[0] == '#')
              colors[key] = val;
          }
        }
      } else {
        std::ofstream f(colorFile);
        f << "# Fyzenor Theme: Catppuccin Mocha (Matugen ready)\n";
        f << "DIR: #89b4fa\n";
        f << "FILE: #cdd6f4\n";
        f << "SEL_BG: #585b70\n";
        f << "MEDIA: #f9e2af\n";
        f << "IMAGE: #f5c2e7\n";
        f << "BORDER: #b4befe\n";
        f << "SUCCESS: #a6e3a1\n";
        f << "ERROR: #f38ba8\n";
        f << "MULTI: #fab387\n";
        f << "PIN_BG: #cba6f7\n";
        f << "PIN_BORDER: #89b4fa\n";
        f << "SEC_SEL_BG: #313244\n";
        f << "CODE: #a6e3a1\n";
        f << "ARCHIVE: #eba0ac\n";
      }
    }

    auto setHex = [](short id, const std::string &hex) {
      if (hex.length() < 7 || hex[0] != '#')
        return;
      int r, g, b;
      if (sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        init_color(id, (short)(r * 1000 / 255), (short)(g * 1000 / 255),
                   (short)(b * 1000 / 255));
      }
    };

    if (can_change_color()) {
      setHex(20, colors["DIR"]);
      setHex(21, colors["FILE"]);
      setHex(22, colors["SEL_BG"]);
      setHex(23, colors["MEDIA"]);
      setHex(24, colors["IMAGE"]);
      setHex(25, colors["BORDER"]);
      setHex(26, colors["SUCCESS"]);
      setHex(27, colors["ERROR"]);
      setHex(28, colors["MULTI"]);
      setHex(29, colors["PIN_BG"]);
      setHex(30, colors["PIN_BORDER"]);
      setHex(31, colors["SEC_SEL_BG"]);
      setHex(32, colors["CODE"]);
      setHex(33, colors["ARCHIVE"]);

      init_pair(1, 20, -1);  // DIR
      init_pair(2, 21, -1);  // FILE
      init_pair(3, 21, 22);  // SEL_FILE
      init_pair(4, 23, -1);  // MEDIA
      init_pair(5, 24, -1);  // IMAGE
      init_pair(6, 25, -1);  // BORDER
      init_pair(7, 26, -1);  // SUCCESS
      init_pair(8, 27, -1);  // ERROR
      init_pair(9, 28, -1);  // MULTI
      init_pair(10, 20, 22); // SEL_DIR
      init_pair(11, 23, 22); // SEL_MEDIA
      init_pair(12, 24, 22); // SEL_IMAGE
      init_pair(13, 21, 31); // SEC_SEL_FILE
      init_pair(14, 20, 31); // SEC_SEL_DIR
      init_pair(15, 30, -1); // PIN_BORDER
      init_pair(16, 32, -1); // CODE
      init_pair(17, 33, -1); // ARCHIVE
      init_pair(18, 32, 22); // SEL_CODE
      init_pair(19, 33, 22); // SEL_ARCHIVE
      init_pair(20, 23, 31); // SEC_SEL_MEDIA
      init_pair(21, 24, 31); // SEC_SEL_IMAGE
      init_pair(22, 32, 31); // SEC_SEL_CODE
      init_pair(23, 33, 31); // SEC_SEL_ARCHIVE
    } else {
      init_pair(1, COLOR_CYAN, -1);
      init_pair(2, COLOR_WHITE, -1);
      init_pair(3, COLOR_BLACK, COLOR_CYAN);
      init_pair(4, COLOR_YELLOW, -1);
      init_pair(5, COLOR_MAGENTA, -1);
      init_pair(6, COLOR_BLUE, -1);
      init_pair(7, COLOR_GREEN, -1);
      init_pair(8, COLOR_RED, -1);
      init_pair(9, COLOR_YELLOW, -1);
      init_pair(10, COLOR_WHITE, COLOR_BLUE);
      init_pair(11, COLOR_BLUE, -1);
      init_pair(12, COLOR_BLACK, COLOR_WHITE);
    }
  }

public:
  FileManager()
      : selectedIndex(0), scrollOffset(0), winPinned(nullptr),
        winParent(nullptr), winCurrent(nullptr), winPreview(nullptr) {
    setlocale(LC_ALL, "");
    loadPins();

    sizeWorker = std::thread(&FileManager::processSizeQueue, this);

    currentPath = fs::current_path();
    loadDirectory(currentPath, currentFiles);
    loadParent();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(50);

    initColors();

    refresh();
  }

  void clearDirectRender() {
    std::cout << "\033_Ga=d,q=2\033\\" << std::flush;
    lastWasDirectRender = false;
  }

  ~FileManager() {
    stopWorker = true;
    queueCv.notify_all();
    if (sizeWorker.joinable())
      sizeWorker.join();
    clearDirectRender();
    endwin();

    if (!cwdFile.empty()) {
      std::ofstream f(cwdFile);
      if (f.is_open()) {
        f << currentPath.string();
        f.close();
      }
    }
  }

  void setCwdFile(const std::string &path) { cwdFile = path; }

  // --- Async Size Worker Function ---
  void processSizeQueue() {
    while (!stopWorker) {
      SizeJob job;
      {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCv.wait(lock, [this] { return !sizeQueue.empty() || stopWorker; });
        if (stopWorker)
          break;
        job = sizeQueue.front();
        sizeQueue.pop_front();
      }

      if (job.viewId != currentViewId)
        continue;

      uintmax_t size = 0;
      try {
        if (fs::exists(job.path) && fs::is_directory(job.path)) {
          for (const auto &entry : fs::recursive_directory_iterator(
                   job.path, fs::directory_options::skip_permission_denied)) {
            if (job.viewId != currentViewId)
              break;
            try {
              if (!fs::is_directory(entry.status())) {
                size += fs::file_size(entry);
              }
            } catch (...) {
            }
          }
        }
      } catch (...) {
      }

      if (job.viewId == currentViewId) {
        std::lock_guard<std::mutex> lock(resultMutex);
        resultQueue.push_back({job.path, size, job.viewId});
      }

      // Always update the cache
      {
        std::lock_guard<std::mutex> lock(cacheMutex);
        dirSizeCache[job.path.string()] = size;
      }
    }
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
    if (ARCHIVE_EXTS.count(f.extension))
      return ICON_ZIP;
    return ICON_FILE;
  }

  // Unified Sorting Logic: Folders Top -> Size/Name
  void sortList(std::vector<FileEntry> &list) {
    std::sort(list.begin(), list.end(),
              [this](const FileEntry &a, const FileEntry &b) {
                // 1. Always keep directories on top
                if (a.is_directory != b.is_directory) {
                  return a.is_directory > b.is_directory;
                }

                // 2. Sort by Size (if enabled)
                if (sortBySize) {
                  if (a.size != b.size)
                    return a.size > b.size; // Descending
                  return a.name < b.name;
                }

                // 3. Default: Sort by Name (Ascending)
                return a.name < b.name;
              });
  }

  void loadDirectory(const fs::path &path, std::vector<FileEntry> &target) {
    target.clear();
    multiSelection.clear();
    currentViewId++;
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      sizeQueue.clear();
    }

    try {
      for (const auto &entry : fs::directory_iterator(path)) {
        if (!showHidden && entry.path().filename().string().front() == '.')
          continue;
        target.emplace_back(entry);
      }
    } catch (...) {
    }

    // Check cache and only queue what's missing
    {
      std::lock_guard<std::mutex> qLock(queueMutex);
      std::lock_guard<std::mutex> cLock(cacheMutex);
      for (auto &entry : target) {
        if (entry.is_directory) {
          auto it = dirSizeCache.find(entry.path.string());
          if (it != dirSizeCache.end()) {
            entry.size = it->second;
          } else {
            sizeQueue.push_back({entry.path, currentViewId.load()});
          }
        }
      }
    }

    // Initial Sort
    sortList(target);

    queueCv.notify_one();
  }

  void loadParent() {
    if (currentPath.has_parent_path() &&
        currentPath != currentPath.parent_path()) {
      parentFiles.clear();
      try {
        for (const auto &entry :
             fs::directory_iterator(currentPath.parent_path())) {
          parentFiles.emplace_back(entry);
        }
      } catch (...) {
      }
      // Standard sort for parent to keep it stable
      std::sort(parentFiles.begin(), parentFiles.end(),
                [](const FileEntry &a, const FileEntry &b) {
                  if (a.is_directory != b.is_directory)
                    return a.is_directory > b.is_directory;
                  return a.name < b.name;
                });
    } else {
      parentFiles.clear();
    }
  }

  void updateLayout() {
    getmaxyx(stdscr, height, width);
    // Adjusted widths for a more modern Miller Column feel (2:3:5 ratio
    // roughly)
    int w1 = static_cast<int>(width * 0.18); // Pins/Parent
    int w2 = static_cast<int>(width * 0.32); // Current Files
    int w3 = width - w1 - w2;                // Large Preview

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

        // Use 90% of the box for the image to satisfy "smaller a bit"
        int targetW = (int)((previewWidth - 4) * 10);
        int targetH = (int)((previewHeight - 4) * 20);
        if (targetW < 10)
          targetW = 10;
        if (targetH < 10)
          targetH = 10;

        std::string scaleFilter = "scale=" + std::to_string(targetW) + ":" +
                                  std::to_string(targetH) +
                                  ":force_original_aspect_ratio=decrease";

        if (path.find(".mp4") != std::string::npos ||
            path.find(".mkv") != std::string::npos ||
            path.find(".webm") != std::string::npos) {
          cmd = "ffmpeg -y -v error -i " + fileCmd + " -vf \"" + scaleFilter +
                "\" -frames:v 1 -f image2 " + PREVIEW_TEMP +
                " > /dev/null 2>&1";
        } else {
          cmd = "ffmpeg -y -v error -i " + fileCmd + " -vf \"" + scaleFilter +
                "\" -f image2 " + PREVIEW_TEMP + " > /dev/null 2>&1";
        }
        int res = system(cmd.c_str());
        (void)res;

        // Get actual scaled dimensions
        int finalW = 0, finalH = 0;
        std::string probeCmd =
            "ffprobe -v error -select_streams v:0 -show_entries "
            "stream=width,height -of csv=s=x:p=0 \"" +
            PREVIEW_TEMP + "\"";
        FILE *p = popen(probeCmd.c_str(), "r");
        if (p) {
          char buf[64];
          if (fgets(buf, sizeof(buf), p)) {
            sscanf(buf, "%dx%d", &finalW, &finalH);
          }
          pclose(p);
        }

        std::ifstream file(PREVIEW_TEMP, std::ios::binary);
        if (file) {
          std::vector<unsigned char> buffer(
              std::istreambuf_iterator<char>(file), {});
          b64 = base64_encode(buffer.data(), buffer.size());
        }

        {
          std::lock_guard<std::mutex> lock(previewMutex);
          if (reqId == requestID) {
            cachedImgW = finalW;
            cachedImgH = finalH;
          }
        }
      } else if (type == PreviewType::TEXT) {
        if (is_binary_file(path)) {
          lines.push_back("\033[1;31m[Binary File]\033[0m");
        } else {
          std::string cmd = "bat --color=always --style=plain --paging=never "
                            "--wrap=character --line-range=:" +
                            std::to_string(previewHeight * 2) + " \"" + path +
                            "\" 2>/dev/null";
          FILE *pipe = popen(cmd.c_str(), "r");
          bool gotOutput = false;
          if (pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
              std::string line(buffer);
              if (!line.empty() && line.back() == '\n')
                line.pop_back();
              lines.push_back(line);
              gotOutput = true;
            }
            pclose(pipe);
          }
          if (!gotOutput) {
            lines.clear();
            cmd = "batcat --color=always --style=plain --paging=never "
                  "--wrap=character --line-range=:" +
                  std::to_string(previewHeight * 2) + " \"" + path +
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

  void sendKittyGraphics(const std::string &b64Data, int pY, int pX, int cols,
                         int rows, int offX = 0, int offY = 0) {
    // Move cursor to start of preview area (1-indexed for terminal)
    // pY+1 is the start of the window, we have 7 lines of header/padding +
    // offY.
    std::cout << "\033[" << (pY + 7 + offY) << ";" << (pX + 3 + offX) << "H";
    const size_t chunk_size = 4096;
    size_t total = b64Data.length();
    size_t offset = 0;
    while (offset < total) {
      size_t chunkLen = std::min(chunk_size, total - offset);
      bool isLast = (offset + chunkLen >= total);
      std::cout << "\033_G";
      if (offset == 0) {
        // a=T: transmit and display, f=100: PNG, t=d: direct
        // c, r: scale image to fit these columns and rows
        std::cout << "a=T,f=100,t=d,q=2,c=" << cols << ",r=" << rows << ",";
      }
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
      int cols = (cachedImgW + 9) / 10;
      int rows = (cachedImgH + 19) / 20;

      // Box starts at line 7, ends at pH-2. Total height = pH - 8.
      // Width starts at pX+3, ends at pX+pW-2. Total width = pW - 4.
      int boxW = pW - 4;
      int boxH = pH - 8;

      int offX = (boxW - cols) / 2;
      int offY = (boxH - rows) / 2;
      if (offX < 0)
        offX = 0;
      if (offY < 0)
        offY = 0;

      sendKittyGraphics(cachedBase64, pY, pX, cols, rows, offX, offY);
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

  void toggleSort() {
    sortBySize = !sortBySize;
    sortList(currentFiles); // Immediate sort
    setStatus(sortBySize ? "Sorted by Size (Desc)" : "Sorted by Name");
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
      if (fs::exists(dest) && !clipboard.isCut && src != dest)
        continue;
      try {
        if (clipboard.isCut) {
          try {
            fs::rename(src, dest);
          } catch (const fs::filesystem_error &e) {
            if (fs::is_directory(src))
              fs::copy(src, dest, fs::copy_options::recursive);
            else
              fs::copy(src, dest);
            fs::remove_all(src);
          }
        } else {
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
  void handleCopyPath() {
    if (currentFiles.empty())
      return;
    std::string path = fs::absolute(currentFiles[selectedIndex].path).string();
    std::string escaped;
    for (char c : path) {
      if (c == '"')
        escaped += "\\\"";
      else
        escaped += c;
    }
    std::string cmd = "echo -n \"" + escaped +
                      "\" | (wl-copy 2>/dev/null || xclip -selection clipboard "
                      "2>/dev/null || pbcopy 2>/dev/null)";
    int res = system(cmd.c_str());
    (void)res;
    setStatus("Copied path");
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
  void drawRoundedBox(WINDOW *win) {
    int my, mx;
    getmaxyx(win, my, mx);
    for (int i = 1; i < mx - 1; ++i) {
      mvwaddstr(win, 0, i, "─");
      mvwaddstr(win, my - 1, i, "─");
    }
    for (int i = 1; i < my - 1; ++i) {
      mvwaddstr(win, i, 0, "│");
      mvwaddstr(win, i, mx - 1, "│");
    }
    mvwaddstr(win, 0, 0, "╭");
    mvwaddstr(win, 0, mx - 1, "╮");
    mvwaddstr(win, my - 1, 0, "╰");
    mvwaddstr(win, my - 1, mx - 1, "╯");
  }

  void drawPinned() {
    werase(winPinned);
    if (focusPinned)
      wattron(winPinned, COLOR_PAIR(10));
    else
      wattron(winPinned, COLOR_PAIR(15));
    drawRoundedBox(winPinned);
    if (focusPinned)
      wattroff(winPinned, COLOR_PAIR(10));
    else
      wattroff(winPinned, COLOR_PAIR(15));

    wattron(winPinned, A_BOLD | COLOR_PAIR(4));
    mvwprintw(winPinned, 0, 2, " 󰐃 Pinned ");
    wattroff(winPinned, A_BOLD | COLOR_PAIR(4));

    for (size_t i = 0;
         i < pinnedPaths.size() && i < (size_t)getmaxy(winPinned) - 2; ++i) {
      wmove(winPinned, i + 1, 1);
      if (focusPinned && i == pinnedIndex) {
        wattron(winPinned, COLOR_PAIR(10) | A_BOLD);
        for (int j = 0; j < getmaxx(winPinned) - 2; ++j)
          waddch(winPinned, ' ');
        wmove(winPinned, i + 1, 1);
      }

      std::string name = pinnedPaths[i].filename().string();
      if (name.empty())
        name = pinnedPaths[i].string();
      if (name.length() > (size_t)getmaxx(winPinned) - 6)
        name = name.substr(0, getmaxx(winPinned) - 6);

      wprintw(winPinned, " %s %s", ICON_PIN, name.c_str());

      if (focusPinned && i == pinnedIndex)
        wattroff(winPinned, COLOR_PAIR(10) | A_BOLD);
    }
    wrefresh(winPinned);
  }

  void drawParent() {
    werase(winParent);
    wattron(winParent, COLOR_PAIR(6));
    drawRoundedBox(winParent);
    wattroff(winParent, COLOR_PAIR(6));

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
    if (start + maxLines > (int)parentFiles.size() &&
        (int)parentFiles.size() > maxLines)
      start = parentFiles.size() - maxLines;

    for (int i = 0; i < maxLines && (start + i) < (int)parentFiles.size();
         ++i) {
      const auto &file = parentFiles[start + i];
      bool isCurrent = (static_cast<int>(start + i) == highlightIdx);
      wmove(winParent, i + 1, 1);

      int colorPair = file.is_directory ? 1 : 2;
      if (VIDEO_EXTS.count(file.extension) || AUDIO_EXTS.count(file.extension))
        colorPair = 4;
      else if (IMAGE_EXTS.count(file.extension))
        colorPair = 5;
      else if (CODE_EXTS.count(file.extension))
        colorPair = 16;
      else if (ARCHIVE_EXTS.count(file.extension))
        colorPair = 17;

      if (isCurrent) {
        int selPair = 13;
        if (file.is_directory)
          selPair = 14;
        else if (VIDEO_EXTS.count(file.extension) ||
                 AUDIO_EXTS.count(file.extension))
          selPair = 20;
        else if (IMAGE_EXTS.count(file.extension))
          selPair = 21;
        else if (CODE_EXTS.count(file.extension))
          selPair = 22;
        else if (ARCHIVE_EXTS.count(file.extension))
          selPair = 23;

        wattron(winParent, COLOR_PAIR(selPair) | A_BOLD);
        for (int j = 0; j < getmaxx(winParent) - 2; ++j)
          waddch(winParent, ' ');
        wmove(winParent, i + 1, 1);
      } else {
        wattron(winParent, COLOR_PAIR(colorPair) | A_DIM);
      }

      std::string display = file.name;
      if (display.length() > (size_t)getmaxx(winParent) - 8)
        display = display.substr(0, getmaxx(winParent) - 11) + "...";

      wprintw(winParent, " %s %s", getIcon(file), display.c_str());

      if (isCurrent) {
        int selPair = 13;
        if (file.is_directory)
          selPair = 14;
        else if (VIDEO_EXTS.count(file.extension) ||
                 AUDIO_EXTS.count(file.extension))
          selPair = 20;
        else if (IMAGE_EXTS.count(file.extension))
          selPair = 21;
        else if (CODE_EXTS.count(file.extension))
          selPair = 22;
        else if (ARCHIVE_EXTS.count(file.extension))
          selPair = 23;
        wattroff(winParent, COLOR_PAIR(selPair) | A_BOLD);
      } else {
        wattroff(winParent, COLOR_PAIR(colorPair) | A_DIM);
      }
    }
    wrefresh(winParent);
  }

  void drawCurrent() {
    werase(winCurrent);
    if (!focusPinned)
      wattron(winCurrent, COLOR_PAIR(6) | A_BOLD);
    else
      wattron(winCurrent, COLOR_PAIR(6));
    drawRoundedBox(winCurrent);
    wattroff(winCurrent, A_BOLD);
    wattroff(winCurrent, COLOR_PAIR(6));

    wattron(winCurrent, A_BOLD | COLOR_PAIR(1));
    mvwprintw(winCurrent, 0, 2, " 󰉖 %s ",
              currentPath.filename().string().c_str());
    wattroff(winCurrent, A_BOLD | COLOR_PAIR(1));

    if (!multiSelection.empty()) {
      std::string selStr =
          " [" + std::to_string(multiSelection.size()) + " selected] ";
      wattron(winCurrent, COLOR_PAIR(9) | A_BOLD);
      mvwprintw(winCurrent, 0, getmaxx(winCurrent) - selStr.length() - 2, "%s",
                selStr.c_str());
      wattroff(winCurrent, COLOR_PAIR(9) | A_BOLD);
    }

    int maxLines = height - 3;
    if (selectedIndex < scrollOffset)
      scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + (size_t)maxLines)
      scrollOffset = selectedIndex - maxLines + 1;

    for (int i = 0; i < maxLines && (scrollOffset + i) < currentFiles.size();
         ++i) {
      int idx = scrollOffset + i;
      const auto &file = currentFiles[idx];
      wmove(winCurrent, i + 1, 1);

      bool isSelected = (!focusPinned && idx == (int)selectedIndex);
      bool isMultiSelected = multiSelection.count(file.path);

      int baseColor = 2;
      if (file.is_directory)
        baseColor = 1;
      else if (VIDEO_EXTS.count(file.extension) ||
               AUDIO_EXTS.count(file.extension))
        baseColor = 4;
      else if (IMAGE_EXTS.count(file.extension))
        baseColor = 5;
      else if (CODE_EXTS.count(file.extension))
        baseColor = 16;
      else if (ARCHIVE_EXTS.count(file.extension))
        baseColor = 17;

      if (isSelected) {
        int selPair = 3;
        if (file.is_directory)
          selPair = 10;
        else if (VIDEO_EXTS.count(file.extension) ||
                 AUDIO_EXTS.count(file.extension))
          selPair = 11;
        else if (IMAGE_EXTS.count(file.extension))
          selPair = 12;
        else if (CODE_EXTS.count(file.extension))
          selPair = 18;
        else if (ARCHIVE_EXTS.count(file.extension))
          selPair = 19;

        wattron(winCurrent, COLOR_PAIR(selPair) | A_BOLD);
        for (int j = 0; j < getmaxx(winCurrent) - 2; ++j)
          waddch(winCurrent, ' ');
        wmove(winCurrent, i + 1, 1);
      } else if (isMultiSelected) {
        wattron(winCurrent, COLOR_PAIR(9) | A_BOLD);
      } else {
        wattron(winCurrent, COLOR_PAIR(baseColor));
      }

      std::string display = file.name;
      int availWidth = getmaxx(winCurrent) - 16;
      if (display.length() > (size_t)availWidth)
        display = display.substr(0, availWidth - 3) + "...";

      char marker = isMultiSelected ? '*' : ' ';
      wprintw(winCurrent, " %c %s %-s", marker, getIcon(file), display.c_str());

      std::string sz = formatSize(file.size);
      mvwprintw(winCurrent, i + 1, getmaxx(winCurrent) - sz.length() - 2, "%s",
                sz.c_str());

      if (isSelected) {
        int selPair = 3;
        if (file.is_directory)
          selPair = 10;
        else if (VIDEO_EXTS.count(file.extension) ||
                 AUDIO_EXTS.count(file.extension))
          selPair = 11;
        else if (IMAGE_EXTS.count(file.extension))
          selPair = 12;
        else if (CODE_EXTS.count(file.extension))
          selPair = 18;
        else if (ARCHIVE_EXTS.count(file.extension))
          selPair = 19;
        wattroff(winCurrent, COLOR_PAIR(selPair) | A_BOLD);
      } else if (isMultiSelected)
        wattroff(winCurrent, COLOR_PAIR(9) | A_BOLD);
      else
        wattroff(winCurrent, COLOR_PAIR(baseColor));
    }
    wrefresh(winCurrent);
  }

  void drawPreview() {
    if (lastWasDirectRender)
      clearDirectRender();
    wclear(winPreview);
    wattron(winPreview, COLOR_PAIR(6));
    drawRoundedBox(winPreview);
    wattroff(winPreview, COLOR_PAIR(6));

    wattron(winPreview, A_BOLD | COLOR_PAIR(5));
    mvwprintw(winPreview, 0, 2, " 󰮫 Preview ");
    wattroff(winPreview, A_BOLD | COLOR_PAIR(5));

    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) {
      wrefresh(winPreview);
      return;
    }
    const auto &file = currentFiles[selectedIndex];
    int maxW = getmaxx(winPreview) - 4;
    int maxH = getmaxy(winPreview) - 2;

    // Header info with better colors
    wattron(winPreview, A_BOLD | COLOR_PAIR(1));
    mvwprintw(winPreview, 1, 2, " %s ", file.name.c_str());
    wattroff(winPreview, A_BOLD | COLOR_PAIR(1));

    wattron(winPreview, A_DIM);
    mvwprintw(winPreview, 2, 2, " Size: %s", formatSize(file.size).c_str());
    mvwprintw(winPreview, 3, 2,
              " Type: %s",
              file.is_directory ? "Directory"
                                : (file.extension.empty() ? "File"
                                                          : file.extension.c_str()));
    wattroff(winPreview, A_DIM);

    wattron(winPreview, COLOR_PAIR(6));
    for (int i = 1; i < getmaxx(winPreview) - 1; ++i)
      mvwaddstr(winPreview, 4, i, "─");
    wattroff(winPreview, COLOR_PAIR(6));


    bool isVid = VIDEO_EXTS.count(file.extension);
    bool isImg = IMAGE_EXTS.count(file.extension);
    bool isCode = CODE_EXTS.count(file.extension);

    if (file.is_directory) {
      wattron(winPreview, COLOR_PAIR(1) | A_BOLD);
      mvwprintw(winPreview, 6, 2, "󰉖 Content:");
      wattroff(winPreview, COLOR_PAIR(1) | A_BOLD);
      try {
        int line = 7;
        for (const auto &entry : fs::directory_iterator(file.path)) {
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;
          if (line >= height - 3)
            break;
          std::string subName = entry.path().filename().string();
          if (subName.length() > (size_t)maxW)
            subName = subName.substr(0, maxW - 3) + "...";

          int cp = fs::is_directory(entry) ? 1 : 2;
          std::string ext = entry.path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          if (VIDEO_EXTS.count(ext) || AUDIO_EXTS.count(ext))
            cp = 4;
          else if (IMAGE_EXTS.count(ext))
            cp = 5;
          else if (CODE_EXTS.count(ext))
            cp = 16;
          else if (ARCHIVE_EXTS.count(ext))
            cp = 17;

          wattron(winPreview, COLOR_PAIR(cp));
          mvwprintw(winPreview, line++, 4, "%s %s",
                    fs::is_directory(entry)
                        ? ICON_DIR
                        : (VIDEO_EXTS.count(ext)
                               ? ICON_VIDEO
                               : (IMAGE_EXTS.count(ext)
                                      ? ICON_IMAGE
                                      : (CODE_EXTS.count(ext)
                                             ? ICON_CODE
                                             : (ARCHIVE_EXTS.count(ext)
                                                    ? ICON_ZIP
                                                    : ICON_FILE)))),
                    subName.c_str());
          wattroff(winPreview, COLOR_PAIR(cp));
        }
      } catch (...) {
      }
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
        wattron(winPreview, A_ITALIC | A_DIM);
        mvwprintw(winPreview, 6, 4, "Generating preview...");
        wattroff(winPreview, A_ITALIC | A_DIM);
        wrefresh(winPreview);
        PreviewType type = isCode ? PreviewType::TEXT : PreviewType::IMAGE;
        startAsyncPreview(file.path.string(), type, maxH - 8, maxW);
      }
    } else {
      if (is_binary_file(file.path.string())) {
        wattron(winPreview, COLOR_PAIR(8));
        mvwprintw(winPreview, 6, 2, " [Binary File - No Preview] ");
        wattroff(winPreview, COLOR_PAIR(8));
      } else {
        std::ifstream f(file.path);
        if (f.is_open()) {
          std::string lineStr;
          int line = 6;
          while (std::getline(f, lineStr) && line < height - 3) {
            std::replace(lineStr.begin(), lineStr.end(), '\t', ' ');
            for (size_t i = 0; i < lineStr.length(); i += maxW) {
              if (line >= height - 3)
                break;
              mvwprintw(winPreview, line++, 2, "%s",
                        lineStr.substr(i, maxW).c_str());
            }
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
      // Check for async size updates
      {
        std::lock_guard<std::mutex> lock(resultMutex);
        if (!resultQueue.empty()) {
          bool updated = false;
          while (!resultQueue.empty()) {
            SizeResult res = resultQueue.front();
            resultQueue.pop_front();
            if (res.viewId == currentViewId) {
              for (auto &f : currentFiles) {
                if (f.path == res.path) {
                  f.size = res.size;
                  updated = true;
                  break;
                }
              }
            }
          }
          if (updated) {
            if (sortBySize)
              sortList(currentFiles); // Re-sort if sorting by size
            needsRedraw = true;
          }
        }
      }

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
          bool isError = statusMessage.find("Failed") != std::string::npos ||
                         statusMessage.find("Error") != std::string::npos;
          attron(COLOR_PAIR(isError ? 8 : 7) | A_BOLD);
          printw(" %s ", statusMessage.c_str());
          attroff(COLOR_PAIR(isError ? 8 : 7) | A_BOLD);
        } else {
          attron(COLOR_PAIR(6) | A_BOLD);
          printw(" Fyzenor ");
          attroff(COLOR_PAIR(6) | A_BOLD);

          attron(A_DIM);
          if (focusPinned)
            printw(" 󰄾 Nav:j/k Jump:Enter Unpin:d Files:Tab");
          else
            printw(" 󰄾 Space:  y:  x:  p:  d:󱂥  z: r:  "
                   "s: Pins: ");
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
        case 'c':
          handleCopyPath();
          break;
        case 's':
          toggleSort();
          break;
        }
      }
    }
  }
};

const std::string VERSION = "1.1.0";

int main(int argc, char *argv[]) {
  std::string cwdFileArg;
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-v" || arg == "--version") {
        std::cout << "Fyzenor version " << VERSION << std::endl;
        return 0;
      } else if (arg == "-h" || arg == "--help") {
        std::cout << "Fyzenor - The Blazing Fast, Modern C++ Terminal File Manager"
                  << std::endl;
        std::cout << "Usage: fyzenor [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -v, --version         Show version information" << std::endl;
        std::cout << "  -h, --help            Show this help message" << std::endl;
        std::cout << "  --cwd-file <file>     Write the final working directory to <file> on exit" << std::endl;
        return 0;
      } else if (arg == "--cwd-file" && i + 1 < argc) {
        cwdFileArg = argv[++i];
      }
    }
  }
  FileManager fm;
  if (!cwdFileArg.empty())
    fm.setCwdFile(cwdFileArg);
  fm.run();
  return 0;
}

