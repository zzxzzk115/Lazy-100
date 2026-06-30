-- hello.lua - Lazy-100 M2 smoke test: shapes drawn from Lua, animated via t().

function _init()
  cx = 160
  cy = 120
end

function _draw()
  cls(1) -- dark blue

  -- palette strip along the top (indices 0..31)
  for i = 0, 31 do
    rectfill(i * 10, 0, i * 10 + 9, 7, i)
  end

  -- framed play area
  rect(8, 16, 311, 231, 7)

  -- crossing lines
  line(8, 16, 311, 231, 8)
  line(311, 16, 8, 231, 12)

  -- a bouncing filled circle
  local bx = cx + math.floor(80 * cos(t() * 1.7))
  local by = cy + math.floor(70 * sin(t() * 2.3))
  circfill(bx, by, 12, 10)
  circ(bx, by, 12, 7)

  -- a pulsing ring in the center
  local r = 20 + math.floor(10 * sin(t() * 3))
  circ(160, 120, r, 14)

  -- text via the built-in font
  print("HELLO LAZY-100", 12, 200, 7)
  print("the quick brown fox 0123456789", 12, 210, 10)
  print("t=" .. flr(t()), 12, 220, 14)
end
