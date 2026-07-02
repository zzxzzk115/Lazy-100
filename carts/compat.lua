-- compat smoke test: exercises the new API surface, draws ok/XX per check.
function _init()
  results = {}
  local function check(name, ok) add(results, {name = name, ok = ok}) end
  local function feq(a, b) return abs(a - b) < 1e-9 end

  check("sin turns", feq(sin(0.25), -1))
  check("cos turns", feq(cos(0.5), -1))
  check("atan2 down", feq(atan2(0, 1), 0.75))
  check("atan2 east", feq(atan2(1, 0), 0))

  local t2 = {}
  add(t2, "a") add(t2, "b") add(t2, "c", 1)
  check("add/pos", t2[1] == "c" and #t2 == 3)
  del(t2, "a")
  check("del", #t2 == 2 and t2[2] == "b")
  deli(t2, 1)
  check("deli", #t2 == 1 and t2[1] == "b")
  check("count", count({1, 2, 2, 3}, 2) == 2)
  local sum = 0
  foreach({1, 2, 3}, function(v) sum = sum + v end)
  check("foreach", sum == 6)

  check("split num", split("1,2,3")[2] == 2)
  check("split str", split("a;b", ";")[2] == "b")
  check("tostr", tostr(nil) == "[nil]" and tostr(255, true) == "0xff")
  check("tonum", tonum("42") == 42)
  check("chr/ord", chr(65) == "A" and ord("A") == 65)
  check("sub", sub("hello", 2, 3) == "el")

  local co = cocreate(function() yield(7) end)
  local okc, v = coresume(co)
  check("coroutine", okc and v == 7 and costatus(co) == "suspended")

  check("rnd table", rnd({5, 5, 5}) == 5)
  check("stat", stat(999) == 0)
  check("stubs", peek(0) == 0 and menuitem() == nil)
  check("cartdata", cartdata("compat") == true)
end

function _draw()
  cls(0)
  camera(-6, -4) -- everything below shifts right 6 / down 4
  local pass, fail = 0, 0
  for i, r in ipairs(results) do
    if r.ok then pass = pass + 1 else fail = fail + 1 end
    print((r.ok and "ok " or "XX ") .. r.name, 0, (i - 1) * 10, r.ok and 11 or 8)
  end
  camera()
  print("pass " .. pass .. " / " .. #results, 240, 4, fail == 0 and 11 or 8)
  -- clip + oval visual check, bottom right: orange fill must stay inside the clip box
  clip(220, 170, 70, 50)
  ovalfill(210, 160, 310, 235, 9)
  clip()
  oval(220, 170, 289, 219, 7)
  rect(219, 169, 290, 220, 5)
end
