/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "pico_mjson.h"
#include "shared_buffer.h"

#include <stdint.h>

#include "http_client.h"
#include "http_request.h"
#include "telegram_keyboard.h"
#include "telegram_updates.h"

#if __has_include("general_config.h")
#include "general_config.h"
#endif

#define MAX_TELEGRAM_BOTID_SIZE 64
#define MAX_TELEGRAM_CHATID_SIZE 32
#define MAX_TELEGRAM_CLAIM_CODE_SIZE 32

#ifndef MAX_TELEGRAM_REPLY
#define MAX_TELEGRAM_REPLY 512
#endif

#ifndef MAX_TELEGRAM_UPDATE_BYTES
#define MAX_TELEGRAM_UPDATE_BYTES 4096
#endif

#ifndef MAX_TELEGRAM_BODY
#define MAX_TELEGRAM_BODY 1024
#endif

#ifndef TELEGRAM_POLL_TIMEOUT_S
#define TELEGRAM_POLL_TIMEOUT_S 50
#endif

#ifndef TELEGRAM_REQUEST_TIMEOUT_S
#define TELEGRAM_REQUEST_TIMEOUT_S 120
#endif

#ifndef TELEGRAM_KEEPALIVE_IDLE_S
#define TELEGRAM_KEEPALIVE_IDLE_S 15
#endif

#ifndef TELEGRAM_KEEPALIVE_INTERVAL_S
#define TELEGRAM_KEEPALIVE_INTERVAL_S 5
#endif

#ifndef TELEGRAM_KEEPALIVE_COUNT
#define TELEGRAM_KEEPALIVE_COUNT 4
#endif

static_assert(TELEGRAM_REQUEST_TIMEOUT_S > TELEGRAM_POLL_TIMEOUT_S, "TELEGRAM_REQUEST_TIMEOUT_S has to be longer than TELEGRAM_POLL_TIMEOUT_S");

class JsonWriter;

struct TelegramConfig
{
    char bot_id[MAX_TELEGRAM_BOTID_SIZE] = {};
    char chat_id[MAX_TELEGRAM_CHATID_SIZE] = {};
    bool claim = false;
    char claim_code[MAX_TELEGRAM_CLAIM_CODE_SIZE] = {};
};

class SendMessageLog : public IHttpCallback
{
public:
	SendMessageLog(uint32_t len) : message_len(len) {}
    bool onHeaderReceived(HTTPHeader& header) override;
    bool onHttpData(u8_t *data, size_t len) override;
    void onRequestDestroyed() override;

private:
	static const uint32_t MAX_ERROR = 256;

	uint32_t message_len = 0;
    uint16_t code = 0;
    uint32_t error_len = 0;
    char error[MAX_ERROR] = {};
};

class TelegramManager : public IHttpCallback
{
public:
    static TelegramManager& get();

    void load_config(const JsonView& config);
    const TelegramConfig& get_config() { return m_config; }

	bool is_configured() const;
    bool claiming() const;

    // Initial menu, parameters must outlive manager, NULL for no commands, if telegram is only used for notifications
    void set_keyboard(const char *title, const TelegramButton *buttons, uint32_t count, uint32_t columns = 2);
    bool send_keyboard();

	// A notification. Fire and forget, and independent of everything below.
    void sendMessage(const char *message);
    void sendFormattedMessage(const char *format, ...) __attribute__((format(printf, 2, 3)));
    bool queue_reply(const char *text, uint32_t len, bool new_line = true);
    void queue_callback_answer(const char *callback_id);

    void set_update_sink(ITelegramUpdates *sink) { m_sink = sink; }
	bool chat_allowed(const char *chat_id) const;
    bool has_commands() const { return m_has_commands; }

	void update(uint64_t now_us);

private:
    TelegramManager();

    enum class Request
    {
        None,
        Poll,
        Answer,
        Reply,
        Message,
        Keyboard,
    };

    bool onHeaderReceived(HTTPHeader& header) override;
    bool onHttpData(u8_t *data, size_t len) override;
    void onResponseComplete(bool ok) override;
    void handle_response(Request kind, bool ok);

    bool start_poll();
    void handle_poll_answer();
    bool try_claim(const TelegramUpdate& update);
    bool send_reply();
    bool send_answer();
    bool send_queued_keyboard();
    bool write_keyboard(JsonWriter& body);
    void send_message(const SharedBuffer& body);
    void send_aside(const SharedBuffer& body);
    SharedBuffer reply_body();
    bool commands_done() const { return (m_sink == nullptr) || !m_sink->busy(); }
    void check_commands(uint64_t now_us);
    bool request(Request kind, const char *method, const SharedBuffer& body, const char *pathFormat, ...) __attribute__((format(printf, 5, 6)));

    TelegramConfig m_config;

    bool m_has_commands = false;
    bool m_draining = false;
    bool m_keyboard_queued = false;
    bool m_save_chat = false;
    bool m_update_full = false;
    uint16_t m_code = 0;
    uint32_t m_button_count = 0;
    uint32_t m_columns = 2;
    uint32_t m_offset = 0;
    uint32_t m_bad_answers = 0;
    uint64_t m_active_since_us = 0;
    uint64_t m_now_us = 0;
    uint64_t m_next_poll_us = 0;
    uint64_t m_commands_since_us = 0;
    char m_answer_id[MAX_TELEGRAM_CALLBACK_ID] = {};

    Request m_active = Request::None;
	ITelegramUpdates *m_sink = nullptr;
    const TelegramButton *m_buttons = nullptr;
    const char *m_title = nullptr;

    SharedBuffer m_reply;
    SharedBuffer m_update;
    SharedBuffer m_message;
    HTTPClient m_client{TELEGRAM_HOST, 443, true};

    static const char *TELEGRAM_HOST;
    static const char *USER_AGENT;
    static const uint32_t POLL_TIMEOUT_S = TELEGRAM_POLL_TIMEOUT_S;
    static const uint64_t POLL_GAP_US = 1000000;
    static const uint64_t STALL_US = TELEGRAM_REQUEST_TIMEOUT_S * 1000000ULL;
    static const uint32_t MAX_BAD_ANSWERS = 3;
};
