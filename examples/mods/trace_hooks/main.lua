local function t(name, ...)
  local parts = {}
  for i, v in ipairs({...}) do parts[#parts + 1] = tostring(v) end
  log(string.format("[%5d] %-14s %s", board.tick(), name, table.concat(parts, ", ")))
end
function on_load() log("tracing hooks") end
function on_level_start(l) t("level_start", "width=" .. l.width, "orange=" .. l.orange, "pegs=" .. l.pegs, "nights=" .. tostring(l.nights)) end
function on_shot(a) t("shot", string.format("%.4f", a)) end
function on_peg_hit(p, b) t("peg_hit", p, b) end
function on_ball_lost() t("ball_lost") end
function on_shot_end(s, st) t("shot_end", s, st) end
function on_style_shot(id, pts) t("style_shot", id, pts) end
function on_power(p, name) t("power", p, name) end
function on_fever() t("fever") end
function on_level_end(r) t("level_end", "won=" .. tostring(r.won), "score=" .. r.score, "shots=" .. r.shots, "orange_left=" .. r.orange_left) end
