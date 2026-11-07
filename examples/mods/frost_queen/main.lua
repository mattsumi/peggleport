register_master {
  id = "frost",
  name = "Frost Queen",
  description = "She rules the winter board and freezes whatever she touches.",
  power_name = "Frost Snap",
  power_description = "For 3 shots, every shot is worth 250 more points, and the green peg lights 3 more pegs.",
  portrait = "images/frost.png",
  turns = 3,
  trigger = "green_peg",
  banner = "Frost Snap!",
  color = 0x66ccff,

  on_activate = function(player, peg)
    local pegs = board.pegs()
    local lit = 0
    for _ = 1, 12 do
      local p = pegs[rng_range(1, #pegs)]
      if p and not p.lit and p.type ~= "green" then
        board.hit_peg(p.id)
        lit = lit + 1
        if lit == 3 then break end
      end
    end
  end,

  on_shot = function(player)
    board.add_score(250)
  end,

  on_end = function(player)
    log("Frost Snap is over for player " .. player)
  end,
}

register_master {
  id = "gleam",
  name = "Gleam",
  description = "A small spark that never goes out.",
  power_name = "Pocket Change",
  power_description = "Every shot starts with 100 bonus points.",
  trigger = "every_shot",
  color = 0xffd040,

  on_shot = function(player)
    board.add_score(100)
  end,
}
