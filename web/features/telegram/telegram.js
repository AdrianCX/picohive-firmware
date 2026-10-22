// --- Telegram chat picker ------------------------------------------------
// config.html has a fleet server to proxy this; the device does not, so the
// browser asks Telegram directly. api.telegram.org answers cross origin, so
// the token goes from this field to Telegram and nowhere else.

function telegramChatName(chat) {
    if (chat.title) return chat.title;
    var name = [chat.first_name, chat.last_name].filter(Boolean).join(" ");
    if (name) return name;
    return chat.username ? "@" + chat.username : String(chat.id);
}

// One chat per id, gathered from every update kind that carries one.
function telegramChats(updates) {
    var seen = {};
    var chats = [];
    (updates || []).forEach(function (u) {
        [u.message, u.edited_message, u.channel_post, u.edited_channel_post,
         u.my_chat_member, u.chat_member,
         u.callback_query && u.callback_query.message].forEach(function (carrier) {
            var chat = carrier && carrier.chat;
            if (!chat || seen[chat.id]) return;
            seen[chat.id] = true;
            chats.push({ id: chat.id, name: telegramChatName(chat), type: chat.type });
        });
    });
    return chats;
}

async function listChats() {
    var token = document.getElementById("telegram_bot_token").value.trim();
    var btn = document.getElementById("telegram_list");
    var select = document.getElementById("telegram_chats");
    var hint = document.getElementById("telegram_hint");

    if (!token) { hudToast("bad", T("tg.noToken")); return; }
    // It goes into the URL path, so anything that could climb out of it is
    // not a token: BotFather only ever issues <digits>:<letters/digits/_->.
    if (!/^[0-9]+:[A-Za-z0-9_-]+$/.test(token)) { hudToast("bad", T("tg.badToken")); return; }

    var original = btn.value;
    btn.disabled = true;
    btn.value = T("tg.loading");
    try {
        var res = await fetch("https://api.telegram.org/bot" + token + "/getUpdates");
        var body = await res.json().catch(() => ({}));
        if (!res.ok || !body.ok) throw new Error(body.description || ("HTTP " + res.status));

        var chats = telegramChats(body.result);
        select.innerHTML = "";
        hint.style.display = "block";
        if (!chats.length) {
            select.style.display = "none";
            hint.textContent = T("tg.none");
            return;
        }
        chats.forEach(function (c) {
            var opt = document.createElement("option");
            opt.value = c.id;
            opt.textContent = c.name + " (" + c.id + ")" + (c.type ? " \u00b7 " + c.type : "");
            select.appendChild(opt);
        });
        select.style.display = "block";
        hint.textContent = T("tg.found").replace("%n", chats.length);
    } catch (e) {
        hudToast("bad", T("tg.failed") + e.message);
    } finally {
        btn.disabled = false;
        btn.value = original;
    }
}

function pickChat() {
    var select = document.getElementById("telegram_chats");
    if (select.value) document.getElementById("telegram_chat_id").value = select.value;
}


// --- Claim by code ----------------------------------------------------------
// For setting up from the board's own access point, where the browser cannot
// reach Telegram to list chats: the board itself takes the first chat that
// sends "/start <code>" as the chat, once it is online.

function telegramClaimShown() {
    var on = document.getElementById("telegram_claim").checked;
    document.getElementById("telegram_claim_row").style.display = on ? "" : "none";
}

onConfigLoaded(telegramClaimShown);
