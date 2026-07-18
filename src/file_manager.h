#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "utils.h"
#include "file_entry.h"
#include "async_task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <clocale>
#include <cctype>
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
#include <ctime>
#include <signal.h>

#include <chrono>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <sys/inotify.h>
#include <poll.h>
#include <cerrno>

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
  struct Tab {
    fs::path currentPath;
    size_t selectedIndex = 0;
    size_t scrollOffset = 0;
    bool isSearching = false;
    bool isTrashMode = false;
    std::set<fs::path> multiSelection;
    std::vector<FileEntry> currentFiles;
    std::vector<fs::path> backHistory;
    std::vector<fs::path> forwardHistory;
  };
  std::vector<Tab> tabs;
  size_t activeTabIndex = 0;

  bool isDualPaneMode = false;
  size_t leftTabIndex = 0;
  size_t rightTabIndex = 1;
  bool focusLeftPane = true;
  int dualPaneSplitOffset = 0;
  bool hidePreview = false;
  bool hideParent = false;

  fs::path currentPath;
  std::vector<FileEntry> currentFiles;
  std::vector<FileEntry> parentFiles;
  std::set<fs::path> multiSelection;
  std::vector<fs::path> pinnedPaths;
  size_t pinnedIndex = 0;
  bool focusPinned = false;
  size_t selectedIndex;
  size_t scrollOffset;
  std::unordered_map<int, std::string> customMacros;

  WINDOW *winPinned, *winParent, *winCurrent, *winPreview;
  int width, height;

  Clipboard clipboard;
  std::string statusMessage;
  std::chrono::steady_clock::time_point statusTime;
  bool showHidden = false;
  SortMode sortMode = SortMode::NAME;
  bool isSearching = false;
  bool isTrashMode = false;
  fs::path preTrashPath;
  std::vector<fs::path> lastTrashedFiles;

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

  // Auto-Update (Inotify) State
  int inotifyFd = -1;
  std::map<int, fs::path> watchDescriptors;
  std::mutex inotifyMutex;
  std::thread inotifyThread;
  std::atomic<bool> stopInotify{false};
  std::atomic<bool> inotifyTriggered{false};
  std::atomic<bool> devicesTriggered{false};

  // Async Background Tasks State
  std::vector<std::shared_ptr<AsyncTask>> activeTasks;
  std::vector<std::string> taskHistoryLogs;
  std::mutex taskMutex;
  int nextTaskId = 1;

  uint64_t getDirectorySize(const fs::path& dir) {
    uint64_t size = 0;
    try {
      for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
        if (fs::is_regular_file(entry.status())) {
          size += fs::file_size(entry);
        }
      }
    } catch (...) {}
    return size;
  }

  void changeDirectory(const fs::path& target, bool recordHistory = true) {
    if (currentPath == target) return;
    if (recordHistory) {
      if (activeTabIndex < tabs.size()) {
        auto& tab = tabs[activeTabIndex];
        if (tab.backHistory.empty() || tab.backHistory.back() != currentPath) {
          tab.backHistory.push_back(currentPath);
          if (tab.backHistory.size() > 100) {
            tab.backHistory.erase(tab.backHistory.begin());
          }
        }
        tab.forwardHistory.clear();
      }
    }
    currentPath = target;
  }

  void handleGoBack() {
    if (activeTabIndex >= tabs.size()) return;
    auto& tab = tabs[activeTabIndex];
    if (tab.backHistory.empty()) {
      setStatus("No back history");
      return;
    }
    tab.forwardHistory.push_back(currentPath);
    if (tab.forwardHistory.size() > 100) {
      tab.forwardHistory.erase(tab.forwardHistory.begin());
    }
    
    fs::path target = tab.backHistory.back();
    tab.backHistory.pop_back();
    
    currentPath = target;
    reloadAll();
    selectedIndex = 0;
    scrollOffset = 0;
    setStatus("Navigated back");
  }

  void handleGoForward() {
    if (activeTabIndex >= tabs.size()) return;
    auto& tab = tabs[activeTabIndex];
    if (tab.forwardHistory.empty()) {
      setStatus("No forward history");
      return;
    }
    tab.backHistory.push_back(currentPath);
    if (tab.backHistory.size() > 100) {
      tab.backHistory.erase(tab.backHistory.begin());
    }
    
    fs::path target = tab.forwardHistory.back();
    tab.forwardHistory.pop_back();
    
    currentPath = target;
    reloadAll();
    selectedIndex = 0;
    scrollOffset = 0;
    setStatus("Navigated forward");
  }

  void drawHistoryOverlay() {
    clearDirectRender();
    if (activeTabIndex >= tabs.size()) return;
    auto& tab = tabs[activeTabIndex];
    
    std::vector<fs::path> hist = tab.backHistory;
    std::reverse(hist.begin(), hist.end());
    
    int h = 15;
    int w = 70;
    if (h > height - 4) h = height - 4;
    if (w > width - 4) w = width - 4;
    if (h < 6) h = 6;
    if (w < 20) w = 20;

    int startY = (height - h) / 2;
    int startX = (width - w) / 2;

    WINDOW* histWin = newwin(h, w, startY, startX);
    if (!histWin) return;
    keypad(histWin, TRUE);
    wtimeout(histWin, 200);

    size_t selectedHistoryIndex = 0;

    while (true) {
      werase(histWin);
      wattron(histWin, COLOR_PAIR(6) | A_BOLD);
      drawRoundedBox(histWin);
      wattroff(histWin, COLOR_PAIR(6) | A_BOLD);

      wattron(histWin, COLOR_PAIR(1) | A_BOLD);
      std::string title = " Navigation History";
      mvwprintw(histWin, 1, 2, "%s", title.c_str());
      wattroff(histWin, COLOR_PAIR(1) | A_BOLD);

      if (hist.empty()) {
        wattron(histWin, A_ITALIC | COLOR_PAIR(27));
        mvwprintw(histWin, h / 2, (w - 20) / 2, "No history recorded yet");
        wattroff(histWin, A_ITALIC | COLOR_PAIR(27));
      } else {
        int maxLines = h - 4;
        for (int i = 0; i < maxLines && i < (int)hist.size(); ++i) {
          const auto& path = hist[i];
          bool isSel = (i == (int)selectedHistoryIndex);
          int lineY = 3 + i;

          std::string pathStr = path.string();
          if ((int)pathStr.length() > w - 6) {
            pathStr = utf8_safe_truncate_left(pathStr, w - 6);
          }

          if (isSel) {
            wattron(histWin, COLOR_PAIR(6) | A_BOLD);
            for (int j = 1; j < w - 1; ++j) {
              mvwaddch(histWin, lineY, j, ' ');
            }
            mvwprintw(histWin, lineY, 2, "➜ %s", pathStr.c_str());
            wattroff(histWin, COLOR_PAIR(6) | A_BOLD);
          } else {
            mvwprintw(histWin, lineY, 2, "  %s", pathStr.c_str());
          }
        }
      }

      wattron(histWin, A_DIM);
      mvwprintw(histWin, h - 2, 2, "[Enter] Jump  [Esc/q] Close");
      wattroff(histWin, A_DIM);

      wrefresh(histWin);

      int ch = wgetch(histWin);
      if (ch == ERR) continue;
      if (ch == 'q' || ch == 27) {
        break;
      }
      if (ch == 'j' || ch == KEY_DOWN) {
        if (!hist.empty() && selectedHistoryIndex < hist.size() - 1) {
          selectedHistoryIndex++;
        }
      }
      if (ch == 'k' || ch == KEY_UP) {
        if (selectedHistoryIndex > 0) {
          selectedHistoryIndex--;
        }
      }
      if (ch == 10) {
        if (!hist.empty() && selectedHistoryIndex < hist.size()) {
          fs::path target = hist[selectedHistoryIndex];
          changeDirectory(target, true);
          reloadAll();
          selectedIndex = 0;
          scrollOffset = 0;
          setStatus("Jumped to history entry");
          break;
        }
      }
    }

    delwin(histWin);
    updateLayout();
  }

  void drawPermissionsOverlay() {
    clearDirectRender();
    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) return;
    fs::path filePath = currentFiles[selectedIndex].path;

    fs::perms perms;
    try {
      perms = fs::status(filePath).permissions();
    } catch (...) {
      setStatus("Error: Cannot read file permissions");
      return;
    }

    bool grid[3][3];
    grid[0][0] = (perms & fs::perms::owner_read) != fs::perms::none;
    grid[0][1] = (perms & fs::perms::owner_write) != fs::perms::none;
    grid[0][2] = (perms & fs::perms::owner_exec) != fs::perms::none;

    grid[1][0] = (perms & fs::perms::group_read) != fs::perms::none;
    grid[1][1] = (perms & fs::perms::group_write) != fs::perms::none;
    grid[1][2] = (perms & fs::perms::group_exec) != fs::perms::none;

    grid[2][0] = (perms & fs::perms::others_read) != fs::perms::none;
    grid[2][1] = (perms & fs::perms::others_write) != fs::perms::none;
    grid[2][2] = (perms & fs::perms::others_exec) != fs::perms::none;

    std::string owner = "";
    std::string group = "";
    struct stat info;
    if (stat(filePath.c_str(), &info) == 0) {
      struct passwd *pw = getpwuid(info.st_uid);
      struct group  *gr = getgrgid(info.st_gid);
      owner = pw ? pw->pw_name : std::to_string(info.st_uid);
      group = gr ? gr->gr_name : std::to_string(info.st_gid);
    }

    int h = 18;
    int w = 60;
    if (h > height - 4) h = height - 4;
    if (w > width - 4) w = width - 4;
    if (h < 6) h = 6;
    if (w < 20) w = 20;

    int startY = (height - h) / 2;
    int startX = (width - w) / 2;

    WINDOW* permWin = newwin(h, w, startY, startX);
    if (!permWin) return;
    keypad(permWin, TRUE);
    wtimeout(permWin, 200);

    int activeRow = 0; 
    int activeCol = 0; 

    while (true) {
      werase(permWin);
      wattron(permWin, COLOR_PAIR(6) | A_BOLD);
      drawRoundedBox(permWin);
      wattroff(permWin, COLOR_PAIR(6) | A_BOLD);

      wattron(permWin, COLOR_PAIR(1) | A_BOLD);
      std::string title = "󰋊 Permissions & Ownership";
      mvwprintw(permWin, 1, 2, "%s", title.c_str());
      wattroff(permWin, COLOR_PAIR(1) | A_BOLD);

      std::string nameDisp = "File: " + filePath.filename().string();
      if ((int)nameDisp.length() > w - 6) {
        nameDisp = utf8_safe_truncate(nameDisp, w - 6);
      }
      mvwprintw(permWin, 3, 2, "%s", nameDisp.c_str());

      mvwprintw(permWin, 5, 16, "Read");
      mvwprintw(permWin, 5, 28, "Write");
      mvwprintw(permWin, 5, 40, "Execute");

      const char* rowLabels[3] = {"Owner:", "Group:", "Others:"};
      for (int r = 0; r < 3; ++r) {
        mvwprintw(permWin, 6 + r, 4, "%s", rowLabels[r]);
        for (int c = 0; c < 3; ++c) {
          int posX = 16 + c * 12;
          bool isSel = (activeRow == r && activeCol == c);
          
          if (isSel) {
            wattron(permWin, COLOR_PAIR(6) | A_BOLD);
          }
          mvwprintw(permWin, 6 + r, posX, "[%s]", grid[r][c] ? "X" : " ");
          if (isSel) {
            wattroff(permWin, COLOR_PAIR(6) | A_BOLD);
          }
        }
      }

      mvwprintw(permWin, 10, 4, "Owner:");
      if (activeRow == 3) {
        wattron(permWin, COLOR_PAIR(6) | A_BOLD);
      }
      mvwprintw(permWin, 10, 16, "%s (press Enter to Edit)", owner.c_str());
      if (activeRow == 3) {
        wattroff(permWin, COLOR_PAIR(6) | A_BOLD);
      }

      mvwprintw(permWin, 11, 4, "Group:");
      if (activeRow == 4) {
        wattron(permWin, COLOR_PAIR(6) | A_BOLD);
      }
      mvwprintw(permWin, 11, 16, "%s (press Enter to Edit)", group.c_str());
      if (activeRow == 4) {
        wattroff(permWin, COLOR_PAIR(6) | A_BOLD);
      }

      int btnY = h - 4;
      if (activeRow == 5 && activeCol == 0) {
        wattron(permWin, COLOR_PAIR(6) | A_BOLD);
      }
      mvwprintw(permWin, btnY, 15, "[ SAVE ]");
      if (activeRow == 5 && activeCol == 0) {
        wattroff(permWin, COLOR_PAIR(6) | A_BOLD);
      }

      if (activeRow == 5 && activeCol == 1) {
        wattron(permWin, COLOR_PAIR(6) | A_BOLD);
      }
      mvwprintw(permWin, btnY, 35, "[ CANCEL ]");
      if (activeRow == 5 && activeCol == 1) {
        wattroff(permWin, COLOR_PAIR(6) | A_BOLD);
      }

      wattron(permWin, A_DIM);
      mvwprintw(permWin, h - 2, 2, "[hjkl/Arrows] Move  [Space/Enter] Toggle  [Esc/q] Close");
      wattroff(permWin, A_DIM);

      wrefresh(permWin);

      int ch = wgetch(permWin);
      if (ch == ERR) continue;
      if (ch == 'q' || ch == 27) {
        break;
      }
      if (ch == 'j' || ch == KEY_DOWN) {
        activeRow = (activeRow + 1) % 6;
      }
      if (ch == 'k' || ch == KEY_UP) {
        activeRow = (activeRow + 5) % 6;
      }
      if (ch == 'h' || ch == KEY_LEFT) {
        if (activeRow < 3) {
          activeCol = (activeCol + 2) % 3;
        } else if (activeRow == 5) {
          activeCol = (activeCol + 1) % 2;
        }
      }
      if (ch == 'l' || ch == KEY_RIGHT) {
        if (activeRow < 3) {
          activeCol = (activeCol + 1) % 3;
        } else if (activeRow == 5) {
          activeCol = (activeCol + 1) % 2;
        }
      }
      if (ch == ' ') {
        if (activeRow < 3) {
          grid[activeRow][activeCol] = !grid[activeRow][activeCol];
        }
      }
      if (ch == 10) { 
        if (activeRow < 3) {
          grid[activeRow][activeCol] = !grid[activeRow][activeCol];
        } else if (activeRow == 3) {
          std::string val = promptInput("New Owner", owner);
          if (!val.empty()) owner = val;
        } else if (activeRow == 4) {
          std::string val = promptInput("New Group", group);
          if (!val.empty()) group = val;
        } else if (activeRow == 5) {
          if (activeCol == 0) {
            fs::perms newPerms = fs::perms::none;
            if (grid[0][0]) newPerms |= fs::perms::owner_read;
            if (grid[0][1]) newPerms |= fs::perms::owner_write;
            if (grid[0][2]) newPerms |= fs::perms::owner_exec;

            if (grid[1][0]) newPerms |= fs::perms::group_read;
            if (grid[1][1]) newPerms |= fs::perms::group_write;
            if (grid[1][2]) newPerms |= fs::perms::group_exec;

            if (grid[2][0]) newPerms |= fs::perms::others_read;
            if (grid[2][1]) newPerms |= fs::perms::others_write;
            if (grid[2][2]) newPerms |= fs::perms::others_exec;

            try {
              fs::permissions(filePath, newPerms, fs::perm_options::replace);
            } catch (const std::exception& e) {
              setStatus("Error setting permissions: " + std::string(e.what()));
              break;
            }

            uid_t newUid = -1;
            gid_t newGid = -1;

            if (!owner.empty()) {
              struct passwd* pw = getpwnam(owner.c_str());
              if (pw) {
                newUid = pw->pw_uid;
              } else {
                try {
                  newUid = std::stoi(owner);
                } catch (...) {
                  setStatus("Error: Invalid owner name/UID");
                  break;
                }
              }
            }

            if (!group.empty()) {
              struct group* gr = getgrnam(group.c_str());
              if (gr) {
                newGid = gr->gr_gid;
              } else {
                try {
                  newGid = std::stoi(group);
                } catch (...) {
                  setStatus("Error: Invalid group name/GID");
                  break;
                }
              }
            }

            if (chown(filePath.c_str(), newUid, newGid) != 0) {
              if (errno == EPERM) {
                setStatus("Error: Permission denied (run as root)");
              } else {
                setStatus("Error: Failed to change owner/group: " + std::string(strerror(errno)));
              }
            } else {
              setStatus("Updated permissions & ownership successfully");
            }
            break;
          } else {
            break;
          }
        }
      }
    }

    delwin(permWin);
    updateLayout();
  }

  bool isDescendant(const fs::path& child, const fs::path& parent) {
    try {
      fs::path absParent = fs::absolute(parent).lexically_normal();
      fs::path absChild = fs::absolute(child).lexically_normal();
      auto rel = absChild.lexically_relative(absParent);
      if (!rel.empty() && rel.string() != "." && rel.string().substr(0, 2) != "..") {
        return true;
      }
    } catch (...) {}
    return false;
  }

  bool copyFileWithProgress(const fs::path& src, const fs::path& dest, std::shared_ptr<AsyncTask> task, uint64_t& bytesCopied, uint64_t totalBytes) {
    try {
      if (fs::is_symlink(fs::symlink_status(dest))) {
        fs::remove(dest);
      }
    } catch (...) {}

    std::ifstream in(src, std::ios::binary);
    if (!in) return false;

    uintmax_t destSize = 0;
    bool appendMode = false;
    try {
      if (fs::exists(dest) && fs::is_regular_file(dest)) {
        destSize = fs::file_size(dest);
        uintmax_t srcSize = fs::file_size(src);
        if (destSize == srcSize) {
          bytesCopied += srcSize;
          task->bytesProcessed = bytesCopied;
          if (totalBytes > 0) {
            int prog = (int)((bytesCopied * 100) / totalBytes);
            task->progress = prog > 100 ? 100 : prog;
          }
          return true; // already copied completely!
        } else if (destSize < srcSize && destSize > 0) {
          appendMode = true;
        }
      }
    } catch (...) {}

    std::ofstream out;
    if (appendMode) {
      in.seekg(destSize);
      if (in.good()) {
        out.open(dest, std::ios::binary | std::ios::in | std::ios::out);
        if (out) {
          out.seekp(destSize);
          if (out.good()) {
            bytesCopied += destSize;
            task->bytesProcessed = bytesCopied;
            if (totalBytes > 0) {
              int prog = (int)((bytesCopied * 100) / totalBytes);
              task->progress = prog > 100 ? 100 : prog;
            }
          } else {
            out.close();
            appendMode = false;
          }
        } else {
          appendMode = false;
        }
      } else {
        appendMode = false;
      }
    }

    if (!appendMode) {
      in.seekg(0);
      out.close();
      out.clear();
      out.open(dest, std::ios::binary);
    }

    if (!out) return false;

    char buffer[65536];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
      task->checkPause();
      if (task->isCancelled.load()) {
        return false;
      }
      out.write(buffer, in.gcount());
      if (out.fail()) {
        return false;
      }
      bytesCopied += in.gcount();
      task->bytesProcessed = bytesCopied;
      if (totalBytes > 0) {
        int prog = (int)((bytesCopied * 100) / totalBytes);
        task->progress = prog > 100 ? 100 : prog;
      } else {
        task->progress = 100;
      }
    }
    if (in.bad() || (in.fail() && !in.eof())) {
      return false;
    }
    return true;
  }

  void startPasteTask(const std::vector<std::pair<fs::path, fs::path>>& jobs, bool isCut) {
    auto task = std::make_shared<AsyncTask>();
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      task->id = nextTaskId++;
      task->type = isCut ? "Move" : "Copy";
      task->description = (jobs.size() > 1)
          ? "Copying " + std::to_string(jobs.size()) + " items to " + currentPath.filename().string()
          : "Copying " + jobs[0].first.filename().string() + " to " + currentPath.filename().string();
      activeTasks.push_back(task);
    }

    std::string typeStr = isCut ? "Moving" : "Copying";
    setStatus(typeStr + " task started in background");

    std::weak_ptr<AsyncTask> weakTask = task;
    task->workerThread = std::thread([this, weakTask, jobs, isCut]() {
      auto task = weakTask.lock();
      if (!task) return;

      uint64_t totalBytes = 0;
      for (const auto& job : jobs) {
        try {
          if (fs::is_directory(job.first)) {
            totalBytes += getDirectorySize(job.first);
          } else if (fs::is_regular_file(job.first)) {
            totalBytes += fs::file_size(job.first);
          }
        } catch (...) {}
      }
      task->totalBytes = totalBytes;

      uint64_t bytesCopied = 0;
      int successCount = 0;
      int failCount = 0;

      for (const auto& job : jobs) {
        task->checkPause();
        if (task->isCancelled.load()) {
          failCount = jobs.size() - successCount;
          break;
        }
        fs::path src = job.first;
        fs::path dest = job.second;
        bool jobOk = false;
        try {
          if (fs::is_directory(src)) {
            fs::create_directories(dest);
            bool dirCopiedFully = true;
            std::vector<fs::path> filesToKeep;
            try {
              for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied)) {
                task->checkPause();
                if (task->isCancelled.load()) {
                  dirCopiedFully = false;
                  break;
                }
                fs::path rel = fs::relative(entry.path(), src);
                fs::path d = dest / rel;
                if (fs::is_directory(entry.status())) {
                  fs::create_directories(d);
                } else if (fs::is_regular_file(entry.status())) {
                  bool ok = copyFileWithProgress(entry.path(), d, task, bytesCopied, totalBytes);
                  if (ok) {
                    if (isCut) filesToKeep.push_back(entry.path());
                  } else {
                    dirCopiedFully = false;
                  }
                }
              }
            } catch (...) {
              dirCopiedFully = false;
            }
            if (isCut) {
              if (dirCopiedFully) {
                fs::remove_all(src);
                jobOk = true;
              } else {
                for (const auto& p : filesToKeep) {
                  try { fs::remove(p); } catch(...) {}
                }
              }
            } else {
              if (dirCopiedFully) {
                jobOk = true;
              }
            }
          } else {
            bool ok = copyFileWithProgress(src, dest, task, bytesCopied, totalBytes);
            if (ok) {
              if (isCut) {
                try { fs::remove(src); } catch(...) {}
              }
              jobOk = true;
            }
          }
          if (jobOk) {
            successCount++;
          } else {
            failCount++;
          }
        } catch (...) {
          failCount++;
        }
      }

      task->progress = 100;
      if (task->isCancelled.load()) {
        task->statusMessage = "Cancelled";
      } else if (failCount > 0) {
        task->statusMessage = "Finished with errors (pasted " + std::to_string(successCount) + " items, " + std::to_string(failCount) + " failed)";
      } else {
        task->statusMessage = "Finished (pasted " + std::to_string(successCount) + " items)";
      }
      std::string logMsg = "[" + task->type + "] " + task->description + " - " + task->statusMessage;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskHistoryLogs.push_back(logMsg);
      }
      task->isFinished = true;
    });
  }

  void startDeleteTask(const std::vector<fs::path>& targets) {
    auto task = std::make_shared<AsyncTask>();
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      task->id = nextTaskId++;
      task->type = "Delete";
      task->description = (targets.size() > 1)
          ? "Deleting " + std::to_string(targets.size()) + " items"
          : "Deleting " + targets[0].filename().string();
      activeTasks.push_back(task);
    }

    setStatus("Deletion task started in background");

    std::weak_ptr<AsyncTask> weakTask = task;
    task->workerThread = std::thread([this, weakTask, targets]() {
      auto task = weakTask.lock();
      if (!task) return;

      int totalItems = targets.size();
      int processed = 0;

      for (const auto& p : targets) {
        task->checkPause();
        if (task->isCancelled.load()) break;
        try {
          fs::remove_all(p);
        } catch (...) {}
        processed++;
        task->progress = (processed * 100) / totalItems;
      }

      task->progress = 100;
      if (task->isCancelled.load()) {
        task->statusMessage = "Cancelled";
      } else {
        task->statusMessage = "Finished (deleted " + std::to_string(totalItems) + " items)";
      }
      std::string logMsg = "[" + task->type + "] " + task->description + " - " + task->statusMessage;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskHistoryLogs.push_back(logMsg);
      }
      task->isFinished = true;
    });
  }

  void startTrashTask(const std::vector<fs::path>& targets) {
    auto task = std::make_shared<AsyncTask>();
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      task->id = nextTaskId++;
      task->type = "Trash";
      task->description = (targets.size() > 1)
          ? "Trashing " + std::to_string(targets.size()) + " items"
          : "Trashing " + targets[0].filename().string();
      activeTasks.push_back(task);
    }

    setStatus("Trash task started in background");

    std::weak_ptr<AsyncTask> weakTask = task;
    task->workerThread = std::thread([this, weakTask, targets]() {
      auto task = weakTask.lock();
      if (!task) return;

      int totalItems = targets.size();
      int processed = 0;
      int successCount = 0;
      std::vector<fs::path> failedTargets;
      std::vector<fs::path> trashedFilesTemp;

      for (const auto& p : targets) {
        task->checkPause();
        if (task->isCancelled.load()) break;

        if (moveToTrash(p)) {
          successCount++;
          fs::path trashed = findTrashedFile(p);
          if (!trashed.empty()) {
            trashedFilesTemp.push_back(trashed);
          }
        } else {
          failedTargets.push_back(p);
        }

        processed++;
        task->progress = (processed * 100) / totalItems;
      }

      {
        std::lock_guard<std::mutex> lock(taskMutex);
        lastTrashedFiles = trashedFilesTemp;
      }

      task->progress = 100;
      if (task->isCancelled.load()) {
        task->statusMessage = "Cancelled";
      } else if (!failedTargets.empty()) {
        task->statusMessage = "Finished with errors (trashed " + std::to_string(successCount) + " items, " + std::to_string(failedTargets.size()) + " failed)";
      } else {
        task->statusMessage = "Finished (trashed " + std::to_string(totalItems) + " items)";
      }
      std::string logMsg = "[" + task->type + "] " + task->description + " - " + task->statusMessage;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskHistoryLogs.push_back(logMsg);
      }
      task->isFinished = true;
    });
  }

  void startZipTask(const std::string& zipCmd, const std::string& zipName, const fs::path& zipDir) {
    auto task = std::make_shared<AsyncTask>();
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      task->id = nextTaskId++;
      task->type = "Zip";
      task->description = "Creating " + zipName;
      task->destPath = zipDir / zipName;
      activeTasks.push_back(task);
    }

    setStatus("Zip task started in background");

    std::string pidFile = "/tmp/fyzenor_zip_" + std::to_string(task->id);
    std::string wrappedCmd = "cd " + escapeShellArg(zipDir.string()) + " && (" + zipCmd + " & echo $! > " + pidFile + "; wait $!) > /dev/null 2>&1";

    std::weak_ptr<AsyncTask> weakTask = task;
    task->workerThread = std::thread([this, weakTask, wrappedCmd]() {
      auto task = weakTask.lock();
      if (!task) return;

      int res = system(wrappedCmd.c_str());
      (void)res;

      std::string pidFile = "/tmp/fyzenor_zip_" + std::to_string(task->id);
      try { fs::remove(pidFile); } catch(...) {}

      task->progress = 100;
      if (task->isCancelled.load()) {
        task->statusMessage = "Cancelled";
      } else {
        task->statusMessage = (res == 0) ? "Finished successfully" : "Failed with exit code " + std::to_string(res);
      }
      std::string logMsg = "[" + task->type + "] " + task->description + " - " + task->statusMessage;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskHistoryLogs.push_back(logMsg);
      }
      task->isFinished = true;
    });
  }

  void startExtractTask(const std::string& extractCmd, const std::string& archiveName, const fs::path& destDir) {
    auto task = std::make_shared<AsyncTask>();
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      task->id = nextTaskId++;
      task->type = "Extract";
      task->description = "Extracting " + archiveName;
      task->destPath = destDir;
      activeTasks.push_back(task);
    }

    setStatus("Extraction task started in background");

    std::string pidFile = "/tmp/fyzenor_extract_" + std::to_string(task->id);
    std::string wrappedCmd = "cd " + escapeShellArg(destDir.string()) + " && (" + extractCmd + " & echo $! > " + pidFile + "; wait $!) > /dev/null 2>&1";

    std::weak_ptr<AsyncTask> weakTask = task;
    task->workerThread = std::thread([this, weakTask, wrappedCmd]() {
      auto task = weakTask.lock();
      if (!task) return;

      int res = system(wrappedCmd.c_str());
      (void)res;

      std::string pidFile = "/tmp/fyzenor_extract_" + std::to_string(task->id);
      try { fs::remove(pidFile); } catch(...) {}

      task->progress = 100;
      if (task->isCancelled.load()) {
        task->statusMessage = "Cancelled";
      } else {
        task->statusMessage = (res == 0) ? "Finished successfully" : "Failed with exit code " + std::to_string(res);
      }
      std::string logMsg = "[" + task->type + "] " + task->description + " - " + task->statusMessage;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskHistoryLogs.push_back(logMsg);
      }
      task->isFinished = true;
    });
  }

  void toggleTaskPause(std::shared_ptr<AsyncTask> task) {
    if (!task || task->isFinished) return;
    
    bool currentlyPaused = task->isPaused;
    task->isPaused = !currentlyPaused;
    
    if (task->type == "Zip" || task->type == "Extract") {
      std::string prefix = (task->type == "Zip") ? "zip" : "extract";
      std::string pidFile = "/tmp/fyzenor_" + prefix + "_" + std::to_string(task->id);
      try {
        if (fs::exists(pidFile)) {
          std::ifstream f(pidFile);
          pid_t pid;
          if (f >> pid) {
            if (task->isPaused) {
              kill(pid, SIGSTOP);
            } else {
              kill(pid, SIGCONT);
            }
          }
        }
      } catch (...) {}
    }
    
    if (!task->isPaused) {
      std::lock_guard<std::mutex> lock(task->pauseMutex);
      task->pauseCv.notify_all();
    }
    
    task->statusMessage = task->isPaused ? "Paused" : "Running";
  }

  void cancelTask(std::shared_ptr<AsyncTask> task) {
    if (!task || task->isFinished) return;
    task->isCancelled = true;

    if (task->isPaused) {
      task->isPaused = false;
      if (task->type == "Zip" || task->type == "Extract") {
        std::string prefix = (task->type == "Zip") ? "zip" : "extract";
        std::string pidFile = "/tmp/fyzenor_" + prefix + "_" + std::to_string(task->id);
        try {
          if (fs::exists(pidFile)) {
            std::ifstream f(pidFile);
            pid_t pid;
            if (f >> pid) {
              kill(pid, SIGCONT);
            }
          }
        } catch (...) {}
      }
      
      {
        std::lock_guard<std::mutex> lock(task->pauseMutex);
        task->pauseCv.notify_all();
      }
    }

    if (task->type == "Zip") {
      std::string pidFile = "/tmp/fyzenor_zip_" + std::to_string(task->id);
      std::ifstream f(pidFile);
      if (f.is_open()) {
        std::string pidStr;
        if (std::getline(f, pidStr)) {
          std::string killCmd = "kill -9 " + pidStr + " 2>/dev/null";
          int res = system(killCmd.c_str());
          (void)res;
        }
        f.close();
      }
      try { fs::remove(pidFile); } catch(...) {}
      if (!task->destPath.empty()) {
        try { fs::remove(task->destPath); } catch(...) {}
      }
    } else if (task->type == "Extract") {
      std::string pidFile = "/tmp/fyzenor_extract_" + std::to_string(task->id);
      std::ifstream f(pidFile);
      if (f.is_open()) {
        std::string pidStr;
        if (std::getline(f, pidStr)) {
          std::string killCmd = "kill -9 " + pidStr + " 2>/dev/null";
          int res = system(killCmd.c_str());
          (void)res;
        }
        f.close();
      }
      try { fs::remove(pidFile); } catch(...) {}
    }
  }

  void initColors() {
    ::initColors();
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
    showHidden = configShowHidden;
    hidePreview = configHidePreview;
    hideParent = configHideParent;
    if (configSortMode == "size") sortMode = SortMode::SIZE;
    else if (configSortMode == "date") sortMode = SortMode::DATE;
    else sortMode = SortMode::NAME;

    setlocale(LC_ALL, "");
    loadPins();
    loadCustomMacros();

    sizeWorker = std::thread(&FileManager::processSizeQueue, this);
    previewWorker = std::thread(&FileManager::processPreviewWorker, this);
    initInotify();

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
    Tab defTab;
    defTab.currentPath = currentPath;
    defTab.selectedIndex = 0;
    defTab.scrollOffset = 0;
    defTab.isSearching = false;
    defTab.isTrashMode = false;
    defTab.multiSelection = {};
    tabs.push_back(defTab);
    activeTabIndex = 0;

    loadDirectory(currentPath, currentFiles);
    loadParent();
    tabs[activeTabIndex].currentFiles = currentFiles;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(50);
    std::cout << "\033[?2004h" << std::flush;

    // Enable mouse tracking to prevent terminal text selection override and handle mouse scroll
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

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
    // 1. Signal cancellation to all active background tasks
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      for (auto& task : activeTasks) {
        cancelTask(task);
      }
    }

    // 2. Join task worker threads
    std::vector<std::shared_ptr<AsyncTask>> tasksToJoin;
    {
      std::lock_guard<std::mutex> lock(taskMutex);
      tasksToJoin = activeTasks;
    }
    for (auto& task : tasksToJoin) {
      if (task->workerThread.joinable()) {
        task->workerThread.join();
      }
    }

    stopWorker = true;
    queueCv.notify_all();
    previewCv.notify_all();

    cancelSearch();

    if (sizeWorker.joinable())
      sizeWorker.join();
    if (previewWorker.joinable())
      previewWorker.join();

    stopInotify = true;
    if (inotifyFd >= 0) {
      close(inotifyFd);
    }
    if (inotifyThread.joinable())
      inotifyThread.join();

    if (winPinned)
      delwin(winPinned);
    if (winParent)
      delwin(winParent);
    if (winCurrent)
      delwin(winCurrent);
    if (winPreview)
      delwin(winPreview);
    clearDirectRender();
    std::cout << "\033[?2004l" << std::flush;
    endwin();
  }

  // --- Auto-Update (Inotify) Functions ---
  void initInotify() {
    inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd < 0) {
      inotifyFd = inotify_init();
    }
    if (inotifyFd >= 0) {
      stopInotify = false;
      inotifyThread = std::thread(&FileManager::inotifyWorker, this);
    }
  }

  void inotifyWorker() {
    struct pollfd pfd;
    pfd.fd = inotifyFd;
    pfd.events = POLLIN;

    char buffer[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));

    while (!stopInotify) {
      int numEvents = poll(&pfd, 1, 200);
      if (numEvents < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (numEvents == 0) continue;

      if (pfd.revents & POLLIN) {
        ssize_t len = read(inotifyFd, buffer, sizeof(buffer));
        if (len < 0 && errno != EAGAIN) {
          break;
        }

        bool gotFsChange = false;
        bool gotDeviceChange = false;
        const struct inotify_event* event;
        for (char* ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
          event = reinterpret_cast<const struct inotify_event*>(ptr);
          if (event->mask & (IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM | IN_ATTRIB)) {
            bool isDevicePath = false;
            {
              std::lock_guard<std::mutex> lock(inotifyMutex);
              auto it = watchDescriptors.find(event->wd);
              if (it != watchDescriptors.end()) {
                const std::string& pathStr = it->second.string();
                if (pathStr.rfind("/media", 0) == 0 || pathStr.find("/gvfs") != std::string::npos) {
                  isDevicePath = true;
                }
              }
            }
            if (isDevicePath) {
              gotDeviceChange = true;
            } else {
              gotFsChange = true;
            }
          }
        }

        if (gotFsChange) {
          inotifyTriggered = true;
        }
        if (gotDeviceChange) {
          devicesTriggered = true;
        }
      }
    }
  }

  void updateInotifyWatches() {
    if (inotifyFd < 0) return;

    std::lock_guard<std::mutex> lock(inotifyMutex);

    std::vector<fs::path> pathsToWatch;
    if (isDualPaneMode) {
      if (leftTabIndex < tabs.size()) {
        pathsToWatch.push_back(tabs[leftTabIndex].currentPath);
      }
      if (rightTabIndex < tabs.size()) {
        pathsToWatch.push_back(tabs[rightTabIndex].currentPath);
      }
    } else {
      pathsToWatch.push_back(currentPath);
      if (currentPath.has_parent_path() && currentPath != currentPath.parent_path()) {
        pathsToWatch.push_back(currentPath.parent_path());
      }
    }

    // Add device paths (media & gvfs) to monitor mounts live
    uid_t uid = geteuid();
    fs::path gvfsPath = "/run/user/" + std::to_string(uid) + "/gvfs";
    if (fs::exists(gvfsPath)) {
      pathsToWatch.push_back(gvfsPath);
    }
    fs::path mediaPath = "/media";
    if (fs::exists(mediaPath)) {
      pathsToWatch.push_back(mediaPath);
      const char* user = getenv("USER");
      if (user) {
        fs::path userMediaPath = mediaPath / user;
        if (fs::exists(userMediaPath)) {
          pathsToWatch.push_back(userMediaPath);
        }
      }
    }

    std::sort(pathsToWatch.begin(), pathsToWatch.end());
    pathsToWatch.erase(std::unique(pathsToWatch.begin(), pathsToWatch.end()), pathsToWatch.end());

    for (auto const& [wd, path] : watchDescriptors) {
      inotify_rm_watch(inotifyFd, wd);
    }
    watchDescriptors.clear();

    for (const auto& path : pathsToWatch) {
      try {
        if (fs::exists(path) && fs::is_directory(path)) {
          int wd = inotify_add_watch(inotifyFd, path.string().c_str(),
                                     IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM | IN_ATTRIB);
          if (wd >= 0) {
            watchDescriptors[wd] = path;
          }
        }
      } catch (...) {}
    }
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

      if (job.path.string().find("/gvfs/") != std::string::npos) {
        {
          std::lock_guard<std::mutex> lock(cacheMutex);
          dirSizeCache[job.path.string()] = 0;
        }
        continue;
      }

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
  void loadCustomMacros() {
    customMacros.clear();
    std::string homeDir = getenv("HOME") ? getenv("HOME") : "";
    if (homeDir.empty()) return;

    std::string configPath = homeDir + "/.config/fyzenor/keys.toml";
    try {
      fs::create_directories(homeDir + "/.config/fyzenor");
    } catch (...) {}

    if (!fs::exists(configPath)) {
      std::ofstream df(configPath);
      df << "# Fyzenor Custom Keys Macro Configuration\n"
         << "# Macros allow you to execute shell command shortcuts using single keystrokes.\n"
         << "# Use single quotes for command strings in TOML.\n"
         << "# Place them under the [macros] section.\n"
         << "#   $f - expands to the currently highlighted file's absolute path\n"
         << "#   $s - expands to space-separated paths of all selected files\n\n"
         << "[macros]\n"
         << "v = 'nvim \"$f\"'\n"
         << "g = 'git status'\n"
         << "l = 'ls -la'\n";
    }

    std::ifstream f(configPath);
    if (!f.is_open()) return;

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

      size_t eq = line.find('=');
      if (eq != std::string::npos && eq > 0) {
        std::string keyPart = line.substr(0, eq);
        std::string cmdPart = line.substr(eq + 1);

        auto k_first = keyPart.find_first_not_of(" \t");
        if (k_first != std::string::npos) {
          auto k_last = keyPart.find_last_not_of(" \t");
          keyPart = keyPart.substr(k_first, k_last - k_first + 1);
        }
        auto c_first = cmdPart.find_first_not_of(" \t");
        if (c_first != std::string::npos) {
          auto c_last = cmdPart.find_last_not_of(" \t");
          cmdPart = cmdPart.substr(c_first, c_last - c_first + 1);
        }

        if (!cmdPart.empty() && cmdPart.front() == '"' && cmdPart.back() == '"') {
          cmdPart = cmdPart.substr(1, cmdPart.length() - 2);
        } else if (!cmdPart.empty() && cmdPart.front() == '\'' && cmdPart.back() == '\'') {
          cmdPart = cmdPart.substr(1, cmdPart.length() - 2);
        }

        if (section == "macros") {
          if (keyPart.length() == 1 && !cmdPart.empty()) {
            int keyChar = keyPart[0];
            customMacros[keyChar] = cmdPart;
          }
        }
      }
    }
  }

  void executeMacro(const std::string& rawCmd) {
    if (rawCmd.empty()) return;

    std::string cmd = rawCmd;
    std::string currentFile = "";
    if (!currentFiles.empty() && selectedIndex < currentFiles.size()) {
      currentFile = currentFiles[selectedIndex].path.string();
    }

    size_t pos;
    while ((pos = cmd.find("$f")) != std::string::npos) {
      cmd.replace(pos, 2, "\"" + currentFile + "\"");
    }

    if (cmd.find("$s") != std::string::npos) {
      std::string selList = "";
      if (!multiSelection.empty()) {
        for (const auto& p : multiSelection) {
          selList += "\"" + p.string() + "\" ";
        }
        if (!selList.empty()) selList.pop_back();
      } else {
        selList = "\"" + currentFile + "\"";
      }
      while ((pos = cmd.find("$s")) != std::string::npos) {
        cmd.replace(pos, 2, selList);
      }
    }

    suspendTerminal();
    
    std::system("clear");
    std::cout << "\033[1;36m[Fyzenor Macro] Running: " << cmd << "\033[0m\n\n";
    
    std::string runCmd = "cd " + escapeShellArg(currentPath.string()) + " && " + cmd;
    int code = std::system(runCmd.c_str());
    (void)code;
    
    std::cout << "\n\033[1;30mPress Enter to return to Fyzenor...\033[0m";
    std::cin.get();
    
    resumeTerminal();
    clear();
    reloadAll();
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
      changeDirectory(target, true);
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

  // Unified Sorting Logic: Folders Top -> Size/Date/Name
  void sortList(std::vector<FileEntry>& list) {
    std::sort(list.begin(), list.end(), [this](const FileEntry& a, const FileEntry& b) {
      // 1. Always keep directories on top
      if (a.is_directory != b.is_directory) {
        return a.is_directory > b.is_directory;
      }

      // 2. Sort by Mode
      if (sortMode == SortMode::SIZE) {
        if (a.size != b.size)
          return a.size > b.size; // Descending
      } else if (sortMode == SortMode::DATE) {
        if (a.modified_time != b.modified_time)
          return a.modified_time > b.modified_time; // Descending (newest first)
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
      if (isTrashMode) {
        std::vector<fs::path> trashDirs = getAllTrashFilesPaths();
        for (const auto& trashDir : trashDirs) {
          if (fs::exists(trashDir) && fs::is_directory(trashDir)) {
            for (const auto& entry : fs::directory_iterator(trashDir)) {
              if (!showHidden && entry.path().filename().string().front() == '.')
                continue;
              FileEntry fe(entry);
              TrashInfo ti = getTrashInfo(entry.path());
              if (!ti.originalPath.empty()) {
                fe.name = fs::path(ti.originalPath).filename().string();
                fe.extension = fs::path(ti.originalPath).extension().string();
              }
              target.push_back(fe);
            }
          }
        }
      } else {
        for (const auto& entry : fs::directory_iterator(path)) {
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;
          target.emplace_back(entry);
        }
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
          if (entry.path.string().find("/gvfs/") != std::string::npos) {
            entry.size = 0;
            continue;
          }
          auto it = dirSizeCache.find(entry.path.string());
          if (it != dirSizeCache.end()) {
            entry.size = it->second;
          } else {
            entry.size = 0;
            if (sortMode == SortMode::SIZE) {
              sizeQueue.push_back({entry.path, currentViewId.load()});
            }
          }
        }
      }
    }

    // Initial Sort
    sortList(target);

    queueCv.notify_one();
  }

  void adjustLeftPane() {
    if (isDualPaneMode) return;
    if (!winPinned || !winParent) return;

    int w1 = static_cast<int>(width * 0.18);
    int hPinned = 0;
    int hParent = 0;

    if (parentFiles.empty()) {
      hPinned = height - 2;
      hParent = 1;
    } else {
      hPinned = (height - 2) / 3;
      hParent = (height - 2) - hPinned;
    }

    wresize(winPinned, hPinned, w1);
    mvwin(winPinned, 1, 0);

    wresize(winParent, hParent, w1);
    mvwin(winParent, parentFiles.empty() ? height - 1 : 1 + hPinned, 0);
  }

  void loadParent() {
    if (isTrashMode) {
      parentFiles.clear();
      adjustLeftPane();
      return;
    }
    if (currentPath.has_parent_path() && currentPath != currentPath.parent_path()) {
      parentFiles.clear();
      try {
        for (const auto& entry : fs::directory_iterator(currentPath.parent_path())) {
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;
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
    adjustLeftPane();
  }

  void suspendTerminal() {
    clearDirectRender();
    std::cout << "\033[?2004l" << std::flush;
    def_prog_mode();
    endwin();
  }

  void resumeTerminal() {
    reset_prog_mode();
    updateLayout();
    refresh();
    std::cout << "\033[?2004h" << std::flush;
    timeout(50);
  }

  void updateLayout() {
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, height, width);

    if (isDualPaneMode) {
      int w1 = width / 2 + dualPaneSplitOffset;
      int minCols = 20;
      if (w1 < minCols) {
        w1 = minCols;
        dualPaneSplitOffset = w1 - width / 2;
      }
      int w2 = width - w1;
      if (w2 < minCols) {
        w2 = minCols;
        w1 = width - w2;
        dualPaneSplitOffset = w1 - width / 2;
      }

      if (winPinned) { delwin(winPinned); winPinned = nullptr; }
      if (winParent) { delwin(winParent); winParent = nullptr; }
      if (winCurrent) delwin(winCurrent);
      if (winPreview) delwin(winPreview);

      winCurrent = newwin(height - 2, w1, 1, 0);
      winPreview = newwin(height - 2, w2, 1, w1);

      refresh();
      return;
    }

    // Adjusted widths dynamically from layout configuration values and visibility states
    int w1 = hideParent ? 0 : static_cast<int>(width * configParentWidth);
    int w3 = hidePreview ? 0 : (width - w1 - static_cast<int>(width * configCurrentWidth));
    if (w3 < 0) w3 = 0;
    int w2 = width - w1 - w3;

    if (winPinned) { delwin(winPinned); winPinned = nullptr; }
    if (winParent) { delwin(winParent); winParent = nullptr; }
    if (winCurrent) { delwin(winCurrent); winCurrent = nullptr; }
    if (winPreview) { delwin(winPreview); winPreview = nullptr; }

    int hPinned = 0;
    int hParent = 0;
    if (!hideParent) {
      if (parentFiles.empty()) {
        hPinned = height - 2;
        hParent = 1;
      } else {
        hPinned = (height - 2) / 3;
        hParent = (height - 2) - hPinned;
      }
      winPinned = newwin(hPinned, w1, 1, 0);
      winParent = newwin(hParent, w1, parentFiles.empty() ? height - 1 : 1 + hPinned, 0);
    }

    winCurrent = newwin(height - 2, w2, 1, w1);

    if (!hidePreview) {
      winPreview = newwin(height - 2, w3, 1, w1 + w2);
    }

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

        if (cachePath == "/tmp/fm_preview_thumb.png") {
          try {
            fs::remove(cachePath);
          } catch (...) {}
        }

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

        if (!fs::exists(cachePath)) {
          continue;
        }

        int finalW = 0, finalH = 0;
        std::string probeCmd = "ffprobe -v error -select_streams v:0 -show_entries "
                               "stream=width,height -of csv=s=x:p=0 \"" +
                               cachePath + "\" 2>/dev/null";
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
        std::string ext = fs::path(job->path).extension().string();
        for (auto& c : ext) c = tolower(c);

        bool isArchive = (ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".tgz" || 
                          ext == ".rar" || ext == ".bz2" || ext == ".xz" || ext == ".7z");
        
        bool isAudio = (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || 
                        ext == ".m4a" || ext == ".aac" || ext == ".opus" || ext == ".wma");

        if (isArchive) {
          std::string archiveCmd;
          if (ext == ".zip") {
            archiveCmd = "unzip -l \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (ext == ".7z") {
            archiveCmd = "7z l \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (ext == ".rar") {
            archiveCmd = "unrar l \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (ext == ".tar") {
            archiveCmd = "tar -tf \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (ext == ".gz" || ext == ".tgz") {
            archiveCmd = "tar -ztf \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (ext == ".bz2") {
            archiveCmd = "tar -jtf \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (ext == ".xz") {
            archiveCmd = "tar -Jtf \"" + job->path + "\" 2>/dev/null | head -n 40";
          }

          lines.push_back("\033[1;36mArchive Contents:\033[0m");
          lines.push_back("--------------------------------");
          if (!archiveCmd.empty()) {
            FILE* pipe = popen(archiveCmd.c_str(), "r");
            if (pipe) {
              char buf[4096];
              bool hasData = false;
              while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                if (job->reqId != requestID || stopWorker)
                  break;
                std::string ln(buf);
                if (!ln.empty() && ln.back() == '\n') ln.pop_back();
                lines.push_back(ln);
                hasData = true;
              }
              pclose(pipe);
              if (!hasData) {
                lines.push_back("(No list tool available or empty archive)");
              }
            } else {
              lines.push_back("(Failed to run preview command)");
            }
          }
        } else if (isAudio) {
          std::string mediaCmd;
          if (isCommandAvailable("mediainfo")) {
            mediaCmd = "mediainfo \"" + job->path + "\" 2>/dev/null | head -n 40";
          } else if (isCommandAvailable("ffprobe")) {
            mediaCmd = "ffprobe -v error -show_format -show_streams \"" + job->path + "\" 2>/dev/null | grep -E \"codec_name|duration|bit_rate|width|height|sample_rate|channels|title|artist\" | head -n 40";
          }

          lines.push_back("\033[1;35mMedia Info Metadata:\033[0m");
          lines.push_back("--------------------------------");
          if (!mediaCmd.empty()) {
            FILE* pipe = popen(mediaCmd.c_str(), "r");
            if (pipe) {
              char buf[4096];
              bool hasData = false;
              while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                if (job->reqId != requestID || stopWorker)
                  break;
                std::string ln(buf);
                if (!ln.empty() && ln.back() == '\n') ln.pop_back();
                lines.push_back(ln);
                hasData = true;
              }
              pclose(pipe);
              if (!hasData) {
                lines.push_back("(Failed to read stream metadata)");
              }
            } else {
              lines.push_back("(Failed to run media probe)");
            }
          } else {
            lines.push_back("(Install 'mediainfo' or 'ffmpeg' for full metadata previews)");
          }
        } else if (ext == ".pdf") {
          if (isCommandAvailable("pdftotext")) {
            std::string pdfCmd = "pdftotext -layout -l 3 \"" + job->path + "\" - 2>/dev/null | head -n 40";
            lines.push_back("\033[1;32mPDF Document Preview (First 3 Pages):\033[0m");
            lines.push_back("--------------------------------");
            FILE* pipe = popen(pdfCmd.c_str(), "r");
            if (pipe) {
              char buf[4096];
              bool hasData = false;
              while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                if (job->reqId != requestID || stopWorker)
                  break;
                std::string ln(buf);
                if (!ln.empty() && ln.back() == '\n') ln.pop_back();
                lines.push_back(ln);
                hasData = true;
              }
              pclose(pipe);
              if (!hasData) {
                lines.push_back("(Empty or encrypted PDF document)");
              }
            } else {
              lines.push_back("(Failed to read PDF document)");
            }
          } else {
            lines.push_back(" \033[1;31m[PDF File - No Preview]\033[0m ");
            lines.push_back(" (Install 'poppler-utils' / 'pdftotext' to view text preview) ");
          }
        } else if (is_binary_file(job->path)) {
          lines.push_back("\033[1;31m[Binary File]\033[0m");
        } else {
          if (job->reqId != requestID)
            continue;

          bool gotOutput = false;
          std::string cmd;
          if (isCommandAvailable("bat")) {
            cmd = "bat --color=always --style=plain --paging=never "
                  "--wrap=character --line-range=:" +
                  std::to_string(job->previewHeight * 2) + " \"" + job->path + "\" 2>/dev/null";
          } else if (isCommandAvailable("batcat")) {
            cmd = "batcat --color=always --style=plain --paging=never "
                  "--wrap=character --line-range=:" +
                  std::to_string(job->previewHeight * 2) + " \"" + job->path + "\" 2>/dev/null";
          }

          if (!cmd.empty()) {
            FILE* pipe = popen(cmd.c_str(), "r");
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

  int getPreviewContentStartLine() {
    if (currentFiles.empty() || selectedIndex >= currentFiles.size()) {
      return 6;
    }
    if (isTrashMode) {
      return 8;
    }
    return currentFiles[selectedIndex].is_symlink ? 7 : 6;
  }

  void sendKittyGraphics(const std::string& b64Data, int pY, int pX, int cols, int rows,
                         int offX = 0, int offY = 0, int startRow = 8) {
    // Move cursor to start of preview area (1-indexed for terminal)
    // pY+1 is the start of the window, we have startRow lines of header/padding +
    // offY.
    std::cout << "\033[" << (pY + startRow + offY) << ";" << (pX + 3 + offX) << "H";
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

      int imgStartRow = getPreviewContentStartLine() + 1;
      int boxW = pW - 4;
      int boxH = pH - (imgStartRow + 1);

      int offX = (boxW - cols) / 2;
      int offY = (boxH - rows) / 2;
      if (offX < 0)
        offX = 0;
      if (offY < 0)
        offY = 0;

      sendKittyGraphics(cachedBase64, pY, pX, cols, rows, offX, offY, imgStartRow);
      lastWasDirectRender = true;
    }
  }

  void drawCachedTextPreview() {
    std::lock_guard<std::mutex> lock(previewMutex);
    if (cachedTextLines.empty())
      return;

    int maxW = getmaxx(winPreview) - 4;
    int startLine = getPreviewContentStartLine();
    int limit = getmaxy(winPreview) - startLine - 2;
    int lineLimit = std::min((int)cachedTextLines.size(), limit);

    for (int i = 0; i < lineLimit; ++i) {
      wprintw_ansi(winPreview, startLine + i, 2, cachedTextLines[i], maxW);
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
      int limit = w - 7;
      if (limit < 1) limit = 1;
      dispMsg = utf8_safe_truncate(dispMsg, limit);
    }

    mvwprintw(toastWin, 1, 2, "%s", dispMsg.c_str());
    wnoutrefresh(toastWin);
    delwin(toastWin);
  }

  std::string promptInput(const std::string& prompt, const std::string& defaultVal = "") {
    clearDirectRender();
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

    // Draw divider line and prompt indicator to make input field look premium
    wattron(win, COLOR_PAIR(6) | A_DIM);
    std::string separator = "";
    for (int i = 0; i < w - 2; ++i) {
      separator += "─";
    }
    mvwprintw(win, 2, 1, "%s", separator.c_str());
    wattroff(win, COLOR_PAIR(6) | A_DIM);

    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 3, 2, " ❯ ");
    wattroff(win, COLOR_PAIR(1) | A_BOLD);

    std::string input = defaultVal;
    int cursorIdx = defaultVal.length();
    int inputFieldX = 5;
    int inputFieldY = 3;
    int maxInputW = w - 7;

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
    if (!multiSelection.empty()) {
      multiSelection.clear();
      setStatus("Cleared selection");
    } else if (!clipboard.paths.empty()) {
      clipboard.paths.clear();
      setStatus("Cleared clipboard selection");
    } else {
      setStatus("Nothing to clear");
    }
  }

  void toggleSort() {
    if (sortMode == SortMode::NAME) {
      sortMode = SortMode::SIZE;
      setStatus("Sorted by Size (Desc)");
      reloadAll();
    } else if (sortMode == SortMode::SIZE) {
      sortMode = SortMode::DATE;
      setStatus("Sorted by Date (Desc)");
      sortList(currentFiles);
    } else {
      sortMode = SortMode::NAME;
      setStatus("Sorted by Name");
      sortList(currentFiles);
    }
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
    if (query.empty()) return true;
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

  void handleSearch() {
    if (!isCommandAvailable("rg")) {
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
      std::string cmd = "rg --files-with-matches --smart-case --hidden --glob \"!.git\" " +
                        escapeShellArg(query) + " " +
                        escapeShellArg(currentPath.string()) + " 2>/dev/null";
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

    std::vector<std::pair<fs::path, fs::path>> jobs;

    for (const auto& src : clipboard.paths) {
      fs::path dest = currentPath / src.filename();
      if (fs::is_directory(src) && isDescendant(dest, src)) {
        setStatus("Error: Cannot copy directory into itself");
        continue;
      }
      if (fs::exists(dest)) {
        if (clipboard.isCut && src == dest) {
          continue;
        }
        std::string filename = src.filename().string();
        if (filename.length() > 30) {
          filename = utf8_safe_truncate(filename, 27);
        }
        std::string promptStr = "'" + filename + "' exists. [r]eplace, [k]eep both, [c]ancel";
        std::string ans = promptInput(promptStr);
        char choice = 'c';
        if (!ans.empty()) {
          choice = std::tolower(ans[0]);
        }
        
        if (choice == 'r') {
          if (src == dest) {
            jobs.push_back({src, dest});
            continue;
          }
          jobs.push_back({src, dest});
        } else if (choice == 'k') {
          jobs.push_back({src, getNonConflictingPath(dest)});
        } else {
          continue;
        }
      } else {
        jobs.push_back({src, dest});
      }
    }

    if (jobs.empty()) return;

    startPasteTask(jobs, clipboard.isCut);

    if (clipboard.isCut) {
      clipboard.paths.clear();
    }
  }

  void handlePasteSymlink() {
    if (clipboard.paths.empty()) {
      setStatus("Clipboard empty");
      return;
    }

    std::vector<std::pair<fs::path, fs::path>> jobs;

    for (const auto& src : clipboard.paths) {
      fs::path dest = currentPath / src.filename();
      if (fs::exists(dest)) {
        std::string filename = src.filename().string();
        if (filename.length() > 30) {
          filename = utf8_safe_truncate(filename, 27);
        }
        std::string promptStr = "'" + filename + "' exists. [r]eplace, [k]eep both, [c]ancel";
        std::string ans = promptInput(promptStr);
        char choice = 'c';
        if (!ans.empty()) {
          choice = std::tolower(ans[0]);
        }
        
        if (choice == 'r') {
          try {
            fs::remove_all(dest);
          } catch (...) {
            setStatus("Error: Failed to replace " + dest.filename().string());
            continue;
          }
          jobs.push_back({src, dest});
        } else if (choice == 'k') {
          jobs.push_back({src, getNonConflictingPath(dest)});
        } else {
          continue;
        }
      } else {
        jobs.push_back({src, dest});
      }
    }

    if (jobs.empty()) return;

    int successCount = 0;
    int failCount = 0;
    for (const auto& job : jobs) {
      fs::path src = job.first;
      fs::path dest = job.second;
      try {
        fs::path src_abs = fs::absolute(src);
        if (fs::is_directory(src_abs)) {
          fs::create_directory_symlink(src_abs, dest);
        } else {
          fs::create_symlink(src_abs, dest);
        }
        successCount++;
      } catch (...) {
        failCount++;
      }
    }

    if (failCount > 0) {
      setStatus("Symlinked " + std::to_string(successCount) + " items (" + std::to_string(failCount) + " failed)");
    } else {
      setStatus("Created " + std::to_string(successCount) + " symlinks");
    }

    reloadAll();
  }

  void handleShellCommand() {
    std::string cmd = promptInput("Shell");
    if (cmd.empty())
      return;

    bool runInBackground = false;
    std::string trimmed = cmd;
    while (!trimmed.empty() && std::isspace(trimmed.back())) {
      trimmed.pop_back();
    }
    if (!trimmed.empty() && trimmed.back() == '&') {
      trimmed.pop_back();
      while (!trimmed.empty() && std::isspace(trimmed.back())) {
        trimmed.pop_back();
      }
      runInBackground = true;
    }

    std::string currentFileEscaped = "";
    if (!currentFiles.empty() && selectedIndex < currentFiles.size()) {
      currentFileEscaped = escapeShellArg(currentFiles[selectedIndex].path.string());
    }

    std::string allSelectedEscaped = "";
    if (!multiSelection.empty()) {
      for (const auto& p : multiSelection) {
        if (!allSelectedEscaped.empty()) {
          allSelectedEscaped += " ";
        }
        allSelectedEscaped += escapeShellArg(p.string());
      }
    } else {
      allSelectedEscaped = currentFileEscaped;
    }

    std::string finalCmd = "";
    for (size_t i = 0; i < trimmed.length(); ++i) {
      if (i + 1 < trimmed.length() && trimmed[i] == '$' && trimmed[i + 1] == 'f') {
        finalCmd += currentFileEscaped;
        i++;
      } else if (i + 1 < trimmed.length() && trimmed[i] == '$' && trimmed[i + 1] == 's') {
        finalCmd += allSelectedEscaped;
        i++;
      } else {
        finalCmd += trimmed[i];
      }
    }

    if (runInBackground) {
      auto task = std::make_shared<AsyncTask>();
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        task->id = nextTaskId++;
        task->type = "Shell";
        task->description = "cmd: " + trimmed;
        activeTasks.push_back(task);
      }

      setStatus("Shell task started in background");
      std::weak_ptr<AsyncTask> weakTask = task;
      task->workerThread = std::thread([this, weakTask, finalCmd]() {
        auto task = weakTask.lock();
        if (!task) return;

        std::string runCmd = "cd " + escapeShellArg(currentPath.string()) + " && (" + finalCmd + ") > /dev/null 2>&1";
        int res = system(runCmd.c_str());
        
        task->isFinished = true;
        if (res == 0) {
          task->statusMessage = "Success";
        } else {
          task->statusMessage = "Failed (exit code " + std::to_string(res) + ")";
        }
      });
    } else {
      suspendTerminal();
      
      std::cout << "\033[H\033[J";
      std::cout << "Executing: " << finalCmd << "\n\n";
      
      std::string runCmd = "cd " + escapeShellArg(currentPath.string()) + " && " + finalCmd;
      int res = system(runCmd.c_str());
      
      std::cout << "\nCommand exited with code: " << res << "\n";
      std::cout << "Press Enter to return to Fyzenor...";
      std::string dummy;
      std::getline(std::cin, dummy);
      
      resumeTerminal();
      reloadAll();
    }
  }

  void handleRename() {
    if (currentFiles.empty())
      return;

    if (!multiSelection.empty()) {
      // Perform bulk rename!
      std::vector<fs::path> selectedPaths;
      for (const auto& p : multiSelection) {
        selectedPaths.push_back(p);
      }
      
      std::sort(selectedPaths.begin(), selectedPaths.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().string() < b.filename().string();
      });

      fs::path tempFile = "/tmp/fyzenor_bulk_rename.txt";
      std::ofstream out(tempFile);
      if (!out.is_open()) {
        setStatus("Error: Failed to create temp file for rename");
        return;
      }
      for (const auto& p : selectedPaths) {
        out << p.filename().string() << "\n";
      }
      out.close();

      suspendTerminal();

      const char* editor = getenv("EDITOR");
      if (!editor)
        editor = getenv("VISUAL");
      if (!editor) {
        if (isCommandAvailable("nvim"))
          editor = "nvim";
        else if (isCommandAvailable("nano"))
          editor = "nano";
        else
          editor = "vi";
      }

      std::string cmd = std::string(editor) + " " + escapeShellArg(tempFile.string());
      int res = system(cmd.c_str());
      (void)res;

      resumeTerminal();

      std::ifstream in(tempFile);
      if (!in.is_open()) {
        setStatus("Error: Failed to read temp file after editing");
        return;
      }

      std::vector<std::string> newNames;
      std::string line;
      while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || std::isspace(line.back()))) {
          line.pop_back();
        }
        if (!line.empty()) {
          newNames.push_back(line);
        }
      }
      in.close();
      try { fs::remove(tempFile); } catch(...) {}

      if (newNames.size() != selectedPaths.size()) {
        setStatus("Bulk rename aborted: Line count mismatch (" + std::to_string(newNames.size()) + " vs " + std::to_string(selectedPaths.size()) + ")");
        return;
      }

      int successCount = 0;
      int failCount = 0;
      for (size_t idx = 0; idx < selectedPaths.size(); ++idx) {
        fs::path src = selectedPaths[idx];
        std::string newName = newNames[idx];
        if (newName.empty() || newName == src.filename().string()) {
          continue;
        }
        fs::path dest = src.parent_path() / newName;
        if (fs::exists(dest)) {
          failCount++;
          continue;
        }
        try {
          fs::rename(src, dest);
          successCount++;
        } catch (...) {
          failCount++;
        }
      }

      multiSelection.clear();
      reloadAll();

      if (failCount > 0) {
        setStatus("Renamed " + std::to_string(successCount) + " files (" + std::to_string(failCount) + " failed)");
      } else {
        setStatus("Bulk renamed " + std::to_string(successCount) + " files");
      }
      return;
    }

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
    if (!isCommandAvailable("zip")) {
      setStatus("Error: 'zip' utility is not installed/available");
      return;
    }
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
    std::string cmd = "zip -r -q " + escapeShellArg(name + ".zip");
    for (const auto& p : targets)
      cmd += " " + escapeShellArg(p.filename().string());

    startZipTask(cmd, name + ".zip", currentPath);
    multiSelection.clear();
  }

  void handleExtract() {
    if (currentFiles.empty())
      return;
    const auto& file = currentFiles[selectedIndex];
    if (file.is_directory) {
      setStatus("Error: Cannot extract a directory!");
      return;
    }
    
    std::string extractCmd;
    std::string requiredTool;
    auto getExtractCommand = [this, &requiredTool](const fs::path& archivePath, const fs::path& destDir, std::string& cmd) -> bool {
      std::string ext = archivePath.extension().string();
      std::string pathStr = archivePath.string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

      bool isTarGz = (pathStr.length() > 7 && pathStr.substr(pathStr.length() - 7) == ".tar.gz") ||
                     (pathStr.length() > 4 && pathStr.substr(pathStr.length() - 4) == ".tgz");
      bool isTarBz2 = (pathStr.length() > 8 && pathStr.substr(pathStr.length() - 8) == ".tar.bz2") ||
                      (pathStr.length() > 5 && pathStr.substr(pathStr.length() - 5) == ".tbz2");
      bool isTarXz = (pathStr.length() > 7 && pathStr.substr(pathStr.length() - 7) == ".tar.xz") ||
                     (pathStr.length() > 4 && pathStr.substr(pathStr.length() - 4) == ".txz");

      if (ext == ".zip") {
        requiredTool = "unzip";
        cmd = "unzip -q " + escapeShellArg(archivePath.string()) + " -d " + escapeShellArg(destDir.string());
        return true;
      } else if (ext == ".tar" || isTarGz || isTarBz2 || isTarXz) {
        requiredTool = "tar";
        cmd = "tar -xf " + escapeShellArg(archivePath.string()) + " -C " + escapeShellArg(destDir.string());
        return true;
      } else if (ext == ".7z") {
        requiredTool = "7z";
        cmd = "7z x -y " + escapeShellArg(archivePath.string()) + " -o" + escapeShellArg(destDir.string());
        return true;
      } else if (ext == ".rar") {
        requiredTool = "unrar";
        cmd = "unrar x -y " + escapeShellArg(archivePath.string()) + " " + escapeShellArg(destDir.string());
        return true;
      }
      return false;
    };

    if (!getExtractCommand(file.path, currentPath, extractCmd)) {
      setStatus("Error: Unsupported archive format!");
      return;
    }

    if (!isCommandAvailable(requiredTool)) {
      setStatus("Error: '" + requiredTool + "' utility is not installed/available");
      return;
    }

    std::string confirm = promptInput("Extract " + file.name + " here? (y/n)");
    if (confirm == "y" || confirm == "Y") {
      startExtractTask(extractCmd, file.name, currentPath);
    }
  }

  void handleCopyPath() {
    if (currentFiles.empty())
      return;
    std::string path = fs::absolute(currentFiles[selectedIndex].path).string();
    std::string cmd = "echo -n " + escapeShellArg(path) +
                      " | (wl-copy 2>/dev/null || xclip -selection clipboard "
                      "2>/dev/null || pbcopy 2>/dev/null)";
    int res = system(cmd.c_str());
    (void)res;
    setStatus("Copied path");
  }

  void handleDragOut() {
    if (currentFiles.empty())
      return;

    std::string tool = "";
    if (isCommandAvailable("ripdrag")) {
      tool = "ripdrag";
    } else if (isCommandAvailable("dragon-drag-and-drop")) {
      tool = "dragon-drag-and-drop";
    } else if (isCommandAvailable("dragon")) {
      tool = "dragon";
    }

    if (tool.empty()) {
      setStatus("Error: 'ripdrag' or 'dragon' is not installed");
      return;
    }

    std::vector<fs::path> targets;
    if (!multiSelection.empty()) {
      for (const auto& p : multiSelection) {
        targets.push_back(p);
      }
    } else {
      targets.push_back(currentFiles[selectedIndex].path);
    }

    std::string cmd = tool + " -x";
    for (const auto& p : targets) {
      cmd += " " + escapeShellArg(fs::absolute(p).string());
    }
    cmd += " >/dev/null 2>&1";

    setStatus("Drag initiated. Drag the file from the pop-up window.");

    std::thread([cmd]() {
      int res = system(cmd.c_str());
      (void)res;
    }).detach();
  }

  void handlePastedData(const std::string& data) {
    std::vector<fs::path> paths = parsePastedPaths(data);
    if (paths.empty()) {
      return;
    }

    int h = 7;
    int w = 60;
    int startY = (height - h) / 2;
    int startX = (width - w) / 2;
    WINDOW* promptWin = newwin(h, w, startY, startX);
    if (!promptWin) return;

    keypad(promptWin, TRUE);
    wattron(promptWin, COLOR_PAIR(6) | A_BOLD);
    drawRoundedBox(promptWin);
    wattroff(promptWin, COLOR_PAIR(6) | A_BOLD);

    wattron(promptWin, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(promptWin, 1, 2, "󰗘 Dropped File(s) Detected");
    wattroff(promptWin, COLOR_PAIR(1) | A_BOLD);

    std::string countStr = (paths.size() > 1) ? std::to_string(paths.size()) + " items" : paths[0].filename().string();
    if (countStr.length() > (size_t)(w - 15)) {
      countStr = countStr.substr(0, w - 18) + "...";
    }
    mvwprintw(promptWin, 3, 2, "Source: %s", countStr.c_str());
    mvwprintw(promptWin, 5, 2, "[c] Copy here  [m] Move here  [Esc] Cancel");

    wrefresh(promptWin);

    int choice = 0;
    while (true) {
      int ch = wgetch(promptWin);
      if (ch == 'c' || ch == 'C') {
        choice = 'c';
        break;
      } else if (ch == 'm' || ch == 'M') {
        choice = 'm';
        break;
      } else if (ch == 27) { // Esc
        choice = 'e';
        break;
      }
    }
    delwin(promptWin);

    if (choice == 'c' || choice == 'm') {
      std::vector<std::pair<fs::path, fs::path>> jobs;
      for (const auto& p : paths) {
        jobs.push_back({p, currentPath / p.filename()});
      }
      startPasteTask(jobs, choice == 'm');
    }
    reloadAll();
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
    
    std::string confirmMsg = isTrashMode ? "Permanently delete " + countStr + " from Trash? (y/n)"
                                         : "Delete " + countStr + " permanently? (y/n)";
    std::string confirm = promptInput(confirmMsg);
    if (confirm != "y" && confirm != "Y")
      return;

    if (isTrashMode) {
      const char* home = std::getenv("HOME");
      if (home) {
        for (const auto& p : targets) {
          fs::path infoFile = fs::path(home) / ".local/share/Trash/info" / (p.filename().string() + ".trashinfo");
          try {
            if (fs::exists(infoFile)) {
              fs::remove(infoFile);
            }
          } catch (...) {}
        }
      }
    }

    startDeleteTask(targets);
    multiSelection.clear();
  }

  struct TrashInfo {
    std::string originalPath;
    std::string deletionDate;
  };

  TrashInfo getTrashInfo(const fs::path& trashFile) {
    TrashInfo info;
    try {
      fs::path infoDir = trashFile.parent_path().parent_path() / "info";
      fs::path infoFile = infoDir / (trashFile.filename().string() + ".trashinfo");
      if (fs::exists(infoFile)) {
        std::ifstream f(infoFile);
        std::string line;
        while (std::getline(f, line)) {
          if (line.rfind("Path=", 0) == 0) {
            info.originalPath = line.substr(5);
          } else if (line.rfind("DeletionDate=", 0) == 0) {
            info.deletionDate = line.substr(13);
            size_t tPos = info.deletionDate.find('T');
            if (tPos != std::string::npos) {
              info.deletionDate[tPos] = ' ';
            }
          }
        }
      }
    } catch (...) {}
    return info;
  }

  fs::path getTrashFilesPath() {
    const char* home = std::getenv("HOME");
    if (home) {
      return fs::path(home) / ".local/share/Trash/files";
    }
    return "";
  }

  fs::path getMountPoint(const fs::path& path) {
    try {
      fs::path absPath = fs::absolute(path);
      struct stat st;
      if (stat(absPath.c_str(), &st) != 0) {
        return "/";
      }
      dev_t devId = st.st_dev;

      fs::path current = absPath;
      while (current.has_parent_path() && current != current.parent_path()) {
        fs::path parent = current.parent_path();
        struct stat pst;
        if (stat(parent.c_str(), &pst) == 0) {
          if (pst.st_dev != devId) {
            return current;
          }
        } else {
          break;
        }
        current = parent;
      }
      return current;
    } catch (...) {
      return "/";
    }
  }

  std::vector<fs::path> getAllTrashFilesPaths() {
    std::vector<fs::path> paths;
    const char* home = std::getenv("HOME");
    if (home) {
      paths.push_back(fs::path(home) / ".local/share/Trash/files");
    }

    uid_t uid = getuid();
    std::vector<fs::path> scanDirs;
    if (home) {
      std::string user = fs::path(home).filename().string();
      scanDirs.push_back("/media/" + user);
      scanDirs.push_back("/run/media/" + user);
    }
    scanDirs.push_back("/mnt");

    for (const auto& scanDir : scanDirs) {
      try {
        if (fs::exists(scanDir) && fs::is_directory(scanDir)) {
          for (const auto& entry : fs::directory_iterator(scanDir)) {
            if (entry.is_directory()) {
              fs::path localTrash = entry.path() / (".Trash-" + std::to_string(uid));
              if (fs::exists(localTrash) && fs::is_directory(localTrash)) {
                paths.push_back(localTrash / "files");
              }
            }
          }
        }
      } catch (...) {}
    }
    return paths;
  }

  bool moveToTrash(const fs::path& path) {
    if (isCommandAvailable("gio")) {
      std::string cmd = "gio trash " + escapeShellArg(path.string()) + " > /dev/null 2>&1";
      if (std::system(cmd.c_str()) == 0) {
        return true;
      }
    }

    try {
      fs::path absPath = fs::absolute(path);
      fs::path mountPoint = getMountPoint(absPath);

      const char* home = std::getenv("HOME");
      if (!home) return false;
      fs::path homePath(home);

      struct stat st_mount, st_home;
      if (stat(mountPoint.c_str(), &st_mount) != 0 || stat(homePath.c_str(), &st_home) != 0) {
        return false;
      }

      bool samePartition = (st_mount.st_dev == st_home.st_dev);
      fs::path trashDir;

      if (samePartition) {
        trashDir = homePath / ".local/share/Trash";
      } else {
        uid_t uid = getuid();
        fs::path localTrash = mountPoint / (".Trash-" + std::to_string(uid));
        if (!fs::exists(localTrash)) {
          try {
            fs::create_directories(localTrash);
            fs::permissions(localTrash, fs::perms::owner_all, fs::perm_options::replace);
          } catch (...) {
            return false;
          }
        }
        trashDir = localTrash;
      }

      fs::path trashFiles = trashDir / "files";
      fs::path trashInfo = trashDir / "info";

      fs::create_directories(trashFiles);
      fs::create_directories(trashInfo);

      std::string filename = absPath.filename().string();
      fs::path destFile = trashFiles / filename;

      int counter = 1;
      while (fs::exists(destFile)) {
        destFile = trashFiles / (filename + "_" + std::to_string(counter));
        counter++;
      }

      try {
        fs::rename(absPath, destFile);
      } catch (const std::filesystem::filesystem_error&) {
        fs::copy(absPath, destFile, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        fs::remove_all(absPath);
      }

      fs::path destInfo = trashInfo / (destFile.filename().string() + ".trashinfo");
      std::ofstream out(destInfo);
      if (out.is_open()) {
        out << "[Trash Info]\n";
        out << "Path=" << absPath.string() << "\n";

        std::time_t now = std::time(nullptr);
        char buf[100];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
        out << "DeletionDate=" << buf << "\n";
        out.close();
      }
      return true;
    } catch (...) {
      return false;
    }
  }

  fs::path findTrashedFile(const fs::path& origPath) {
    try {
      fs::path absPath = fs::absolute(origPath);
      const char* home = std::getenv("HOME");
      std::vector<fs::path> searchDirs;
      if (home) {
        searchDirs.push_back(fs::path(home) / ".local/share/Trash");
      }
      uid_t uid = getuid();
      fs::path mountPoint = getMountPoint(absPath);
      fs::path localTrash = mountPoint / (".Trash-" + std::to_string(uid));
      if (fs::exists(localTrash)) {
        searchDirs.push_back(localTrash);
      }

      for (const auto& trashDir : searchDirs) {
        fs::path infoDir = trashDir / "info";
        fs::path filesDir = trashDir / "files";
        if (fs::exists(infoDir)) {
          for (const auto& entry : fs::directory_iterator(infoDir)) {
            if (entry.path().extension() == ".trashinfo") {
              std::ifstream f(entry.path());
              std::string line;
              while (std::getline(f, line)) {
                if (line.rfind("Path=", 0) == 0) {
                  std::string p = line.substr(5);
                  if (p == absPath.string()) {
                    std::string base = entry.path().stem().string();
                    return filesDir / base;
                  }
                }
              }
            }
          }
        }
      }
    } catch (...) {}
    return "";
  }

  void handleUndoTrash() {
    if (lastTrashedFiles.empty()) {
      setStatus("Nothing to undo");
      return;
    }

    int successCount = 0;
    for (const auto& p : lastTrashedFiles) {
      TrashInfo ti = getTrashInfo(p);
      if (ti.originalPath.empty()) continue;
      try {
        fs::path dest(ti.originalPath);
        if (dest.has_parent_path()) {
          fs::create_directories(dest.parent_path());
        }
        if (fs::exists(dest)) {
          std::string newName = dest.filename().string() + "_restored";
          dest = dest.parent_path() / newName;
          int count = 1;
          while (fs::exists(dest)) {
            dest = dest.parent_path() / (newName + "_" + std::to_string(count));
            count++;
          }
        }
        try {
          fs::rename(p, dest);
        } catch (...) {
          fs::copy(p, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
          fs::remove_all(p);
        }

        fs::path infoFile = p.parent_path().parent_path() / "info" / (p.filename().string() + ".trashinfo");
        if (fs::exists(infoFile)) {
          fs::remove(infoFile);
        }
        successCount++;
      } catch (...) {}
    }

    if (successCount > 0) {
      setStatus("Undone: Restored " + std::to_string(successCount) + " item(s)");
    } else {
      setStatus("Undo failed");
    }
    lastTrashedFiles.clear();
    reloadAll();
  }

  void handleMoveToTrash() {
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
    std::string confirm = promptInput("Move " + countStr + " to Trash? (y/n)");
    if (confirm != "y" && confirm != "Y")
      return;

    startTrashTask(targets);
    multiSelection.clear();
  }

  void handleRestoreFromTrash() {
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
    std::string confirm = promptInput("Restore " + countStr + " from Trash? (y/n)");
    if (confirm != "y" && confirm != "Y")
      return;

    int successCount = 0;
    for (const auto& p : targets) {
      TrashInfo ti = getTrashInfo(p);
      if (ti.originalPath.empty()) {
        continue;
      }
      try {
        fs::path dest(ti.originalPath);
        if (dest.has_parent_path()) {
          fs::create_directories(dest.parent_path());
        }

        if (fs::exists(dest)) {
          std::string newName = dest.filename().string() + "_restored";
          dest = dest.parent_path() / newName;
          int count = 1;
          while (fs::exists(dest)) {
            dest = dest.parent_path() / (newName + "_" + std::to_string(count));
            count++;
          }
        }

        try {
          fs::rename(p, dest);
        } catch (const std::filesystem::filesystem_error&) {
          fs::copy(p, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
          fs::remove_all(p);
        }

        fs::path infoFile = p.parent_path().parent_path() / "info" / (p.filename().string() + ".trashinfo");
        if (fs::exists(infoFile)) {
          fs::remove(infoFile);
        }
        successCount++;
      } catch (...) {}
    }

    if (successCount == (int)targets.size()) {
      setStatus("Restored successfully");
    } else {
      setStatus("Restored " + std::to_string(successCount) + "/" + std::to_string(targets.size()) + " items");
    }

    multiSelection.clear();
    reloadAll();
  }

  void handleEmptyTrash() {
    std::string confirm = promptInput("Empty Trash? All files will be permanently deleted. (y/n)");
    if (confirm != "y" && confirm != "Y")
      return;

    std::vector<fs::path> targets;
    std::vector<fs::path> trashDirs = getAllTrashFilesPaths();
    for (const auto& filesDir : trashDirs) {
      try {
        fs::path trashDir = filesDir.parent_path();
        fs::path trashFiles = trashDir / "files";
        fs::path trashInfo = trashDir / "info";

        if (fs::exists(trashFiles)) {
          for (const auto& entry : fs::directory_iterator(trashFiles)) {
            targets.push_back(entry.path());
          }
        }
        if (fs::exists(trashInfo)) {
          for (const auto& entry : fs::directory_iterator(trashInfo)) {
            targets.push_back(entry.path());
          }
        }
      } catch (...) {}
    }

    if (targets.empty()) {
      setStatus("Trash is already empty");
      return;
    }

    startDeleteTask(targets);
    setStatus("Emptying Trash in background...");
  }

  void toggleTrashMode() {
    if (isTrashMode) {
      isTrashMode = false;
      if (!preTrashPath.empty() && fs::exists(preTrashPath)) {
        currentPath = preTrashPath;
      } else {
        const char* home = std::getenv("HOME");
        currentPath = home ? fs::path(home) : "/";
      }
      reloadAll();
      selectedIndex = 0;
      scrollOffset = 0;
      setStatus("Exited Trash");
    } else {
      fs::path trashPath = getTrashFilesPath();
      if (trashPath.empty()) {
        setStatus("Error: Trash not available");
        return;
      }
      if (!fs::exists(trashPath)) {
        try {
          fs::create_directories(trashPath);
        } catch (...) {
          setStatus("Error: Trash not available");
          return;
        }
      }
      preTrashPath = currentPath;
      isTrashMode = true;
      currentPath = trashPath;
      reloadAll();
      selectedIndex = 0;
      scrollOffset = 0;
      setStatus("Entered Trash Manager (press 'T' to exit, 'r' to restore, 'e' to empty)");
    }
  }

  void reloadAll() {
    loadDirectory(currentPath, currentFiles);
    loadParent();
    tabs[activeTabIndex].currentFiles = currentFiles;
    if (isDualPaneMode) {
      size_t inactiveIdx = (activeTabIndex == leftTabIndex) ? rightTabIndex : leftTabIndex;
      if (inactiveIdx < tabs.size()) {
        tabs[inactiveIdx].currentFiles.clear();
      }
    }
    updateInotifyWatches();
  }
  void toggleHidden() {
    showHidden = !showHidden;
    reloadAll();
    setStatus(showHidden ? "Showing hidden" : "Hidden masked");
  }

  void toggleDualPaneMode() {
    isDualPaneMode = !isDualPaneMode;
    if (isDualPaneMode) {
      if (tabs.size() < 2) {
        Tab newTab;
        newTab.currentPath = currentPath;
        newTab.selectedIndex = 0;
        newTab.scrollOffset = 0;
        newTab.isSearching = false;
        newTab.isTrashMode = false;
        newTab.multiSelection = {};
        newTab.currentFiles = {};
        tabs.push_back(newTab);
      }
      leftTabIndex = activeTabIndex;
      if (leftTabIndex == 0) {
        rightTabIndex = 1;
      } else {
        rightTabIndex = 0;
      }
      focusLeftPane = (activeTabIndex == leftTabIndex);
      size_t inactiveIdx = focusLeftPane ? rightTabIndex : leftTabIndex;
      loadInactiveTabDirectoryIfNeeded(inactiveIdx);
      setStatus("Entered dual-pane mode");
    } else {
      setStatus("Exited dual-pane mode");
    }
    updateLayout();
    reloadAll();
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
    if (!winPinned) return;
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
      std::string name = pinnedPaths[i].filename().string();
      if (name.empty())
        name = pinnedPaths[i].string();
      int limit = getmaxx(winPinned) - 8;
      if (limit < 1) limit = 1;
      if (name.length() > (size_t)limit) {
        name = utf8_safe_truncate(name, limit);
      }

      if (focusPinned && i == pinnedIndex) {
        wattron(winPinned, COLOR_PAIR(10) | A_BOLD);
        for (int j = 0; j < getmaxx(winPinned) - 2; ++j)
          waddch(winPinned, ' ');
        wmove(winPinned, i + 1, 1);

        wattron(winPinned, COLOR_PAIR(4) | A_BOLD);
        waddstr(winPinned, "┃");
        wattroff(winPinned, COLOR_PAIR(4) | A_BOLD);

        wattron(winPinned, COLOR_PAIR(10) | A_BOLD);
        wprintw(winPinned, " %s %s", ICON_PIN, name.c_str());
        wattroff(winPinned, COLOR_PAIR(10) | A_BOLD);
      } else {
        wprintw(winPinned, "  %s %s", ICON_PIN, name.c_str());
      }
    }
    wnoutrefresh(winPinned);
  }

  void drawParent() {
    if (!winParent) return;
    werase(winParent);
    if (parentFiles.empty()) {
      wnoutrefresh(winParent);
      return;
    }
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

      FileStyle style = getFileStyle(file.name, file.extension, file.is_directory, file.is_empty_directory);
      if (file.is_symlink) {
        style.icon = ICON_LINK;
      }
      int finalPair = getFinalPair(style.pair, false, isCurrent);

      std::string display = file.name;
      if (display.length() > (size_t)getmaxx(winParent) - 8) {
        int limit = getmaxx(winParent) - 11;
        if (limit < 1) limit = 1;
        display = utf8_safe_truncate(display, limit);
      }

      if (isCurrent) {
        wattron(winParent, COLOR_PAIR(finalPair) | A_BOLD);
        for (int j = 0; j < getmaxx(winParent) - 2; ++j)
          waddch(winParent, ' ');
        wmove(winParent, i + 1, 1);

        wattron(winParent, COLOR_PAIR(6) | A_BOLD);
        waddstr(winParent, "┃");
        wattroff(winParent, COLOR_PAIR(6) | A_BOLD);

        wattron(winParent, COLOR_PAIR(finalPair) | A_BOLD);
        wprintw(winParent, " %s %s", style.icon, display.c_str());
        wattroff(winParent, COLOR_PAIR(finalPair) | A_BOLD);
      } else {
        wattron(winParent, COLOR_PAIR(finalPair) | A_DIM);
        wprintw(winParent, "  %s %s", style.icon, display.c_str());
        wattroff(winParent, COLOR_PAIR(finalPair) | A_DIM);
      }
    }
    wnoutrefresh(winParent);
  }

  void drawTabs() {
    move(0, 0);
    clrtoeol();

    if (tabs.empty()) return;

    // Prepare tab displays and widths
    std::vector<std::string> tabDisplays;
    std::vector<int> tabWidths;
    for (size_t i = 0; i < tabs.size(); ++i) {
      std::string tabNumStr = (i == 9) ? "0" : std::to_string(i + 1);
      std::string tabName = tabs[i].currentPath.filename().string();
      if (tabName.empty()) {
        tabName = "/";
      }
      std::string disp = " " + tabNumStr + " " + tabName + " ";
      tabDisplays.push_back(disp);
      tabWidths.push_back(disp.length() + 4);
    }

    // Determine visible range [startTab, endTab] around activeTabIndex
    size_t startTab = 0;
    size_t endTab = tabs.size() - 1;

    while (true) {
      int totalWidth = 2;
      if (startTab > 0) totalWidth += 4;
      for (size_t i = startTab; i <= endTab; ++i) {
        totalWidth += tabWidths[i];
      }
      if (endTab < tabs.size() - 1) totalWidth += 4;

      if (totalWidth <= width || startTab == endTab) {
        break;
      }

      if (activeTabIndex - startTab > endTab - activeTabIndex) {
        startTab++;
      } else {
        endTab--;
      }
    }

    int x = 2;
    if (startTab > 0) {
      attron(COLOR_PAIR(6) | A_BOLD);
      mvprintw(0, x, "«");
      attroff(COLOR_PAIR(6) | A_BOLD);
      x += 3;
    }

    for (size_t i = startTab; i <= endTab && i < tabs.size(); ++i) {
      bool isActive = (i == activeTabIndex);
      bool isLeft = isDualPaneMode && (i == leftTabIndex);
      bool isRight = isDualPaneMode && (i == rightTabIndex);
      bool isPaneTab = isLeft || isRight;
      const std::string& disp = tabDisplays[i];

      if (isActive) {
        attron(COLOR_PAIR(6) | A_BOLD);
        mvprintw(0, x, "");
        attroff(COLOR_PAIR(6) | A_BOLD);

        attron(COLOR_PAIR(6) | A_BOLD | A_REVERSE);
        printw("%s", disp.c_str());
        attroff(COLOR_PAIR(6) | A_BOLD | A_REVERSE);

        attron(COLOR_PAIR(6) | A_BOLD);
        printw("");
        attroff(COLOR_PAIR(6) | A_BOLD);
      } else if (isPaneTab) {
        attron(COLOR_PAIR(6));
        mvprintw(0, x, "");
        attroff(COLOR_PAIR(6));

        attron(COLOR_PAIR(6) | A_BOLD);
        printw("%s", disp.c_str());
        attroff(COLOR_PAIR(6) | A_BOLD);

        attron(COLOR_PAIR(6));
        printw("");
        attroff(COLOR_PAIR(6));
      } else {
        attron(COLOR_PAIR(6) | A_DIM);
        mvprintw(0, x, "  %s  ", disp.c_str());
        attroff(COLOR_PAIR(6) | A_DIM);
      }
      x += tabWidths[i];
    }

    if (endTab < tabs.size() - 1) {
      attron(COLOR_PAIR(6) | A_BOLD);
      mvprintw(0, x, "»");
      attroff(COLOR_PAIR(6) | A_BOLD);
    }
  }

  void createTab() {
    if (tabs.size() >= 10) {
      setStatus("Maximum 10 tabs allowed");
      return;
    }
    tabs[activeTabIndex].currentPath = currentPath;
    tabs[activeTabIndex].selectedIndex = selectedIndex;
    tabs[activeTabIndex].scrollOffset = scrollOffset;
    tabs[activeTabIndex].isSearching = isSearching;
    tabs[activeTabIndex].isTrashMode = isTrashMode;
    tabs[activeTabIndex].multiSelection = multiSelection;
    tabs[activeTabIndex].currentFiles = currentFiles;

    Tab newTab;
    newTab.currentPath = currentPath;
    newTab.selectedIndex = selectedIndex;
    newTab.scrollOffset = scrollOffset;
    newTab.isSearching = false;
    newTab.isTrashMode = false;
    newTab.multiSelection = {};
    newTab.currentFiles = {};

    tabs.push_back(newTab);
    activeTabIndex = tabs.size() - 1;

    reloadAll();
    onTabSwitched();
    setStatus("Tab created");
  }

  void closeTab() {
    if (tabs.size() <= 1) {
      setStatus("Cannot close the last tab");
      return;
    }

    if (isDualPaneMode && tabs.size() <= 2) {
      isDualPaneMode = false;
      setStatus("Exited dual-pane mode (need at least 2 tabs)");
    }

    tabs.erase(tabs.begin() + activeTabIndex);
    if (activeTabIndex >= tabs.size()) {
      activeTabIndex = tabs.size() - 1;
    }

    if (isDualPaneMode) {
      leftTabIndex = activeTabIndex;
      if (leftTabIndex == 0) {
        rightTabIndex = 1;
      } else {
        rightTabIndex = 0;
      }
      focusLeftPane = (activeTabIndex == leftTabIndex);
    }

    currentPath = tabs[activeTabIndex].currentPath;
    selectedIndex = tabs[activeTabIndex].selectedIndex;
    scrollOffset = tabs[activeTabIndex].scrollOffset;
    isSearching = tabs[activeTabIndex].isSearching;
    isTrashMode = tabs[activeTabIndex].isTrashMode;
    currentFiles = tabs[activeTabIndex].currentFiles;
    
    auto savedSelection = tabs[activeTabIndex].multiSelection;
    reloadAll();
    multiSelection = savedSelection;
    if (!isDualPaneMode) {
      setStatus("Tab closed");
    }
  }

  void switchTab(size_t index) {
    if (index >= tabs.size()) return;
    
    tabs[activeTabIndex].currentPath = currentPath;
    tabs[activeTabIndex].selectedIndex = selectedIndex;
    tabs[activeTabIndex].scrollOffset = scrollOffset;
    tabs[activeTabIndex].isSearching = isSearching;
    tabs[activeTabIndex].isTrashMode = isTrashMode;
    tabs[activeTabIndex].multiSelection = multiSelection;
    tabs[activeTabIndex].currentFiles = currentFiles;

    activeTabIndex = index;
    currentPath = tabs[activeTabIndex].currentPath;
    selectedIndex = tabs[activeTabIndex].selectedIndex;
    scrollOffset = tabs[activeTabIndex].scrollOffset;
    isSearching = tabs[activeTabIndex].isSearching;
    isTrashMode = tabs[activeTabIndex].isTrashMode;
    currentFiles = tabs[activeTabIndex].currentFiles;

    auto savedSelection = tabs[activeTabIndex].multiSelection;
    reloadAll();
    multiSelection = savedSelection;

    onTabSwitched();
  }

  void onTabSwitched() {
    if (!isDualPaneMode) return;
    if (focusLeftPane) {
      if (activeTabIndex == rightTabIndex) {
        rightTabIndex = leftTabIndex;
      }
      leftTabIndex = activeTabIndex;
    } else {
      if (activeTabIndex == leftTabIndex) {
        leftTabIndex = rightTabIndex;
      }
      rightTabIndex = activeTabIndex;
    }
  }

  void loadInactiveTabDirectoryIfNeeded(size_t inactiveIdx) {
    if (inactiveIdx >= tabs.size()) return;
    if (tabs[inactiveIdx].currentFiles.empty()) {
      try {
        std::vector<FileEntry> tempFiles;
        for (const auto& entry : fs::directory_iterator(tabs[inactiveIdx].currentPath)) {
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;
          tempFiles.emplace_back(entry);
        }
        sortList(tempFiles);
        tabs[inactiveIdx].currentFiles = tempFiles;
      } catch (...) {
        tabs[inactiveIdx].currentFiles.clear();
      }
    }
  }

  void drawPane(WINDOW* win, const fs::path& panePath, const std::vector<FileEntry>& paneFiles,
                size_t paneSelectedIndex, size_t& paneScrollOffset,
                const std::set<fs::path>& paneMultiSelection, bool paneIsSearching,
                bool paneIsTrashMode, bool hasFocus) {
    werase(win);
    if (hasFocus)
      wattron(win, COLOR_PAIR(6) | A_BOLD);
    else
      wattron(win, COLOR_PAIR(6));
    drawRoundedBox(win);
    wattroff(win, A_BOLD);
    wattroff(win, COLOR_PAIR(6));

    wattron(win, A_BOLD | COLOR_PAIR(1));
    if (paneIsSearching) {
      mvwprintw(win, 0, 2, "  Search Results ");
    } else if (paneIsTrashMode) {
      mvwprintw(win, 0, 2, " 󰩹 Trash ");
    } else {
      std::string title = " 󰉖 " + panePath.filename().string() + " ";
      int maxTitleW = getmaxx(win) - 4;
      if (maxTitleW < 5) maxTitleW = 5;
      if ((int)title.length() > maxTitleW) {
        std::string filename = panePath.filename().string();
        int maxFilenameW = maxTitleW - 6; // Subtracting " 󰉖 " and " "
        if (maxFilenameW < 3) maxFilenameW = 3;
        if ((int)filename.length() > maxFilenameW) {
          filename = utf8_safe_truncate(filename, maxFilenameW - 3) + "...";
        }
        title = " 󰉖 " + filename + " ";
      }
      mvwprintw(win, 0, 2, "%s", title.c_str());
    }
    wattroff(win, A_BOLD | COLOR_PAIR(1));

    if (paneIsSearching && paneFiles.empty()) {
      int my = getmaxy(win);
      int mx = getmaxx(win);
      wattron(win, COLOR_PAIR(7) | A_BOLD);
      mvwprintw(win, my / 2, (mx - 15) / 2, "  Searching... ");
      wattroff(win, COLOR_PAIR(7) | A_BOLD);
      wnoutrefresh(win);
      return;
    }

    if (!paneMultiSelection.empty()) {
      std::string selStr =
          " [ MULTI-SELECT: " + std::to_string(paneMultiSelection.size()) + " ITEMS ] ";
      wattron(win, COLOR_PAIR(9) | A_BOLD | A_REVERSE);
      mvwprintw(win, 0, getmaxx(win) - selStr.length() - 2, "%s", selStr.c_str());
      wattroff(win, COLOR_PAIR(9) | A_BOLD | A_REVERSE);
    }

    int maxLines = getmaxy(win) - 2;
    if (paneFiles.empty()) {
      wnoutrefresh(win);
      return;
    }

    size_t safeSelectedIndex = paneSelectedIndex;
    if (safeSelectedIndex >= paneFiles.size()) {
      safeSelectedIndex = paneFiles.size() - 1;
    }

    if (safeSelectedIndex < paneScrollOffset)
      paneScrollOffset = safeSelectedIndex;
    if (safeSelectedIndex >= paneScrollOffset + (size_t)maxLines)
      paneScrollOffset = safeSelectedIndex - maxLines + 1;

    for (int i = 0; i < maxLines && (paneScrollOffset + i) < paneFiles.size(); ++i) {
      int idx = paneScrollOffset + i;
      const auto& file = paneFiles[idx];
      wmove(win, i + 1, 1);

      bool isSelected = (hasFocus && idx == (int)safeSelectedIndex);
      bool isMultiSelected = paneMultiSelection.count(file.path);

      bool inClipboard = false;
      for (const auto& p : clipboard.paths) {
        if (p == file.path) {
          inClipboard = true;
          break;
        }
      }
      bool isDimmed = inClipboard && clipboard.isCut && !isSelected;

      FileStyle style = getFileStyle(file.name, file.extension, file.is_directory, file.is_empty_directory);
      if (file.is_symlink) {
        style.icon = ICON_LINK;
      }
      int finalPair = getFinalPair(style.pair, isSelected, false);

      if (isSelected) {
        wattron(win, COLOR_PAIR(finalPair) | A_BOLD);
        for (int j = 0; j < getmaxx(win) - 2; ++j)
          waddch(win, ' ');
        wmove(win, i + 1, 1);
      } else if (isMultiSelected) {
        wattron(win, COLOR_PAIR(9) | A_BOLD);
      } else {
        wattron(win, COLOR_PAIR(finalPair));
      }
      if (isDimmed) {
        wattron(win, A_DIM);
      }

      std::string dirPart = "";
      std::string filePart = file.name;
      if (paneIsSearching) {
        try {
          std::string relPath = fs::relative(file.path, panePath).string();
          size_t lastSlash = relPath.find_last_of("/\\");
          if (lastSlash != std::string::npos) {
            dirPart = relPath.substr(0, lastSlash + 1);
            filePart = relPath.substr(lastSlash + 1);
          } else {
            filePart = relPath;
          }
        } catch (...) {
          filePart = file.name;
        }
      }

      std::string sz;
      if (sortMode == SortMode::SIZE) {
        if (file.is_directory && file.path.string().find("/gvfs/") != std::string::npos) {
          sz = "DIR";
        } else {
          sz = formatSize(file.size);
        }
      } else {
        sz = file.modified_time_str;
      }

      int availWidth = getmaxx(win) - sz.length() - 11;
      if (availWidth < 10) availWidth = 10;

      std::string fullDisplay = dirPart + filePart;
      std::string symDisplay = "";
      if (file.is_symlink) {
        symDisplay = " 󰌹 " + file.symlink_target;
      }

      std::string totalDisplay = fullDisplay + symDisplay;
      size_t totalLen = utf8_length(totalDisplay);
      if (totalLen > (size_t)availWidth) {
        size_t fullLen = utf8_length(fullDisplay);
        if (fullLen >= (size_t)availWidth) {
          int limit = (int)availWidth - 3;
          if (limit < 1) limit = 1;
          fullDisplay = utf8_safe_truncate(fullDisplay, limit);
          size_t lastSlash = fullDisplay.find_last_of("/\\");
          if (lastSlash != std::string::npos) {
            dirPart = fullDisplay.substr(0, lastSlash + 1);
            filePart = fullDisplay.substr(lastSlash + 1);
          } else {
            dirPart = "";
            filePart = fullDisplay;
          }
          symDisplay = "";
        } else {
          size_t maxSymLen = availWidth - fullLen;
          if (maxSymLen >= 7) {
            std::string symTarget = file.symlink_target;
            int limit = (int)maxSymLen - 3;
            if (limit < 1) limit = 1;
            symTarget = utf8_safe_truncate(symTarget, limit);
            symDisplay = " 󰌹 " + symTarget;
          } else {
            symDisplay = "";
          }
        }
      }

      std::string marker = " ";
      if (isMultiSelected) {
        marker = "*";
      } else if (inClipboard) {
        marker = clipboard.isCut ? "󰆐" : "󰆏";
      }

      if (isSelected) {
        wattron(win, COLOR_PAIR(6) | A_BOLD);
        waddstr(win, "┃");
        wattroff(win, COLOR_PAIR(6) | A_BOLD);

        wattron(win, COLOR_PAIR(finalPair) | A_BOLD);
        wprintw(win, "%s%s ", marker.c_str(), style.icon);
      } else {
        wprintw(win, " %s%s ", marker.c_str(), style.icon);
      }

      if (paneIsSearching && !dirPart.empty()) {
        if (isSelected) {
          wprintw(win, "%s%s", dirPart.c_str(), filePart.c_str());
        } else {
          wattron(win, A_DIM);
          wprintw(win, "%s", dirPart.c_str());
          wattroff(win, A_DIM);
          wprintw(win, "%s", filePart.c_str());
        }
      } else {
        wprintw(win, "%s", filePart.c_str());
      }

      if (!symDisplay.empty()) {
        bool targetDimmed = !isSelected;
        if (targetDimmed) {
          wattron(win, A_DIM);
        }
        wprintw(win, "%s", symDisplay.c_str());
        if (targetDimmed) {
          wattroff(win, A_DIM);
        }
      }

      // Explicitly clear the gap between filename/symlink and the date/size string
      int dateStart = getmaxx(win) - sz.length() - 2;
      int curY, curX;
      getyx(win, curY, curX);
      for (int k = curX; k < dateStart; ++k) {
        waddch(win, ' ');
      }
      wprintw(win, "%s", sz.c_str());

      if (isDimmed) {
        wattron(win, A_DIM);
      }
      if (isSelected) {
        wattroff(win, COLOR_PAIR(finalPair) | A_BOLD);
      } else if (isMultiSelected)
        wattroff(win, COLOR_PAIR(9) | A_BOLD);
      else
        wattroff(win, COLOR_PAIR(finalPair));
      if (isDimmed) {
        wattroff(win, A_DIM);
      }
    }

    // Redraw the borders at the very end of rendering to ensure they are never broken by text drawing
    if (hasFocus)
      wattron(win, COLOR_PAIR(6) | A_BOLD);
    else
      wattron(win, COLOR_PAIR(6));
    drawRoundedBox(win);
    wattroff(win, A_BOLD);
    wattroff(win, COLOR_PAIR(6));

    wnoutrefresh(win);
  }

  void drawCurrent() {
    if (isDualPaneMode) {
      size_t leftIdx = leftTabIndex;
      if (activeTabIndex == leftIdx) {
        drawPane(winCurrent, currentPath, currentFiles, selectedIndex, scrollOffset, multiSelection, isSearching, isTrashMode, true);
      } else {
        loadInactiveTabDirectoryIfNeeded(leftIdx);
        drawPane(winCurrent, tabs[leftIdx].currentPath, tabs[leftIdx].currentFiles,
                 tabs[leftIdx].selectedIndex, tabs[leftIdx].scrollOffset,
                 tabs[leftIdx].multiSelection, tabs[leftIdx].isSearching, tabs[leftIdx].isTrashMode, false);
      }
      return;
    }

    drawPane(winCurrent, currentPath, currentFiles, selectedIndex, scrollOffset, multiSelection, isSearching, isTrashMode, !focusPinned);
  }

  struct DeviceInfo {
    std::string name;
    std::string unixDevice;      // e.g. /dev/nvme0n1p3 or /dev/bus/usb/003/009
    std::string activationRoot;   // e.g. mtp://SAMSUNG_SAMSUNG_Android_RZCW91FV5WA/
    std::string type;            // "MTP" or "Block"
    bool isMounted = false;
    std::string mountPath;       // e.g. /run/user/1000/gvfs/mtp:host=... or /media/...
    bool canMount = false;
    bool canUnmount = false;
  };

  std::string resolveMtpPath(uid_t uid, const std::string& host) {
    std::string path = "/run/user/" + std::to_string(uid) + "/gvfs/mtp:host=" + host;
    if (fs::exists(path)) return path;

    std::string gvfsBase = "/run/user/" + std::to_string(uid) + "/gvfs";
    try {
      if (fs::exists(gvfsBase)) {
        for (const auto& entry : fs::directory_iterator(gvfsBase)) {
          std::string name = entry.path().filename().string();
          if (name.rfind("mtp:host=", 0) == 0) {
            if (name.find(host) != std::string::npos) {
              return entry.path().string();
            }
            return entry.path().string();
          }
        }
      }
    } catch (...) {}

    return path;
  }

  std::string urlDecode(const std::string& str) {
    std::string decoded = "";
    for (size_t i = 0; i < str.length(); ++i) {
      if (str[i] == '%' && i + 2 < str.length()) {
        int hexVal;
        if (sscanf(str.substr(i + 1, 2).c_str(), "%x", &hexVal) == 1) {
          decoded += static_cast<char>(hexVal);
          i += 2;
        } else {
          decoded += str[i];
        }
      } else if (str[i] == '+') {
        decoded += ' ';
      } else {
        decoded += str[i];
      }
    }
    return decoded;
  }

  std::vector<DeviceInfo> detectDevices() {
    std::vector<DeviceInfo> devices;
    FILE* pipe = popen("gio mount -li 2>/dev/null", "r");
    if (!pipe) return devices;

    char buffer[512];
    DeviceInfo curDev;
    bool inVolume = false;
    uid_t uid = geteuid();

    while (fgets(buffer, sizeof(buffer), pipe)) {
      std::string line(buffer);
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
      }

      std::string trimmed = line;
      size_t firstNonSpace = trimmed.find_first_not_of(" \t");
      if (firstNonSpace != std::string::npos) {
        trimmed = trimmed.substr(firstNonSpace);
      } else {
        trimmed = "";
      }

      if (trimmed.rfind("Volume(", 0) == 0) {
        if (inVolume) {
          devices.push_back(curDev);
        }
        inVolume = true;
        curDev = DeviceInfo();
        size_t colon = trimmed.find("): ");
        if (colon != std::string::npos) {
          curDev.name = trimmed.substr(colon + 3);
        } else {
          curDev.name = "Unknown Volume";
        }
        continue;
      }

      if (!inVolume) continue;

      if (!line.empty() && !isspace(line[0]) && trimmed.rfind("Volume(", 0) != 0) {
        devices.push_back(curDev);
        inVolume = false;
        continue;
      }

      if (trimmed.empty()) continue;
      std::string propLine = trimmed;

      if (propLine.rfind("Type: GProxyVolume (GProxyVolumeMonitorMTP)", 0) == 0) {
        curDev.type = "MTP";
      } else if (propLine.rfind("Type: GProxyVolume (GProxyVolumeMonitorUDisks2)", 0) == 0) {
        curDev.type = "Block";
      } else if (propLine.rfind("unix-device:", 0) == 0) {
        size_t quoteStart = propLine.find('\'');
        size_t quoteEnd = propLine.find('\'', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
          curDev.unixDevice = propLine.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
      } else if (propLine.rfind("activation_root=", 0) == 0) {
        curDev.activationRoot = propLine.substr(16);
      } else if (propLine.rfind("can_mount=", 0) == 0) {
        curDev.canMount = (propLine.substr(10) == "1");
      } else if (propLine.rfind("Mount(", 0) == 0) {
        curDev.isMounted = true;
        size_t arrow = propLine.find(" -> ");
        if (arrow != std::string::npos) {
          std::string uri = propLine.substr(arrow + 4);
          if (uri.rfind("file://", 0) == 0) {
            curDev.mountPath = urlDecode(uri.substr(7));
            if (!curDev.mountPath.empty() && curDev.mountPath.back() == '/') {
              curDev.mountPath.pop_back();
            }
          } else if (uri.rfind("mtp://", 0) == 0) {
            std::string host = uri.substr(6);
            if (!host.empty() && host.back() == '/') {
              host.pop_back();
            }
            curDev.mountPath = resolveMtpPath(uid, host);
          } else if (uri.rfind("gphoto2://", 0) == 0) {
            std::string host = uri.substr(10);
            if (!host.empty() && host.back() == '/') {
              host.pop_back();
            }
            curDev.mountPath = "/run/user/" + std::to_string(uid) + "/gvfs/gphoto2:host=" + host;
          }
        }
      } else if (propLine.rfind("can_unmount=", 0) == 0) {
        curDev.canUnmount = (propLine.substr(12) == "1");
      }
    }

    if (inVolume) {
      devices.push_back(curDev);
    }

    pclose(pipe);
    return devices;
  }

  void mountDevice(const DeviceInfo& dev) {
    std::string cmd;
    if (!dev.unixDevice.empty()) {
      cmd = "gio mount -d " + dev.unixDevice + " 2>&1";
    } else if (!dev.activationRoot.empty()) {
      cmd = "gio mount " + dev.activationRoot + " 2>&1";
    } else {
      return;
    }
    setStatus("Mounting " + dev.name + "...");
    FILE* p = popen(cmd.c_str(), "r");
    if (p) {
      char buf[256];
      std::string err;
      while (fgets(buf, sizeof(buf), p)) {
        err += buf;
      }
      int status = pclose(p);
      if (status == 0) {
        setStatus("Mounted " + dev.name);
      } else {
        while (!err.empty() && (err.back() == '\n' || err.back() == '\r')) err.pop_back();
        if (err.empty()) err = "Process exited with code " + std::to_string(status);
        setStatus("Mount failed: " + err);
      }
    }
  }

  void unmountDevice(const DeviceInfo& dev) {
    std::string target;
    if (!dev.activationRoot.empty()) {
      target = dev.activationRoot;
    } else if (!dev.mountPath.empty()) {
      target = "file://" + dev.mountPath;
    } else {
      return;
    }
    std::string cmd = "gio mount -u \"" + target + "\" 2>&1";
    setStatus("Unmounting " + dev.name + "...");
    FILE* p = popen(cmd.c_str(), "r");
    if (p) {
      char buf[256];
      std::string err;
      while (fgets(buf, sizeof(buf), p)) {
        err += buf;
      }
      int status = pclose(p);
      if (status == 0) {
        setStatus("Unmounted " + dev.name);
      } else {
        while (!err.empty() && (err.back() == '\n' || err.back() == '\r')) err.pop_back();
        if (err.empty()) err = "Process exited with code " + std::to_string(status);
        setStatus("Unmount failed: " + err);
      }
    }
  }

  void drawDevicesOverlay() {
    clearDirectRender();
    std::vector<DeviceInfo> devices = detectDevices();

    int h = 18;
    int w = 66;
    if (h > height - 4) h = height - 4;
    if (w > width - 4) w = width - 4;
    if (h < 6) h = 6;
    if (w < 20) w = 20;

    int startY = (height - h) / 2;
    int startX = (width - w) / 2;

    WINDOW* devWin = newwin(h, w, startY, startX);
    keypad(devWin, TRUE);
    wtimeout(devWin, 200);

    size_t selectedDeviceIndex = 0;

    while (true) {
      werase(devWin);
      wattron(devWin, COLOR_PAIR(6) | A_BOLD);
      drawRoundedBox(devWin);
      wattroff(devWin, COLOR_PAIR(6) | A_BOLD);

      wattron(devWin, COLOR_PAIR(1) | A_BOLD);
      std::string title = "󰋊 Devices & External Storage";
      if ((int)title.length() > w - 6) {
        title = utf8_safe_truncate(title, w - 6);
      }
      mvwprintw(devWin, 1, 2, "%s", title.c_str());
      wattroff(devWin, COLOR_PAIR(1) | A_BOLD);

      if (devices.empty()) {
        wattron(devWin, A_ITALIC | COLOR_PAIR(27));
        mvwprintw(devWin, h / 2, (w - 20) / 2, "No devices detected");
        wattroff(devWin, A_ITALIC | COLOR_PAIR(27));
      } else {
        int maxLines = h - 5;
        for (int i = 0; i < maxLines && i < (int)devices.size(); ++i) {
          const auto& dev = devices[i];
          bool isSel = (i == (int)selectedDeviceIndex);
          int lineY = 3 + i;

          if (isSel) {
            wattron(devWin, COLOR_PAIR(6) | A_BOLD);
            for (int j = 1; j < w - 1; ++j) {
              mvwaddch(devWin, lineY, j, ' ');
            }
            wmove(devWin, lineY, 1);
            waddstr(devWin, "┃");
          } else {
            wmove(devWin, lineY, 2);
          }

          std::string icon = "󰋊";
          if (dev.type == "MTP") {
            icon = "";
          } else if (dev.unixDevice.rfind("/dev/sd", 0) == 0 || dev.unixDevice.rfind("/dev/mmcblk", 0) == 0) {
            icon = "󰕓";
          }

          std::string status = dev.isMounted ? "Mounted" : "Unmounted";
          std::string devName = dev.name;
          int maxNameW = w - 24;
          if (maxNameW < 10) maxNameW = 10;
          if ((int)devName.length() > maxNameW) {
            devName = utf8_safe_truncate(devName, maxNameW - 3);
          }

          if (isSel) {
            wattron(devWin, COLOR_PAIR(6) | A_BOLD);
            wprintw(devWin, " %s  %-20s  [%s]", icon.c_str(), devName.c_str(), status.c_str());
            wattroff(devWin, COLOR_PAIR(6) | A_BOLD);
          } else {
            int color = dev.isMounted ? 7 : 2;
            wattron(devWin, COLOR_PAIR(color));
            wprintw(devWin, " %s  %-20s", icon.c_str(), devName.c_str());
            wattroff(devWin, COLOR_PAIR(color));

            wattron(devWin, A_DIM);
            wprintw(devWin, "  [%s]", status.c_str());
            wattroff(devWin, A_DIM);
          }

          if (dev.isMounted && !dev.mountPath.empty()) {
            std::string pathStr = dev.mountPath;
            int availSpace = w - 40;
            if (availSpace > 5) {
              if ((int)pathStr.length() > availSpace) {
                pathStr = utf8_safe_truncate_left(pathStr, availSpace - 3);
              }
              wattron(devWin, A_DIM);
              mvwprintw(devWin, lineY, w - pathStr.length() - 2, "%s", pathStr.c_str());
              wattroff(devWin, A_DIM);
            }
          }
        }
      }

      std::string instr = "[Enter] Open/Mount  [m] Mount  [u] Unmount  [Esc/q] Close";
      if ((int)instr.length() > w - 4) {
        instr = "[Enter] Open  [m] Mount  [u] Unmount  [Esc] Close";
        if ((int)instr.length() > w - 4) {
          instr = "[Ent]Open [m]Mnt [u]Unmnt [Esc]Cls";
          if ((int)instr.length() > w - 4) {
            instr = "Ent:Open m:Mnt u:Unmnt";
          }
        }
      }
      wattron(devWin, A_DIM);
      mvwprintw(devWin, h - 2, 2, "%s", instr.c_str());
      wattroff(devWin, A_DIM);

      wrefresh(devWin);

      int ch = wgetch(devWin);
      if (ch == ERR) {
        if (devicesTriggered) {
          devicesTriggered = false;
          devices = detectDevices();
        }
        continue;
      }
      if (ch == 'q' || ch == 27) {
        break;
      }
      if (ch == 'j' || ch == KEY_DOWN) {
        if (!devices.empty() && selectedDeviceIndex < devices.size() - 1) {
          selectedDeviceIndex++;
        }
      }
      if (ch == 'k' || ch == KEY_UP) {
        if (selectedDeviceIndex > 0) {
          selectedDeviceIndex--;
        }
      }
      if (ch == 'm') {
        if (!devices.empty() && selectedDeviceIndex < devices.size()) {
          const auto& dev = devices[selectedDeviceIndex];
          if (!dev.isMounted) {
            mountDevice(dev);
            devices = detectDevices();
          } else {
            setStatus(dev.name + " is already mounted");
          }
        }
      }
      if (ch == 'u') {
        if (!devices.empty() && selectedDeviceIndex < devices.size()) {
          const auto& dev = devices[selectedDeviceIndex];
          if (dev.isMounted) {
            unmountDevice(dev);
            if (!dev.mountPath.empty() && (currentPath == dev.mountPath || isDescendant(currentPath, dev.mountPath))) {
              const char* home = getenv("HOME");
              changeDirectory(home ? fs::path(home) : fs::path("/"), true);
              reloadAll();
            }
            devices = detectDevices();
          } else {
            setStatus(dev.name + " is not mounted");
          }
        }
      }
      if (ch == 10) {
        if (!devices.empty() && selectedDeviceIndex < devices.size()) {
          auto dev = devices[selectedDeviceIndex];
          if (!dev.isMounted) {
            mountDevice(dev);
            devices = detectDevices();
            if (selectedDeviceIndex < devices.size()) {
              dev = devices[selectedDeviceIndex];
            }
          }
          if (dev.isMounted && !dev.mountPath.empty()) {
            clearDirectRender();
            changeDirectory(dev.mountPath, true);
            reloadAll();
            break;
          }
        }
      }
    }

    delwin(devWin);
    updateLayout();
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
      bool cached = false;
      {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = dirSizeCache.find(details.absolutePath);
        if (it != dirSizeCache.end()) {
          details.size = it->second;
          cached = true;
        } else {
          it = dirSizeCache.find(p.string());
          if (it != dirSizeCache.end()) {
            details.size = it->second;
            cached = true;
          }
        }
      }

      if (!cached) {
        if (details.absolutePath.find("/gvfs/") != std::string::npos) {
          details.size = 0;
        } else {
          details.size = SIZE_CALCULATING;
          std::lock_guard<std::mutex> qLock(queueMutex);
          bool alreadyQueued = false;
          for (const auto& job : sizeQueue) {
            if (job.path == p) {
              alreadyQueued = true;
              break;
            }
          }
          if (!alreadyQueued) {
            sizeQueue.push_back({p, currentViewId.load()});
            queueCv.notify_one();
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
    clearDirectRender();
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

    auto printField = [&](int row, const std::string& label, const std::string& val, int valColorPair) {
      mvwprintw(detWin, row, 2, "%-15s", label.c_str());
      int maxValW = w - 20;
      if (maxValW < 5) maxValW = 5;
      std::string showVal = val;
      if ((int)showVal.length() > maxValW) {
        int subLen = maxValW - 3;
        if (subLen < 1) subLen = 1;
        showVal = utf8_safe_truncate(showVal, subLen);
      }
      wattron(detWin, COLOR_PAIR(valColorPair));
      mvwprintw(detWin, row, 18, "%s", showVal.c_str());
      wattroff(detWin, COLOR_PAIR(valColorPair));
    };

    keypad(detWin, TRUE);
    wtimeout(detWin, 200);

    while (true) {
      werase(detWin);
      wattron(detWin, COLOR_PAIR(6) | A_BOLD);
      drawRoundedBox(detWin);
      wattroff(detWin, COLOR_PAIR(6) | A_BOLD);

      wattron(detWin, COLOR_PAIR(1) | A_BOLD);
      mvwprintw(detWin, 1, 2, "󰋽 File Information");
      wattroff(detWin, COLOR_PAIR(1) | A_BOLD);

      if (details.isDir && details.size == SIZE_CALCULATING) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = dirSizeCache.find(details.absolutePath);
        if (it != dirSizeCache.end()) {
          details.size = it->second;
        }
      }

      int row = 3;
      printField(row++, "Name:", details.name, details.isDir ? 1 : 2);
      printField(row++, "Path:", details.absolutePath, 2);

      if (details.isSymlink) {
        printField(row++, "Target:", details.symlinkTarget, 4);
      }

      printField(row++, "Type:", details.type, 2);

      std::string sizeStr;
      if (details.isDir && details.absolutePath.find("/gvfs/") != std::string::npos) {
        sizeStr = "DIR (size calculation skipped for MTP)";
      } else if (details.size == SIZE_CALCULATING) {
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

      int ch = wgetch(detWin);
      if (ch != ERR) {
        break;
      }
    }

    delwin(detWin);
  }

  void handleFuzzyFind() {
    std::string query = promptInput("Fuzzy Find");
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
      std::string cmd = "find " + escapeShellArg(currentPath.string()) + " -name .git -prune -o -print 2>/dev/null";
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
          setStatus("Error: Failed to run find");
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
            if (p == currentPath) continue;
            std::string relPath = fs::relative(p, currentPath).string();
            if (fuzzyMatch(relPath, query)) {
              if (fs::exists(p)) {
                results.emplace_back(p);
              }
            }
          } catch (...) {
          }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() > 50 && !results.empty()) {
          if (reqId == searchRequestID) {
            std::lock_guard<std::mutex> lock(searchResultMutex);
            pendingSearchResults = results;
            pendingSearchStatus = "Fuzzy Find: Searching... Found " + std::to_string(results.size()) + " matches";
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
        pendingSearchStatus = results.empty() ? ("No matches found for: " + query) : ("Fuzzy Find finished. Found " + std::to_string(results.size()) + " matches");
        hasPendingSearchResults = true;
        searchReady = true;
      }
    });
  }

  void drawHelpOverlay() {
    clearDirectRender();
    int h = 26;
    int w = 82;
    if (h > height - 2) h = height - 2;
    if (w > width - 2) w = width - 2;

    int startY = (height - h) / 2;
    int startX = (width - w) / 2;

    WINDOW* helpWin = newwin(h, w, startY, startX);
    if (!helpWin) return;

    wattron(helpWin, COLOR_PAIR(6) | A_BOLD);
    drawRoundedBox(helpWin);
    wattroff(helpWin, COLOR_PAIR(6) | A_BOLD);

    wattron(helpWin, COLOR_PAIR(1) | A_BOLD);
    std::string title = "󰘳 Fyzenor Keybindings";
    if ((int)title.length() > w - 6) {
      title = utf8_safe_truncate(title, w - 6);
    }
    mvwprintw(helpWin, 1, 2, "%s", title.c_str());
    wattroff(helpWin, COLOR_PAIR(1) | A_BOLD);

    auto printHelpLine = [&](int row, int col, const std::string& key, const std::string& desc) {
      if (row >= h - 2) return;
      std::string lineStr = key;
      while (lineStr.length() < 13) lineStr += " ";
      lineStr += " → " + desc;
      int maxLen = (w / 2) - 4;
      if ((int)lineStr.length() > maxLen) {
        lineStr = utf8_safe_truncate(lineStr, maxLen);
      }
      mvwprintw(helpWin, row, col, "%s", lineStr.c_str());
    };

    // Left Column (Col 2)
    printHelpLine(3, 2, "j / k", "Navigate");
    printHelpLine(4, 2, "h / l", "Back / Open");
    printHelpLine(5, 2, "Space / v", "Select");
    printHelpLine(6, 2, "a", "Select All");
    printHelpLine(7, 2, "Esc", "Clear Selection");
    printHelpLine(8, 2, "y", "Copy");
    printHelpLine(9, 2, "x", "Cut");
    printHelpLine(10, 2, "p", "Paste");
    printHelpLine(11, 2, "Y", "Paste as Symlink");
    printHelpLine(12, 2, "d / Delete", "Move to Trash");
    printHelpLine(13, 2, "D", "Delete Permanently");
    printHelpLine(14, 2, "T", "Toggle Trash Manager");
    printHelpLine(15, 2, "r", "Rename (Restore)");
    printHelpLine(16, 2, "u", "Undo Trash Action");
    printHelpLine(17, 2, "n / N", "New File / Folder");
    printHelpLine(18, 2, "z", "Zip");
    printHelpLine(19, 2, "e", "Extract / Empty Trash");
    printHelpLine(20, 2, ".", "Toggle Hidden");
    printHelpLine(21, 2, "s", "Toggle Sorting");
    printHelpLine(22, 2, "Ctrl+G", "Grow Pane Width");
    printHelpLine(23, 2, "F3", "Toggle Preview Pane");

    // Right Column (Col w / 2 + 1)
    int rCol = w / 2 + 1;
    printHelpLine(3, rCol, "P", "Pin Directory");
    printHelpLine(4, rCol, "F5 / Ctrl+R", "Refresh Directory");
    printHelpLine(5, rCol, "/", "Search (ripgrep)");
    printHelpLine(6, rCol, "f", "Fuzzy Find");
    printHelpLine(7, rCol, "w", "Show Active Tasks");
    printHelpLine(8, rCol, "i", "Show File Details");
    printHelpLine(9, rCol, "I", "Edit Permissions");
    printHelpLine(10, rCol, "Ctrl+O", "History Back");
    printHelpLine(11, rCol, "Ctrl+P", "History Forward");
    printHelpLine(12, rCol, "H", "History Jump List");
    printHelpLine(13, rCol, "t", "Create New Tab");
    printHelpLine(14, rCol, "W / Ctrl+W", "Close Current Tab");
    printHelpLine(15, rCol, "[ / ]", "Prev / Next Tab");
    printHelpLine(16, rCol, "1 - 9, 0", "Switch Tab 1-10");
    printHelpLine(17, rCol, ":", "Execute Shell Cmd");
    printHelpLine(18, rCol, "F2", "Toggle Dual-Pane");
    printHelpLine(19, rCol, "Tab", "Switch Pane / Pin");
    printHelpLine(20, rCol, "m", "Mounts & Devices");
    printHelpLine(21, rCol, "?", "Show Help");
    printHelpLine(22, rCol, "Ctrl+B / H", "Shrink Pane Width");
    printHelpLine(23, rCol, "F4", "Toggle Parent Pane");

    std::string closeMsg = "Press any key to close...";
    if ((int)closeMsg.length() > w - 4) {
      closeMsg = "Press key to close";
    }
    wattron(helpWin, A_DIM);
    mvwprintw(helpWin, h - 2, 2, "%s", closeMsg.c_str());
    wattroff(helpWin, A_DIM);

    wrefresh(helpWin);

    timeout(-1);
    getch();
    timeout(50);

    delwin(helpWin);
  }

  void drawTasksOverlay() {
    clearDirectRender();
    int h = height - 4;
    int w = width - 8;
    if (h < 18) h = 18;
    if (w < 80) w = 80;
    if (h > height - 2) h = height - 2;
    if (w > width - 2) w = width - 2;

    int splitLine = h / 2 + 1;
    int startY = (height - h) / 2;
    int startX = (width - w) / 2;

    WINDOW* taskWin = newwin(h, w, startY, startX);
    if (!taskWin) return;

    keypad(taskWin, TRUE);
    wtimeout(taskWin, 200);

    size_t highlightedIndex = 0;

    while (true) {
      werase(taskWin);
      wattron(taskWin, COLOR_PAIR(6) | A_BOLD);
      drawRoundedBox(taskWin);
      wattroff(taskWin, COLOR_PAIR(6) | A_BOLD);

      wattron(taskWin, COLOR_PAIR(1) | A_BOLD);
      std::string title = "󰙵 Active Tasks & Workers";
      if ((int)title.length() > w - 6) {
        title = utf8_safe_truncate(title, w - 6);
      }
      mvwprintw(taskWin, 1, 2, "%s", title.c_str());
      wattroff(taskWin, COLOR_PAIR(1) | A_BOLD);

      wattron(taskWin, COLOR_PAIR(6) | A_DIM);
      std::string separator = "";
      for (int k = 0; k < w - 2; ++k) {
        separator += "─";
      }
      mvwprintw(taskWin, 2, 1, "%s", separator.c_str());
      wattroff(taskWin, COLOR_PAIR(6) | A_DIM);

      std::vector<std::shared_ptr<AsyncTask>> tasksCopy;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        tasksCopy = activeTasks;
      }

      int maxDisplay = splitLine - 3;
      if (tasksCopy.empty()) {
        wattron(taskWin, A_DIM);
        mvwprintw(taskWin, 5, (w - 20) / 2, "No active background tasks");
        wattroff(taskWin, A_DIM);
        highlightedIndex = 0;
      } else {
        if (highlightedIndex >= tasksCopy.size()) {
          highlightedIndex = tasksCopy.empty() ? 0 : tasksCopy.size() - 1;
        }

        for (size_t i = 0; i < tasksCopy.size() && i < (size_t)maxDisplay; ++i) {
          const auto& task = tasksCopy[i];
          int y = 3 + i;
          bool isSelected = (i == highlightedIndex);

          if (isSelected) {
            wattron(taskWin, COLOR_PAIR(10) | A_BOLD);
            for (int x = 1; x < w - 1; ++x) {
              mvwaddch(taskWin, y, x, ' ');
            }
            wattron(taskWin, COLOR_PAIR(4) | A_BOLD);
            mvwprintw(taskWin, y, 1, "┃");
            wattroff(taskWin, COLOR_PAIR(4) | A_BOLD);

            wattron(taskWin, COLOR_PAIR(10) | A_BOLD);
            std::string typeText = task->type + (task->isPaused.load() ? " (Paused)" : "");
            mvwprintw(taskWin, y, 3, "[%d] %s", task->id, typeText.c_str());
          } else {
            std::string typeText = task->type + (task->isPaused.load() ? " (Paused)" : "");
            if (task->isPaused.load()) {
              wattron(taskWin, COLOR_PAIR(8) | A_BOLD);
            } else {
              wattron(taskWin, COLOR_PAIR(1) | A_BOLD);
            }
            mvwprintw(taskWin, y, 3, "[%d] %s", task->id, typeText.c_str());
            wattroff(taskWin, A_BOLD);
            if (task->isPaused.load()) {
              wattroff(taskWin, COLOR_PAIR(8));
            } else {
              wattroff(taskWin, COLOR_PAIR(1));
            }
          }

          int barW = 16;
          int prog = task->progress;
          if (prog < 0) prog = 0;
          if (prog > 100) prog = 100;
          std::string bar = "[";
          int filled = (prog * (barW - 2)) / 100;
          for (int b = 0; b < barW - 2; ++b) {
            if (b < filled) bar += "■";
            else bar += " ";
          }
          bar += "]";

          if (isSelected) {
            mvwprintw(taskWin, y, 14, "%s %3d%%", bar.c_str(), prog);
          } else {
            wattron(taskWin, COLOR_PAIR(7));
            mvwprintw(taskWin, y, 14, "%s %3d%%", bar.c_str(), prog);
            wattroff(taskWin, COLOR_PAIR(7));
          }

          std::string metrics = "";
          if (!task->isFinished.load()) {
            if (task->totalBytes > 0 && task->bytesProcessed.load() >= task->totalBytes) {
              metrics = "[Finalizing...]";
            } else {
              double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - task->startTime).count();
              if (elapsed > 0.1) {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(0) << elapsed << "s";
                metrics = "[" + ss.str() + "]";
                if ((task->type == "Copy" || task->type == "Move") && task->bytesProcessed.load() > 0) {
                  double speed = (task->bytesProcessed.load() / (1024.0 * 1024.0)) / elapsed;
                  std::stringstream ssSpeed;
                  ssSpeed << std::fixed << std::setprecision(1) << speed << " MB/s";
                  metrics += " (" + ssSpeed.str() + ")";

                  if (task->totalBytes > task->bytesProcessed.load() && !task->isPaused.load()) {
                    uint64_t remainingBytes = task->totalBytes - task->bytesProcessed.load();
                    double speedBytesPerSec = task->bytesProcessed.load() / elapsed;
                    if (speedBytesPerSec > 1.0) {
                      double remainingSeconds = (double)remainingBytes / speedBytesPerSec;
                      auto formatETA = [](double remSec) -> std::string {
                        if (remSec < 0) return "0s";
                        int total_secs = (int)remSec;
                        if (total_secs < 60) return std::to_string(total_secs) + "s";
                        int mins = total_secs / 60;
                        int secs = total_secs % 60;
                        if (mins < 60) return std::to_string(mins) + "m " + std::to_string(secs) + "s";
                        int hours = mins / 60;
                        mins = mins % 60;
                        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
                      };
                      metrics += " ETA: " + formatETA(remainingSeconds);
                    }
                  }
                }
              }
            }
          }

          std::string desc = task->description;
          int maxTotalW = w - 38;
          int metricsW = metrics.empty() ? 0 : (metrics.length() + 1);
          int maxDescW = maxTotalW - metricsW;
          if (maxDescW < 10) maxDescW = 10;

          if (desc.length() > (size_t)maxDescW) {
            int limit = maxDescW - 3;
            if (limit < 1) limit = 1;
            desc = utf8_safe_truncate(desc, limit) + "...";
          }

          if (!metrics.empty()) {
            desc += " " + metrics;
          }
          mvwprintw(taskWin, y, 36, "%s", desc.c_str());

          if (isSelected) {
            wattroff(taskWin, COLOR_PAIR(10) | A_BOLD);
          }
        }
      }

      // Draw Completed Tasks History Panel
      wattron(taskWin, COLOR_PAIR(6) | A_DIM);
      mvwprintw(taskWin, splitLine, 1, "%s", separator.c_str());
      wattroff(taskWin, COLOR_PAIR(6) | A_DIM);

      wattron(taskWin, COLOR_PAIR(1) | A_BOLD);
      mvwprintw(taskWin, splitLine, 2, " Completed Tasks Log");
      wattroff(taskWin, COLOR_PAIR(1) | A_BOLD);

      int historyStartY = splitLine + 1;
      int historyMaxLines = h - 3 - historyStartY; // e.g. 18 - 3 - 11 = 4 lines
      std::vector<std::string> logsCopy;
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        logsCopy = taskHistoryLogs;
      }

      if (logsCopy.empty()) {
        wattron(taskWin, A_DIM);
        mvwprintw(taskWin, historyStartY + 1, (w - 20) / 2, "No task logs recorded");
        wattroff(taskWin, A_DIM);
      } else {
        int logCount = logsCopy.size();
        int showCount = std::min(historyMaxLines, logCount);
        for (int i = 0; i < showCount; ++i) {
          std::string logLine = logsCopy[logCount - 1 - i]; // newest first
          if ((int)logLine.length() > w - 6) {
            logLine = utf8_safe_truncate(logLine, w - 6);
          }
          mvwprintw(taskWin, historyStartY + i, 2, "%s", logLine.c_str());
        }
      }

      std::string instr = "j/k: Navigate | Space/p: Pause/Resume | c: Clear logs | x/d: Kill | Esc: Close";
      if ((int)instr.length() > w - 4) {
        instr = "j/k: Nav | Space/p: Pause | c: Clr | x: Kill | Esc: Close";
      }
      wattron(taskWin, A_DIM);
      mvwprintw(taskWin, h - 2, 2, "%s", instr.c_str());
      wattroff(taskWin, A_DIM);

      wrefresh(taskWin);

      int ch = wgetch(taskWin);
      if (ch != ERR) {
        if (ch == 'c' || ch == 'C') {
          std::lock_guard<std::mutex> lock(taskMutex);
          for (auto& t : activeTasks) {
            if (t->isFinished.load() && t->workerThread.joinable()) {
              t->workerThread.join();
            }
          }
          activeTasks.erase(
            std::remove_if(activeTasks.begin(), activeTasks.end(), 
                           [](const std::shared_ptr<AsyncTask>& t) { return t->isFinished.load(); }),
            activeTasks.end()
          );
          taskHistoryLogs.clear();
        } else if (ch == 'j' || ch == KEY_DOWN) {
          if (!tasksCopy.empty() && highlightedIndex < tasksCopy.size() - 1) {
            highlightedIndex++;
          }
        } else if (ch == 'k' || ch == KEY_UP) {
          if (highlightedIndex > 0) {
            highlightedIndex--;
          }
        } else if (ch == 'x' || ch == 'X' || ch == 'd' || ch == 'D') {
          if (!tasksCopy.empty() && highlightedIndex < tasksCopy.size()) {
            auto taskToCancel = tasksCopy[highlightedIndex];
            cancelTask(taskToCancel);
          }
        } else if (ch == ' ' || ch == 'p' || ch == 'P') {
          if (!tasksCopy.empty() && highlightedIndex < tasksCopy.size()) {
            auto taskToToggle = tasksCopy[highlightedIndex];
            toggleTaskPause(taskToToggle);
          }
        } else if (ch == 27 || ch == 'q' || ch == 'Q') {
          break;
        }
      }
    }

    timeout(50);
    delwin(taskWin);
    updateLayout();
  }

  void drawPreview() {
    if (!winPreview) return;
    if (isDualPaneMode) {
      size_t rightIdx = rightTabIndex;
      if (activeTabIndex == rightIdx) {
        drawPane(winPreview, currentPath, currentFiles, selectedIndex, scrollOffset, multiSelection, isSearching, isTrashMode, true);
      } else {
        loadInactiveTabDirectoryIfNeeded(rightIdx);
        drawPane(winPreview, tabs[rightIdx].currentPath, tabs[rightIdx].currentFiles,
                 tabs[rightIdx].selectedIndex, tabs[rightIdx].scrollOffset,
                 tabs[rightIdx].multiSelection, tabs[rightIdx].isSearching, tabs[rightIdx].isTrashMode, false);
      }
      return;
    }

    bool samePathAndImage = false;
    if (!currentFiles.empty() && selectedIndex < currentFiles.size()) {
      const auto& nextFile = currentFiles[selectedIndex];
      std::string extLower = nextFile.extension;
      std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
      bool isArchive = (extLower == ".zip" || extLower == ".tar" || extLower == ".gz" || extLower == ".tgz" || 
                        extLower == ".rar" || extLower == ".bz2" || extLower == ".xz" || extLower == ".7z");
      bool isAudio = (extLower == ".mp3" || extLower == ".wav" || extLower == ".flac" || extLower == ".ogg" || 
                      extLower == ".m4a" || extLower == ".aac" || extLower == ".opus" || extLower == ".wma");
      bool isCode = isCodeFile(nextFile.extension);
      bool isTextPreviewable = isCode || isArchive || isAudio;
      
      bool isVid = VIDEO_EXTS.count(nextFile.extension);
      bool isImg = IMAGE_EXTS.count(nextFile.extension);
      
      if (nextFile.path.string() == cachedPath && !isTextPreviewable && (isVid || isImg)) {
        samePathAndImage = true;
      }
    }

    pendingDirectRenderType = PreviewType::NONE;
    if (lastWasDirectRender && !samePathAndImage)
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
    std::string dispName = file.name;
    int titleMaxW = getmaxx(winPreview) - 8;
    if (titleMaxW < 5) titleMaxW = 5;
    if ((int)dispName.length() > titleMaxW) {
      int limit = titleMaxW - 3;
      if (limit < 1) limit = 1;
      dispName = utf8_safe_truncate(dispName, limit);
    }
    mvwprintw(winPreview, 1, 2, " %s ", dispName.c_str());
    wattroff(winPreview, A_BOLD | COLOR_PAIR(1));

    wattron(winPreview, A_DIM);
    std::string previewSizeStr;
    if (file.is_directory) {
      if (file.path.string().find("/gvfs/") != std::string::npos) {
        previewSizeStr = "DIR";
      } else {
        uintmax_t sizeVal = 0;
        bool cached = false;
        {
          std::lock_guard<std::mutex> lock(cacheMutex);
          auto it = dirSizeCache.find(file.path.string());
          if (it != dirSizeCache.end()) {
            sizeVal = it->second;
            cached = true;
          }
        }
        if (cached) {
          previewSizeStr = formatSize(sizeVal);
        } else {
          previewSizeStr = "Calculating...";
          std::lock_guard<std::mutex> qLock(queueMutex);
          bool alreadyQueued = false;
          for (const auto& job : sizeQueue) {
            if (job.path == file.path) {
              alreadyQueued = true;
              break;
            }
          }
          if (!alreadyQueued) {
            sizeQueue.push_back({file.path, currentViewId.load()});
            queueCv.notify_one();
          }
        }
      }
    } else {
      previewSizeStr = formatSize(file.size);
    }
    mvwprintw(winPreview, 2, 2, " Size: %s", previewSizeStr.c_str());

    std::string typeStr;
    if (file.is_symlink) {
      typeStr = "Symlink -> ";
      if (!file.symlink_target_exists) {
        typeStr += "[Broken]";
      } else if (file.is_symlink_directory) {
        typeStr += "Directory";
      } else {
        typeStr += (file.extension.empty() ? "File" : file.extension.c_str());
      }
    } else {
      typeStr = file.is_directory ? "Directory"
                                  : (file.extension.empty() ? "File" : file.extension.c_str());
    }
    mvwprintw(winPreview, 3, 2, " Type: %s", typeStr.c_str());
    mvwprintw(winPreview, 4, 2, " Modified: %s", getFileModifiedTime(file.path).c_str());
    wattroff(winPreview, A_DIM);

    int dividerLine = 5;
    if (isTrashMode) {
      TrashInfo ti = getTrashInfo(file.path);
      std::string orig = ti.originalPath;
      int maxPathW = getmaxx(winPreview) - 15;
      if (maxPathW < 10) maxPathW = 10;
      if ((int)orig.length() > maxPathW) {
        orig = utf8_safe_truncate_left(orig, maxPathW - 3);
      }
      wattron(winPreview, A_DIM);
      mvwprintw(winPreview, 5, 2, " Original: %s", orig.c_str());
      mvwprintw(winPreview, 6, 2, " Deleted:  %s", ti.deletionDate.c_str());
      wattroff(winPreview, A_DIM);
      dividerLine = 7;
    } else if (file.is_symlink) {
      wattron(winPreview, A_DIM);
      mvwprintw(winPreview, 5, 2, " Target: %s", file.symlink_target.c_str());
      wattroff(winPreview, A_DIM);
      dividerLine = 6;
    }

    wattron(winPreview, COLOR_PAIR(6));
    for (int i = 1; i < getmaxx(winPreview) - 1; ++i)
      mvwaddstr(winPreview, dividerLine, i, "─");
    wattroff(winPreview, COLOR_PAIR(6));

    bool isVid = VIDEO_EXTS.count(file.extension);
    bool isImg = IMAGE_EXTS.count(file.extension);
    bool isCode = isCodeFile(file.extension);

    std::string extLower = file.extension;
    std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
    bool isArchive = (extLower == ".zip" || extLower == ".tar" || extLower == ".gz" || extLower == ".tgz" || 
                      extLower == ".rar" || extLower == ".bz2" || extLower == ".xz" || extLower == ".7z");
    bool isAudio = (extLower == ".mp3" || extLower == ".wav" || extLower == ".flac" || extLower == ".ogg" || 
                    extLower == ".m4a" || extLower == ".aac" || extLower == ".opus" || extLower == ".wma");
    bool isPdf = (extLower == ".pdf");
    bool isTextPreviewable = isCode || isArchive || isAudio || isPdf;

    bool isDoc = (file.extension == ".doc" || file.extension == ".docx");
    bool isXls = (file.extension == ".xls" || file.extension == ".xlsx");
    bool isPpt = (file.extension == ".ppt" || file.extension == ".pptx");

    int contentStart = getPreviewContentStartLine();

    if (file.is_directory) {
      wattron(winPreview, COLOR_PAIR(1) | A_BOLD);
      mvwprintw(winPreview, contentStart, 2, "󰉖 Content:");
      wattroff(winPreview, COLOR_PAIR(1) | A_BOLD);
      try {
        int line = contentStart + 1;
        for (const auto& entry : fs::directory_iterator(file.path)) {
          if (!showHidden && entry.path().filename().string().front() == '.')
            continue;
          if (line >= height - 3)
            break;
          std::string subName = entry.path().filename().string();
          int maxSubW = getmaxx(winPreview) - 8;
          if (maxSubW < 5) maxSubW = 5;
          if ((int)subName.length() > maxSubW) {
            int limit = maxSubW - 3;
            if (limit < 1) limit = 1;
            subName = utf8_safe_truncate(subName, limit);
          }

          std::string ext = entry.path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          
          bool isSubSym = fs::is_symlink(fs::symlink_status(entry.path()));
          bool isSubDir = false;
          if (isSubSym) {
            try {
              fs::path resSub = fs::read_symlink(entry.path());
              if (resSub.is_relative()) {
                resSub = entry.path().parent_path() / resSub;
              }
              isSubDir = fs::is_directory(resSub);
            } catch (...) {}
          } else {
            isSubDir = fs::is_directory(entry);
          }

          bool isSubDirEmpty = false;
          if (isSubDir) {
            try {
              isSubDirEmpty = (fs::directory_iterator(entry.path()) == fs::directory_iterator());
            } catch (...) {}
          }
          FileStyle s = getFileStyle(entry.path().filename().string(), ext, isSubDir, isSubDirEmpty);
          if (isSubSym) {
            s.icon = ICON_LINK;
          }

          wattron(winPreview, COLOR_PAIR(s.pair));
          mvwprintw(winPreview, line++, 4, "%s %s", s.icon, subName.c_str());
          wattroff(winPreview, COLOR_PAIR(s.pair));
        }
      } catch (...) {
      }
      wnoutrefresh(winPreview);
    } else if (isDoc || isXls || isPpt) {
      wattron(winPreview, COLOR_PAIR(8));
      if (isDoc)
        mvwprintw(winPreview, contentStart, 2, " [Word Document - No Preview] ");
      else if (isXls)
        mvwprintw(winPreview, contentStart, 2, " [Excel Spreadsheet - No Preview] ");
      else if (isPpt)
        mvwprintw(winPreview, contentStart, 2, " [PowerPoint Presentation - No Preview] ");
      wattroff(winPreview, COLOR_PAIR(8));
      wnoutrefresh(winPreview);
    } else if (isVid || isImg || isTextPreviewable) {
      bool isGvfs = (file.path.string().find("/gvfs/") != std::string::npos);
      if ((isVid || isImg) && isGvfs) {
        wattron(winPreview, COLOR_PAIR(8));
        mvwprintw(winPreview, contentStart, 2, " [Media File - No Preview on MTP] ");
        wattroff(winPreview, COLOR_PAIR(8));
        wnoutrefresh(winPreview);
      } else if ((isVid || isImg) && (!isCommandAvailable("ffmpeg") || !isCommandAvailable("ffprobe"))) {
        wattron(winPreview, COLOR_PAIR(8));
        mvwprintw(winPreview, contentStart, 2, " [Media File - Install ffmpeg & ffprobe for preview] ");
        wattroff(winPreview, COLOR_PAIR(8));
        wnoutrefresh(winPreview);
      } else {
        bool match = false;
        {
          std::lock_guard<std::mutex> lock(previewMutex);
          if (cachedPath == file.path.string())
            match = true;
        }
        if (match) {
          if (isTextPreviewable)
            drawCachedTextPreview();
          else
            pendingDirectRenderType = PreviewType::IMAGE;
        } else if (requestedPath != file.path.string()) {
          wattron(winPreview, A_ITALIC | A_DIM);
          mvwprintw(winPreview, contentStart, 4, "Generating preview...");
          wattroff(winPreview, A_ITALIC | A_DIM);
          PreviewType type = isTextPreviewable ? PreviewType::TEXT : PreviewType::IMAGE;
          startAsyncPreview(file.path.string(), type, maxH - (contentStart + 1), maxW);
        }
      }
    } else {
      if (is_binary_file(file.path.string())) {
        wattron(winPreview, COLOR_PAIR(8));
        mvwprintw(winPreview, contentStart, 2, " [Binary File - No Preview] ");
        wattroff(winPreview, COLOR_PAIR(8));
      } else {
        std::ifstream f(file.path);
        if (f.is_open()) {
          std::string lineStr;
          int line = contentStart;
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

    std::vector<fs::path> pathsToOpen;
    if (!multiSelection.empty()) {
      for (const auto& p : multiSelection) {
        pathsToOpen.push_back(p);
      }
    } else {
      pathsToOpen.push_back(currentFiles[selectedIndex].path);
    }

    if (pathsToOpen.empty())
      return;

    if (pathsToOpen.size() == 1 && fs::is_directory(pathsToOpen[0])) {
      clearDirectRender();
      currentPath = pathsToOpen[0];
      selectedIndex = 0;
      scrollOffset = 0;
      isSearching = false;
      reloadAll();
      return;
    }

    std::vector<fs::path> mediaFiles;
    std::vector<fs::path> codeFiles;
    std::vector<fs::path> otherFiles;

    for (const auto& p : pathsToOpen) {
      if (fs::is_directory(p)) continue;
      std::string ext = p.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

      bool isKnownBinary = (ext == ".pdf" || ext == ".zip" || ext == ".7z" || ext == ".rar" || 
                            ext == ".tar" || ext == ".gz" || ext == ".tgz" || ext == ".bz2" || 
                            ext == ".xz" || ext == ".doc" || ext == ".docx" || ext == ".xls" || 
                            ext == ".xlsx" || ext == ".ppt" || ext == ".pptx" || ext == ".epub" || 
                            ext == ".odt" || ext == ".ods" || ext == ".odp" || ext == ".png" || 
                            ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".webp" || 
                            ext == ".bmp" || ext == ".ico" || ext == ".exe" || ext == ".dll" || 
                            ext == ".so" || ext == ".o" || ext == ".a" || ext == ".bin");

      if (VIDEO_EXTS.count(ext) || AUDIO_EXTS.count(ext)) {
        if (isCommandAvailable("mpv")) {
          mediaFiles.push_back(p);
        } else {
          otherFiles.push_back(p);
        }
      } else if (isKnownBinary) {
        otherFiles.push_back(p);
      } else if (isCodeFile(ext) || !is_binary_file(p.string())) {
        codeFiles.push_back(p);
      } else {
        otherFiles.push_back(p);
      }
    }

    if (mediaFiles.empty() && codeFiles.empty() && otherFiles.empty()) {
      const auto& file = currentFiles[selectedIndex];
      if (file.is_directory) {
        clearDirectRender();
        changeDirectory(file.path, true);
        selectedIndex = 0;
        scrollOffset = 0;
        isSearching = false;
        reloadAll();
      }
      return;
    }

    bool needsSuspension = !codeFiles.empty() || !mediaFiles.empty();
    if (needsSuspension) {
      suspendTerminal();
    }

    if (!codeFiles.empty()) {
      const char* editor = getenv("EDITOR");
      if (!editor)
        editor = getenv("VISUAL");
      if (!editor) {
        if (isCommandAvailable("nvim"))
          editor = "nvim";
        else if (isCommandAvailable("nano"))
          editor = "nano";
        else
          editor = "vi";
      }
      std::string cmd = std::string(editor);
      for (const auto& f : codeFiles) {
        cmd += " " + escapeShellArg(f.string());
      }
      int res = system(cmd.c_str());
      (void)res;
    }

    if (!mediaFiles.empty()) {
      std::string cmd = "mpv";
      for (const auto& f : mediaFiles) {
        cmd += " " + escapeShellArg(f.string());
      }
      cmd += " 2> /dev/null";
      int res = system(cmd.c_str());
      (void)res;
    }

    if (!otherFiles.empty()) {
      for (const auto& f : otherFiles) {
        std::string cmd;
#ifdef __APPLE__
        cmd = "open " + escapeShellArg(f.string());
#else
        cmd = "xdg-open " + escapeShellArg(f.string());
#endif
        cmd += " > /dev/null 2>&1 &";
        int res = system(cmd.c_str());
        (void)res;
      }
    }

    if (needsSuspension) {
      resumeTerminal();
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
      changeDirectory(currentPath.parent_path(), true);
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
      if (inotifyTriggered) {
        inotifyTriggered = false;
        reloadAll();
        needsRedraw = true;
      }
      if (devicesTriggered) {
        devicesTriggered = false;
        reloadAll();
        needsRedraw = true;
      }
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
            if (sortMode == SortMode::SIZE)
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
      // Check for completed async tasks
      {
        std::lock_guard<std::mutex> lock(taskMutex);
        bool shouldReload = false;
        for (auto& task : activeTasks) {
          if (task->isFinished && !task->notified) {
            task->notified = true;
            setStatus(task->type + " task completed: " + task->statusMessage);
            shouldReload = true;
            needsRedraw = true;
          }
        }
        if (shouldReload) {
          reloadAll();
        }
      }

      if (needsRedraw) {
        if (!currentFiles.empty()) {
          if (selectedIndex >= currentFiles.size())
            selectedIndex = currentFiles.size() - 1;
        } else
          selectedIndex = 0;

        drawTabs();
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
        std::string pathStr = isTrashMode ? "trash://" : currentPath.string();
        int maxPathW = width - 35;
        if (maxPathW < 10) maxPathW = 10;
        if ((int)pathStr.length() > maxPathW) {
          pathStr = utf8_safe_truncate_left(pathStr, maxPathW - 3);
        }
        printw(" %s", pathStr.c_str());
        attroff(A_DIM);

        if (!isTrashMode) {
          try {
            fs::space_info si = fs::space(currentPath);
            if (si.capacity > 0) {
              double pct = (double)(si.capacity - si.available) / si.capacity * 100.0;
              int filled = (int)(pct / 10.0 + 0.5);
              if (filled < 0) filled = 0;
              if (filled > 10) filled = 10;
              
              std::string bar = "";
              for (int j = 0; j < filled; ++j) bar += "█";
              for (int j = filled; j < 10; ++j) bar += "░";
              
              std::string rootName = getMountPoint(currentPath).filename().string();
              if (rootName.empty()) rootName = "Root";
              
              std::string freeStr = formatSize(si.available);
              std::string capStr = formatSize(si.capacity);
              
              char diskBuf[128];
              std::snprintf(diskBuf, sizeof(diskBuf), " 󰋊 %s: [%s] %.0f%% (%s/%s free)", 
                            rootName.c_str(), bar.c_str(), pct, freeStr.c_str(), capStr.c_str());
              
              int currentX = getcurx(stdscr);
              if (width - currentX > (int)std::string(diskBuf).length() + 35) {
                attron(COLOR_PAIR(6));
                printw("%s", diskBuf);
                attroff(COLOR_PAIR(6));
              }
            }
          } catch (...) {}
        }

        int rightOffset = 2; // Right padding

        int runningCount = 0;
        {
          std::lock_guard<std::mutex> lock(taskMutex);
          for (const auto& t : activeTasks) {
            if (!t->isFinished) {
              runningCount++;
            }
          }
        }
        if (runningCount > 0) {
          int visualLen = 12 + std::to_string(runningCount).length();
          attron(COLOR_PAIR(4) | A_BOLD);
          mvprintw(height - 1, width - visualLen - 2, "󰙵  Tasks • %d", runningCount);
          attroff(COLOR_PAIR(4) | A_BOLD);
          rightOffset += visualLen + 2;
        }

        if (!clipboard.paths.empty()) {
          int count = clipboard.paths.size();
          std::string clipStr;
          int colorPair = 0;
          if (clipboard.isCut) {
            clipStr = "󰆐  Cut • " + std::to_string(count);
            colorPair = 8; // Red/warning
          } else {
            clipStr = "󰆏  Yank • " + std::to_string(count);
            colorPair = 28; // Purple/Font
          }
          int visualLen = (clipboard.isCut ? 10 : 11) + std::to_string(count).length();
          attron(COLOR_PAIR(colorPair) | A_BOLD);
          mvprintw(height - 1, width - rightOffset - visualLen, "%s", clipStr.c_str());
          attroff(COLOR_PAIR(colorPair) | A_BOLD);
        }

        drawStatusToast();

        flushScreen();
        needsRedraw = false;
      }

      int ch = getch();
      if (ch == 27) {
        nodelay(stdscr, TRUE);
        int ch1 = getch();
        int ch2 = getch();
        int ch3 = getch();
        int ch4 = getch();
        int ch5 = getch();
        nodelay(stdscr, FALSE);

        if (ch1 == '[' && ch2 == '2' && ch3 == '0' && ch4 == '0' && ch5 == '~') {
          std::string pastedData = "";
          while (true) {
            int c = getch();
            if (c == ERR) {
              std::this_thread::sleep_for(std::chrono::milliseconds(5));
              c = getch();
              if (c == ERR) break;
            }
            if (c == 27) {
              nodelay(stdscr, TRUE);
              int e1 = getch();
              int e2 = getch();
              int e3 = getch();
              int e4 = getch();
              int e5 = getch();
              nodelay(stdscr, FALSE);
              if (e1 == '[' && e2 == '2' && e3 == '0' && e4 == '1' && e5 == '~') {
                break;
              } else {
                pastedData += (char)c;
                if (e1 != ERR) pastedData += (char)e1;
                if (e2 != ERR) pastedData += (char)e2;
                if (e3 != ERR) pastedData += (char)e3;
                if (e4 != ERR) pastedData += (char)e4;
                if (e5 != ERR) pastedData += (char)e5;
              }
            } else {
              pastedData += (char)c;
            }
          }
          handlePastedData(pastedData);
          needsRedraw = true;
          continue;
        } else {
          if (ch1 == ERR) {
            clearSelection();
          }
          needsRedraw = true;
          continue;
        }
      }

      if (ch == KEY_MOUSE) {
        MEVENT event;
        if (getmouse(&event) == OK) {
          #ifndef BUTTON4_PRESSED
          #define BUTTON4_PRESSED 0x10000
          #endif
          #ifndef BUTTON5_PRESSED
          #define BUTTON5_PRESSED 0x200000
          #endif
          if (event.bstate & BUTTON4_PRESSED) {
            if (focusPinned) {
              if (pinnedIndex > 0) pinnedIndex--;
            } else {
              if (selectedIndex > 0) selectedIndex--;
            }
            needsRedraw = true;
          } else if (event.bstate & BUTTON5_PRESSED) {
            if (focusPinned) {
              if (!pinnedPaths.empty() && pinnedIndex < pinnedPaths.size() - 1) pinnedIndex++;
            } else {
              if (!currentFiles.empty() && selectedIndex < currentFiles.size() - 1) selectedIndex++;
            }
            needsRedraw = true;
          }
        }
        continue;
      }
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

      // Check for custom key bindings macro first!
      if (!focusPinned && ch != ERR) {
        auto macroIt = customMacros.find(ch);
        if (macroIt != customMacros.end()) {
          executeMacro(macroIt->second);
          continue;
        }
      }

      if (ch == 'q') {
        bool active = false;
        {
          std::lock_guard<std::mutex> lock(taskMutex);
          for (const auto& t : activeTasks) {
            if (!t->isFinished) {
              active = true;
              break;
            }
          }
        }
        if (active) {
          std::string confirm = promptInput("Quit with active tasks in progress? (y/n)");
          if (confirm != "y" && confirm != "Y") {
            needsRedraw = true;
            continue;
          }
        }
        return;
      }
      if (ch == KEY_RESIZE) {
        clearDirectRender();
        updateLayout();
        needsRedraw = true;
        continue;
      }
      if (ch == 18 || ch == KEY_F(5)) { // Ctrl+R or F5
        handleRefresh();
        continue;
      }
      if (ch == KEY_F(2)) {
        toggleDualPaneMode();
        continue;
      }
      if (ch == KEY_F(3)) { // F3 (Toggle Preview Pane visibility)
        if (!isDualPaneMode) {
          hidePreview = !hidePreview;
          updateLayout();
          reloadAll();
          setStatus(hidePreview ? "Preview hidden" : "Preview visible");
          needsRedraw = true;
        }
        continue;
      }
      if (ch == KEY_F(4)) { // F4 (Toggle Parent Pane visibility)
        if (!isDualPaneMode) {
          hideParent = !hideParent;
          updateLayout();
          reloadAll();
          setStatus(hideParent ? "Parent pane hidden" : "Parent pane visible");
          needsRedraw = true;
        }
        continue;
      }
      if (ch == 7) { // Ctrl+G (Grow focused pane width)
        if (isDualPaneMode) {
          if (focusLeftPane) {
            dualPaneSplitOffset++;
          } else {
            dualPaneSplitOffset--;
          }
          updateLayout();
          reloadAll();
          setStatus("Split adjusted (left: " + std::to_string(width / 2 + dualPaneSplitOffset) + " cols, right: " + std::to_string(width - (width / 2 + dualPaneSplitOffset)) + " cols)");
          needsRedraw = true;
        }
        continue;
      }
      if (ch == 2 || ch == 8) { // Ctrl+B or Ctrl+H (Shrink focused pane width)
        if (isDualPaneMode) {
          if (focusLeftPane) {
            dualPaneSplitOffset--;
          } else {
            dualPaneSplitOffset++;
          }
          updateLayout();
          reloadAll();
          setStatus("Split adjusted (left: " + std::to_string(width / 2 + dualPaneSplitOffset) + " cols, right: " + std::to_string(width - (width / 2 + dualPaneSplitOffset)) + " cols)");
          needsRedraw = true;
        }
        continue;
      }
      if (ch == '\t') {
        if (isDualPaneMode) {
          size_t nextTab = (activeTabIndex == leftTabIndex) ? rightTabIndex : leftTabIndex;
          switchTab(nextTab);
          focusLeftPane = (activeTabIndex == leftTabIndex);
        } else {
          focusPinned = !focusPinned;
        }
        continue;
      }

      if (ch == 't') {
        createTab();
        continue;
      }
      if (ch == 'W' || ch == 23) {
        closeTab();
        continue;
      }
      if (ch == '[') {
        if (activeTabIndex > 0) {
          switchTab(activeTabIndex - 1);
        } else {
          switchTab(tabs.size() - 1);
        }
        continue;
      }
      if (ch == ']') {
        if (activeTabIndex < tabs.size() - 1) {
          switchTab(activeTabIndex + 1);
        } else {
          switchTab(0);
        }
        continue;
      }
      if (ch >= '0' && ch <= '9') {
        size_t targetIdx = (ch == '0') ? 9 : (ch - '1');
        if (targetIdx < tabs.size()) {
          switchTab(targetIdx);
        }
        continue;
      }
      if (ch == 15) { // Ctrl+O
        handleGoBack();
        continue;
      }
      if (ch == 16) { // Ctrl+P
        handleGoForward();
        continue;
      }
      if (ch == 'H') {
        drawHistoryOverlay();
        continue;
      }

      if (focusPinned) {
        switch (ch) {
        case 'j':
        case KEY_DOWN:
          if (!pinnedPaths.empty()) {
            if (pinnedIndex < pinnedPaths.size() - 1)
              pinnedIndex++;
            else
              pinnedIndex = 0;
          }
          break;
        case 'k':
        case KEY_UP:
          if (!pinnedPaths.empty()) {
            if (pinnedIndex > 0)
              pinnedIndex--;
            else
              pinnedIndex = pinnedPaths.size() - 1;
          }
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
          if (!currentFiles.empty()) {
            if (selectedIndex < currentFiles.size() - 1)
              selectedIndex++;
            else
              selectedIndex = 0;
          }
          break;
        case 'k':
        case KEY_UP:
          if (!currentFiles.empty()) {
            if (selectedIndex > 0)
              selectedIndex--;
            else
              selectedIndex = currentFiles.size() - 1;
          }
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
        case 4: // Ctrl-D (Drag Out)
          handleDragOut();
          break;
        case 27:
          clearSelection();
          break;
        case 'y':
          handleCopy();
          break;
        case 'Y':
          handlePasteSymlink();
          break;
        case 'd':
        case KEY_DC:
          if (isTrashMode) {
            handleDelete();
          } else {
            handleMoveToTrash();
          }
          break;
        case 'D':
          handleDelete();
          break;
        case 'T':
          toggleTrashMode();
          break;
        case 'x':
          handleCut();
          break;
        case 'u':
          if (!isTrashMode) {
            handleUndoTrash();
          }
          break;
        case 'p':
          handlePaste();
          break;
        case 'r':
          if (isTrashMode) {
            handleRestoreFromTrash();
          } else {
            handleRename();
          }
          break;
        case 'e':
          if (isTrashMode) {
            handleEmptyTrash();
          } else {
            handleExtract();
          }
          break;
        case 'm':
          drawDevicesOverlay();
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
        case ':':
          handleShellCommand();
          break;
        case '/':
          handleSearch();
          break;
        case 'i':
          showFileDetails();
          break;
        case 'I':
          drawPermissionsOverlay();
          break;
        case 'f':
          handleFuzzyFind();
          break;
        case 'w':
          drawTasksOverlay();
          break;
        case '?':
          drawHelpOverlay();
          break;
        }
      }
    }
  }
};

#endif // FILE_MANAGER_H
