if vim.g.loaded_fyzenor == 1 then
  return
end
vim.g.loaded_fyzenor = 1

local fyzenor = require("fyzenor")

-- Define :Fyzenor command
vim.api.nvim_create_user_command("Fyzenor", function(opts)
  local arg = opts.args
  if arg == "" then
    fyzenor.open()
  else
    fyzenor.open(arg)
  end
end, {
  nargs = "?",
  complete = "file",
  desc = "Open Fyzenor floating file manager",
})

-- Define :FyzenorToggle command
vim.api.nvim_create_user_command("FyzenorToggle", function(opts)
  local arg = opts.args
  if arg == "" then
    fyzenor.toggle()
  else
    fyzenor.toggle(arg)
  end
end, {
  nargs = "?",
  complete = "file",
  desc = "Toggle Fyzenor floating file manager",
})

-- Define :FyzenorCwd command
vim.api.nvim_create_user_command("FyzenorCwd", function()
  fyzenor.open_at_cwd()
end, {
  desc = "Open Fyzenor at Neovim's current working directory",
})
