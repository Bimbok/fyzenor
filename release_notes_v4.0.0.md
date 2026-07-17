# Release Notes: Fyzenor v4.0.0 — Breaking Changes & TOML Configuration Migration

This major release introduces standardized configuration formatting, migrating all legacy custom configuration file structures (`colors.fz` and `keys.fz`) to robust, standard **TOML** configurations.

---

## ⚠️ Breaking Changes & Migrations

- **Theme Configuration**: The custom format file `~/.config/fyzenor/colors.fz` has been replaced by `~/.config/fyzenor/theme.toml` utilizing a standard TOML parser structure under `[colors]`.
- **Keyboard Macros**: The custom format file `~/.config/fyzenor/keys.fz` has been replaced by `~/.config/fyzenor/keys.toml` utilizing a standard TOML parser structure under `[macros]`.

On first launch of v4.0.0 or through the installer, three standardized TOML configuration templates will be deployed to your `~/.config/fyzenor/` directory:

1. **`config.toml`**: Exposes general properties, column width layout ratios, custom file type icons, and file extension categories.
2. **`theme.toml`**: Controls the TUI's 19 customizable color attributes (Tokyo Night, Catppuccin, Gruvbox, etc.).
3. **`keys.toml`**: Maps single keyboard characters to interactive shell command macros.

---

## ⚙️ Standardized TOML Scheles

### 1. `config.toml` (General, Layout & Icons Configuration)
```toml
# Fyzenor Default Configuration File
# Deployed to ~/.config/fyzenor/config.toml

[general]
# Show hidden files by default
show_hidden = false

# Default sorting mode: "name", "size" (descending), or "date" (descending)
sort_mode = "name"

[layout]
# Width percentages for the parent and current columns in normal mode (must sum to < 1.0)
parent_width = 0.18
current_width = 0.32

# Set to true to hide the preview pane or the parent directory listings by default
hide_preview = false
hide_parent = false

[icons]
# Glyph icons used for different file categories and states (Nerd Fonts required)
dir = " "
video = " "
image = " "
core = " "
frontend = "󰖟 "
config = " "
script = " "
docs = " "
font = " "
file = " "
music = " "
pin = " "
zip = "󰿺 "
link = "󰌹 "

[categories]
# Associate file extensions with styling and behavior groups
video = [".mp4", ".mkv", ".avi", ".mov", ".flv", ".webm", ".m4v", ".mpg", ".mpeg"]
image = [".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".tiff", ".ico", ".psd", ".ai"]
frontend = [".js", ".jsx", ".ts", ".tsx", ".css", ".scss", ".sass", ".less", ".styl", ".vue", ".html", ".svelte", ".htm", ".astro", ".mjx", ".dart", ".swift"]
scripts = [".sh", ".bash", ".zsh", ".fish", ".ksh", ".command", ".pl", ".pm", ".t", ".awk", ".ps1", ".psm1", ".bat", ".cmd", ".vbs", ".wsf"]
config = [".json", ".json5", ".jsonc", ".xml", ".xsd", ".xsl", ".gpx", ".yaml", ".yml", ".toml", ".ini", ".conf", ".cfg", ".prefs", ".properties", ".lock", ".env", ".dockerfile", ".gitignore", ".gitconfig", ".gitattributes", ".gitmodules"]
documentation = [".md", ".markdown", ".txt", ".text", ".log", ".pdf", ".doc", ".docx", ".odt", ".rtf", ".ppt", ".pptx", ".odp", ".xls", ".xlsx", ".ods", ".csv"]
core = [".py", ".pyw", ".ipynb", ".pyc", ".pyd", ".rb", ".ru", ".gemspec", ".php", ".cpp", ".cxx", ".cc", ".hpp", ".hxx", ".ixx", ".c", ".h", ".rs", ".java", ".class", ".jar", ".war", ".go", ".lua", ".sql", ".db", ".sqlite", ".sqlite3", ".db3", ".mdb", ".accdb", ".cmake", ".make", ".diff", ".patch", ".kt", ".kts", ".cs", ".csx", ".scala", ".sc", ".hs", ".lhs", ".clj", ".cljs", ".cljc", ".edn", ".r", ".rmd", ".jl", ".fs", ".fsi", ".fsx"]
font = [".woff", ".woff2", ".ttf", ".eot", ".otf"]
audio = [".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".wma", ".opus", ".mid", ".midi"]
archive = [".zip", ".tar", ".gz", ".tgz", ".7z", ".rar", ".xz", ".bz2", ".tbz2", ".lzma", ".cab"]
```

### 2. `theme.toml` (Theme Variables & Wallpaper generation)
```toml
# Fyzenor Theme Configuration File
# Matches Catppuccin Mocha preset

[colors]
dir = "#89b4fa"
file = "#cdd6f4"
sel_bg = "#585b70"
media = "#f9e2af"
image = "#f5c2e7"
border = "#b4befe"
success = "#a6e3a1"
error = "#f38ba8"
multi = "#f5e0dc"
pin_bg = "#cba6f7"
pin_border = "#89b4fa"
sec_sel_bg = "#313244"
core = "#a6e3a1"
archive = "#eba0ac"
frontend = "#fab387"
config = "#94e2d5"
script = "#f9e2af"
docs = "#f2cdcd"
font = "#cba6f7"
```

### 3. `keys.toml` (Custom Keyboard macros)
```toml
# Custom macro mappings
# Use single quotes for command strings inside TOML

[macros]
# $f - Expands to highlighted file path
# $s - Expands to space-separated selected paths
v = 'nvim "$f"'
g = 'git diff'
l = 'ls -la'
```

---

## 🚀 Installation & Update Instructions

1. Retrieve the latest updates from the source repository:
   ```bash
   git pull origin main
   ```
2. Clean rebuild and install the binary globally using the universal installer script:
   ```bash
   ./install.sh
   ```
