#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

class FileManager;

class PluginManager {
public:
  PluginManager();
  ~PluginManager();

  void init(FileManager* fm);
  void loadPlugins();
  
  bool handleKey(const std::string& key);
  bool hasCustomPreviewer(const std::string& ext) const;
  std::string runCustomPreviewer(const std::string& ext, const std::string& filePath, int width, int height);

  // Lua C-Callback Helpers
  static int lua_GetSingleCurrentFile(lua_State* L);
  static int lua_GetSelectedFiles(lua_State* L);
  static int lua_GetCurrentPath(lua_State* L);
  static int lua_SetStatus(lua_State* L);
  static int lua_Exec(lua_State* L);
  static int lua_Reload(lua_State* L);
  static int lua_ReadFile(lua_State* L);
  static int lua_AddKeymap(lua_State* L);
  static int lua_RegisterPreviewer(lua_State* L);

private:
  lua_State* L;
  FileManager* fileManager;
  std::mutex luaMutex;

  std::unordered_map<std::string, std::string> keymaps; // key_combo -> lua_func_name
  std::unordered_map<std::string, std::string> previewers; // file_extension -> lua_func_name

  void registerLuaBindings();
};

#endif // PLUGIN_MANAGER_H
