/*
 * bluetooth-ag-manager.c
 *
 * Copyright (c) 2014 - 2015 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:	Hocheol Seo <hocheol.seo@samsung.com>
 *		Chethan TN <chethan.tn@samsung.com>
 *		Chanyeol Park <chanyeol.park@samsung.com>
 *		Rakesh MK <rakesh.mk@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>
#include "bluetooth-ag-agent.h"
#include "bluetooth-ag-handler.h"
#include <dbus/dbus.h>

#define PHONEBOOK_AGENT_BUS_NAME "org.bluez.pb_agent"
#define PHONEBOOK_AGENT_PATH	 "/org/bluez/pb_agent"
#define PHONEBOOK_AGENT_INTERFACE "org.bluez.PbAgent.At"

struct telephony_call {
	char *call_path;
	int call_status;
	gboolean call_originating;
	gboolean call_emergency;
	gboolean call_on_hold;
	gboolean call_conference;
	char *call_number;
	gboolean call_setup;
	uint32_t call_id;
	char *call_sender;
};

#define HFP_AGENT_ACTIVITY_STATUS_READY 0
#define HFP_AGENT_ACTIVITY_STATUS_UNAVAILABLE 1
#define HFP_AGENT_ACTIVITY_STATUS_UNKNOWN 2
#define HFP_AGENT_ACTIVITY_STATUS_RINGING 3
#define HFP_AGENT_ACTIVITY_STATUS_CALL_IN_PROGRESS 4

#define HFP_AGENT_BATTERY_INDICATOR "battchg"
#define HFP_AGENT_CALL_INDICATOR		"call"
#define HFP_AGENT_CALLHELD_INDICATOR	"callheld"
#define HFP_AGENT_CALLSETUP_INDICATOR	"callsetup"
#define HFP_AGENT_ROAMING_INDICATOR "roam"
#define HFP_AGENT_SERVICE_INDICATOR  "service"
#define HFP_AGENT_SIGNAL_INDICATOR	"signal"

#define HFP_AGENT_CALL_IDLE  0
#define HFP_AGENT_CALL_ACTIVE 1

#define HFP_INCOMING_CALLSETUP	1
#define HFP_OUTGOING_CALLSETUP	2
#define RESTRAIN_CALL_FLAG 0x01
#define ALLOW_CALL_FLAG  0x02

#define HFP_CALL_STATUS_IDLE	0
#define HFP_CALL_STATUS_CREATE		1
#define HFP_CALL_STATUS_COMING		2
#define HFP_CALL_STATUS_PROCEEDING	3
#define HFP_CALL_STATUS_MO_ALERTING	4
#define HFP_CALL_STATUS_MT_ALERTING		5
#define HFP_CALL_STATUS_WAITING		6
#define HFP_CALL_STATUS_ANSWERED	7
#define HFP_CALL_STATUS_ACTIVE		8
#define HFP_CALL_STATUS_MO_RELEASE	9
#define HFP_CALL_STATUS_MT_RELEASE	10
#define HFP_CALL_STATUS_HOLD_INITIATED		11
#define HFP_CALL_STATUS_HOLD		12
#define HFP_CALL_STATUS_RETRIEVE_INITIATED	13
#define HFP_CALL_STATUS_RECONNECT_PENDING	14
#define HFP_CALL_STATUS_TERMINATED		15
#define HFP_CALL_STATUS_SWAP_INITIATED		16

#define AGENT_MAX_PB_COUNT		1000
#define AGENT_PB_NAME_MAX_LENGTH		20
#define AGENT_PB_NUMBER_MAX_LENGTH	20
#define AGENT_MAX_CALLLOG_COUNT		30
#define ERR_NOT_FOUND -1
#define AG_MAX_LENGTH 16

static gboolean update_events = FALSE;
static int caller_id = 0;

static GSList *call_senders_paths = NULL;
static GSList *existing_call_list = NULL;
static GSList *agent_active_call_list = NULL;
static char *ag_subscriber_num = NULL;

static guint call_on_hold_timer = 0;

typedef struct {
	gchar *sender_path;
	gchar *sender_name;
} sender_info_t;

static struct {
	char *network_operator_name;
	uint8_t network_status;
	int32_t signal_strength;
} network_info = {
	.network_operator_name = NULL,
	.network_status = BT_AGENT_NETWORK_REG_STATUS_UNKOWN,
	.signal_strength = 0,
};

static const char *agent_pb_store_list[] =  {
	"\"ME\"", "\"DC\"", "\"MC\"", "\"RC\""
};

static const char *agent_supported_character_set[] = {
	"\"UTF-8\"", "\"IRA\""
};

#if defined(TIZEN_WEARABLE) && defined(TIZEN_TELEPHONY_ENABLED)
static const char *ag_chld_str = "0,1,2";
#else
static const char *ag_chld_str = "0,1,2,3";
#endif

#define AGENT_PB_STORE_LIST_SIZE (sizeof(agent_pb_store_list) \
				/sizeof(const char *))
#define AGENT_SUPPORTED_CHARACTER_SET_SIZE ( \
		sizeof(agent_supported_character_set)/sizeof(const char *))

static bt_ag_indicators_t hfp_ag_ind[] = {
	{ "call", "0,1", 0, TRUE, TRUE },
	{ "callsetup", "0-3", 0 , TRUE, TRUE },
	{ "battchg", "0-5", 5 , TRUE, TRUE },
	{ "callheld", "0-2", 0 , FALSE, TRUE },
	{ "roam", "0,1", 0 , TRUE, TRUE },
	{ "signal", "0-5", 0 , TRUE, TRUE },
	{ "service",	"0,1", 0, TRUE, TRUE },
	{ NULL }
};

static struct {
	int32_t path_id;
	int32_t charset_id;
} ag_pb_info = {
	.path_id = 0,
	.charset_id = 0
};

static gboolean __bt_hfp_check_for_callpath(const char *call_path,
					const char *call_sender)
{
	GSList *sender_list = call_senders_paths;
	sender_info_t *sender;

	DBG("call path is  = %s\n", call_path);
	DBG("sender is  = %s\n", call_sender);

	if (call_path == NULL || call_sender == NULL) {

		ERR("Invalid Parameters");
		return FALSE;
	}

	/*check if the call is already registered*/
	DBG("Checking if the call is already registered");
	while (sender_list != NULL) {
		sender = sender_list->data;

		if (sender == NULL)
			break;

		if (g_strcmp0(sender->sender_path, call_path) == 0) {
			DBG("sender path and call path match... so return true");
			return TRUE;
		}

		sender_list = sender_list->next;
	}

	ERR("Call path is not already registered");
	return FALSE;
}

static void __bt_hfp_clear_sender_path(sender_info_t *s_path)
{
	if (s_path == NULL)
		return;

	g_free(s_path->sender_name);
	g_free(s_path->sender_path);
	g_free(s_path);

	if (g_slist_length(call_senders_paths) == 0) {
		g_slist_free(call_senders_paths);
		call_senders_paths = NULL;
	}
}

static void __bt_hfp_free_call(struct telephony_call *t_call)
{
	if (t_call == NULL)
		return;

	g_free(t_call->call_number);
	g_free(t_call->call_path);
	g_free(t_call->call_sender);
	g_free(t_call);
}

static void __bt_hfp_reset_indicators(void)
{
	int i;

	for (i = 0; hfp_ag_ind[i].indicator_desc != NULL; i++)
		hfp_ag_ind[i].is_activated = TRUE;
}

void _bt_hfp_device_disconnected(void *t_device)
{
	DBG("hfp_agent: device %p disconnected", t_device);
	update_events = FALSE;
	__bt_hfp_reset_indicators();
}

void _bt_hfp_initialize_telephony_manager(uint32_t ag_features)
{
	int index;
	int value;
	int ret;

	/* Reset the indicator values */
	for (index = 0; hfp_ag_ind[index].indicator_desc != NULL; index++) {
		if (g_str_equal(hfp_ag_ind[index].indicator_desc, "battchg")) {
			ret = vconf_get_int(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
									&value);
			if (ret != 0) {
				ERR("Get battery status failed : %d\n", ret);
			} else {
				/* Send battery status ranging from 0-5 */
				if (value < 5)
					hfp_ag_ind[index].hfp_value = 0;
				else if (value >= 100)
					hfp_ag_ind[index].hfp_value = 5;
				else
					hfp_ag_ind[index].hfp_value = value / 20 + 1;
			}
		} else if (g_str_equal(hfp_ag_ind[index].indicator_desc, "signal")) {
			ret = vconf_get_int(VCONFKEY_TELEPHONY_RSSI, &value);
			if (ret != 0) {
				ERR("Get signal status failed err = %d\n", ret);
			} else {
				BT_CHECK_SIGNAL_STRENGTH(value);
				hfp_ag_ind[index].hfp_value = value;
			}
		} else if (g_str_equal(hfp_ag_ind[index].indicator_desc, "roam")) {
			ret = vconf_get_int(VCONFKEY_TELEPHONY_SVC_ROAM, &value);
			if (ret != 0)
				ERR("Get roaming status failed err = %d\n", ret);
			else
				hfp_ag_ind[index].hfp_value = value;
		} else if (g_str_equal(hfp_ag_ind[index].indicator_desc, "service")) {
			ret = vconf_get_int(VCONFKEY_TELEPHONY_SVCTYPE, &value);
			if (ret != 0) {
				ERR("Get Service status failed : %d\n", ret);
			} else {
				switch (value) {
				case VCONFKEY_TELEPHONY_SVCTYPE_NONE:
				case VCONFKEY_TELEPHONY_SVCTYPE_NOSVC:
				case VCONFKEY_TELEPHONY_SVCTYPE_SEARCH:
					hfp_ag_ind[index].hfp_value =
						INDICATOR_EVENT_SERVICE_NONE;
					break;
				default:
					hfp_ag_ind[index].hfp_value =
						INDICATOR_EVENT_SERVICE_PRESENT;
					break;
				}
			}
		} else {
			hfp_ag_ind[index].hfp_value = 0;
		}
	}

	/*Initializatoin of the indicators*/
	_bt_hfp_set_ag_indicator(ag_features, hfp_ag_ind,
					BT_RSP_HOLD_NOT_SUPPORTED,
					ag_chld_str);
}

void _bt_hfp_deinitialize_telephony_manager(void)
{
	GSList *list = call_senders_paths;

	g_free(ag_subscriber_num);
	ag_subscriber_num = NULL;

	g_free(network_info.network_operator_name);
	network_info.network_operator_name = NULL;

	network_info.network_status = BT_AGENT_NETWORK_REG_STATUS_UNKOWN;
	network_info.signal_strength = 0;

	g_slist_free(agent_active_call_list);
	agent_active_call_list = NULL;

	g_slist_foreach(existing_call_list, (GFunc) __bt_hfp_free_call, NULL);
	g_slist_free(existing_call_list);
	existing_call_list = NULL;

	while (list != NULL) {
		__bt_hfp_clear_sender_path(list->data);
		list = list->next;
	}

	g_slist_free(call_senders_paths);
	call_senders_paths = NULL;

	_bt_hfp_deinitialize();
}

bt_hfp_agent_error_t _bt_hfp_register_telephony_agent(gboolean register_flag,
		const char *path_to_register,
		const char *sender)
{
	sender_info_t *sender_info;

	if (sender == NULL || path_to_register == NULL)
		return BT_HFP_AGENT_ERROR_INVALID_PARAM;

	DBG(" register_flag = %d", register_flag);
	DBG(" path_to_register = %s", path_to_register);
	DBG(" sender = %s", sender);

	if (register_flag) {
		if (__bt_hfp_check_for_callpath(path_to_register, sender))
			return BT_HFP_AGENT_ERROR_ALREADY_EXSIST;

		/* add call path to the senders list*/
		DBG("Call path doesn't exist. Add path %s to global path",
						path_to_register);
		sender_info = g_new0(sender_info_t, 1);
		sender_info->sender_path = g_strdup(path_to_register);
		sender_info->sender_name = g_strdup(sender);
		call_senders_paths = g_slist_append(call_senders_paths,
								sender_info);

		return BT_HFP_AGENT_ERROR_NONE;
	} else {
		/*remove the call from senders list */
		GSList *s_list = call_senders_paths;

		while (s_list != NULL) {
			sender_info = s_list->data;

			if (sender_info == NULL)
				return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

			if (g_strcmp0(sender_info->sender_path,
					path_to_register) == 0) {
				call_senders_paths = g_slist_remove(
							call_senders_paths,
							sender_info);
				__bt_hfp_clear_sender_path(sender_info);
				return BT_HFP_AGENT_ERROR_NONE;
			}
			s_list = s_list->next;
		}

		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
	}
}

static gboolean __bt_hfp_is_call_allowed(const char *call_path)
{
	GSList *call_list = existing_call_list;

	/*if prior call list doesn't exisit, allow the call as it can be a new-call*/
	if (!existing_call_list) {
		DBG(" This must be a new call... Allow it!");
		return TRUE;
	}

	while (call_list != NULL) {

		struct telephony_call *t_call = call_list->data;

		if (g_strcmp0(t_call->call_path, call_path) == 0)
			return TRUE;

		call_list = call_list->next;
	}

	ERR("call is not allowed");
	return FALSE;
}

static struct telephony_call *__bt_hfp_create_new_call(
					const char *incoming_path,
					uint32_t incoming_call_id,
					const char *incoming_number,
					const char *sender)
{
	struct telephony_call *t_call = NULL;
	GSList *call_list = existing_call_list;

	while (call_list != NULL) {
		t_call = call_list->data;

		if (t_call->call_id == incoming_call_id)
			break;
		else
			t_call = NULL;

		call_list = call_list->next;
	}

	DBG("Create a new call");

	if (t_call == NULL) {
		t_call = g_new0(struct telephony_call, 1);
		t_call->call_id = incoming_call_id;
		t_call->call_path = g_strdup(incoming_path);
		t_call->call_sender = g_strdup(sender);
		t_call->call_number = g_strdup(incoming_number);

		existing_call_list = g_slist_append(existing_call_list,
							t_call);
	}
	return t_call;
}

gboolean _bt_hfp_is_call_exist(void)
{
	DBG("_bt_hfp_is_call_exist [%x]", existing_call_list);
	if (existing_call_list)
		return TRUE;
	else
		return FALSE;
}

static struct telephony_call *__bt_hfp_get_call_with_status(int call_status)
{
	DBG("Get Call with status %d", call_status);

	GSList *temp_list = existing_call_list;

	if (existing_call_list != NULL) {
		while (temp_list != NULL) {
			struct telephony_call *t_call = temp_list->data;
			if (t_call->call_status == call_status)
				return t_call;
			temp_list = temp_list->next;
		}
	}

	DBG("Existing call list is NULL. So return NULL");
	return NULL;
}

static bt_hfp_agent_error_t __bt_hfp_modify_indicator(
			const char *indicator_name,
			int update_value)
{
	bt_ag_indicators_t *hf_ind = NULL;
	int i;

	for (i = 0; hfp_ag_ind[i].indicator_desc != NULL; i++) {
		if (g_str_equal(hfp_ag_ind[i].indicator_desc,
						indicator_name)) {
			hf_ind = &hfp_ag_ind[i];
			break;
		}
	}

	if (hf_ind == NULL)
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	if (hf_ind->hfp_value == update_value && hf_ind->ignore)
		return BT_HFP_AGENT_ERROR_NONE;

	if (hf_ind->is_activated ==  FALSE)
		return BT_HFP_AGENT_ERROR_NONE;

	hf_ind->hfp_value = update_value;

	DBG("updating hfp event indicator [%s] with value [%d]",
				indicator_name, hf_ind->hfp_value);

	return _bt_hfp_event_indicator(i);
}

static int __bt_hfp_get_indicator_value(
			const bt_ag_indicators_t *ag_indicators,
			const char *hf_desc)
{
	int x;
	for (x = 0; ag_indicators[x].indicator_desc != NULL; x++) {
		if (g_str_equal(ag_indicators[x].indicator_desc, hf_desc))
			return ag_indicators[x].hfp_value;
	}

	return ERR_NOT_FOUND;
}

static void __bt_hfp_handle_call_conference(void)
{
	GSList *t_call_list;
	struct telephony_call *t_active_call = NULL;
	int t_active_call_count = 0;

	struct telephony_call *t_held_call = NULL;
	int t_held_call_count = 0;

	for (t_call_list = existing_call_list; t_call_list != NULL;
		t_call_list = t_call_list->next) {

		struct telephony_call *t_call = t_call_list->data;

		if (t_call->call_status == HFP_CALL_STATUS_ACTIVE) {
			if (t_active_call == NULL)
				t_active_call = t_call;

			t_active_call_count++;

			if (t_active_call_count >= 2) {
				if (!t_active_call->call_conference)
					t_active_call->call_conference = TRUE;
				t_call->call_conference = TRUE;
			}

		} else if (t_call->call_status == HFP_CALL_STATUS_HOLD) {
			if (t_held_call == NULL)
				t_held_call = t_call;

			t_held_call_count++;

			if (t_held_call_count >= 2) {
				if (!t_held_call->call_conference)
					t_held_call->call_conference = TRUE;
				t_call->call_conference = TRUE;
			}
		}
	}

	if (t_held_call_count == 1) {
			if (t_held_call->call_conference)
				t_held_call->call_conference = FALSE;
		}

	if (t_active_call_count == 1) {
		if (t_active_call->call_conference)
			t_active_call->call_conference = FALSE;
	}
}

static gboolean __bt_hfp_on_call_hold_timeout(gpointer t_data)
{
	int status;

	if (__bt_hfp_get_call_with_status(HFP_CALL_STATUS_HOLD)) {
		if (__bt_hfp_get_call_with_status(HFP_CALL_STATUS_ACTIVE))
			status = INDICATOR_EVENT_CALLHELD_MULTIPLE;
		else
			status = INDICATOR_EVENT_CALLHELD_ON_HOLD;
	} else {
		status = INDICATOR_EVENT_CALLHELD_NONE;
	}

	__bt_hfp_modify_indicator("callheld", status);

	call_on_hold_timer = 0;
	return FALSE;
}

static void __bt_hfp_handle_call_on_hold_request(void)
{
	DBG(" Starting the timer for call on hold");
	if (call_on_hold_timer)
		g_source_remove(call_on_hold_timer);

	call_on_hold_timer = g_timeout_add(250, __bt_hfp_on_call_hold_timeout,
							NULL);
	DBG(" returning from the timer call");
}

static void __bt_hfp_set_call_status(struct telephony_call *t_call,
				int call_status)
{
	int call_held = 0;
	int org_status = t_call->call_status;

	call_held = __bt_hfp_get_indicator_value(hfp_ag_ind, "callheld");

	if (org_status == call_status) {
		DBG("Ignore the CSD Call state change to existing state");
		return;
	}

	t_call->call_status = call_status;

	DBG(" call status is   %d", call_status);

	switch (call_status) {
	case HFP_CALL_STATUS_IDLE:
		if (t_call->call_setup) {
			__bt_hfp_modify_indicator("callsetup",
				INDICATOR_EVENT_CALLSETUP_INACTIVE);
			if (!t_call->call_originating)
				_bt_calling_stopped_indicator();
		}

		g_free(t_call->call_number);
		t_call->call_number = NULL;
		t_call->call_originating = FALSE;
		t_call->call_emergency = FALSE;
		t_call->call_on_hold = FALSE;
		t_call->call_conference = FALSE;
		t_call->call_setup = FALSE;
		break;

	case HFP_CALL_STATUS_COMING:
		t_call->call_originating = FALSE;
		t_call->call_setup = TRUE;
		__bt_hfp_modify_indicator("callsetup",
					INDICATOR_EVENT_CALLSETUP_INCOMING);
		break;

	case HFP_CALL_STATUS_CREATE:
		t_call->call_originating = TRUE;
		t_call->call_setup = TRUE;
		break;

	case HFP_CALL_STATUS_MO_ALERTING:
		__bt_hfp_modify_indicator("callsetup",
					INDICATOR_EVENT_CALLSETUP_ALERTING);
		break;

	case HFP_CALL_STATUS_MT_ALERTING: {
		int  t_number = AGENT_NUMBER_TYPE_TELEPHONY;

		if (t_call->call_number == NULL) {
			t_number = AGENT_NUMBER_TYPE_TELEPHONY;
		} else {
			if (t_call->call_number[0] == '+' ||
				strncmp(t_call->call_number, "00", 2) == 0)
				t_number = AGENT_NUMBER_TYPE_INTERNATIONAL;
		}

		if (org_status == HFP_CALL_STATUS_WAITING)
			_bt_incoming_call_indicator(t_call->call_number,
						t_number);
	}
		break;

	case HFP_CALL_STATUS_ACTIVE:
		DBG(" This is an Active call");
		if (t_call->call_on_hold) {
			t_call->call_on_hold = FALSE;
			__bt_hfp_handle_call_on_hold_request();
		} else {
			if (!g_slist_find(agent_active_call_list, t_call)) {
				DBG(" This call is not in the active call list. So Add it to the list.\n");
				agent_active_call_list =
					g_slist_prepend(agent_active_call_list,
							t_call);
			}
			if (g_slist_length(agent_active_call_list) == 1) {
				DBG(" Update indicator to show the call presence.\n");
				__bt_hfp_modify_indicator("call",
						INDICATOR_EVENT_CALL_ACTIVE);
			}

			__bt_hfp_modify_indicator("callsetup",
					INDICATOR_EVENT_CALLSETUP_INACTIVE);
			__bt_hfp_handle_call_on_hold_request();

			if (!t_call->call_originating)
				_bt_calling_stopped_indicator();

			t_call->call_setup = FALSE;
		}
		break;

	case HFP_CALL_STATUS_MO_RELEASE:
	case HFP_CALL_STATUS_MT_RELEASE:
		agent_active_call_list = g_slist_remove(agent_active_call_list,
							t_call);
		if (g_slist_length(agent_active_call_list) == 0)
			__bt_hfp_modify_indicator("call",
					INDICATOR_EVENT_CALL_INACTIVE);

		if (org_status == HFP_CALL_STATUS_HOLD) {
			__bt_hfp_modify_indicator("callheld", INDICATOR_EVENT_CALLHELD_NONE);
		}

		if ((org_status == HFP_CALL_STATUS_MO_ALERTING) ||
			(org_status == HFP_CALL_STATUS_COMING) ||
			(org_status == HFP_CALL_STATUS_CREATE) ||
			(org_status == HFP_CALL_STATUS_WAITING)) {
				__bt_hfp_modify_indicator("callsetup",
					INDICATOR_EVENT_CALLSETUP_INACTIVE);
		}

		if (org_status == HFP_CALL_STATUS_COMING) {
			if (!t_call->call_originating)
				_bt_calling_stopped_indicator();
		}
		existing_call_list = g_slist_remove(existing_call_list, t_call);
		__bt_hfp_free_call(t_call);
		break;

	case HFP_CALL_STATUS_HOLD:
		t_call->call_on_hold = TRUE;
		__bt_hfp_handle_call_on_hold_request();
		break;

	case HFP_CALL_STATUS_TERMINATED:
		if (t_call->call_on_hold &&
		!__bt_hfp_get_call_with_status(HFP_CALL_STATUS_HOLD)) {
			__bt_hfp_modify_indicator("callheld",
					INDICATOR_EVENT_CALLHELD_NONE);
			return;
		}

		if (call_held == INDICATOR_EVENT_CALLHELD_MULTIPLE &&
		__bt_hfp_get_call_with_status(HFP_CALL_STATUS_HOLD) &&
		!__bt_hfp_get_call_with_status(HFP_CALL_STATUS_ACTIVE))
			__bt_hfp_modify_indicator("callheld",
				INDICATOR_EVENT_CALLHELD_ON_HOLD);
		break;

	case HFP_CALL_STATUS_PROCEEDING:
	case HFP_CALL_STATUS_SWAP_INITIATED:
	case HFP_CALL_STATUS_RETRIEVE_INITIATED:
	case HFP_CALL_STATUS_RECONNECT_PENDING:
	case HFP_CALL_STATUS_HOLD_INITIATED:
	case HFP_CALL_STATUS_WAITING:
	case HFP_CALL_STATUS_ANSWERED:
		break;

	default:
		break;
	}

	/* Update the call conference status for each of the call */
	__bt_hfp_handle_call_conference();
}

bt_hfp_agent_error_t _bt_hfp_incoming_call(const char *call_path,
		const char *incoming_number,
		uint32_t incoming_call_id,
		const char *sender)
{
	struct telephony_call *t_call = NULL;
	bt_hfp_agent_error_t hfp_err = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
	int t_number = AGENT_NUMBER_TYPE_TELEPHONY;
	int error;

	if (sender == NULL || call_path == NULL)
		return BT_HFP_AGENT_ERROR_INVALID_PARAM;

	if (!__bt_hfp_check_for_callpath(call_path, sender))
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	if (!__bt_hfp_is_call_allowed(call_path))
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	/* Its a new call, so create a list for it*/
	t_call = __bt_hfp_create_new_call(call_path, incoming_call_id,
						incoming_number,
						sender);

	/*get the type of the incoming number*/
	if (t_call->call_number == NULL) {
		t_number = AGENT_NUMBER_TYPE_TELEPHONY;
		ERR("call_number is NULL");
	} else {
		if (t_call->call_number[0] == '+' || strncmp(
					t_call->call_number, "00", 2) == 0)
			t_number = AGENT_NUMBER_TYPE_INTERNATIONAL;

		if (__bt_hfp_get_call_with_status(HFP_CALL_STATUS_ACTIVE) ||
				__bt_hfp_get_call_with_status(HFP_CALL_STATUS_HOLD)) {
			error = _bt_call_waiting_indicator(t_call->call_number,
								t_number);
			if (error != 0)
				ERR(" Fail to update CCWA information");

			if (update_events) {
				hfp_err = __bt_hfp_modify_indicator(
					HFP_AGENT_CALLSETUP_INDICATOR,
					HFP_INCOMING_CALLSETUP);
				if (hfp_err  != BT_HFP_AGENT_ERROR_NONE)
					ERR("Failed to update the indicators");
			}
			__bt_hfp_set_call_status(t_call, HFP_CALL_STATUS_WAITING);
		} else {
			DBG(" It is an incoming call");

			if (update_events) {
				hfp_err = __bt_hfp_modify_indicator(
					HFP_AGENT_CALLSETUP_INDICATOR,
					HFP_INCOMING_CALLSETUP);
				if (hfp_err  != BT_HFP_AGENT_ERROR_NONE)
					ERR("Failed to update the indicators");
			}

			error = _bt_incoming_call_indicator(t_call->call_number,
								t_number);

			__bt_hfp_set_call_status(t_call, HFP_CALL_STATUS_COMING);

			if (error == -ENODEV)
				return BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			else if (error == -EBUSY)
				return BT_HFP_AGENT_ERROR_BUSY;
		}
	}

	return hfp_err;
}

bt_hfp_agent_error_t _bt_hfp_outgoing_call(const char *call_path,
				const char *number,
				uint32_t call_id, const char *sender)
{
	struct telephony_call *t_call = NULL;
	bt_hfp_agent_error_t ret =  BT_HFP_AGENT_ERROR_NONE;
	gboolean err = FALSE;


	err = __bt_hfp_check_for_callpath(call_path, sender);
	if (!err)
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	/*check if the call_path exisits in the active call list, if not
	don't allow as the call may be initated by some other application*/

	err = __bt_hfp_is_call_allowed(call_path);
	if (!err)
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	/* create a new call for the call_path */
	t_call = __bt_hfp_create_new_call(call_path, call_id, number, sender);

	__bt_hfp_set_call_status(t_call, HFP_CALL_STATUS_CREATE);

	ret = __bt_hfp_modify_indicator(HFP_AGENT_CALLSETUP_INDICATOR,
					HFP_OUTGOING_CALLSETUP);
	if (ret != BT_HFP_AGENT_ERROR_NONE)
		DBG("Error in updating indicator");

	return ret;
}

bt_hfp_agent_error_t _bt_hfp_change_call_status(const char *call_path,
		const char *number, uint32_t call_status,
		uint32_t call_id, const char *sender)
{
	GSList *call_list = existing_call_list;
	struct telephony_call *t_call = NULL;
	gboolean ret = FALSE;

	if (call_status > AG_MAX_LENGTH)
		return BT_HFP_AGENT_ERROR_INVALID_PARAM;

	ret = __bt_hfp_check_for_callpath(call_path, sender);

	if (!ret)
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	ret = __bt_hfp_is_call_allowed(call_path);
	if (!ret)
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	/* find call with the given call_id*/
	DBG(" Find call with the given call Id from the list");
	while (call_list != NULL) {
		t_call = call_list->data;

		if (t_call->call_id == call_id) {
			DBG("Call Id Match");
			break;
		} else {
			t_call = NULL;
		}

		call_list = call_list->next;
	}

	if (t_call == NULL) {
		DBG("t_call is NULL. So create new call");
		t_call = __bt_hfp_create_new_call(call_path,
					call_id, number, sender);
	}

	__bt_hfp_set_call_status(t_call, call_status);

	return BT_HFP_AGENT_ERROR_NONE;
}

static int __bt_hfp_update_battery_strength(int32_t battery_strength)
{
	int bat_strength = 0;
	int x, change_value;

	DBG(" Battery strength is.... %d", battery_strength);

	/* get the current battery level */
	for (x = 0; hfp_ag_ind[x].indicator_desc != NULL; x++) {
		if (g_str_equal(hfp_ag_ind[x].indicator_desc, "battchg"))
			bat_strength = hfp_ag_ind[x].hfp_value;
	}

	/* We need to send battery status ranging from 0-5 */
	if (battery_strength < 5)
		 change_value = 0;
	else if (battery_strength >= 100)
		change_value = 5;
	else
		change_value = battery_strength / 20 + 1;

	if (bat_strength == change_value) {
		DBG("no change in battery strength");
		return 0;
	}

	if (__bt_hfp_modify_indicator("battchg",
			change_value) == BT_HFP_AGENT_ERROR_NONE)
		return 1;

	return 0;
}

static int __bt_hfp_update_signal_strength(int32_t signal_strength_bars)
{
	if (signal_strength_bars < 0)
		signal_strength_bars = 0;
	else if (signal_strength_bars > 5)
		signal_strength_bars = 5;

	if (network_info.signal_strength == signal_strength_bars) {
		DBG("no change in signal strength");
		return 0;
	}

	network_info.signal_strength = signal_strength_bars;

	if (__bt_hfp_modify_indicator("signal",
			signal_strength_bars) == BT_HFP_AGENT_ERROR_NONE)
		return 1;

	return 0;
}

static int __bt_hfp_update_registration_status(uint8_t register_status)
{
	bt_hfp_agent_error_t reg_ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;

	DBG("Updating registration status to.... %d", register_status);

	if (network_info.network_status == register_status) {
		DBG("No change in registration status");
		return 0;
	}

	if (register_status == BT_AGENT_NETWORK_REG_STATUS_ROAMING) {
		reg_ret = __bt_hfp_modify_indicator("roam",
					INDICATOR_EVENT_ROAM_ACTIVE);

		if (network_info.network_status >
					BT_AGENT_NETWORK_REG_STATUS_ROAMING)
			reg_ret = __bt_hfp_modify_indicator("service",
					INDICATOR_EVENT_SERVICE_PRESENT);
	} else if (register_status == BT_AGENT_NETWORK_REG_STATUS_HOME) {
		reg_ret = __bt_hfp_modify_indicator("roam",
					INDICATOR_EVENT_ROAM_INACTIVE);

		if (network_info.network_status >
					BT_AGENT_NETWORK_REG_STATUS_ROAMING)
			reg_ret = __bt_hfp_modify_indicator("service",
					INDICATOR_EVENT_SERVICE_PRESENT);
	} else if (register_status == BT_AGENT_NETWORK_REG_STATUS_OFFLINE ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_SEARCHING ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_NO_SIM ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_POWEROFF ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_POWERSAFE ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_NO_COVERAGE ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_REJECTED ||
		register_status == BT_AGENT_NETWORK_REG_STATUS_UNKOWN) {
		if (network_info.network_status <
					BT_AGENT_NETWORK_REG_STATUS_OFFLINE)
			reg_ret = __bt_hfp_modify_indicator("service",
						INDICATOR_EVENT_SERVICE_NONE);
	}

	network_info.network_status = register_status;
	if (reg_ret == BT_HFP_AGENT_ERROR_NONE)
		return 1;

	return 0;
}

int _bt_hfp_set_property_value(const char *property, int value)
{
	int ret = 0;

	DBG("Property is %s", property);

	if (g_str_equal("RegistrationChanged", property))
		ret = __bt_hfp_update_registration_status(value);

	else if (g_str_equal("SignalBarsChanged", property))
		ret = __bt_hfp_update_signal_strength(value);

	else if (g_str_equal("BatteryBarsChanged", property))
		ret = __bt_hfp_update_battery_strength(value);

	return ret;
}

int _bt_hfp_set_property_name(const char *property, const char *operator_name)
{
	int ret = 0;

	if (operator_name == NULL)
		return 0;

	if (g_str_equal("OperatorNameChanged", property)) {
		g_free(network_info.network_operator_name);
		network_info.network_operator_name =
				g_strndup(operator_name, 16);
		ret = 1;
	}

	if (g_str_equal("SubscriberNumberChanged", property)) {
		g_free(ag_subscriber_num);
		ag_subscriber_num = g_strdup(operator_name);
		DBG("HFP: subscriber_number updated: %s", ag_subscriber_num);
		ret = 1;
	}
	return ret;
}

static int __bt_hfp_answer_call(struct telephony_call *t_call)
{
	if (t_call->call_id != 0 && t_call->call_path != NULL &&
		t_call->call_sender != NULL) {
		_bt_ag_agent_answer_call(t_call->call_id,
						t_call->call_path,
						t_call->call_sender);
		return 0;
	}
	return -1;
}

void _bt_hfp_answer_call_request(void *t_device)
{
	struct telephony_call *t_call;

	t_call = __bt_hfp_get_call_with_status(HFP_CALL_STATUS_COMING);

	if (t_call == NULL)
		t_call = __bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_MT_ALERTING);

	if (t_call == NULL)
		t_call = __bt_hfp_get_call_with_status(
			HFP_CALL_STATUS_PROCEEDING);

	if (t_call == NULL)
		t_call = __bt_hfp_get_call_with_status(
					HFP_CALL_STATUS_WAITING);

	if (t_call == NULL) {
		_bt_answer_call_response(t_device,
					HFP_STATE_MNGR_ERR_NOT_ALLOWED);
		return;
	}

	if (__bt_hfp_answer_call(t_call) <  0)
		_bt_answer_call_response(t_device,
				HFP_STATE_MNGR_ERR_AG_FAILURE);
	else
		_bt_answer_call_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}


void _bt_hfp_dial_number_request(const char *dial_number, void *t_device)
{
	int call_flag = caller_id;
	bt_hfp_agent_error_t error_code = 0;

	if (strncmp(dial_number, "#31#", 4) == 0) {
		dial_number = dial_number + 4;
		call_flag = ALLOW_CALL_FLAG;
	} else if (strncmp(dial_number, "*31#", 4) == 0) {
		dial_number = dial_number + 4;
		call_flag = RESTRAIN_CALL_FLAG;
	} else if (dial_number[0] == '>') {
		int dial_location = strtol(&dial_number[1], NULL, 0);

		error_code = _bt_ag_agent_dial_memory(dial_location);

		if (error_code ==  BT_HFP_AGENT_ERROR_NONE)
			_bt_dial_number_response(t_device,
					HFP_STATE_MNGR_ERR_NONE);
		else
			_bt_dial_number_response(t_device,
				HFP_STATE_MNGR_ERR_AG_FAILURE);
		return;
	}

	error_code = _bt_ag_agent_dial_num(dial_number, call_flag);

	if (error_code == BT_HFP_AGENT_ERROR_NONE) {
		_bt_dial_number_response(t_device, HFP_STATE_MNGR_ERR_NONE);
		return;
	}

	_bt_dial_number_response(t_device, HFP_STATE_MNGR_ERR_AG_FAILURE);

}

void _bt_hfp_update_event_request(int indicator, void *t_device)
{
	if (indicator == 1)
		update_events = TRUE;
	else
		update_events = FALSE;

	_bt_event_reporting_response(t_device,
					HFP_STATE_MNGR_ERR_NONE);
}

static int __bt_bt_hfp_reject_call(struct telephony_call *t_call)
{
	gboolean ret;

	if (t_call != NULL) {
		DBG(" rejecting call from sender %s with call path %s and call id %d",
				t_call->call_sender,
				t_call->call_path,
				t_call->call_id);

		ret = _bt_ag_agent_reject_call(t_call->call_id,
						t_call->call_path,
						t_call->call_sender);
		if (ret)
			return 0;
	}

	return -1;
}

static int __bt_hfp_release_call(struct telephony_call *t_call)
{
	gboolean ret = _bt_ag_agent_release_call(t_call->call_id,
					t_call->call_path,
					t_call->call_sender);
	if (!ret)
		return -1;

	return 0;
}

static int __bt_hfp_release_conference(void)
{
	GSList *temp_list = existing_call_list;

	while (temp_list != NULL) {
		struct telephony_call *t_call = temp_list->data;

		if (t_call->call_conference)
			__bt_hfp_release_call(t_call);

		temp_list = temp_list->next;
	}
	return 0;
}

void _bt_hfp_terminate_call_request(void *t_device)
{
	struct telephony_call *t_call;
	struct telephony_call *t_alert = NULL;
	int t_error = 0;

	t_call = __bt_hfp_get_call_with_status(HFP_CALL_STATUS_ACTIVE);

	if (t_call == NULL) {
		DBG("Find non-idle call");
		GSList *temp_call_list = existing_call_list;
		while (temp_call_list != NULL) {
			t_call = temp_call_list->data;

			if (t_call->call_status == HFP_AGENT_CALL_IDLE)
				temp_call_list = temp_call_list->next;
			else
				break;
		}
	}

	if (t_call == NULL) {
		DBG("Seems like there are no active calls. So do not allow the call");
		_bt_terminate_call_response(t_device,
					HFP_STATE_MNGR_ERR_NOT_ALLOWED);
		return;
	}

	if (__bt_hfp_get_call_with_status(HFP_CALL_STATUS_WAITING) != NULL) {
		int value = 1;
		t_error = _bt_ag_agent_threeway_call(value, t_call->call_path,
				t_call->call_sender);
	} else if ((t_alert = __bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_CREATE))
		!= NULL) {
		t_error = __bt_bt_hfp_reject_call(t_alert);
	} else if ((t_alert = __bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_MO_ALERTING))
		!= NULL) {
		t_error = __bt_bt_hfp_reject_call(t_alert);
	} else if	((t_alert =  __bt_hfp_get_call_with_status(
			HFP_CALL_STATUS_COMING)) != NULL) {
		t_error = __bt_bt_hfp_reject_call(t_alert);
	} else if (t_call->call_conference)
		t_error = __bt_hfp_release_conference();
	else
		t_error = __bt_hfp_release_call(t_call);

	if (t_error < 0)
		_bt_terminate_call_response(t_device,
				HFP_STATE_MNGR_ERR_AG_FAILURE);
	else
		_bt_terminate_call_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_call_hold_request(const char *t_cmd, void *t_device)
{

	struct telephony_call *t_call = NULL;
	GSList *t_sender_list = call_senders_paths;
	sender_info_t *sender_info = NULL;
	uint32_t t_chld_value;

	t_call = __bt_hfp_get_call_with_status(HFP_CALL_STATUS_ACTIVE);
	if (t_call == NULL) {
		if ((t_call =
			__bt_hfp_get_call_with_status(HFP_CALL_STATUS_HOLD))
			== NULL) {
			if ((t_call = __bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_WAITING)) == NULL) {
				/* means there is no outgoing call*/
				_bt_call_hold_response(t_device,
					HFP_STATE_MNGR_ERR_AG_FAILURE);
				return;
			}
		}
	}

	while (t_sender_list != NULL) {
		sender_info = t_sender_list->data;
		if (sender_info == NULL) {
			_bt_call_hold_response(t_device,
				HFP_STATE_MNGR_ERR_AG_FAILURE);
			return;
		}
		if (g_strcmp0(t_call->call_path, sender_info->sender_path)
				== 0)
			break;

		t_sender_list = t_sender_list->next;
	}

	t_chld_value = strtoul(&t_cmd[0], NULL, 0);
	gboolean ret = _bt_ag_agent_threeway_call(t_chld_value,
			t_call->call_path,
			t_call->call_sender);

	if (ret == TRUE)
		_bt_call_hold_response(t_device, HFP_STATE_MNGR_ERR_NONE);
	else {
		_bt_call_hold_response(t_device, HFP_STATE_MNGR_ERR_AG_FAILURE);
	}
}

void _bt_hfp_key_press_request(const char *t_key_press, void *t_device)
{
	struct telephony_call *t_active_call;
	struct telephony_call *t_waiting_call;
	int t_error = 0;

	t_waiting_call = __bt_hfp_get_call_with_status(HFP_CALL_STATUS_COMING);

	if (t_waiting_call == NULL)
		t_waiting_call = __bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_MT_ALERTING);

	if (t_waiting_call == NULL)
		t_waiting_call = __bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_PROCEEDING);

	t_active_call = __bt_hfp_get_call_with_status(HFP_CALL_STATUS_ACTIVE);


	if (t_waiting_call != NULL)
		t_error = __bt_hfp_answer_call(t_waiting_call);
	else if (t_active_call != NULL)
		t_error = __bt_hfp_release_call(t_active_call);
	else {
		if (_bt_ag_agent_dial_last_num(t_device) !=
				BT_HFP_AGENT_ERROR_NONE)
			_bt_dial_number_response(t_device,
						HFP_STATE_MNGR_ERR_AG_FAILURE);
		else
			_bt_dial_number_response(t_device,
						HFP_STATE_MNGR_ERR_NONE);
		return;
	}

	if (t_error < 0)
		_bt_key_press_response(t_device,
					HFP_STATE_MNGR_ERR_AG_FAILURE);
	else
		_bt_key_press_response(t_device,
						HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_last_dialed_number_request(void *t_device)
{
	bt_hfp_agent_error_t error = _bt_ag_agent_dial_last_num(t_device);

	if (error != BT_HFP_AGENT_ERROR_NONE)
		_bt_dial_number_response(t_device,
					HFP_STATE_MNGR_ERR_AG_FAILURE);
	else
		_bt_dial_number_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_channel_dtmf_request(char t_tone, void *t_device)
{
	char buf[2] = { t_tone, '\0' };
	char *tone_buffer = buf;

	struct telephony_call *t_call = __bt_hfp_get_call_with_status(
					HFP_CALL_STATUS_ACTIVE);
	if (t_call == NULL) {
		t_call = __bt_hfp_get_call_with_status(HFP_CALL_STATUS_HOLD);
		if (t_call == NULL) {
			t_call = __bt_hfp_get_call_with_status(
					HFP_CALL_STATUS_WAITING);
			if (t_call == NULL) {
				/* if this point is reached,
				it means there is no ongoing call */
				_bt_transmit_dtmf_response(t_device,
						HFP_STATE_MNGR_ERR_AG_FAILURE);
				return;
			}
		}
	}

	if (_bt_ag_agent_send_dtmf(tone_buffer, t_call->call_path,
		t_call->call_sender) != BT_HFP_AGENT_ERROR_NONE) {
		_bt_transmit_dtmf_response(t_device,
					HFP_STATE_MNGR_ERR_AG_FAILURE);
			return;
		}

	_bt_transmit_dtmf_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_vendor_cmd_request(const char *cmd,
						void *t_device)
{
	GSList *t_sender_list = call_senders_paths;
	sender_info_t *sender_info = NULL;
	GSList *l;
	bt_hfp_agent_error_t error = BT_HFP_AGENT_ERROR_NONE;

	if (NULL != t_sender_list) {
		for (l = t_sender_list; l != NULL; l = l->next) {
			sender_info = l->data;
			error = _bt_ag_agent_vendor_cmd(t_device,
				sender_info->sender_path,
				sender_info->sender_name);
			if (error != BT_HFP_AGENT_ERROR_NONE)
				break;
		}
	}

	if (error != BT_HFP_AGENT_ERROR_NONE)
		_bt_vendor_cmd_response(t_device,
					HFP_STATE_MNGR_ERR_AG_FAILURE);
	else
		_bt_vendor_cmd_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_subscriber_number_request(void *t_device)
{
	if (ag_subscriber_num != NULL) {

		int  t_number =  AGENT_NUMBER_TYPE_TELEPHONY;

		if (ag_subscriber_num[0] == '+' || strncmp(
					ag_subscriber_num, "00", 2) == 0)
			t_number = AGENT_NUMBER_TYPE_INTERNATIONAL;

		_bt_subscriber_number_indicator(ag_subscriber_num,
			t_number, AGENT_SUBSCRIBER_SERVICE_VOICE);
	}

	_bt_subscriber_number_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}

static int __bt_hfp_get_call_status(struct telephony_call *t_call)
{
	switch (t_call->call_status) {
	case HFP_CALL_STATUS_IDLE:
	case HFP_CALL_STATUS_MO_RELEASE:
	case HFP_CALL_STATUS_MT_RELEASE:
	case HFP_CALL_STATUS_TERMINATED:
		return -1;

	case HFP_CALL_STATUS_ANSWERED:
	case HFP_CALL_STATUS_ACTIVE:
	case HFP_CALL_STATUS_RECONNECT_PENDING:
	case HFP_CALL_STATUS_SWAP_INITIATED:
	case HFP_CALL_STATUS_HOLD_INITIATED:
		return AGENT_CALL_STATUS_ACTIVE;

	case HFP_CALL_STATUS_RETRIEVE_INITIATED:
	case HFP_CALL_STATUS_HOLD:
		return AGENT_CALL_STATUS_HELD;

	case  HFP_CALL_STATUS_WAITING:
		return AGENT_CALL_STATUS_WAITING;

	case HFP_CALL_STATUS_CREATE:
		return AGENT_CALL_STATUS_DIALING;

	case HFP_CALL_STATUS_PROCEEDING:
		if (t_call->call_originating)
			return AGENT_CALL_STATUS_DIALING;
		if (g_slist_length(agent_active_call_list) > 0)
			return AGENT_CALL_STATUS_WAITING;
		else
			return AGENT_CALL_STATUS_INCOMING;

	case HFP_CALL_STATUS_COMING:
		if (g_slist_length(agent_active_call_list) > 0)
			return AGENT_CALL_STATUS_WAITING;
		else
			return AGENT_CALL_STATUS_INCOMING;

	case HFP_CALL_STATUS_MO_ALERTING:
		return AGENT_CALL_STATUS_ALERTING;

	case HFP_CALL_STATUS_MT_ALERTING:
		return AGENT_CALL_STATUS_INCOMING;

	default:
		return -1;
	}
}

void _bt_list_current_calls(void *t_device)
{
	GSList *t_call_list =  existing_call_list;
	int t_status;
	int t_number = AGENT_NUMBER_TYPE_TELEPHONY;
	int t_direction, t_call_conference;
	int index;

	while (t_call_list != NULL) {
		struct telephony_call *t_call  = t_call_list->data;
		t_status = __bt_hfp_get_call_status(t_call);
		if (t_status >= 0) {
			if (t_call->call_originating != TRUE)
				t_direction = AGENT_CALL_DIRECTION_INCOMING;
			else
				t_direction = AGENT_CALL_DIRECTION_OUTGOING;

			if (t_call->call_conference != TRUE)
				t_call_conference = AGENT_CALL_MULTIPARTY_NO;
			else
				t_call_conference = AGENT_CALL_MULTIPARTY_YES;

			if (t_call->call_number == NULL) {
				t_number = AGENT_NUMBER_TYPE_TELEPHONY;
			} else {
				if (t_call->call_number[0] == '+' || strncmp(
						t_call->call_number, "00", 2) == 0)
					t_number = AGENT_NUMBER_TYPE_INTERNATIONAL;
			}

			index = t_call->call_id;
			_bt_list_current_call_indicator(index, t_direction,
					AGENT_CALL_MODE_VOICE,
					t_status,
					t_call->call_number,
					t_call_conference,
					t_number);
		}
		t_call_list = t_call_list->next;
	}
	_bt_list_current_calls_response(t_device, HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_noise_red_and_echo_cancel_request(gboolean t_enable,
			void *t_device)
{
	if (_bt_hfp_agent_nrec_status(t_enable) == TRUE)
		_bt_nr_and_ec_response(t_device, HFP_STATE_MNGR_ERR_NONE);
	else
		_bt_nr_and_ec_response(t_device, HFP_STATE_MNGR_ERR_AG_FAILURE);

	return;
}

void _bt_hfp_voice_dial_request(gboolean t_enable, void *t_device)
{
	gboolean ret = FALSE;
	int call_state = 0;

	if (vconf_get_int(VCONFKEY_CALL_STATE, &call_state) < 0)
		ERR("vconf_get_int is failed");

	if ((t_enable == TRUE && call_state == 0) || t_enable == FALSE)
		ret = _bt_ag_agent_voice_dial(t_enable);

	if (ret == TRUE)
		_bt_voice_dial_response(t_device, HFP_STATE_MNGR_ERR_NONE);
	else
		_bt_voice_dial_response(t_device,
				HFP_STATE_MNGR_ERR_AG_FAILURE);

	return;
}

void _bt_hfp_set_indicators(const char *t_command, void *t_device)
{
	const char delims = ',';
	char *str = NULL;
	int i = 0;
	if (t_command == NULL)
		goto fail;

	str = strchr(t_command, '=');
	while (hfp_ag_ind[i].indicator_desc != NULL && str != NULL) {
		str++;

		if ((g_strcmp0(hfp_ag_ind[i].indicator_desc, "call") != 0) &&
		(g_strcmp0(hfp_ag_ind[i].indicator_desc, "callheld") != 0) &&
		(g_strcmp0(hfp_ag_ind[i].indicator_desc, "callsetup") != 0)) {

			if (*str == '0') {
				hfp_ag_ind[i].is_activated = FALSE;
			} else if (*str == '1') {
				hfp_ag_ind[i].is_activated = TRUE;
			} else {
				DBG(" no change in is_activated for[%s]\n",
				hfp_ag_ind[i].indicator_desc);
			}
		}
		str = strchr(str, delims);
		i++;
	}

	_bt_indicators_activation_response(t_device, HFP_STATE_MNGR_ERR_NONE);
	return;

fail:
	_bt_indicators_activation_response(t_device,
			HFP_STATE_MNGR_ERR_INVALID_CHAR_IN_STRING);
	return;
}

static int __bt_hfp_get_phonebook_count(const char *path, uint32_t *max_size,
				uint32_t *used)
{
#ifndef TIZEN_WEARABLE
	GDBusConnection *g_conn;
	GDBusProxy *g_proxy;
	GError *err = NULL;
	GVariant *ret = NULL;
	uint32_t max = 0;
	uint32_t size = 0;

	g_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);

	if (!g_conn) {
		if (err) {
			ERR("Unable to connect to gdbus: %s", err->message);
			g_clear_error(&err);
		}
		return -1;
	}

	g_proxy =  g_dbus_proxy_new_sync(g_conn,
			G_DBUS_PROXY_FLAGS_NONE, NULL,
			PHONEBOOK_AGENT_BUS_NAME, PHONEBOOK_AGENT_PATH,
			PHONEBOOK_AGENT_INTERFACE, NULL, &err);

	if (!g_proxy) {
		if (err) {
			ERR("Unable to connect to gdbus: %s", err->message);
			g_clear_error(&err);
		}
		return -1;
	}

	ret = g_dbus_proxy_call_sync(g_proxy, "GetPhonebookSizeAt",
			g_variant_new("(s)",
			path),
			G_DBUS_CALL_FLAGS_NONE, -1,
			NULL, &err);

	if (ret == NULL) {
		ERR("dbus call failed");
		if (err != NULL) {
			ERR("D-Bus API failure: errCode[%x], message[%s]",
			       err->code, err->message);

			g_clear_error(&err);
		}
	}
	if (ret != NULL) {
		g_variant_get(ret, "(u)", &size);
		g_variant_unref(ret);
	}
	DBG("Size returned %d", size);
	if ((g_strcmp0(path, "\"SM\"") == 0) ||
			(g_strcmp0(path, "\"ME\"") == 0)) {
		max = AGENT_MAX_PB_COUNT;
	} else if ((g_strcmp0(path, "\"DC\"") == 0) ||
			(g_strcmp0(path, "\"MC\"") == 0) ||
			(g_strcmp0(path, "\"RC\"") == 0)) {
		max = AGENT_MAX_CALLLOG_COUNT;
	}

	if (max_size)
		*max_size = max;
	if (used) {
		if (size > max)
			*used = max;
		else
			*used = size;
	}

	if (g_conn)
		g_object_unref(g_conn);
	if (g_proxy)
		g_object_unref(g_proxy);
#endif
	return 0;
}

void _bt_hfp_select_phonebook_memory_status(void *t_device)
{
	int32_t path_id = ag_pb_info.path_id;
	uint32_t used = 0;
	uint32_t max_size = 0;

	hfp_state_manager_err_t err = HFP_STATE_MNGR_ERR_NONE;

	if (path_id < 0 || path_id >= AGENT_PB_STORE_LIST_SIZE)
		path_id = 0;

	if (__bt_hfp_get_phonebook_count(agent_pb_store_list[path_id],
			&max_size, &used))
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;

	_bt_select_phonebook_memory_status_response(t_device,
			agent_pb_store_list[path_id],
			max_size, used,
			err);
}

static char *__bt_hfp_get_supported_list(const char *char_list[],
					unsigned int size)
{
	GString *strng;

	int index = 0;

	if (char_list == NULL || size == 0)
		return NULL;

	strng = g_string_new("(");

	while (index < size) {
		if (index > 0)
			g_string_append(strng, ",");

		g_string_append(strng, char_list[index]);
		index++;
	}

	g_string_append(strng, ")");

	return g_string_free(strng, FALSE);
}

void _bt_hfp_select_phonebook_memory_list(void *t_device)
{
	char *str;

	str = __bt_hfp_get_supported_list(agent_pb_store_list,
			AGENT_PB_STORE_LIST_SIZE);

	_bt_select_phonebook_memory_list_response(t_device,
			str, HFP_STATE_MNGR_ERR_NONE);

	g_free(str);
}

void _bt_hfp_select_phonebook_memory(void *t_device, const gchar *pb_path)
{
	int i = 0;
	hfp_state_manager_err_t err;

	while (i < AGENT_PB_STORE_LIST_SIZE) {
		if (strcmp(agent_pb_store_list[i], pb_path) == 0)
			break;
		i++;
	}

	if	(i >= 0 && i < AGENT_PB_STORE_LIST_SIZE) {
		err = HFP_STATE_MNGR_ERR_NONE;
		ag_pb_info.path_id = i;
	} else {
		err = HFP_STATE_MNGR_ERR_INVALID_CHAR_IN_STRING;
	}
	_bt_select_phonebook_memory_response(t_device, err);
}

void _bt_hfp_read_phonebook_entries_list(void *t_device)
{
	hfp_state_manager_err_t err = HFP_STATE_MNGR_ERR_NONE;

	int32_t path_id = ag_pb_info.path_id;
	uint32_t used = 0;

	if (path_id < 0 || path_id >= AGENT_PB_STORE_LIST_SIZE)
		err = HFP_STATE_MNGR_ERR_INVALID_INDEX;
	else {
		if (__bt_hfp_get_phonebook_count(agent_pb_store_list[path_id],
					NULL, &used) != 0) {
			err = HFP_STATE_MNGR_ERR_NOT_ALLOWED;
		}
	}

	_bt_read_phonebook_entries_list_response(t_device, used,
		AGENT_PB_NUMBER_MAX_LENGTH, AGENT_PB_NAME_MAX_LENGTH,
			err);
}

static int __bt_hfp_get_phonebook_entries(int start_index, int end_index)
{
#ifdef TIZEN_WEARABLE
	int count = 0;
#else
	GDBusConnection *g_conn;
	GDBusProxy *g_proxy;
	GError *err = NULL;
	GVariant *ret = NULL;
	GVariantIter *iter;
	int count = 0;
	GVariant *value = NULL;
	GVariant *tuple = NULL;
	const char *name = NULL;
	const char *number = NULL;

	char *uni_name;
	char *uni_number;

	uint32_t handle = 0;

	g_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);

	if (!g_conn) {
		if (err) {
			ERR("Unable to connect to gdbus: %s", err->message);
			g_clear_error(&err);
		}
		return -1;
	}

	g_proxy =  g_dbus_proxy_new_sync(g_conn,
			G_DBUS_PROXY_FLAGS_NONE, NULL,
			PHONEBOOK_AGENT_BUS_NAME, PHONEBOOK_AGENT_PATH,
			PHONEBOOK_AGENT_INTERFACE, NULL, &err);
	if (!g_proxy) {
		if (err) {
			ERR("Unable to connect to gdbus: %s", err->message);
			g_clear_error(&err);
		}
		return -1;
	}

	ret = g_dbus_proxy_call_sync(g_proxy, "GetPhonebookEntriesAt",
			g_variant_new("(sii)",
			agent_pb_store_list[ag_pb_info.path_id],
			start_index, end_index),
			G_DBUS_CALL_FLAGS_NONE, -1,
			NULL, &err);

	if (ret == NULL) {
		ERR("Dbus call failed");
		if (err != NULL) {
			ERR("D-Bus API failure: errCode[%x], message[%s]",
				   err->code, err->message);

			g_clear_error(&err);
		} else {
			ERR("error returned was NULL");
		}
		return -1;
	} else {
		tuple = g_variant_get_child_value(ret, 0);
		if (tuple != NULL) {
			int number_contacts = g_variant_n_children(tuple);
			DBG("number of contacts %d", number_contacts);
			while (count < number_contacts) {
				value = NULL;

				value = g_variant_get_child_value(tuple, count);

				g_variant_get(value, "(ssu)", &name, &number, &handle);
				count++;
				uni_name = g_strndup(name, AGENT_PB_NAME_MAX_LENGTH);
				uni_number = g_strndup(number, AGENT_PB_NAME_MAX_LENGTH);

				_bt_read_phonebook_entries_indicator(uni_name,
						uni_number, handle);
				g_variant_unref(value);
				g_free(uni_name);
				g_free(uni_number);
				g_free(name);
				g_free(number);
			}
			g_variant_unref(tuple);
		}
	}
	g_variant_unref(ret);
	if (g_conn)
		g_object_unref(g_conn);
	if (g_proxy)
		g_object_unref(g_proxy);
#endif
	return count;
}

void _bt_hfp_read_phonebook_entries(void *t_device, const char *cmd)
{
	int start_index = 0;
	int end_index = 0;

	int count = 0;

	char *str = NULL;
	char *next = NULL;

	hfp_state_manager_err_t err;

	if (cmd == NULL)
		return;

	str = g_strdup(cmd);
	next = strchr(str, ',');

	if (next) {
		*next = '\0';
		next++;

		end_index = strtol(next, NULL, 10);
	}

	start_index = strtol(str, NULL, 10);

	g_free(str);

	count = __bt_hfp_get_phonebook_entries(start_index, end_index);

	if (count < 0)
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;
	else if (count == 0)
		err = HFP_STATE_MNGR_ERR_INVALID_INDEX;
	else
		err = HFP_STATE_MNGR_ERR_NONE;

	_bt_read_phonebook_entries_response(t_device, err);
}

void _bt_hfp_find_phonebook_entries_status(void *t_device)
{
	_bt_find_phonebook_entries_status_indicator(
			AGENT_PB_NUMBER_MAX_LENGTH,
			AGENT_PB_NAME_MAX_LENGTH);

	_bt_find_phonebook_entries_status_response(t_device,
						HFP_STATE_MNGR_ERR_NONE);
}

static int __bt_hfp_find_pb_entries(const char *str)
{
	return 0;
}

void _bt_hfp_find_phonebook_entries(void *t_device, const char *cmd)
{
	gchar *st = NULL;
	gchar *unquoted = NULL;

	hfp_state_manager_err_t err = HFP_STATE_MNGR_ERR_NONE;

	/* remove quote and compress */
	st = strchr(cmd, '"');
	if (st == NULL)
		unquoted = g_strdup(cmd);
	else {
		gchar *end = NULL;

		end = strrchr(cmd, '"');
		if (end == NULL)
			unquoted = g_strdup(cmd);
		else
			unquoted = g_strndup(st + 1, end - st - 1);
	}

	if (__bt_hfp_find_pb_entries(unquoted))
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;

	_bt_find_phonebook_entries_response(t_device, err);

	g_free(unquoted);
}

void _bt_hfp_get_character_set(void *t_device)
{
	_bt_supported_character_generic_response(t_device,
		(char *)agent_supported_character_set[ag_pb_info.charset_id],
		HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_list_supported_character(void *t_device)
{
	char *str;

	str = __bt_hfp_get_supported_list(agent_supported_character_set,
			AGENT_SUPPORTED_CHARACTER_SET_SIZE);

	_bt_supported_character_generic_response(t_device,
			str, HFP_STATE_MNGR_ERR_NONE);

	g_free(str);
}

void _bt_hfp_set_character_set(void *t_device, const char *cmd)
{
	int index = 0;

	while (index < AGENT_SUPPORTED_CHARACTER_SET_SIZE) {
		if (strcmp(agent_supported_character_set[index], cmd) == 0) {
			_bt_set_characterset_generic_response(t_device,
					HFP_STATE_MNGR_ERR_NONE);

			ag_pb_info.charset_id = index;
			return;
		}
		index++;
	}

	_bt_set_characterset_generic_response(t_device,
			HFP_STATE_MNGR_ERR_NOT_SUPPORTED);
	return;
}

void _bt_hfp_signal_quality_reply(int32_t rssi, int32_t ber,
	void *t_device)
{
	DBG("signal_quality_reply");

	if (rssi == -1 && ber == -1) {
		_bt_signal_quality_response(t_device, rssi, ber,
		HFP_STATE_MNGR_ERR_AG_FAILURE);
	} else {
		_bt_signal_quality_response(t_device, rssi, ber,
		HFP_STATE_MNGR_ERR_NONE);
	}
}

void _bt_hfp_battery_property_reply(void *data, int32_t bcs,
			int32_t bcl)
{
	if (bcs == -1 || bcl == -1) {
		_bt_battery_charge_status_response(data, bcs,
			bcl, HFP_STATE_MNGR_ERR_AG_FAILURE);
	} else {
		_bt_battery_charge_status_response(data, bcs,
			bcl, HFP_STATE_MNGR_ERR_NONE);
	}

	return;
}

void _bt_hfp_get_battery_property(void *t_device)
{
	_bt_ag_agent_get_battery_status(t_device);
}

void _bt_hfp_operator_reply(char *operator_name,  void *t_device)
{
	if (operator_name == NULL)
		goto failed;

	network_info.network_operator_name = g_strndup(operator_name, 16);

	_bt_operator_selection_indicator(AGENT_OPERATOR_MODE_AUTO,
				operator_name);
	_bt_operator_selection_response(t_device, HFP_STATE_MNGR_ERR_NONE);
	return;

failed:
	_bt_operator_selection_indicator(AGENT_OPERATOR_MODE_AUTO, "UNKNOWN");
	_bt_operator_selection_response(t_device,
				HFP_STATE_MNGR_ERR_AG_FAILURE);
}

void _bt_hfp_get_operator_selection_request(void *t_device)
{
	_bt_ag_agent_get_operator_name(t_device);
}

void _bt_hfp_response_and_hold_request(void *t_device)
{
	_bt_response_and_hold_response(t_device,
			HFP_STATE_MNGR_ERR_NOT_SUPPORTED);
}

void _bt_get_activity_status(void *t_device)
{
	DBG("telephony-tizen: telephony_get_activity_status");

	if (NULL != (__bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_MT_ALERTING)) ||
		NULL != (__bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_MO_ALERTING)) ||
		NULL != (__bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_COMING)) ||
		NULL != (__bt_hfp_get_call_with_status(
				HFP_CALL_STATUS_CREATE)))
		_bt_hfp_get_activity_status_rsp(t_device,
				HFP_AGENT_ACTIVITY_STATUS_RINGING,
				HFP_STATE_MNGR_ERR_NONE);
	else if (NULL != (__bt_hfp_get_call_with_status(
					HFP_CALL_STATUS_WAITING)) ||
		NULL != (__bt_hfp_get_call_with_status(
						HFP_CALL_STATUS_ACTIVE)))
		_bt_hfp_get_activity_status_rsp(t_device,
				HFP_AGENT_ACTIVITY_STATUS_CALL_IN_PROGRESS,
				HFP_STATE_MNGR_ERR_NONE);
	else
		_bt_hfp_get_activity_status_rsp(t_device,
				HFP_AGENT_ACTIVITY_STATUS_READY,
				HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_get_imei_number_reply(char *imei_number,  void *t_device)
{
	_bt_hfp_get_equipment_identity_rsp(t_device, imei_number,
					HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_get_imsi_reply(char *mcc, char *mnc, char *msin, void *t_device)
{
	if (mcc != NULL && mnc != NULL && msin != NULL)
		_bt_hfp_get_imsi_rsp(t_device, mcc, mnc, msin,
				HFP_STATE_MNGR_ERR_NONE);
	else
		_bt_hfp_get_imsi_rsp(t_device,NULL,NULL,NULL,
				HFP_STATE_MNGR_ERR_NOT_ALLOWED);
}

void _bt_hfp_get_creg_status_reply(int n, int status, void *t_device)
{
	_bt_hfp_get_creg_status_rsp(t_device, n, status,
				HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_get_equipment_identity_req(void *t_device)
{
	_bt_ag_agent_get_imei_number(t_device);
}

void _bt_hfp_get_model_info_reply(char *model,  void *t_device)
{
	_bt_hfp_get_model_info_rsp(t_device, model,
					HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_get_model_info_req(void *t_device)
{
	_bt_ag_agent_get_model_name(t_device);
}

void _bt_hfp_get_device_manufacturer_reply(char *manufacturer,  void *t_device)
{
	_bt_hfp_get_device_manufacturer_rsp(t_device, manufacturer,
					HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_get_device_manufacturer_req(void *t_device)
{
	_bt_ag_agent_get_manufacturer_name(t_device);
}

void _bt_hfp_get_imsi_req(void *t_device)
{
	_bt_ag_agent_get_imsi(t_device);
}

void _bt_hfp_get_creg_status_req(void *t_device)
{
	_bt_ag_agent_get_creg_status(t_device);
}

void _bt_hfp_get_revision_info_reply(char *revision,  void *t_device)
{
	_bt_hfp_get_revision_info_rsp(t_device, revision,
					HFP_STATE_MNGR_ERR_NONE);
}

void _bt_hfp_get_revision_info_req(void *t_device)
{
	_bt_ag_agent_get_revision_information(t_device);
}

