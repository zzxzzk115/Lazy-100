-- map.lua - Lazy-100 M9 demo: tile map via mset/mget/map().
-- Paints two 16x16 sprites (grass=1, wall=2) at _init, lays out a little room in the
-- tile map, then blits it with map(). In the editor you'd draw this in the MAP tab and
-- it would round-trip through the cart's __map__ section instead.

function _init()
  -- sprite 1: grass (a speckled green block)
  for y = 0, 15 do
    for x = 0, 15 do
      sset(16 + x, y, 3)                       -- sheet col 1 (sprite 1) is at x=16
    end
  end
  sset(16 + 4, 4, 11) sset(16 + 11, 9, 11)     -- a couple lighter specks
  -- sprite 2: wall (a solid brown block with a lit top edge)
  for y = 0, 15 do
    for x = 0, 15 do
      sset(32 + x, y, 4)                        -- sprite 2 is at x=32
    end
  end
  for x = 0, 15 do sset(32 + x, 0, 15) end      -- top highlight

  -- lay out a 12x7 room: walls around the border, grass inside
  for ty = 0, 6 do
    for tx = 0, 11 do
      local edge = (tx == 0 or tx == 11 or ty == 0 or ty == 6)
      mset(tx, ty, edge and 2 or 1)
    end
  end
end

function _draw()
  cls(1)
  print("MAP  mset / mget / map()", 6, 4, 7)
  -- draw the 12x7 cell region at screen (16,24)
  map(0, 0, 16, 24, 12, 7)
  print("tile at (1,1) = "..mget(1, 1), 6, 150, 6)
end
