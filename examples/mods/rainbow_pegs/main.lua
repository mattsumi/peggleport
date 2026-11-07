function on_frame(info)
  local t = info.frame * 0.004
  for i, p in ipairs(board.pegs()) do
    local hue = (t + p.x * 0.0023 + p.y * 0.0017) % 1
    draw.circle(p.x, p.y, 12, draw.hsv(hue, 1, 1, p.lit and 110 or 55), true)
    draw.circle(p.x, p.y, 10.5, draw.hsv(hue, 1, 1, p.lit and 255 or 190), false)
    draw.circle(p.x, p.y, 9.5, draw.hsv(hue, 1, 1, p.lit and 255 or 190), false)
    draw.circle(p.x, p.y, 7.5, draw.hsv(hue + 0.5, 0.9, 1, p.lit and 200 or 90), false)
  end
end
