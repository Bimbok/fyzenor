#include "file_manager.h"
#include <iostream>

const std::string VERSION = "3.0.0";

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
