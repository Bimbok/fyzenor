#ifndef FILE_ENTRY_H
#define FILE_ENTRY_H

#include "utils.h"

struct FileEntry {
  fs::path path;
  std::string name;
  bool is_directory;
  uintmax_t size;
  std::string extension;
  bool is_symlink;
  std::string symlink_target;
  bool symlink_target_exists;
  bool is_symlink_directory;
  fs::file_time_type modified_time;
  std::string modified_time_str;

  FileEntry(const fs::path& p);
  FileEntry(const fs::directory_entry& entry);
};

#endif // FILE_ENTRY_H
