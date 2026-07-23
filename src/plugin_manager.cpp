#include "plugin_manager.h"
#include "file_manager.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

static PluginManager* getPM(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "__fyzenor_pm");
  PluginManager* pm = static_cast<PluginManager*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return pm;
}

PluginManager::PluginManager() : L(nullptr), fileManager(nullptr) {}

PluginManager::~PluginManager() {
  if (L) {
    lua_close(L);
    L = nullptr;
  }
}

void PluginManager::init(FileManager* fm) {
  fileManager = fm;
  L = luaL_newstate();
  if (!L) {
    std::cerr << "[Fyzenor Lua] Failed to initialize Lua state." << std::endl;
    return;
  }

  luaL_openlibs(L);

  // Store pointer in registry
  lua_pushlightuserdata(L, static_cast<void*>(this));
  lua_setfield(L, LUA_REGISTRYINDEX, "__fyzenor_pm");

  registerLuaBindings();
  loadPlugins();
}

void PluginManager::registerLuaBindings() {
  lua_newtable(L); // fyzenor table

  static const struct luaL_Reg fyzenorLib[] = {
      {"get_current_path", lua_GetCurrentPath},
      {"get_current_file", lua_GetSingleCurrentFile},
      {"get_selected_files", lua_GetSelectedFiles},
      {"set_status", lua_SetStatus},
      {"exec", lua_Exec},
      {"shell_output", lua_ShellOutput},
      {"reload", lua_Reload},
      {"read_file", lua_ReadFile},
      {"add_keymap", lua_AddKeymap},
      {"register_previewer", lua_RegisterPreviewer},
      {"prompt", lua_Prompt},
      {"change_directory", lua_ChangeDirectory},
      {"get_version", lua_GetVersion},
      {NULL, NULL}
  };

#if LUA_VERSION_NUM >= 502
  luaL_setfuncs(L, fyzenorLib, 0);
#else
  luaL_register(L, NULL, fyzenorLib);
#endif

  lua_setglobal(L, "fyzenor");
}

void PluginManager::loadPlugins() {
  const char* home = getenv("HOME");
  if (!home) return;

  fs::path pluginsDir = fs::path(home) / ".config" / "fyzenor" / "plugins";
  try {
    if (!fs::exists(pluginsDir)) {
      fs::create_directories(pluginsDir);
      
      // Create a default example plugin
      fs::path examplePluginDir = pluginsDir / "example";
      fs::create_directories(examplePluginDir);
      fs::path exampleFile = examplePluginDir / "init.lua";
      std::ofstream out(exampleFile);
      out << "-- Fyzenor Lua Plugin Example\n"
          << "fyzenor.set_status('Lua Plugin Engine Loaded Successfully!')\n"
          << "\n"
          << "-- Custom keybinding example (Press Ctrl+X to run)\n"
          << "fyzenor.add_keymap('Ctrl+X', function()\n"
          << "    local f = fyzenor.get_current_file()\n"
          << "    if f then\n"
          << "        fyzenor.set_status('Plugin: Current item is ' .. f.name)\n"
          << "    end\n"
          << "end)\n";
      out.close();
    }

    // Load init.lua in pluginsDir directly if it exists
    fs::path mainInit = pluginsDir / "init.lua";
    if (fs::exists(mainInit)) {
      if (luaL_dofile(L, mainInit.string().c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::cerr << "[Fyzenor Lua Error] " << (err ? err : "Unknown error") << std::endl;
        lua_pop(L, 1);
      }
    }

    // Load sub-directory plugins (~/.config/fyzenor/plugins/<plugin_name>/init.lua)
    for (const auto& entry : fs::directory_iterator(pluginsDir)) {
      if (entry.is_directory()) {
        fs::path pInit = entry.path() / "init.lua";
        if (fs::exists(pInit)) {
          if (luaL_dofile(L, pInit.string().c_str()) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            std::cerr << "[Fyzenor Lua Error] " << (err ? err : "Unknown error") << std::endl;
            lua_pop(L, 1);
          }
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[Fyzenor Lua Exception] " << e.what() << std::endl;
  }
}

// C-Callback Implementations
int PluginManager::lua_GetCurrentPath(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  std::string pathStr = pm->fileManager->currentPath.string();
  lua_pushstring(L, pathStr.c_str());
  return 1;
}

int PluginManager::lua_GetSingleCurrentFile(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  if (pm->fileManager->currentFiles.empty() || 
      pm->fileManager->selectedIndex >= pm->fileManager->currentFiles.size()) {
    lua_pushnil(L);
    return 1;
  }

  const FileEntry& fe = pm->fileManager->currentFiles[pm->fileManager->selectedIndex];

  lua_newtable(L);
  lua_pushstring(L, fe.path.string().c_str());
  lua_setfield(L, -2, "path");

  lua_pushstring(L, fe.name.c_str());
  lua_setfield(L, -2, "name");

  lua_pushstring(L, fe.extension.c_str());
  lua_setfield(L, -2, "extension");

  lua_pushnumber(L, static_cast<double>(fe.size));
  lua_setfield(L, -2, "size");

  lua_pushboolean(L, fe.is_directory);
  lua_setfield(L, -2, "is_dir");

  lua_pushboolean(L, fe.is_symlink);
  lua_setfield(L, -2, "is_symlink");

  return 1;
}

int PluginManager::lua_GetSelectedFiles(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  std::vector<FileEntry> targets;
  if (!pm->fileManager->multiSelection.empty()) {
    for (const auto& f : pm->fileManager->currentFiles) {
      if (pm->fileManager->multiSelection.count(f.path)) {
        targets.push_back(f);
      }
    }
  } else if (!pm->fileManager->currentFiles.empty() && 
             pm->fileManager->selectedIndex < pm->fileManager->currentFiles.size()) {
    targets.push_back(pm->fileManager->currentFiles[pm->fileManager->selectedIndex]);
  }

  lua_newtable(L);

  int idx = 1;
  for (const auto& fe : targets) {
    lua_pushinteger(L, idx++);
    lua_newtable(L);

    lua_pushstring(L, fe.path.string().c_str());
    lua_setfield(L, -2, "path");

    lua_pushstring(L, fe.name.c_str());
    lua_setfield(L, -2, "name");

    lua_pushstring(L, fe.extension.c_str());
    lua_setfield(L, -2, "extension");

    lua_pushnumber(L, static_cast<double>(fe.size));
    lua_setfield(L, -2, "size");

    lua_pushboolean(L, fe.is_directory);
    lua_setfield(L, -2, "is_dir");

    lua_settable(L, -3);
  }

  return 1;
}

int PluginManager::lua_SetStatus(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  if (lua_isstring(L, 1)) {
    std::string msg = lua_tostring(L, 1);
    pm->fileManager->setStatus(msg);
  }
  return 0;
}

int PluginManager::lua_Exec(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  if (lua_isstring(L, 1)) {
    std::string cmd = lua_tostring(L, 1);
    std::thread([cmd]() {
      int res = system(cmd.c_str());
      (void)res;
    }).detach();
  }
  return 0;
}

int PluginManager::lua_ShellOutput(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    lua_pushnil(L);
    return 1;
  }

  std::string cmd = lua_tostring(L, 1);
  if (cmd.empty()) {
    lua_pushstring(L, "");
    return 1;
  }

  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    lua_pushnil(L);
    return 1;
  }

  char buf[4096];
  std::string output = "";
  size_t totalBytes = 0;
  const size_t MAX_BYTES = 1024 * 1024; // 1MB output safety cap

  while (fgets(buf, sizeof(buf), pipe) != nullptr) {
    size_t len = strlen(buf);
    if (totalBytes + len > MAX_BYTES) {
      output.append(buf, MAX_BYTES - totalBytes);
      break;
    }
    output += buf;
    totalBytes += len;
  }
  pclose(pipe);

  lua_pushstring(L, output.c_str());
  return 1;
}

int PluginManager::lua_Reload(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  pm->fileManager->reloadAll();
  return 0;
}

int PluginManager::lua_ReadFile(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    lua_pushnil(L);
    return 1;
  }

  std::string path = lua_tostring(L, 1);
  int maxLines = 100;
  if (lua_isnumber(L, 2)) {
    maxLines = static_cast<int>(lua_tonumber(L, 2));
    if (maxLines <= 0) maxLines = 100;
    if (maxLines > 2000) maxLines = 2000;
  }

  std::ifstream in(path);
  if (!in.is_open()) {
    lua_pushnil(L);
    return 1;
  }

  std::string result = "";
  std::string line;
  int count = 0;
  while (std::getline(in, line) && count < maxLines) {
    result += line + "\n";
    count++;
  }

  lua_pushstring(L, result.c_str());
  return 1;
}

std::string PluginManager::normalizeKey(const std::string& key) {
  if (key.size() >= 5 && (key.rfind("ctrl+", 0) == 0 || key.rfind("CTRL+", 0) == 0 || key.rfind("Ctrl+", 0) == 0)) {
    std::string charPart = key.substr(5);
    if (!charPart.empty()) {
      charPart[0] = std::toupper(charPart[0]);
    }
    return "Ctrl+" + charPart;
  }
  if (key.size() >= 4 && (key.rfind("alt+", 0) == 0 || key.rfind("ALT+", 0) == 0 || key.rfind("Alt+", 0) == 0)) {
    std::string charPart = key.substr(4);
    if (!charPart.empty()) {
      charPart[0] = std::toupper(charPart[0]);
    }
    return "Alt+" + charPart;
  }
  return key;
}

int PluginManager::lua_AddKeymap(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm) return 0;

  if (lua_isstring(L, 1) && (lua_isfunction(L, 2) || lua_isstring(L, 2))) {
    std::string key = pm->normalizeKey(lua_tostring(L, 1));
    
    // Store function in Lua registry table with key name
    lua_pushvalue(L, 2);
    std::string registryKey = "__fyzenor_key_" + key;
    lua_setfield(L, LUA_REGISTRYINDEX, registryKey.c_str());

    pm->keymaps[key] = registryKey;
  }
  return 0;
}

int PluginManager::lua_RegisterPreviewer(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm) return 0;

  if (lua_isstring(L, 1) && (lua_isfunction(L, 2) || lua_isstring(L, 2))) {
    std::string ext = lua_tostring(L, 1);
    if (!ext.empty() && ext.front() != '.') {
      ext = "." + ext;
    }

    lua_pushvalue(L, 2);
    std::string registryKey = "__fyzenor_preview_" + ext;
    lua_setfield(L, LUA_REGISTRYINDEX, registryKey.c_str());

    pm->previewers[ext] = registryKey;
  }
  return 0;
}

bool PluginManager::handleKey(const std::string& rawKey) {
  std::lock_guard<std::mutex> lock(luaMutex);
  if (!L) return false;

  std::string key = normalizeKey(rawKey);
  auto it = keymaps.find(key);
  if (it == keymaps.end()) return false;

  lua_getfield(L, LUA_REGISTRYINDEX, it->second.c_str());
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      const char* err = lua_tostring(L, -1);
      if (fileManager) {
        fileManager->setStatus("Lua Key Error: " + std::string(err ? err : "Unknown"));
      }
      lua_pop(L, 1);
    }
    return true;
  }
  lua_pop(L, 1);
  return false;
}

bool PluginManager::hasCustomPreviewer(const std::string& ext) const {
  std::lock_guard<std::mutex> lock(luaMutex);
  return previewers.find(ext) != previewers.end();
}

std::string PluginManager::runCustomPreviewer(const std::string& ext, const std::string& filePath, int width, int height) {
  std::lock_guard<std::mutex> lock(luaMutex);
  if (!L) return "";

  auto it = previewers.find(ext);
  if (it == previewers.end()) return "";

  lua_getfield(L, LUA_REGISTRYINDEX, it->second.c_str());
  if (lua_isfunction(L, -1)) {
    lua_pushstring(L, filePath.c_str());
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);

    if (lua_pcall(L, 3, 1, 0) == LUA_OK) {
      std::string res = "";
      if (lua_isstring(L, -1)) {
        res = lua_tostring(L, -1);
      }
      lua_pop(L, 1);
      return res;
    } else {
      lua_pop(L, 1);
    }
  } else {
    lua_pop(L, 1);
  }
  return "";
}

int PluginManager::lua_Prompt(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) {
    lua_pushnil(L);
    return 1;
  }

  std::string promptStr = lua_isstring(L, 1) ? lua_tostring(L, 1) : "Query:";
  std::string defaultVal = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";

  std::string result = pm->fileManager->promptInput(promptStr, defaultVal);
  lua_pushstring(L, result.c_str());
  return 1;
}

int PluginManager::lua_ChangeDirectory(lua_State* L) {
  PluginManager* pm = getPM(L);
  if (!pm || !pm->fileManager) return 0;

  if (lua_isstring(L, 1)) {
    std::string pathStr = lua_tostring(L, 1);
    if (!pathStr.empty()) {
      pm->fileManager->changeDirectory(fs::path(pathStr));
    }
  }
  return 0;
}

int PluginManager::lua_GetVersion(lua_State* L) {
  lua_pushstring(L, FYZENOR_VERSION.c_str());
  return 1;
}
