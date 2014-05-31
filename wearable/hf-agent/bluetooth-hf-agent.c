/*
 * Bluetooth-hf-agent
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:	Hocheol Seo <hocheol.seo@samsung.com>
 *		Chethan T N <chethan.tn@samsung.com>
 *		Chanyeol Park <chanyeol.park@samsung.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <aul.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#ifdef FEATURE_TIZENW
#include <bluetooth-api.h>
#include <bluetooth-audio-api.h>
#endif
#include <syspopup_caller.h>
#include <dd-display.h>
#include <app_manager.h>
#include <vconf.h>
#include <vconf-keys.h>

#include "alarm.h"
#include "bluetooth-hf-agent.h"

/* LE Auto connection defines */
#define MANUFACTURER_DATA_TYPE		0xFF
#define MANUFACTURER_DATA_CID		0x75	/* Samsung */
#define ADVERTISING_INTERVAL_MIN	500	/* ms */
#define ADVERTISING_INTERVAL_MAX	500	/* ms */
#define ADVERTISING_FILTER_DEFAULT	0x00
#define ADVERTISING_FILTER_ALLOW_SCAN_CONN_WL	0x03

#ifdef FEATURE_TIZENW
#define ADVERTISING_INITIAL_TIMEOUT	8	/* sec */
#else
#define ADVERTISING_INITIAL_TIMEOUT	30	/* sec */
#define ADVERTISING_SHORT_INTERVAL_MIN	150	/* ms */
#define ADVERTISING_SHORT_INTERVAL_MAX	150	/* ms */
#endif /* FEATURE_TIZENW */
/* End of LE Auto connection defines */

#define BT_AGENT_SYSPOPUP_MAX_ATTEMPT 3
#define CALL_APP_ID "org.tizen.call"

#define MAX_WAITING_DELAY 8

#define BT_ADDRESS_STRING_SIZE 18

#define ret_if(expr) \
	do { \
		if (expr) { \
			ERR("(%s) return", #expr); \
			return; \
		} \
	} while (0)

static GMainLoop *gmain_loop = NULL;
static char *g_obj_path;

static GDBusConnection *gdbus_conn;
static GDBusProxy *service_gproxy;
static int owner_sig_id = -1;

/*Below Inrospection data is exposed to bluez from agent*/
static const gchar hf_agent_bluez_introspection_xml[] =
"<node name='/'>"
" <interface name='org.bluez.HandsfreeAgent'>"
"     <method name='NewConnection'>"
"          <arg type='h' name='fd' direction='in'/>"
"          <arg type='q' name='version' direction='in'/>"
"     </method>"
"     <method name='Release'/>"
"  </interface>"
"</node>";

/*Below Inrospection data is exposed to application from agent*/
static const gchar hf_agent_introspection_xml[] =
"<node name='/'>"
" <interface name='org.tizen.HfApp'>"
"		<method name='AnswerCall'>"
"		 </method>"
"		 <method name='TerminateCall'>"
"		 </method>"
"		 <method name='InitiateCall'>"
"			 <arg type='s' name='phoneno' direction='in'/>"
"		 </method>"
"		 <method name='VoiceRecognition'>"
"			 <arg type='i' name='status' direction='in'/>"
"		 </method>"
"		 <method name='ScoDisconnect'>"
"		 </method>"
"		 <method name='SpeakerGain'>"
"			 <arg type='u' name='gain' direction='in'/>"
"		 </method>"
"		 <method name='SendDtmf'>"
"			 <arg type='s' name='dtmf' direction='in'/>"
"		 </method>"
"		 <method name='SendAtCmd'>"
"			 <arg type='s' name='atcmd' direction='in'/>"
"		 </method>"
"		 <method name='ReleaseAndAccept'>"
"		 </method>"
"		 <method name='CallSwap'>"
"		 </method>"
"		 <method name='ReleaseAllCall'>"
"		 </method>"
"		 <method name='JoinCall'>"
"		 </method>"
"		 <method name='GetCurrentCodec'>"
"			<arg type='i' name='codec' direction='out'/>"
"		 </method>"
"		 <method name='RequestCallList'>"
"			<arg type='i' name='count' direction='out'/>"
"			<arg type='a(siiii)' name='callList' direction='out'/>"
"		 </method>"
"		 <method name='GetAudioConnected'>"
"			<arg type='i' name='status' direction='out'/>"
"		 </method>"
"		 <method name='IsHfConnected'>"
"			<arg type='b' name='status' direction='out'/>"
"		 </method>"
" </interface>"
"</node>";

static bt_hf_agent_info_t bt_hf_info;
static gboolean is_hf_connected = FALSE;
static alarm_id_t adv_timer_id = 0;
static int32_t current_codec_id = BT_HF_CODEC_ID_CVSD;
static int32_t sco_audio_connected = BT_HF_AUDIO_DISCONNECTED;

static char prev_cmd[BT_HF_CMD_BUF_SIZE];

typedef struct {
	int idx;
	int dir;
	int status;
	int mode;
	int multi_party;
	int type;
	char *number;
} hf_call_list_info_t;

static GError *__bt_hf_agent_set_error(bt_hf_agent_error_t error);
static int __bt_hf_agent_get_error(const char *error_message);
static gboolean __bt_hf_agent_emit_property_changed(
				GDBusConnection *connection,
				const char *path,
				const char *interface,
				const char *name,
				GVariant *property);

static gboolean __bt_hf_agent_data_cb(GIOChannel *chan, GIOCondition cond,
					bt_hf_agent_info_t *bt_hf_info);
static void __bt_hf_agent_stop_watch(bt_hf_agent_info_t *bt_hf_info);
static void __bt_hf_agent_start_watch(bt_hf_agent_info_t *bt_hf_info);
static gboolean __bt_hf_channel_write(GIOChannel *io, gchar *data,
					gsize count);
static gboolean __bt_hf_send_only(bt_hf_agent_info_t *bt_hf_info, gchar *data,
					gsize count);
static gboolean __bt_hf_send_and_read(bt_hf_agent_info_t *bt_hf_info,
				gchar *data, gchar *response, gsize count);
static GSList *__bt_hf_parse_indicator_names(gchar *names, GSList *indies);
static GSList *__bt_hf_parse_indicator_values(gchar *values, GSList *indies);
static guint __bt_hf_get_hold_mpty_features(gchar *features);
static gboolean __bt_establish_service_level_conn(bt_hf_agent_info_t *bt_hf_info);
static void __bt_hf_agent_sigterm_handler(int signo);
static gboolean __bt_hf_agent_release(void);

static gboolean __bt_get_current_indicators(bt_hf_agent_info_t *bt_hf_info);
static gboolean __bt_get_supported_indicators(bt_hf_agent_info_t *bt_hf_info);
static gboolean __bt_hf_agent_connection(gint32 fd, guint16 version);
static gboolean __bt_hf_agent_connection_release(void);

struct indicator {
	gchar descr[BT_HF_INDICATOR_DESCR_SIZE];
	gint value;
};

static int _hf_agent_answer_call(void);

static int _hf_agent_terminate_call(void);

static int _hf_agent_dial_no(char *no);

static int _hf_agent_set_speaker_gain(unsigned int gain);

static int _hf_agent_send_cmd_read_resp(char *cmd);

static int _hf_agent_voice_recognition(unsigned int status);

static gboolean bt_hf_agent_sco_disconnect(void);

static int _hf_agent_send_dtmf(char *dtmf);

static GVariant *bt_hf_agent_request_call_list(void);

static int bt_hf_agent_send_at_cmd(char *atcmd);

static GQuark __bt_hf_agent_error_quark(void)
{
	DBG("\n");

	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string("hf-agent");

	return quark;
}

static GError *__bt_hf_agent_set_error(bt_hf_agent_error_t error)
{
	ERR("error[%d]\n", error);

	switch (error) {
	case BT_HF_AGENT_ERROR_NOT_AVAILABLE:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_NOT_AVAILABLE);
	case BT_HF_AGENT_ERROR_NOT_CONNECTED:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_NOT_CONNECTED);
	case BT_HF_AGENT_ERROR_CONNECTION_FAILED:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_NOT_CONNECTION_FAILED);
	case BT_HF_AGENT_ERROR_BUSY:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_BUSY);
	case BT_HF_AGENT_ERROR_INVALID_PARAM:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_INVALID_PARAM);
	case BT_HF_AGENT_ERROR_ALREADY_EXIST:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_ALREADY_EXIST);
	case BT_HF_AGENT_ERROR_ALREADY_CONNECTED:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_ALREADY_CONNECTED);
	case BT_HF_AGENT_ERROR_NO_MEMORY:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_NO_MEMORY);
	case BT_HF_AGENT_ERROR_I_O_ERROR:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_I_O_ERROR);
	case BT_HF_AGENT_ERROR_APPLICATION:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_OPERATION_NOT_AVAILABLE);
	case BT_HF_AGENT_ERROR_NOT_ALLOWED:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_OPERATION_NOT_ALLOWED);
	case BT_HF_AGENT_ERROR_NOT_SUPPORTED:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_OPERATION_NOT_SUPPORTED);
	case BT_HF_AGENT_ERROR_INVALID_FILE_DESCRIPTOR:
		return g_error_new(BT_HF_AGENT_ERROR, error,
					BT_ERROR_INVALID_FILE_DESCRIPTOR);
	case BT_HF_AGENT_ERROR_INTERNAL:
	default:
		return g_error_new(BT_HF_AGENT_ERROR, error,
						BT_ERROR_INTERNAL);
	}
}

static int __bt_hf_agent_get_error(const char *error_message)
{
	if (error_message == NULL) {
		DBG("Error message NULL\n");
		return BT_HF_AGENT_ERROR_INTERNAL;
	}

	ERR("Error message = %s\n", error_message);

	if (g_strcmp0(error_message, BT_ERROR_NOT_AVAILABLE) == 0)
		return BT_HF_AGENT_ERROR_NOT_AVAILABLE;
	else if (g_strcmp0(error_message, BT_ERROR_NOT_CONNECTED) == 0)
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	else if (g_strcmp0(error_message, BT_ERROR_BUSY) == 0)
		return BT_HF_AGENT_ERROR_BUSY;
	else if (g_strcmp0(error_message, BT_ERROR_INVALID_PARAM) == 0)
		return BT_HF_AGENT_ERROR_INVALID_PARAM;
	else if (g_strcmp0(error_message, BT_ERROR_ALREADY_EXIST) == 0)
		return BT_HF_AGENT_ERROR_ALREADY_EXIST;
	else if (g_strcmp0(error_message, BT_ERROR_ALREADY_CONNECTED) == 0)
		return BT_HF_AGENT_ERROR_ALREADY_CONNECTED;
	else if (g_strcmp0(error_message, BT_ERROR_NO_MEMORY) == 0)
		return BT_HF_AGENT_ERROR_NO_MEMORY;
	else if (g_strcmp0(error_message, BT_ERROR_I_O_ERROR) == 0)
		return BT_HF_AGENT_ERROR_I_O_ERROR;
	else
		return BT_HF_AGENT_ERROR_INTERNAL;
}

#ifdef FEATURE_TIZENW
static void __bt_set_adv_data(void)
{
	int len = 0;
	int is_setup = 0;
	bluetooth_advertising_data_t adv;

	/*
	 * Advertising data format
	 * Service ID : 00 02
	 * Device ID : 00 01(b2), 00 02(wing-tip)
	 * Version : 01
	 * Purpose : 01 (setup), 02 (auto connection)
	 */

	memset(adv.data, 0x00, sizeof(adv.data));
	adv.data[len++] = 9;	/* length */
	adv.data[len++] = MANUFACTURER_DATA_TYPE;
	adv.data[len++] = 0x00;
	adv.data[len++] = MANUFACTURER_DATA_CID;

	/* Service ID */
	adv.data[len++] = 0x00;
	adv.data[len++] = 0x02;

	/* Device ID */
	adv.data[len++] = 0x00;
	adv.data[len++] = 0x01;

	/* Version */
	adv.data[len++] = 0x01;

	/* Purpose */
	if (vconf_get_int(VCONFKEY_SETUP_WIZARD_STATE, &is_setup) < 0)
		ERR("vconf_get_int is failed");
	adv.data[len++] = is_setup ? 0x01 : 0x02;

	bluetooth_set_advertising_data(&adv, len);
}

static void __bt_start_adv(void)
{
	int ret;

	DBG("Try Lock");
	ret = display_lock_state(LCD_OFF, STAY_CUR_STATE, 2000);
	if (ret >= 0)
		DBG("Lock PM state as current state!");
	else
		DBG("deviced error!");


	__bt_set_adv_data();

	DBG("*** Start ADV ***\n");
	bluetooth_set_custom_advertising(TRUE, ADVERTISING_INTERVAL_MIN,
			ADVERTISING_INTERVAL_MAX,
			ADVERTISING_FILTER_DEFAULT);

	return;
}

static void __bt_stop_adv(void)
{
	DBG("*** Stop ADV ***\n");
	bluetooth_set_advertising(FALSE);
	return;
}

int __bt_start_adv_timer_cb(alarm_id_t alarm_id, void *data)
{
	DBG("*** Advertising timer expired ***\n");

	if (adv_timer_id > 0) {
		DBG("Other alarm was already started\n");
		alarmmgr_remove_alarm(adv_timer_id);
		adv_timer_id =  0;
	}

	if (is_hf_connected) {
		DBG("Already connected to HF");
		return 0;
	}

	__bt_start_adv();

	return 0;
}

static void __bt_start_adv_timer(void)
{
	int ret_val;

	if (is_hf_connected) {
		DBG("Already connected to HF");
		return;
	}

	if (adv_timer_id > 0) {
		DBG("Other alarm was already started\n");
		alarmmgr_remove_alarm(adv_timer_id);
		adv_timer_id = 0;
	}

	ret_val = alarmmgr_set_cb(__bt_start_adv_timer_cb, NULL);
	if (ret_val != ALARMMGR_RESULT_SUCCESS) {
		DBG("alarmmgr_set_cb is failed : 0x%X\n", ret_val);
		return;
	}

	ret_val = alarmmgr_add_alarm(ALARM_TYPE_VOLATILE,
				     ADVERTISING_INITIAL_TIMEOUT, 0, NULL,
				     &adv_timer_id);
	if (ret_val != ALARMMGR_RESULT_SUCCESS) {
		DBG("alarmmgr_add_alarm is failed : 0x%X\n", ret_val);
		return;
	}

	return;
}

static int  __bt_get_last_bonded_device(bluetooth_device_address_t *device_address)
{
	DBG("+");
	int ret;
	int i;
	bluetooth_device_info_t *ptr;

	GPtrArray *dev_list = NULL;
	dev_list = g_ptr_array_new();

	ret = bluetooth_get_bonded_device_list(&dev_list);
	if (ret < 0) {
		DBG("failed bluetooth_get_bonded_device_list");
		g_ptr_array_free(dev_list, TRUE);
		return 1;
	}
	DBG("g pointer arrary count : [%d]", dev_list->len);

	if (dev_list->len == 0) {
		DBG("No paired device found");
		g_ptr_array_free(dev_list, TRUE);
		return 1;
	}

	for (i = 0; i < dev_list->len; i++) {
		ptr = g_ptr_array_index(dev_list, i);
		if (ptr == NULL)
			continue;
		DBG("[%d] %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X", i + 1,
			ptr->device_address.addr[0], ptr->device_address.addr[1],
			ptr->device_address.addr[2], ptr->device_address.addr[3],
			ptr->device_address.addr[4], ptr->device_address.addr[5]);
		memcpy(device_address->addr, ptr->device_address.addr,
				sizeof(bluetooth_device_address_t));
		DBG("\n");
	}
	g_ptr_array_foreach(dev_list, (GFunc)g_free, NULL);
	g_ptr_array_free(dev_list, TRUE);
	DBG("-");
	return 0;
}

static gboolean __bt_hf_auto_connect(void)
{
	int ret;
	bluetooth_device_address_t device_address = { {0} };

	if (is_hf_connected) {
		DBG("Already connected to HF");
		return FALSE;
	}

	ret = __bt_get_last_bonded_device(&device_address);
	if (ret != 0) {
		DBG("Error in getting last bonded device.....");
		return FALSE;
	}

	DBG("*** Try to connect ***\n");
	ret = bluetooth_hf_connect(&device_address);
	if (ret != BLUETOOTH_ERROR_NONE) {
		DBG("*** Connect is failed ***\n");
		return FALSE;
	}
	DBG("*** Connect is done ***\n");

	return TRUE;
}

/* Dummy event handler for bluetooth_get_bonded_device_list */
void __hf_event_handler(int event, bt_hf_event_param_t *param, void *data)
{
	/* No need to handle events */
	return;
}
#endif /* FEATURE_TIZENW */

static gboolean __bt_agent_system_popup_timer_cb(gpointer user_data)
{
	int ret;
	static int retry_count;
	bundle *b = (bundle *)user_data;
	retv_if(user_data == NULL, FALSE);

	++retry_count;

	ret = syspopup_launch("bt-syspopup", b);
	if (ret < 0) {
		ERR("Sorry! Can't launch popup, ret=%d, Re-try[%d] time..",
							ret, retry_count);
		if (retry_count >= BT_AGENT_SYSPOPUP_MAX_ATTEMPT) {
			ERR("Sorry!! Max retry %d reached", retry_count);
			bundle_free(b);
			retry_count = 0;
			return FALSE;
		}
	} else {
		DBG("Hurray!! Finally Popup launched");
		retry_count = 0;
		bundle_free(b);
	}

	return (ret < 0) ? TRUE : FALSE;
}

static int __launch_system_popup(bt_hfp_agent_event_type_t event_type)
{
	int ret;
	bundle *b;
	char event_str[BT_MAX_EVENT_STR_LENGTH + 1];

	DBG("_bt_agent_launch_system_popup: Toast popup +");

	b = bundle_create();
	if (!b)
		return -1;

	switch (event_type) {
	case BT_AGENT_EVENT_HANDSFREE_DISCONNECT:
		g_strlcpy(event_str, "handsfree-disconnect-request", sizeof(event_str));
		break;

	case BT_AGENT_EVENT_HANDSFREE_CONNECT:
		g_strlcpy(event_str, "handsfree-connect-request", sizeof(event_str));
		break;

	default:
		bundle_free(b);
		return -1;
	}

	bundle_add(b, "event-type", event_str);
	ret = syspopup_launch("bt-syspopup", b);
	if (0 > ret) {
		DBG("Popup launch failed...retry %d\n", ret);

		g_timeout_add(BT_AGENT_SYSPOPUP_TIMEOUT_FOR_MULTIPLE_POPUPS,
				  (GSourceFunc)__bt_agent_system_popup_timer_cb, b);
	} else {
		bundle_free(b);
	}

	DBG("_bt_agent_launch_system_popup -%d", ret);
	return 0;
}

static void __hf_agent_method(GDBusConnection *connection,
			    const gchar *sender,
			    const gchar *object_path,
			    const gchar *interface_name,
			    const gchar *method_name,
			    GVariant *parameters,
			    GDBusMethodInvocation *invocation,
			    gpointer user_data)
{
	DBG("+");

	DBG("method %s", method_name);
	int ret = 0;
	GError *err;

	if (g_strcmp0(method_name, "NewConnection") == 0) {
		gint32 fd;
		int index;
		guint16 version;
		GDBusMessage *msg;
		GUnixFDList *fd_list;

		g_variant_get(parameters, "(hq)", &index, &version);
		msg = g_dbus_method_invocation_get_message(invocation);
		fd_list = g_dbus_message_get_unix_fd_list(msg);
		if (fd_list == NULL) {
			ret = BT_HF_AGENT_ERROR_INVALID_FILE_DESCRIPTOR;
			goto fail;
		}

		fd = g_unix_fd_list_get(fd_list, index, NULL);
		if (fd == -1) {
			ret = BT_HF_AGENT_ERROR_INVALID_FILE_DESCRIPTOR;
			goto fail;
		}

		DBG("FD is = [%d], version = [%d]\n", fd, version);

		if (!__bt_hf_agent_connection(fd, version)) {
			ret = BT_HF_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "Release") == 0) {
		if (!__bt_hf_agent_connection_release()) {
			ret = BT_HF_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "AnswerCall") == 0) {
		DBG("Going to call AnswerCall");
		ret = _hf_agent_answer_call();
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "TerminateCall") == 0) {
		DBG("Going to call TerminateCall");
		ret = _hf_agent_terminate_call();
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "InitiateCall") == 0) {
		char *phoneno = NULL;

		g_variant_get(parameters, "(&s)", &phoneno);

		DBG_SECURE("Going to call InitiateCall, Number is = [%s]\n", phoneno);
		ret = _hf_agent_dial_no(phoneno);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "VoiceRecognition") == 0) {
		int status = 0;

		g_variant_get(parameters, "(i)", &status);

		DBG("Going to call VoiceRecognition, Status [%d]", status);
		ret = _hf_agent_voice_recognition(status);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "ScoDisconnect") == 0) {
		DBG("Going to call ScoDisconnect");
		if (!bt_hf_agent_sco_disconnect()) {
			ret = BT_HF_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "SpeakerGain") == 0) {
		unsigned int gain = 0;

		g_variant_get(parameters, "(u)", &gain);

		DBG("Going to call SpeakerGain, gain is = [%d]\n", gain);
		ret = _hf_agent_set_speaker_gain(gain);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "SendDtmf") == 0) {
		char *dtmf = NULL;

		g_variant_get(parameters, "(&s)", &dtmf);

		DBG("Going to call SendDtmf, dtmf is = [%s]\n", dtmf);
		ret = _hf_agent_send_dtmf(dtmf);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "SendAtCmd") == 0) {
		char *cmd = NULL;

		g_variant_get(parameters, "(&s)", &cmd);

		DBG("Going to call SendAtCmd, cmd is = [%s]\n", cmd);
		ret = bt_hf_agent_send_at_cmd(cmd);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "ReleaseAndAccept") == 0) {
		DBG("Going to call ReleaseAndAccept");
		ret = _hf_agent_send_cmd_read_resp(BT_HF_RELEASE_AND_ACCEPT);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "CallSwap") == 0) {
		DBG("Going to call CallSwap");
		ret = _hf_agent_send_cmd_read_resp(BT_HF_ACCEPT_AND_HOLD);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "ReleaseAllCall") == 0) {
		DBG("Going to call ReleaseAllCall");
		ret = _hf_agent_send_cmd_read_resp(BT_HF_RELEASE_ALL);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "JoinCall") == 0) {
		DBG("Going to call JoinCall");
		ret = _hf_agent_send_cmd_read_resp(BT_HF_JOIN_CALL);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "GetCurrentCodec") == 0) {
		DBG("Going to call GetCurrentCodec");
		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(i)", current_codec_id));
	} else if (g_strcmp0(method_name, "RequestCallList") == 0) {
		GVariant *call_var;

		DBG("Going to call RequestCallList");
		call_var = bt_hf_agent_request_call_list();
		if (!call_var) {
			ret = BT_HF_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}
		g_dbus_method_invocation_return_value(invocation, call_var);
	} else if (g_strcmp0(method_name, "GetAudioConnected") == 0) {
		DBG("Going to call GetAudioConnected");
		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(i)", sco_audio_connected));
	} else if (g_strcmp0(method_name, "IsHfConnected") == 0) {
		DBG("Going to call IsHfConnected");
		DBG("is_hf_connected : %s", is_hf_connected ? "Connected":"Disconnected");

		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(b)", is_hf_connected));
	}
	DBG("-");
	return;

fail:
	err = __bt_hf_agent_set_error(ret);
	g_dbus_method_invocation_return_gerror(invocation, err);
	g_error_free(err);
	DBG("-");
}

static const GDBusInterfaceVTable method_table = {
	__hf_agent_method,
	NULL,
	NULL,
};

static GDBusNodeInfo *__bt_hf_create_method_node_info
					(const gchar *introspection_data)
{
	if (introspection_data == NULL)
		return NULL;

	return g_dbus_node_info_new_for_xml(introspection_data, NULL);
}

static GDBusConnection *__bt_hf_get_gdbus_connection(void)
{
	DBG("+");

	GError *err = NULL;

	if (gdbus_conn == NULL)
		gdbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);

	if (!gdbus_conn) {
		if (err) {
			ERR("Unable to connect to dbus: %s", err->message);
			g_clear_error(&err);
		}
		return NULL;
	}
	DBG("-");

	return gdbus_conn;
}

static gboolean __bt_hf_register_profile_methods(void)
{
	DBG("+");
	GError *error = NULL;
	guint object_id;
	guint owner_id;
	GDBusNodeInfo *node_info;
	gchar *path;

	owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
				BT_HF_SERVICE_NAME,
				G_BUS_NAME_OWNER_FLAGS_NONE,
				NULL, NULL, NULL,
				NULL, NULL);

	DBG("owner_id is [%d]", owner_id);

	node_info = __bt_hf_create_method_node_info(
				hf_agent_bluez_introspection_xml);
	if (node_info == NULL)
		return FALSE;

	path = g_strdup(BT_HF_BLUEZ_OBJECT_PATH);
	DBG("path is [%s]", path);

	object_id = g_dbus_connection_register_object(gdbus_conn, path,
					node_info->interfaces[0],
					&method_table,
					NULL, NULL, &error);
	if (object_id == 0) {
		ERR("Failed to register: %s", error->message);
		g_error_free(error);
		g_free(path);
		return FALSE;
	}
	g_free(path);

	node_info = __bt_hf_create_method_node_info(hf_agent_introspection_xml);
	if (node_info == NULL)
		return FALSE;

	path = g_strdup(BT_HF_AGENT_OBJECT_PATH);
	DBG("path is [%s]", path);

	object_id = g_dbus_connection_register_object(gdbus_conn, path,
						node_info->interfaces[0],
						&method_table,
						NULL, NULL, &error);
	if (object_id == 0) {
		ERR("Failed to register: %s", error->message);
		g_error_free(error);
		g_free(path);
		return FALSE;
	}
	g_free(path);

	DBG("-");
	return TRUE;
}

static GDBusProxy *__bt_hf_gdbus_init_service_proxy(const gchar *service,
				const gchar *path, const gchar *interface)
{
	DBG("+");

	GDBusProxy *proxy;
	GError *err = NULL;

	if (gdbus_conn == NULL)
		gdbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);

	if (!gdbus_conn) {
		if (err) {
			ERR("Unable to connect to gdbus: %s", err->message);
			g_clear_error(&err);
		}
		return NULL;
	}

	proxy =  g_dbus_proxy_new_sync(gdbus_conn,
			G_DBUS_PROXY_FLAGS_NONE, NULL,
			service, path,
			interface, NULL, &err);

	if (!proxy) {
		if (err) {
			ERR("Unable to create proxy: %s", err->message);
			 g_clear_error(&err);
		}
		return NULL;
	}

	DBG("-");
	return proxy;
}

static GDBusProxy *__bt_hf_gdbus_get_service_proxy(const gchar *service,
				const gchar *path, const gchar *interface)
{
	return (service_gproxy) ? service_gproxy :
			__bt_hf_gdbus_init_service_proxy(service,
					path, interface);
}


static int __bt_hf_agent_gdbus_method_send(const char *service,
				GVariant *path, const char *interface,
				const char *method)
{
	DBG("+");

	GVariant *ret;
	GDBusProxy *proxy;
	GError *error = NULL;

	proxy = __bt_hf_gdbus_get_service_proxy(service, g_obj_path, interface);
	if (!proxy)
		return BT_HF_AGENT_ERROR_INTERNAL;

	ret = g_dbus_proxy_call_sync(proxy,
				method, path,
				G_DBUS_CALL_FLAGS_NONE, -1,
				NULL, &error);
	if (ret == NULL) {
		/* dBUS-RPC is failed */
		ERR("dBUS-RPC is failed");
		if (error != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
			       error->code, error->message);

			g_clear_error(&error);
		}
		return BT_HF_AGENT_ERROR_INTERNAL;
	}

	g_variant_unref(ret);

	return BT_HF_AGENT_ERROR_NONE;
}

/*
Below methods exposed to Applicatoins
*/
static gboolean __bt_hf_agent_emit_signal(GDBusConnection *connection,
				const char *path, const char *interface,
				const char *signal_name, GVariant *param)
{
	DBG("+");

	GError *error = NULL;
	gboolean ret;
	ret =  g_dbus_connection_emit_signal(connection,
				 NULL, path,
				 interface, signal_name,
				 param, &error);
	if (!ret) {
		if (error != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
			       error->code, error->message);
			g_clear_error(&error);
		}
	}
	DBG("signal_name = [%s]", signal_name);
	DBG("-");
	return ret;
}

static gboolean __bt_hf_agent_emit_property_changed(
				GDBusConnection *connection,
				const char *path,
				const char *interface,
				const char *name,
				GVariant *property)
{
	DBG("+");

	GError *error = NULL;
	gboolean ret;
	ret =  g_dbus_connection_emit_signal(connection,
				NULL, path, interface,
				"PropertyChanged",
				g_variant_new("s(v)", name, property),
				&error);
	if (!ret) {
		if (error != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
			       error->code, error->message);
			g_clear_error(&error);
		}
	}
	DBG("-");
	return ret;
}

/*
Below methods exposed to Bluez
*/

static void __bt_hf_agent_handle_ind_change(bt_hf_agent_info_t *bt_hf_info,
							guint index, gint value)
{
	gchar *name;
	struct indicator *ind = g_slist_nth_data(bt_hf_info->indies, index - 1);
	if (ind == NULL) {
		ERR("Indicator is NULL");
		return;
	}

	name = ind->descr;
	ind->value = value;

	DBG("__bt_hf_agent_process_ind_change, name is %s, value = [%d]",
								name, value);
	if (!strcmp(name, "\"call\"")) {
		bt_hf_info->ciev_call_status = value;
		if (value > 0) {
			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallStarted", NULL);
			bt_hf_info->is_dialing = FALSE;
			bt_hf_info->call_active = TRUE;
		} else if (bt_hf_info->call_active) {
			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallEnded", NULL);
			bt_hf_info->call_active = FALSE;
		}

	} else if (!strcmp(name, "\"callsetup\"")) {
		bt_hf_info->ciev_call_setup_status = value;
		if (value == 0 && bt_hf_info->is_dialing) {
			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallTerminated",
					NULL);
			bt_hf_info->is_dialing = FALSE;
		} else if (!bt_hf_info->is_dialing && value > 0)
			bt_hf_info->is_dialing = TRUE;

	} else if (!strcmp(name, "\"callheld\"")) {
		if (value == 0) { /* No calls held*/
			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"NoCallsHeld",
					NULL);
		} else if (value == 1) {
			/*Call is placed on hold or active/held calls swapped */
			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallsSwapped", NULL);
			bt_hf_info->is_dialing = FALSE;
		} else {
			/*Call on hold, no active call*/
			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallOnHold", NULL);
			bt_hf_info->is_dialing = FALSE;
		}
	} else if (!strcmp(name, "\"service\""))
		__bt_hf_agent_emit_property_changed(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE,
				"RegistrationStatus",
				g_variant_new("(q)", value));
	else if (!strcmp(name, "\"signal\""))
		__bt_hf_agent_emit_property_changed(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE, "SignalStrength",
				g_variant_new("(q)", value));
	else if (!strcmp(name, "\"roam\""))
		__bt_hf_agent_emit_property_changed(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE, "RoamingStatus",
				g_variant_new("(q)", value));
	else if (!strcmp(name, "\"battchg\""))
		__bt_hf_agent_emit_property_changed(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE, "BatteryCharge",
				g_variant_new("(q)", value));
}


static gboolean  __bt_hf_agent_launch_call_app(const char *launch_type,
							const char *number)
{
	bundle *b;
	bool is_running;

	DBG("+");
	app_manager_is_running(CALL_APP_ID, &is_running);
	if (is_running)
		return FALSE;

	DBG_SECURE("Launch type = %s, number(%s)", launch_type, number);

	b = bundle_create();
	if (NULL == b) {
		DBG("bundle_create() Failed");
		return FALSE;
	}

	bundle_add(b, "launch-type", launch_type);

	if (strlen(number) != 0)
		bundle_add(b, "number", number);

	aul_launch_app(CALL_APP_ID, b);
	bundle_free(b);

	DBG("-");

	return TRUE;
}

static void __bt_hf_agent_handle_voice_activation(gint value)
{
	__bt_hf_agent_emit_signal(gdbus_conn, BT_HF_AGENT_OBJECT_PATH,
		BT_HF_SERVICE_INTERFACE,
		"VoiceRecognition",
		g_variant_new("(i)", value));

	return;
}

static void __bt_hf_agent_handle_speaker_gain(gint value)
{
	__bt_hf_agent_emit_signal(gdbus_conn, BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE, "VolumeSpeaker",
				g_variant_new("(i)", value));

	return;
}

static int __bt_hf_agent_handle_ccwa(bt_hf_agent_info_t *bt_hf_info,
							const gchar *buf)
{
	gchar *ccwa;
	gchar number[BT_HF_CALLER_NUM_SIZE];
	gchar *sep;
	char fmt_str[BT_HF_FMT_STR_SIZE];
	int len = strlen(buf);

	DBG("__bt_hf_agent_handle_ccwa +");
	if (len > BT_HF_CALLER_NUM_SIZE + 10) {
		DBG("buf len %d is too long '%s'", len, buf);
		return 1;
	}

	if ((ccwa = strstr(buf, "\r\n+CCWA"))) {
		snprintf(fmt_str, sizeof(fmt_str), "\r\n+CCWA: \"%%%ds", sizeof(number) - 1);
		if (sscanf(ccwa, fmt_str, number) == 1) {
			sep = strchr(number, '"');
			sep[0] = '\0';

			ccwa = number;

			__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE, "CallWaiting",
					g_variant_new("(s)", ccwa));
		} else {
			ERR_SECURE("CCWA '%s' is Call Wating", buf);
			return 1;
		}
	}
	DBG("__bt_hf_agent_handle_ccwa -");
	return 0;
}

static GSList *__bt_hf_prepare_call_list(const char *buf) {
	GSList *call_list = NULL;
	char *str = NULL;
	char *ptr = NULL;
	char *temp = NULL;
	char delim_sep[] = "\r\n";
	hf_call_list_info_t *call_info;

	DBG("+");
	str = strtok(buf, delim_sep);
	while (str != NULL) {
		if (!(strstr(str, "+CLCC:"))) {
			str = strtok(NULL, delim_sep);
			continue;
		}

		call_info = g_new0(hf_call_list_info_t, 1);

		sscanf(str, "+CLCC: %1d,%1d, %1d, %1d, %1d",
				&call_info->idx, &call_info->dir,
				&call_info->status, &call_info->mode,
				&call_info->multi_party);
		DBG("Index = [%d], Direction = [%d], Status = [%d], Mode = [%d], Multi_party = [%d]\n",
				call_info->idx, call_info->dir, call_info->status,
				call_info->mode, call_info->multi_party);

		ptr = strstr(str, "\"");
		if (ptr) {
			temp = strstr(ptr + 1, "\"");
			if (temp) {
				*temp = '\0';
				DBG_SECURE("\tPhone Number = [%s]\n", ptr + 1);
				call_info->number = g_strdup(ptr + 1);

				if (strstr(temp + 1, ",")) {
					temp += 2;
					DBG("\tType = [%s]\n", temp);
					call_info->type = atoi(temp);
				}
			}
		} else {
			/*In case of no phone no. in CLCC respnse, we should launch call application
			 * with NULL string. By doing so "unknown" shall be displayed*/
			DBG("Phone number does not exist\n");
			call_info->number = g_strdup("");
		}

		call_list = g_slist_append(call_list, call_info);
		str = strtok(NULL, delim_sep);
	}
	DBG("-");
	return call_list;
}

static GSList *__bt_hf_get_call_list(bt_hf_agent_info_t *bt_hf_info)
{
	char buf[BT_HF_DATA_BUF_SIZE] = {0,};
	GSList *call_list = NULL;

	DBG("+");

	/* Send CLCC when the callsetup */
	__bt_hf_send_and_read(bt_hf_info, BT_HF_CALLLIST, buf,
			sizeof(BT_HF_CALLLIST) - 1);
	DBG_SECURE("Receive CLCC response buffer = '%s'", buf);

	call_list =  __bt_hf_prepare_call_list(buf);
	DBG("-");
	return call_list;
}

static void __bt_hf_call_info_free(void *data)
{
	DBG("+");

	hf_call_list_info_t *call_info = data;
	g_free(call_info->number);
	g_free(call_info);

	DBG("-");
}

static void __bt_hf_free_call_list(GSList *call_list)
{
	DBG("+");

	g_slist_free_full(call_list, __bt_hf_call_info_free);

	DBG("-");
}

static void __bt_hf_launch_call_using_call_list(GSList *call_list,
					bt_hf_agent_info_t *bt_hf_info)
{
	guint len;
	const char *launch_type_str;
	hf_call_list_info_t *call_info;

	DBG("+");
	if (call_list == NULL)
		return;

	len = g_slist_length(call_list);

	while (len--) {
		call_info = g_slist_nth_data(call_list, len);

		/* Launch based on below conditions
		  * DC - Active call which is initiated from H
		  * MR - Alerting call which is initiated from H
		  * MT - Incoming call */
		if (call_info->status == BT_HF_CALL_STAT_ACTIVE) {
			launch_type_str =  "DC";
		} else {
			if (call_info->dir == BT_HF_CALL_DIR_INCOMING)
				launch_type_str =  "MT";
			else
				launch_type_str =  "MR";
		}

		if (__bt_hf_agent_launch_call_app(launch_type_str,
					call_info->number)  == FALSE)
			DBG("call app launching failed");
	}
	DBG("-");
}

static GVariant *__bt_hf_agent_get_call_status_info(GSList *call_list)
{
	DBG("+");

	int32_t call_count;
	gchar *caller;
	hf_call_list_info_t *call_info;

	GVariantBuilder *builder;
	GVariant *var_data;

	builder = g_variant_builder_new(G_VARIANT_TYPE("a(siiii)"));

	call_count = g_slist_length(call_list);
	DBG("Total call count = '%d'", call_count);

	while (call_count--) {
		call_info = g_slist_nth_data(call_list, call_count);
		DBG_SECURE("Idx=%d, Dir=%d, status=%d, mode=%d, mparty=%d, number=[%s]\n",
		call_info->idx, call_info->dir, call_info->status,
		call_info->mode, call_info->multi_party, call_info->number);
		caller = call_info->number;

		g_variant_builder_add(builder, "(siiii)",
				caller, call_info->dir, call_info->status,
				call_info->multi_party, call_info->idx);
	}
	var_data = g_variant_new("(ia(siiii))",
				g_slist_length(call_list), builder);

	g_variant_builder_unref(builder);
	DBG("-");
	return  var_data;
}

static void __bt_hf_clear_prev_sent_cmd(void)
{
	if (prev_cmd[0] != 0)
		ERR("No sent command");

	memset(prev_cmd, 0, BT_HF_CMD_BUF_SIZE);

	return;
}

static void __bt_hf_agent_send_call_status_info(GSList *call_list)
{
	GVariant *var_data;

	var_data = __bt_hf_agent_get_call_status_info(call_list);

	if (gdbus_conn)
		__bt_hf_agent_emit_signal(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE,
				"CallStatusUpdate",
				var_data);
}

static void __bt_hf_agent_handle_call_list(bt_hf_agent_info_t *bt_hf_info)
{
	int ret;
	char buf[BT_HF_DATA_BUF_SIZE] = {0,};

	DBG("Try Lock");
	ret = display_lock_state(LCD_OFF, STAY_CUR_STATE, 0);
	if (ret >= 0)
		DBG("Lock PM state as current state!");
	else
		DBG("deviced error!");

	/* Send CLCC. The response will be handled in the handler */
	__bt_hf_send_and_read(bt_hf_info, BT_HF_CALLLIST, buf,
			sizeof(BT_HF_CALLLIST) - 1);

	DBG("Try Unlock");
	ret = display_unlock_state(LCD_OFF, PM_SLEEP_MARGIN);
	if (ret >= 0)
		DBG("UnLock PM state");
	else
		DBG("deviced error!");
}

static void __bt_hf_agent_request_call_list_info(bt_hf_agent_info_t *bt_hf_info,
								guint index)
{
	char *name;
	int ret;
	struct indicator *ind = g_slist_nth_data(bt_hf_info->indies, index - 1);
	if (ind == NULL) {
		ERR("Indicator is NULL");
		return;
	}
	name = ind->descr;
	DBG("name : %s", name);

	if ((strcmp(name, "\"callsetup\"") != 0) &&
			(strcmp(name, "\"call\"") != 0) &&
				(strcmp(name, "\"callheld\"") != 0))
		return;

	DBG("Try Lock!!");
	ret = display_lock_state(LCD_OFF, STAY_CUR_STATE, 0);
	if (ret >= 0)
		DBG("Lock PM state as current state!!");
	else
		DBG("deviced error!!");

	__bt_hf_agent_handle_call_list(bt_hf_info);

	DBG("Try Unlock!!");
	ret = display_unlock_state(LCD_OFF, PM_SLEEP_MARGIN);
	if (ret >= 0)
		DBG("UnLock PM state!!");
	else
		DBG("deviced error!!");

}

static gboolean __bt_hf_send_available_codec(bt_hf_agent_info_t *bt_hf_info)
{
	gchar buf[BT_HF_DATA_BUF_SIZE];
	gchar cmd_buf[BT_HF_CMD_BUF_SIZE] = {0};
	gboolean ret;

	snprintf(cmd_buf, sizeof(cmd_buf), BT_HF_AVAILABLE_CODEC,
			BT_HF_CODEC_ID_CVSD, BT_HF_CODEC_ID_MSBC);
	ret = __bt_hf_send_and_read(bt_hf_info, cmd_buf, buf,
				strlen(cmd_buf));
	if (!ret || !strstr(buf, "OK"))
		return FALSE;

	return TRUE;
}

static  int _hf_agent_codec_setup(guint codec_id)
{
	int ret;

	if (!g_obj_path) {
		DBG("g_obj_path is NULL\n");
		return BT_HF_AGENT_ERROR_INTERNAL;
	}

	switch (codec_id) {
	case BT_HF_CODEC_ID_CVSD:
		ret = __bt_hf_agent_gdbus_method_send(BLUEZ_SERVICE_NAME,
						NULL,
						BLUEZ_HF_INTERFACE_NAME,
						"SetNbParameters");
		break;
	case BT_HF_CODEC_ID_MSBC:
		ret = __bt_hf_agent_gdbus_method_send(BLUEZ_SERVICE_NAME,
						NULL,
						BLUEZ_HF_INTERFACE_NAME,
						"SetWbsParameters");
		break;
	default:
		ret = BT_HF_AGENT_ERROR_INTERNAL;
		DBG("Invalid Codec\n");
		break;
	}

	if (ret)
		DBG("Failed to setup the Codec\n");
	else
		current_codec_id = codec_id;

	return ret;
}

static void __bt_hf_agent_handle_codec_select(bt_hf_agent_info_t *bt_hf_info,
							guint codec_id)
{
	gchar buf[BT_HF_DATA_BUF_SIZE];
	gchar cmd_buf[BT_HF_CMD_BUF_SIZE] = {0};
	gboolean ret;

	if (codec_id != BT_HF_CODEC_ID_CVSD && codec_id != BT_HF_CODEC_ID_MSBC) {
		DBG("Codec id doesn't match, so send available codec again");
		ret = __bt_hf_send_available_codec(bt_hf_info);
		if (!ret)
			DBG("Failed to send avalable codec");
		return;
	}

	/* HF should be ready accpet SCO connection before sending theresponse for
	"\r\n+BCS=>Codec ID\r\n", Keep the BT chip ready to recevie encoded SCO data */
	ret = _hf_agent_codec_setup(codec_id);

	snprintf(cmd_buf, sizeof(cmd_buf), BT_HF_CODEC_SELECT, codec_id);
	ret = __bt_hf_send_and_read(bt_hf_info, cmd_buf, buf,
				strlen(cmd_buf));
	if (!ret || !strstr(buf, "OK")) {
		DBG("Failed to select the Codec\n");
	} else {
		/* Inform other modules (Ex:MM) about the selected codec */
		__bt_hf_agent_emit_signal(gdbus_conn, BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE, "SelectedCodec",
				g_variant_new("(u)", codec_id));
	}

}

void __bt_hf_agent_print_at_buffer(const char *buf)
{

	int i = 0;
	char s[1024] = {0, };
	strncpy(s, buf, BT_HF_DATA_BUF_SIZE - 1);
	while (s[i] != '\0') {
		if (s[i] == '\r' || s[i] == '\n')
			s[i] = '.';

		i++;
	}
	DBG_SECURE("Received AT Buffer, length = %d Buffer = >>>>> %s\n", strlen(s), s);
}

static int __bt_hf_agent_handler_ciev(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	gchar indicator[BT_HF_INDICATOR_DESCR_SIZE + 4];
	gchar *sep;
	gint value;
	guint index;
	char fmt_str[BT_HF_FMT_STR_SIZE];

	DBG("++++++++ __bt_hf_agent_handler_ciev +++++++++");

	snprintf(fmt_str, sizeof(fmt_str), "\r\n+CIEV:%%%ds\r\n", sizeof(indicator) - 1);
	if (sscanf(buf, fmt_str, indicator) == 1) {
		sep = strchr(indicator, ',');
		sep[0] = '\0';
		sep += 1;
		index = atoi(indicator);
		value = atoi(sep);
		__bt_hf_agent_handle_ind_change(bt_hf_info, index, value);

		if (bt_hf_info->ciev_call_status == 0 &&
				bt_hf_info->ciev_call_setup_status == 0)
			DBG("No active call");
		else
			/* Request CLCC based on indicator change for call/callsetup/callHeld */
			__bt_hf_agent_request_call_list_info(bt_hf_info, index);
	}
	DBG("--------- __bt_hf_agent_handler_ciev ------------");
	return 0;
}

static void __bt_hf_agent_handle_ven_samsung(bt_hf_agent_info_t *bt_hf_info,
						gint app_id, const char *msg)
{
	/* Whomesoever wants need to handle it */
	char *sig_name = "SamsungXSAT";
	__bt_hf_agent_emit_signal(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE,
				sig_name,
				g_variant_new("(is)", app_id, msg));
}

static int __bt_hf_agent_handler_ring(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	DBG("++++++++ __bt_hf_agent_handler_ring ++++++++");
	DBG("---------__bt_hf_agent_handler_ring --------");

	return 0;
}

static int __bt_hf_agent_handler_clip(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	DBG("+++++++++ __bt_hf_agent_handler_clip ++++++++");
	DBG("---------__bt_hf_agent_handler_clip --------");

	return 0;
}

static int __bt_hf_agent_handler_bvra(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	DBG("+++++++++ __bt_hf_agent_handler_bvra +++++++++");
	gint value;
	 if (sscanf(buf, "\r\n+BVRA:%1d\r\n", &value) == 1)
		__bt_hf_agent_handle_voice_activation(value);

	 DBG("---------__bt_hf_agent_handler_bvra --------");
	return 0;
}

static int __bt_hf_agent_handler_bcs(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	guint codec_id;
	DBG("+++++++++ __bt_hf_agent_handler_bcs +++++++++-");
	if (sscanf(buf, "\r\n+BCS:%3d\r\n", &codec_id))
		__bt_hf_agent_handle_codec_select(bt_hf_info, codec_id);

	DBG("---------__bt_hf_agent_handler_bcs --------");
	return 0;
}

static int __bt_hf_agent_handler_vgs(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	gint value;
	DBG("+++++++++ __bt_hf_agent_handler_vgs +++++++++");
	if (sscanf(buf, "\r\n+VGS:%2d\r\n", &value))
		__bt_hf_agent_handle_speaker_gain(value);

	DBG("---------__bt_hf_agent_handler_vgs --------");

	return 0;
}

static int __bt_hf_agent_handler_ccwa(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	DBG("+++++++++ __bt_hf_agent_handler_ccwa ++++++++");
		__bt_hf_agent_handle_ccwa(bt_hf_info, buf);
	DBG("---------__bt_hf_agent_handler_ccwa --------");

	return 0;
}


static int __bt_hf_agent_handler_xsat(bt_hf_agent_info_t *bt_hf_info,
							const char *buf)
{
	gint app_id;
	char msg[BT_HF_DATA_BUF_SIZE];
	char fmt_str[BT_HF_CMD_BUF_SIZE];

	DBG("+++++++++ __bt_hf_agent_handler_xsat +++++++++");

	snprintf(fmt_str, sizeof(fmt_str), "\r\n+XSAT:%%d,%%%ds\r\n", sizeof(msg) - 1);
	if (sscanf(buf, fmt_str, &app_id, msg))
		__bt_hf_agent_handle_ven_samsung(bt_hf_info, app_id, msg);

	DBG("---------__bt_hf_agent_handler_xsat --------");

	return 0;
}

static int __bt_hf_agent_handler_cme_error(bt_hf_agent_info_t *bt_hf_info,
							const char *buf)
{
	DBG("+++++++++ __bt_hf_agent_handler_cme_error ++++++++");

	if (strstr(prev_cmd, "ATD")) {
		__bt_hf_agent_emit_signal(gdbus_conn,
					BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallTerminated",
					NULL);
	}

	__bt_hf_clear_prev_sent_cmd();

	DBG("---------__bt_hf_agent_handler_cme_error --------");
	return 0;
}

static int __bt_hf_agent_handler_response_ok(bt_hf_agent_info_t *bt_hf_info,
							const char *buf)
{
	DBG("+++++++++ __bt_hf_agent_handler_response_ok ++++++++");
	__bt_hf_clear_prev_sent_cmd();
	return 0;
}

static int __bt_hf_agent_handler_response_err(bt_hf_agent_info_t *bt_hf_info,
							const char *buf)
{
	DBG("+++++++++ __bt_hf_agent_handler_response_err ++++++++");

	if (strstr(prev_cmd, "ATD")) {
		__bt_hf_agent_emit_signal(gdbus_conn, BT_HF_AGENT_OBJECT_PATH,
					BT_HF_SERVICE_INTERFACE,
					"CallTerminated",
					NULL);
	}
	__bt_hf_clear_prev_sent_cmd();
	return 0;
}

static int __bt_hf_agent_handler_response_serr(bt_hf_agent_info_t *bt_hf_info,
							const char *buf)
{
	__bt_hf_clear_prev_sent_cmd();
	return 0;
}

static int __bt_hf_agent_handler_clcc(bt_hf_agent_info_t *bt_hf_info, const char *buffer)
{

	GSList *call_list = NULL;
	int ret;
	DBG("+++++++++ __bt_hf_agent_handler_clcc ++++++++");
	DBG_SECURE("Receive CLCC response buffer = '%s'", buffer);

	DBG("Try Lock");
	ret = display_lock_state(LCD_OFF, STAY_CUR_STATE, 0);
	if (ret >= 0)
		DBG("Lock PM state as current state!");
	else
		DBG("deviced error!");

	call_list = __bt_hf_prepare_call_list(buffer);

	if (call_list == NULL)
		goto done;

	__bt_hf_launch_call_using_call_list(call_list, bt_hf_info);

	__bt_hf_agent_send_call_status_info(call_list);

	__bt_hf_free_call_list(call_list);

done:
	DBG("Try Unlock");
	ret = display_unlock_state(LCD_OFF, PM_SLEEP_MARGIN);
	if (ret >= 0)
		DBG("UnLock PM state");
	else
		DBG("deviced error!");
	DBG("---------__bt_hf_agent_handler_clcc --------");
	return 0;
}

static struct hf_event hf_event_callbacks[] = {
	{ "\r\n+CIEV:", __bt_hf_agent_handler_ciev },
	{ "\r\nRING", __bt_hf_agent_handler_ring },
	{ "\r\n+CLIP:", __bt_hf_agent_handler_clip },
	{ "\r\n+BVRA:", __bt_hf_agent_handler_bvra },
	{ "\r\n+BCS:", __bt_hf_agent_handler_bcs },
	{ "\r\n+VGS:", __bt_hf_agent_handler_vgs },
	{ "\r\n+CCWA:", __bt_hf_agent_handler_ccwa },
	{ "\r\n+XSAT:", __bt_hf_agent_handler_xsat },
	{"\r\n+CLCC:", __bt_hf_agent_handler_clcc },
	{ 0 }
};

static struct hf_event hf_event_resp_callbacks[] = {
	{ "\r\n+CME ERROR:", __bt_hf_agent_handler_cme_error },
	{ "\r\nOK\r\n", __bt_hf_agent_handler_response_ok },
	{ "ERROR", __bt_hf_agent_handler_response_err },
	{ "SERR", __bt_hf_agent_handler_response_serr },
	{ 0 }
};

static int hf_handle_rx_at_cmd(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	struct hf_event *ev;

	INFO_SECURE("[HF AT CMD][RCVD] [%s]", buf);
	for (ev = hf_event_callbacks; ev->cmd; ev++) {
		if (!strncmp(buf, ev->cmd, strlen(ev->cmd)))
			return ev->callback(bt_hf_info, buf);
	}

	for (ev = hf_event_resp_callbacks; ev->cmd; ev++) {
		if (strstr(buf, ev->cmd))
			return ev->callback(bt_hf_info, buf);
	}

	DBG("Unhandled AT command.");

	return -EINVAL;
}

static int hf_handle_append_clcc_buff(char *cmd_buf, const char *buf)
{
	int buf_length;
	int cmd_length =  0;
	int cmd_buf_len = 0;
	char *pos_start, *pos_end;
	const char *datap = buf;

	cmd_buf_len = strlen(cmd_buf);
	buf_length = strlen(buf);
	DBG("buf_length = %d, cmd_buf_len = %d", buf_length, cmd_buf_len);

	if (buf_length > 0 && strstr(datap, "+CLCC")) {

		pos_start = strstr(datap, "\r\n");
		if (pos_start == NULL) {
			DBG("Invalid AT command signature..\n");
			return 0;
		}

		pos_end = g_strrstr(datap, "+CLCC");
		pos_end =  strstr(pos_end, "\r\n");
		cmd_length =   (pos_end - pos_start) + 2;
		DBG("CLCC balance Cmd Length = %d\n", cmd_length);
		memcpy(cmd_buf + cmd_buf_len, pos_start, cmd_length);
		cmd_buf[cmd_buf_len + cmd_length] = '\0';
	}
	return cmd_length;
}


static int hf_handle_rx_at_buff(bt_hf_agent_info_t *bt_hf_info, const char *buf)
{
	int buf_length;
	int cmd_length;
	char *pos_start, *pos_end;
	int tmp;
	gchar cmd_buf[BT_HF_DATA_BUF_SIZE] = {0,};
	const char *datap = buf;

	__bt_hf_agent_print_at_buffer(buf);

	buf_length = strlen(buf);

	while (buf_length > 0) {
		pos_start = strstr(datap, "\r\n");
		if (pos_start == NULL) {
			DBG("Invalid AT command start signature..\n");
			break;
		}

		datap += 2;
		pos_end = strstr(datap, "\r\n");
		if (pos_end == NULL) {
			DBG("Invalid AT command end signature..\n");
			break;
		}
		cmd_length =   (pos_end - pos_start) + 2;
		DBG("Cmd Length = %d\n", cmd_length);

		memcpy(cmd_buf, pos_start, cmd_length);
		cmd_buf[cmd_length] = '\0';

		buf_length = buf_length - cmd_length;
		datap = datap + cmd_length - 2;

		/* We need to pass all the CLCC's together to its handler */
		if (strstr(cmd_buf, "+CLCC")) {
			tmp = hf_handle_append_clcc_buff(cmd_buf, datap);
			datap += tmp;
			buf_length = buf_length - tmp;
		}
		hf_handle_rx_at_cmd(bt_hf_info, cmd_buf);
		DBG("Pending buf_length = %d\n", buf_length);
	}
	return TRUE;

}
static gboolean __bt_hf_agent_data_cb(GIOChannel *chan, GIOCondition cond,
					bt_hf_agent_info_t *bt_hf_info)
{
	gchar buf[BT_HF_DATA_BUF_SIZE] = {0,};
	gsize read;
	GError *gerr = NULL;

	if (cond & (G_IO_ERR | G_IO_HUP)) {
		ERR("HF connection terminated...! cond = %d\n", cond);
		is_hf_connected = FALSE;
#ifdef FEATURE_TIZENW
		__bt_start_adv();
#endif
		__bt_hf_agent_release();
		return FALSE;
	}

	if (g_io_channel_read_chars(chan, buf, sizeof(buf) - 1, &read, &gerr)
			!= G_IO_STATUS_NORMAL) {
		if (gerr) {
			ERR("Read failed, cond = [%d], Err msg = [%s]",
							cond, gerr->message);
			g_error_free(gerr);
		}
		return TRUE;
	}
	buf[read] = '\0';
	DBG("***  Received AT buffer packet ****");
	hf_handle_rx_at_buff(bt_hf_info, buf);

	return TRUE;
}

static void __bt_hf_agent_start_watch(bt_hf_agent_info_t *bt_hf_info)
{
	bt_hf_info->watch_id = g_io_add_watch(bt_hf_info->io_chan,
			G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
			(GIOFunc) __bt_hf_agent_data_cb, bt_hf_info);
}

static void __bt_hf_agent_stop_watch(bt_hf_agent_info_t *bt_hf_info)
{
	g_source_remove(bt_hf_info->watch_id);
}

static gboolean __bt_hf_channel_write(GIOChannel *io, gchar *data,
					gsize count)
{
	gsize written = 0;
	GIOStatus status;

	while (count > 0) {
		status = g_io_channel_write_chars(io, data, count, &written,
						NULL);
		if (status != G_IO_STATUS_NORMAL)
			return FALSE;

		data += written;
		count -= written;
	}
	return TRUE;
}

static gboolean __bt_hf_send_only(bt_hf_agent_info_t *bt_hf_info, gchar *data,
								gsize count)
{
	GIOChannel *io_chan = bt_hf_info->io_chan;

	if (!__bt_hf_channel_write(io_chan, data, count))
		return FALSE;

	g_io_channel_flush(io_chan, NULL);

	DBG("Send only buffer size =[%d] - Send <<<<<| %s", count, data);

	return TRUE;
}

static gboolean __bt_hf_send_and_read(bt_hf_agent_info_t *bt_hf_info,
		gchar *data, gchar *response, gsize count)
{
	GIOChannel *io_chan = bt_hf_info->io_chan;
	gsize rd_size = 0;
	gboolean recvd_ok = FALSE;
	gboolean recvd_error = FALSE;
	gboolean recvd_sec_error = FALSE;
	gchar *resp_buf = response;
	gsize toread = BT_HF_DATA_BUF_SIZE - 1;
	int i = 0;
	int fd;
	int err;
	struct pollfd p;

	/* Should not send cmds if DUT send a command and wait the response */
	if (prev_cmd[0] != 0) {
		DBG("DUT is waiting a respond for previous TX cmd. Skip sennding.");
		return FALSE;
	}

	memset(resp_buf, 0, BT_HF_DATA_BUF_SIZE);

	if (!__bt_hf_channel_write(io_chan, data, count))
		return FALSE;

	g_io_channel_flush(io_chan, NULL);

	INFO_SECURE("[HF AT CMD][SENT] %s", data);
	DBG("Send buffer size =[%d] - Send <<<<< %s", count, data);

	fd = g_io_channel_unix_get_fd(io_chan);
	p.fd = fd;
	p.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

	/* Maximun 8 seconds of poll or 8 minus no of cmd received */
	for (i = 1; i <= MAX_WAITING_DELAY; i++) {
		DBG("Loop Counter = %d", i);
		p.revents = 0;
		err = poll(&p, 1, 1000);
		if (err < 0) {
			DBG("Loop Counter = %d, >>>> Poll error happen", i);
			return FALSE;
		} else if (err == 0) {
			DBG("Loop Counter = %d, >>>> Poll Timeout", i);
		}

		if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			DBG("Loop Counter = %d, >> Poll ERR/HUP/INV (%d)",
								i, p.revents);

			__bt_hf_agent_emit_signal(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE,
				"Disconnected",
				g_variant_new("(s)",
						bt_hf_info->remote_addr));

			bt_hf_info->state = BT_HF_STATE_DISCONNECTED;
			return FALSE;
		}

		if (p.revents & POLLIN) {
			rd_size = read(fd, resp_buf, toread);
			resp_buf[rd_size] = '\0';
			INFO_SECURE("[HF AT CMD][RCVD] (send&read) [%s]", resp_buf);
			recvd_ok = NULL != strstr(resp_buf, BT_HF_OK_RESPONSE);
			recvd_error = NULL != strstr(resp_buf, BT_HF_ERROR_RESPONSE);
			recvd_sec_error = NULL != strstr(resp_buf, BT_HF_SEC_ERROR_RESPONSE);

			resp_buf += rd_size;
			toread -= rd_size;

			if (recvd_ok || recvd_error || recvd_sec_error) {
				DBG("Break Loop Counter = %d", i);
				break;
			}
		}
	}

	/* Once service level connection is established we need to handle
	 * all the intermediate AT commands */
	if (bt_hf_info->state == BT_HF_STATE_CONNECTED)
		hf_handle_rx_at_buff(bt_hf_info, response);
	return TRUE;
}

static GSList *__bt_hf_parse_indicator_names(gchar *names, GSList *indies)
{
	gchar *current = names - 1;
	GSList *result = indies;
	gchar *next;
	struct indicator *ind;
	DBG("Indicator buffer = %s", names);
	while (current != NULL) {
		current += 2;
		next = strstr(current, ",(");
		ind = g_slice_new(struct indicator);
		g_strlcpy(ind->descr, current, BT_HF_INDICATOR_DESCR_SIZE);
		ind->descr[(intptr_t) next - (intptr_t) current] = '\0';
		result = g_slist_append(result, (gpointer) ind);
		current = strstr(next + 1, ",(");
	}
	return result;
}

/* get values from <val0>,<val1>,... */
static GSList *__bt_hf_parse_indicator_values(gchar *values, GSList *indies)
{
	gint val;
	gchar *current = values - 1;
	GSList *runner = indies;
	struct indicator *ind;
	DBG("Indicator string = %s", values);
	while (current != NULL) {
		current += 1;
		sscanf(current, "%1d", &val);
		current = strchr(current, ',');
		ind = g_slist_nth_data(runner, 0);
		ind->value = val;
		runner = g_slist_next(runner);
	}
	return indies;
}

static guint __bt_hf_get_hold_mpty_features(gchar *features)
{
	guint result = 0;

	if (strstr(features, "0"))
		result |= BT_HF_CHLD_0;

	if (strstr(features, "1"))
		result |= BT_HF_CHLD_1;

	if (strstr(features, "1x"))
		result |= BT_HF_CHLD_1x;

	if (strstr(features, "2"))
		result |= BT_HF_CHLD_2;

	if (strstr(features, "2x"))
		result |= BT_HF_CHLD_2x;

	if (strstr(features, "3"))
		result |= BT_HF_CHLD_3;

	if (strstr(features, "4"))
		result |= BT_HF_CHLD_4;

	return result;
}

static gboolean __bt_hf_agent_sco_conn_cb(GIOChannel *chan, GIOCondition cond, gpointer user_data)
{
	bt_hf_agent_info_t *bt_hf_info = user_data;

	DBG("");
	if (cond & G_IO_NVAL)
		return FALSE;

	if (cond & (G_IO_HUP | G_IO_ERR)) {
		g_io_channel_shutdown(chan, TRUE, NULL);
		close(bt_hf_info->cli_sco_fd);

		DBG("Emit AudioDisconnected Signal");

		sco_audio_connected = BT_HF_AUDIO_DISCONNECTED;

		__bt_hf_agent_emit_signal(gdbus_conn,
				BT_HF_AGENT_OBJECT_PATH,
				BT_HF_SERVICE_INTERFACE,
				"AudioDisconnected", NULL);

		return FALSE;
	}

	return TRUE;
}

static gboolean __bt_agent_query_and_update_call_list(gpointer data)
{
	DBG("+");
	bt_hf_agent_info_t *bt_hf_info = data;

	if (bt_hf_info->cli_sco_fd >= 0)
		__bt_hf_agent_handle_call_list(bt_hf_info);
	else
		DBG("SCO Audio is already disconnected");

	DBG("-");

	return FALSE;
}

static gboolean __bt_hf_agent_sco_accept_cb(GIOChannel *chan, GIOCondition cond, gpointer user_data)
{
	bt_hf_agent_info_t *bt_hf_info = user_data;
	int sco_skt;
	int cli_sco_sock;
	GIOChannel *sco_io;

	DBG("");

	if (cond & G_IO_NVAL)
		return FALSE;

	sco_skt = g_io_channel_unix_get_fd(chan);

	if (cond & (G_IO_HUP | G_IO_ERR)) {
		close(sco_skt);
		return FALSE;
	}

	cli_sco_sock = accept(sco_skt, NULL, NULL);
	if (cli_sco_sock < 0)
		return FALSE;

	bt_hf_info->cli_sco_fd = cli_sco_sock;

	sco_io = g_io_channel_unix_new(cli_sco_sock);
	g_io_channel_set_close_on_unref(sco_io, TRUE);
	g_io_channel_set_encoding(sco_io, NULL, NULL);
	g_io_channel_set_flags(sco_io, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_buffered(sco_io, FALSE);

	g_io_add_watch(sco_io, G_IO_HUP | G_IO_ERR | G_IO_NVAL,
					__bt_hf_agent_sco_conn_cb, bt_hf_info);

	/* S-Voice app requires the AudioConnected signal earlier */
	DBG("Emit AudioConnected Signal");

	sco_audio_connected = BT_HF_AUDIO_CONNECTED;

	__bt_hf_agent_emit_signal(gdbus_conn,
			BT_HF_AGENT_OBJECT_PATH,
			BT_HF_SERVICE_INTERFACE,
			"AudioConnected", NULL);

	/* In the case of incoming call, the call app is already launched,
	 * hence AudioConnected signal is enough to update the call status.
	 * In the case of outgoing call we need to lauch the callapp.
	 */
	if (bt_hf_info->ciev_call_status == 0
		&& bt_hf_info->ciev_call_setup_status == 0)
		__bt_get_current_indicators(bt_hf_info);

	g_idle_add(__bt_agent_query_and_update_call_list, bt_hf_info);

	return TRUE;
}

static gboolean __bt_hf_agent_sco_accept(bt_hf_agent_info_t *bt_hf_info)
{
	struct sockaddr_sco addr;
	GIOChannel *sco_io;
	bdaddr_t bd_addr = {{0},};
	int sco_skt;

	if (bt_hf_info->state != BT_HF_STATE_CONNECTED)
		return FALSE;

	/* Create socket */
	sco_skt = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);
	if (sco_skt < 0) {
		DBG("Can't create socket:\n");
		return FALSE;
	}

	/* Bind to local address */
	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	memcpy(&addr.sco_bdaddr, &bd_addr, sizeof(bdaddr_t));

	if (bind(sco_skt, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		DBG("Can't bind socket:\n");
		goto error;
	}

	if (listen(sco_skt, 1)) {
		DBG("Can not listen on the socket:\n");
		goto error;
	}

	sco_io = g_io_channel_unix_new(sco_skt);
	g_io_channel_set_close_on_unref(sco_io, TRUE);
	g_io_channel_set_encoding(sco_io, NULL, NULL);
	g_io_channel_set_flags(sco_io, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_buffered(sco_io, FALSE);

	bt_hf_info->sco_fd = sco_skt;
	bt_hf_info->sco_io_chan = sco_io;

	bt_hf_info->sco_watch_id = g_io_add_watch(sco_io,
			G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL, __bt_hf_agent_sco_accept_cb, bt_hf_info);

	g_io_channel_unref(sco_io);

	return TRUE;

error:
	close(sco_skt);
	return FALSE;
}

static gboolean __bt_get_supported_indicators(bt_hf_agent_info_t *bt_hf_info)
{
	gchar buf[BT_HF_DATA_BUF_SIZE] = {0,};
	gboolean ret;

	ret = __bt_hf_send_and_read(bt_hf_info, BT_HF_INDICATORS_SUPP, buf,
				sizeof(BT_HF_INDICATORS_SUPP) - 1);
	if (!ret || !strstr(buf, "+CIND:"))
		return FALSE;

	bt_hf_info->indies = __bt_hf_parse_indicator_names(strchr(buf, '('), NULL);

	return TRUE;
}

static gboolean __bt_get_bia_cmd(bt_hf_agent_info_t *bt_hf_info, gchar *cmd, gsize cmd_size)
{
	GSList *l;
	gsize ret;

	if (bt_hf_info == NULL || cmd == NULL) {
		ERR("Invalid parameter");
		return FALSE;
	}

	ret = g_strlcpy(cmd, BT_HF_INDICATORS_ACTIVATION, cmd_size);

	for (l = bt_hf_info->indies; l != NULL; l = g_slist_next(l)) {
		ret = g_strlcat(cmd, "0,", cmd_size);
		if (ret >= cmd_size) {
			ERR("Too many indices");
			return FALSE;
		}

	}

	cmd[ret - 1] = '\0';
	DBG("BIA Str : %s", cmd);

	ret = g_strlcat(cmd, "\r", cmd_size);
	if (ret >= cmd_size) {
		ERR("Too many indices");
		return FALSE;
	}

	return TRUE;
}

static gboolean __bt_get_current_indicators(bt_hf_agent_info_t *bt_hf_info)
{
	gchar buf[BT_HF_DATA_BUF_SIZE] = {0,};
	gboolean ret;
	gchar *str;
	GSList *l;
	int index =  1;

	ret = __bt_hf_send_and_read(bt_hf_info, BT_HF_INDICATORS_VAL, buf,
		sizeof(BT_HF_INDICATORS_VAL) - 1);
	if (!ret || !strstr(buf, "+CIND:"))
		return FALSE;

	/* if buf has other command prefix, skip it */
	str = strstr(buf, "+CIND");
	if (str == NULL)
		return FALSE;

	bt_hf_info->indies = __bt_hf_parse_indicator_values(str + 6, bt_hf_info->indies);

	/* Parse the updated value */
	for (l = bt_hf_info->indies; l != NULL; l = g_slist_next(l), ++index) {
		struct indicator *ind = l->data;
		if (!ind) {
			DBG("Index is NULL");
			break;
		}

		if (0 == g_strcmp0(ind->descr, "\"call\"")) {
			DBG("CIND Match found index = %d, %s, value = %d",
						index, ind->descr, ind->value);
			bt_hf_info->ciev_call_status = ind->value;
			if (ind->value > 0) {
				bt_hf_info->is_dialing = FALSE;
				bt_hf_info->call_active = TRUE;
			}
		} else if (0 == g_strcmp0(ind->descr, "\"callsetup\"")) {
			DBG("CIND Match found index = %d, %s, value = %d",
						index, ind->descr, ind->value);
			bt_hf_info->ciev_call_setup_status = ind->value;
			if (!bt_hf_info->is_dialing && ind->value > 0)
				bt_hf_info->is_dialing = TRUE;
		}
	}

	return TRUE;
}

static gboolean __bt_establish_service_level_conn(bt_hf_agent_info_t *bt_hf_info)
{
	gchar buf[BT_HF_DATA_BUF_SIZE];
	gchar cmd_buf[BT_HF_CMD_BUF_SIZE] = {0};
	gboolean ret;
	char *buf_ptr;
	guint feature = BT_HF_FEATURE_EC_ANDOR_NR |
			BT_HF_FEATURE_CALL_WAITING_AND_3WAY |
			BT_HF_FEATURE_CLI_PRESENTATION |
			BT_HF_FEATURE_VOICE_RECOGNITION |
			BT_HF_FEATURE_REMOTE_VOLUME_CONTROL |
			BT_HF_FEATURE_ENHANCED_CALL_STATUS |
			BT_HF_FEATURE_CODEC_NEGOTIATION;

	snprintf(cmd_buf, sizeof(cmd_buf), BT_HF_FEATURES, feature);
	ret = __bt_hf_send_and_read(bt_hf_info, cmd_buf, buf,
				strlen(cmd_buf));
	if (!ret )
		return FALSE;

	buf_ptr = strstr(buf, "\r\n+BRSF:");
	if (buf_ptr == NULL)
		return FALSE;

	if (!ret || sscanf(buf_ptr, "\r\n+BRSF:%5d", &bt_hf_info->ag_features) != 1)
		return FALSE;
	DBG("Gateway supported features are 0x%X", bt_hf_info->ag_features);

	if (bt_hf_info->ag_features & BT_AG_FEATURE_CODEC_NEGOTIATION) {
		ret = _hf_agent_codec_setup(BT_HF_CODEC_ID_MSBC);
		if (ret != BT_HF_AGENT_ERROR_NONE)
			DBG("Unable to set the default WBC codec");

		ret = __bt_hf_send_available_codec(bt_hf_info);
		if (!ret)
			return FALSE;
	} else {
		/* Default codec is NB */
		ret = _hf_agent_codec_setup(BT_HF_CODEC_ID_CVSD);
		if (ret != BT_HF_AGENT_ERROR_NONE)
			DBG("Unable to set the default NBC codec");
	}

	ret = __bt_get_supported_indicators(bt_hf_info);
	if (!ret)
		return FALSE;


	ret = __bt_get_current_indicators(bt_hf_info);
	if (!ret)
		return FALSE;

	ret = __bt_hf_send_and_read(bt_hf_info, BT_HF_INDICATORS_ENABLE, buf,
					sizeof(BT_HF_INDICATORS_ENABLE) - 1);
	if (!ret || !strstr(buf, "OK"))
		return FALSE;

	if ((bt_hf_info->ag_features & BT_AG_FEATURE_3WAY) != 0) {
		ret = __bt_hf_send_and_read(bt_hf_info, BT_HF_HOLD_MPTY_SUPP,
					buf, sizeof(BT_HF_HOLD_MPTY_SUPP) - 1);
		if (!ret || !strstr(buf, "+CHLD:")) {
			ERR("Unable to get the CHLD Supported info");
			return FALSE;
		}
		bt_hf_info->hold_multiparty_features = __bt_hf_get_hold_mpty_features(
							strchr(buf, '('));
	} else
		bt_hf_info->hold_multiparty_features = 0;

	DBG("Service layer connection successfully established...!");

	__bt_hf_send_and_read(bt_hf_info, BT_HF_CALLER_IDENT_ENABLE, buf,
			sizeof(BT_HF_CALLER_IDENT_ENABLE) - 1);
	__bt_hf_send_and_read(bt_hf_info, BT_HF_CARRIER_FORMAT, buf,
			sizeof(BT_HF_CARRIER_FORMAT) - 1);
	__bt_hf_send_and_read(bt_hf_info, BT_HF_CALLWAIT_NOTI_ENABLE, buf,
			sizeof(BT_HF_CALLWAIT_NOTI_ENABLE) - 1);

	if ((bt_hf_info->ag_features & BT_AG_FEATURE_NREC) != 0)
		__bt_hf_send_and_read(bt_hf_info, BT_HF_NREC, buf,
						sizeof(BT_HF_NREC) - 1);

	if ((bt_hf_info->ag_features & BT_AG_FEATURE_EXTENDED_RES_CODE) != 0)
		__bt_hf_send_and_read(bt_hf_info, BT_HF_EXTENDED_RESULT_CODE,
			buf, sizeof(BT_HF_EXTENDED_RESULT_CODE) - 1);

	if (__bt_get_bia_cmd(bt_hf_info, cmd_buf, sizeof(cmd_buf)) == TRUE)
		__bt_hf_send_and_read(bt_hf_info, cmd_buf, buf, strlen(cmd_buf));
	else
		ERR("__bt_get_bia_cmd is failed");

	ret = __bt_hf_send_and_read(bt_hf_info, BT_HF_XSAT, buf,
						sizeof(BT_HF_XSAT) - 1);
	DBG("sent BT_HF_XSAT");

	/* send Bluetooth Samsung Support Feature cmd */
	ret = __bt_hf_send_and_read(bt_hf_info, BT_HF_BSSF, buf,
						sizeof(BT_HF_BSSF) - 1);
	DBG("sent BT_HF_BSSF");

	return TRUE;
}

static void __bt_hf_agent_sigterm_handler(int signo)
{
	ERR("***** Signal handler came with signal %d *****", signo);
	__bt_hf_agent_emit_signal(gdbus_conn,
			BT_HF_AGENT_OBJECT_PATH,
			BT_HF_SERVICE_INTERFACE,
			"CallEnded", NULL);
	DBG("CallEnded Signal done");
	if (gmain_loop) {
		g_main_loop_quit(gmain_loop);
		DBG("Exiting");
		gmain_loop = NULL;
	} else {
		DBG("Terminating HF agent");
		exit(0);
	}
}

static void __bt_convert_addr_type_to_rev_string(char *address,
				unsigned char *addr)
{
	ret_if(address == NULL);
	ret_if(addr == NULL);

	g_snprintf(address, BT_ADDRESS_STRING_SIZE,
			"%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
			addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static gboolean __bt_agent_request_service_level_conn(gpointer data)
{
	char *remote_addr;
	GError *error;
	int bt_device_state = VCONFKEY_BT_DEVICE_NONE;

	DBG("+");
	memset(prev_cmd, 0, BT_HF_CMD_BUF_SIZE);

	if (!__bt_establish_service_level_conn(&bt_hf_info)) {
		error = __bt_hf_agent_set_error(BT_HF_AGENT_ERROR_CONNECTION_FAILED);
		g_error_free(error);
		//:TODO: in case of any error Close the socket
		DBG("Service Level Connection is fail");
		goto done;
	}

	bt_hf_info.state = BT_HF_STATE_CONNECTED;

	__bt_hf_agent_sco_accept(&bt_hf_info);

	__bt_hf_agent_start_watch(&bt_hf_info);

	remote_addr = bt_hf_info.remote_addr;

	DBG_SECURE("Address is : %s", remote_addr);

	if (vconf_get_int(VCONFKEY_BT_DEVICE, &bt_device_state) == 0) {
		DBG("BT device state is : 0x%X", bt_device_state);
		bt_device_state |= VCONFKEY_BT_DEVICE_AG_CONNECTED;
		if (vconf_set_int(VCONFKEY_BT_DEVICE, bt_device_state) != 0) {
			DBG("vconf_set_int failed");
		}
	} else {
		DBG("vconf_get_str failed");
	}

	__bt_hf_agent_emit_signal(gdbus_conn,
			BT_HF_AGENT_OBJECT_PATH,
			BT_HF_SERVICE_INTERFACE,
			"Connected",
			g_variant_new("(s)", remote_addr));

	/* Request the call list and launch call app if required */
	__bt_hf_agent_handle_call_list(&bt_hf_info);

#ifdef FEATURE_TIZENW
	__bt_stop_adv();
#endif
done:
	DBG("-");
	return FALSE;
}

static gboolean __bt_hf_agent_connection(gint32 fd, guint16 version)
{
	GIOFlags flags;

	struct sockaddr_remote address;
	socklen_t address_len;

	DBG("**** New HFP connection ****\n");

	is_hf_connected = TRUE;

	if (adv_timer_id > 0) {
		DBG("**** Alarm is going on. It is cancelled ****\n");
		alarmmgr_remove_alarm(adv_timer_id);
		adv_timer_id = 0;
	}

	address_len = sizeof(address);
	if (getpeername(fd, (struct sockaddr *) &address, &address_len) != 0)
		DBG("BD_ADDR is NULL");

	DBG("RFCOMM connection for HFP is completed. Fd = [%d]\n", fd);
	bt_hf_info.fd = fd;
	bt_hf_info.io_chan = g_io_channel_unix_new(bt_hf_info.fd);
	flags = g_io_channel_get_flags(bt_hf_info.io_chan);

	flags &= ~G_IO_FLAG_NONBLOCK;
	flags &= G_IO_FLAG_MASK;
	g_io_channel_set_flags(bt_hf_info.io_chan, flags, NULL);
	g_io_channel_set_encoding(bt_hf_info.io_chan, NULL, NULL);
	g_io_channel_set_buffered(bt_hf_info.io_chan, FALSE);

	bt_hf_info.remote_addr = g_malloc0(BT_ADDRESS_STRING_SIZE);
	__bt_convert_addr_type_to_rev_string(bt_hf_info.remote_addr,
						address.remote_bdaddr.b);

	g_idle_add(__bt_agent_request_service_level_conn, NULL);

	return TRUE;
}

static void __bt_hf_agent_indicator_slice_free(gpointer mem)
{
	g_slice_free(struct indicator, mem);
}

static gboolean __bt_hf_agent_release(void)
{
	int bt_device_state = VCONFKEY_BT_DEVICE_NONE;

	g_slist_foreach(bt_hf_info.indies, (GFunc) __bt_hf_agent_indicator_slice_free, NULL);
	g_slist_free(bt_hf_info.indies);

	g_io_channel_shutdown(bt_hf_info.io_chan, TRUE, NULL);
	g_io_channel_unref(bt_hf_info.io_chan);
	bt_hf_info.io_chan = NULL;

	g_source_remove(bt_hf_info.sco_watch_id);

	bt_hf_info.state = BT_HF_STATE_DISCONNECTED;

	if (vconf_get_int(VCONFKEY_BT_DEVICE, &bt_device_state) == 0) {
		DBG("BT device state is : 0x%X", bt_device_state);
		bt_device_state ^= VCONFKEY_BT_DEVICE_AG_CONNECTED;
		if (vconf_set_int(VCONFKEY_BT_DEVICE, bt_device_state) != 0) {
			DBG("vconf_set_int failed");
		}
	} else {
		DBG("vconf_get_str failed");
	}

	__bt_hf_agent_stop_watch(&bt_hf_info);
	__bt_hf_agent_emit_signal(gdbus_conn,
			BT_HF_AGENT_OBJECT_PATH,
			BT_HF_SERVICE_INTERFACE,
			"Disconnected",
			g_variant_new("(s)", bt_hf_info.remote_addr));

	g_free(bt_hf_info.remote_addr);

	__launch_system_popup(BT_AGENT_EVENT_HANDSFREE_DISCONNECT);
	return TRUE;
}

static gboolean __bt_hf_agent_connection_release(void)
{
	return __bt_hf_agent_release();
}

static void __bt_hf_agent_register(void)
{
	DBG("+");

	gchar *path = g_strdup(BT_HF_BLUEZ_OBJECT_PATH);

	__bt_hf_agent_gdbus_method_send(BLUEZ_SERVICE_NAME,
				g_variant_new("(o)", path),
				BLUEZ_HF_INTERFACE_NAME,
				"RegisterAgent");

	g_free(path);

	DBG("-");
	return;
}

static void __bt_hf_agent_unregister(void)
{
	DBG("+");

	gchar *path = g_strdup(BT_HF_BLUEZ_OBJECT_PATH);

	if (g_obj_path) {
		__bt_hf_agent_gdbus_method_send(BLUEZ_SERVICE_NAME,
						g_variant_new("(o)", path),
						BLUEZ_HF_INTERFACE_NAME,
						"UnregisterAgent");
		g_free(g_obj_path);
		g_obj_path = NULL;
	}

	g_free(path);

	DBG("-");
	return;
}

static void __bt_hf_agent_filter_cb(GDBusConnection *connection,
						 const gchar *sender_name,
						 const gchar *object_path,
						 const gchar *interface_name,
						 const gchar *signal_name,
						 GVariant *parameters,
						 gpointer user_data)
{
	DBG("+");
	char *path = NULL;

	if (strcasecmp(signal_name, "AdapterAdded") == 0) {

		g_variant_get(parameters, "(&o)", &path);
		if (!path) {
			ERR("Invalid adapter path");
			return;
		}

		DBG("Adapter Path = [%s]", path);

		g_obj_path = g_strdup(path);
		DBG("g_obj_path = [%s]", g_obj_path);

		__bt_hf_agent_register();
#ifdef FEATURE_TIZENW
		__bt_start_adv_timer();
		__bt_hf_auto_connect();
#endif /* FEATURE_TIZENW */
	} else if (strcasecmp(signal_name, "AdapterRemoved") == 0) {
		__bt_hf_agent_unregister();
	}

	DBG("-");
}

static void __bt_hf_agent_dbus_init(void)
{

	DBG("+");

	if (__bt_hf_get_gdbus_connection() == NULL) {
		ERR("Error in creating the gdbus connection\n");
		return;
	}
	if (!__bt_hf_register_profile_methods()) {
		ERR("Error in register_profile_methods\n");
		return;
	}

	owner_sig_id = g_dbus_connection_signal_subscribe(gdbus_conn,
				NULL, "org.bluez.Manager", NULL, NULL, NULL, 0,
				__bt_hf_agent_filter_cb, NULL, NULL);

	DBG("-");
	return;
}

static void __bt_hf_agent_dbus_deinit(void)
{

	if (service_gproxy) {
		g_object_unref(service_gproxy);
		service_gproxy = NULL;
	}

	if (gdbus_conn) {
		if (owner_sig_id != -1)
			g_dbus_connection_signal_unsubscribe(gdbus_conn,
						owner_sig_id);

		g_object_unref(gdbus_conn);
		gdbus_conn = NULL;
	}
	return;
}

static int __bt_hf_agent_convert_cme_error(int value)
{
	ERR("CMEE Error, value = %d", value);

	switch (value) {
	case BT_AG_CME_ERROR_NO_PHONE_CONNECTION:
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	case BT_AG_CME_ERROR_NOT_ALLOWED:
		return BT_HF_AGENT_ERROR_NOT_ALLOWED;
	case BT_AG_CME_ERROR_NOT_SUPPORTED:
		return BT_HF_AGENT_ERROR_NOT_SUPPORTED;

	default:
		return BT_HF_AGENT_ERROR_INTERNAL;
		break;
	}
}
static int _hf_agent_answer_call(void)
{
	int ret;
	char buf[BT_HF_DATA_BUF_SIZE];
	DBG("+\n");

	if (bt_hf_info.state != BT_HF_STATE_CONNECTED) {
		ERR("HF not Connected");
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	}

	ret = __bt_hf_send_and_read(&bt_hf_info, BT_HF_ANSWER_CALL, buf,
				sizeof(BT_HF_ANSWER_CALL) - 1);
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(buf, BT_HF_OK_RESPONSE)) {
		DBG("OK, return\n");
		return BT_HF_AGENT_ERROR_NONE;
	}

	if ((bt_hf_info.ag_features & BT_AG_FEATURE_EXTENDED_RES_CODE) != 0) {
		int value;
		char *buf_ptr;
		buf_ptr = strstr(buf, "\r\n+CME ERROR:");
		if (buf_ptr) {
			if (sscanf(buf_ptr, "\r\n+CME ERROR:%2d\r\n", &value))
				return __bt_hf_agent_convert_cme_error(value);
		}
	}

	DBG("-\n");
	return BT_HF_AGENT_ERROR_INTERNAL;
}

static int _hf_agent_terminate_call(void)
{
	int ret;
	char buf[BT_HF_DATA_BUF_SIZE];
	DBG("+\n");
	if (bt_hf_info.state != BT_HF_STATE_CONNECTED) {
		ERR("HF not Connected");
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	}

	ret = __bt_hf_send_and_read(&bt_hf_info, BT_HF_END_CALL, buf,
				sizeof(BT_HF_END_CALL) - 1);
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(buf, BT_HF_OK_RESPONSE)) {
		DBG("OK, return\n");
		return BT_HF_AGENT_ERROR_NONE;
	}
	DBG("-\n");
	return BT_HF_AGENT_ERROR_INTERNAL;
}

static int _hf_agent_dial_no(char *no)
{
	int ret;
	char buf[BT_MAX_TEL_NUM_STR + 6] = {0};
	char rbuf[BT_HF_DATA_BUF_SIZE];

	if (bt_hf_info.state != BT_HF_STATE_CONNECTED) {
		ERR("HF not Connected");
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	}

	if (strlen(no) > 0) {
		snprintf(buf, sizeof(buf),  BT_HF_DIAL_NO, no);

		/* prev_cmd is meant for only meant for ATD Error handling */
		snprintf(prev_cmd, BT_HF_CMD_BUF_SIZE, "%s", buf);

		ret = __bt_hf_send_only(&bt_hf_info, buf, strlen(buf));

		if (!ret)
			return BT_HF_AGENT_ERROR_INTERNAL;

		return BT_HF_AGENT_ERROR_NONE;
	}
	ret = __bt_hf_send_and_read(&bt_hf_info, BT_HF_REDIAL, rbuf,
						sizeof(BT_HF_REDIAL) - 1);
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(rbuf, BT_HF_OK_RESPONSE))
		return BT_HF_AGENT_ERROR_NONE;

	if ((bt_hf_info.ag_features & BT_AG_FEATURE_EXTENDED_RES_CODE) != 0) {
		int value;
		char *rbuf_ptr;
		rbuf_ptr =  strstr(rbuf, "\r\n+CME ERROR:");
		if (rbuf_ptr) {
			if (sscanf(rbuf_ptr, "\r\n+CME ERROR:%2d\r\n", &value))
				return __bt_hf_agent_convert_cme_error(value);
		}
	}

	return BT_HF_AGENT_ERROR_INTERNAL;
}

static int _hf_agent_voice_recognition(unsigned int status)
{
	int ret;
	char buf[20] = {0};
	char rbuf[BT_HF_DATA_BUF_SIZE];

	if (bt_hf_info.state != BT_HF_STATE_CONNECTED) {
		ERR("HF not Connected");
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	}

	snprintf(buf, sizeof(buf),  BT_HF_VOICE_RECOGNITION, status);
	ret = __bt_hf_send_and_read(&bt_hf_info, buf, rbuf,
				strlen(buf));
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(rbuf, BT_HF_OK_RESPONSE)) {
		DBG("OK, return\n");
		return BT_HF_AGENT_ERROR_NONE;
	}
	return BT_HF_AGENT_ERROR_INTERNAL;
}

static int _hf_agent_set_speaker_gain(unsigned int gain)
{
	int ret;
	char buf[20] = {0};
	char rbuf[BT_HF_DATA_BUF_SIZE];

	if (bt_hf_info.state != BT_HF_STATE_CONNECTED) {
		ERR("HF not Connected");
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	}

	if (gain > BT_HF_MAX_SPEAKER_GAIN)
		return BT_HF_AGENT_ERROR_INVALID_PARAM;

	snprintf(buf, sizeof(buf),  BT_HF_SPEAKER_GAIN, gain);
	ret = __bt_hf_send_and_read(&bt_hf_info, buf, rbuf,
				strlen(buf));
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(rbuf, BT_HF_OK_RESPONSE)) {
		DBG("OK, return\n");
		return BT_HF_AGENT_ERROR_NONE;
	}
	return BT_HF_AGENT_ERROR_INTERNAL;

}

static int _hf_agent_send_dtmf(char *dtmf)
{
	int ret;
	char buf[20] = {0};

	if (strlen(dtmf) <= 0)
		return BT_HF_AGENT_ERROR_INVALID_PARAM;

	if (bt_hf_info.state != BT_HF_STATE_CONNECTED) {
		ERR("HF not Connected");
		return BT_HF_AGENT_ERROR_NOT_CONNECTED;
	}

	snprintf(buf, sizeof(buf),  BT_HF_DTMF, dtmf);
	ret = __bt_hf_send_only(&bt_hf_info, buf, strlen(buf));
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	return BT_HF_AGENT_ERROR_NONE;
}


static int _hf_agent_send_cmd_read_resp(char *cmd)
{
	int ret;
	char buf[BT_HF_DATA_BUF_SIZE];

	if (strlen(cmd) <= 0)
		return BT_HF_AGENT_ERROR_INVALID_PARAM;

	ret = __bt_hf_send_and_read(&bt_hf_info, cmd, buf,
				strlen(cmd));
	if (!ret)
		return BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(buf, BT_HF_OK_RESPONSE)) {
		DBG("OK, return\n");
		return BT_HF_AGENT_ERROR_NONE;
	}

	return BT_HF_AGENT_ERROR_INTERNAL;
}

static gboolean bt_hf_agent_sco_disconnect(void)
{
	DBG("+");
	close(bt_hf_info.cli_sco_fd);
	bt_hf_info.cli_sco_fd = -1;

	DBG("Emit AudioDisconnected Signal");
	__bt_hf_agent_emit_signal(gdbus_conn,
			BT_HF_AGENT_OBJECT_PATH,
			BT_HF_SERVICE_INTERFACE,
			"AudioDisconnected", NULL);
	DBG("-");
	return TRUE;
}

static GVariant *bt_hf_agent_request_call_list()
{
	GSList *call_list = NULL;
	GVariant *var_data;
	DBG("+");

	call_list = __bt_hf_get_call_list(&bt_hf_info);
	if (!call_list) {
		DBG("call list is NULL");
		return NULL;
	}

	var_data = __bt_hf_agent_get_call_status_info(call_list);
	__bt_hf_free_call_list(call_list);

	DBG("-");
	return var_data;
}

static int bt_hf_agent_send_at_cmd(char *atcmd)

{
	gboolean ret;
	char cmd_buf[BT_HF_CMD_BUF_SIZE * 2] = {0, };
	char rbuf[BT_HF_DATA_BUF_SIZE];
	DBG("+");

	if (atcmd == NULL)
		return  BT_HF_AGENT_ERROR_INVALID_PARAM;

	if (bt_hf_info.state != BT_HF_STATE_CONNECTED)
		return  BT_HF_AGENT_ERROR_NOT_CONNECTED;

	strncpy(cmd_buf, atcmd, sizeof(cmd_buf) - 1);
	strncat(cmd_buf, "\r", (sizeof(cmd_buf) - 1) - strlen(cmd_buf));

	ret = __bt_hf_send_and_read(&bt_hf_info, cmd_buf, rbuf, strlen(cmd_buf));
	if (!ret)
		ret = BT_HF_AGENT_ERROR_INTERNAL;

	if (strstr(rbuf, BT_HF_OK_RESPONSE))
		DBG("OK, return\n");

	DBG("-");
	return BT_HF_AGENT_ERROR_NONE;
}

int main(void)
{
	int ret_val;
	struct sigaction sa;
	const char *pkg_name = "org.tizen.hf_agent";

	DBG("Starting Bluetooth HF agent");

	g_type_init();

	ret_val = alarmmgr_init(pkg_name);
	if (ret_val != ALARMMGR_RESULT_SUCCESS) {
		DBG("Alarm Init failed ");
		return FALSE;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_NOCLDSTOP;
	sa.sa_handler = __bt_hf_agent_sigterm_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
	gmain_loop = g_main_loop_new(NULL, FALSE);

	if (gmain_loop == NULL) {
		ERR("GMainLoop create failed\n");
		return EXIT_FAILURE;
	}

	__bt_hf_agent_dbus_init();
#ifdef FEATURE_TIZENW
	bluetooth_hf_init(__hf_event_handler, NULL);
#endif
	g_main_loop_run(gmain_loop);

	__bt_hf_agent_dbus_deinit();

	if (gmain_loop)
		g_main_loop_unref(gmain_loop);

	DBG("Terminating Bluetooth HF agent");
	return 0;
}
