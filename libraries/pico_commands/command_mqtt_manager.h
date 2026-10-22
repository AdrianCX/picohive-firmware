/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <string_view>

#include "command_dispatcher.h"
#include "command_registration.h"
#include "mqtt_handler.h"
#include "pending_clears.h"
#include "session.h"

#ifndef MQTT_COMMAND_SLOTS
#define MQTT_COMMAND_SLOTS 1
#endif

#ifndef MQTT_UNACKED_MAX
#define MQTT_UNACKED_MAX 8
#endif

class MqttCommandContext : public CommandContext
{
public:
    MqttCommandContext() { set_queue(m_waiting); }

    uint32_t send_space() override;
    uint32_t message_cost(std::string_view topic, uint32_t len) const override;
    ClearResult clear_retained(uint32_t waiter) override;

protected:
    void on_stream_cut() override;
    bool send_data(std::string_view topic, const uint8_t *data, uint32_t len) override;
    bool open_stream(std::string_view topic, uint32_t total_bytes) override;
    bool write_chunk(const uint8_t *data, uint32_t len) override;

private:
    ReplyBuffer m_waiting[COMMAND_REPLY_QUEUE_LENGTH];
};

class CommandMqttManager : public MQTTSocketInterface
{
public:
    static CommandMqttManager& get();

    bool update(uint64_t now_us);

    bool send_log(const char *message, uint32_t message_size);
    void reload_config();

    bool is_connected() { return m_connected; }

    void close_cut_stream();

    bool publish_text(std::string_view topic, const char *text, uint32_t len);

    bool publish_header(std::string_view topic, uint32_t total_bytes);
    bool publish_body(const uint8_t *data, uint32_t len);
    uint32_t link_space();
    ClearResult clear_retained_message(uint32_t waiter);

private:
    CommandMqttManager();

    void activate();
    bool dispatch(std::string_view topic, uint32_t message_length);
    void hold_ack(uint16_t message_id);
    bool ack_when_done();
    bool report_crash();

    void on_connected() override;
    bool on_conn_ack(conn_ack_code_t code, uint8_t flags) override;
    bool on_publish_header(uint8_t *topic, uint16_t topic_length, uint16_t message_id, uint32_t message_length) override;
    bool on_publish_data(uint8_t *data, uint32_t len, bool last_chunk) override;
    bool on_pub_ack(uint16_t message_id) override;
    bool on_ping_req() override;
    void on_closed() override;
    bool on_sent(uint16_t len) override;

    uint64_t m_disconnect_us = 0;

    MQTTSocketHandler m_mqtt;
    Session m_connection;

    bool m_connected = false;
    bool m_reconnect = false;
    bool m_active = false;
    bool m_was_connected = false;
    bool m_close_requested = false;
    bool m_message_topic_fits = false;
    uint8_t m_unacked_count = 0;
    uint16_t m_unacked[MQTT_UNACKED_MAX] = {};
    uint64_t m_unacked_since_us = 0;

    static const uint32_t SMALL_BUFFER_LEN = 100;

    MqttCommandContext m_context;
    CommandSlot m_slots[MQTT_COMMAND_SLOTS];
    CommandDispatcher m_dispatcher;
    PendingClears m_pending;

    static const uint64_t HOLD_TIMEOUT_US = 300000000;
    static const uint64_t CLEAR_TIMEOUT_US = 300000000;
    static const uint32_t TOPIC_LEN = 128;

    char m_topic[TOPIC_LEN] = {};
    char m_message_topic[TOPIC_LEN] = {};
};
