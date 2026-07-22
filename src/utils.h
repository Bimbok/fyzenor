#ifndef UTILS_H
#define UTILS_H

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

// Configuration extern declarations
extern std::set<std::string> VIDEO_EXTS;
extern std::set<std::string> IMAGE_EXTS;
extern std::set<std::string> FRONTEND_EXTS;
extern std::set<std::string> SCRIPTS_EXTS;
extern std::set<std::string> CONFIG_EXTS;
extern std::set<std::string> DOCUMENTATION_EXTS;
extern std::set<std::string> CORE_EXTS;
extern std::set<std::string> FONT_EXTS;
extern std::set<std::string> AUDIO_EXTS;
extern std::set<std::string> ARCHIVE_EXTS;

extern bool configShowHidden;
extern std::string configSortMode;
extern double configParentWidth;
extern double configCurrentWidth;
extern bool configHidePreview;
extern bool configHideParent;
extern bool configHidePinned;
extern std::chrono::steady_clock::time_point globalStartTime;
void loadConfiguration();
std::string keyToName(int ch);

extern const char* ICON_DIR;
extern const char* ICON_VIDEO;
extern const char* ICON_IMAGE;
extern const char* ICON_CORE;
extern const char* ICON_FRONTEND;
extern const char* ICON_CONFIG;
extern const char* ICON_SCRIPT;
extern const char* ICON_DOCS;
extern const char* ICON_FONT;
extern const char* ICON_FILE;
extern const char* ICON_MUSIC;
extern const char* ICON_PIN;
extern const char* ICON_ZIP;
extern const char* ICON_LINK;

extern const std::string PREVIEW_TEMP;
extern const uintmax_t SIZE_CALCULATING;

enum class SortMode {
  NAME,
  SIZE,
  DATE
};

enum class PreviewType { NONE, IMAGE, TEXT };

struct Clipboard {
  std::vector<fs::path> paths;
  bool isCut = false;
};

struct FileStyle {
  int pair;
  const char* icon;
};

// Declarations of utility functions
std::string getCacheDir();
std::string getCachePath(const fs::path& p, int w, int h);
size_t utf8_length(const std::string& str);
std::string utf8_safe_truncate(const std::string& str, size_t max_cols);
std::string utf8_safe_truncate_left(const std::string& str, size_t max_cols);
FileStyle getFileStyle(const std::string& name, const std::string& ext, bool isDir, bool isEmptyDir = false);
int getFinalPair(int base, bool isSelected, bool isSecondary);
std::string base64_encode(const unsigned char* bytes, size_t len);
std::string formatSize(uintmax_t size);
std::string getFileModifiedTime(const fs::path& path);
bool is_binary_file(const std::string& path);
std::string escapeShellArg(const std::string& str);
bool fuzzyMatch(const std::string& str, const std::string& query);
void initColors();
bool isCommandAvailable(const std::string& cmd);
std::string urlDecode(const std::string& str);
std::vector<fs::path> parsePastedPaths(const std::string& data);

#endif // UTILS_H
