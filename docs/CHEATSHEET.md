# LAZY-100 API Cheatsheet

Every function the console exposes to carts, grouped by feature. Optional parameters are in
`[brackets]`. This mirrors the in-editor cheatsheet (the book icon in the code editor) and the
registry in `source/lazy100/script/api_doc.cpp` — update them together.

中文版: [zh_CN/CHEATSHEET.md](zh_CN/CHEATSHEET.md)

## Callbacks

```lua
_init()                                    -- called once when the cart starts
_update()                                  -- game logic at 30 fps
_update60()                                -- define instead of _update for 60 fps
_draw()                                    -- render one frame (after update)
```

## Graphics

```lua
cls([color])                               -- clear the screen (default color 0)
pset(x, y, [color])                        -- set one pixel
pget(x, y)                                 -- read one pixel's palette index
line(x0, y0, x1, y1, [color])              -- line between two points
rect(x0, y0, x1, y1, [color])              -- rectangle outline
rectfill(x0, y0, x1, y1, [color])          -- filled rectangle
circ(x, y, radius, [color])                -- circle outline
circfill(x, y, radius, [color])            -- filled circle
oval(x0, y0, x1, y1, [color])              -- ellipse outline in a bounding box
ovalfill(x0, y0, x1, y1, [color])          -- filled ellipse in a bounding box
print(text, [x], [y], [color])             -- draw text; returns the end x
camera([x], [y])                           -- scroll offset for all drawing; no args resets
clip([x], [y], [w], [h])                   -- restrict drawing to a rect; no args resets
fillp([pattern], [color2])                 -- 4x4 dither for shapes; set bits draw color2
                                           -- (default: skipped/transparent); no args resets
```

`fillp` applies to `pset`, `line`, `rect(fill)`, `circ(fill)` and `oval(fill)` — not to
sprites or `print`. The pattern is 16 bits, bit 15 = top-left of the 4x4 tile
(e.g. `fillp(0b0101101001011010)` is a checkerboard).

### Text glyphs

Low bytes `\1`..`\15` inside a printed string draw as 8x8 button/mark pictographs, e.g.
`print("press \6 to jump")`:

```
\1 <-  \2 ->  \3 up  \4 down  \5 O button  \6 X button
\7 heart  \8 star  \11 note  \12 diamond  \14 flag  \15 face
```

## Sprites

```lua
spr(n, x, y, [w], [h], [flip_x], [flip_y]) -- draw sprite n (w*h block of 16px cells)
sspr(sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])
                                           -- draw a sheet rect, scaled to dw*dh
sget(x, y)                                 -- read a sheet pixel
sset(x, y, [color])                        -- write a sheet pixel
fget(n, [bit])                             -- sprite flags (or one flag bit)
fset(n, flags_or_bit, [on])                -- set sprite flags (or one flag bit)
```

## Map

```lua
mget(cel_x, cel_y)                         -- tile at a map cell (255 = empty)
mset(cel_x, cel_y, tile)                   -- write a map cell
map([cel_x], [cel_y], [scr_x], [scr_y], [cel_w], [cel_h])
                                           -- draw a map region as 16px tiles
```

## Palette

```lua
pal([c0], [c1], [mode])                    -- remap color c0 -> c1 (mode 1 = screen); no args resets
palt([color], [transparent])               -- set a color's transparency; no args resets
```

## Audio

```lua
sfx(n, [channel], [offset], [length])      -- play sfx n from note offset for length notes
                                           -- n = -1 stops the channel, -2 releases its loop
music([n])                                 -- play music from pattern n; music(-1) stops
```

Each sfx pattern also carries a **speed**, an optional **loop region** (the pattern repeats
between loop start/end until stopped or released), and a per-note **effect**: 1 slide,
2 vibrato, 3 drop, 4 fade in, 5 fade out, 6/7 fast/slow arpeggio. Music patterns take
loop-start / loop-end / stop flags in the music editor.

## Input

```lua
btn([b], [player])                         -- button held? no args = bitmask
btnp([b], [player])                        -- button pressed (with auto-repeat)?
stat(n)                                    -- console state: 32/33 mouse x/y, 34 buttons
```

## Math

Angles are in **turns**: `1.0` is a full revolution, the y axis points down (`sin` is negated).

```lua
flr(x)                                     -- round down to an integer
ceil(x)                                    -- round up to an integer
abs(x)                                     -- absolute value
min(a, b)                                  -- smaller of two values
max(a, b)                                  -- larger of two values
mid(a, b, c)                               -- middle of three values (clamp)
sgn(x)                                     -- -1 or 1 (0 counts as positive)
sqrt(x)                                    -- square root
sin(turns)                                 -- sine; angle in turns, inverted y
cos(turns)                                 -- cosine; angle in turns
atan2(dx, dy)                              -- angle of a vector, in turns 0..1
rnd([max_or_table])                        -- random [0,max), or a random element of a table
srand([seed])                              -- seed the random generator
```

## Time

```lua
t()                                        -- seconds since the cart started
time()                                     -- alias of t()
```

## Save data

```lua
cartdata(id)                               -- open the cart's 64-slot save file (saves/<id>)
dset(index, value)                         -- store a number in slot 0..63 (persisted)
dget(index)                                -- read a saved slot (0 if unset)
```

## Tables

```lua
add(t, value, [index])                     -- append (or insert) into a table; returns value
del(t, value)                              -- remove the first matching value
deli(t, [index])                           -- remove by index (default: last)
count(t, [value])                          -- #t, or how many equal value
all(t)                                     -- iterator: for item in all(t) do ... end
foreach(t, fn)                             -- call fn(item) for every item
```

## Strings

```lua
tostr(value, [hex])                        -- value -> string (hex formats numbers)
tonum(s)                                   -- string -> number, or nil
chr(code, ...)                             -- character(s) from byte codes
ord(s, [index])                            -- byte code of a character
sub(s, from, [to])                         -- substring (1-based, inclusive)
split(s, [sep], [to_number])               -- split into a table (default sep ",")
```

## Coroutines

```lua
cocreate(fn)                               -- create a coroutine
coresume(co, ...)                          -- run/continue it; returns ok, ...
costatus(co)                               -- "suspended", "running" or "dead"
yield(...)                                 -- pause the coroutine, handing values back
```
