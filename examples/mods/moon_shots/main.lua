function on_load()
  physics.set{gravity_y = 0.02, ball_bounce = 1.1, drag = 0.001}
end

function on_level_start(level)
  board.say("Low gravity")
end
