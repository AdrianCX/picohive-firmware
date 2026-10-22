/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os_platform.h"

#include "app_config.h"
#include "config_store.h"
#include "pico_mjson.h"
#include "pico_logger.h"
#include "telegram_manager.h"
#include "upgrade_helper.h"

const char *TelegramManager::TELEGRAM_HOST = "api.telegram.org";
const char *TelegramManager::USER_AGENT = "PicoW custom agent - github.com/AdrianCX/picohive-firmware";

bool SendMessageLog::onHeaderReceived(HTTPHeader& header)
{
	code = header.getResponseCode();
	return true;
}

bool SendMessageLog::onHttpData(u8_t *data, size_t len)
{
	if (code != 200)
	{
		uint32_t room = MAX_ERROR - 1 - error_len;
		uint32_t bytes = (len < room) ? len : room;
		
		memcpy(&error[error_len], data, bytes);
		error_len += bytes;
	}
	return true;
}

void SendMessageLog::onRequestDestroyed()
{
	if (code == 200)
	{
		trace("telegram: sendMessage delivered, %u bytes", (unsigned int)message_len);
	}
	else if (code == 0)
	{
		trace("telegram: sendMessage got no answer - connection failed or closed, %u bytes", (unsigned int)message_len);
	}
	else
	{
		trace("telegram: sendMessage refused %d: %s", code, error);
	}
	
	delete this;
}

TelegramManager::TelegramManager()
{
	m_client.setHttpCallback(this);
	m_client.setKeepAlive(TELEGRAM_KEEPALIVE_IDLE_S * 1000, TELEGRAM_KEEPALIVE_INTERVAL_S * 1000, TELEGRAM_KEEPALIVE_COUNT);
}

TelegramManager& TelegramManager::get()
{
    static TelegramManager instance;
    return instance;
}

void TelegramManager::load_config(const JsonView& config)
{
    bool claim = false;
    config.get_bool("$.telegram.claim", claim);

    if (config.same_string("$.telegram.token", m_config.bot_id)
		&& config.same_string("$.telegram.chat", m_config.chat_id)
        && (claim == m_config.claim)
		&& config.same_string("$.telegram.claim_code", m_config.claim_code))
    {
        trace("TelegramManager::load_config: this=%p, unchanged", this);
        return;
    }

    config.get_whole_string("$.telegram.token", m_config.bot_id, sizeof(m_config.bot_id));
    config.get_whole_string("$.telegram.chat", m_config.chat_id, sizeof(m_config.chat_id));
    config.get_whole_string("$.telegram.claim_code", m_config.claim_code, sizeof(m_config.claim_code));
    m_config.claim = claim;

    m_offset = 0;
    m_reply = SharedBuffer();
    m_message = SharedBuffer();
    m_answer_id[0] = 0;

    m_keyboard_queued = false;
    m_save_chat = false;

    trace("TelegramManager::load_config: this=%p, bot_token[%s] chat_id[%s] claim[%s] new config", this, (m_config.bot_id[0] != 0) ? "*" : "", m_config.chat_id, claiming() ? "waiting for /start <code>" : "no");
}

bool TelegramManager::is_configured() const
{
    return (m_config.bot_id[0] != 0) && (m_config.chat_id[0] != 0);
}

bool TelegramManager::claiming() const
{
    return (m_config.bot_id[0] != 0) && (m_config.chat_id[0] == 0) && m_config.claim && (m_config.claim_code[0] != 0);
}

bool TelegramManager::try_claim(const TelegramUpdate& update)
{
    if (!claiming() || (update.chat_id[0] == 0))
    {
        return false;
    }

    std::string_view command;
    std::string_view code;
    telegram_split_command(update.text, command, code);

    if (command != "/start")
    {
        return false;
    }

    while (!code.empty() && ((code.back() == ' ') || (code.back() == '\n') || (code.back() == '\r') || (code.back() == '\t')))
    {
        code.remove_suffix(1);
    }

    if (code != m_config.claim_code)
    {
        trace("telegram: /start from chat %s without the claim code, ignored", update.chat_id);
        return false;
    }

    snprintf(m_config.chat_id, sizeof(m_config.chat_id), "%s", update.chat_id);
    m_save_chat = true;

    trace("telegram: chat %s claimed the board", m_config.chat_id);

    const char *claimed = "This chat now controls the board.";
    queue_reply(claimed, strlen(claimed));
    return true;
}

bool TelegramManager::chat_allowed(const char *chat_id) const
{
    return (chat_id != NULL) && (chat_id[0] != 0) && (strncmp(chat_id, m_config.chat_id, MAX_TELEGRAM_CHATID_SIZE) == 0);
}

void TelegramManager::sendMessage(const char *message)
{
    if (!is_configured() || (message == NULL))
    {
        return;
    }

    SharedBuffer text(MAX_TELEGRAM_BODY - 1);
    if (!text)
    {
        trace("telegram: no memory for a request body");
        return;
    }

    JsonWriter body(text.data(), MAX_TELEGRAM_BODY);
    body.begin_object()
            .add_string("chat_id", m_config.chat_id)
            .add_string("text", message)
        .end_object();

    if (!body.ok())
    {
        trace("telegram: sendMessage dropped, it does not fit %d bytes", MAX_TELEGRAM_BODY);
        return;
    }

    text.set_size(body.length());
    send_message(text);
}

void TelegramManager::sendFormattedMessage(const char *format, ...)
{
    if (!is_configured() || (format == NULL))
    {
        return;
    }

    SharedBuffer text(MAX_TELEGRAM_BODY - 1);
    if (!text)
    {
        trace("telegram: no memory for a request body");
        return;
    }

    JsonWriter body(text.data(), MAX_TELEGRAM_BODY);

    va_list args;
    va_start(args, format);
    body.begin_object()
            .add_string("chat_id", m_config.chat_id)
            .add_vstring("text", format, args)
        .end_object();
    va_end(args);

    if (!body.ok())
    {
        trace("telegram: sendMessage dropped, it does not fit %d bytes", MAX_TELEGRAM_BODY);
        return;
    }

    text.set_size(body.length());
    send_message(text);
}

void TelegramManager::send_message(const SharedBuffer& body)
{
    if ((m_active == Request::Poll) || m_message)
    {
        send_aside(body);
        return;
    }

    m_message = body;
}

void TelegramManager::send_aside(const SharedBuffer& body)
{
    HTTPRequest *request = HTTPRequest::create(TELEGRAM_HOST, 443, true, "POST", "/bot%s/sendMessage", m_config.bot_id);

    request->addHeader("User-Agent", USER_AGENT);
    request->addHeader("Content-Type", "application/json");
    request->setBody((u8_t *)body.c_str(), body.size());

    SendMessageLog *log = new SendMessageLog(body.size());
    request->setHttpCallback(log);

    trace("telegram: sendMessage on a connection of its own, %u bytes: %.120s%s", (unsigned int)body.size(), body.c_str(), (body.size() > 120) ? "..." : "");

    request->send();
}

void TelegramManager::set_keyboard(const char *title, const TelegramButton *buttons, uint32_t count, uint32_t columns)
{
    m_title = title;

    m_has_commands = (buttons != NULL) && (count > 0);

    m_buttons = m_has_commands ? buttons : NULL;
    m_button_count = m_has_commands ? count : 0;
    m_columns = (columns > 0) ? columns : 1;

    if (!m_has_commands)
    {
        m_keyboard_queued = false;
        trace("telegram: no keyboard, notifications only - not polling for updates unless claiming a chat");
    }
}

bool TelegramManager::send_keyboard()
{
    if ((m_buttons == NULL) || (m_button_count == 0))
    {
        return false;
    }

    m_keyboard_queued = true;
    return true;
}

bool TelegramManager::queue_reply(const char *text, uint32_t len, bool new_line)
{
    if ((text == NULL) || (len == 0))
    {
        return true;
    }

    if (!m_reply)
    {
        m_reply = SharedBuffer(MAX_TELEGRAM_REPLY - 1);

        if (!m_reply)
        {
            return false;
        }
    }

    if (new_line && !m_reply.empty())
    {
        m_reply.append("\n");
    }

    uint32_t size = m_reply.size();
    uint32_t room = m_reply.capacity() - size;
    uint32_t bytes = (len < room) ? len : room;

    if (bytes > 0)
    {
        memcpy(m_reply.data() + size, text, bytes);
        m_reply.set_size(size + bytes);
    }

    return bytes == len;
}

void TelegramManager::queue_callback_answer(const char *callback_id)
{
    if ((callback_id == NULL) || (callback_id[0] == 0))
    {
        return;
    }

    snprintf(m_answer_id, sizeof(m_answer_id), "%s", callback_id);
}

void TelegramManager::update(uint64_t now_us)
{
    m_now_us = now_us;

    if (!is_configured() && !claiming())
    {
        return;
    }

    m_client.update();
    check_commands(now_us);

    if (m_save_chat)
    {
        m_save_chat = false;

        StoredConfig stored;

        if (!stored.set_field("telegram", "chat", m_config.chat_id) || !stored.save())
        {
            trace("telegram: claimed chat %s could not be saved, it is lost on reboot", m_config.chat_id);
        }
    }

    if (m_active != Request::None)
    {
        if ((now_us - m_active_since_us) > STALL_US)
        {
            trace("telegram: request %d has been open for %llus, dropping the connection", (int)m_active, (unsigned long long)((now_us - m_active_since_us) / 1000000));
            m_client.close();
        }

        if ((m_active == Request::Poll) && !m_reply.empty() && commands_done())
        {
            SharedBuffer body = reply_body();

            if (body)
            {
                send_aside(body);
            }
        }

        return;
    }

    if (UpgradeHelper::isUpgrading())
    {
        return;
    }

    if (m_answer_id[0] != 0)
    {
        send_answer();
    }
    else if (m_message)
    {
        SharedBuffer body = m_message;
        m_message = SharedBuffer();

        trace("telegram: sendMessage, %u bytes: %.120s%s", (unsigned int)body.size(), body.c_str(), (body.size() > 120) ? "..." : "");
        request(Request::Message, "POST", body, "/bot%s/sendMessage", m_config.bot_id);
    }
    else if (!m_reply.empty() && commands_done())
    {
        send_reply();
    }
    else if (m_keyboard_queued)
    {
        send_queued_keyboard();
    }
    else if (((m_has_commands && (m_sink != NULL)) || claiming()) && commands_done() && (now_us >= m_next_poll_us))
    {
        start_poll();
    }
}

void TelegramManager::check_commands(uint64_t now_us)
{
    if (commands_done())
    {
        m_commands_since_us = 0;
        return;
    }

    if (m_commands_since_us == 0)
    {
        m_commands_since_us = now_us;
        return;
    }

    if ((now_us - m_commands_since_us) > STALL_US)
    {
        trace("telegram: a command has been running for %llus, aborting it", (unsigned long long)((now_us - m_commands_since_us) / 1000000));
        m_sink->abort("it ran too long");
        m_commands_since_us = 0;
    }
}

bool TelegramManager::start_poll()
{
    m_update = SharedBuffer();
    m_update_full = false;

    if (m_draining)
    {
        return request(Request::Poll, "GET", SharedBuffer(), "/bot%s/getUpdates?offset=-1&limit=1&timeout=0", m_config.bot_id);
    }

    return request(Request::Poll, "GET", SharedBuffer(), "/bot%s/getUpdates?offset=%u&limit=1&timeout=%u", m_config.bot_id, (unsigned int)m_offset, (unsigned int)POLL_TIMEOUT_S);
}

bool TelegramManager::send_reply()
{
    SharedBuffer body = reply_body();
    return body && request(Request::Reply, "POST", body, "/bot%s/sendMessage", m_config.bot_id);
}

SharedBuffer TelegramManager::reply_body()
{
    SharedBuffer text(MAX_TELEGRAM_BODY - 1);
    if (!text)
    {
        trace("telegram: no memory for a request body");
        return SharedBuffer();
    }

    JsonWriter body(text.data(), MAX_TELEGRAM_BODY);
    body.begin_object()
            .add_string("chat_id", m_config.chat_id)
            .add_string("text", m_reply.c_str())
        .end_object();

    m_reply = SharedBuffer();

    if (!body.ok())
    {
        trace("telegram: reply dropped, it does not fit %d bytes", MAX_TELEGRAM_BODY);
        return SharedBuffer();
    }

    text.set_size(body.length());
    return text;
}

bool TelegramManager::send_answer()
{
    SharedBuffer text(MAX_TELEGRAM_BODY - 1);
    if (!text)
    {
        trace("telegram: no memory for a request body");
        return false;
    }

    JsonWriter body(text.data(), MAX_TELEGRAM_BODY);
    body.begin_object()
            .add_string("callback_query_id", m_answer_id)
        .end_object();

    m_answer_id[0] = 0;

    text.set_size(body.length());
    return request(Request::Answer, "POST", text, "/bot%s/answerCallbackQuery", m_config.bot_id);
}

bool TelegramManager::send_queued_keyboard()
{
    m_keyboard_queued = false;

    if ((m_buttons == NULL) || (m_button_count == 0))
    {
        return false;
    }

    SharedBuffer text(MAX_TELEGRAM_BODY - 1);
    if (!text)
    {
        trace("telegram: no memory for a request body");
        return false;
    }

    JsonWriter body(text.data(), MAX_TELEGRAM_BODY);
    body.begin_object()
            .add_string("chat_id", m_config.chat_id);

    if (!write_keyboard(body))
    {
        trace("telegram: keyboard dropped, it does not fit %d bytes", MAX_TELEGRAM_BODY);
        return false;
    }

    text.set_size(body.length());
    return request(Request::Keyboard, "POST", text, "/bot%s/sendMessage", m_config.bot_id);
}

bool TelegramManager::write_keyboard(JsonWriter& body)
{
    body.add_string("text", ((m_title != NULL) && (m_title[0] != 0)) ? m_title : "Commands")
        .begin_object("reply_markup")
            .begin_array("inline_keyboard");

    uint32_t in_row = 0;

    for (uint32_t i = 0; i < m_button_count; ++i)
    {
        const TelegramButton& button = m_buttons[i];

        if ((button.text == NULL) || (button.command == NULL))
        {
            continue;
        }

        if (in_row == 0)
        {
            body.begin_array();
        }

        body.begin_object()
                .add_string("text", button.text)
                .add_string("callback_data", button.command)
            .end_object();

        if (++in_row >= m_columns)
        {
            body.end_array();
            in_row = 0;
        }
    }

    if (in_row > 0)
    {
        body.end_array();
    }

    body.end_array().end_object().end_object();

    return body.ok();
}

bool TelegramManager::request(Request kind, const char *method, const SharedBuffer& body, const char *pathFormat, ...)
{
    va_list args;
    va_start(args, pathFormat);
    bool built = m_client.vbeginf(method, pathFormat, args);
    va_end(args);

    built = built && m_client.addHeader("User-Agent", USER_AGENT);

    if (built && body)
    {
        built = m_client.addHeader("Content-Type", "application/json");
    }

    m_active = kind;
    m_active_since_us = m_now_us;
    m_code = 0;

    if (!built || !(body ? m_client.send(body) : m_client.send()))
    {
        trace("telegram: request %d failed to start", (int)kind);

        m_active = Request::None;
        m_next_poll_us = m_now_us + POLL_GAP_US;

        return false;
    }

    return true;
}

bool TelegramManager::onHeaderReceived(HTTPHeader& header)
{
    uint16_t code = header.getResponseCode();
    m_code = code;

    if (code != 200)
    {
        trace("telegram: request %d answered %d", (int)m_active, code);
    }

    if (m_active == Request::Poll)
    {
        const char *length = header.getHeaderValue("Content-Length");
        uint32_t size = (length != NULL) ? (uint32_t)strtoul(length, NULL, 10) : MAX_TELEGRAM_UPDATE_BYTES;
        m_update = SharedBuffer((size < MAX_TELEGRAM_UPDATE_BYTES) ? size : MAX_TELEGRAM_UPDATE_BYTES);
    }

    return true;
}

bool TelegramManager::onHttpData(u8_t *data, size_t len)
{
    if (m_active != Request::Poll)
    {
        return true;
    }

    if (!m_update)
    {
        m_update = SharedBuffer(MAX_TELEGRAM_UPDATE_BYTES);
    }

    uint32_t size = m_update.size();
    uint32_t room = m_update.capacity() - size;

    if (len > room)
    {
        m_update_full = true;
        len = room;
    }

    if (len > 0)
    {
        memcpy(m_update.data() + size, data, len);
        m_update.set_size(size + len);
    }

    return true;
}

void TelegramManager::onResponseComplete(bool ok)
{
    Request kind = m_active;

    m_active = Request::None;

    handle_response(kind, ok);

    m_update = SharedBuffer();
}

void TelegramManager::handle_response(Request kind, bool ok)
{
    if (!ok)
    {
        trace("telegram: request %d got no complete answer", (int)kind);
    }

    if (kind == Request::Keyboard)
    {
        if (!ok || (m_code != 200))
        {
            trace("telegram: keyboard to chat %s not sent (%d)", m_config.chat_id, m_code);
        }
        return;
    }

    if (kind != Request::Poll)
    {
        return;
    }

    if (!ok && !m_update_full)
    {
        m_next_poll_us = to_us_since_boot(get_absolute_time()) + POLL_GAP_US;
        return;
    }

    handle_poll_answer();
}

void TelegramManager::handle_poll_answer()
{
    m_next_poll_us = to_us_since_boot(get_absolute_time()) + POLL_GAP_US;

    bool draining = m_draining;
    m_draining = false;

    if (m_update_full)
    {
        trace("telegram: an update did not fit %d bytes", MAX_TELEGRAM_UPDATE_BYTES);
        ++m_bad_answers;
    }
    else if (m_update.empty())
    {
        ++m_bad_answers;
    }
    else
    {
        bool claim = claiming() && !draining;

        ITelegramUpdates *sink = (draining || claim) ? NULL : m_sink;
        uint32_t highest = 0;

        TelegramUpdate decoded;

        if (decode_telegram_updates(m_update.c_str(), m_update.size(), sink, highest, decoded))
        {
            if (claim && try_claim(decoded) && (m_sink != NULL))
            {
                m_sink->on_telegram_update(decoded);
            }

            m_bad_answers = 0;
            if (highest > 0)
            {
                m_offset = highest + 1;
            }
        }
        else
        {
            ++m_bad_answers;
        }
    }

    if (m_bad_answers >= MAX_BAD_ANSWERS)
    {
        trace("telegram: %u answers in a row were no use, asking where to carry on from", (unsigned int)m_bad_answers);

        m_bad_answers = 0;
        m_draining = true;
    }
}
