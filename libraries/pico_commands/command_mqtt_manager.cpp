/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <stdio.h>
#include <string.h>

#include "os_platform.h"

#include "app_config.h"
#include "command_mqtt_manager.h"
#include "pico_logger.h"
#include "upgrade_helper.h"

bool MqttCommandContext::send_data(std::string_view topic, const uint8_t *data, uint32_t len)
{
    CommandMqttManager& mqtt = CommandMqttManager::get();

    return mqtt.publish_header(topic, len) && mqtt.publish_body(data, len);
}

bool MqttCommandContext::open_stream(std::string_view topic, uint32_t total_bytes)
{
    return CommandMqttManager::get().publish_header(topic, total_bytes);
}

uint32_t MqttCommandContext::send_space()
{
    return CommandMqttManager::get().link_space();
}

uint32_t MqttCommandContext::message_cost(std::string_view topic, uint32_t len) const
{
    static const uint32_t FRAMING = 5 + 2 + 2;
    static const uint32_t TOPIC_AROUND = sizeof("devices//recv/") + MAX_CONFIG_STR;

    return len + topic.size() + prefix.size() + TOPIC_AROUND + FRAMING;
}

bool MqttCommandContext::write_chunk(const uint8_t *data, uint32_t len)
{
    return CommandMqttManager::get().publish_body(data, len);
}

void MqttCommandContext::on_stream_cut()
{
    CommandMqttManager::get().close_cut_stream();
}

ClearResult MqttCommandContext::clear_retained(uint32_t waiter)
{
    return CommandMqttManager::get().clear_retained_message(waiter);
}

CommandMqttManager& CommandMqttManager::get()
{
    static CommandMqttManager instance;
    return instance;
}

CommandMqttManager::CommandMqttManager()
    : m_dispatcher(MqttCommandSet::all(), m_context, m_slots)
{
    trace("CommandMqttManager::CommandMqttManager: this=%p", this);
	
    //m_connection.set_debug(true);
    
    m_mqtt.set_upstream(this);
    m_mqtt.set_downstream(&m_connection);

    m_connection.set_callback(&m_mqtt);

    activate();    
}

void CommandMqttManager::activate()
{
    trace("CommandMqttManager::activate: this=%p mqtt_host=%s port=%d", this, get_config().mqtt_host, get_config().mqtt_port);
    m_reconnect = false;
    
    if ((get_config().mqtt_user[0] != 0) && (get_config().mqtt_host[0] != 0) && (get_config().mqtt_port != 0))
    {
        m_active = true;
        m_connection.set_tls(get_config().mqtt_tls);
        m_connection.connect(get_config().mqtt_host, get_config().mqtt_port);
    }
    else
    {
        m_active = false;
    }
}

void CommandMqttManager::reload_config()
{
    trace("CommandMqttManager::reload_config: this=%p, isUpgrading=%d, m_connected=%d, m_active=%d", this, UpgradeHelper::isUpgrading(), m_connected, m_active);
    
    if (UpgradeHelper::isUpgrading())
    {
        return;
    }
    
    if (m_connected)
    {
        m_connection.close();
    }
    else if (!m_active)
    {
        activate();
    }
}

void CommandMqttManager::on_connected()
{
    trace("CommandMqttManager::on_connected: this=%p", this);

    int keepalive = 60;

	char smallBuffer[SMALL_BUFFER_LEN] = {};
	
    snprintf(smallBuffer, 128, "devices/%s/status", get_config().mqtt_user);
    m_mqtt.send_connect(true, keepalive, get_config().mqtt_user, smallBuffer, "offline", get_config().mqtt_user, get_config().mqtt_pass);
}

bool CommandMqttManager::on_conn_ack(conn_ack_code_t code, uint8_t flags)
{
    if (code != CONNECTION_ACCEPTED)
    {
        trace("CommandMqttManager::on_conn_ack: this=%p, connection failed with code[%d] flags[%d]", this, code, flags);
        return false;
    }

    trace("CommandMqttManager::on_conn_ack: this=%p, code=%d, flags=%d", this, code, flags);

    m_connected = true;
	char smallBuffer[SMALL_BUFFER_LEN] = {};

    snprintf(smallBuffer, SMALL_BUFFER_LEN, "devices/%s/upgrade", get_config().mqtt_user);
    if (!m_mqtt.send_subscribe(smallBuffer))
    {
        return false;
    }

    snprintf(smallBuffer, SMALL_BUFFER_LEN, "devices/%s/send/#", get_config().mqtt_user);
    if (!m_mqtt.send_subscribe(smallBuffer))
    {
        return false;
    }

    snprintf(smallBuffer, SMALL_BUFFER_LEN, "devices/%s/status", get_config().mqtt_user);
    const char *message = "online";
    
    if (!m_mqtt.send_publish_header(smallBuffer, strlen(message), true))
    {
        trace("CommandMqttManager::on_conn_ack: this=%p failed to publish header for status message", this);
        return false;
    }

    if (!m_mqtt.send_publish_data((uint8_t*)message, strlen(message)))
    {
        trace("CommandMqttManager::on_conn_ack: this=%p failed to publish data for status message", this);
        return false;
    }

    return report_crash();
}

bool CommandMqttManager::on_publish_header(uint8_t *topic, uint16_t topic_length, uint16_t message_id, uint32_t message_length)
{
    //trace("CommandMqttManager::on_publish_header: this=%p, topic=[%.*s] message_id[%d] message_length[%d]", this, topic_length, topic, message_id, message_length);

    hold_ack(message_id);

    const int HEADER_LEN = 8;
    if (strncmp((const char *)topic, "devices/", HEADER_LEN) != 0)
    {
        // Command not meant for us - only messages towards our app id are accepted.
        return ack_when_done();
    }

    topic += HEADER_LEN;
    topic_length -= HEADER_LEN;
    
    int name_length = strlen(get_config().mqtt_user);
    if (strncmp((const char *)topic, get_config().mqtt_user, name_length) != 0)
    {
        // Command not meant for us - only messages towards our app id are accepted.
        return ack_when_done();
    }

    // strip out app name
    topic += name_length + 1;
    topic_length -= name_length + 1;

    std::string_view command((const char *)topic, topic_length);
    
    //trace("CommandMqttManager::on_publish_header: this=%p, topic=[%.*s] message_id[%d] message_length[%d] forwarding to upper layer", this, topic_length, topic, message_id, message_length);
    bool result = dispatch(command, message_length);
    return ack_when_done() && result;
}

bool CommandMqttManager::on_publish_data(uint8_t *data, uint32_t len, bool last_chunk)
{
    //if (last_chunk)
    //{
    //    trace("CommandMqttManager::on_publish_data: this=%p, len[%d] last_chunk[%d]", this, len, last_chunk);
    //}

    bool result = m_dispatcher.dispatch_data(data, len, last_chunk);
    return ack_when_done() && result;
}

bool CommandMqttManager::on_pub_ack(uint16_t message_id)
{
    uint32_t waiter = NO_WAITER;

    if (m_pending.take(message_id, waiter) && (waiter != NO_WAITER))
    {
        m_dispatcher.retained_cleared(waiter);
    }

    return ack_when_done();
}

void CommandMqttManager::hold_ack(uint16_t message_id)
{
    // QoS 0: nothing to acknowledge.
    if (message_id == 0)
    {
        return;
    }

    if (m_unacked_count >= MQTT_UNACKED_MAX)
    {
        trace("mqtt: %d messages unacknowledged, acking message_id[%d] now", m_unacked_count, message_id);
        m_mqtt.send_puback(message_id);
        return;
    }

    if (m_unacked_count == 0)
    {
        m_unacked_since_us = to_us_since_boot(get_absolute_time());
    }

    m_unacked[m_unacked_count++] = message_id;
}

bool CommandMqttManager::ack_when_done()
{
    if ((m_unacked_count == 0) || !m_dispatcher.idle())
    {
        return true;
    }

    uint8_t count = m_unacked_count;
    m_unacked_count = 0;

    for (uint8_t i = 0; i < count; ++i)
    {
        if (!m_mqtt.send_puback(m_unacked[i]))
        {
            return false;
        }
    }

    return true;
}


bool CommandMqttManager::on_ping_req()
{
    trace("CommandMqttManager::on_ping_req: this=%p", this);
    
    m_mqtt.send_ping();
    return true;
}


void CommandMqttManager::on_closed()
{
    trace("CommandMqttManager::on_closed: this=%p",this);

    m_connected = false;

    m_reconnect = true;
    m_disconnect_us = to_us_since_boot(get_absolute_time());
}

bool CommandMqttManager::report_crash()
{
    uint32_t *callstack = nullptr;
    uint32_t depth = get_crash_callstack(&callstack);

    if (depth > 0)
    {
		static const uint32_t BUFFER_LEN = 800;
		
		char smallBuffer[SMALL_BUFFER_LEN] = {};

        get_build_id(smallBuffer, SMALL_BUFFER_LEN);

        size_t bytes = 0;
		char buffer[BUFFER_LEN] = {};
        bytes = snprintf(buffer, BUFFER_LEN, "{ \"buildid\": \"%s\", \"callstack\":[ ", smallBuffer);

        for (size_t i = 0; i < depth && bytes < BUFFER_LEN; i++)
        {
            if (i == 0)
            {
                bytes += snprintf(&buffer[bytes], BUFFER_LEN-bytes, "\"%08lx\"", (unsigned long)callstack[i]);
            }
            else
            {
                bytes += snprintf(&buffer[bytes], BUFFER_LEN-bytes, ", \"%08lx\"", (unsigned long)callstack[i]);
            }
        }

        if (bytes < BUFFER_LEN)
        {
            bytes += snprintf(&buffer[bytes], BUFFER_LEN-bytes, " ] }");
        }

        trace("CommandMqttManager::report_crash: this=%p message_len=%d", this, bytes);

        snprintf(smallBuffer, SMALL_BUFFER_LEN, "devices/%s/crash", get_config().mqtt_user);

        if (!m_mqtt.send_publish_header(smallBuffer, bytes, false))
        {
            trace("CommandMqttManager::report_crash: this=%p failed to publish header", this);
            return false;
        }

        if (!m_mqtt.send_publish_data((uint8_t*)buffer, bytes))
        {
            trace("CommandMqttManager::report_crash: this=%p failed to publish data", this);
            return false;
        }
        
        clear_saved_crash();
    }

    return true;
}

bool CommandMqttManager::send_log(const char *message, uint32_t message_size)
{
	char smallBuffer[SMALL_BUFFER_LEN] = {};
    snprintf(smallBuffer, SMALL_BUFFER_LEN, "devices/%s/recv/debug", get_config().mqtt_user);
    
    if (!m_mqtt.send_publish_header(smallBuffer, message_size, false, nullptr, true))
    {
        trace("MqttManager::send_log: this=%p failed to publish header. buffer=%s, message_size=%d", this, smallBuffer, message_size);
        return false;
    }
    
    if (!m_mqtt.send_publish_data((uint8_t*)message, message_size, true))
    {
        trace("MqttManager::send_log: this=%p failed to publish message data", this);
        return false;
    }

    return true;
}


bool CommandMqttManager::on_sent(uint16_t len)
{
    bool result = m_dispatcher.dispatch_sent();
    return ack_when_done() && result;
}

bool CommandMqttManager::dispatch(std::string_view topic, uint32_t message_length)
{
    if (message_length == 0)
    {
        std::string_view args;
        const Command *command = MqttCommandSet::find(topic, args);

        // A cleared retained message coming back to us, our own clear or devicepage cancelling: nothing to run.
        if ((command != nullptr) && command->clears_retained)
        {
            return true;
        }
    }

    if (m_dispatcher.is_exclusive())
    {
        trace("mqtt: %.*s dropped, exclusive command running", (int)topic.size(), topic.data());
        return true;
    }

    m_message_topic_fits = (topic.size() < TOPIC_LEN);
    if (m_message_topic_fits)
    {
        memcpy(m_message_topic, topic.data(), topic.size());
        m_message_topic[topic.size()] = 0;
    }

    return m_dispatcher.dispatch_header(topic, message_length);
}

bool CommandMqttManager::update(uint64_t now_us)
{
    bool result = true;

    if (m_close_requested)
    {
        m_close_requested = false;

        if (m_connected)
        {
            m_connection.close();
        }
    }

    if (m_connected && m_connection.is_connected())
    {
        m_mqtt.update(now_us);
    }

    if (m_reconnect && ((now_us - m_disconnect_us) > 5000000LL))
    {
        activate();
    }

    if (m_was_connected && !is_connected())
    {
        m_dispatcher.abort("the mqtt connection dropped");
        m_pending.clear_all();

        // A clean session: the broker forgets what it sent us too.
        m_unacked_count = 0;
    }
    else if (is_connected())
    {
        if (m_dispatcher.is_sending() || m_dispatcher.has_queued())
        {
            result = m_dispatcher.dispatch_sent() && result;
        }

        int i;
        while ((i = m_pending.find_expired(now_us, CLEAR_TIMEOUT_US)) >= 0)
        {
            uint32_t waiter = m_pending.entries[i].waiter;

            trace("mqtt: no PUBACK for the clear message_id[%d] after %llu ms", m_pending.entries[i].message_id, (unsigned long long)(CLEAR_TIMEOUT_US / 1000));
            m_pending.remove(i);

            if (waiter != NO_WAITER)
            {
                m_dispatcher.abandon(waiter, "the broker never acknowledged clearing the retained message");
            }
        }

        if ((m_unacked_count > 0) && ((now_us - m_unacked_since_us) > HOLD_TIMEOUT_US))
        {
            trace("mqtt: a message held for %llu ms, aborting", (unsigned long long)(HOLD_TIMEOUT_US / 1000));
            m_dispatcher.abort("held a message too long");
        }

        result = ack_when_done() && result;
    }

    m_was_connected = is_connected();
    return result;
}

void CommandMqttManager::close_cut_stream()
{
    if (!m_connected)
    {
        return;
    }

    trace("mqtt: a reply stream stopped part way, closing the connection");
    m_close_requested = true;
}

bool CommandMqttManager::publish_header(std::string_view topic, uint32_t total_bytes)
{
    int len = snprintf(m_topic, TOPIC_LEN, "devices/%s/recv/%.*s", get_config().mqtt_user, (int)topic.size(), topic.data());

    if ((len < 0) || (len >= (int)TOPIC_LEN))
    {
        trace("mqtt: reply topic too long: %.*s", (int)topic.size(), topic.data());
        return false;
    }

    return m_mqtt.send_publish_header(m_topic, total_bytes, false, nullptr, true);
}

bool CommandMqttManager::publish_text(std::string_view topic, const char *text, uint32_t len)
{
    if (!m_connected || m_dispatcher.is_sending() || m_dispatcher.has_queued() || (link_space() < (len + TOPIC_LEN + 16)))
    {
        return false;
    }

    return publish_header(topic, len) && publish_body((const uint8_t *)text, len);
}

bool CommandMqttManager::publish_body(const uint8_t *data, uint32_t len)
{
    return m_mqtt.send_publish_data((uint8_t *)data, len, true);
}

uint32_t CommandMqttManager::link_space()
{
    return m_connection.send_buffer_size();
}

ClearResult CommandMqttManager::clear_retained_message(uint32_t waiter)
{
    int len = snprintf(m_topic, TOPIC_LEN, "devices/%s/%s", get_config().mqtt_user, m_message_topic);

    if (!m_message_topic_fits || (len < 0) || (len >= (int)TOPIC_LEN))
    {
        trace("mqtt: topic too long to clear");
        return ClearResult::Failed;
    }

    if ((waiter != NO_WAITER) && m_pending.full())
    {
        trace("mqtt: too many clears pending, %s stays retained", m_message_topic);
        return ClearResult::Failed;
    }

    uint16_t message_id = 0;

    if (!m_mqtt.send_publish_header(m_topic, 0, true, &message_id, true))
    {
        return ClearResult::Failed;
    }

    // Nobody waits: the clear is fire and forget.
    if (waiter == NO_WAITER)
    {
        return ClearResult::Done;
    }

    m_pending.add(message_id, waiter, to_us_since_boot(get_absolute_time()));
    return ClearResult::Pending;
}
