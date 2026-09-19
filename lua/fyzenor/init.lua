local config = require("fyzenor.config")

local M = {}

---@type { active_win: integer?, active_buf: integer?, active_job: integer?, prev_win: integer? }
local state = {
  active_win = nil,
  active_buf = nil,
  active_job = nil,
  prev_win = nil,
}

--- Resolves the Fyzenor executable path
---@param bin_path string
---@return string?
local function resolve_bin(bin_path)
  if bin_path:find("/") and vim.fn.executable(bin_path) == 1 then
    return vim.fn.fnamemodify(bin_path, ":p")
  end

  if vim.fn.executable(bin_path) == 1 then
    return bin_path
  end

  -- Check inside the plugin's build directory if installed directly from source
  local info = debug.getinfo(1, "S")
  if info and info.source and info.source:sub(1, 1) == "@" then
    local plugin_root = vim.fn.fnamemodify(info.source:sub(2), ":p:h:h:h")
    local built_bin = plugin_root .. "/build/fyzenor"
    if vim.fn.executable(built_bin) == 1 then
      return built_bin
    end
  end

  return nil
end

--- Read all non-empty lines from a file
---@param filepath string
---@return string[]
local function read_lines(filepath)
  local lines = {}
  local f = io.open(filepath, "r")
  if not f then
    return lines
  end
  for line in f:lines() do
    local trimmed = line:gsub("\r$", "")
    if trimmed ~= "" then
      table.insert(lines, trimmed)
    end
  end
  f:close()
  return lines
end

--- Opens a file in Neovim using the specified command
---@param file_path string
---@param cmd string
local function open_file_in_nvim(file_path, cmd)
  local escaped = vim.fn.fnameescape(file_path)
  local ok, err = pcall(vim.cmd, cmd .. " " .. escaped)
  if not ok then
    vim.notify("Fyzenor: failed to open file '" .. file_path .. "': " .. tostring(err), vim.log.levels.ERROR)
  end
end

--- Setup user options and optional directory hijacking
---@param user_opts FyzenorConfig?
function M.setup(user_opts)
  local opts = config.setup(user_opts)

  if opts.open_for_directories then
    -- Disable default netrw to prevent conflicts
    vim.g.loaded_netrw = 1
    vim.g.loaded_netrwPlugin = 1

    local group = vim.api.nvim_create_augroup("FyzenorDirectoryHijack", { clear = true })
    vim.api.nvim_create_autocmd({ "BufEnter", "BufReadCmd" }, {
      group = group,
      desc = "Open Fyzenor when opening a directory",
      callback = function(args)
        local path = args.file
        if path == "" then
          path = vim.api.nvim_buf_get_name(args.buf)
        end
        if path and path ~= "" and vim.fn.isdirectory(path) == 1 then
          vim.schedule(function()
            if vim.api.nvim_buf_is_valid(args.buf) then
              pcall(vim.api.nvim_buf_delete, args.buf, { force = true })
            end
            M.open(path)
          end)
        end
      end,
    })
  end

  return opts
end

--- Check if the Fyzenor floating window is currently active
---@return boolean
function M.is_open()
  return state.active_win ~= nil and vim.api.nvim_win_is_valid(state.active_win)
end

--- Close the Fyzenor floating window if open
function M.close()
  if M.is_open() then
    if state.active_job then
      pcall(vim.fn.jobstop, state.active_job)
      state.active_job = nil
    end
    if state.active_win and vim.api.nvim_win_is_valid(state.active_win) then
      pcall(vim.api.nvim_win_close, state.active_win, true)
    end
    if state.active_buf and vim.api.nvim_buf_is_valid(state.active_buf) then
      pcall(vim.api.nvim_buf_delete, state.active_buf, { force = true })
    end
    state.active_win = nil
    state.active_buf = nil
  end
end

--- Toggle the Fyzenor floating window
---@param target_path string?
---@param opts table?
function M.toggle(target_path, opts)
  if M.is_open() then
    M.close()
  else
    M.open(target_path, opts)
  end
end

--- Open Fyzenor floating file manager
---@param target_path string? Optional directory or file to highlight
---@param opts table? Runtime overrides for configuration
function M.open(target_path, opts)
  if M.is_open() then
    vim.api.nvim_set_current_win(state.active_win)
    vim.cmd("startinsert")
    return
  end

  local cfg = vim.tbl_deep_extend("force", config.options, opts or {})
  local bin = resolve_bin(cfg.fyzenor_path)

  if not bin then
    vim.notify(
      "Fyzenor binary not found. Please install fyzenor or configure `fyzenor_path` in setup().",
      vim.log.levels.ERROR
    )
    return
  end

  -- Resolve target path
  local resolved_target = nil
  if target_path == "cwd" then
    resolved_target = vim.fn.getcwd()
  elseif target_path and target_path ~= "" then
    resolved_target = vim.fn.expand(target_path)
  else
    local current_buf_name = vim.api.nvim_buf_get_name(0)
    if current_buf_name ~= "" and vim.fn.filereadable(current_buf_name) == 1 then
      resolved_target = current_buf_name
    else
      resolved_target = vim.fn.getcwd()
    end
  end

  if resolved_target and resolved_target ~= "" then
    resolved_target = vim.fn.fnamemodify(resolved_target, ":p")
    -- Strip trailing slash for consistent argument passing
    if #resolved_target > 1 and resolved_target:sub(-1) == "/" then
      resolved_target = resolved_target:sub(1, -2)
    end
  end

  -- Window dimensions
  local scale = math.min(1.0, math.max(0.2, cfg.floating_window_scaling_factor or 0.9))
  local total_cols = vim.o.columns
  local total_lines = vim.o.lines
  local width = math.max(20, math.floor(total_cols * scale))
  local height = math.max(10, math.floor(total_lines * scale))
  local col = math.max(0, math.floor((total_cols - width) / 2))
  local row = math.max(0, math.floor((total_lines - height) / 2) - 1)

  state.prev_win = vim.api.nvim_get_current_win()

  -- Scratch buffer for terminal
  local buf = vim.api.nvim_create_buf(false, true)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].filetype = "fyzenor"

  -- Temporary files for chooser selection and cwd tracking
  local chooser_file = vim.fn.tempname()
  local cwd_file = vim.fn.tempname()

  -- Window options
  local win_config = {
    relative = "editor",
    width = width,
    height = height,
    row = row,
    col = col,
    style = "minimal",
    border = cfg.border or "rounded",
    title = " Fyzenor ",
    title_pos = "center",
  }

  local win = vim.api.nvim_open_win(buf, true, win_config)
  if cfg.fyzenor_floating_window_winblend and cfg.fyzenor_floating_window_winblend > 0 then
    vim.wo[win].winblend = cfg.fyzenor_floating_window_winblend
  end

  state.active_win = win
  state.active_buf = buf

  -- Target open mode for selection (defaults to edit)
  local open_cmd = cfg.open_file_default_command or "edit"

  -- Helper to send keystroke to Fyzenor terminal
  local function send_key(key)
    if state.active_job then
      vim.api.nvim_chan_send(state.active_job, key)
    end
  end

  -- Keybindings inside Fyzenor floating terminal
  local km = cfg.keymaps or {}

  if km.open_file_in_vertical_split then
    vim.keymap.set("t", km.open_file_in_vertical_split, function()
      open_cmd = "vsplit"
      send_key("\r")
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Open in vertical split" })
  end

  if km.open_file_in_horizontal_split then
    vim.keymap.set("t", km.open_file_in_horizontal_split, function()
      open_cmd = "split"
      send_key("\r")
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Open in horizontal split" })
  end

  if km.open_file_in_tab then
    vim.keymap.set("t", km.open_file_in_tab, function()
      open_cmd = "tabedit"
      send_key("\r")
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Open in new tab" })
  end

  if km.send_to_quickfix_list then
    vim.keymap.set("t", km.send_to_quickfix_list, function()
      open_cmd = "quickfix"
      send_key("\r")
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Send to quickfix list" })
  end

  if km.copy_relative_path_to_selected_files then
    vim.keymap.set("t", km.copy_relative_path_to_selected_files, function()
      open_cmd = "copy_path"
      send_key("\r")
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Copy relative path" })
  end

  if km.grep_in_directory then
    vim.keymap.set("t", km.grep_in_directory, function()
      open_cmd = "grep"
      send_key("q")
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Grep in directory" })
  end

  if km.show_help then
    vim.keymap.set("t", km.show_help, function()
      local help_text = table.concat({
        "Fyzenor Neovim Shortcuts:",
        "  <Enter>      Open in current buffer (" .. open_cmd .. ")",
        "  " .. (km.open_file_in_vertical_split or "disabled") .. "        Open in vertical split",
        "  " .. (km.open_file_in_horizontal_split or "disabled") .. "        Open in horizontal split",
        "  " .. (km.open_file_in_tab or "disabled") .. "        Open in new tab",
        "  " .. (km.send_to_quickfix_list or "disabled") .. "        Send to quickfix list",
        "  " .. (km.copy_relative_path_to_selected_files or "disabled") .. "        Copy relative path",
        "  " .. (km.grep_in_directory or "disabled") .. "        Grep in directory",
        "  C            Choose current directory (chooser mode)",
        "  q            Cancel and close",
      }, "\n")
      vim.notify(help_text, vim.log.levels.INFO, { title = "Fyzenor Help" })
    end, { buffer = buf, nowait = true, desc = "Fyzenor: Show keybindings help" })
  end

  -- Trigger fyzenor_opened hook
  if cfg.hooks and cfg.hooks.fyzenor_opened then
    pcall(cfg.hooks.fyzenor_opened, resolved_target)
  end

  -- Build command line arguments
  local cmd = {
    bin,
    "--chooser-file=" .. chooser_file,
    "--cwd-file=" .. cwd_file,
  }
  if resolved_target and resolved_target ~= "" then
    table.insert(cmd, resolved_target)
  end

  -- Spawn terminal job
  local job_id = vim.fn.termopen(cmd, {
    on_exit = function(_, exit_code, _)
      -- Close floating window
      if vim.api.nvim_win_is_valid(win) then
        pcall(vim.api.nvim_win_close, win, true)
      end
      if vim.api.nvim_buf_is_valid(buf) then
        pcall(vim.api.nvim_buf_delete, buf, { force = true })
      end

      state.active_win = nil
      state.active_buf = nil
      state.active_job = nil

      -- Restore focus to previous window
      if state.prev_win and vim.api.nvim_win_is_valid(state.prev_win) then
        vim.api.nvim_set_current_win(state.prev_win)
      end

      -- Read chosen files and final cwd
      local chosen_paths = read_lines(chooser_file)
      local final_cwds = read_lines(cwd_file)
      local last_cwd = #final_cwds > 0 and final_cwds[1] or nil

      -- Clean up temp files
      pcall(os.remove, chooser_file)
      pcall(os.remove, cwd_file)

      -- Change Neovim working directory if requested
      if cfg.change_neovim_cwd_on_close and last_cwd and vim.fn.isdirectory(last_cwd) == 1 then
        pcall(vim.api.nvim_set_current_dir, last_cwd)
      end

      -- Handle grep action
      if open_cmd == "grep" then
        local target_dir = last_cwd or (resolved_target and vim.fn.isdirectory(resolved_target) == 1 and resolved_target) or vim.fn.getcwd()
        if cfg.integrations and type(cfg.integrations.grep) == "function" then
          cfg.integrations.grep(target_dir)
        else
          local has_telescope, telescope = pcall(require, "telescope.builtin")
          if has_telescope then
            telescope.live_grep({ cwd = target_dir })
          else
            local has_fzf, fzf = pcall(require, "fzf-lua")
            if has_fzf then
              fzf.live_grep({ cwd = target_dir })
            else
              vim.cmd("silent grep! -r '' " .. vim.fn.fnameescape(target_dir))
              vim.cmd("copen")
            end
          end
        end
        return
      end

      -- If no files chosen, exit cleanly
      if #chosen_paths == 0 then
        return
      end

      -- Trigger fyzenor_closed_successfully hook
      if cfg.hooks and cfg.hooks.fyzenor_closed_successfully then
        pcall(cfg.hooks.fyzenor_closed_successfully, chosen_paths)
      end

      -- Handle copy_path action
      if open_cmd == "copy_path" then
        local rel_paths = {}
        local nvim_cwd = vim.fn.getcwd()
        for _, p in ipairs(chosen_paths) do
          local rel = vim.fn.fnamemodify(p, ":~:.")
          table.insert(rel_paths, rel)
        end
        local result = table.concat(rel_paths, "\n")
        vim.fn.setreg("+", result)
        vim.fn.setreg('"', result)
        vim.notify("Copied to clipboard: " .. result, vim.log.levels.INFO, { title = "Fyzenor" })
        return
      end

      -- Handle quickfix action
      if open_cmd == "quickfix" then
        local qf_items = {}
        for _, p in ipairs(chosen_paths) do
          table.insert(qf_items, {
            filename = p,
            lnum = 1,
            col = 1,
            text = vim.fn.fnamemodify(p, ":t"),
          })
        end
        vim.fn.setqflist(qf_items, "r")
        vim.cmd("copen")
        return
      end

      -- Open first chosen file in target window
      local first_file = chosen_paths[1]
      open_file_in_nvim(first_file, open_cmd)

      if cfg.hooks and cfg.hooks.on_file_opened then
        pcall(cfg.hooks.on_file_opened, first_file)
      end

      -- Load remaining chosen files into buffer list
      for i = 2, #chosen_paths do
        local other_file = chosen_paths[i]
        pcall(vim.fn.bufadd, other_file)
        if cfg.hooks and cfg.hooks.on_file_opened then
          pcall(cfg.hooks.on_file_opened, other_file)
        end
      end
    end,
  })

  state.active_job = job_id

  -- Immediately enter insert mode so Fyzenor responds to keyboard input
  vim.cmd("startinsert")
end

--- Open Fyzenor at the current buffer's file
---@param opts table?
function M.open_at_current_file(opts)
  local current_buf = vim.api.nvim_buf_get_name(0)
  M.open(current_buf, opts)
end

--- Open Fyzenor at Neovim's current working directory
---@param opts table?
function M.open_at_cwd(opts)
  M.open("cwd", opts)
end

return M
