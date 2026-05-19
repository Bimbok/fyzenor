# Contributing to Fyzenor

Thank you for contributing to Fyzenor.

## Before You Start

- Check existing issues and pull requests before starting work.
- Open an issue first for large features, UI changes, or behavioral changes.
- Keep pull requests focused. Avoid bundling unrelated changes together.

## Local Setup

### Prerequisites
- `g++` with full C++17 filesystem support (GCC >= 8 recommended)
- `libncursesw` development package
- `ffmpeg`
- `zip`
- `bat` or `batcat`

### Windows Notes

Older MinGW GCC versions on Windows may not fully support the `<filesystem>` header required by Fyzenor.

If you encounter:

```bash
fatal error: filesystem: No such file or directory
```

Recommended alternatives:
- MSYS2 MinGW-w64
- WSL (Windows Subsystem for Linux)

Check compiler version:

```bash
g++ --version
```

### Build

```bash
git clone https://github.com/Bimbok/fyzenor.git
cd fyzenor
g++ -std=c++17 -O3 file_manager.cpp -o fyzenor -lncursesw -lpthread
./fyzenor
```

### Installer

```bash
chmod +x install.sh
./install.sh
```

## Ways to Contribute

- Fix bugs
- Improve performance
- Add or refine file operations
- Improve preview behavior
- Improve theming and terminal compatibility
- Improve documentation

## Coding Guidelines

- Follow the current code style in `file_manager.cpp`.
- Keep changes minimal and targeted.
- Prefer clear, readable logic over clever abstractions.
- Do not introduce unnecessary dependencies.
- Preserve keyboard-driven workflows and existing UX patterns unless the change explicitly improves them.

## Pull Request Guidelines

1. Fork the repository and create a feature branch from `main`.
2. Make your changes in small, reviewable commits.
3. Test the change locally.
4. Update `README.md` if behavior, setup, controls, or dependencies changed.
5. Open a pull request with:
   - A clear title
   - A concise summary
   - Screenshots or terminal captures for UI changes
   - Notes about platform or terminal assumptions when relevant

## Commit Messages

Use descriptive commit messages. Examples:

- `fix: prevent preview refresh flicker on fast navigation`
- `feat: improve pin navigation behavior`
- `docs: update installation instructions`

## Reporting Bugs

When opening a bug report, include:

- Your OS and terminal emulator
- Whether Kitty graphics support is available
- Steps to reproduce
- Expected behavior
- Actual behavior
- Screenshots or terminal output if relevant

## Code of Conduct

By participating in this project, you agree to follow the guidelines in [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
