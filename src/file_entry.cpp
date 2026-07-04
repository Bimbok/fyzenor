#include "file_entry.h"
#include <algorithm>
#include <chrono>

FileEntry::FileEntry(const fs::path& p) : path(p) {
  name = p.filename().string();
  is_directory = false;
  is_symlink = false;
  symlink_target = "";
  symlink_target_exists = false;
  is_symlink_directory = false;
  extension = p.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

  bool isGvfs = (p.string().find("/gvfs/") != std::string::npos);

  try {
    if (isGvfs) {
      modified_time = fs::file_time_type::min();
    } else {
      modified_time = fs::last_write_time(p);
    }
  } catch (...) {
    modified_time = fs::file_time_type::min();
  }

  try {
    is_symlink = fs::is_symlink(fs::symlink_status(p));
    if (is_symlink) {
      try {
        fs::path target_path = fs::read_symlink(p);
        symlink_target = target_path.string();
        fs::path resolved = target_path;
        if (target_path.is_relative()) {
          resolved = p.parent_path() / target_path;
        }
        if (fs::exists(resolved)) {
          symlink_target_exists = true;
          if (fs::is_directory(resolved)) {
            is_symlink_directory = true;
          }
        }
      } catch (...) {}
    }
  } catch (...) {}

  try {
    if (is_symlink) {
      is_directory = is_symlink_directory;
    } else {
      is_directory = fs::is_directory(p);
    }
  } catch (...) {
    is_directory = false;
  }

  try {
    if (is_directory) {
      size = isGvfs ? 0 : SIZE_CALCULATING;
    } else {
      size = fs::file_size(p);
    }
  } catch (...) {
    size = 0;
  }

  // Pre-format the compact modified time
  if (isGvfs || modified_time == fs::file_time_type::min()) {
    modified_time_str = "Unknown";
  } else {
    try {
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          modified_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
      std::time_t ctime = std::chrono::system_clock::to_time_t(sctp);
      std::tm* ltime = std::localtime(&ctime);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", ltime);
      modified_time_str = std::string(buf);
    } catch (...) {
      modified_time_str = "Unknown";
    }
  }
}

FileEntry::FileEntry(const fs::directory_entry& entry) : path(entry.path()) {
  name = path.filename().string();
  is_directory = false;
  is_symlink = false;
  symlink_target = "";
  symlink_target_exists = false;
  is_symlink_directory = false;
  extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

  bool isGvfs = (path.string().find("/gvfs/") != std::string::npos);

  try {
    is_symlink = entry.is_symlink();
    if (is_symlink) {
      try {
        fs::path target_path = fs::read_symlink(path);
        symlink_target = target_path.string();
        fs::path resolved = target_path;
        if (target_path.is_relative()) {
          resolved = path.parent_path() / target_path;
        }
        if (fs::exists(resolved)) {
          symlink_target_exists = true;
          if (fs::is_directory(resolved)) {
            is_symlink_directory = true;
          }
        }
      } catch (...) {}
    }
  } catch (...) {}

  try {
    if (is_symlink) {
      is_directory = is_symlink_directory;
    } else {
      is_directory = entry.is_directory();
    }
  } catch (...) {
    is_directory = false;
  }

  try {
    if (isGvfs) {
      modified_time = fs::file_time_type::min();
    } else {
      modified_time = entry.last_write_time();
    }
  } catch (...) {
    modified_time = fs::file_time_type::min();
  }

  try {
    if (is_directory) {
      size = isGvfs ? 0 : SIZE_CALCULATING;
    } else {
      size = entry.file_size();
    }
  } catch (...) {
    size = 0;
  }

  // Pre-format the compact modified time
  if (isGvfs || modified_time == fs::file_time_type::min()) {
    modified_time_str = "Unknown";
  } else {
    try {
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          modified_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
      std::time_t ctime = std::chrono::system_clock::to_time_t(sctp);
      std::tm* ltime = std::localtime(&ctime);
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", ltime);
      modified_time_str = std::string(buf);
    } catch (...) {
      modified_time_str = "Unknown";
    }
  }
}
