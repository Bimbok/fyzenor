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
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <chrono>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

namespace fs = std::filesystem;

// Configuration
const std::set<std::string> VIDEO_EXTS = {".mp4", ".mkv", ".avi", ".mov", ".flv", ".wmv", ".webm"};
const std::set<std::string> IMAGE_EXTS = {".png", ".jpg",  ".jpeg", ".gif",
                                          ".bmp", ".webp", ".svg",  ".tiff"};

const std::set<std::string> FRONTEND_EXTS = {".js",    ".jsx", ".ts",   ".tsx",    ".css",
                                             ".scss",  ".vue", ".html", ".svelte", ".htm",
                                             ".astro", ".mjx", ".dart", ".swift"};
const std::set<std::string> SCRIPTS_EXTS = {".sh", ".bash", ".zsh", ".pl", ".awk", ".ps1", ".psm1"};
const std::set<std::string> CONFIG_EXTS = {
    ".json", ".xml",        ".yaml",       ".yml",  ".toml",      ".ini",          ".conf",
    ".env",  ".dockerfile", ".properties", ".lock", ".gitignore", ".gitattributes"};
const std::set<std::string> DOCUMENTATION_EXTS = {".md",  ".txt",  ".pdf", ".doc",  ".docx",
                                                  ".ppt", ".pptx", ".xls", ".xlsx", ".csv"};
const std::set<std::string> CORE_EXTS = {
    ".py",  ".rb",  ".php",   ".cpp",  ".c",    ".hpp",   ".h",  ".rs", ".java", ".go",
    ".lua", ".sql", ".cmake", ".make", ".diff", ".patch", ".kt", ".cs", ".scala"};
const std::set<std::string> FONT_EXTS = {".woff", ".woff2", ".ttf", ".eot", ".otf"};

const std::set<std::string> AUDIO_EXTS = {".mp3", ".wav", ".flac", ".m4a",
                                          ".aac", ".ogg", ".wma",  ".opus"};
const std::set<std::string> ARCHIVE_EXTS = {".zip", ".tar", ".gz", ".7z", ".rar", ".xz", ".bz2"};

const char* ICON_DIR = " ";
const char* ICON_VIDEO = " ";
const char* ICON_IMAGE = " ";
const char* ICON_CORE = " ";
const char* ICON_FRONTEND = "󰖟 ";
const char* ICON_CONFIG = " ";
const char* ICON_SCRIPT = " ";
const char* ICON_DOCS = " ";
const char* ICON_FONT = " ";
const char* ICON_FILE = " ";
const char* ICON_MUSIC = " ";
const char* ICON_PIN = " ";
const char* ICON_ZIP = "󰿺 ";

std::string getCacheDir() {
  const char* home = getenv("HOME");
  fs::path cacheDir;
  if (home) {
    cacheDir = fs::path(home) / ".cache/fyzenor/previews";
  } else {
    cacheDir = fs::temp_directory_path() / "fyzenor/previews";
  }
  if (!fs::exists(cacheDir)) {
    try {
      fs::create_directories(cacheDir);
    } catch (...) {
      return "/tmp";
    }
  }
  return cacheDir.string();
}

std::string getCachePath(const fs::path& p, int w, int h) {
  try {
    auto mtime = fs::last_write_time(p).time_since_epoch().count();
    std::string to_hash =
        p.string() + std::to_string(mtime) + std::to_string(w) + std::to_string(h);

    unsigned long hash = 5381;
    for (char c : to_hash)
      hash = ((hash << 5) + hash) + (unsigned char)c;

    char hex[32];
    snprintf(hex, sizeof(hex), "%lx", hash);
    return (fs::path(getCacheDir()) / (std::string(hex) + ".png")).string();
  } catch (...) {
    return "/tmp/fm_preview_thumb.png";
  }
}

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

  FileEntry(const fs::path& p) : path(p) {
    name = p.filename().string();
    is_directory = fs::is_directory(p);
    extension = p.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

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
struct FileStyle {
  int pair;
  const char* icon;
};

FileStyle getFileStyle(const std::string& ext, bool isDir) {
  if (isDir)
    return {1, ICON_DIR};

  if (VIDEO_EXTS.count(ext))
    return {4, ICON_VIDEO};
  if (IMAGE_EXTS.count(ext))
    return {5, ICON_IMAGE};
  if (AUDIO_EXTS.count(ext))
    return {4, ICON_MUSIC};
  if (FRONTEND_EXTS.count(ext))
    return {24, ICON_FRONTEND};
  if (CONFIG_EXTS.count(ext))
    return {25, ICON_CONFIG};
  if (SCRIPTS_EXTS.count(ext))
    return {26, ICON_SCRIPT};
  if (DOCUMENTATION_EXTS.count(ext))
    return {27, ICON_DOCS};
  if (FONT_EXTS.count(ext))
    return {28, ICON_FONT};
  if (CORE_EXTS.count(ext))
    return {16, ICON_CORE};
  if (ARCHIVE_EXTS.count(ext))
    return {17, ICON_ZIP};

  return {2, ICON_FILE};
}

int getFinalPair(int base, bool isSelected, bool isSecondary) {
  if (isSelected)
    return base + 40;
  if (isSecondary)
    return base + 80;
  return base;
}

static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789+/";

std::string base64_encode(const unsigned char* bytes, size_t len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  while (len--) {
    char_array_3[i++] = *(bytes++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
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
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
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
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
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

std::string getFileModifiedTime(const fs::path& path) {
  try {
    auto ftime = fs::last_write_time(path);
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t ctime = std::chrono::system_clock::to_time_t(sctp);
    std::tm* ltime = std::localtime(&ctime);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %I:%M:%S %p", ltime);
    return std::string(buffer);
  } catch (...) {
    return "Unknown";
  }
}

bool is_binary_file(const std::string& path) {
  try {
    if (fs::is_directory(path))
      return false;
  } catch (...) {
    return false;
  }
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
  std::chrono::steady_clock::time_point statusTime;
  bool showHidden = false;
  bool sortBySize = false;
  bool isSearching = false;

  // Async Preview State
  std::mutex previewMutex;
  std::atomic<bool> imageReady{false};
  std::string cachedBase64;
  int cachedImgW = 0, cachedImgH = 0;
  std::vector<std::string> cachedTextLines;
  PreviewType pendingDirectRenderType = PreviewType::NONE;
  struct ImageCacheEntry {
    std::string b64;
    int w, h;
  };
  std::unordered_map<std::string, ImageCacheEntry> sessionImageCache;
  std::string cachedPath;
  std::string requestedPath;
  long long requestID = 0;
  bool lastWasDirectRender = false;

  struct PreviewJob {
    std::string path;
    PreviewType type;
    int previewHeight;
    int previewWidth;
    long long reqId;
  };
  std::unique_ptr<PreviewJob> nextPreviewJob;
  std::condition_variable previewCv;
  std::thread previewWorker;

  std::thread searchThread;
  FILE* searchPipe = nullptr;
  std::mutex searchMutex;
  long long searchRequestID = 0;
  std::atomic<bool> searchReady{false};

  std::vector<FileEntry> pendingSearchResults;
  std::string pendingSearchStatus;
  bool hasPendingSearchResults = false;
  std::mutex searchResultMutex;

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
        {"DIR", "#89b4fa"},     {"FILE", "#cdd6f4"},       {"SEL_BG", "#585b70"},
        {"MEDIA", "#f9e2af"},   {"IMAGE", "#f5c2e7"},      {"BORDER", "#b4befe"},
        {"SUCCESS", "#a6e3a1"}, {"ERROR", "#f38ba8"},      {"MULTI", "#f5e0dc"},
        {"PIN_BG", "#cba6f7"},  {"PIN_BORDER", "#89b4fa"}, {"SEC_SEL_BG", "#313244"},
        {"CORE", "#a6e3a1"},    {"ARCHIVE", "#eba0ac"},    {"FRONTEND", "#fab387"},
        {"CONFIG", "#94e2d5"},  {"SCRIPT", "#f9e2af"},     {"DOCS", "#f2cdcd"},
        {"FONT", "#cba6f7"}};

    const char* home = getenv("HOME");
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
        f << "MULTI: #f5e0dc\n";
        f << "PIN_BG: #cba6f7\n";
        f << "PIN_BORDER: #89b4fa\n";
        f << "SEC_SEL_BG: #313244\n";
        f << "CORE: #a6e3a1\n";
        f << "ARCHIVE: #eba0ac\n";
        f << "FRONTEND: #fab387\n";
        f << "CONFIG: #94e2d5\n";
        f << "SCRIPT: #f9e2af\n";
        f << "DOCS: #f2cdcd\n";
        f << "FONT: #cba6f7\n";
      }
    }

    auto setHex = [](short id, const std::string& hex) {
      if (hex.length() < 7 || hex[0] != '#')
        return;
      int r, g, b;
      if (sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        init_color(id, (short)(r * 1000 / 255), (short)(g * 1000 / 255), (short)(b * 1000 / 255));
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
      setHex(32, colors["CORE"]);
      setHex(33, colors["ARCHIVE"]);
      setHex(34, colors["FRONTEND"]);
      setHex(35, colors["CONFIG"]);
      setHex(36, colors["SCRIPT"]);
      setHex(37, colors["DOCS"]);
      setHex(38, colors["FONT"]);
      init_pair(1, 20, -1);   // DIR
      init_pair(2, 21, -1);   // FILE
      init_pair(4, 23, -1);   // MEDIA
      init_pair(5, 24, -1);   // IMAGE
      init_pair(16, 32, -1);  // CORE
      init_pair(17, 33, -1);  // ARCHIVE
      init_pair(24, 34, -1);  // FRONTEND
      init_pair(25, 35, -1);  // CONFIG
      init_pair(26, 36, -1);  // SCRIPT
      init_pair(27, 37, -1);  // DOCS
      init_pair(28, 38, -1);  // FONT
      init_pair(41, 20, 22);  // SEL_DIR
      init_pair(42, 21, 22);  // SEL_FILE
      init_pair(44, 23, 22);  // SEL_MEDIA
      init_pair(45, 24, 22);  // SEL_IMAGE
      init_pair(56, 32, 22);  // SEL_CORE
      init_pair(57, 33, 22);  // SEL_ARCHIVE
      init_pair(64, 34, 22);  // SEL_FRONTEND
      init_pair(65, 35, 22);  // SEL_CONFIG
      init_pair(66, 36, 22);  // SEL_SCRIPT
      init_pair(67, 37, 22);  // SEL_DOCS
      init_pair(68, 38, 22);  // SEL_FONT
      init_pair(81, 20, 31);  // SEC_SEL_DIR
      init_pair(82, 21, 31);  // SEC_SEL_FILE
      init_pair(84, 23, 31);  // SEC_SEL_MEDIA
      init_pair(85, 24, 31);  // SEC_SEL_IMAGE
      init_pair(96, 32, 31);  // SEC_SEL_CORE
      init_pair(97, 33, 31);  // SEC_SEL_ARCHIVE
      init_pair(104, 34, 31); // SEC_SEL_FRONTEND
      init_pair(105, 35, 31); // SEC_SEL_CONFIG
      init_pair(106, 36, 31); // SEC_SEL_SCRIPT
      init_pair(107, 37, 31); // SEC_SEL_DOCS
      init_pair(108, 38, 31); // SEC_SEL_FONT
      init_pair(6, 25, -1);   // BORDER
      init_pair(7, 26, -1);   // SUCCESS
      init_pair(8, 27, -1);   // ERROR
      init_pair(9, 28, -1);   // MULTI
      init_pair(15, 30, -1);  // PIN_BORDER
      init_pair(10, 31, 29);  // SEL_PIN (foreground SEC_SEL_BG, background PIN_BG)
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
      init_pair(15, COLOR_BLUE, -1);
    }
  }

  int get_or_create_color_pair(short fg, short bg) {
    static std::map<std::pair<short, short>, int> pairCache;
    static int nextPairId = 110;

    auto key = std::make_pair(fg, bg);
    auto it = pairCache.find(key);
    if (it != pairCache.end()) {
      return it->second;
    }
    if (nextPairId < COLOR_PAIRS) {
      init_pair(nextPairId, fg, bg);
      pairCache[key] = nextPairId;
      return nextPairId++;
    }
    return 0;
  }

  void wprintw_ansi(WINDOW* win, int y, int x, const std::string& line, int maxW) {
    wmove(win, y, x);
    int startX = x;

    attr_t currentAttrs = A_NORMAL;
    short fg = -1;
    short bg = -1;

    size_t i = 0;
    while (i < line.size()) {
      int cy, cx;
      getyx(win, cy, cx);
      if (cx >= startX + maxW) {
        break;
      }

      if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
        i += 2;
        std::string seq;
        while (i < line.size() && !isalpha(line[i])) {
          seq += line[i];
          i++;
        }
        if (i >= line.size()) break;
        char cmd = line[i];
        i++;

        if (cmd == 'm') {
          std::vector<int> params;
          int val = 0;
          bool hasVal = false;
          for (char c : seq) {
            if (c == ';') {
              params.push_back(hasVal ? val : 0);
              val = 0;
              hasVal = false;
            } else if (c >= '0' && c <= '9') {
              val = val * 10 + (c - '0');
              hasVal = true;
            }
          }
          if (hasVal) {
            params.push_back(val);
          }
          if (params.empty()) {
            params.push_back(0);
          }

          for (size_t p = 0; p < params.size(); ++p) {
            int valParam = params[p];
            if (valParam == 0) {
              currentAttrs = A_NORMAL;
              fg = -1;
              bg = -1;
            } else if (valParam == 1) {
              currentAttrs |= A_BOLD;
            } else if (valParam == 2) {
              currentAttrs |= A_DIM;
            } else if (valParam == 3) {
              currentAttrs |= A_ITALIC;
            } else if (valParam == 4) {
              currentAttrs |= A_UNDERLINE;
            } else if (valParam == 22) {
              currentAttrs &= ~A_BOLD;
              currentAttrs &= ~A_DIM;
            } else if (valParam == 23) {
              currentAttrs &= ~A_ITALIC;
            } else if (valParam == 24) {
              currentAttrs &= ~A_UNDERLINE;
            } else if (valParam >= 30 && valParam <= 37) {
              fg = valParam - 30;
            } else if (valParam == 39) {
              fg = -1;
            } else if (valParam >= 40 && valParam <= 47) {
              bg = valParam - 40;
            } else if (valParam == 49) {
              bg = -1;
            } else if (valParam >= 90 && valParam <= 97) {
              fg = valParam - 90 + 8;
            } else if (valParam >= 100 && valParam <= 107) {
              bg = valParam - 100 + 8;
            } else if (valParam == 38) {
              if (p + 2 < params.size() && params[p + 1] == 5) {
                fg = params[p + 2];
                p += 2;
              } else if (p + 4 < params.size() && params[p + 1] == 2) {
                int r = params[p + 2];
                int g = params[p + 3];
                int b = params[p + 4];
                if (r == g && g == b) {
                  if (r < 8) fg = 16;
                  else if (r > 248) fg = 231;
                  else fg = 232 + (r - 8) * 24 / 240;
                } else {
                  int qr = (r * 5 + 127) / 255;
                  int qg = (g * 5 + 127) / 255;
                  int qb = (b * 5 + 127) / 255;
                  fg = 16 + 36 * qr + 6 * qg + qb;
                }
                p += 4;
              }
            } else if (valParam == 48) {
              if (p + 2 < params.size() && params[p + 1] == 5) {
                bg = params[p + 2];
                p += 2;
              } else if (p + 4 < params.size() && params[p + 1] == 2) {
                int r = params[p + 2];
                int g = params[p + 3];
                int b = params[p + 4];
                if (r == g && g == b) {
                  if (r < 8) bg = 16;
                  else if (r > 248) bg = 231;
                  else bg = 232 + (r - 8) * 24 / 240;
                } else {
                  int qr = (r * 5 + 127) / 255;
                  int qg = (g * 5 + 127) / 255;
                  int qb = (b * 5 + 127) / 255;
                  bg = 16 + 36 * qr + 6 * qg + qb;
                }
                p += 4;
              }
            }
          }

          wattrset(win, currentAttrs);
          if (fg != -1 || bg != -1) {
            int pair = get_or_create_color_pair(fg, bg);
            wattron(win, COLOR_PAIR(pair));
          } else {
            wattron(win, COLOR_PAIR(2));
          }
        }
      } else {
        char c = line[i];
        if (c == '\r' || c == '\n') {
          i++;
          continue;
        }
        if (c == '\t') {
          int spacesToPrint = 4 - ((cx - startX) % 4);
          for (int s = 0; s < spacesToPrint; ++s) {
            getyx(win, cy, cx);
            if (cx >= startX + maxW) break;
            waddch(win, ' ');
          }
          i++;
        } else {
          waddch(win, (unsigned char)c);
          i++;
        }
      }
    }

    wattrset(win, A_NORMAL);
    wattron(win, COLOR_PAIR(2));
    int cy, cx;
    getyx(win, cy, cx);
    int prevCx = cx;
    while (cx < startX + maxW) {
      waddch(win, ' ');
      getyx(win, cy, cx);
      if (cx <= prevCx) {
        break;
      }
      prevCx = cx;
    }
  }

public:
  FileManager()
      : selectedIndex(0), scrollOffset(0), winPinned(nullptr), winParent(nullptr),
        winCurrent(nullptr), winPreview(nullptr) {
    setlocale(LC_ALL, "");
    loadPins();

    sizeWorker = std::thread(&FileManager::processSizeQueue, this);
    previewWorker = std::thread(&FileManager::processPreviewWorker, this);

    try {
      currentPath = fs::current_path();
    } catch (...) {
      const char* home = getenv("HOME");
      if (home) {
        currentPath = fs::path(home);
      } else {
        currentPath = fs::path("/");
      }
    }
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

  void cancelSearch() {
    FILE* pipeToClose = nullptr;
    {
      std::lock_guard<std::mutex> lock(searchMutex);
      searchRequestID++;
      if (searchPipe) {
        pipeToClose = searchPipe;
        searchPipe = nullptr;
      }
    }
    if (pipeToClose) {
      fclose(pipeToClose);
    }
    if (searchThread.joinable()) {
      searchThread.join();
    }
  }

  ~FileManager() {
    stopWorker = true;
    queueCv.notify_all();
    previewCv.notify_all();

    cancelSearch();

    if (sizeWorker.joinable())
      sizeWorker.join();
    if (previewWorker.joinable())
      previewWorker.join();

    if (winPinned)
      delwin(winPinned);
    if (winParent)
      delwin(winParent);
    if (winCurrent)
      delwin(winCurrent);
    if (winPreview)
      delwin(winPreview);
    clearDirectRender();
    endwin();
  }

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
          for (const auto& entry : fs::recursive_directory_iterator(
                   job.path, fs::directory_options::skip_permission_denied)) {
            if (job.viewId != currentViewId || stopWorker)
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
    const char* home = getenv("HOME");
    if (home)
      return std::string(home) + "/.fm_pins";
    return ".fm_pins";
  }
  void loadPins() {
    pinnedPaths.clear();
    std::ifstream f(getPinFile());
    std::string line;
    while (std::getline(f, line)) {
      if (!line.empty()) {
        try {
          if (fs::exists(line))
            pinnedPaths.push_back(line);
        } catch (...) {}
      }
    }
  }
  void savePins() {
    std::ofstream f(getPinFile());
    for (const auto& p : pinnedPaths)
      f << p.string() << "\n";
  }
  void handlePin() {
    for (const auto& p : pinnedPaths) {
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
      fs::path target = pinnedPaths[pinnedIndex];
      try {
        if (!fs::exists(target)) {
          setStatus("Error: Pinned path no longer exists!");
          return;
        }
        if (!fs::is_directory(target)) {
          setStatus("Error: Pinned path is not a directory!");
          return;
        }
      } catch (...) {
        setStatus("Error: Cannot access pinned path");
        return;
      }
      currentPath = target;
      reloadAll();
      focusPinned = false;
      setStatus("Jumped to pin");
    }
  }

  bool isCodeFile(const std::string& ext) {
    if (ext == ".pdf" || ext == ".doc" || ext == ".docx" ||
        ext == ".ppt" || ext == ".pptx" || ext == ".xls" || ext == ".xlsx") {
      return false;
    }
    return CORE_EXTS.count(ext) || FRONTEND_EXTS.count(ext) || SCRIPTS_EXTS.count(ext) ||
           CONFIG_EXTS.count(ext) || DOCUMENTATION_EXTS.count(ext);
  }

  const char* getIcon(const FileEntry& f) {
    if (f.is_directory)
      return ICON_DIR;
    if (VIDEO_EXTS.count(f.extension))
      return ICON_VIDEO;
    if (IMAGE_EXTS.count(f.extension))
      return ICON_IMAGE;
    if (AUDIO_EXTS.count(f.extension))
      return ICON_MUSIC;
    if (FRONTEND_EXTS.count(f.extension))
      return ICON_FRONTEND;
    if (CONFIG_EXTS.count(f.extension))
      return ICON_CONFIG;
    if (SCRIPTS_EXTS.count(f.extension))
      return ICON_SCRIPT;
    if (DOCUMENTATION_EXTS.count(f.extension))
      return ICON_DOCS;
    if (FONT_EXTS.count(f.extension))
      return ICON_FONT;
    if (CORE_EXTS.count(f.extension))
      return ICON_CORE;
    if (ARCHIVE_EXTS.count(f.extension))
      return ICON_ZIP;
    return ICON_FILE;
  }

  // Unified Sorting Logic: Folders Top -> Size/Name
  void sortList(std::vector<FileEntry>& list) {
    std::sort(list.begin(), list.end(), [this](const FileEntry& a, const FileEntry& b) {
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

  void loadDirectory(const fs::path& path, std::vector<FileEntry>& target) {
    cancelSearch();
    isSearching = false;
    target.clear();
    multiSelection.clear();
    currentViewId++;
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      sizeQueue.clear();
    }
    {
      std::lock_guard<std::mutex> lock(resultMutex);
      resultQueue.clear();
    }
    try {
      for (const auto& entry : fs::directory_iterator(path)) {
        if (!showHidden && entry.path().filename().string().front() == '.')
          continue;
        target.emplace_back(entry);
      }
    } catch (const std::exception& e) {
      setStatus("Error: " + std::string(e.what()));
    }

    // Check cache and only queue what's missing
    {
      std::lock_guard<std::mutex> qLock(queueMutex);
      std::lock_guard<std::mutex> cLock(cacheMutex);
      for (auto& entry : target) {
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
    if (currentPath.has_parent_path() && currentPath != currentPath.parent_path()) {
      parentFiles.clear();
      try {
        for (const auto& entry : fs::directory_iterator(currentPath.parent_path())) {
          parentFiles.emplace_back(entry);
        }
      } catch (...) {
      }
      // Standard sort for parent to keep it stable
      std::sort(parentFiles.begin(), parentFiles.end(), [](const FileEntry& a, const FileEntry& b) {
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
  void startAsyncPreview(const std::string& path, PreviewType type, int previewHeight,
                         int previewWidth) {
    requestID++;
    requestedPath = path;
    imageReady = false;

    if (type == PreviewType::IMAGE) {
      std::lock_guard<std::mutex> lock(previewMutex);
      auto it = sessionImageCache.find(path);
      if (it != sessionImageCache.end()) {
        cachedBase64 = it->second.b64;
        cachedImgW = it->second.w;
        cachedImgH = it->second.h;
        cachedPath = path;
        imageReady = true;
        return;
      }
    }

    {
      std::lock_guard<std::mutex> lock(previewMutex);
      nextPreviewJob = std::make_unique<PreviewJob>(PreviewJob{path, type, previewHeight, previewWidth, requestID});
    }
    previewCv.notify_one();
  }

  void processPreviewWorker() {
    while (!stopWorker) {
      std::unique_ptr<PreviewJob> job;
      {
        std::unique_lock<std::mutex> lock(previewMutex);
        previewCv.wait(lock, [this] { return nextPreviewJob != nullptr || stopWorker; });
        if (stopWorker)
          break;
        job = std::move(nextPreviewJob);
      }

      if (!job)
        continue;

      if (job->reqId != requestID)
        continue;

      std::string b64;
      std::vector<std::string> lines;

      if (job->type == PreviewType::IMAGE) {
        int targetW = (int)((job->previewWidth - 4) * 10);
        int targetH = (int)((job->previewHeight - 4) * 20);
        if (targetW < 10)
          targetW = 10;
        if (targetH < 10)
          targetH = 10;

        std::string cachePath = getCachePath(job->path, targetW, targetH);

        std::string ext = fs::path(job->path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        bool isVid = VIDEO_EXTS.count(ext);

        if (job->reqId != requestID)
          continue;

        if (!fs::exists(cachePath)) {
          std::string fileCmd = "\"" + job->path + "\"";
          std::string scaleFilter = "scale=" + std::to_string(targetW) + ":" +
                                    std::to_string(targetH) +
                                    ":force_original_aspect_ratio=decrease";

          std::string cmd;
          if (isVid) {
            cmd = "ffmpeg -y -v error -i " + fileCmd + " -vf \"" + scaleFilter +
                  "\" -frames:v 1 -f image2 \"" + cachePath + "\" > /dev/null 2>&1";
          } else {
            cmd = "ffmpeg -y -v error -i " + fileCmd + " -vf \"" + scaleFilter + "\" -f image2 \"" +
                  cachePath + "\" > /dev/null 2>&1";
          }
          int res = system(cmd.c_str());
          (void)res;
        }

        if (job->reqId != requestID)
          continue;

        int finalW = 0, finalH = 0;
        std::string probeCmd = "ffprobe -v error -select_streams v:0 -show_entries "
                               "stream=width,height -of csv=s=x:p=0 \"" +
                               cachePath + "\"";
        FILE* p = popen(probeCmd.c_str(), "r");
        if (p) {
          char buf[64];
          if (fgets(buf, sizeof(buf), p)) {
            sscanf(buf, "%dx%d", &finalW, &finalH);
          }
          pclose(p);
        }

        if (job->reqId != requestID)
          continue;

        std::ifstream file(cachePath, std::ios::binary);
        if (file) {
          std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
          b64 = base64_encode(buffer.data(), buffer.size());
        }

        {
          std::lock_guard<std::mutex> lock(previewMutex);
          if (job->reqId == requestID) {
            cachedImgW = finalW;
            cachedImgH = finalH;
            cachedBase64 = b64;
            sessionImageCache[job->path] = {b64, cachedImgW, cachedImgH};
            cachedPath = job->path;
            imageReady = true;
          }
        }
      } else if (job->type == PreviewType::TEXT) {
        if (is_binary_file(job->path)) {
          lines.push_back("\033[1;31m[Binary File]\033[0m");
        } else {
          if (job->reqId != requestID)
            continue;

          std::string cmd = "bat --color=always --style=plain --paging=never "
                            "--wrap=character --line-range=:" +
                            std::to_string(job->previewHeight * 2) + " \"" + job->path + "\" 2>/dev/null";
          FILE* pipe = popen(cmd.c_str(), "r");
          bool gotOutput = false;
          if (pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
              if (job->reqId != requestID || stopWorker)
                break;
              std::string line(buffer);
              if (!line.empty() && line.back() == '\n')
                line.pop_back();
              lines.push_back(line);
              gotOutput = true;
            }
            pclose(pipe);
          }

          if (job->reqId != requestID)
            continue;

          if (!gotOutput) {
            lines.clear();
            cmd = "batcat --color=always --style=plain --paging=never "
                  "--wrap=character --line-range=:" +
                  std::to_string(job->previewHeight * 2) + " \"" + job->path + "\" 2>/dev/null";
            pipe = popen(cmd.c_str(), "r");
            if (pipe) {
              char buffer[1024];
              while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                if (job->reqId != requestID || stopWorker)
                  break;
                std::string line(buffer);
                if (!line.empty() && line.back() == '\n')
                  line.pop_back();
                lines.push_back(line);
                gotOutput = true;
              }
              pclose(pipe);
            }
          }

          if (job->reqId != requestID)
            continue;

          if (!gotOutput) {
            lines.clear();
            std::ifstream f(job->path);
            if (f.is_open()) {
              std::string lineStr;
              int count = 0;
              while (std::getline(f, lineStr) && count < job->previewHeight) {
                if (job->reqId != requestID || stopWorker)
                  break;
                std::string clean;
                for (char c : lineStr) {
                  if (c == '\t')
                    clean += "    ";
                  else
                    clean += c;
                }
                if (clean.length() > (size_t)job->previewWidth)
                  clean = clean.substr(0, job->previewWidth);
                lines.push_back(clean);
                count++;
              }
            }
          }
        }

        {
          std::lock_guard<std::mutex> lock(previewMutex);
          if (job->reqId == requestID) {
            cachedTextLines = lines;
            cachedPath = job->path;
            imageReady = true;
          }
        }
      }
    }
  }

  void sendKittyGraphics(const std::string& b64Data, int pY, int pX, int cols, int rows,
                         int offX = 0, int offY = 0) {
    // Move cursor to start of preview area (1-indexed for terminal)
    // pY+1 is the start of the window, we have 8 lines of header/padding +
    // offY.
    std::cout << "\033[" << (pY + 8 + offY) << ";" << (pX + 3 + offX) << "H";
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

      // Box starts at line 8, ends at pH-2. Total height = pH - 9.
      // Width starts at pX+3, ends at pX+pW-2. Total width = pW - 4.
      int boxW = pW - 4;
      int boxH = pH - 9;

      int offX = (boxW - cols) / 2;
      int offY = (boxH - rows) / 2;
      if (offX < 0)
        offX = 0;
      if (offY < 0)
        offY = 0;

      sendKittyGraphics(cachedBase64, pY, pX, cols, rows, offX, offY);
      lastWasDirectRender = true;
    }
  }

  void drawCachedTextPreview() {
    std::lock_guard<std::mutex> lock(previewMutex);
    if (cachedTextLines.empty())
      return;

    int maxW = getmaxx(winPreview) - 4;
    int lineLimit = std::min((int)cachedTextLines.size(), getmaxy(winPreview) - 9);

    for (int i = 0; i < lineLimit; ++i) {
      wprintw_ansi(winPreview, 7 + i, 2, cachedTextLines[i], maxW);
    }
  }
  // ----------------------------

  void setStatus(const std::string& msg) {
    statusMessage = msg;
    statusTime = std::chrono::steady_clock::now();
  }

  void drawStatusToast() {
    if (statusMessage.empty())
      return;

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - statusTime).count() > 1800) {
      statusMessage = "";
      return;
    }

    int w = statusMessage.length() + 4;
    if (w > width - 4) {
      w = width - 4;
    }
    if (w < 6) {
      w = 6;
    }
    int h = 3;
    int y = height - h - 1;
    if (y < 1) {
      y = 1;
    }
    int x = width - w - 2;
    if (x < 1) {
      x = 1;
    }

    WINDOW* toastWin = newwin(h, w, y, x);
    if (!toastWin) return;

    bool isError = statusMessage.find("Failed") != std::string::npos ||
                   statusMessage.find("Error") != std::string::npos;
    int colorPair = isError ? 8 : 7;

    wattron(toastWin, COLOR_PAIR(colorPair) | A_BOLD);
    drawRoundedBox(toastWin);
    wattroff(toastWin, COLOR_PAIR(colorPair) | A_BOLD);

    std::string dispMsg = statusMessage;
    if ((int)dispMsg.length() > w - 4) {
      dispMsg = dispMsg.substr(0, w - 7) + "...";
    }

    mvwprintw(toastWin, 1, 2, "%s", dispMsg.c_str());
    wnoutrefresh(toastWin);
    delwin(toastWin);
  }

  std::string promptInput(const std::string& prompt, const std::string& defaultVal = "") {
    int w = std::max((int)prompt.length() + 10, 50);
    if (w > width - 4)
      w = width - 4;
    int h = 5;
    int y = (height - h) / 2;
    int x = (width - w) / 2;

    WINDOW* win = newwin(h, w, y, x);
    if (!win) return defaultVal;
    keypad(win, TRUE);

    wattron(win, COLOR_PAIR(6) | A_BOLD);
    drawRoundedBox(win);
    wattroff(win, COLOR_PAIR(6) | A_BOLD);

    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 1, 2, "%s", prompt.c_str());
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    std::string input = defaultVal;
    int cursorIdx = defaultVal.length();
    int inputFieldX = 2;
    int inputFieldY = 3;
    int maxInputW = w - 4;

    timeout(-1);
    noecho();
    curs_set(1);

    while (true) {
      wmove(win, inputFieldY, inputFieldX);
      for (int i = 0; i < maxInputW; ++i) {
        waddch(win, ' ');
      }

      int startIdx = 0;
      if (cursorIdx >= maxInputW) {
        startIdx = cursorIdx - maxInputW + 1;
      }
      std::string visibleInput = input.substr(startIdx);
      if ((int)visibleInput.length() > maxInputW) {
        visibleInput = visibleInput.substr(0, maxInputW);
      }
      mvwprintw(win, inputFieldY, inputFieldX, "%s", visibleInput.c_str());

      int cursorCol = inputFieldX + (cursorIdx - startIdx);
      wmove(win, inputFieldY, cursorCol);
      wrefresh(win);

      int ch = wgetch(win);
      if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
        break;
      } else if (ch == 27) {
        input = "";
        break;
      } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (cursorIdx > 0 && !input.empty()) {
          input.erase(cursorIdx - 1, 1);
          cursorIdx--;
        }
      } else if (ch == KEY_DC) {
        if (cursorIdx < (int)input.length()) {
          input.erase(cursorIdx, 1);
        }
      } else if (ch == KEY_LEFT) {
        if (cursorIdx > 0)
          cursorIdx--;
      } else if (ch == KEY_RIGHT) {
        if (cursorIdx < (int)input.length())
          cursorIdx++;
      } else if (ch == KEY_HOME || ch == 1) {
        cursorIdx = 0;
      } else if (ch == KEY_END || ch == 5) {
        cursorIdx = input.length();
      } else if (ch >= 32 && ch <= 126) {
        if (input.length() < 255) {
          input.insert(cursorIdx, 1, (char)ch);
          cursorIdx++;
        }
      }
    }

    curs_set(0);
    timeout(50);
    delwin(win);

    updateLayout();
    return input;
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
    for (const auto& f : currentFiles)
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

  std::string escapeDoubleQuotes(const std::string& str) {
    std::string result;
    for (char c : str) {
      if (c == '"' || c == '\\' || c == '$' || c == '`') {
        result += '\\';
      }
      result += c;
    }
    return result;
  }

  void handleSearch() {
    if (system("which rg > /dev/null 2>&1") != 0) {
      setStatus("Error: ripgrep ('rg') is not installed");
      return;
    }

    std::string query = promptInput("Search (ripgrep)");
    if (query.empty())
      return;

    setStatus("Searching...");
    isSearching = true;
    currentFiles.clear();
    selectedIndex = 0;
    scrollOffset = 0;
    refresh();

    cancelSearch();

    long long reqId = searchRequestID;

    searchThread = std::thread([this, query, reqId]() {
      std::string cmd = "rg --files-with-matches --smart-case --hidden --glob \"!.git\" \"" +
                        escapeDoubleQuotes(query) + "\" \"" +
                        escapeDoubleQuotes(currentPath.string()) + "\" 2>/dev/null";
      FILE* pipe = nullptr;
      {
        std::lock_guard<std::mutex> lock(searchMutex);
        if (reqId != searchRequestID) return;
        searchPipe = popen(cmd.c_str(), "r");
        pipe = searchPipe;
      }
      if (!pipe) {
        {
          std::lock_guard<std::mutex> lock(searchMutex);
          if (searchPipe == pipe) searchPipe = nullptr;
        }
        if (reqId == searchRequestID) {
          setStatus("Error: Failed to run ripgrep");
        }
        return;
      }

      std::vector<FileEntry> results;
      char buffer[4096];
      auto lastUpdate = std::chrono::steady_clock::now();

      while (true) {
        char* res = fgets(buffer, sizeof(buffer), pipe);
        if (!res || reqId != searchRequestID) break;
        std::string pathStr(buffer);
        if (!pathStr.empty() && pathStr.back() == '\n')
          pathStr.pop_back();
        if (!pathStr.empty()) {
          try {
            fs::path p(pathStr);
            if (fs::exists(p)) {
              results.emplace_back(p);
            }
          } catch (...) {
          }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() > 50 && !results.empty()) {
          if (reqId == searchRequestID) {
            std::lock_guard<std::mutex> lock(searchResultMutex);
            pendingSearchResults = results;
            pendingSearchStatus = "Searching... Found " + std::to_string(results.size()) + " matches";
            hasPendingSearchResults = true;
            searchReady = true;
          }
          lastUpdate = now;
        }
      }

      {
        std::lock_guard<std::mutex> lock(searchMutex);
        if (searchPipe == pipe) {
          searchPipe = nullptr;
        } else {
          pipe = nullptr;
        }
      }
      if (pipe) {
        pclose(pipe);
      }

      if (reqId == searchRequestID) {
        std::lock_guard<std::mutex> lock(searchResultMutex);
        pendingSearchResults = results;
        pendingSearchStatus = results.empty() ? ("No matches found for: " + query) : ("Search finished. Found " + std::to_string(results.size()) + " matches");
        hasPendingSearchResults = true;
        searchReady = true;
      }
    });
  }

  void handleCopy() {
    if (currentFiles.empty())
      return;
    clipboard.paths.clear();
    if (multiSelection.empty())
      clipboard.paths.push_back(currentFiles[selectedIndex].path);
    else
      for (const auto& p : multiSelection)
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
      for (const auto& p : multiSelection)
        clipboard.paths.push_back(p);
    clipboard.isCut = true;
    setStatus("Cut items");
    multiSelection.clear();
  }

  fs::path getNonConflictingPath(const fs::path& base) {
    if (!fs::exists(base))
      return base;
    fs::path parent = base.parent_path();
    std::string stem = base.stem().string();
    std::string ext = base.extension().string();
    int counter = 1;
    while (true) {
      fs::path newPath = parent / (stem + "_" + std::to_string(counter) + ext);
      if (!fs::exists(newPath))
        return newPath;
      counter++;
    }
  }

  void handlePaste() {
    if (clipboard.paths.empty()) {
      setStatus("Clipboard empty");
      return;
    }
    int successCount = 0;
    for (const auto& src : clipboard.paths) {
      fs::path dest = currentPath / src.filename();
      if (fs::exists(dest)) {
        if (clipboard.isCut && src == dest) {
          continue;
        }
        std::string filename = src.filename().string();
        if (filename.length() > 30) {
          filename = filename.substr(0, 27) + "...";
        }
        std::string promptStr = "'" + filename + "' exists. [r]eplace, [k]eep both, [c]ancel";
        std::string ans = promptInput(promptStr);
        char choice = 'c';
        if (!ans.empty()) {
          choice = std::tolower(ans[0]);
        }
        
        if (choice == 'r') {
          if (src == dest) {
            successCount++;
            continue;
          }
          try {
            fs::remove_all(dest);
          } catch (...) {
            setStatus("Error: Failed to replace " + dest.filename().string());
            continue;
          }
        } else if (choice == 'k') {
          dest = getNonConflictingPath(dest);
        } else {
          continue;
        }
      }
      try {
        if (clipboard.isCut) {
          try {
            fs::rename(src, dest);
          } catch (const fs::filesystem_error& e) {
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
    const auto& file = currentFiles[selectedIndex];
    std::string newName = promptInput("Rename " + file.name + " to", file.name);
    if (newName.empty())
      return;

    fs::path target = currentPath / newName;
    if (fs::exists(target)) {
      setStatus("Error: File already exists!");
      return;
    }

    try {
      fs::rename(file.path, target);
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
    fs::path target = currentPath / name;
    if (fs::exists(target)) {
      setStatus("Error: File already exists!");
      return;
    }
    std::ofstream(target).close();
    setStatus("Created file");
    reloadAll();
  }
  void handleNewFolder() {
    std::string name = promptInput("New Folder Name");
    if (name.empty())
      return;
    fs::path target = currentPath / name;
    if (fs::exists(target)) {
      setStatus("Error: Folder already exists!");
      return;
    }
    try {
      fs::create_directory(target);
      setStatus("Created folder");
      reloadAll();
    } catch (...) {
      setStatus("Error: Failed to create folder");
    }
  }
  void handleZip() {
    if (currentFiles.empty())
      return;
    std::vector<fs::path> targets;
    if (multiSelection.empty())
      targets.push_back(currentFiles[selectedIndex].path);
    else
      for (const auto& p : multiSelection)
        targets.push_back(p);
    std::string name = promptInput("Zip Name");
    if (name.empty())
      return;
    fs::path targetZip = currentPath / (name + ".zip");
    if (fs::exists(targetZip)) {
      setStatus("Error: Zip file already exists!");
      return;
    }
    std::string cmd = "zip -r -q \"" + name + ".zip\"";
    for (const auto& p : targets)
      cmd += " \"" + p.filename().string() + "\"";
    cmd += " > /dev/null 2>&1";
    fs::path old;
    bool pathSaved = false;
    try {
      old = fs::current_path();
      fs::current_path(currentPath);
      pathSaved = true;
    } catch (...) {}
    int res = system(cmd.c_str());
    (void)res;
    if (pathSaved) {
      try {
        fs::current_path(old);
      } catch (...) {}
    }
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
      for (const auto& p : multiSelection)
        targets.push_back(p);
    std::string countStr = (targets.size() > 1) ? std::to_string(targets.size()) + " items"
                                                : targets[0].filename().string();
    std::string confirm = promptInput("Delete " + countStr + "? (y/n)");
    if (confirm != "y" && confirm != "Y")
      return;
    for (const auto& p : targets) {
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
  void drawRoundedBox(WINDOW* win) {
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
      wattron(winPinned, COLOR_PAIR(6) | A_BOLD);
    else
      wattron(winPinned, COLOR_PAIR(15));
    drawRoundedBox(winPinned);
    if (focusPinned)
      wattroff(winPinned, COLOR_PAIR(6) | A_BOLD);
    else
      wattroff(winPinned, COLOR_PAIR(15));

    wattron(winPinned, A_BOLD | COLOR_PAIR(4));
    mvwprintw(winPinned, 0, 2, " 󰐃 Pinned ");
    wattroff(winPinned, A_BOLD | COLOR_PAIR(4));

    for (size_t i = 0; i < pinnedPaths.size() && i < (size_t)getmaxy(winPinned) - 2; ++i) {
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
    wnoutrefresh(winPinned);
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
    if (start + maxLines > (int)parentFiles.size() && (int)parentFiles.size() > maxLines)
      start = parentFiles.size() - maxLines;

    for (int i = 0; i < maxLines && (start + i) < (int)parentFiles.size(); ++i) {
      const auto& file = parentFiles[start + i];
      bool isCurrent = (static_cast<int>(start + i) == highlightIdx);
      wmove(winParent, i + 1, 1);

      FileStyle style = getFileStyle(file.extension, file.is_directory);
      int finalPair = getFinalPair(style.pair, false, isCurrent);

      if (isCurrent) {
        wattron(winParent, COLOR_PAIR(finalPair) | A_BOLD);
        for (int j = 0; j < getmaxx(winParent) - 2; ++j)
          waddch(winParent, ' ');
        wmove(winParent, i + 1, 1);
      } else {
        wattron(winParent, COLOR_PAIR(finalPair) | A_DIM);
      }

      std::string display = file.name;
      if (display.length() > (size_t)getmaxx(winParent) - 8)
        display = display.substr(0, getmaxx(winParent) - 11) + "...";

      wprintw(winParent, " %s %s", style.icon, display.c_str());

      if (isCurrent) {
        wattroff(winParent, COLOR_PAIR(finalPair) | A_BOLD);
      } else {
        wattroff(winParent, COLOR_PAIR(finalPair) | A_DIM);
      }
    }
    wnoutrefresh(winParent);
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
    if (isSearching) {
      mvwprintw(winCurrent, 0, 2, " 󰉖 Search Results ");
    } else {
      mvwprintw(winCurrent, 0, 2, " 󰉖 %s ", currentPath.filename().string().c_str());
    }
    wattroff(winCurrent, A_BOLD | COLOR_PAIR(1));

    if (isSearching && currentFiles.empty()) {
      int my = getmaxy(winCurrent);
      int mx = getmaxx(winCurrent);
      wattron(winCurrent, COLOR_PAIR(7) | A_BOLD);
      mvwprintw(winCurrent, my / 2, (mx - 12) / 2, "Searching...");
      wattroff(winCurrent, COLOR_PAIR(7) | A_BOLD);
      wnoutrefresh(winCurrent);
      return;
    }

    if (!multiSelection.empty()) {
      std::string selStr =
          " [ MULTI-SELECT: " + std::to_string(multiSelection.size()) + " ITEMS ] ";

      wattron(winCurrent, COLOR_PAIR(9) | A_BOLD | A_REVERSE);

      mvwprintw(winCurrent, 0, getmaxx(winCurrent) - selStr.length() - 2, "%s", selStr.c_str());

      wattroff(winCurrent, COLOR_PAIR(9) | A_BOLD | A_REVERSE);
    }

    int maxLines = height - 3;
    if (selectedIndex < scrollOffset)
      scrollOffset = selectedIndex;
    if (selectedIndex >= scrollOffset + (size_t)maxLines)
      scrollOffset = selectedIndex - maxLines + 1;

    for (int i = 0; i < maxLines && (scrollOffset + i) < currentFiles.size(); ++i) {
      int idx = scrollOffset + i;
      const auto& file = currentFiles[idx];
      wmove(winCurrent, i + 1, 1);

      bool isSelected = (!focusPinned && idx == (int)selectedIndex);
      bool isMultiSelected = multiSelection.count(file.path);

      FileStyle style = getFileStyle(file.extension, file.is_directory);
      int finalPair = getFinalPair(style.pair, isSelected, false);

      if (isSelected) {
        wattron(winCurrent, COLOR_PAIR(finalPair) | A_BOLD);
        for (int j = 0; j < getmaxx(winCurrent) - 2; ++j)
          waddch(winCurrent, ' ');
        wmove(winCurrent, i + 1, 1);
      } else if (isMultiSelected) {
        wattron(winCurrent, COLOR_PAIR(9) | A_BOLD);
      } else {
        wattron(winCurrent, COLOR_PAIR(finalPair));
      }

      std::string display = file.name;
      if (isSearching) {
        try {
          display = fs::relative(file.path, currentPath).string();
        } catch (...) {
          display = file.name;
        }
      }
      int availWidth = getmaxx(winCurrent) - 16;
      if (display.length() > (size_t)availWidth)
        display = display.substr(0, availWidth - 3) + "...";

      char marker = isMultiSelected ? '*' : ' ';
      wprintw(winCurrent, " %c %s %-s", marker, style.icon, display.c_str());

      std::string sz = formatSize(file.size);
      mvwprintw(winCurrent, i + 1, getmaxx(winCurrent) - sz.length() - 2, "%s", sz.c_str());

      if (isSelected) {
        wattroff(winCurrent, COLOR_PAIR(finalPair) | A_BOLD);
      } else if (isMultiSelected)
        wattroff(winCurrent, COLOR_PAIR(9) | A_BOLD);
      else
        wattroff(winCurrent, COLOR_PAIR(finalPair));
    }
    wnoutrefresh(winCurrent);
  }

  struct FileDetails {
    std::string name;
    std::string absolutePath;
    std::string type;
    bool isSymlink = false;
    std::string symlinkTarget;
    uintmax_t size;
    bool isDir = false;
    std::string permissionsSymbolic;
    std::string permissionsOctal;
    std::string ownerName;
    std::string groupName;
    uid_t uid;
    gid_t gid;
    std::string accessTime;
    std::string modifyTime;
    std::string statusChangeTime;
  };

  FileDetails getFileDetails(const fs::path& p) {
    FileDetails details;
    details.name = p.filename().string();
    try {
      details.absolutePath = fs::absolute(p).string();
    } catch (...) {
      details.absolutePath = p.string();
    }

    struct stat st;
    if (lstat(details.absolutePath.c_str(), &st) != 0) {
      if (lstat(p.string().c_str(), &st) != 0) {
        details.type = "Unknown";
        details.permissionsSymbolic = "???";
        details.permissionsOctal = "???";
        details.ownerName = "unknown";
        details.groupName = "unknown";
        details.size = 0;
        return details;
      }
    }

    details.isDir = S_ISDIR(st.st_mode);
    if (S_ISLNK(st.st_mode)) {
      details.type = "Symbolic Link";
      details.isSymlink = true;
      details.isDir = false;
      try {
        details.symlinkTarget = fs::read_symlink(p).string();
      } catch (...) {
        details.symlinkTarget = "Unknown";
      }
    } else if (S_ISREG(st.st_mode)) {
      details.type = "Regular File";
    } else if (S_ISDIR(st.st_mode)) {
      details.type = "Directory";
    } else if (S_ISCHR(st.st_mode)) {
      details.type = "Character Device";
    } else if (S_ISBLK(st.st_mode)) {
      details.type = "Block Device";
    } else if (S_ISFIFO(st.st_mode)) {
      details.type = "FIFO (Named Pipe)";
    } else if (S_ISSOCK(st.st_mode)) {
      details.type = "Socket";
    } else {
      details.type = "Unknown";
    }

    char perm[11];
    perm[0] = S_ISLNK(st.st_mode) ? 'l' :
              S_ISDIR(st.st_mode) ? 'd' :
              S_ISCHR(st.st_mode) ? 'c' :
              S_ISBLK(st.st_mode) ? 'b' :
              S_ISFIFO(st.st_mode) ? 'p' :
              S_ISSOCK(st.st_mode) ? 's' : '-';
    perm[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
    perm[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
    perm[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
    perm[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
    perm[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
    perm[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
    perm[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
    perm[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
    perm[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
    perm[10] = '\0';

    if (st.st_mode & S_ISUID) perm[3] = (st.st_mode & S_IXUSR) ? 's' : 'S';
    if (st.st_mode & S_ISGID) perm[6] = (st.st_mode & S_IXGRP) ? 's' : 'S';
    if (st.st_mode & S_ISVTX) perm[9] = (st.st_mode & S_IXOTH) ? 't' : 'T';

    details.permissionsSymbolic = perm;

    char oct[8];
    snprintf(oct, sizeof(oct), "0%o", st.st_mode & 07777);
    details.permissionsOctal = oct;

    details.size = st.st_size;
    if (details.isDir) {
      std::lock_guard<std::mutex> lock(cacheMutex);
      auto it = dirSizeCache.find(details.absolutePath);
      if (it != dirSizeCache.end()) {
        details.size = it->second;
      } else {
        it = dirSizeCache.find(p.string());
        if (it != dirSizeCache.end()) {
          details.size = it->second;
        } else {
          bool found = false;
          for (const auto& f : currentFiles) {
            if (f.path == p) {
              details.size = f.size;
              found = true;
              break;
            }
          }
          if (!found) {
            details.size = SIZE_CALCULATING;
          }
        }
      }
    }

    details.uid = st.st_uid;
    struct passwd* pw = getpwuid(st.st_uid);
    if (pw) {
      details.ownerName = pw->pw_name;
    } else {
      details.ownerName = std::to_string(st.st_uid);
    }

    details.gid = st.st_gid;
    struct group* gr = getgrgid(st.st_gid);
    if (gr) {
      details.groupName = gr->gr_name;
    } else {
      details.groupName = std::to_string(st.st_gid);
    }

    auto formatTime = [](time_t t) -> std::string {
      struct tm* ltime = std::localtime(&t);
      if (!ltime) return "Unknown";
      char buffer[64];
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %I:%M:%S %p", ltime);
      return std::string(buffer);
    };

    details.accessTime = formatTime(st.st_atime);
    details.modifyTime = formatTime(st.st_mtime);
    details.statusChangeTime = formatTime(st.st_ctime);

    return details;
  }

  void showFileDetails() {
    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) {
      setStatus("No file selected");
      return;
    }
    const auto& file = currentFiles[selectedIndex];
    FileDetails details = getFileDetails(file.path);

    int h = details.isSymlink ? 17 : 16;
    int w = 70;
    if (w > width - 4) w = width - 4;
    if (h > height - 2) h = height - 2;
    if (w < 10) w = 10;
    if (h < 5) h = 5;

    int startY = (height - h) / 2;
    int startX = (width - w) / 2;
    if (startY < 0) startY = 0;
    if (startX < 0) startX = 0;

    WINDOW* detWin = newwin(h, w, startY, startX);
    if (!detWin) return;

    wattron(detWin, COLOR_PAIR(6) | A_BOLD);
    drawRoundedBox(detWin);
    wattroff(detWin, COLOR_PAIR(6) | A_BOLD);

    // Title
    wattron(detWin, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(detWin, 1, 2, "󰋽 File Information");
    wattroff(detWin, COLOR_PAIR(1) | A_BOLD);

    auto printField = [&](int row, const std::string& label, const std::string& val, int valColorPair) {
      mvwprintw(detWin, row, 2, "%-15s", label.c_str());
      int maxValW = w - 20;
      if (maxValW < 5) maxValW = 5;
      std::string showVal = val;
      if ((int)showVal.length() > maxValW) {
        int subLen = maxValW - 3;
        if (subLen < 1) subLen = 1;
        showVal = showVal.substr(0, subLen) + "...";
      }
      wattron(detWin, COLOR_PAIR(valColorPair));
      mvwprintw(detWin, row, 18, "%s", showVal.c_str());
      wattroff(detWin, COLOR_PAIR(valColorPair));
    };

    int row = 3;
    printField(row++, "Name:", details.name, details.isDir ? 1 : 2);
    printField(row++, "Path:", details.absolutePath, 2);

    if (details.isSymlink) {
      printField(row++, "Target:", details.symlinkTarget, 4);
    }

    printField(row++, "Type:", details.type, 2);

    std::string sizeStr;
    if (details.size == SIZE_CALCULATING) {
      sizeStr = "Calculating...";
    } else {
      sizeStr = formatSize(details.size) + " (" + std::to_string(details.size) + " bytes)";
    }
    printField(row++, "Size:", sizeStr, 2);

    std::string permStr = details.permissionsSymbolic + " (" + details.permissionsOctal + ")";
    printField(row++, "Permissions:", permStr, 5);

    std::string ownerStr = details.ownerName + " (" + std::to_string(details.uid) + ")";
    printField(row++, "Owner:", ownerStr, 2);

    std::string groupStr = details.groupName + " (" + std::to_string(details.gid) + ")";
    printField(row++, "Group:", groupStr, 2);

    printField(row++, "Accessed:", details.accessTime, 2);
    printField(row++, "Modified:", details.modifyTime, 2);
    printField(row++, "Changed:", details.statusChangeTime, 2);

    wattron(detWin, A_DIM);
    mvwprintw(detWin, h - 2, 2, "Press any key to close...");
    wattroff(detWin, A_DIM);

    wrefresh(detWin);

    timeout(-1);
    getch();
    timeout(50);

    delwin(detWin);
  }

  void drawHelpOverlay() {
    int h = 24;
    int w = 60;

    int startY = (height - h) / 2;
    int startX = (width - w) / 2;

    WINDOW* helpWin = newwin(h, w, startY, startX);

    wattron(helpWin, COLOR_PAIR(6) | A_BOLD);
    drawRoundedBox(helpWin);
    wattroff(helpWin, COLOR_PAIR(6) | A_BOLD);

    wattron(helpWin, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(helpWin, 1, 2, "󰘳 Fyzenor Keybindings");
    wattroff(helpWin, COLOR_PAIR(1) | A_BOLD);

    mvwprintw(helpWin, 3, 2, "j / k        → Navigate");
    mvwprintw(helpWin, 4, 2, "h / l        → Back / Open");
    mvwprintw(helpWin, 5, 2, "Space / v    → Select");
    mvwprintw(helpWin, 6, 2, "a            → Select All");
    mvwprintw(helpWin, 7, 2, "Esc          → Clear Selection");
    mvwprintw(helpWin, 8, 2, "y            → Copy");
    mvwprintw(helpWin, 9, 2, "x            → Cut");
    mvwprintw(helpWin, 10, 2, "p            → Paste");
    mvwprintw(helpWin, 11, 2, "d            → Delete");
    mvwprintw(helpWin, 12, 2, "r            → Rename");
    mvwprintw(helpWin, 13, 2, "n / N        → New File / Folder");
    mvwprintw(helpWin, 14, 2, "z            → Zip");
    mvwprintw(helpWin, 15, 2, ".            → Toggle Hidden");
    mvwprintw(helpWin, 16, 2, "s            → Toggle Sorting");
    mvwprintw(helpWin, 17, 2, "P            → Pin Directory");
    mvwprintw(helpWin, 18, 2, "F5 / Ctrl+R  → Refresh Directory");
    mvwprintw(helpWin, 19, 2, "/            → Search (ripgrep)");
    mvwprintw(helpWin, 20, 2, "i            → Show File Details");
    mvwprintw(helpWin, 21, 2, "?            → Show Help");

    wattron(helpWin, A_DIM);
    mvwprintw(helpWin, h - 2, 2, "Press any key to close...");
    wattroff(helpWin, A_DIM);

    wrefresh(helpWin);

    timeout(-1);
    getch();
    timeout(50);

    delwin(helpWin);
  }

  void drawPreview() {
    pendingDirectRenderType = PreviewType::NONE;
    if (lastWasDirectRender)
      clearDirectRender();
    werase(winPreview);
    wattron(winPreview, COLOR_PAIR(6));
    drawRoundedBox(winPreview);
    wattroff(winPreview, COLOR_PAIR(6));

    wattron(winPreview, A_BOLD | COLOR_PAIR(5));
    mvwprintw(winPreview, 0, 2, " 󰮫 Preview ");
    wattroff(winPreview, A_BOLD | COLOR_PAIR(5));

    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) {
      wnoutrefresh(winPreview);
      return;
    }
    const auto& file = currentFiles[selectedIndex];
    int maxW = getmaxx(winPreview) - 4;
    int maxH = getmaxy(winPreview) - 2;

    // Header info with better colors
    wattron(winPreview, A_BOLD | COLOR_PAIR(1));
    mvwprintw(winPreview, 1, 2, " %s ", file.name.c_str());
    wattroff(winPreview, A_BOLD | COLOR_PAIR(1));

    wattron(winPreview, A_DIM);
    mvwprintw(winPreview, 2, 2, " Size: %s", formatSize(file.size).c_str());
    mvwprintw(winPreview, 3, 2, " Type: %s",
              file.is_directory ? "Directory"
                                : (file.extension.empty() ? "File" : file.extension.c_str()));
    mvwprintw(winPreview, 4, 2, " Modified: %s", getFileModifiedTime(file.path).c_str());
    wattroff(winPreview, A_DIM);

    wattron(winPreview, COLOR_PAIR(6));
    for (int i = 1; i < getmaxx(winPreview) - 1; ++i)
      mvwaddstr(winPreview, 5, i, "─");
    wattroff(winPreview, COLOR_PAIR(6));

    bool isVid = VIDEO_EXTS.count(file.extension);
    bool isImg = IMAGE_EXTS.count(file.extension);
    bool isCode = isCodeFile(file.extension);

    bool isPdf = (file.extension == ".pdf");
    bool isDoc = (file.extension == ".doc" || file.extension == ".docx");
    bool isXls = (file.extension == ".xls" || file.extension == ".xlsx");
    bool isPpt = (file.extension == ".ppt" || file.extension == ".pptx");

    if (file.is_directory) {
      wattron(winPreview, COLOR_PAIR(1) | A_BOLD);
      mvwprintw(winPreview, 7, 2, "󰉖 Content:");
      wattroff(winPreview, COLOR_PAIR(1) | A_BOLD);
      try {
        int line = 8;
        for (const auto& entry : fs::directory_iterator(file.path)) {
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;
          if (line >= height - 3)
            break;
          std::string subName = entry.path().filename().string();
          if (subName.length() > (size_t)maxW)
            subName = subName.substr(0, maxW - 3) + "...";

          std::string ext = entry.path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          FileStyle s = getFileStyle(ext, fs::is_directory(entry));

          wattron(winPreview, COLOR_PAIR(s.pair));
          mvwprintw(winPreview, line++, 4, "%s %s", s.icon, subName.c_str());
          wattroff(winPreview, COLOR_PAIR(s.pair));
        }
      } catch (...) {
      }
      wnoutrefresh(winPreview);
    } else if (isPdf || isDoc || isXls || isPpt) {
      wattron(winPreview, COLOR_PAIR(8));
      if (isPdf)
        mvwprintw(winPreview, 7, 2, " [PDF File - No Preview] ");
      else if (isDoc)
        mvwprintw(winPreview, 7, 2, " [Word Document - No Preview] ");
      else if (isXls)
        mvwprintw(winPreview, 7, 2, " [Excel Spreadsheet - No Preview] ");
      else if (isPpt)
        mvwprintw(winPreview, 7, 2, " [PowerPoint Presentation - No Preview] ");
      wattroff(winPreview, COLOR_PAIR(8));
      wnoutrefresh(winPreview);
    } else if (isVid || isImg || isCode) {
      bool match = false;
      {
        std::lock_guard<std::mutex> lock(previewMutex);
        if (cachedPath == file.path.string())
          match = true;
      }
      if (match) {
        if (isCode)
          drawCachedTextPreview();
        else
          pendingDirectRenderType = PreviewType::IMAGE;
      } else if (requestedPath != file.path.string()) {
        wattron(winPreview, A_ITALIC | A_DIM);
        mvwprintw(winPreview, 7, 4, "Generating preview...");
        wattroff(winPreview, A_ITALIC | A_DIM);
        PreviewType type = isCode ? PreviewType::TEXT : PreviewType::IMAGE;
        startAsyncPreview(file.path.string(), type, maxH - 9, maxW);
      }
    } else {
      if (is_binary_file(file.path.string())) {
        wattron(winPreview, COLOR_PAIR(8));
        mvwprintw(winPreview, 7, 2, " [Binary File - No Preview] ");
        wattroff(winPreview, COLOR_PAIR(8));
      } else {
        std::ifstream f(file.path);
        if (f.is_open()) {
          std::string lineStr;
          int line = 7;
          while (std::getline(f, lineStr) && line < height - 3) {
            std::replace(lineStr.begin(), lineStr.end(), '\t', ' ');
            for (size_t i = 0; i < lineStr.length(); i += maxW) {
              if (line >= height - 3)
                break;
              mvwprintw(winPreview, line++, 2, "%s", lineStr.substr(i, maxW).c_str());
            }
          }
        }
      }
    }
    wnoutrefresh(winPreview);
  }

  void flushScreen() {
    wnoutrefresh(stdscr);
    doupdate();

    if (pendingDirectRenderType != PreviewType::NONE) {
      drawFromCache(pendingDirectRenderType);
      pendingDirectRenderType = PreviewType::NONE;
    }
  }

  void openFile() {
    if (currentFiles.empty())
      return;
    const auto& file = currentFiles[selectedIndex];
    if (file.is_directory) {
      clearDirectRender();
      currentPath = file.path;
      selectedIndex = 0;
      scrollOffset = 0;
      isSearching = false;
      reloadAll();
    } else {
      clearDirectRender();
      def_prog_mode();
      endwin();
      std::string cmd;
      if (VIDEO_EXTS.count(file.extension) || AUDIO_EXTS.count(file.extension)) {
        cmd = "mpv \"" + file.path.string() + "\" 2> /dev/null";
      } else if (isCodeFile(file.extension)) {
        const char* editor = getenv("EDITOR");
        if (!editor)
          editor = getenv("VISUAL");
        if (!editor) {
          if (system("which nvim > /dev/null 2>&1") == 0)
            editor = "nvim";
          else if (system("which nano > /dev/null 2>&1") == 0)
            editor = "nano";
          else
            editor = "vi";
        }
        cmd = std::string(editor) + " \"" + file.path.string() + "\"";
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
    if (isSearching) {
      cancelSearch();
      isSearching = false;
      reloadAll();
      selectedIndex = 0;
      scrollOffset = 0;
      setStatus("Search cleared");
      return;
    }
    if (currentPath.has_parent_path() && currentPath != currentPath.parent_path()) {
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

  void handleRefresh() {
    {
      std::lock_guard<std::mutex> lock(cacheMutex);
      dirSizeCache.clear();
    }
    {
      std::lock_guard<std::mutex> lock(previewMutex);
      requestID++;
      cachedPath = "";
      requestedPath = "";
      cachedTextLines.clear();
      cachedBase64 = "";
      sessionImageCache.clear();
    }
    reloadAll();
    setStatus("Refreshed");
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
              for (auto& f : currentFiles) {
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

      // Check for async search updates
      {
        std::lock_guard<std::mutex> lock(searchResultMutex);
        if (hasPendingSearchResults) {
          isSearching = true;

          // Remember previously selected path to restore cursor position
          fs::path prevSelectedPath;
          if (!currentFiles.empty() && selectedIndex < currentFiles.size()) {
            prevSelectedPath = currentFiles[selectedIndex].path;
          }

          currentFiles = pendingSearchResults;
          sortList(currentFiles);

          if (!prevSelectedPath.empty()) {
            bool found = false;
            for (size_t idx = 0; idx < currentFiles.size(); ++idx) {
              if (currentFiles[idx].path == prevSelectedPath) {
                selectedIndex = idx;
                found = true;
                break;
              }
            }
            if (!found) {
              if (selectedIndex >= currentFiles.size()) {
                selectedIndex = currentFiles.empty() ? 0 : currentFiles.size() - 1;
              }
            }
          } else {
            selectedIndex = 0;
            scrollOffset = 0;
          }

          setStatus(pendingSearchStatus);
          hasPendingSearchResults = false;
          needsRedraw = true;
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

        attron(COLOR_PAIR(6) | A_BOLD);
        printw(" Fyzenor ");
        attroff(COLOR_PAIR(6) | A_BOLD);

        if (!multiSelection.empty()) {
          attron(COLOR_PAIR(9) | A_BOLD);
          printw(" [%zu selected] ", multiSelection.size());
          attroff(COLOR_PAIR(9) | A_BOLD);
        }

        attron(A_DIM);
        printw(" %s", currentPath.string().c_str());
        attroff(A_DIM);

        drawStatusToast();

        flushScreen();
        needsRedraw = false;
      }

      int ch = getch();
      if (ch == ERR) {
        bool statusTimedOut = false;
        if (!statusMessage.empty()) {
          auto now = std::chrono::steady_clock::now();
          if (std::chrono::duration_cast<std::chrono::milliseconds>(now - statusTime).count() > 1800) {
            statusMessage = "";
            statusTimedOut = true;
          }
        }
        if (imageReady || searchReady || statusTimedOut) {
          needsRedraw = true;
          imageReady = false;
          searchReady = false;
        }
        continue;
      }
      needsRedraw = true;
      if (ch != ERR) {
        statusMessage = "";
      }

      if (ch == 'q') {
        return;
      }
      if (ch == KEY_RESIZE) {
        clearDirectRender();
        updateLayout();
        continue;
      }
      if (ch == 18 || ch == KEY_F(5)) { // Ctrl+R or F5
        handleRefresh();
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
        case '/':
          handleSearch();
          break;
        case 'i':
          showFileDetails();
          break;
        case '?':
          drawHelpOverlay();
          break;
        }
      }
    }
  }
};

const std::string VERSION = "1.4.0";

int main(int argc, char* argv[]) {
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-v" || arg == "--version") {
        std::cout << "Fyzenor version " << VERSION << std::endl;
        return 0;
      } else if (arg == "-h" || arg == "--help") {
        std::cout << "Fyzenor - The Blazing Fast, Modern C++ Terminal File Manager" << std::endl;
        std::cout << "Usage: fyzenor [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -v, --version         Show version information" << std::endl;
        std::cout << "  -h, --help            Show this help message" << std::endl;
        return 0;
      }
    }
  }
  FileManager fm;
  fm.run();
  return 0;
}
