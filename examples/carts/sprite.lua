-- sprite.lua - Lazy-100 M4 demo: sprite sheet, transparency, flips, sspr, pal recolor.
-- Sprite 0 is painted into the sheet at _init via sset (no PNG loader yet); '.' = index 0
-- (transparent by default), digits = palette indices.

function _init()
  local art = {
    "..3.....",
    "..33....",
    "..333...",
    "..3333..",
    "..333...",
    "..33....",
    "..3.....",
    "........",
  }
  for y = 1, 8 do
    for x = 1, 8 do
      local ch = art[y]:sub(x, x)
      if ch ~= "." then sset(x - 1, y - 1, tonumber(ch)) end
    end
  end
end

function _draw()
  cls(1)
  print("SPRITES  spr / sspr / flip / pal / palt", 6, 6, 7)

  -- gray panel; sprite index-0 pixels are transparent, so it shows through
  rectfill(16, 28, 120, 78, 5)

  spr(0, 28, 44)                  -- normal (points right)
  spr(0, 52, 44, 1, 1, true)      -- flip X (points left)
  spr(0, 76, 44, 1, 1, false, true) -- flip Y
  spr(0, 100, 44, 1, 1, true, true) -- flip both

  -- sspr: scale sprite 0 up 4x
  print("sspr 4x", 152, 30, 6)
  sspr(0, 0, 8, 8, 152, 40, 36, 36)

  -- pal() draw-palette recolor: animate the arrow's color (index 3 -> c)
  print("pal() recolor", 6, 96, 6)
  local c = 8 + flr(t() * 4) % 8
  pal(3, c)
  for i = 0, 11 do spr(0, 16 + i * 16, 110) end
  pal() -- reset

  -- screen-palette swap: make the background (index 1) pulse
  print("pal(...,1) screen swap", 6, 150, 6)
  local bg = (flr(t() * 2) % 2 == 0) and 1 or 2
  pal(1, bg, 1)
  rectfill(16, 164, 304, 200, 1)
  print("background index 1 remapped on screen", 24, 178, 7)
end
