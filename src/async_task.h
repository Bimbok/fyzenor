#ifndef ASYNC_TASK_H
#define ASYNC_TASK_H

#include "utils.h"
#include <atomic>
#include <thread>

struct AsyncTask {
  int id;
  std::string type; // "Copy", "Move", "Delete", "Zip", "Extract"
  std::string description;
  std::atomic<int> progress{0}; // 0 to 100
  std::atomic<bool> isFinished{false};
  std::atomic<bool> isCancelled{false};
  bool notified = false;
  std::string statusMessage = "Running";
  fs::path destPath;
  std::thread workerThread;
};

#endif // ASYNC_TASK_H
