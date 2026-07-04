cmake_minimum_required(VERSION 3.12)
project(fyzenor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Ncurses with wide-character support (ncursesw)
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

# Define the executable and its sources
add_executable(fyzenor
    src/main.cpp
    src/utils.cpp
    src/file_entry.cpp
    src/file_manager.cpp
)

# Include directories
target_include_directories(fyzenor PRIVATE src ${CURSES_INCLUDE_DIRS})

# Link libraries
target_link_libraries(fyzenor PRIVATE ${CURSES_LIBRARIES} pthread)
