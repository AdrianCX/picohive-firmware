// Skin selection is a local preference. It never goes near the device config.
// THEMES and STR come from the page's strings (build_web_page.py). Each skin
// brings its own vocabulary; a key a skin lacks falls back to "cartoon", and a
// browser with no stored preference gets "clean".
var currentTheme = "cartoon";

// WPA3 (4, 5) gets disassociated every ~11h by the CYW43439 firmware, see cyw43-driver #161.
function updateWifiAuthWarn() {
    var auth = document.getElementById("wifi_auth");
    var warn = document.getElementById("wifi_auth_warn");
    if (!auth || !warn) return;
    warn.style.display = (auth.value == "4" || auth.value == "5") ? "" : "none";
}

function T(key) {
    var t = STR[currentTheme] || STR.cartoon;
    return (key in t) ? t[key] : STR.cartoon[key];
}

// Re-labels every element carrying data-t, plus the document title.
function applyThemeText() {
    if (!document.body) return;
    var nodes = document.querySelectorAll("[data-t]");
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        var v = T(n.getAttribute("data-t"));
        if (v === undefined) continue;
        if (n.tagName === "INPUT") n.value = v; else n.textContent = v;
    }
    document.title = T("title");
    // Defined in a later script block, so it is not there for the early call.
    if (typeof refreshSecretPlaceholders === "function") refreshSecretPlaceholders();
    var h1 = document.querySelector("h1[data-text]");
    if (h1) h1.setAttribute("data-text", h1.textContent);
    // hudSet skips repaints that would not change anything; clear its memo so
    // the strip picks the new vocabulary up on the next 200ms tick.
    if (typeof hudLast !== "undefined") hudLast = "";
}

function setTheme(name) {
    if (THEMES.indexOf(name) < 0) name = "cartoon";
    currentTheme = name;
    var root = document.documentElement;
    for (var i = 0; i < THEMES.length; i++) root.classList.remove("t-" + THEMES[i]);
    root.classList.add("t-" + name);
    try { localStorage.setItem("epaper.theme", name); } catch (e) {}
    var sel = document.getElementById("theme");
    if (sel && sel.value !== name) sel.value = name;
    // A line from the previous skin has no business on the new one, so it
    // goes now and the new skin's first line follows it in.
    var w = document.getElementById("whisper");
    if (w) {
        w.classList.remove("on");
        w.textContent = "";
        if (window.__whisperNext) setTimeout(window.__whisperNext, 300);
    }
    applyThemeText();
}

// Runs while <head> is still parsing, so the page never flashes the other skin.
(function () {
    var saved = null;
    try { saved = localStorage.getItem("epaper.theme"); } catch (e) {}
    setTheme(saved || "clean");
})();

document.addEventListener("DOMContentLoaded", function () {
    var sel = document.getElementById("theme");
    if (sel) sel.value = currentTheme;
    applyThemeText();   // the early call ran before there was a body to label
});
