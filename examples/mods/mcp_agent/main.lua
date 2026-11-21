local refresh = 0

function on_load()
  fs.remove("request.json")
  fs.remove("response.json")
  fs.remove("state.json")
end

function on_control()
  local request = fs.read("request.json")
  if request and fs.remove("request.json") then
    fs.write("response.json", control.command(request))
    refresh = 0
  end
  if refresh == 0 then
    fs.write("state.json", control.state())
  end
  refresh = (refresh + 1) % 10
end
