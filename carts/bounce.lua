-- bounce.lua - Lazy-100 M3 input demo.
-- Arrows move the dot; Z cycles its color; X drops a trail marker.

function _init()
  x = 160
  y = 120
  c = 12
  marks = {}
end

function _update()
  if btn(0) then x = x - 2 end -- left
  if btn(1) then x = x + 2 end -- right
  if btn(2) then y = y - 2 end -- up
  if btn(3) then y = y + 2 end -- down
  x = mid(5, x, 314)
  y = mid(5, y, 234)

  if btnp(4) then        -- Z: next color + a rising beep
    c = (c + 1) % 32
    sfx(c % 10)
  end
  if btnp(5) then        -- X: drop a marker + a beep
    marks[#marks + 1] = { x = x, y = y, c = c }
    if #marks > 64 then table.remove(marks, 1) end
    sfx(7)
  end
end

function _draw()
  cls(1)
  print("ARROWS MOVE   Z COLOR   X MARK", 8, 8, 7)

  for i = 1, #marks do
    local m = marks[i]
    circ(m.x, m.y, 3, m.c)
  end

  circfill(x, y, 5, c)
  circ(x, y, 5, 7)

  print("x=" .. flr(x) .. " y=" .. flr(y) .. " c=" .. c, 8, 226, 6)
end
