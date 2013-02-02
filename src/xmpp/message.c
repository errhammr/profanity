/*
 * message.c
 *
 * Copyright (C) 2012, 2013 James Booth <boothj5@gmail.com>
 *
 * This file is part of Profanity.
 *
 * Profanity is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Profanity is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Profanity.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <strophe.h>

#include "chat_session.h"
#include "log.h"
#include "muc.h"
#include "preferences.h"
#include "profanity.h"
#include "xmpp.h"
#include "stanza.h"
#include "message.h"

#define HANDLE(ns, type, func) xmpp_handler_add(conn, func, ns, STANZA_NAME_MESSAGE, type, ctx)

static int _message_handler(xmpp_conn_t * const conn,
    xmpp_stanza_t * const stanza, void * const userdata);
static int _groupchat_message_handler(xmpp_stanza_t * const stanza);
static int _chat_message_handler(xmpp_stanza_t * const stanza);

void
message_add_handlers(void)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    HANDLE(NULL, NULL, _message_handler);
}

void
message_send(const char * const msg, const char * const recipient)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    if (prefs_get_states()) {
        if (!chat_session_exists(recipient)) {
            chat_session_start(recipient, TRUE);
        }
    }

    xmpp_stanza_t *message;
    if (prefs_get_states() && chat_session_get_recipient_supports(recipient)) {
        chat_session_set_active(recipient);
        message = stanza_create_message(ctx, recipient, STANZA_TYPE_CHAT,
            msg, STANZA_NAME_ACTIVE);
    } else {
        message = stanza_create_message(ctx, recipient, STANZA_TYPE_CHAT,
            msg, NULL);
    }

    xmpp_send(conn, message);
    xmpp_stanza_release(message);
}

void
message_send_groupchat(const char * const msg, const char * const recipient)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    xmpp_stanza_t *message = stanza_create_message(ctx, recipient,
        STANZA_TYPE_GROUPCHAT, msg, NULL);

    xmpp_send(conn, message);
    xmpp_stanza_release(message);
}

void
message_send_composing(const char * const recipient)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    xmpp_stanza_t *stanza = stanza_create_chat_state(ctx, recipient,
        STANZA_NAME_COMPOSING);

    xmpp_send(conn, stanza);
    xmpp_stanza_release(stanza);
    chat_session_set_sent(recipient);
}

void
message_send_paused(const char * const recipient)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    xmpp_stanza_t *stanza = stanza_create_chat_state(ctx, recipient,
        STANZA_NAME_PAUSED);

    xmpp_send(conn, stanza);
    xmpp_stanza_release(stanza);
    chat_session_set_sent(recipient);
}

void
message_send_inactive(const char * const recipient)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    xmpp_stanza_t *stanza = stanza_create_chat_state(ctx, recipient,
        STANZA_NAME_INACTIVE);

    xmpp_send(conn, stanza);
    xmpp_stanza_release(stanza);
    chat_session_set_sent(recipient);
}

void
message_send_gone(const char * const recipient)
{
    xmpp_conn_t * const conn = jabber_get_conn();
    xmpp_ctx_t * const ctx = jabber_get_ctx();
    xmpp_stanza_t *stanza = stanza_create_chat_state(ctx, recipient,
        STANZA_NAME_GONE);

    xmpp_send(conn, stanza);
    xmpp_stanza_release(stanza);
    chat_session_set_sent(recipient);
}

static int
_message_handler(xmpp_conn_t * const conn,
    xmpp_stanza_t * const stanza, void * const userdata)
{
    gchar *type = xmpp_stanza_get_attribute(stanza, STANZA_ATTR_TYPE);

    if (type == NULL) {
        log_error("Message stanza received with no type attribute");
        return 1;
    } else if (strcmp(type, STANZA_TYPE_ERROR) == 0) {
        return error_handler(stanza);
    } else if (strcmp(type, STANZA_TYPE_GROUPCHAT) == 0) {
        return _groupchat_message_handler(stanza);
    } else if (strcmp(type, STANZA_TYPE_CHAT) == 0) {
        return _chat_message_handler(stanza);
    } else {
        log_error("Message stanza received with unknown type: %s", type);
        return 1;
    }
}

static int
_groupchat_message_handler(xmpp_stanza_t * const stanza)
{
    char *message = NULL;
    char *room_jid = xmpp_stanza_get_attribute(stanza, STANZA_ATTR_FROM);
    Jid *jid = jid_create(room_jid);

    // handle room broadcasts
    if (jid->resourcepart == NULL) {
        xmpp_stanza_t *subject = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_SUBJECT);

        // handle subject
        if (subject != NULL) {
            message = xmpp_stanza_get_text(subject);
            if (message != NULL) {
                prof_handle_room_subject(jid->barejid, message);
            }

            return 1;

        // handle other room broadcasts
        } else {
            xmpp_stanza_t *body = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_BODY);
            if (body != NULL) {
                message = xmpp_stanza_get_text(body);
                if (message != NULL) {
                    prof_handle_room_broadcast(room_jid, message);
                }
            }

            return 1;
        }
    }


    if (!jid_is_valid_room_form(jid)) {
        log_error("Invalid room JID: %s", jid->str);
        return 1;
    }

    // room not active in profanity
    if (!muc_room_is_active(jid)) {
        log_error("Message recieved for inactive chat room: %s", jid->str);
        return 1;
    }

    // determine if the notifications happened whilst offline
    GTimeVal tv_stamp;
    gboolean delayed = stanza_get_delay(stanza, &tv_stamp);
    xmpp_stanza_t *body = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_BODY);

    // check for and deal with message
    if (body != NULL) {
        char *message = xmpp_stanza_get_text(body);
        if (delayed) {
            prof_handle_room_history(jid->barejid, jid->resourcepart, tv_stamp, message);
        } else {
            prof_handle_room_message(jid->barejid, jid->resourcepart, message);
        }
    }

    jid_destroy(jid);

    return 1;
}

static int
_chat_message_handler(xmpp_stanza_t * const stanza)
{
    gchar *from = xmpp_stanza_get_attribute(stanza, STANZA_ATTR_FROM);
    Jid *jid = jid_create(from);

    // private message from chat room use full jid (room/nick)
    if (muc_room_is_active(jid)) {
        // determine if the notifications happened whilst offline
        GTimeVal tv_stamp;
        gboolean delayed = stanza_get_delay(stanza, &tv_stamp);

        // check for and deal with message
        xmpp_stanza_t *body = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_BODY);
        if (body != NULL) {
            char *message = xmpp_stanza_get_text(body);
            if (delayed) {
                prof_handle_delayed_message(jid->str, message, tv_stamp, TRUE);
            } else {
                prof_handle_incoming_message(jid->str, message, TRUE);
            }
        }

        free(jid);
        return 1;

    // standard chat message, use jid without resource
    } else {
        // determine chatstate support of recipient
        gboolean recipient_supports = FALSE;
        if (stanza_contains_chat_state(stanza)) {
            recipient_supports = TRUE;
        }

        // create or update chat session
        if (!chat_session_exists(jid->barejid)) {
            chat_session_start(jid->barejid, recipient_supports);
        } else {
            chat_session_set_recipient_supports(jid->barejid, recipient_supports);
        }

        // determine if the notifications happened whilst offline
        GTimeVal tv_stamp;
        gboolean delayed = stanza_get_delay(stanza, &tv_stamp);

        // deal with chat states if recipient supports them
        if (recipient_supports && (!delayed)) {
            if (xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_COMPOSING) != NULL) {
                if (prefs_get_notify_typing() || prefs_get_intype()) {
                    prof_handle_typing(jid->barejid);
                }
            } else if (xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_GONE) != NULL) {
                prof_handle_gone(jid->barejid);
            } else if (xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_PAUSED) != NULL) {
                // do something
            } else if (xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_INACTIVE) != NULL) {
                // do something
            } else { // handle <active/>
                // do something
            }
        }

        // check for and deal with message
        xmpp_stanza_t *body = xmpp_stanza_get_child_by_name(stanza, STANZA_NAME_BODY);
        if (body != NULL) {
            char *message = xmpp_stanza_get_text(body);
            if (delayed) {
                prof_handle_delayed_message(jid->barejid, message, tv_stamp, FALSE);
            } else {
                prof_handle_incoming_message(jid->barejid, message, FALSE);
            }
        }

        free(jid);
        return 1;
    }

}

