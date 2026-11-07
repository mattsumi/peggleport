register_peg_type{
  id = "ice",
  name = "Ice Peg",
  stages = { "images/ice_0.png", "images/ice_1.png" },
  lit_image = "images/ice_lit.png",
  color = 0x9fd8ff,
  hits_to_clear = 2,
  score = 50,
  on_clear = function(peg)
    board.add_score(100)
  end,
}

register_peg_type{
  id = "iron",
  name = "Iron Peg",
  image = "images/iron.png",
  color = 0xb0b4c0,
  clears_on_hit = false,
  score = 20,
  bounce = 1.3,
}

register_peg_type{
  id = "gold",
  name = "Gold Peg",
  color = 0xffd040,
  score = 200,
  on_hit = function(peg, ball)
    state.add("gold", 1)
  end,
}

function on_level_start(level)
  local plan = {}
  local n = 0
  for _, p in ipairs(board.pegs()) do
    if p.type == "blue" and p.custom == nil then
      n = n + 1
      if n % 17 == 0 then
        plan[p.id] = "gold"
      elseif n % 11 == 0 then
        plan[p.id] = "iron"
      elseif n % 5 == 0 then
        plan[p.id] = "ice"
      end
    end
  end
  board.set_peg_types(plan)
end
