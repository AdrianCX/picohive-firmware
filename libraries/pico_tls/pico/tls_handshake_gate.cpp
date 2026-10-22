/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "tls_handshake_gate.h"
#include "session.h"

#include "lwip/timeouts.h"

Session *TLSHandshakeGate::m_active = NULL;
Session *TLSHandshakeGate::m_queue[TLS_HANDSHAKE_QUEUE_SIZE] = {};
uint32_t TLSHandshakeGate::m_start = 0;

bool TLSHandshakeGate::enter(Session *session)
{
    if ((m_active == NULL) || (m_active == session))
    {
        m_active = session;
        sys_untimeout(timeout, NULL);
        sys_timeout(TLS_HANDSHAKE_TIMEOUT_MS, timeout, NULL);
        return true;
    }

    leave(session, NULL);

    for (uint32_t i = 0; i < TLS_HANDSHAKE_QUEUE_SIZE; i++)
    {
        Session *&slot = m_queue[(m_start + i) % TLS_HANDSHAKE_QUEUE_SIZE];

        if (slot == NULL)
        {
            slot = session;
            trace("TLSHandshakeGate: session=%p host=%s waits for its turn", session, safestr(session->host()));
            return false;
        }
    }

    trace("TLSHandshakeGate: session=%p host=%s goes ahead, the queue is full", session, safestr(session->host()));
    return true;
}

void TLSHandshakeGate::leave(Session *session, const char *why)
{
    for (Session *&slot : m_queue)
    {
        if (slot == session)
        {
            slot = NULL;
        }
    }

    if (m_active == session)
    {
        m_active = NULL;
        sys_untimeout(timeout, NULL);

        trace("TLSHandshakeGate: session=%p host=%s turn over: %s", session, safestr(session->host()), why);

        sys_timeout(0, start_next, NULL);
    }
}

void TLSHandshakeGate::start_next(void *arg)
{
    if (m_active != NULL)
    {
        return;
    }

    for (uint32_t i = 0; i < TLS_HANDSHAKE_QUEUE_SIZE; i++)
    {
        uint32_t index = (m_start + i) % TLS_HANDSHAKE_QUEUE_SIZE;
        Session *next = m_queue[index];

        if (next != NULL)
        {
            m_queue[index] = NULL;
            m_start = (index + 1) % TLS_HANDSHAKE_QUEUE_SIZE;
            m_active = next;

            trace("TLSHandshakeGate: session=%p host=%s has its turn", next, safestr(next->host()));

            next->resume_connect();
            return;
        }
    }
}

void TLSHandshakeGate::timeout(void *arg)
{
    if (m_active != NULL)
    {
        trace("TLSHandshakeGate: session=%p host=%s no handshake after %d ms, closing it", m_active, safestr(m_active->host()), TLS_HANDSHAKE_TIMEOUT_MS);

        m_active->close();
    }
}
