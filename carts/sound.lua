-- sound.lua - Lazy-100 M11 smoke test: exercise the audio engine paths.
-- The sound bank is empty unless authored in the SFX/MUSIC editors, but calling sfx()/music()
-- still drives the queue, the channel sequencer, and the music snapshot double-buffer. Press
-- Z to trigger an sfx; the music sequencer starts at _init.

function _init()
  music(0)   -- start the (empty) music sequencer: exercises the bank snapshot + chaining
  frame = 0
end

function _update()
  frame = frame + 1
  if btnp(4) then sfx(0) end        -- Z: play sfx 0 on an auto-picked channel
  if frame == 90 then music(-1) end -- stop music after ~3s
end

function _draw()
  cls(1)
  print("SOUND smoke test", 8, 8, 7)
  print("Z: sfx(0)   music(0) at init", 8, 24, 6)
  print("frame "..frame, 8, 48, 5)
end
