local M = {}

---@class FyzenorKeymaps
---@field show_help string|boolean
---@field open_file_in_vertical_split string|boolean
---@field open_file_in_horizontal_split string|boolean
---@field open_file_in_tab string|boolean
---@field send_to_quickfix_list string|boolean
---@field copy_relative_path_to_selected_files string|boolean
---@field grep_in_directory string|boolean

---@class FyzenorHooks
---@field fyzenor_opened fun(target_path: string)?
---@field fyzenor_closed_successfully fun(chosen_files: string[])?
---@field on_file_opened fun(file: string)?

---@class FyzenorConfig
---@field open_for_directories boolean
---@field change_neovim_cwd_on_close boolean
---@field floating_window_scaling_factor number
---@field fyzenor_floating_window_winblend number
---@field border string|table
---@field fyzenor_path string
---@field open_file_default_command string
---@field keymaps FyzenorKeymaps
---@field hooks FyzenorHooks
---@field integrations table<string, any>

--- Default configuration options
M.defaults = {
  open_for_directories = false,
  change_neovim_cwd_on_close = false,
  floating_window_scaling_factor = 0.9,
  fyzenor_floating_window_winblend = 0,
  border = "rounded",
  fyzenor_path = "fyzenor",
  open_file_default_command = "edit",
  keymaps = {
    show_help = "<f1>",
    open_file_in_vertical_split = "<c-v>",
    open_file_in_horizontal_split = "<c-x>",
    open_file_in_tab = "<c-t>",
    send_to_quickfix_list = "<c-q>",
    copy_relative_path_to_selected_files = "<c-y>",
    grep_in_directory = "<c-f>",
  },
  hooks = {
    fyzenor_opened = nil,
    fyzenor_closed_successfully = nil,
    on_file_opened = nil,
  },
  integrations = {
    grep = nil,
  },
}

M.options = vim.deepcopy(M.defaults)

--- Setup user options
---@param user_opts FyzenorConfig?
---@return FyzenorConfig
function M.setup(user_opts)
  M.options = vim.tbl_deep_extend("force", vim.deepcopy(M.defaults), user_opts or {})
  return M.options
end

return M
