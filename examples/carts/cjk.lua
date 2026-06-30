-- cjk.lua - Lazy-100 runtime font demo: mixed Latin + 中日韩 via the Fusion Pixel font.

function _draw()
  cls(1)
  print("Lazy-100 fantasy console", 8, 8, 7)
  print("the quick brown fox 0123456789", 8, 22, 6)

  print("简体中文: 你好，世界！梦幻游戏主机", 8, 44, 10)
  print("日本語: こんにちは、世界！ゲーム", 8, 58, 11)
  print("한국어: 안녕하세요, 세계! 게임기", 8, 72, 12)

  print("混排 mixed: AI + 像素 ピクセル 픽셀", 8, 94, 14)

  local n = flr(t() * 8) % 100
  print("帧计数 frame: " .. n, 8, 116, 9)
end
