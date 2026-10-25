
// --- Hooks for features ----------------------------------------------------
// A feature's script plugs in through these instead of being named in here, so
// leaving a feature out of the page leaves nothing behind that calls it.
//
//   onMessage("/rc/ai/status", fn)   fn(body) gets that answer instead of a toast
//   onConfigLoaded(fn)               fn(config) after the board's settings fill the fields
//   onConfigSent(fn)                 fn(config) after Bind sent them
//   onSubTabShown("sub_calendar", fn) fn() when that Config section is opened
var MESSAGE_HANDLERS = {};
var STREAMED = {};
var CONFIG_LOADED_HOOKS = [];
var CONFIG_SAVE_HOOKS = [];
var CONFIG_SENT_HOOKS = [];
var SUBTAB_HOOKS = {};

function onMessage(path, fn) { MESSAGE_HANDLERS[path] = fn; }
// A command that streams its answer (streams_output on the board) sends
// "<path>\0<total bytes>", then the body in pieces: fn gets it whole.
function onStreamedMessage(path, fn) { STREAMED[path] = true; MESSAGE_HANDLERS[path] = fn; }
function onConfigLoaded(fn) { CONFIG_LOADED_HOOKS.push(fn); }
// Before a save: fn(config) can change what is sent, or return false to stop it
// (after saying why, e.g. with hudToast).
function onConfigSave(fn) { CONFIG_SAVE_HOOKS.push(fn); }
function onConfigSent(fn) { CONFIG_SENT_HOOKS.push(fn); }
function onSubTabShown(panel, fn) { (SUBTAB_HOOKS[panel] = SUBTAB_HOOKS[panel] || []).push(fn); }

// --- Connection HUD ------------------------------------------------------
// The only answer the page acts on is the config (/rc/config/read), so there is no application
// level ack to wait for on the other commands. What the browser does give us
// is ws.bufferedAmount: bytes sit there until they have been handed to the
// TCP stack, and for anything larger than a socket buffer (a firmware
// image is ~500KB) that only drains as the far end acks. So "delivered"
// below means
// the bytes left the browser, and a socket that dies with bytes still queued
// means the command did not make it.

function fmtBytes(n) {
    if (n < 1024) return n + " B";
    if (n < 1024*1024) return (n/1024).toFixed(1) + " KB";
    return (n/(1024*1024)).toFixed(1) + " MB";
}

var hudLast = "";

function hudSet(cls, title, detail) {
    var bar = document.getElementById("hud");
    if (!bar) return; // still parsing <head>, the next tick will paint it
    var signature = cls + "|" + title + "|" + detail;
    if (signature === hudLast) return;
    hudLast = signature;
    bar.className = "hud hud-" + cls;
    document.getElementById("hud-state").textContent = title;
    document.getElementById("hud-detail").textContent = detail;
}

function hudProgress(fraction) {
    var fill = document.getElementById("hud-bar-fill");
    if (fill) fill.style.width = Math.round(fraction * 100) + "%";
}

// The same toast again while it is still up is counted on it, not stacked, and keeps it up.
var hudToasts = {};

function hudToast(kind, text) {
    var box = document.getElementById("toasts");
    if (!box) return;

    var key = kind + "|" + text;
    var toast = hudToasts[key];

    if (toast) {
        ++toast.count;
        toast.el.textContent = text + " \u00D7" + toast.count;
        clearTimeout(toast.timer);
    }
    else {
        var el = document.createElement("div");
        el.className = "toast " + kind;
        el.textContent = text;
        box.appendChild(el);
        toast = hudToasts[key] = { el: el, count: 1, timer: 0 };
    }

    toast.timer = setTimeout(() => {
        delete hudToasts[key];
        toast.el.className += " out";
        setTimeout(() => { if (toast.el.parentNode) toast.el.parentNode.removeChild(toast.el); }, 400);
    }, 3500);
}

// Websocket passing of data to the pico
class WebSocketHandler {
    constructor() {
        this.ws = null;
        this.retries = 0;          // failed attempts since the last good link
        this.opened = false;       // did *this* socket ever reach OPEN?
        this.queuedBytes = 0;      // everything handed to ws.send() since connect
        this.pending = [];         // commands not fully out of the browser yet
        this.streamIn = null;      // a streamed answer being gathered, see onStreamedMessage
        this.flash = null;         // short lived "delivered!" / "failed!" banner
        this.reason = "";
        this.retryAt = 0;
        this.reconnectTimer = null;

        this.connect();
        this.timer = setInterval(() => this.periodic(), 200);
    }

    url() {
        if (window.location.protocol == "https:") {
            return "wss://" + location.host + "/websocket";
        }
        if (window.location.protocol == "http:") {
            return "ws://" + location.host + "/websocket";
        }
        return "ws://127.0.0.1:80/websocket";
    }

    connect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        // Never leave a half open socket behind, the pico only has room for
        // a couple of connections. Its close event lands after this.ws has
        // been replaced, so the listeners below drop it as stale.
        if (this.ws) {
            try { this.ws.close(); } catch (e) {}
        }

        this.opened = false;
        this.queuedBytes = 0;
        this.pending = [];
        this.retryAt = 0;

        var sock;
        try {
            sock = new WebSocket(this.url());
        } catch (e) {
            console.log("WebSocket create failed: ", e);
            this.scheduleReconnect(T("err.noOpen"));
            return;
        }

        // Stale sockets keep firing events after we moved on, so every
        // listener checks it is still the socket we care about.
        this.ws = sock;
        this.lastError = "";
        this.render();

        sock.addEventListener("open", () => {
            if (sock !== this.ws) return;
            console.log("WebSocket open");
            this.opened = true;
            this.retries = 0;
            this.reason = "";
            hudToast("ok", T("toast.contact"));
            this.send(new Blob(["/rc/config/read"]), T("lbl.mindRead"), true);
        });

        sock.addEventListener("error", (event) => {
            console.log("WebSocket error: ", event);
            if (sock !== this.ws) return;
            this.lastError = this.opened ? T("err.wire") : T("err.noAnswer");
        });

        sock.addEventListener("close", (event) => {
            console.log("WebSocket closed: ", event.code);
            if (sock !== this.ws) return;
            this.streamIn = null;
            this.failPending(this.opened ? T("err.snapMid") : T("err.noLink"));
            this.scheduleReconnect(this.opened
                ? T("err.snapped") + (event.code ? " (code " + event.code + ")" : "")
                : (this.lastError || T("err.refused")));
        });

        sock.addEventListener("message", (event) => {
            console.log("Message from server ", event.data);
            console.log(event.data);

            var reader = new FileReader();

            reader.onload = () => {
                var text = reader.result;
                console.log("Result: " + text);

                // A streamed answer (onStreamedMessage) is gathered here, then
                // handled below as if it had come whole.
                if (this.streamIn) {
                    this.streamIn.parts.push(text);
                    this.streamIn.left -= event.data.size;
                    if (this.streamIn.left > 0) return;
                    text = this.streamIn.path + "\0" + this.streamIn.parts.join("");
                    this.streamIn = null;
                } else {
                    var head = text.indexOf("\0");
                    var start = (head > 0) ? text.substring(0, head) : "";
                    if (STREAMED[start] && /^\d+$/.test(text.substring(head + 1))) {
                        var total = Number(text.substring(head + 1));
                        if (total > 0) {
                            this.streamIn = { path: start, left: total, parts: [] };
                            return;
                        }
                        text = start + "\0";
                    }
                }

                // Every answer is "<path>" then a NUL then its body. One with a
                // handler (see onMessage) goes to it - the config fills the form,
                // a feature's status fills its box; anything else - a command
                // saying how it went - is shown, not parsed.
                var nul = text.indexOf('\0');
                var path = (nul < 0) ? text : text.substring(0, nul);
                var body = (nul < 0) ? "" : text.substring(nul + 1);

                if (MESSAGE_HANDLERS[path]) {
                    MESSAGE_HANDLERS[path](body);
                    return;
                }

                hudToast("ok", (nul < 0) ? text : path + ": " + body);
            };

            reader.readAsText(event.data);
        });
    }

    // Every in flight command dies with the socket it was riding on.
    failPending(why) {
        if (!this.pending.length) return;
        var lost = this.pending.map((cmd) => cmd.label).join(", ");
        this.pending = [];
        hudToast("bad", T("toast.lost") + lost + " (" + why + ")");
        this.flashState("fail", T("err.neverLanded"), lost + T("err.diedOnWay") + why);
    }

    scheduleReconnect(reason) {
        this.retries += 1;
        this.reason = reason;

        // Every 30s forever. The RE-SUMMON button is there for the impatient.
        var delay = 30000;
        this.retryAt = Date.now() + delay;

        if (this.retries <= 3) {
            hudToast("bad", reason + T("toast.retry"));
        }
        if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, delay);

        this.render();
    }

    // Wired to the RE-SUMMON button.
    reconnectNow() {
        this.retries = 0;
        this.reason = "";
        this.flash = null;
        hudToast("info", T("toast.reconnect"));
        this.connect();
    }

    flashState(cls, title, detail) {
        this.flash = { cls: cls, title: title, detail: detail, until: Date.now() + 3000 };
    }

    // quiet: housekeeping traffic we do not want to toast about.
    // quiet: housekeeping traffic, neither toasted nor shown in the strip.
    //
    // lock: the command owns the page while it is in flight, so every button
    // fades and the cursor turns - right for a firmware image or a config
    // write, wrong for something sent several times a second while a slider
    // is dragged. Left out, it locks.
    send(data, label, quiet, lock) {
        label = label || "Command";

        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
            hudToast("bad", label + T("toast.noLink"));
            this.flashState("fail", T("err.neverLeft"), label + T("err.noWire"));
            return false;
        }

        var size = (data instanceof Blob) ? data.size : new Blob([data]).size;

        try {
            this.ws.send(data);
        } catch (e) {
            console.log("WebSocket send failed: ", e);
            hudToast("bad", label + T("toast.threw") + e);
            this.flashState("fail", T("err.neverLeft"), String(e));
            return false;
        }

        this.queuedBytes += size;
        this.pending.push({ label: label, size: size, endOffset: this.queuedBytes, quiet: quiet, lock: (lock !== false) });
        this.render();
        return true;
    }

    periodic() {
        if (this.ws && this.pending.length) {
            // Bytes the browser has actually pushed out since we connected.
            var flushed = this.queuedBytes - this.ws.bufferedAmount;
            while (this.pending.length && flushed >= this.pending[0].endOffset) {
                var done = this.pending.shift();
                if (done.quiet) continue;
                hudToast("ok", done.label + T("toast.sent") + fmtBytes(done.size) + ")");
                this.flashState("ok", T("err.delivered"), done.label + ", " + fmtBytes(done.size) + T("err.allTheWayOut"));
            }
        }
        this.render();
    }

    render() {
        var now = Date.now();
        var state = this.ws ? this.ws.readyState : WebSocket.CLOSED;
        var cls, title, detail;

        // Quiet traffic is answered faster than the eye follows, so it stays
        // out of the strip: it would only flicker between sending and online.
        var loud = this.pending.filter(function (cmd) { return !cmd.quiet; });

        if (loud.length && this.ws) {
            var head = loud[0];
            var flushed = this.queuedBytes - this.ws.bufferedAmount;
            var intoHead = Math.max(0, Math.min(head.size, flushed - (head.endOffset - head.size)));

            var fraction = head.size ? intoHead / head.size : 1;

            cls = "busy";
            title = T("rt.sending");
            detail = head.label + T("rt.sendSep") + Math.round(fraction * 100) + "% of " + fmtBytes(head.size) + T("rt.sendTail");
            if (loud.length > 1) {
                detail += " (+" + (loud.length - 1) + " queued)";
            }
            hudProgress(fraction);
        }
        else if (this.flash && now < this.flash.until) {
            cls = this.flash.cls;
            title = this.flash.title;
            detail = this.flash.detail;
        }
        else if (state === WebSocket.OPEN) {
            cls = "online";
            title = T("rt.online");
            detail = T("rt.onlineDetail");
        }
        else if (state === WebSocket.CONNECTING) {
            cls = "connecting";
            title = T("rt.connecting");
            detail = this.retries ? T("rt.attempt").replace("%n", this.retries + 1) : T("rt.attemptFirst");
        }
        else {
            cls = "offline";
            title = T("rt.offline");
            detail = (this.reason || T("rt.noReason")) + T("rt.sep") + this.retries + T("rt.attempts") + (this.retries == 1 ? "" : "s");
            if (this.retryAt > now) {
                detail += T("rt.retryIn").replace("%n", Math.ceil((this.retryAt - now) / 1000));
            }
        }

        hudSet(cls, title, detail);
        if (document.body) {
            document.body.classList.toggle("ws-busy", this.pending.some(function (cmd) { return cmd.lock; }));
        }
    }
}

let handler = new WebSocketHandler();

// A command with no body, as the board names it: sendCommand("push/1").
// label is what the HUD and the toasts call it.
function sendCommand(command, label) {
    return handler.send(new Blob(["/rc/" + command]), label || command);
}

function changeTab(evt, tabName) {
    var i, tabcontent, tablinks;
    tabcontent = document.getElementsByClassName("tabcontent");
    for (i = 0; i < tabcontent.length; i++) {
        tabcontent[i].style.display = "none";
    }
    tablinks = document.querySelectorAll(".tablinks:not(.subtab)");
    for (i = 0; i < tablinks.length; i++) {
        tablinks[i].className = tablinks[i].className.replace(" active", "");
    }
    document.getElementById(tabName).style.display = "block";
    evt.currentTarget.className += " active";
    // The sections share the tabs' tray, but only Config has sections.
    document.getElementById("config_subtabs").style.display = (tabName == "Config") ? "" : "none";
}

// Sections of the config tab. Only what is shown changes: every field is
// still sent by Bind, whichever section is open. The open one is remembered
// per browser.
function changeSubTab(evt, panel) {
    showSubTab(panel);
    (SUBTAB_HOOKS[panel] || []).forEach((fn) => fn());
    try { localStorage.setItem("configSubTab", panel); } catch (e) {}
}

function showSubTab(panel) {
    if (!document.getElementById(panel)) return;
    var panels = document.getElementsByClassName("subtabcontent");
    for (var i = 0; i < panels.length; i++) {
        panels[i].style.display = (panels[i].id == panel) ? "block" : "none";
    }
    var buttons = document.querySelectorAll(".tablinks.subtab");
    for (var i = 0; i < buttons.length; i++) {
        var on = buttons[i].getAttribute("onclick").indexOf("'" + panel + "'") >= 0;
        buttons[i].classList.toggle("active", on);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    var panel = null;
    try { panel = localStorage.getItem("configSubTab"); } catch (e) {}
    if (panel) showSubTab(panel);
});

// --- Config string (v3) -----------------------------------------------------
// What the server's Device Config exports (devicepage server/device_export.go), base64url
// without padding of:
//   u8 version 3 | 16 uuid | u8 key length (16|32) + log key | u16 MQTT port | u16 log port
//   | u8 flags (bit 0 TLS) | u8 length + password | u8 length + host | u32 CRC-32 of the rest
// (big-endian). A string that doesn't check out fills nothing in; the line under the field
// says why. A whole setup link (…#cfg=<string>) pasted in works too.
var CFG_VERSION = 3;
var CFG_MAX_STR = 63;   // MAX_CONFIG_STR less its NUL (app_config.h)
var cfgCrcTable = null;

function cfgCrc32(bytes, end) {
    if (!cfgCrcTable) {
        cfgCrcTable = [];
        for (var n = 0; n < 256; n++) {
            var c = n;
            for (var k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
            cfgCrcTable[n] = c >>> 0;
        }
    }
    var crc = 0xFFFFFFFF;
    for (var i = 0; i < end; i++) crc = cfgCrcTable[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
    return (crc ^ 0xFFFFFFFF) >>> 0;
}

function cfgHex(bytes) {
    var out = "";
    for (var i = 0; i < bytes.length; i++) out += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
    return out;
}

// base64url (or plain base64) to bytes; null unless it is exactly what an encoder writes,
// so a changed last character can't hide in its unused bits.
function cfgBytes(text) {
    var s = String(text).replace(/\s+/g, "");
    var at = s.indexOf("#cfg=");
    if (at >= 0) s = s.substring(at + 5);
    s = s.replace(/-/g, "+").replace(/_/g, "/").replace(/=+$/, "");
    if (!/^[A-Za-z0-9+\/]*$/.test(s) || (s.length % 4) === 1) return null;
    var padded = s;
    while (padded.length % 4) padded += "=";
    var bin;
    try { bin = atob(padded); } catch (e) { return null; }
    if (btoa(bin).replace(/=+$/, "") !== s) return null;
    var bytes = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return bytes;
}

// null for an empty field, {error: reason} or the settings.
function parseServerConfig(text) {
    if (!String(text || "").trim()) return null;
    var b = cfgBytes(text);
    if (!b) return {error: "b64"};
    if (b.length < 1 || b[0] !== CFG_VERSION) return {error: "version"};
    var end = b.length - 4, p = 1;
    function take(n) {
        if (p + n > end) throw "short";
        var v = b.subarray(p, p + n);
        p += n;
        return v;
    }
    function str() {
        var v = take(take(1)[0]);
        if (!v.length || v.length > CFG_MAX_STR) throw "field";
        return new TextDecoder("utf-8", {fatal: true}).decode(v);
    }
    var cfg = {};
    try {
        var id = cfgHex(take(16));
        cfg.uuid = id.substr(0, 8) + "-" + id.substr(8, 4) + "-" + id.substr(12, 4) + "-" + id.substr(16, 4) + "-" + id.substr(20);
        var keyLen = take(1)[0];
        if (keyLen !== 16 && keyLen !== 32) throw "key";
        cfg.logkey = cfgHex(take(keyLen));
        var nums = take(5);
        cfg.port = (nums[0] << 8) | nums[1];
        cfg.logport = (nums[2] << 8) | nums[3];
        if (!cfg.port || !cfg.logport || (nums[4] & ~1)) throw "field";
        cfg.tls = (nums[4] & 1) === 1;
        cfg.pass = str();
        cfg.host = str();
        if (p !== end) throw "long";
        var crc = ((b[end] << 24) | (b[end + 1] << 16) | (b[end + 2] << 8) | b[end + 3]) >>> 0;
        if (crc !== cfgCrc32(b, end)) throw "crc";
    } catch (e) {
        return {error: (typeof e === "string") ? e : "field"};   // TextDecoder throws on bad UTF-8
    }
    return cfg;
}

// The fields as a config string, or "" while one of them can't go in one (a password the
// board holds is not on the page, for instance).
function buildServerConfig() {
    var uuid = document.getElementById("uuid").value.trim().toLowerCase();
    var key = document.getElementById("logkey").value.trim().toLowerCase();
    var port = document.getElementById("port").value.trim();
    var logport = document.getElementById("logport").value.trim();
    var enc = new TextEncoder();
    var pass = enc.encode(document.getElementById("pass").value.trim());
    var host = enc.encode(document.getElementById("host").value.trim());
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(uuid)) return "";
    if (!/^([0-9a-f]{32}|[0-9a-f]{64})$/.test(key)) return "";
    if (!/^\d{1,5}$/.test(port) || !/^\d{1,5}$/.test(logport)) return "";
    port = Number(port);
    logport = Number(logport);
    if (port < 1 || port > 65535 || logport < 1 || logport > 65535) return "";
    if (!pass.length || pass.length > CFG_MAX_STR || !host.length || host.length > CFG_MAX_STR) return "";

    var b = [CFG_VERSION];
    var hex = uuid.replace(/-/g, "");
    for (var i = 0; i < 32; i += 2) b.push(parseInt(hex.substr(i, 2), 16));
    b.push(key.length / 2);
    for (var i = 0; i < key.length; i += 2) b.push(parseInt(key.substr(i, 2), 16));
    b.push(port >> 8, port & 0xFF, logport >> 8, logport & 0xFF);
    b.push(document.getElementById("tls").checked ? 1 : 0);
    b.push(pass.length);
    for (var i = 0; i < pass.length; i++) b.push(pass[i]);
    b.push(host.length);
    for (var i = 0; i < host.length; i++) b.push(host[i]);
    var crc = cfgCrc32(b, b.length);
    b.push(crc >>> 24, (crc >>> 16) & 0xFF, (crc >>> 8) & 0xFF, crc & 0xFF);
    return btoa(String.fromCharCode.apply(null, b)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

// The line under the field: valid (what it holds) or not (why).
function showServerConfigStatus(cfg) {
    var el = document.getElementById("server_config_status");
    if (!el) return;
    el.className = "cfg-status";
    if (!cfg) {
        el.textContent = "";
        return;
    }
    if (cfg.error) {
        el.className += " bad";
        el.textContent = "✗ " + T("cfgck.bad") + ": " + T("cfgck.e." + cfg.error);
        return;
    }
    el.className += " ok";
    var text = "✓ " + T("cfgck.ok") + " " + cfg.uuid.substring(0, 8) + "… — " +
               cfg.host + ":" + cfg.port + (cfg.tls ? " (TLS)" : "") + ", " +
               T("cfgck.logport") + " " + cfg.logport + ", " + (cfg.logkey.length * 4) + T("cfgck.keybits");
    var mqtt = (lastConfig && lastConfig.mqtt) || {};
    var current = cleanConfigValue(mqtt.user).toLowerCase();
    if (current && current !== cfg.uuid) text += "\n⚠ " + T("cfgck.other");
    el.textContent = text;
}

function encodeServerConfig() {
    var field = document.getElementById("server_config");
    field.value = buildServerConfig();
    showServerConfigStatus(parseServerConfig(field.value));
}

function decodeServerConfig() {
    var cfg = parseServerConfig(document.getElementById("server_config").value);
    showServerConfigStatus(cfg);
    if (!cfg || cfg.error) return;

    document.getElementById("host").value = cfg.host;
    document.getElementById("port").value = cfg.port;
    document.getElementById("tls").checked = cfg.tls;
    document.getElementById("uuid").value = cfg.uuid;
    typeSecret("pass", cfg.pass);
    document.getElementById("logport").value = cfg.logport;
    typeSecret("logkey", cfg.logkey);

    syncLocalOnly();
}

// The QR code on the server's Device Config opens http://<board>/#cfg=<string>. It holds the
// broker password and log key, so it leaves the address bar at once. It is filled in when
// the page has loaded, and again once the board's own settings arrive, since they overwrite
// the fields.
var linkConfig = null;
(function () {
    var m = /(?:^#|&)cfg=([^&]*)/.exec(location.hash);
    if (!m) return;
    linkConfig = m[1];
    try { history.replaceState(null, "", location.pathname + location.search); } catch (e) {}
})();

function applyLinkConfig(final) {
    if (!linkConfig) return;
    document.getElementById("server_config").value = linkConfig;
    decodeServerConfig();
    showSubTab("sub_server");
    if (!final) return;
    var cfg = parseServerConfig(linkConfig);
    var good = cfg && !cfg.error;
    hudToast(good ? "ok" : "bad", good ? T("cfgck.link") : T("cfgck.bad"));
    linkConfig = null;
}
                    
// The settings as the board last sent them. sendConfig() starts from these,
// so sections this page does not show - set from elsewhere, like the debug
// log size - are kept. Their secrets come back as true, which keeps them.
var lastConfig = {};

// A number field, or nothing when it is empty so the board keeps its default.
function numberValue(id) {
    var value = document.getElementById(id).value.trim();
    return (value === "") ? undefined : Number(value);
}

// --- Settings <-> fields ----------------------------------------------------
// Each field holding a setting says where it lives in the config JSON:
// data-config="telegram.chat". Loading fills every such field from the board's
// answer and Bind writes every one back over lastConfig, so a feature only has
// to put its fields in the page; leaving a feature out leaves its settings on
// the board as they are.
//
//   in a .secret-row     a secret: comes back as true/false, sent with secretValue()
//   type="checkbox"      true/false; with no value from the board, as the page has it checked
//   data-type="number"   sent as a number, or left out when empty (the board keeps its default)
//   data-trim            sent trimmed
//   data-default="1883"  shown when the board has no value
//   <select>             a value it does not list falls back to its selected option

function configGet(config, path) {
    return path.split(".").reduce((obj, key) => (obj && typeof obj === "object") ? obj[key] : undefined, config);
}

function configSet(config, path, value) {
    var keys = path.split(".");
    var obj = config;
    for (var i = 0; i < keys.length - 1; i++) {
        if (!obj[keys[i]] || typeof obj[keys[i]] !== "object") obj[keys[i]] = {};
        obj = obj[keys[i]];
    }
    obj[keys[keys.length - 1]] = value;
}

function isSecretField(input) {
    return !!input.closest(".secret-row");
}

function fieldDefault(input) {
    if (input.dataset.default !== undefined) return input.dataset.default;
    if (input.tagName === "SELECT") {
        var option = input.querySelector("option[selected]") || input.options[0];
        return option ? option.value : "";
    }
    return "";
}

function loadField(input, value) {
    if (isSecretField(input)) {
        showSecret(input.id, value === true);
    } else if (input.type === "checkbox") {
        input.checked = (value === undefined) ? input.defaultChecked : (value === true);
    } else {
        input.value = (value === undefined || value === null || value === "") ? fieldDefault(input) : value;
        if (input.tagName === "SELECT" && input.selectedIndex < 0) input.value = fieldDefault(input);
    }
}

function fieldValue(input) {
    if (isSecretField(input)) return secretValue(input.id);
    if (input.type === "checkbox") return input.checked;
    if (input.dataset.type === "number") return numberValue(input.id);
    return (input.dataset.trim !== undefined) ? input.value.trim() : input.value;
}

// The settings as one JSON document. Secrets come back only as true (one is
// saved) or false, never the value.
onMessage("/rc/config/read", (body) => {
    var config = {};
    try {
        config = JSON.parse(body);
    } catch (e) {
        hudToast("bad", "config: " + e);
        return;
    }
    console.log(config);
    lastConfig = config;

    document.querySelectorAll("[data-config]").forEach((input) => {
        loadField(input, configGet(config, input.dataset.config));
    });
    CONFIG_LOADED_HOOKS.forEach((fn) => fn(config));
    applyLinkConfig(true);
});

// Every setting in one JSON document. The board stores it as it is, keeping
// a secret left empty and clearing one sent as null.
async function sendConfig() {
    var config = JSON.parse(JSON.stringify(lastConfig));

    document.querySelectorAll("[data-config]").forEach((input) => {
        configSet(config, input.dataset.config, fieldValue(input));
    });

    for (var i = 0; i < CONFIG_SAVE_HOOKS.length; i++) {
        if (CONFIG_SAVE_HOOKS[i](config) === false) return;
    }

    if (handler.send(new Blob(["/rc/config/write", '\0', JSON.stringify(config)]), T("lbl.rewire"))) {
        CONFIG_SENT_HOOKS.forEach((fn) => fn(config));
    }
}

// --- Secrets -------------------------------------------------------------
// The device never sends a saved password or token back, only whether it
// holds one. The field stays empty: leave it empty to keep the saved one, type
// a new one to replace it, or press Forget to remove it on save. So every other
// field here can be changed and bound without the page ever holding them.

function showSecret(id, saved) {
    var input = document.getElementById(id);
    input.value = "";
    input.dataset.saved = saved ? "1" : "";
    input.dataset.forget = "";
    secretPlaceholder(input);
    document.getElementById(id + "_forget").style.display = saved ? "" : "none";
}

function forgetSecret(id) {
    var input = document.getElementById(id);
    input.value = "";
    // Nothing stored means nothing to forget: the field just goes back to empty.
    input.dataset.forget = input.dataset.saved ? "1" : "";
    secretPlaceholder(input);
    document.getElementById(id + "_forget").style.display = "none";
}

// A pasted glyph can carry a password. It counts as one just typed in, so it
// replaces what the device holds and cancels a Forget waiting to be bound.
function typeSecret(id, value) {
    var input = document.getElementById(id);
    input.value = value;
    input.dataset.forget = "";
    secretPlaceholder(input);
    document.getElementById(id + "_forget").style.display = input.dataset.saved ? "" : "none";
}

function secretValue(id) {
    var input = document.getElementById(id);
    var value = input.value.trim();
    if (value) return value;
    return (input.dataset.forget == "1") ? null : "";
}

function secretPlaceholder(input) {
    if (input.dataset.forget == "1") input.placeholder = T("cfg.forgetting");
    else if (input.dataset.saved) input.placeholder = T("cfg.secretKept");
    else input.placeholder = input.dataset.hint;
}

// The placeholders are written out in whichever skin was current at the time,
// so a skin change has to say them again.
function refreshSecretPlaceholders() {
    document.querySelectorAll(".secret-row input[type=text]").forEach(secretPlaceholder);
}

async function sendReboot() {
    var blob = new Blob(["/dbg/reboot"]);
    handler.send(blob, T("lbl.reboot"));
}

async function formatFilesystem() {
    var blob = new Blob(["/dbg/wipefilesystem"]);
    handler.send(blob, T("lbl.wipe"));
}


// Values can arrive as String objects from the config parser (always truthy, even
// when empty) or padded with spaces/NULs by the firmware, so scrub before testing.
function cleanConfigValue(value) {
    if (value === null || value === undefined) return "";
    return String(value).replace(/[\s\0]+/g, "");
}

function isLocalOnlyConfig(host, port) {
    var h = cleanConfigValue(host);
    var p = cleanConfigValue(port);
    return !h || !p || Number(p) === 0;
}

// Re-derive the Local Only checkbox from whatever host/port currently hold.
function syncLocalOnly() {
    var localOnly = isLocalOnlyConfig(document.getElementById("host").value,
                                      document.getElementById("port").value);
    document.getElementById("local_only").checked = localOnly;
    updateLocalOnlyVisibility();
}

function updateLocalOnlyVisibility() {
    var localOnly = document.getElementById("local_only").checked;
    document.getElementById("server_config_section").style.display = localOnly ? "none" : "block";
}

function toggleLocalOnly() {
    if (document.getElementById("local_only").checked) {
        document.getElementById("host").value = "";
        document.getElementById("port").value = "";
        document.getElementById("tls").checked = false;
        document.getElementById("uuid").value = "";
        forgetSecret("pass");
        document.getElementById("logport").value = "";
        forgetSecret("logkey");
        document.getElementById("server_config").value = "";
        showServerConfigStatus(null);
    }
    updateLocalOnlyVisibility();
}

document.addEventListener('DOMContentLoaded', () => {
    // The first tab of the row opens (tabs: order: in page.yaml), Config unless
    // the board put another first.
    var first = document.querySelector(".tab-row .tablinks.active:not(.subtab)");
    var tab = first ? first.dataset.tab : "Config";
    document.getElementById(tab).style.display = "block";
    document.getElementById("config_subtabs").style.display = (tab == "Config") ? "" : "none";
    syncLocalOnly();
    applyLinkConfig(false);
});

// The board's settings just filled the fields: the warnings and sections that
// depend on them follow.
onConfigLoaded(updateWifiAuthWarn);
onConfigLoaded(syncLocalOnly);
