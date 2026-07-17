#include "utils.h"
#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <ncurses.h>
#include <unistd.h>
#include <sys/stat.h>

// Definition of configuration constants
std::set<std::string> VIDEO_EXTS = {
    ".mp4", ".mkv", ".avi", ".mov", ".flv", ".wmv", ".webm", ".m4v", ".mpg", ".mpeg"
};
std::set<std::string> IMAGE_EXTS = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".tiff", ".ico", ".psd", ".ai"
};
std::set<std::string> FRONTEND_EXTS = {
    ".js", ".jsx", ".ts", ".tsx", ".css", ".scss", ".sass", ".less", ".styl",
    ".vue", ".html", ".svelte", ".htm", ".astro", ".mjx", ".dart", ".swift"
};
std::set<std::string> SCRIPTS_EXTS = {
    ".sh", ".bash", ".zsh", ".fish", ".ksh", ".command", ".pl", ".pm", ".t", ".awk",
    ".ps1", ".psm1", ".bat", ".cmd", ".vbs", ".wsf"
};
std::set<std::string> CONFIG_EXTS = {
    ".json", ".json5", ".jsonc", ".xml", ".xsd", ".xsl", ".gpx", ".yaml", ".yml",
    ".toml", ".ini", ".conf", ".cfg", ".prefs", ".properties", ".lock",
    ".env", ".dockerfile", ".gitignore", ".gitconfig", ".gitattributes", ".gitmodules"
};
std::set<std::string> DOCUMENTATION_EXTS = {
    ".md", ".markdown", ".txt", ".text", ".log", ".pdf",
    ".doc", ".docx", ".odt", ".rtf", ".ppt", ".pptx", ".odp", ".xls", ".xlsx", ".ods", ".csv"
};
std::set<std::string> CORE_EXTS = {
    ".py", ".pyw", ".ipynb", ".pyc", ".pyd", ".rb", ".ru", ".gemspec", ".php",
    ".cpp", ".cxx", ".cc", ".hpp", ".hxx", ".ixx", ".c", ".h", ".rs",
    ".java", ".class", ".jar", ".war", ".go", ".lua", ".sql", ".db", ".sqlite",
    ".sqlite3", ".db3", ".mdb", ".accdb", ".cmake", ".make", ".diff", ".patch",
    ".kt", ".kts", ".cs", ".csx", ".scala", ".sc", ".hs", ".lhs",
    ".clj", ".cljs", ".cljc", ".edn", ".r", ".rmd", ".jl", ".fs", ".fsi", ".fsx"
};
std::set<std::string> FONT_EXTS = {".woff", ".woff2", ".ttf", ".eot", ".otf"};
std::set<std::string> AUDIO_EXTS = {
    ".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".wma", ".opus", ".mid", ".midi"
};
std::set<std::string> ARCHIVE_EXTS = {
    ".zip", ".tar", ".gz", ".tgz", ".7z", ".rar", ".xz", ".bz2", ".tbz2", ".lzma", ".cab"
};

bool configShowHidden = false;
std::string configSortMode = "name";
double configParentWidth = 0.18;
double configCurrentWidth = 0.32;
bool configHidePreview = false;
bool configHideParent = false;

std::string g_icon_dir = " ";
std::string g_icon_video = " ";
std::string g_icon_image = " ";
std::string g_icon_core = " ";
std::string g_icon_frontend = "󰖟 ";
std::string g_icon_config = " ";
std::string g_icon_script = " ";
std::string g_icon_docs = " ";
std::string g_icon_font = " ";
std::string g_icon_file = " ";
std::string g_icon_music = " ";
std::string g_icon_pin = " ";
std::string g_icon_zip = "󰿺 ";
std::string g_icon_link = "󰌹 ";

const char* ICON_DIR = g_icon_dir.c_str();
const char* ICON_VIDEO = g_icon_video.c_str();
const char* ICON_IMAGE = g_icon_image.c_str();
const char* ICON_CORE = g_icon_core.c_str();
const char* ICON_FRONTEND = g_icon_frontend.c_str();
const char* ICON_CONFIG = g_icon_config.c_str();
const char* ICON_SCRIPT = g_icon_script.c_str();
const char* ICON_DOCS = g_icon_docs.c_str();
const char* ICON_FONT = g_icon_font.c_str();
const char* ICON_FILE = g_icon_file.c_str();
const char* ICON_MUSIC = g_icon_music.c_str();
const char* ICON_PIN = g_icon_pin.c_str();
const char* ICON_ZIP = g_icon_zip.c_str();
const char* ICON_LINK = g_icon_link.c_str();

const std::string PREVIEW_TEMP = "/tmp/fm_preview_thumb.png";
const uintmax_t SIZE_CALCULATING = UINTMAX_MAX;

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

size_t utf8_length(const std::string& str) {
  size_t len = 0;
  size_t i = 0;
  while (i < str.length()) {
    unsigned char c = str[i];
    size_t char_len = 1;
    if (c >= 0xf0)
      char_len = 4;
    else if (c >= 0xe0)
      char_len = 3;
    else if (c >= 0xc0)
      char_len = 2;
    i += char_len;
    len++;
  }
  return len;
}

std::string utf8_safe_truncate(const std::string& str, size_t max_cols) {
  size_t cols = 0;
  size_t bytes = 0;
  while (bytes < str.length() && cols < max_cols) {
    unsigned char c = str[bytes];
    size_t char_len = 1;
    if (c >= 0xf0)
      char_len = 4;
    else if (c >= 0xe0)
      char_len = 3;
    else if (c >= 0xc0)
      char_len = 2;
    if (bytes + char_len > str.length()) {
      break;
    }
    cols++;
    bytes += char_len;
  }
  if (bytes < str.length()) {
    return str.substr(0, bytes) + "...";
  }
  return str;
}

std::string utf8_safe_truncate_left(const std::string& str, size_t max_cols) {
  size_t total_cols = utf8_length(str);
  if (total_cols <= max_cols) {
    return str;
  }
  size_t cols = 0;
  size_t bytes = str.length();
  while (bytes > 0 && cols < max_cols) {
    size_t char_len = 1;
    while (bytes - char_len > 0) {
      unsigned char c = str[bytes - char_len];
      if ((c & 0xC0) != 0x80) {
        break;
      }
      char_len++;
    }
    bytes -= char_len;
    cols++;
  }
  return "..." + str.substr(bytes);
}

FileStyle getFileStyle(const std::string& name, const std::string& ext, bool isDir, bool isEmptyDir) {
  std::string lowerName = name;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

  if (isDir) {
    if (lowerName == ".git" || lowerName == ".github" || lowerName == "git")
      return {1, " "};
    if (lowerName == "node_modules")
      return {1, " "};
    if (lowerName == "src" || lowerName == "source" || lowerName == "sources" || lowerName == "code")
      return {1, "󱧼 "};
    if (lowerName == "build" || lowerName == "bin" || lowerName == "target" ||
        lowerName == "dist" || lowerName == "out" || lowerName == "release" || lowerName == "debug")
      return {1, "󱂀 "};
    if (lowerName == "include" || lowerName == "headers" || lowerName == "include_dir" || lowerName == "inc")
      return {1, "󰙶 "};
    if (lowerName == "test" || lowerName == "tests" || lowerName == "spec" || lowerName == "specs" ||
        lowerName == "__tests__" || lowerName == "testing")
      return {1, "󰙨 "};
    if (lowerName == "doc" || lowerName == "docs" || lowerName == "documentation" || lowerName == "manual")
      return {1, "󰈙 "};
    if (lowerName == "img" || lowerName == "images" || lowerName == "pictures" || lowerName == "pics" ||
        lowerName == "photos" || lowerName == "assets" || lowerName == "static" || lowerName == "public")
      return {1, "󰥶 "};
    if (lowerName == "music" || lowerName == "songs" || lowerName == "audio" || lowerName == "sounds")
      return {1, "󰎆 "};
    if (lowerName == "video" || lowerName == "videos" || lowerName == "movies" || lowerName == "clips")
      return {1, "󰎁 "};
    if (lowerName == "downloads" || lowerName == "download")
      return {1, "󰇚 "};
    if (lowerName == "desktop")
      return {1, "󰪧 "};
    if (lowerName == "documents" || lowerName == "document")
      return {1, "󱧬 "};
    if (lowerName == ".vscode" || lowerName == ".idea" || lowerName == ".settings")
      return {1, " "};
    if (lowerName == "config" || lowerName == ".config" || lowerName == "settings" || lowerName == "preferences")
      return {1, " "};
    if (lowerName == "lib" || lowerName == "libs" || lowerName == "library" || lowerName == "libraries")
      return {1, "󰓏 "};
    if (lowerName == "temp" || lowerName == "tmp" || lowerName == "cache" || lowerName == ".cache")
      return {1, "󰏕 "};
    if (lowerName == "db" || lowerName == "database" || lowerName == "sql" || lowerName == "data")
      return {1, " "};
    if (lowerName == "logs" || lowerName == "log")
      return {1, "󰘔 "};
    if (lowerName == "backup" || lowerName == "backups" || lowerName == "archive" || lowerName == "archives")
      return {1, "󰁯 "};
    if (lowerName == ".ssh" || lowerName == "ssh" || lowerName == "keys" || lowerName == ".gnupg")
      return {1, "󰣀 "};
    if (lowerName == "mail" || lowerName == "email" || lowerName == "mails")
      return {1, "󰇰 "};
    if (lowerName == "games" || lowerName == "game")
      return {1, "󰊖 "};
    if (lowerName == "apps" || lowerName == "applications" || lowerName == "programs")
      return {1, "󰀻 "};
    if (lowerName == "theme" || lowerName == "themes" || lowerName == "styles" || lowerName == "css")
      return {1, "󰔎 "};

    return {1, isEmptyDir ? " " : ICON_DIR};
  }

  if (lowerName == "cmakelists.txt")
    return {16, " "};
  if (lowerName == "makefile" || lowerName == "gnumakefile" || lowerName == "makefile.win" ||
      lowerName == "makefile.am" || lowerName == "makefile.in")
    return {16, " "};
  if (lowerName == "license" || lowerName == "license.txt" || lowerName == "copying" ||
      lowerName == "license.md" || lowerName == "copyleft")
    return {26, "󰘥 "};
  if (lowerName == "readme" || lowerName == "readme.md" || lowerName == "readme.txt" ||
      lowerName == "changelog" || lowerName == "changelog.md" || lowerName == "contributing.md")
    return {27, "󰂺 "};
  if (lowerName == "package.json")
    return {24, " "};
  if (lowerName == "package-lock.json" || lowerName == "yarn.lock" || lowerName == "pnpm-lock.yaml")
    return {25, " "};
  if (lowerName == "cargo.toml")
    return {16, " "};
  if (lowerName == "cargo.lock")
    return {25, " "};
  if (lowerName == "go.mod" || lowerName == "go.sum" || lowerName == "go.work")
    return {16, " "};
  if (lowerName == "composer.json" || lowerName == "composer.lock")
    return {16, " "};
  if (lowerName == "gemfile" || lowerName == "gemfile.lock")
    return {16, " "};
  if (lowerName == "dockerfile" || lowerName == ".dockerignore")
    return {25, "󰡨 "};
  if (lowerName == "docker-compose.yml" || lowerName == "docker-compose.yaml")
    return {25, "󰡨 "};
  if (lowerName == ".gitignore" || lowerName == ".gitconfig" || lowerName == ".gitattributes" || lowerName == ".gitmodules")
    return {25, " "};
  if (lowerName == ".env" || lowerName == ".env.local" || lowerName == ".env.development" ||
      lowerName == ".env.test" || lowerName == ".env.production")
    return {25, " "};
  if (lowerName == "webpack.config.js" || lowerName == "webpack.config.ts")
    return {24, "󰘦 "};
  if (lowerName == "tsconfig.json")
    return {25, " "};
  if (lowerName == "babel.config.js" || lowerName == "babel.config.json")
    return {24, " "};
  if (lowerName == "vite.config.js" || lowerName == "vite.config.ts")
    return {24, "⚡ "};
  if (lowerName == "tailwind.config.js" || lowerName == "tailwind.config.ts")
    return {24, "󱏿 "};
  if (lowerName == "eslint.config.js" || lowerName == ".eslintrc" || lowerName == ".eslintrc.js" ||
      lowerName == ".eslintrc.json" || lowerName == ".eslintignore")
    return {25, " "};

  if (ext == ".py" || ext == ".pyw" || ext == ".ipynb" || ext == ".pyc" || ext == ".pyd")
    return {16, " "};
  if (ext == ".rs")
    return {16, " "};
  if (ext == ".go")
    return {16, " "};
  if (ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".hpp" || ext == ".hxx" || ext == ".ixx")
    return {16, " "};
  if (ext == ".c" || ext == ".h")
    return {16, " "};
  if (ext == ".java" || ext == ".class" || ext == ".jar" || ext == ".war")
    return {16, " "};
  if (ext == ".js" || ext == ".mjs" || ext == ".cjs")
    return {24, " "};
  if (ext == ".ts" || ext == ".mts" || ext == ".cts")
    return {24, " "};
  if (ext == ".tsx")
    return {24, " "};
  if (ext == ".jsx")
    return {24, " "};
  if (ext == ".vue")
    return {24, " "};
  if (ext == ".svelte")
    return {24, " "};
  if (ext == ".php" || ext == ".phtml" || ext == ".php4" || ext == ".php5")
    return {16, " "};
  if (ext == ".rb" || ext == ".ru" || ext == ".gemspec")
    return {16, " "};
  if (ext == ".lua")
    return {16, " "};
  if (ext == ".pl" || ext == ".pm" || ext == ".t")
    return {26, " "};
  if (ext == ".hs" || ext == ".lhs")
    return {16, " "};
  if (ext == ".scala" || ext == ".sc")
    return {16, " "};
  if (ext == ".clj" || ext == ".cljs" || ext == ".cljc" || ext == ".edn")
    return {16, " "};
  if (ext == ".r" || ext == ".rmd")
    return {16, " "};
  if (ext == ".jl")
    return {16, " "};
  if (ext == ".dart")
    return {24, " "};
  if (ext == ".swift")
    return {16, " "};
  if (ext == ".kt" || ext == ".kts")
    return {16, " "};
  if (ext == ".cs" || ext == ".csx")
    return {16, " "};
  if (ext == ".fs" || ext == ".fsi" || ext == ".fsx")
    return {16, " "};
  if (ext == ".sql" || ext == ".db" || ext == ".sqlite" || ext == ".sqlite3" || ext == ".db3" || ext == ".mdb" || ext == ".accdb")
    return {25, " "};
  if (ext == ".html" || ext == ".htm")
    return {24, " "};
  if (ext == ".css")
    return {24, " "};
  if (ext == ".scss" || ext == ".sass")
    return {24, " "};
  if (ext == ".less")
    return {24, " "};
  if (ext == ".styl")
    return {24, " "};
  if (ext == ".sh" || ext == ".bash" || ext == ".zsh" || ext == ".fish" || ext == ".ksh" || ext == ".command")
    return {26, " "};
  if (ext == ".ps1" || ext == ".psm1" || ext == ".bat" || ext == ".cmd" || ext == ".vbs" || ext == ".wsf")
    return {26, " "};
  if (ext == ".json" || ext == ".json5" || ext == ".jsonc")
    return {25, " "};
  if (ext == ".yaml" || ext == ".yml")
    return {25, " "};
  if (ext == ".toml" || ext == ".ini" || ext == ".conf" || ext == ".cfg" || ext == ".prefs" || ext == ".properties")
    return {25, " "};
  if (ext == ".xml" || ext == ".xsd" || ext == ".xsl" || ext == ".gpx")
    return {25, "󰗀 "};
  if (ext == ".md" || ext == ".markdown")
    return {27, " "};
  if (ext == ".txt" || ext == ".text" || ext == ".log")
    return {27, "󰈙 "};
  if (ext == ".pdf")
    return {27, "󰈦 "};
  if (ext == ".doc" || ext == ".docx" || ext == ".odt" || ext == ".rtf")
    return {27, "󰈬 "};
  if (ext == ".xls" || ext == ".xlsx" || ext == ".ods" || ext == ".csv")
    return {27, "󰈛 "};
  if (ext == ".ppt" || ext == ".pptx" || ext == ".odp")
    return {27, "󰈧 "};

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
  if (path.string().find("/gvfs/") != std::string::npos) {
    return "Unknown";
  }
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

std::string escapeShellArg(const std::string& str) {
  std::string result = "'";
  for (char c : str) {
    if (c == '\'') {
      result += "'\\''";
    } else {
      result += c;
    }
  }
  result += "'";
  return result;
}

bool fuzzyMatch(const std::string& str, const std::string& query) {
  if (query.empty())
    return true;
  size_t queryIdx = 0;
  for (char c : str) {
    if (tolower(c) == tolower(query[queryIdx])) {
      queryIdx++;
      if (queryIdx == query.length()) {
        return true;
      }
    }
  }
  return false;
}

bool isCommandAvailable(const std::string& cmd) {
  static std::unordered_map<std::string, bool> cache;
  static std::mutex cacheMutex;

  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(cmd);
  if (it != cache.end()) {
    return it->second;
  }

  std::string checkCmd = "which " + cmd + " > /dev/null 2>&1";
  int res = std::system(checkCmd.c_str());
  bool available = (res == 0);
  cache[cmd] = available;
  return available;
}

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
    fs::path colorFile = configDir / "theme.toml";
    if (fs::exists(colorFile)) {
      std::ifstream f(colorFile);
      std::string line;
      std::string section = "";
      while (std::getline(f, line)) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);

        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
          section = line.substr(1, line.length() - 2);
          std::transform(section.begin(), section.end(), section.begin(), ::tolower);
          continue;
        }

        size_t pos = line.find('=');
        if (pos != std::string::npos) {
          std::string key = line.substr(0, pos);
          std::string val = line.substr(pos + 1);

          auto k_first = key.find_first_not_of(" \t");
          if (k_first != std::string::npos) {
            auto k_last = key.find_last_not_of(" \t");
            key = key.substr(k_first, k_last - k_first + 1);
          }
          auto v_first = val.find_first_not_of(" \t");
          if (v_first != std::string::npos) {
            auto v_last = val.find_last_not_of(" \t");
            val = val.substr(v_first, v_last - v_first + 1);
          }

          if (!val.empty() && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.length() - 2);
          } else if (!val.empty() && val.front() == '\'' && val.back() == '\'') {
            val = val.substr(1, val.length() - 2);
          }

          if (section == "colors") {
            std::transform(key.begin(), key.end(), key.begin(), ::toupper);
            std::replace(key.begin(), key.end(), '-', '_');
            if (!val.empty() && val[0] == '#') {
              colors[key] = val;
            }
          }
        }
      }
    } else {
      std::ofstream f(colorFile);
      f << "# Fyzenor Theme Configuration File\n"
        << "# Catppuccin Mocha colors\n\n"
        << "[colors]\n"
        << "dir = \"#89b4fa\"\n"
        << "file = \"#cdd6f4\"\n"
        << "sel_bg = \"#585b70\"\n"
        << "media = \"#f9e2af\"\n"
        << "image = \"#f5c2e7\"\n"
        << "border = \"#b4befe\"\n"
        << "success = \"#a6e3a1\"\n"
        << "error = \"#f38ba8\"\n"
        << "multi = \"#f5e0dc\"\n"
        << "pin_bg = \"#cba6f7\"\n"
        << "pin_border = \"#89b4fa\"\n"
        << "sec_sel_bg = \"#313244\"\n"
        << "core = \"#a6e3a1\"\n"
        << "archive = \"#eba0ac\"\n"
        << "frontend = \"#fab387\"\n"
        << "config = \"#94e2d5\"\n"
        << "script = \"#f9e2af\"\n"
        << "docs = \"#f2cdcd\"\n"
        << "font = \"#cba6f7\"\n";
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
    init_pair(16, COLOR_GREEN, -1);
    init_pair(17, COLOR_RED, -1);
    init_pair(24, COLOR_YELLOW, -1);
    init_pair(25, COLOR_WHITE, -1);
    init_pair(26, COLOR_CYAN, -1);
    init_pair(27, COLOR_RED, -1);
    init_pair(28, COLOR_MAGENTA, -1);

    short selBg = COLOR_BLUE;
    short secSelBg = COLOR_CYAN;

    std::vector<int> bases = {1, 2, 4, 5, 16, 17, 24, 25, 26, 27, 28};
    for (int base : bases) {
      short fg = COLOR_WHITE;
      if (base == 1)
        fg = COLOR_CYAN;
      else if (base == 4)
        fg = COLOR_YELLOW;
      else if (base == 5)
        fg = COLOR_MAGENTA;
      else if (base == 16)
        fg = COLOR_GREEN;
      else if (base == 17)
        fg = COLOR_RED;
      else if (base == 24)
        fg = COLOR_YELLOW;
      else if (base == 25)
        fg = COLOR_WHITE;
      else if (base == 26)
        fg = COLOR_CYAN;
      else if (base == 27)
        fg = COLOR_RED;
      else if (base == 28)
        fg = COLOR_MAGENTA;

      if (base + 40 < COLOR_PAIRS)
        init_pair(base + 40, fg, selBg);
      if (base + 80 < COLOR_PAIRS)
        init_pair(base + 80, fg, secSelBg);
    }

    init_pair(6, COLOR_BLUE, -1);
    init_pair(7, COLOR_GREEN, -1);
    init_pair(8, COLOR_RED, -1);
    init_pair(9, COLOR_YELLOW, -1);
    init_pair(10, COLOR_WHITE, COLOR_BLUE);
  }
}

void loadConfiguration() {
  const char* home = getenv("HOME");
  if (!home) return;
  fs::path confDir = fs::path(home) / ".config/fyzenor";
  fs::path confPath = confDir / "config.toml";

  try {
    if (!fs::exists(confDir)) {
      fs::create_directories(confDir);
    }
  } catch (...) {}

  if (!fs::exists(confPath)) {
    std::ofstream out(confPath);
    if (out.is_open()) {
      out << "# Fyzenor Configuration File\n\n"
          << "[general]\n"
          << "show_hidden = false\n"
          << "sort_mode = \"name\" # \"name\", \"size\", or \"date\"\n\n"
          << "[layout]\n"
          << "parent_width = 0.18\n"
          << "current_width = 0.32\n"
          << "hide_preview = false\n"
          << "hide_parent = false\n\n"
          << "[icons]\n"
          << "dir = \" \"\n"
          << "video = \" \"\n"
          << "image = \" \"\n"
          << "core = \" \"\n"
          << "frontend = \"󰖟 \"\n"
          << "config = \" \"\n"
          << "script = \" \"\n"
          << "docs = \" \"\n"
          << "font = \" \"\n"
          << "file = \" \"\n"
          << "music = \" \"\n"
          << "pin = \" \"\n"
          << "zip = \"󰿺 \"\n"
          << "link = \"󰌹 \"\n\n"
          << "[categories]\n"
          << "video = [\".mp4\", \".mkv\", \".avi\", \".mov\", \".flv\", \".wmv\", \".webm\", \".m4v\", \".mpg\", \".mpeg\"]\n"
          << "image = [\".png\", \".jpg\", \".jpeg\", \".gif\", \".bmp\", \".webp\", \".svg\", \".tiff\", \".ico\", \".psd\", \".ai\"]\n"
          << "frontend = [\".js\", \".jsx\", \".ts\", \".tsx\", \".css\", \".scss\", \".sass\", \".less\", \".styl\", \".vue\", \".html\", \".svelte\", \".htm\", \".astro\", \".mjx\", \".dart\", \".swift\"]\n"
          << "scripts = [\".sh\", \".bash\", \".zsh\", \".fish\", \".ksh\", \".command\", \".pl\", \".pm\", \".t\", \".awk\", \".ps1\", \".psm1\", \".bat\", \".cmd\", \".vbs\", \".wsf\"]\n"
          << "config = [\".json\", \".json5\", \".jsonc\", \".xml\", \".xsd\", \".xsl\", \".gpx\", \".yaml\", \".yml\", \".toml\", \".ini\", \".conf\", \".cfg\", \".prefs\", \".properties\", \".lock\", \".env\", \".dockerfile\", \".gitignore\", \".gitconfig\", \".gitattributes\", \".gitmodules\"]\n"
          << "documentation = [\".md\", \".markdown\", \".txt\", \".text\", \".log\", \".pdf\", \".doc\", \".docx\", \".odt\", \".rtf\", \".ppt\", \".pptx\", \".odp\", \".xls\", \".xlsx\", \".ods\", \".csv\"]\n"
          << "core = [\".py\", \".pyw\", \".ipynb\", \".pyc\", \".pyd\", \".rb\", \".ru\", \".gemspec\", \".php\", \".cpp\", \".cxx\", \".cc\", \".hpp\", \".hxx\", \".ixx\", \".c\", \".h\", \".rs\", \".java\", \".class\", \".jar\", \".war\", \".go\", \".lua\", \".sql\", \".db\", \".sqlite\", \".sqlite3\", \".db3\", \".mdb\", \".accdb\", \".cmake\", \".make\", \".diff\", \".patch\", \".kt\", \".kts\", \".cs\", \".csx\", \".scala\", \".sc\", \".hs\", \".lhs\", \".clj\", \".cljs\", \".cljc\", \".edn\", \".r\", \".rmd\", \".jl\", \".fs\", \".fsi\", \".fsx\"]\n"
          << "font = [\".woff\", \".woff2\", \".ttf\", \".eot\", \".otf\"]\n"
          << "audio = [\".mp3\", \".wav\", \".flac\", \".m4a\", \".aac\", \".ogg\", \".wma\", \".opus\", \".mid\", \".midi\"]\n"
          << "archive = [\".zip\", \".tar\", \".gz\", \".tgz\", \".7z\", \".rar\", \".xz\", \".bz2\", \".tbz2\", \".lzma\", \".cab\"]\n";
      out.close();
    }
  }

  std::ifstream f(confPath);
  if (!f.is_open()) return;

  std::string line;
  std::string section = "";

  auto trim = [](const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string("");
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
  };

  auto parse_string = [](const std::string& val) {
    auto first = val.find('"');
    auto last = val.find_last_of('"');
    if (first != std::string::npos && last != std::string::npos && first < last) {
      return val.substr(first + 1, last - first - 1);
    }
    return val;
  };

  auto parse_list = [&](const std::string& val) {
    std::set<std::string> res;
    auto start = val.find('[');
    auto end = val.find(']');
    if (start != std::string::npos && end != std::string::npos && start < end) {
      std::string content = val.substr(start + 1, end - start - 1);
      std::stringstream ss(content);
      std::string item;
      while (std::getline(ss, item, ',')) {
        item = trim(item);
        std::string s_val = parse_string(item);
        if (!s_val.empty()) {
          res.insert(s_val);
        }
      }
    }
    return res;
  };

  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;

    if (line[0] == '[' && line.back() == ']') {
      section = line.substr(1, line.length() - 2);
      std::transform(section.begin(), section.end(), section.begin(), ::tolower);
      continue;
    }

    auto eq = line.find('=');
    if (eq == std::string::npos) continue;

    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    auto hash = val.find('#');
    if (hash != std::string::npos) {
      val = trim(val.substr(0, hash));
    }

    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    if (section == "general") {
      if (key == "show_hidden") {
        configShowHidden = (val == "true");
      } else if (key == "sort_mode") {
        configSortMode = parse_string(val);
      }
    } else if (section == "layout") {
      if (key == "parent_width") {
        try { configParentWidth = std::stod(val); } catch (...) {}
      } else if (key == "current_width") {
        try { configCurrentWidth = std::stod(val); } catch (...) {}
      } else if (key == "hide_preview") {
        configHidePreview = (val == "true");
      } else if (key == "hide_parent") {
        configHideParent = (val == "true");
      }
    } else if (section == "icons") {
      std::string icon_val = parse_string(val);
      if (key == "dir") g_icon_dir = icon_val;
      else if (key == "video") g_icon_video = icon_val;
      else if (key == "image") g_icon_image = icon_val;
      else if (key == "core") g_icon_core = icon_val;
      else if (key == "frontend") g_icon_frontend = icon_val;
      else if (key == "config") g_icon_config = icon_val;
      else if (key == "script") g_icon_script = icon_val;
      else if (key == "docs" || key == "file") {
        g_icon_docs = icon_val;
        g_icon_file = icon_val;
      }
      else if (key == "font") g_icon_font = icon_val;
      else if (key == "music") g_icon_music = icon_val;
      else if (key == "pin") g_icon_pin = icon_val;
      else if (key == "zip") g_icon_zip = icon_val;
      else if (key == "link") g_icon_link = icon_val;
    } else if (section == "categories") {
      std::set<std::string> ext_set = parse_list(val);
      if (!ext_set.empty()) {
        if (key == "video") VIDEO_EXTS = ext_set;
        else if (key == "image") IMAGE_EXTS = ext_set;
        else if (key == "frontend") FRONTEND_EXTS = ext_set;
        else if (key == "scripts") SCRIPTS_EXTS = ext_set;
        else if (key == "config") CONFIG_EXTS = ext_set;
        else if (key == "documentation") DOCUMENTATION_EXTS = ext_set;
        else if (key == "core") CORE_EXTS = ext_set;
        else if (key == "font") FONT_EXTS = ext_set;
        else if (key == "audio") AUDIO_EXTS = ext_set;
        else if (key == "archive") ARCHIVE_EXTS = ext_set;
      }
    }
  }

  // Re-assign pointers to ensure they point to the updated std::string buffers
  ICON_DIR = g_icon_dir.c_str();
  ICON_VIDEO = g_icon_video.c_str();
  ICON_IMAGE = g_icon_image.c_str();
  ICON_CORE = g_icon_core.c_str();
  ICON_FRONTEND = g_icon_frontend.c_str();
  ICON_CONFIG = g_icon_config.c_str();
  ICON_SCRIPT = g_icon_script.c_str();
  ICON_DOCS = g_icon_docs.c_str();
  ICON_FONT = g_icon_font.c_str();
  ICON_FILE = g_icon_file.c_str();
  ICON_MUSIC = g_icon_music.c_str();
  ICON_PIN = g_icon_pin.c_str();
  ICON_ZIP = g_icon_zip.c_str();
  ICON_LINK = g_icon_link.c_str();
}
