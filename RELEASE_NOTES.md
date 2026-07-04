# Fyzenor v2.0.0 Release Notes

We are thrilled to announce **Fyzenor v2.0.0**, a major milestone release representing a significant leap forward in architecture, usability, performance, and features. This release introduces a complete modular refactoring of the C++ codebase, migrates the build pipeline to CMake, and resolves key user experience issues.

---

## 🚀 Key Highlights & New Features

### 📦 1. Comprehensive Archive Extraction Support (`e`)
Fyzenor can now unzip and extract archives natively in the background.
- **Supported Formats**: `.zip`, `.tar`, `.tar.gz`/`.tgz`, `.tar.bz2`/`.tbz2`, `.tar.xz`/`.txz`, `.7z`, and `.rar`.
- **Keyboard Shortcut**: Press `e` on any selected archive to extract it to a subdirectory named after the archive.
- **Asynchronous Execution**: Extractions run on background threads without locking the UI. You can monitor progress or cancel running extraction jobs from the Tasks overlay (`w`).

### 🔄 2. Circular Navigation (Wrap-Around)
Navigation inside lists has been enhanced to follow modern TUI conventions (similar to `yazi`):
- Pressing `j` (or `Down`) on the last file wraps selection directly back to the first file at the top.
- Pressing `k` (or `Up`) on the first file wraps selection directly to the last file at the bottom.
- Pinned panels and main lists both dynamically update scroll offsets to match selection instantly.

### 🖱️ 3. Native Mouse Interception & Wheel Scrolling
- **Selection Interference Fix**: Enabled native ncurses mouse masks (`mousemask`) to capture click sequences. This prevents standard terminal emulators from overriding text selection and drawing white highlighting when you click/drag on the UI.
- **Scroll Wheel Support**: You can now use your mouse scroll wheel (Scroll Up/Down) to smoothly scroll through lists.

### 🖼️ 4.Kitty Image Preview Overlay Collision Fixes
- Terminal image rendering layers are now cleared instantly when overlays or dialog prompts (e.g. Help `?`, Tasks `w`, Devices `d`, Details `i`, or Shell Command `:`) are drawn.
- Graphics previews resume rendering automatically once those dialogs are closed.

### 📏 5. Compact Spacing & Panel Border Title Truncation
- **Indentation Gap Reduced**: Reduced the left padding margin before icons in file list views by exactly **2 columns** to eliminate empty space and align lists with the Pinned Panel layout.
- **Path Title Truncation**: Folder names displayed in pane borders that exceed the available terminal width are now dynamically truncated safely using UTF-8 width checks and appended with `...`, preventing line-wrapping bugs.

---

## 🛠️ Performance & Shutdown Optimizations

- **Startup Latency Reduced**: Optimized transitions in subfolders by updating parent loader routines to skip directory statistics calculations for hidden files when hidden file visibility is disabled.
- **Destructor Safety Auditing**: Verified all background threads, search pipes, inotify watchers, and direct Kitty rendering layers terminate and free resources cleanly when the program exits.

---

## 🏗️ Architectural Refactoring (Monolith to Modular C++)

The single, 5,565-line monolithic `file_manager.cpp` has been successfully modularized into discrete translation units under `src/` to follow professional C++ design guidelines:
- **`src/utils.h` / `src/utils.cpp`**: Constants, extensibility categories (file extension sets), themes, color configs, string width helpers, and low-level terminal controls.
- **`src/file_entry.h` / `src/file_entry.cpp`**: Represents file model entities and directories.
- **`src/async_task.h`**: Lightweight structure for monitoring thread pool progress.
- **`src/file_manager.h` / `src/file_manager.cpp`**: Core application controller, window panels, layouts, overlays, and keyboard loops.
- **`src/main.cpp`**: Program entry point.

### ⚙️ CMake Build Integration
- Added **`CMakeLists.txt`** to manage compiling and linking against `ncursesw` and `pthread`.
- Updated **`install.sh`** to install via CMake automatically.
- Updated GitHub **CI/CD Workflows** (`ci.yml` and `release.yml`) to compile, test, and package release assets using CMake.

---

## 📝 Upgrading

### Using the Installer (Recommended)
Simply pull the updates and run the installer:
```bash
./install.sh
```

### Manual Compilation
```bash
mkdir -p build && cd build
cmake ..
make
./fyzenor
```
