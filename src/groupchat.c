/*  groupchat.c
 *
 *
 *  Copyright (C) 2014 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE    /* needed for strcasestr() and wcswidth() */
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <wchar.h>
#include <unistd.h>

#ifdef NO_GETTEXT
#define gettext(A) (A)
#else
#include <libintl.h>
#endif

#ifdef AUDIO
#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
/* compatibility with older versions of OpenAL */
#ifndef ALC_ALL_DEVICES_SPECIFIER
#include <AL/alext.h>
#endif  /* ALC_ALL_DEVICES_SPECIFIER */
#endif  /* __APPLE__ */
#endif  /* AUDIO */

#include "windows.h"
#include "toxic.h"
#include "execute.h"
#include "misc_tools.h"
#include "groupchat.h"
#include "prompt.h"
#include "toxic_strings.h"
#include "log.h"
#include "line_info.h"
#include "settings.h"
#include "input.h"
#include "help.h"
#include "notify.h"
#include "autocomplete.h"
#include "device.h"

extern char *DATA_FILE;

static GroupChat groupchats[MAX_GROUPCHAT_NUM];
static int max_groupchat_index = 0;

extern struct user_settings *user_settings;
extern struct Winthread Winthread;

#ifdef AUDIO
#define AC_NUM_GROUP_COMMANDS 34
#else
#define AC_NUM_GROUP_COMMANDS 30
#endif /* AUDIO */

/* groupchat command names used for tab completion. */
static const char group_cmd_list[AC_NUM_GROUP_COMMANDS][MAX_CMDNAME_SIZE] = {
    { "/accept"     },
    { "/add"        },
    { "/avatar"     },
    { "/chatid"     },
    { "/clear"      },
    { "/close"      },
    { "/connect"    },
    { "/decline"    },
    { "/exit"       },
    { "/group"      },
    { "/help"       },
    { "/ignore"     },
    { "/join"       },
    { "/kick"       },
    { "/log"        },
    { "/mod"        },
    { "/myid"       },
    { "/nick"       },
    { "/note"       },
    { "/passwd"     },
    { "/peerlimit"  },
    { "/privacy"    },
    { "/quit"       },
    { "/rejoin"     },
    { "/requests"   },
    { "/status"     },
    { "/topic"      },
    { "/unignore"   },
    { "/unmod"      },
    { "/whisper"    },

#ifdef AUDIO

    { "/lsdev"       },
    { "/sdev"        },
    { "/mute"        },
    { "/sense"       },

#endif /* AUDIO */
};

static void groupchat_set_group_name(ToxWindow *self, Tox *m, int groupnum);
ToxWindow new_group_chat(Tox *m, int groupnum, const char *groupname, int length);
static void groupchat_onGroupNamelistChange(ToxWindow *self, Tox *m, int groupnum);

int init_groupchat_win(Tox *m, int groupnum, const char *groupname, int length)
{
    if (groupnum > MAX_GROUPCHAT_NUM)
        return -1;

    ToxWindow self = new_group_chat(m, groupnum, groupname, length);

    /* In case we're loading a saved group */
    if (length <= 0)
        groupchat_set_group_name(&self, m, groupnum);

    int i;

    for (i = 0; i <= max_groupchat_index; ++i) {
        if (!groupchats[i].active) {
            groupchats[i].chatwin = add_window(m, self);
            groupchats[i].active = true;
            groupchats[i].peer_names = malloc(sizeof(uint8_t) * TOX_MAX_NAME_LENGTH);
            groupchats[i].peer_name_lengths = malloc(sizeof(uint16_t));
            groupchats[i].groupnumber = groupnum;

            if (groupchats[i].peer_names == NULL || groupchats[i].peer_name_lengths == NULL)
                exit_toxic_err(gettext("failed in init_groupchat_win"), FATALERR_MEMORY);

            if (i == max_groupchat_index)
                ++max_groupchat_index;

            set_active_window(groupchats[i].chatwin);
            groupchat_onGroupNamelistChange(&self, m, groupnum);
            store_data(m, DATA_FILE);

            return 0;
        }
    }

    return -1;
}

static void kill_groupchat_window(ToxWindow *self)
{
    ChatContext *ctx = self->chatwin;

    log_disable(ctx->log);
    line_info_cleanup(ctx->hst);
    delwin(ctx->linewin);
    delwin(ctx->history);
    delwin(ctx->sidebar);
    free(ctx->log);
    free(ctx);
    free(self->help);
    del_window(self);
}

/* Closes groupchat window and cleans up. */
void close_groupchat(ToxWindow *self, Tox *m, int groupnum)
{
    free(groupchats[groupnum].peer_names);
    free(groupchats[groupnum].peer_name_lengths);
    memset(&groupchats[groupnum], 0, sizeof(GroupChat));

    int i;

    for (i = max_groupchat_index; i > 0; --i) {
        if (groupchats[i - 1].active)
            break;
    }

    max_groupchat_index = i;
    kill_groupchat_window(self);
}

static void exit_groupchat(ToxWindow *self, Tox *m, int groupnum, const char *partmessage, int length)
{
    tox_group_delete(m, groupnum, (uint8_t *) partmessage, (uint16_t) length);
    close_groupchat(self, m, groupnum);
}

/* Note: the arguments to these functions are validated in the caller functions */
void set_nick_all_groups(Tox *m, const char *nick, uint16_t length)
{
    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    int i;

    for (i = 0; i < max_groupchat_index; ++i) {
        if (groupchats[i].active) {
            ToxWindow *self = get_window_ptr(groupchats[i].chatwin);
            int ret = tox_group_set_self_name(m, groupchats[i].groupnumber, (uint8_t *) nick, length);

            if (ret == -1 && groupchats[i].is_connected)
                line_info_add(self, NULL, NULL, 0, SYS_MSG, 0, 0, gettext("Invalid nick"));
            else if (ret == -2)
                line_info_add(self, NULL, NULL, 0, SYS_MSG, 0, RED, gettext("-!- That nick is already in use"));
            else
                line_info_add(self, timefrmt, NULL, nick, NAME_CHANGE, 0, MAGENTA, gettext("You are now known as "));
        }
    }
}

void set_status_all_groups(Tox *m, uint8_t status)
{
    int i;

    for (i = 0; i < max_groupchat_index; ++i) {
        if (groupchats[i].active)
            tox_group_set_status(m, groupchats[i].groupnumber, status);
    }
}

int group_get_nick_peernumber(int groupnum, const char *nick)
{
    int i;

    for (i = 0; i < groupchats[groupnum].num_peers; ++i) {
        if (strcasecmp(nick, (char *) &groupchats[groupnum].peer_names[i * TOX_MAX_NAME_LENGTH]) == 0)
            return i;
    }

    return -1;
}

/* destroys and re-creates groupchat window with or without the peerlist */
void redraw_groupchat_win(ToxWindow *self)
{
    ChatContext *ctx = self->chatwin;

    endwin();
    refresh();
    clear();

    int x2, y2;
    getmaxyx(stdscr, y2, x2);
    y2 -= 2;

    if (ctx->sidebar) {
        delwin(ctx->sidebar);
        ctx->sidebar = NULL;
    }

    delwin(ctx->linewin);
    delwin(ctx->history);
    delwin(self->window);

    self->window = newwin(y2, x2, 0, 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);

    if (self->show_peerlist) {
        ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT + 1, x2 - SIDEBAR_WIDTH - 1, 0, 0);
        ctx->sidebar = subwin(self->window, y2 - CHATBOX_HEIGHT + 1, SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);
    } else {
        ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT + 1, x2, 0, 0);
    }

    scrollok(ctx->history, 0);

}

static void groupchat_onGroupMessage(ToxWindow *self, Tox *m, int groupnum, int peernum,
                                     const char *msg, uint16_t len)
{
    if (self->num != groupnum)
        return;

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, nick, peernum, groupnum);

    char selfnick[TOX_MAX_NAME_LENGTH];
    tox_self_get_name(m, (uint8_t *) selfnick);

    size_t sn_len = tox_self_get_name_size(m);
    selfnick[sn_len] = '\0';

    int nick_clr = CYAN;

    /* Only play sound if mentioned by someone else */
    if (strcasestr(msg, selfnick) && strcmp(selfnick, nick)) {
        sound_notify(self, generic_message, NT_WNDALERT_0, NULL);

        if (self->active_box != -1)
            box_silent_notify2(self, NT_NOFOCUS, self->active_box, "%s %s", nick, msg);
        else
            box_silent_notify(self, NT_NOFOCUS, &self->active_box, self->name, "%s %s", nick, msg);

        nick_clr = RED;
    }
    else {
        sound_notify(self, silent, NT_WNDALERT_1, NULL);
    }

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, nick, NULL, IN_MSG, 0, nick_clr, "%s", msg);
    write_to_log(msg, nick, ctx->log, false);
}

static void groupchat_onGroupAction(ToxWindow *self, Tox *m, int groupnum, int peernum, const char *action,
                                    uint16_t len)
{
    if (self->num != groupnum)
        return;

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, nick, peernum, groupnum);

    char selfnick[TOX_MAX_NAME_LENGTH];
    tox_self_get_name(m, (uint8_t *) selfnick);

    size_t n_len = tox_self_get_name_size(m);
    selfnick[n_len] = '\0';

    if (strcasestr(action, selfnick)) {
        sound_notify(self, generic_message, NT_WNDALERT_0, NULL);

        if (self->active_box != -1)
            box_silent_notify2(self, NT_NOFOCUS, self->active_box, "* %s %s", nick, action );
        else
            box_silent_notify(self, NT_NOFOCUS, &self->active_box, self->name, "* %s %s", nick, action);
    }
    else {
        sound_notify(self, silent, NT_WNDALERT_1, NULL);
    }

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, nick, NULL, IN_ACTION, 0, 0, "%s", action);
    write_to_log(action, nick, ctx->log, true);
}

static void groupchat_onGroupPrivateMessage(ToxWindow *self, Tox *m, int groupnum, uint32_t peernum,
                                            const char *msg, uint16_t len)
{
    if (self->num != groupnum)
        return;

    ChatContext *ctx = self->chatwin;

    char nick[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, nick, peernum, groupnum);

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, nick, NULL, IN_PRVT_MSG, 0, MAGENTA, "%s", msg);
    write_to_log(msg, nick, ctx->log, false);

    sound_notify(self, generic_message, NT_WNDALERT_0, NULL);

    if (self->active_box != -1)
        box_silent_notify2(self, NT_NOFOCUS, self->active_box, "%s %s", nick, msg);
    else
        box_silent_notify(self, NT_NOFOCUS, &self->active_box, self->name, "%s %s", nick, msg);
}

static void groupchat_onGroupTopicChange(ToxWindow *self, Tox *m, int groupnum, uint32_t peernum,
                                         const char *topic, uint16_t length)
{
    ChatContext *ctx = self->chatwin;

    if (self->num != groupnum)
        return;

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    char nick[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, nick, peernum, groupnum);
    line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, MAGENTA, gettext("-!- %s set the topic to: %s"), nick, topic);

    char tmp_event[MAX_STR_SIZE];
    snprintf(tmp_event, sizeof(tmp_event), gettext(" set the group topic to %s"), topic);
    write_to_log(tmp_event, nick, ctx->log, true);
}

/* Copies peer names/lengths  */
static void copy_peernames(int gnum, uint8_t peerlist[][TOX_MAX_NAME_LENGTH], uint16_t lengths[], int npeers)
{
    free(groupchats[gnum].peer_names);
    free(groupchats[gnum].peer_name_lengths);

    int N = TOX_MAX_NAME_LENGTH;

    groupchats[gnum].peer_names = calloc(1, sizeof(uint8_t) * MAX(1, npeers) * N);
    groupchats[gnum].peer_name_lengths = calloc(1, sizeof(uint32_t) * MAX(1, npeers));

    if (groupchats[gnum].peer_names == NULL || groupchats[gnum].peer_name_lengths == NULL)
        exit_toxic_err(gettext("failed in copy_peernames"), FATALERR_MEMORY);

    if (npeers == 0)
        return;

    uint16_t u_len = strlen(UNKNOWN_NAME);
    int i;

    for (i = 0; i < npeers; ++i) {
         if (!lengths[i]) {
            memcpy(&groupchats[gnum].peer_names[i * N], UNKNOWN_NAME, u_len);
            groupchats[gnum].peer_names[i * N + u_len] = '\0';
            groupchats[gnum].peer_name_lengths[i] = u_len;
        } else {
            uint16_t n_len = MIN(lengths[i], TOXIC_MAX_NAME_LENGTH - 1);
            memcpy(&groupchats[gnum].peer_names[i * N], peerlist[i], n_len);
            groupchats[gnum].peer_names[i * N + n_len] = '\0';
            groupchats[gnum].peer_name_lengths[i] = n_len;
            filter_str((char *) &groupchats[gnum].peer_names[i * N], n_len);
        }
    }
}

static void groupchat_onGroupNamelistChange(ToxWindow *self, Tox *m, int groupnum)
{
    if (self->num != groupnum)
        return;

    int num_peers = tox_group_get_number_peers(m, groupnum);

    if (num_peers <= 0)
        return;

    uint8_t tmp_peerlist[num_peers][TOX_MAX_NAME_LENGTH];
    uint16_t tmp_peerlens[num_peers];

    if (tox_group_get_names(m, groupnum, tmp_peerlist, tmp_peerlens, num_peers) == -1)
        num_peers = 0;

    groupchats[groupnum].num_peers = num_peers;
    copy_peernames(groupnum, tmp_peerlist, tmp_peerlens, num_peers);
}

static void groupchat_onGroupPeerJoin(ToxWindow *self, Tox *m, int groupnum, uint32_t peernum)
{
    if (groupnum != self->num)
        return;

    if (peernum > groupchats[groupnum].num_peers)
        return;

    char name[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, name, peernum, groupnum);

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, name, NULL, CONNECTION, 0, GREEN, gettext("has joined the room"));

    char log_str[TOXIC_MAX_NAME_LENGTH + 32];
    snprintf(log_str, sizeof(log_str), gettext("%s has joined the room"), name);

    write_to_log(log_str, name, self->chatwin->log, true);
    sound_notify(self, silent, NT_WNDALERT_2, NULL);
}

static void groupchat_onGroupPeerExit(ToxWindow *self, Tox *m, int groupnum, uint32_t peernum,
                                      const char *partmessage, uint16_t len)
{
    if (groupnum != self->num)
        return;

    if (peernum > groupchats[groupnum].num_peers)
        return;

    char name[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, name, peernum, groupnum);

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, name, NULL, DISCONNECTION, 0, RED, gettext("has left the room (%s)"), partmessage);

    char log_str[TOXIC_MAX_NAME_LENGTH + MAX_STR_SIZE];
    snprintf(log_str, sizeof(log_str), gettext("%s has left the room (%s)"), name, partmessage);

    write_to_log(log_str, name, self->chatwin->log, true);
    sound_notify(self, silent, NT_WNDALERT_2, NULL);
}

static void groupchat_set_group_name(ToxWindow *self, Tox *m, int groupnum)
{
    char tmp_groupname[TOX_MAX_GROUP_NAME_LENGTH];
    int glen = tox_group_get_group_name(m, groupnum, (uint8_t *) tmp_groupname);

    char groupname[TOX_MAX_GROUP_NAME_LENGTH];
    copy_tox_str(groupname, sizeof(groupname), tmp_groupname, glen);

    if (glen > 0)
        set_window_title(self, groupname, glen);
}

static void groupchat_onGroupSelfJoin(ToxWindow *self, Tox *m, int groupnum)
{
    if (groupnum != self->num)
        return;

    groupchat_set_group_name(self, m, groupnum);

    char tmp_topic[TOX_MAX_GROUP_TOPIC_LENGTH];
    int tlen = tox_group_get_topic(m, groupnum, (uint8_t *) tmp_topic);

    char topic[TOX_MAX_GROUP_TOPIC_LENGTH];
    copy_tox_str(topic, sizeof(topic), tmp_topic, tlen);

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    int i;

    for (i = 0; i < max_groupchat_index; ++i) {
        if (groupchats[i].active && groupchats[i].groupnumber == groupnum) {
            groupchats[i].is_connected = true;
            break;
        }
    }

    line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, MAGENTA, gettext("-!- Topic set to: %s"), topic);
}

static void groupchat_onGroupRejected(ToxWindow *self, Tox *m, int groupnum, uint8_t type)
{
    if (groupnum != self->num)
        return;

    const char *msg;

    switch (type) {
        case TOX_GJ_NICK_TAKEN:
            msg = gettext("Nick already in use. Change your nick and use the '/rejoin' command.");
            break;
        case TOX_GJ_GROUP_FULL:
            msg = gettext("Group is full. Try again with the '/rejoin' command.");
            break;
        case TOX_GJ_INCORRECT_PASSWORD:
            msg = gettext("Invalid password.");
            break;
        case TOX_GJ_INVITE_FAILED:
            msg = gettext("Invite failed. Try again with the '/rejoin' command.");
            break;
        default:
            return;
    }

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 0, RED, "-!- %s", msg);
}

static void groupchat_onGroupModeration(ToxWindow *self, Tox *m, int groupnum, uint32_t src_peernum,
                                        uint32_t tgt_peernum, TOX_GROUP_MOD_EVENT type)
{
    if (groupnum != self->num)
        return;

    char src_name[TOX_MAX_NAME_LENGTH];
    char tgt_name[TOX_MAX_NAME_LENGTH];

    if (get_group_nick_truncate(m, src_name, src_peernum, groupnum) == -1)
        return;

    if (get_group_nick_truncate(m, tgt_name, tgt_peernum, groupnum) == -1)
        return;

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    switch (type) {
        case TOX_GROUP_MOD_EVENT_KICK:
            line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, RED, gettext("-!- %s has been kicked by %s"), tgt_name, src_name);
            break;
        case TOX_GROUP_MOD_EVENT_BAN:
            line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, RED, gettext("-!- %s has been banned by %s"), tgt_name, src_name);
            break;
        case TOX_GROUP_MOD_EVENT_OBSERVER:
            line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, BLUE, gettext("-!- %s has been silenced by %s"), tgt_name, src_name);
            break;
        case TOX_GROUP_MOD_EVENT_USER:
            line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, BLUE, gettext("-!- %s has been made a normal user by %s"), tgt_name, src_name);
            break;
        case TOX_GROUP_MOD_EVENT_MODERATOR:
            line_info_add(self, timefrmt, NULL, NULL, SYS_MSG, 1, BLUE, gettext("-!- %s has been promoted to moderator by %s"), tgt_name, src_name);
            break;
        default:
            return;
    }
}

static void groupchat_onGroupNickChange(ToxWindow *self, Tox *m, int groupnum, uint32_t peernum,
                                        const char *newnick, uint16_t len)
{
    if (groupnum != self->num)
        return;

    char oldnick[TOX_MAX_NAME_LENGTH];
    get_group_nick_truncate(m, oldnick, peernum, groupnum);

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));
    line_info_add(self, timefrmt, oldnick, newnick, NAME_CHANGE, 0, MAGENTA, gettext(" is now known as "));
}


static void send_group_message(ToxWindow *self, Tox *m, int groupnum, const char *msg)
{
    ChatContext *ctx = self->chatwin;

    if (msg == NULL) {
        wprintw(ctx->history, gettext("Invalid syntax.\n"));
        return;
    }

    if (tox_group_message_send(m, self->num, (uint8_t *) msg, strlen(msg)) == -1) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, RED, gettext(" * Failed to send message"));
        return;
    }

    char selfname[TOX_MAX_NAME_LENGTH];
    uint16_t len = tox_group_get_self_name(m, groupnum, (uint8_t *) selfname);
    selfname[len] = '\0';

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, selfname, NULL, OUT_MSG_READ, 0, 0, "%s", msg);
    write_to_log(msg, selfname, ctx->log, false);
}

static void send_group_action(ToxWindow *self, Tox *m, int groupnum, const char *action)
{
    ChatContext *ctx = self->chatwin;

    if (action == NULL) {
        wprintw(ctx->history, gettext("Invalid syntax.\n"));
        return;
    }

    if (tox_group_action_send(m, groupnum, (uint8_t *) action, strlen(action)) == -1) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, RED, gettext(" * Failed to send action"));
        return;
    }

    char selfname[TOX_MAX_NAME_LENGTH];
    uint16_t len = tox_group_get_self_name(m, groupnum, (uint8_t *) selfname);
    selfname[len] = '\0';

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, selfname, NULL, OUT_ACTION_READ, 0, 0, "%s", action);
    write_to_log(action, selfname, ctx->log, true);
}

static void send_group_prvt_message(ToxWindow *self, Tox *m, int groupnum, const char *data)
{
    ChatContext *ctx = self->chatwin;

    if (data == NULL) {
        wprintw(ctx->history, gettext("Invalid syntax.\n"));
        return;
    }

    size_t i;
    int peernum = -1, nick_len = 0;
    char *nick = NULL;

    /* need to match the longest nick in case of nicks that are smaller sub-strings */
    for (i = 0; i < groupchats[groupnum].num_peers; ++i) {
        if (memcmp((char *) &groupchats[groupnum].peer_names[i * TOX_MAX_NAME_LENGTH], data,
                             groupchats[groupnum].peer_name_lengths[i]) == 0) {
            if (groupchats[groupnum].peer_name_lengths[i] > nick_len) {
                nick_len = groupchats[groupnum].peer_name_lengths[i];
                nick = (char *) &groupchats[groupnum].peer_names[i * TOX_MAX_NAME_LENGTH];
                peernum = i;
            }
        }
    }

    if (peernum == -1) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, 0, gettext("Invalid peer name"));
        return;
    }

    int msg_len = strlen(data) - groupchats[groupnum].peer_name_lengths[peernum] - 1;

    if (msg_len <= 0)
        return;

    const char *msg = data + groupchats[groupnum].peer_name_lengths[peernum] + 1;

    if (tox_group_private_message_send(m, groupnum, peernum, (uint8_t *) msg, msg_len) == -1) {
        line_info_add(self, NULL, NULL, NULL, SYS_MSG, 0, RED, gettext(" * Failed to send private message"));
        return;
    }

    /* turn "peername" into ">peername<" to signify private message */
    char pm_nick[TOX_MAX_NAME_LENGTH + 2];
    strcpy(pm_nick, ">");
    strcpy(pm_nick + 1, nick);
    strcpy(pm_nick + 1 + groupchats[groupnum].peer_name_lengths[peernum], "<");

    char timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    line_info_add(self, timefrmt, pm_nick, NULL, OUT_PRVT_MSG, 0, 0, "%s", msg);
    write_to_log(msg, pm_nick, ctx->log, false);
}

static void groupchat_onKey(ToxWindow *self, Tox *m, wint_t key, bool ltr)
{
    ChatContext *ctx = self->chatwin;

    int x, y, y2, x2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);

    if (x2 <= 0)
        return;

    if (self->help->active) {
        help_onKey(self, key);
        return;
    }

    if (ltr) {    /* char is printable */
        input_new_char(self, key, x, y, x2, y2);
        return;
    }

    if (line_info_onKey(self, key))
        return;

    if (input_handle(self, key, x, y, x2, y2))
        return;

    if (key == '\t') {  /* TAB key: auto-completes peer name or command */
        if (ctx->len > 0) {
            int diff;

            /* TODO: make this not suck */
            if (ctx->line[0] != L'/' || wcschr(ctx->line, L' ') != NULL) {
                diff = complete_line(self, groupchats[self->num].peer_names, groupchats[self->num].num_peers,
                                     TOX_MAX_NAME_LENGTH);
            } else if (wcsncmp(ctx->line, L"/avatar \"", wcslen(L"/avatar \"")) == 0) {
                diff = dir_match(self, m, ctx->line, L"/avatar");
            } else {
                diff = complete_line(self, group_cmd_list, AC_NUM_GROUP_COMMANDS, MAX_CMDNAME_SIZE);
            }

            if (diff != -1) {
                if (x + diff > x2 - 1) {
                    int wlen = wcswidth(ctx->line, sizeof(ctx->line));
                    ctx->start = wlen < x2 ? 0 : wlen - x2 + 1;
                }
            } else {
                sound_notify(self, notif_error, 0, NULL);
            }
        } else {
            sound_notify(self, notif_error, 0, NULL);
        }
    } else if (key == user_settings->key_peer_list_down) {    /* Scroll peerlist up and down one position */
        int L = y2 - CHATBOX_HEIGHT - SDBAR_OFST;

        if (groupchats[self->num].side_pos < groupchats[self->num].num_peers - L)
            ++groupchats[self->num].side_pos;
    } else if (key == user_settings->key_peer_list_up) {
        if (groupchats[self->num].side_pos > 0)
            --groupchats[self->num].side_pos;
    } else if (key == '\n') {
        rm_trailing_spaces_buf(ctx);

        char line[MAX_STR_SIZE];

        if (wcs_to_mbs_buf(line, ctx->line, MAX_STR_SIZE) == -1)
            memset(&line, 0, sizeof(line));

        if (!string_is_empty(line))
            add_line_to_hist(ctx);

        if (line[0] == '/') {
            if (strncmp(line, "/close", strlen("/close")) == 0) {
                int offset = 6;

                if (line[offset] != '\0')
                    ++offset;

                exit_groupchat(self, m, self->num, line + offset, ctx->len - offset);
                return;
            } else if (strncmp(line, "/me ", strlen("/me ")) == 0) {
                send_group_action(self, m, self->num, line + 4);
            } else if (strncmp(line, "/whisper ", strlen("/whisper ")) == 0) {
                send_group_prvt_message(self, m, self->num, line + 9);
            } else {
                execute(ctx->history, self, m, line, GROUPCHAT_COMMAND_MODE);
            }
        } else if (!string_is_empty(line)) {
            send_group_message(self, m, self->num, line);
        }

        wclear(ctx->linewin);
        wmove(self->window, y2 - CURS_Y_OFFSET, 0);
        reset_buf(ctx);
    }
}

static void groupchat_onDraw(ToxWindow *self, Tox *m)
{
    int x2, y2;
    getmaxyx(self->window, y2, x2);

    ChatContext *ctx = self->chatwin;

    line_info_print(self);
    wclear(ctx->linewin);

    curs_set(1);

    if (ctx->len > 0)
        mvwprintw(ctx->linewin, 1, 0, "%ls", &ctx->line[ctx->start]);

    wclear(ctx->sidebar);
    mvwhline(self->window, y2 - CHATBOX_HEIGHT, 0, ACS_HLINE, x2);

    if (self->show_peerlist) {
        mvwvline(ctx->sidebar, 0, 0, ACS_VLINE, y2 - CHATBOX_HEIGHT);
        mvwaddch(ctx->sidebar, y2 - CHATBOX_HEIGHT, 0, ACS_BTEE);

        pthread_mutex_lock(&Winthread.lock);
        int num_peers = groupchats[self->num].num_peers;
        pthread_mutex_unlock(&Winthread.lock);

        wmove(ctx->sidebar, 0, 1);
        wattron(ctx->sidebar, A_BOLD);
        wprintw(ctx->sidebar, gettext("Peers: %d\n"), num_peers);
        wattroff(ctx->sidebar, A_BOLD);

        mvwaddch(ctx->sidebar, 1, 0, ACS_LTEE);
        mvwhline(ctx->sidebar, 1, 1, ACS_HLINE, SIDEBAR_WIDTH - 1);

        int maxlines = y2 - SDBAR_OFST - CHATBOX_HEIGHT;
        int i;

        for (i = 0; i < num_peers && i < maxlines; ++i) {
            wmove(ctx->sidebar, i + 2, 1);
            int peer = i + groupchats[self->num].side_pos;

            /* TODO: Make this not poll */
            pthread_mutex_lock(&Winthread.lock);
            uint8_t status = tox_group_get_status(m, self->num, i);
            TOX_GROUP_ROLE role = tox_group_get_role(m, self->num, i);
            pthread_mutex_unlock(&Winthread.lock);

            int maxlen_offset = role == TOX_GR_USER ? 2 : 3;

            /* truncate nick to fit in side panel without modifying list */
            char tmpnck[TOX_MAX_NAME_LENGTH];
            int maxlen = SIDEBAR_WIDTH - maxlen_offset;
            pthread_mutex_lock(&Winthread.lock);
            memcpy(tmpnck, &groupchats[self->num].peer_names[peer * TOX_MAX_NAME_LENGTH], maxlen);
            pthread_mutex_unlock(&Winthread.lock);
            tmpnck[maxlen] = '\0';

            int namecolour = WHITE;

            if (status == TOX_GS_AWAY)
                namecolour = YELLOW;
            else if (status == TOX_GS_BUSY)
                namecolour = RED;

            /* Signify roles (e.g. founder, moderator) */
            const char *rolesig = "";
            int rolecolour = WHITE;

            if (role == TOX_GR_FOUNDER) {
                rolesig = "&";
                rolecolour = BLUE;
            } else if (role == TOX_GR_MODERATOR) {
                rolesig = "+";
                rolecolour = GREEN;
            }

            wattron(ctx->sidebar, COLOR_PAIR(rolecolour) | A_BOLD);
            wprintw(ctx->sidebar, "%s", rolesig);
            wattroff(ctx->sidebar, COLOR_PAIR(rolecolour) | A_BOLD);

            wattron(ctx->sidebar, COLOR_PAIR(namecolour));
            wprintw(ctx->sidebar, "%s\n", tmpnck);
            wattroff(ctx->sidebar, COLOR_PAIR(namecolour));
        }
    }

    int y, x;
    getyx(self->window, y, x);
    (void) x;
    int new_x = ctx->start ? x2 - 1 : wcswidth(ctx->line, ctx->pos);
    wmove(self->window, y + 1, new_x);

    wrefresh(self->window);

    if (self->help->active)
        help_onDraw(self);
}

static void groupchat_onInit(ToxWindow *self, Tox *m)
{
    int x2, y2;
    getmaxyx(self->window, y2, x2);

    ChatContext *ctx = self->chatwin;

    ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT + 1, x2 - SIDEBAR_WIDTH - 1, 0, 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);
    ctx->sidebar = subwin(self->window, y2 - CHATBOX_HEIGHT + 1, SIDEBAR_WIDTH, 0, x2 - SIDEBAR_WIDTH);

    ctx->hst = calloc(1, sizeof(struct history));
    ctx->log = calloc(1, sizeof(struct chatlog));

    if (ctx->log == NULL || ctx->hst == NULL)
        exit_toxic_err(gettext("failed in groupchat_onInit"), FATALERR_MEMORY);

    line_info_init(ctx->hst);

    if (user_settings->autolog == AUTOLOG_ON) {
        char myid[TOX_ADDRESS_SIZE];
        tox_self_get_address(m, (uint8_t *) myid);
        log_enable(self->name, myid, NULL, ctx->log, LOG_GROUP);
    }

    execute(ctx->history, self, m, "/log", GLOBAL_COMMAND_MODE);

    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);
}

ToxWindow new_group_chat(Tox *m, int groupnum, const char *groupname, int length)
{
    ToxWindow ret;
    memset(&ret, 0, sizeof(ret));

    ret.active = true;
    ret.is_groupchat = true;

    ret.onKey = &groupchat_onKey;
    ret.onDraw = &groupchat_onDraw;
    ret.onInit = &groupchat_onInit;
    ret.onGroupMessage = &groupchat_onGroupMessage;
    ret.onGroupNamelistChange = &groupchat_onGroupNamelistChange;
    ret.onGroupPrivateMessage = &groupchat_onGroupPrivateMessage;
    ret.onGroupAction = &groupchat_onGroupAction;
    ret.onGroupPeerJoin = &groupchat_onGroupPeerJoin;
    ret.onGroupPeerExit = &groupchat_onGroupPeerExit;
    ret.onGroupTopicChange = &groupchat_onGroupTopicChange;
    ret.onGroupNickChange = &groupchat_onGroupNickChange;
    ret.onGroupSelfJoin = &groupchat_onGroupSelfJoin;
    ret.onGroupRejected = &groupchat_onGroupRejected;
    ret.onGroupModeration = &groupchat_onGroupModeration;

    ChatContext *chatwin = calloc(1, sizeof(ChatContext));
    Help *help = calloc(1, sizeof(Help));

    if (chatwin == NULL || help == NULL)
        exit_toxic_err(gettext("failed in new_group_chat"), FATALERR_MEMORY);

    ret.chatwin = chatwin;
    ret.help = help;

    ret.num = groupnum;
    ret.show_peerlist = true;
    ret.active_box = -1;

    if (groupname && length > 0)
        set_window_title(&ret, groupname, length);
    else
        snprintf(ret.name, sizeof(ret.name), gettext("Group %d"), groupnum);

    return ret;
}
