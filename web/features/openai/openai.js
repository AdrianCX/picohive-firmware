// --- Asking OpenAI ---------------------------------------------------------
// The answer can take a minute, and the board cannot push it to the page,
// so ai/status is asked every two seconds until it says it is done.
var AI_POLL_MS = 2000;
var aiPolling = false;

function aiAsk() {
    var text = document.getElementById("openai_request").value.trim();
    if (!text) return;
    document.getElementById("openai_status").textContent = "\u2026";
    if (handler.send(new Blob(["/rc/ai", '\0', text]), T("cfg.aiAsk"))) {
        aiPolling = true;
        setTimeout(aiPoll, AI_POLL_MS);
    }
}

function aiStop() {
    handler.send(new Blob(["/rc/ai/stop"]), "ai/stop", true);
}

function aiPoll() {
    if (aiPolling) handler.send(new Blob(["/rc/ai/status"]), "ai/status", true);
}

onMessage("/rc/ai/status", showAiStatus);

function showAiStatus(text) {
    document.getElementById("openai_status").textContent = text;
    aiPolling = !/^(idle|done)/.test(text);
    if (aiPolling) setTimeout(aiPoll, AI_POLL_MS);
}

// --- Hold to talk --------------------------------------------------------
// The browser's own speech recognition: Chrome sends the audio to Google,
// Safari to Apple. Browsers only give it a microphone on a secure page, so on
// http the row points at the https one instead.
//
// One phrase at a time, started again while the button is still held:
// Android's Chrome in continuous mode hands every partial guess over as a
// result of its own, and joining those wrote "grabgrab thegrab the...".
var aiSpeech = null;
var aiHolding = false;
var aiPhrases = [];     // finished phrases
var aiPartial = "";     // the one being heard

function aiTalkRemember() {
    try { localStorage.setItem("aiTalkSend", document.getElementById("openai_talk_send").checked ? "1" : "0"); } catch (e) {}
}

function aiHeardText() {
    return aiPhrases.concat(aiPartial ? [aiPartial] : []).join(" ").replace(/\s+/g, " ").trim();
}

document.addEventListener("DOMContentLoaded", () => {
    var Recognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    var btn = document.getElementById("openai_talk");
    if (!btn || !Recognition) return;

    if (!window.isSecureContext) {
        var link = document.getElementById("openai_talk_link");
        link.href = "https://" + location.host + location.pathname;
        link.textContent = link.href;
        document.getElementById("openai_talk_https").style.display = "";
        return;
    }

    try {
        if (localStorage.getItem("aiTalkSend") === "0") document.getElementById("openai_talk_send").checked = false;
    } catch (e) {}

    aiSpeech = new Recognition();
    aiSpeech.interimResults = true;
    aiSpeech.continuous = false;
    aiSpeech.lang = navigator.language || "en-US";

    // One phrase per session, so only the newest result counts: Android keeps
    // its stale guesses in the list ahead of it.
    aiSpeech.onresult = (event) => {
        var newest = event.results[event.results.length - 1];
        var text = newest[0].transcript.trim();

        if (newest.isFinal) {
            // A phrase that only grows the last one replaces it.
            var last = aiPhrases.length ? aiPhrases[aiPhrases.length - 1] : "";
            if (last && text.toLowerCase().startsWith(last.toLowerCase())) aiPhrases[aiPhrases.length - 1] = text;
            else if (text) aiPhrases.push(text);
            aiPartial = "";
        } else {
            aiPartial = text;
        }

        document.getElementById("openai_request").value = aiHeardText();
    };
    aiSpeech.onerror = (event) => {
        if (event.error !== "no-speech" && event.error !== "aborted") hudToast("bad", "speech: " + event.error);
    };
    // A pause ends the phrase: listen again while the button is held, and
    // only when it is let go is what was heard asked.
    aiSpeech.onend = () => {
        if (aiHolding) {
            try { aiSpeech.start(); return; } catch (e) {}
        }

        btn.classList.remove("talking");
        btn.value = T("cfg.aiTalk");

        var text = aiHeardText();
        document.getElementById("openai_request").value = text;
        if (text && document.getElementById("openai_talk_send").checked) aiAsk();
        aiPhrases = [];
        aiPartial = "";
    };

    btn.addEventListener("pointerdown", (event) => {
        event.preventDefault();
        aiPhrases = [];
        aiPartial = "";
        document.getElementById("openai_request").value = "";
        try { aiSpeech.start(); } catch (e) { return; }
        aiHolding = true;
        btn.classList.add("talking");
        btn.value = T("ai.listening");
    });
    ["pointerup", "pointercancel", "pointerleave"].forEach((name) => {
        btn.addEventListener(name, () => {
            if (!aiHolding) return;
            aiHolding = false;
            try { aiSpeech.stop(); } catch (e) {}
        });
    });
    btn.addEventListener("contextmenu", (event) => event.preventDefault());

    document.getElementById("openai_talk_row").style.display = "";
});

