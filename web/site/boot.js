/* Shared emscripten Module wiring for the console-bearing pages (home, console). Runs BEFORE
   lazy100.js. locateFile resolves lazy100.wasm next to this script (site root), so the same
   file works from /console/ and /carts/ subpages via relative <script src="../boot.js">. */
(function () {
  var dir = new URL(".", document.currentScript.src).href; // site root (where lazy100.js/.wasm live)
  var canvas = document.getElementById("canvas");
  var overlay = document.getElementById("overlay");
  var statusEl = document.getElementById("status");
  var logEl = document.getElementById("log");
  var footEl = document.getElementById("foot");
  var lines = 0, MAX = 800;

  window.lzAppendLog = function (text, cls) {
    if (text == null) return;
    var msg = String(text);
    if (footEl) footEl.textContent = msg;
    if (!logEl) return;
    var div = document.createElement("div");
    div.className = cls || "l"; div.textContent = msg;
    logEl.appendChild(div);
    if (++lines > MAX && logEl.firstChild) { logEl.removeChild(logEl.firstChild); lines--; }
    logEl.scrollTop = logEl.scrollHeight;
  };

  var Module = window.Module || {};
  Module.locateFile = function (p) { return dir + p; };
  Module.canvas = canvas;
  Module.print = function (t) { window.lzAppendLog(t, "l"); };
  Module.printErr = function (t) { window.lzAppendLog(t, /error|fail|abort/i.test(String(t)) ? "e" : "w"); };
  Module.setStatus = function (t) { if (statusEl) statusEl.textContent = t || "Running"; };
  Module.preRun = Module.preRun || [];
  Module.preRun.push(function () {
    try {
      var f = (typeof FS !== "undefined") ? FS : Module.FS;
      try { f.mkdir("/carts"); } catch (e) {}
      try { f.mkdir("/saves"); } catch (e) {}
      var idbfs = f.filesystems && f.filesystems.IDBFS;
      var addDep = Module.addRunDependency, remDep = Module.removeRunDependency;
      if (!idbfs || !addDep || !remDep) { window.lzAppendLog("no IDBFS - storage is session-only", "w"); return; }
      f.mount(idbfs, {}, "/carts");
      f.mount(idbfs, {}, "/saves");
      addDep("lz-idbfs");
      f.syncfs(true, function (err) { if (err) window.lzAppendLog("storage load failed: " + err, "w"); remDep("lz-idbfs"); });
    } catch (e) { window.lzAppendLog("storage init failed: " + e, "w"); }
  });
  Module.onRuntimeInitialized = function () {
    window.lzReady = true;
    if (overlay) overlay.classList.add("hidden");
    window.lzAppendLog("ready", "ok");
    if (window.lzOnReady) window.lzOnReady();
  };
  Module.onAbort = function (what) {
    window.lzAppendLog("ABORT: " + what, "e");
    if (overlay) overlay.classList.remove("hidden");
    if (statusEl) statusEl.textContent = "aborted";
  };
  if (overlay) setTimeout(function () { overlay.classList.add("hidden"); }, 8000);
  window.Module = Module;
})();
