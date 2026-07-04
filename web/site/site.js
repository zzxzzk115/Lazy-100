/* LAZY-100 site logic, shared by the three pages (body[data-page] = home | carts | console).
   - home/console embed the wasm console (boot.js wired Module beforehand).
   - home: featured rail; first interaction boots a random cart (or ?cart=<id> if given).
   - carts: searchable/filterable grid; clicking a cart opens the home player (../?cart=<id>).
   - console: the full shell + a file-insert. A touch gamepad drives home/console. */
(function () {
  "use strict";

  var CATALOG_BASE = "https://raw.githubusercontent.com/zzxzzk115/Lazy-100-games/main/";
  var PAGE = document.body.dataset.page || "home";
  var HAS_CONSOLE = PAGE === "home"; // the console lives on home now; carts is a catalog only

  var games = [], activeCat = null, query = "", catalogReady = false;
  var cartParam = new URLSearchParams(location.search).get("cart");

  var canvas = document.getElementById("canvas");
  var foot = document.getElementById("foot");
  var labelEl = document.getElementById("cart-label");

  function log(t, c) { if (window.lzAppendLog) window.lzAppendLog(t, c); }
  function esc(s) { var d = document.createElement("div"); d.textContent = s == null ? "" : s; return d.innerHTML; }
  function absURL(p) { return /^https?:/i.test(p) ? p : CATALOG_BASE + p.replace(/^\.?\//, ""); }
  function basename(p) { return p.split("/").pop(); }
  function thumbURL(g) {
    var p = g.preview || (g.cart && /\.png$/i.test(g.cart) ? g.cart : null);
    return p ? absURL(p) : null;
  }
  function setLabel(g) { if (labelEl) labelEl.innerHTML = g ? "<b>" + esc(g.name || g.id) + "</b>" : "<b>LAZY</b>-100"; }
  function focusCanvas() { try { if (canvas) canvas.focus(); } catch (e) {} }

  /* ---- console input injection (shared by the gamepad + the on-screen keyboard) ----
     Pad bits match Input::Button (Left=1 Right=2 Up=4 Down=8 O=16 X=32); key bits match
     Keyboard::Key (Escape=0 Return=1 Backspace=2 Delete=3 Tab=4 Left=5 Right=6 Up=7 Down=8). */
  var KB = { Escape: 0, Return: 1, Backspace: 2, Delete: 3, Tab: 4, Left: 5, Right: 6, Up: 7, Down: 8 };
  var padMaskG = 0, keyMaskG = 0;
  function pushInput() {
    if (!window.lzReady || !window.Module) return;
    window.Module.ccall("lazy100_set_pad", null, ["number"], [padMaskG]);
    window.Module.ccall("lazy100_set_keys", null, ["number"], [keyMaskG]);
  }
  function holdKey(bit, on) { if (on) keyMaskG |= (1 << bit); else keyMaskG &= ~(1 << bit); pushInput(); }
  function pulseKey(bit) { holdKey(bit, true); setTimeout(function () { holdKey(bit, false); }, 70); }
  function typeText(s) { if (window.lzReady && window.Module) window.Module.ccall("lazy100_type_text", null, ["string"], [s]); }

  /* ---- virtual mouse: the console screen is a trackpad (relative cursor + tap/hold-drag click) ---- */
  var mfbX = 160, mfbY = 120; // cursor in framebuffer coords (0..319 / 0..239)
  function sendMouse(btn) {
    if (window.lzReady && window.Module)
      window.Module.ccall("lazy100_set_mouse", null, ["number", "number", "number"], [Math.round(mfbX), Math.round(mfbY), btn]);
  }
  var ctrlMode = "pad"; // "pad" = run/boot (gamepad); "tool" = shell/editor/explore (keyboard + trackpad)

  /* ---- console sizing: largest integer multiple of 320x240 that fits (crisp pixels) ---- */
  var BASE_W = 320, BASE_H = 240, MAX_SCALE = 8, CHROME_X = 96, CHROME_Y = 136, MAIN_PAD = 40, GAP = 18;
  // Size the console to the largest INTEGER scale of 320x240 that fits, measured in CSS pixels.
  // Backing store == CSS size, so the console never up/downscales its own render; the browser then
  // maps that CSS canvas to device pixels with image-rendering:pixelated => a crisp integer DPR
  // upscale on hi-DPI phones. Mobile is clamped to at least 1x so the console + gamepad always draw.
  function fitConsole() {
    if (!canvas) return;
    var top = document.querySelector(".topbar"), bot = document.querySelector(".statusbar");
    var hH = top ? top.offsetHeight : 60, fH = bot ? bot.offsetHeight : 40;
    var vw = window.innerWidth, vh = window.innerHeight;
    var touch = window.matchMedia && window.matchMedia("(pointer:coarse)").matches;
    var narrow = vw <= 860 || touch; // handheld app-shell: narrow phones OR any touch device (e.g. iPad)
    // Reserve exactly what the CSS reserves for the fixed gamepad (var(--pad-reserve), incl. safe-area):
    // read .main's computed padding-bottom so JS and CSS can never drift out of sync.
    var padH = 0;
    if (touch) { var mainEl = document.querySelector(".main"); padH = mainEl ? (parseFloat(getComputedStyle(mainEl).paddingBottom) || 184) : 184; }
    document.body.classList.remove("too-small");

    var availW, availH;
    if (narrow) {
      // Console + gamepad win the vertical budget; the featured strip (if any) gets whatever's left,
      // and is capped in CSS so it can never steal the console's room.
      var stripH = 0;
      if (PAGE === "home") { var ph = document.getElementById("playhint"); stripH = (ph && ph.offsetParent) ? ph.offsetHeight + 8 : 0; }
      availW = vw - 16;
      availH = vh - hH - stripH - padH - 20;
    } else {
      var railW = (PAGE === "home") ? Math.min(260, Math.max(180, vw * 0.15)) : 0;
      var railReserve = (PAGE === "home") ? 2 * railW + 3 * GAP : 0;
      availW = vw - railReserve - CHROME_X - MAIN_PAD;
      availH = vh - hH - fH - CHROME_Y - MAIN_PAD;
    }
    var n = Math.floor(Math.min(availW / BASE_W, availH / BASE_H));
    if (n < 1) { if (narrow) n = 1; else { document.body.classList.add("too-small"); return; } }
    n = Math.min(MAX_SCALE, n);
    var bw = BASE_W * n, bh = BASE_H * n; // backing store == CSS size (crisp integer render)
    // Resize THROUGH SDL once the console is up: setting canvas.width directly leaves SDL
    // believing the old window size, and the present letterboxes into that stale region —
    // a spurious black frame inside the bezel. Pre-boot, set the attributes as a fallback.
    if (window.lzReady && window.Module) {
      try { window.Module.ccall("lazy100_resize", null, ["number", "number"], [bw, bh]); } catch (e) {}
    } else {
      if (canvas.width !== bw) canvas.width = bw;
      if (canvas.height !== bh) canvas.height = bh;
    }
    canvas.style.width = bw + "px";
    canvas.style.height = bh + "px";
  }
  var fitPending = false;
  function scheduleFit() { if (fitPending) return; fitPending = true; requestAnimationFrame(function () { fitPending = false; fitConsole(); }); }
  window.addEventListener("resize", scheduleFit);

  /* ---- booting a cart (home/console) ---- */
  // Fetch a cart's .lz100.png into MEMFS, then hand the path to `fn` (boot or arm).
  function withCart(g, fn) {
    if (!window.lzReady) { log("still booting — try again in a moment", "w"); return; }
    setLabel(g);
    fetch(absURL(g.cart)).then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status); return r.arrayBuffer(); })
      .then(function (buf) {
        var f = window.Module.FS || window.FS;
        var path = "/carts/" + basename(g.cart);
        try { f.mkdir("/carts"); } catch (e) {}
        f.writeFile(path, new Uint8Array(buf));
        fn(path, g);
        focusCanvas();
      }).catch(function (e) { log("cart download failed: " + e.message, "e"); });
  }
  // A chosen cart (a card click / ?cart / insert) boots right away — the click is the gesture.
  function bootGame(g) {
    if (!HAS_CONSOLE) { location.href = "../?cart=" + encodeURIComponent(g.id); return; }
    log("loading " + (g.name || g.id) + "…", "l");
    withCart(g, function (path, gg) {
      var ok = window.Module.ccall("lazy100_boot_cart", "number", ["string"], [path]);
      if (ok) { document.body.classList.add("booted"); log("running " + (gg.name || gg.id), "ok"); }
      else log("failed to start " + gg.id, "e");
    });
  }
  // Home: ARM a random cart so the console's own "press a key to start" gate runs it — the gate's
  // gesture (key, tap, or a virtual gamepad button) is what unlocks/warms the audio.
  var armed = false;
  function armRandom() {
    if (PAGE !== "home" || cartParam || armed || !games.length || !window.lzReady) return;
    armed = true;
    withCart(games[Math.floor(Math.random() * games.length)], function (path, gg) {
      window.Module.ccall("lazy100_arm_cart", "number", ["string"], [path]);
      log("press any key to play " + (gg.name || gg.id), "l");
    });
  }

  /* ---- cart cards: just the .lz100.png (art + title + author), at its natural ratio ---- */
  function makeCard(g) {
    var card = document.createElement("button");
    card.className = "card"; card.type = "button";
    card.title = (g.name || g.id) + (g.author ? " — by " + g.author : "");
    var t = thumbURL(g);
    card.innerHTML = t
      ? '<img class="thumb" alt="' + esc(g.name || g.id) + '" loading="lazy" src="' + esc(t) + '">'
      : '<div class="thumb noimg">' + esc(g.name || g.id) + '<br>' + esc(g.author ? "by " + g.author : "") + '</div>';
    card.addEventListener("click", function () { bootGame(g); });
    return card;
  }

  function renderRails() {
    var L = document.getElementById("rail-left"), R = document.getElementById("rail-right");
    if (!L || !R) return;
    var featured = games.filter(function (g) { return g.featured; });
    if (!featured.length) featured = games.slice(0, 6);
    // Handheld (touch or narrow) uses ONE top strip; desktop flanks the console with two rails.
    var single = window.matchMedia && (window.matchMedia("(pointer:coarse)").matches || window.innerWidth <= 860);
    L.innerHTML = '<div class="rail-title">▾ featured</div>';
    R.innerHTML = single ? "" : '<div class="rail-title">▾ featured</div>';
    featured.forEach(function (g, i) { ((!single && i % 2) ? R : L).appendChild(makeCard(g)); });
    var more = document.createElement("a");
    more.className = "morecarts"; more.href = "carts/"; more.textContent = "more carts ▸";
    (single ? L : R).appendChild(more);
    // Desktop: the insert/random buttons live in the right rail (mobile keeps them as the top strip).
    var ph = document.getElementById("playhint");
    if (ph && !single) R.appendChild(ph);
  }

  function renderChips() {
    var chips = document.getElementById("chips"); if (!chips) return;
    var cats = {}; games.forEach(function (g) { if (g.category) cats[g.category] = 1; });
    chips.innerHTML = "";
    Object.keys(cats).sort().forEach(function (name) {
      var b = document.createElement("button");
      b.className = "chip" + (activeCat === name ? " active" : "");
      b.type = "button"; b.textContent = name;
      b.addEventListener("click", function () { activeCat = (activeCat === name) ? null : name; renderChips(); renderGrid(); });
      chips.appendChild(b);
    });
  }
  function matches(g) {
    if (activeCat && g.category !== activeCat) return false;
    if (!query) return true;
    return [g.name, g.author, g.description, g.category, (g.tags || []).join(" ")].join(" ").toLowerCase().indexOf(query) !== -1;
  }
  function renderGrid() {
    var grid = document.getElementById("grid"), empty = document.getElementById("empty");
    if (!grid) return;
    grid.innerHTML = "";
    var shown = games.filter(matches);
    shown.forEach(function (g) { grid.appendChild(makeCard(g)); });
    if (empty) empty.hidden = shown.length > 0;
  }

  /* ---- touch gamepad (home/console): OR a button mask into the console input ---- */
  function setupGamepad() {
    var pad = document.getElementById("gamepad");
    if (!pad) return;
    // Each touch button drives TWO paths: the 6-bit game pad (for the running cart) AND the console
    // keyboard (so menus are navigable like a handheld). d-pad -> arrows, O -> Return, X -> Backspace,
    // ESC -> Escape.
    var PAD = { ArrowLeft: 1, ArrowRight: 2, ArrowUp: 4, ArrowDown: 8, KeyZ: 16, KeyX: 32 };
    var KEY = { Escape: 1 << KB.Escape, KeyZ: 1 << KB.Return, KeyX: 1 << KB.Backspace,
                ArrowLeft: 1 << KB.Left, ArrowRight: 1 << KB.Right, ArrowUp: 1 << KB.Up, ArrowDown: 1 << KB.Down };
    pad.querySelectorAll(".pad").forEach(function (btn) {
      var pbit = PAD[btn.dataset.key] || 0, kbit = KEY[btn.dataset.key] || 0;
      var down = function (e) { e.preventDefault(); padMaskG |= pbit; keyMaskG |= kbit; btn.classList.add("held"); pushInput(); };
      var up = function (e) { e.preventDefault(); padMaskG &= ~pbit; keyMaskG &= ~kbit; btn.classList.remove("held"); pushInput(); };
      btn.addEventListener("pointerdown", down);
      btn.addEventListener("pointerup", up);
      btn.addEventListener("pointercancel", up);
      btn.addEventListener("pointerleave", up);
      btn.addEventListener("contextmenu", function (e) { e.preventDefault(); });
    });
  }

  /* ---- on-screen keyboard (shell/editor/explore modes): a compact US layout; Shift is one-shot ---- */
  function buildSoftKeyboard() {
    var kb = document.getElementById("softkb");
    if (!kb) return;
    // Each printable key = [lower, shifted]; specials use {k:name,w:width,lbl:label}.
    var ROWS = [
      ["`~", "1!", "2@", "3#", "4$", "5%", "6^", "7&", "8*", "9(", "0)", "-_", "=+"],
      ["qQ", "wW", "eE", "rR", "tT", "yY", "uU", "iI", "oO", "pP", "[{", "]}", "\\|"],
      ["aA", "sS", "dD", "fF", "gG", "hH", "jJ", "kK", "lL", ";:", "'\"", { k: "Return", w: 2, lbl: "⏎" }],
      [{ k: "Shift", w: 2, lbl: "⇧" }, "zZ", "xX", "cC", "vV", "bB", "nN", "mM", ",<", ".>", "/?", { k: "Backspace", w: 2, lbl: "⌫" }],
      [{ k: "Escape", w: 2, lbl: "esc" }, { k: "Tab", lbl: "tab" }, { k: "Space", w: 5, lbl: "" }, { k: "Left", lbl: "←" }, { k: "Down", lbl: "↓" }, { k: "Up", lbl: "↑" }, { k: "Right", lbl: "→" }]
    ];
    var shift = false;
    function setShift(on) { shift = on; kb.querySelectorAll(".key.shift").forEach(function (k) { k.classList.toggle("on", on); }); kb.querySelectorAll(".key[data-ch]").forEach(function (k) { k.textContent = on ? k.dataset.sh : k.dataset.ch; }); }
    kb.innerHTML = "";
    ROWS.forEach(function (row) {
      var r = document.createElement("div"); r.className = "krow";
      row.forEach(function (spec) {
        var b = document.createElement("button"); b.type = "button"; b.className = "key";
        if (typeof spec === "string") {
          b.dataset.ch = spec[0]; b.dataset.sh = spec[1]; b.textContent = spec[0];
          b.addEventListener("pointerdown", function (e) { e.preventDefault(); b.classList.add("held"); typeText(shift ? b.dataset.sh : b.dataset.ch); if (shift) setShift(false); });
          b.addEventListener("pointerup", function (e) { e.preventDefault(); b.classList.remove("held"); });
          b.addEventListener("pointerleave", function () { b.classList.remove("held"); });
        } else {
          if (spec.w) b.style.flexGrow = spec.w;
          b.textContent = spec.lbl;
          if (spec.k === "Shift") {
            b.className = "key shift";
            b.addEventListener("pointerdown", function (e) { e.preventDefault(); setShift(!shift); });
          } else if (spec.k === "Space") {
            b.addEventListener("pointerdown", function (e) { e.preventDefault(); b.classList.add("held"); typeText(" "); });
            b.addEventListener("pointerup", function (e) { e.preventDefault(); b.classList.remove("held"); });
            b.addEventListener("pointerleave", function () { b.classList.remove("held"); });
          } else {
            // special keys inject Keyboard::Key edges (held while pressed for repeat)
            var bit = KB[spec.k];
            b.addEventListener("pointerdown", function (e) { e.preventDefault(); b.classList.add("held"); holdKey(bit, true); });
            b.addEventListener("pointerup", function (e) { e.preventDefault(); b.classList.remove("held"); holdKey(bit, false); });
            b.addEventListener("pointercancel", function () { b.classList.remove("held"); holdKey(bit, false); });
            b.addEventListener("pointerleave", function () { if (b.classList.contains("held")) { b.classList.remove("held"); holdKey(bit, false); } });
          }
        }
        b.addEventListener("contextmenu", function (e) { e.preventDefault(); });
        r.appendChild(b);
      });
      kb.appendChild(r);
    });
  }

  /* ---- the console screen as a trackpad: relative cursor move; tap = click; hold+drag = drag ---- */
  function setupTrackpad() {
    if (!canvas) return;
    var active = false, moved = false, dragging = false, lastX = 0, lastY = 0, startX = 0, startY = 0, downT = 0, holdT = null;
    var SENS = 1.4;
    function scale() { var r = canvas.getBoundingClientRect(); return r.width > 0 ? (BASE_W / r.width) * SENS : SENS; }
    canvas.addEventListener("pointerdown", function (e) {
      if (ctrlMode !== "tool") return; // only a trackpad in shell/editor/explore
      e.preventDefault();
      active = true; moved = false; dragging = false; downT = Date.now();
      lastX = startX = e.clientX; lastY = startY = e.clientY;
      try { canvas.setPointerCapture(e.pointerId); } catch (er) {}
      holdT = setTimeout(function () { if (active && !moved) { dragging = true; sendMouse(1); } }, 320); // long-press -> button down
    });
    canvas.addEventListener("pointermove", function (e) {
      if (!active) return;
      e.preventDefault();
      var s = scale();
      mfbX = Math.max(0, Math.min(BASE_W - 1, mfbX + (e.clientX - lastX) * s));
      mfbY = Math.max(0, Math.min(BASE_H - 1, mfbY + (e.clientY - lastY) * s));
      lastX = e.clientX; lastY = e.clientY;
      if (Math.abs(e.clientX - startX) + Math.abs(e.clientY - startY) > 6) moved = true;
      sendMouse(dragging ? 1 : 0);
    });
    function end(e) {
      if (!active) return;
      active = false;
      if (holdT) { clearTimeout(holdT); holdT = null; }
      if (dragging) { dragging = false; sendMouse(0); }
      else if (!moved && Date.now() - downT < 320) { sendMouse(1); setTimeout(function () { sendMouse(0); }, 70); } // tap = click
      else sendMouse(0);
    }
    canvas.addEventListener("pointerup", end);
    canvas.addEventListener("pointercancel", end);
  }

  /* ---- mode-aware controls: run-cart -> gamepad; shell/editor/explore -> keyboard + trackpad ---- */
  function setControlMode(mode) {
    if (mode === ctrlMode) return;
    ctrlMode = mode;
    document.body.dataset.ctrl = mode;
    if (mode === "pad") { sendMouse(-1); }        // release the injected mouse
    else { mfbX = BASE_W / 2; mfbY = BASE_H / 2; sendMouse(0); } // park cursor at centre
    scheduleFit(); // the keyboard reserves more height than the gamepad, so re-fit the console
  }
  function pollMode() {
    if (!window.lzReady || !window.Module) return;
    var m = window.Module.ccall("lazy100_mode", "number", [], []);
    // Only Shell/Editor need typing (keyboard + trackpad). Explore is arrows/Enter navigation,
    // which the gamepad already drives via its keyboard injection — so it stays on the pad.
    setControlMode((m === 1 || m === 3) ? "tool" : "pad");
  }

  /* ---- home mobile: no featured strip; an insert + random-cart prompt instead ---- */
  function setupPlayhint() {
    var ins = document.getElementById("hint-insert"), rnd = document.getElementById("hint-random");
    var file = document.getElementById("file");
    if (ins && file) ins.addEventListener("click", function () { file.click(); });
    if (rnd) rnd.addEventListener("click", function () {
      if (!games.length) { log("catalog still loading…", "w"); return; }
      bootGame(games[Math.floor(Math.random() * games.length)]);
    });
  }

  /* ---- local file insert -> play directly (the picker is opened by the playhint insert button) ---- */
  function setupInsert() {
    var pick = document.getElementById("pick"), file = document.getElementById("file");
    if (!file) return;
    if (pick) pick.addEventListener("click", function () { file.click(); });
    file.addEventListener("change", function (e) {
      var files = e.target.files; if (!files || !files.length) return;
      var f0 = files[0]; e.target.value = "";
      if (!window.lzReady) { log("still booting…", "w"); return; }
      var reader = new FileReader();
      reader.onload = function () {
        var fs = window.Module.FS || window.FS;
        var path = "/carts/" + f0.name;
        try { fs.mkdir("/carts"); } catch (e2) {}
        fs.writeFile(path, new Uint8Array(reader.result));
        try { fs.syncfs(false, function () {}); } catch (e2) {}
        var ok = window.Module.ccall("lazy100_boot_cart", "number", ["string"], [path]);
        log(ok ? "running " + f0.name : "failed to start " + f0.name, ok ? "ok" : "e");
        document.body.classList.add("booted");
        setLabel({ name: f0.name.replace(/\.[^.]+$/, "") });
        focusCanvas();
      };
      reader.readAsArrayBuffer(f0);
    });
  }

  /* ---- block iOS pinch-zoom + the double-tap-and-hold magnifier loupe ---- */
  function setupMobileChrome() {
    // touch-action in CSS already kills double-tap zoom; these kill the multi-touch pinch gestures
    // that iOS still allows despite the viewport meta.
    ["gesturestart", "gesturechange", "gestureend"].forEach(function (evt) {
      document.addEventListener(evt, function (e) { e.preventDefault(); }, { passive: false });
    });
    document.addEventListener("touchmove", function (e) {
      if (e.touches && e.touches.length > 1) e.preventDefault();
    }, { passive: false });
    // The magnifier loupe arms on a RAPID re-touch (tap, then touch again and hold). user-select:none
    // doesn't stop it; preventDefault on that quick second touchstart does. Pointer events still fire
    // (they dispatch before/independently of touch defaults), so mashing pads/keys keeps working —
    // only the synthetic click of that second tap is lost, which no control here relies on.
    var lastTouchEnd = 0;
    document.addEventListener("touchend", function () { lastTouchEnd = Date.now(); }, true);
    document.addEventListener("touchstart", function (e) {
      if (Date.now() - lastTouchEnd < 350) {
        var t = e.target;
        if (!(t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA"))) e.preventDefault();
      }
    }, { passive: false });
  }

  /* ---- audio device lifecycle across background/foreground ----
     iOS revokes the page's audio session in the background, and the old AudioContext is unreliable
     afterwards (it can even claim "running" while producing silence). So: on hide, auto-pause the
     cart AND kill the audio device outright (lazy100_audio_suspend); on return, rebuild it from
     scratch (lazy100_audio_resume — fresh AudioContext, warm-up re-armed). A context rebuilt
     outside a gesture starts suspended, so keep nudging it on each gesture until it runs. */
  var audioDown = false; // armed when the tab hides; cleared once the fresh context is running
  function audioResume() {
    if (!window.lzReady || !window.Module) return;
    try { window.Module.ccall("lazy100_audio_resume", null, [], []); } catch (e) {} // idempotent
    try {
      var ma = window.miniaudio, ok = true;
      if (ma && ma.devices) for (var i = 0; i < ma.devices.length; ++i) {
        var d = ma.devices[i];
        if (d && d.webaudio && d.webaudio.state !== "running") { ok = false; d.webaudio.resume().catch(function () {}); }
      }
      if (ok) audioDown = false; // fresh context is running — done
    } catch (e) {}
  }
  function setupAudioResume() {
    document.addEventListener("visibilitychange", function () {
      if (!window.lzReady || !window.Module) return;
      if (document.hidden) {
        audioDown = true;
        // Synchronous C calls — safe even though the render loop is about to stop.
        try { window.Module.ccall("lazy100_pause", null, [], []); } catch (e) {}         // ESC pause menu
        try { window.Module.ccall("lazy100_audio_suspend", null, [], []); } catch (e) {} // kill the device
      } else audioResume();
    });
    window.addEventListener("pagehide", function () { audioDown = true; });
    window.addEventListener("pageshow", function () { if (audioDown) audioResume(); });
    window.addEventListener("focus", function () { if (audioDown) audioResume(); });
    document.addEventListener("touchend", function () { if (audioDown) audioResume(); }, true);
    document.addEventListener("pointerdown", function () { if (audioDown) audioResume(); }, true);
  }

  /* ---- per-page init ---- */
  function tryStartParam() {
    if (PAGE !== "home" || !cartParam || !catalogReady || !window.lzReady) return;
    var g = games.filter(function (x) { return x.id === cartParam; })[0];
    if (g) { bootGame(g); cartParam = null; }
  }

  setupMobileChrome();
  if (HAS_CONSOLE) {
    // The full console lives on home now: gamepad for carts, on-screen keyboard + screen-trackpad for
    // the shell/editors. Mode polling swaps the control set. (No kiosk gating — home is not crippled.)
    document.body.dataset.ctrl = "pad";
    window.lzOnReady = function () { fitConsole(); tryStartParam(); armRandom(); pollMode(); };
    fitConsole();
    setupGamepad();
    buildSoftKeyboard();
    setupTrackpad();
    setupInsert();
    setupPlayhint();
    setupAudioResume();
    setInterval(pollMode, 200);
  }

  /* Catalog: home needs the rail, carts needs the grid; console can skip it. */
  if (PAGE !== "console") {
    fetch(CATALOG_BASE + "games.json").then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status); return r.json(); })
      .then(function (j) {
        games = (j && j.games) || [];
        catalogReady = true;
        if (foot) foot.textContent = games.length + " carts in catalog";
        if (PAGE === "home") { renderRails(); scheduleFit(); tryStartParam(); armRandom(); }
        if (PAGE === "carts") {
          renderChips(); renderGrid();
          var search = document.getElementById("search");
          if (search) search.addEventListener("input", function () { query = search.value.trim().toLowerCase(); renderGrid(); });
        }
      }).catch(function (e) { log("catalog load failed: " + e.message, "e"); if (foot) foot.textContent = "catalog unavailable"; });
  }
})();
