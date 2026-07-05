#ifndef ASYNC_TASK_H
#define ASYNC_TASK_H

#include "utils.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

struct AsyncTask {
  int id;
  std::string type; // "Copy", "Move", "Delete", "Zip", "Extract"
  std::string description;
  std::atomic<int> progress{0}; // 0 to 100
  std::atomic<bool> isFinished{false};
  std::atomic<bool> isCancelled{false};
  std::atomic<bool> isPaused{false};
  bool notified = false;
  std::string statusMessage = "Running";
  fs::path destPath;
  std::thread workerThread;

  std::mutex pauseMutex;
  std::condition_variable pauseCv;

  void checkPause() {
    if (isPaused) {
      std::unique_lock<std::mutex> lock(pauseMutex);
      pauseCv.wait(lock, [this]() { return !isPaused || isCancelled; });
    }
  }
};

#endif // ASYNC_TASK_H
