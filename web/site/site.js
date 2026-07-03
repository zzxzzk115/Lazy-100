/* LAZY-100 site logic: fetch the Lazy-100-games catalog, render cartridge cards, switch
   views, and boot a clicked cart in the embedded console via the exported lazy100_boot_cart.
   The wasm Module is wired up in index.html (runs before lazy100.js); here we only use
   Module.ccall / Module.FS once window.lzReady is set. */
(function () {
  "use strict";

  var CATALOG_BASE = "https://raw.githubusercontent.com/zzxzzk115/Lazy-100-games/main/";

  var games = [];          // catalog entries
  var activeCat = null;    // category filter, or null for all
  var query = "";          // search text (lowercased)
  var bootedId = null;     // currently running cart id

  var el = {
    body: document.body,
    railL: document.getElementById("rail-left"),
    railR: document.getElementById("rail-right"),
    grid: document.getElementById("grid"),
    empty: document.getElementById("empty"),
    chips: document.getElementById("chips"),
    search: document.getElementById("search"),
    label: document.getElementById("cart-label"),
    canvas: document.getElementById("canvas"),
    foot: document.getElementById("foot")
  };

  /* ---- helpers ---- */
  function log(t, c) { if (window.lzAppendLog) window.lzAppendLog(t, c); }
  function absURL(p) { return /^https?:/i.test(p) ? p : CATALOG_BASE + p.replace(/^\.?\//, ""); }
  function thumbURL(g) {
    var p = g.preview || (g.cart && /\.png$/i.test(g.cart) ? g.cart : null);
    return p ? absURL(p) : null;
  }
  function basename(p) { return p.split("/").pop(); }
  function setLabel(g) {
    el.label.innerHTML = g ? "<b>" + esc(g.name || g.id) + "</b>" : "<b>LAZY</b>-100";
  }
  function esc(s) { var d = document.createElement("div"); d.textContent = s == null ? "" : s; return d.innerHTML; }

  /* ---- console sizing: discrete integer scale of the 320x240 screen ----
     Pick the largest N (1..MAX) such that 320N x 240N fits the space left after the header,
     footer, cart bezel and (on Home) the flanking rails. Set both the canvas backing store and
     its CSS size to 320N x 240N so the GL present renders at exactly Nx and every pixel is crisp.
     Below a 1x fit, hide the console (body.too-small). */
  var BASE_W = 320, BASE_H = 240, MAX_SCALE = 8;
  var CHROME_X = 96, CHROME_Y = 136, MAIN_PAD = 40, GAP = 18; // cart bezel + label + .main padding

  function fitConsole() {
    var isConsole = el.body.classList.contains("view-console");
    var isCarts = el.body.classList.contains("view-carts");
    if (isCarts) return; // carts view hides the stage
    var top = document.querySelector(".topbar"), bot = document.querySelector(".statusbar");
    var hH = top ? top.offsetHeight : 60, fH = bot ? bot.offsetHeight : 40;
    var vw = window.innerWidth, vh = window.innerHeight;
    var railW = (!isConsole) ? Math.min(260, Math.max(180, vw * 0.15)) : 0;
    var railReserve = (!isConsole) ? 2 * railW + 3 * GAP : 0;
    var availW = vw - railReserve - CHROME_X - MAIN_PAD;
    var availH = vh - hH - fH - CHROME_Y - MAIN_PAD;
    var n = Math.floor(Math.min(availW / BASE_W, availH / BASE_H));
    var tooSmall = n < 1;
    el.body.classList.toggle("too-small", tooSmall);
    if (tooSmall) return;
    n = Math.min(MAX_SCALE, n);
    var w = BASE_W * n, h = BASE_H * n;
    if (el.canvas.width !== w) el.canvas.width = w;     // backing store: GL renders 320x240 -> Nx
    if (el.canvas.height !== h) el.canvas.height = h;
    el.canvas.style.width = w + "px";
    el.canvas.style.height = h + "px";
  }
  var fitPending = false;
  function scheduleFit() {
    if (fitPending) return; fitPending = true;
    requestAnimationFrame(function () { fitPending = false; fitConsole(); });
  }
  window.addEventListener("resize", scheduleFit);

  /* ---- view switching ---- */
  function setView(name) {
    el.body.className = "view-" + name;
    var tabs = document.querySelectorAll(".tab");
    for (var i = 0; i < tabs.length; i++) tabs[i].classList.toggle("active", tabs[i].dataset.view === name);
    fitConsole();
    if (name !== "carts") focusCanvas();
  }
  function focusCanvas() { try { el.canvas.focus(); } catch (e) {} }

  document.querySelectorAll(".tab").forEach(function (t) {
    t.addEventListener("click", function () { setView(t.dataset.view); });
  });
  document.getElementById("home-link").addEventListener("click", function () { setView("home"); });

  /* ---- booting a cart ---- */
  function bootGame(g) {
    if (!window.lzReady) { log("still booting — try again in a moment", "w"); return; }
    var url = absURL(g.cart);
    setLabel(g);
    log("loading " + (g.name || g.id) + "…", "l");
    fetch(url).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.arrayBuffer();
    }).then(function (buf) {
      var f = window.Module.FS || window.FS;
      var path = "/carts/" + basename(g.cart);
      try { f.mkdir("/carts"); } catch (e) {}
      f.writeFile(path, new Uint8Array(buf));
      var ok = window.Module.ccall("lazy100_boot_cart", "number", ["string"], [path]);
      if (ok) { bootedId = g.id; log("running " + (g.name || g.id), "ok"); }
      else { log("failed to start " + g.id, "e"); }
      focusCanvas();
    }).catch(function (e) { log("cart download failed: " + e.message, "e"); });
  }

  /* ---- card rendering ---- */
  function makeCard(g) {
    var card = document.createElement("button");
    card.className = "card"; card.type = "button";
    var t = thumbURL(g);
    card.innerHTML =
      (t ? '<img class="thumb" alt="" loading="lazy" src="' + esc(t) + '">'
         : '<div class="thumb"></div>') +
      '<span class="cname">' + esc(g.name || g.id) + '</span>' +
      '<span class="cauthor">' + esc(g.author ? "by " + g.author : "") + '</span>';
    card.addEventListener("click", function () { bootGame(g); setView("home"); });
    return card;
  }

  function renderRails() {
    var featured = games.filter(function (g) { return g.featured; });
    if (!featured.length) featured = games.slice(0, 6);
    el.railL.innerHTML = '<div class="rail-title">▾ featured</div>';
    el.railR.innerHTML = '<div class="rail-title">▾ featured</div>';
    featured.forEach(function (g, i) { (i % 2 ? el.railR : el.railL).appendChild(makeCard(g)); });
    var more = document.createElement("button");
    more.className = "morecarts"; more.type = "button"; more.textContent = "more carts ▸";
    more.addEventListener("click", function () { setView("carts"); });
    el.railR.appendChild(more);
  }

  function renderChips() {
    var cats = {};
    games.forEach(function (g) { if (g.category) cats[g.category] = 1; });
    var names = Object.keys(cats).sort();
    el.chips.innerHTML = "";
    names.forEach(function (name) {
      var b = document.createElement("button");
      b.className = "chip" + (activeCat === name ? " active" : "");
      b.type = "button"; b.textContent = name;
      b.addEventListener("click", function () {
        activeCat = (activeCat === name) ? null : name; renderChips(); renderGrid();
      });
      el.chips.appendChild(b);
    });
  }

  function matches(g) {
    if (activeCat && g.category !== activeCat) return false;
    if (!query) return true;
    var hay = [g.name, g.author, g.description, g.category, (g.tags || []).join(" ")].join(" ").toLowerCase();
    return hay.indexOf(query) !== -1;
  }

  function renderGrid() {
    el.grid.innerHTML = "";
    var shown = games.filter(matches);
    shown.forEach(function (g) { el.grid.appendChild(makeCard(g)); });
    el.empty.hidden = shown.length > 0;
  }

  el.search.addEventListener("input", function () { query = el.search.value.trim().toLowerCase(); renderGrid(); });

  /* ---- console tab: insert-cartridge → play directly ---- */
  document.getElementById("pick").addEventListener("click", function () { document.getElementById("file").click(); });
  document.getElementById("file").addEventListener("change", function (e) {
    var files = e.target.files; if (!files || !files.length) return;
    var file = files[0]; e.target.value = "";
    if (!window.lzReady) { log("still booting…", "w"); return; }
    var reader = new FileReader();
    reader.onload = function () {
      var f = window.Module.FS || window.FS;
      var path = "/carts/" + file.name;
      try { f.mkdir("/carts"); } catch (e2) {}
      f.writeFile(path, new Uint8Array(reader.result));
      try { f.syncfs(false, function () {}); } catch (e2) {}
      var ok = window.Module.ccall("lazy100_boot_cart", "number", ["string"], [path]);
      log(ok ? "running " + file.name : "failed to start " + file.name, ok ? "ok" : "e");
      setLabel({ name: file.name.replace(/\.[^.]+$/, "") });
      focusCanvas();
    };
    reader.readAsArrayBuffer(file);
  });

  /* ---- boot ---- The console idles on its power-on "play" gate; clicking a cart boots it
     through the splash ceremony (lazy100_boot_cart -> restart_with_cart). No silent autoplay. */
  window.lzOnReady = function () { fitConsole(); };

  fetch(CATALOG_BASE + "games.json").then(function (r) {
    if (!r.ok) throw new Error("HTTP " + r.status);
    return r.json();
  }).then(function (j) {
    games = (j && j.games) || [];
    if (el.foot) el.foot.textContent = games.length + " carts in catalog";
    renderRails(); renderChips(); renderGrid();
  }).catch(function (e) {
    log("catalog load failed: " + e.message, "e");
    if (el.foot) el.foot.textContent = "catalog unavailable";
  });

  fitConsole(); /* size the console immediately (recomputed on ready / resize / tab change) */
})();
