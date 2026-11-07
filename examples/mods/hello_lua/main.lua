
local COMBO_AT = 4

function on_load()
  log("loaded " .. mod.name .. " v" .. mod.version .. " (API " .. mod.api .. ")")
  ui.menu("Say hello", function() ui.message("Hello from the " .. mod.name .. " mod!") end)
end

function on_level_start(level)
  state.set("combo", 0)
  board.say("Hello Lua!  " .. level.pegs .. " pegs, " .. level.orange .. " orange")
end

function on_shot(angle)
  state.set("combo", 0)
end

function on_peg_hit(peg, ball)
  local combo = state.add("combo", 1)
  if combo == COMBO_AT then
    board.style_shot("Hello Combo", 1000)
  end
  if rng_range(1, 12) == 1 then
    board.add_score(100)
  end
end

register_power {
  name = "Sparkle", replaces = "Flower Power", mode = "extra",
  on_activate = function(player, peg)
    local pegs = board.pegs()
    local lit = 0
    for i = 1, 3 do
      local p = pegs[rng_range(1, #pegs)]
      if p and not p.lit and p.type ~= "green" then board.hit_peg(p.id); lit = lit + 1 end
    end
    board.say("Sparkle!")
    log("Sparkle for player " .. player .. " at peg " .. peg .. ": lighting " .. lit .. " more pegs")
  end,
}

function on_frame(info)
  draw.text(8, 552, "Hello Lua  combo " .. (state.get("combo", 0)) .. "  tick " .. info.tick, draw.rgb(255, 255, 160, 230), 0, 1.0)
end
