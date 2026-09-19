#include "file_manager.h"
#include "utils.h"
#include <iostream>

int main(int argc, char* argv[]) {
  globalStartTime = std::chrono::steady_clock::now();
  std::string startPath = "";
  std::string chooserFile = "";
  std::string cwdFile = "";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-v" || arg == "--version") {
      std::cout << "Fyzenor version " << FYZENOR_VERSION << std::endl;
      return 0;
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Fyzenor - The Blazing Fast, Modern C++ Terminal File Manager" << std::endl;
      std::cout << "Usage: fyzenor [options] [path]" << std::endl;
      std::cout << "\nArguments:" << std::endl;
      std::cout << "  [path]                Directory to open, or file to select" << std::endl;
      std::cout << "\nOptions:" << std::endl;
      std::cout << "  --chooser-file <file> Write chosen files to this file on exit" << std::endl;
      std::cout << "  --cwd-file <file>     Write the last working directory to this file on exit"
                << std::endl;
      std::cout << "  -v, --version         Show version information" << std::endl;
      std::cout << "  -h, --help            Show this help message" << std::endl;
      return 0;
    } else if (arg == "--chooser-file") {
      if (i + 1 < argc) {
        chooserFile = argv[++i];
      }
    } else if (arg.rfind("--chooser-file=", 0) == 0) {
      chooserFile = arg.substr(15);
    } else if (arg == "--cwd-file") {
      if (i + 1 < argc) {
        cwdFile = argv[++i];
      }
    } else if (arg.rfind("--cwd-file=", 0) == 0) {
      cwdFile = arg.substr(11);
    } else if (!arg.empty() && arg[0] != '-') {
      if (startPath.empty()) {
        startPath = arg;
      }
    }
  }

  loadConfiguration();
  FileManager fm(startPath, chooserFile, cwdFile);
  fm.run();
  return 0;
}
