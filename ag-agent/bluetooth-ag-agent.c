/*
 * Bluetooth-ag-agent
 *
 * Copyright (c) 2014 - 2015 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:	Hocheol Seo <hocheol.seo@samsung.com>
 *		Chethan T N <chethan.tn@samsung.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/poll.h>
#include <gio/gunixfdlist.h>
#include <bundle_internal.h>
#include "bluetooth-ag-agent.h"
#include "bluetooth-ag-handler.h"

#include <TapiUtility.h>
#include <ITapiSim.h>
#include <ITapiModem.h>
#include <TelNetwork.h>
#include <app.h>
#include <aul.h>
#include <system_info.h>

#include "contacts.h"
#include "appsvc.h"

static GMainLoop *gmain_loop = NULL;
static GDBusProxy *service_gproxy;
static int owner_sig_id = -1;
static int name_owner_sig_id = -1;
GDBusConnection *ag_dbus_conn = NULL;
gchar *remote_dev_path = NULL;
gboolean wbs_en;
uint16_t hfp_ver;
uint16_t hsp_ver;
static TapiHandle *tapi_handle;
extern wbs_options wbs_opts;
GSList *active_devices = NULL;
static gchar *local_addr = NULL;
static GDBusProxy *app_gproxy;
static gboolean call_launch_requested = FALSE;
static gchar* sco_owner = NULL;
static guint sco_open_timer_id = 0;
static gboolean sco_open_request = FALSE;
static guint hf_bluez_id;
static guint hs_bluez_id;
static guint app_id;
#ifdef TIZEN_MEDIA_ENHANCE
static int media_sig_id = -1;
static int media_state_sig_id = -1;
static bt_ag_media_transport_state_t transport_state;
#endif

#define HSP_AG_UUID "00001112-0000-1000-8000-00805f9b34fb"
#define HFP_AG_UUID "0000111f-0000-1000-8000-00805f9b34fb"
#ifdef TIZEN_MEDIA_ENHANCE
#define A2DP_SINK_UUID "0000110b-0000-1000-8000-00805f9b34fb"
#endif
#define DEFAULT_ADAPTER_OBJECT_PATH "/org/bluez/hci0"

#ifdef TIZEN_SUPPORT_LUNAR_DEVICE
#define VCONF_KEY_BT_LUNAR_ENABLED "db/wms/bt_loop_device_hfp_connected"
#endif

#if defined(TIZEN_WEARABLE) && defined(TIZEN_BT_HFP_AG_ENABLE)
#define CALL_APP_ID "org.tizen.call-ui"
#endif

#if defined(TIZEN_SUPPORT_DUAL_HF)
#define VCONF_KEY_BT_HOST_BT_MAC_ADDR "db/wms/host_bt_mac"
#define MAX_CONNECTED_DEVICES 2
#else
#define MAX_CONNECTED_DEVICES 1
#endif

#define BT_AG_SIG_NUM 3
static struct sigaction bt_ag_sigoldact[BT_AG_SIG_NUM];
static int bt_ag_sig_to_handle[] = { SIGABRT, SIGSEGV, SIGTERM };

/*Below Inrospection data is exposed to bluez from agent*/
static const gchar ag_agent_bluez_introspection_xml[] =
"<node name='/'>"
" <interface name='org.bluez.Profile1'>"
"     <method name='NewConnection'>"
"          <arg type='o' name='device' direction='in'/>"
"          <arg type='h' name='fd' direction='in'/>"
"          <arg type='a{sv}' name='options' direction='in'/>"
"     </method>"
"     <method name='RequestDisconnection'>"
"          <arg type='o' name='device' direction='in'/>"
"     </method>"
" </interface>"
"</node>";

/*Below Introspection data is exposed to application from agent*/
static const gchar ag_agent_app_introspection_xml[] =
"<node name='/'>"
"  <interface name='Org.Hfp.App.Interface'>"
"     <method name='RegisterApplication'>"
"          <arg type='s' name='path' direction='in'/>"
"          <arg type='s' name='address' direction='in'/>"
"     </method>"
"     <method name='UnregisterApplication'>"
"          <arg type='s' name='path' direction='in'/>"
"     </method>"
"     <method name='IncomingCall'>"
"          <arg type='s' name='path' direction='in'/>"
"          <arg type='s' name='number' direction='in'/>"
"          <arg type='i' name='id' direction='in'/>"
"     </method>"
"     <method name='OutgoingCall'>"
"          <arg type='s' name='path' direction='in'/>"
"          <arg type='s' name='number' direction='in'/>"
"          <arg type='i' name='id' direction='in'/>"
"     </method>"
"     <method name='ChangeCallStatus'>"
"          <arg type='s' name='path' direction='in'/>"
"          <arg type='s' name='number' direction='in'/>"
"          <arg type='i' name='status' direction='in'/>"
"          <arg type='i' name='id' direction='in'/>"
"     </method>"
"     <method name='GetProperties'>"
"		<arg type='a{sv}' name='properties' direction='out'/>"
"     </method>"
"	<method name='Disconnect'>"
"	</method>"
"	<method name='IsConnected'>"
"		<arg type='b' name='connected' direction='out'/>"
"	</method>"
"	<method name='IndicateCall'>"
"	</method>"
"	<method name='CancelCall'>"
"	</method>"
"	<method name='Play'>"
"	</method>"
"	<method name='Stop'>"
"	</method>"
"	<method name='IsPlaying'>"
"	<arg type='b' name='playing' direction='out'/>"
"	</method>"
"	<method name='GetSpeakerGain'>"
"		<arg type='q' name='gain' direction='out'/>"
"	</method>"
"	<method name='GetMicrophoneGain'>"
"		<arg type='q' name='gain' direction='out'/>"
"	</method>"
"	<method name='SetSpeakerGain'>"
"		<arg type='q' name='gain' direction='in'/>"
"	</method>"
"	<method name='SetMicrophoneGain'>"
"		<arg type='q' name='gain' direction='in'/>"
"	</method>"
"	<method name='SetVoiceDial'>"
"		<arg type='b' name='enable' direction='in'/>"
"	</method>"
"	<method name='CheckPrivilege'>"
"	</method>"
"  </interface>"
"</node>";

struct event {
	const char *cmd;
	int (*callback)(bt_ag_info_t *hs, const char *buf);
};

struct sco_socket_addr {
	sa_family_t     sco_family;
	bt_addr		sco_bdaddr;
};

typedef struct {
	uint16_t setting;
} bt_voice;

struct ag_codec {
	bt_ag_info_t *bt_ag_info;
	char *codec_status;
};

bt_ag_status_t ag = {
	.telephony_ready = FALSE,
	.features = 0,
	.er_mode = 3,
	.er_ind = 0,
	.rh = BT_RSP_HOLD_NOT_SUPPORTED,
	.number = NULL,
	.number_type = 0,
	.ring_timer = 0,
	.sdp_features = 0,
};
static void __bt_ag_agent_sigterm_handler(int signo);
static gboolean __bt_ag_agent_connection(gint32 fd, const gchar *device_path,
						const gchar *object_path);
static gboolean __bt_ag_agent_connection_release(bt_ag_info_t *hs);
static gboolean __bt_ag_event_handler(GIOChannel *channel, GIOCondition cond,
	void *user_data);
static int __bt_ag_sco_connect(bt_ag_info_t *hs);
void _bt_ag_set_headset_state(bt_ag_info_t *hs, hs_state_t state);
static void __bt_ag_agent_reg_sim_event(TapiHandle *handle, void *user_data);
static void __bt_ag_agent_dereg_sim_event(TapiHandle *handle);
static void __bt_ag_name_owner_changed_cb(GDBusConnection *connection,
					const gchar *sender_name,
					const gchar *object_path,
					const gchar *interface_name,
					const gchar *signal_name,
					GVariant *parameters,
					gpointer user_data);

static void __bt_convert_addr_type_to_rev_string(char *address,
				unsigned char *addr)
{
	ret_if(address == NULL);
	ret_if(addr == NULL);

	g_snprintf(address, BT_ADDRESS_STRING_SIZE,
			"%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
			addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static GDBusProxy *__bt_ag_gdbus_init_service_proxy(const gchar *service,
				const gchar *path, const gchar *interface)
{
	FN_START;

	GDBusProxy *proxy;
	GError *err = NULL;

	if (ag_dbus_conn == NULL)
		ag_dbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);

	if (!ag_dbus_conn) {
		if (err) {
			ERR("Unable to connect to gdbus: %s", err->message);
			g_clear_error(&err);
		}
		return NULL;
	}

	proxy =  g_dbus_proxy_new_sync(ag_dbus_conn,
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

	FN_END;
	return proxy;
}

static GDBusProxy *__bt_ag_gdbus_get_app_proxy(const gchar *service,
				const gchar *path, const gchar *interface)
{
	return (app_gproxy) ? app_gproxy :
			__bt_ag_gdbus_init_service_proxy(service,
					path, interface);
}

static int __bt_ag_agent_gdbus_method_send(const char *service,
				const gchar *path, const char *interface,
				const char *method, gboolean response,
				GVariant *parameters)
{
	FN_START;

	GVariant *ret;
	GDBusProxy *proxy;
	GError *error = NULL;

	proxy = __bt_ag_gdbus_get_app_proxy(service, path, interface);
	if (!proxy)
		return BT_HFP_AGENT_ERROR_INTERNAL;

	if (response) {
		ret = g_dbus_proxy_call_sync(proxy,
					method, parameters,
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
			return BT_HFP_AGENT_ERROR_INTERNAL;
		}

		g_variant_unref(ret);
	} else {
		g_dbus_proxy_call(proxy,
					method, parameters,
					G_DBUS_CALL_FLAGS_NONE, 2000,
					NULL, NULL, NULL);
	}
	return BT_HFP_AGENT_ERROR_NONE;
}

gboolean _bt_ag_agent_emit_signal(
				GDBusConnection *connection,
				const char *path,
				const char *interface,
				const char *name,
				GVariant *property)
{
	FN_START;

	GError *error = NULL;
	gboolean ret;
	ret =  g_dbus_connection_emit_signal(connection,
				NULL, path, interface,
				name, property,
				&error);
	if (!ret) {
		if (error != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
				error->code, error->message);
			g_clear_error(&error);
		}
	}
	INFO_C("Emit Signal done = [%s]", name);

	FN_END;
	return ret;
}

gboolean _bt_ag_agent_emit_property_changed(
				GDBusConnection *connection,
				const char *path,
				const char *interface,
				const char *name,
				GVariant *property)
{
	FN_START;

	gboolean ret;
	GVariant *var_data;

	var_data = g_variant_new("(sv)",	name, property);

	ret =  _bt_ag_agent_emit_signal(connection,
				path, interface,
				"PropertyChanged", var_data);
	FN_END;
	return ret;
}

static void __bt_ag_agent_start_watch(bt_ag_info_t *bt_ag_info)
{
	bt_ag_info->watch_id = g_io_add_watch(bt_ag_info->io_chan,
			G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
			(GIOFunc) __bt_ag_event_handler, bt_ag_info);
}

static void __bt_ag_agent_remove_watch(guint *watch_id)
{
	DBG("Remove IO watch ID %d", *watch_id);
	if (*watch_id > 0) {
		g_source_remove(*watch_id);
		*watch_id = 0;
	}
}

#if defined(TIZEN_SUPPORT_DUAL_HF)
gboolean __bt_ag_agent_is_companion_device(const char *addr)
{
#if defined(TIZEN_WEARABLE)
	char *host_device_address = NULL;
	host_device_address = vconf_get_str(VCONF_KEY_BT_HOST_BT_MAC_ADDR);

	if (!host_device_address) {
		INFO("Failed to get a companion device address");
		return FALSE;
	}

	if (g_strcmp0(host_device_address, addr) == 0) {
		INFO("addr[%s] is companion device", addr);
		return TRUE;
	}

	return FALSE;
#else
	/* TODO : Need to add companion device check condition for Phone models */
	return FALSE;
#endif
}

void __bt_convert_device_path_to_address(const gchar *device_path,
						char *device_address)
{
	char address[BT_ADDRESS_STRING_SIZE] = { 0 };
	char *dev_addr;

	ret_if(device_path == NULL);
	ret_if(device_address == NULL);

	dev_addr = strstr(device_path, "dev_");
	if (dev_addr != NULL) {
		char *pos = NULL;
		dev_addr += 4;
		g_strlcpy(address, dev_addr, sizeof(address));

		while ((pos = strchr(address, '_')) != NULL) {
			*pos = ':';
		}

		g_strlcpy(device_address, address, BT_ADDRESS_STRING_SIZE);
	}
}

static gboolean  __bt_ag_agent_is_companion_device_connected(void)
{
	GSList *l;

	for (l = active_devices ; l; l = l->next) {
		bt_ag_info_t *data = l->data;

		if (data->is_companion_device) {
			DBG("Companion device found");
			return TRUE;
		}
	}

	return FALSE;
}

gboolean __bt_ag_agent_check_dual_hf_condition(const gchar *device_path)
{
	char device_address[BT_ADDRESS_STRING_SIZE] = { 0 };
	gboolean is_companion_device;

	__bt_convert_device_path_to_address(device_path, device_address);
	is_companion_device = __bt_ag_agent_is_companion_device(device_address);

	DBG(" device_address[%s]", device_address);
	DBG(" is_companion_device[%d]", is_companion_device);

	if (__bt_ag_agent_is_companion_device_connected()) {
		if (is_companion_device)
			return FALSE;
	} else {
		if (!is_companion_device)
			return FALSE;
	}

	return TRUE;
}
#endif /* TIZEN_SUPPORT_DUAL_HF */

static gboolean __bt_is_phone_locked(int *phone_lock_state)
{
	FN_START;
	int ret;

	if (NULL == phone_lock_state)
		return FALSE;

	ret = vconf_get_int(VCONFKEY_IDLE_LOCK_STATE, phone_lock_state);
	if (ret != 0) {
		ERR("Failed to read  [%s]\n", VCONFKEY_IDLE_LOCK_STATE);
		return FALSE;
	}

	FN_END;
	return TRUE;
}

static gboolean __bt_get_outgoing_callapp_type(int *callapp_type)
{
	FN_START;

	if (NULL == callapp_type)
		return FALSE;

#if defined(TIZEN_WEARABLE) && defined(TIZEN_BT_HFP_AG_ENABLE)
	*callapp_type = BT_VOICE_CALL;
	FN_END;
	return TRUE;
#else
#if 0
	int ret;
	ret = vconf_get_int(
			VCONFKEY_CISSAPPL_OUTGOING_CALL_TYPE_INT,
			callapp_type);
	if (ret != 0) {
		ERR("Failed to read  [%s]\n",
			VCONFKEY_CISSAPPL_OUTGOING_CALL_TYPE_INT);
		return FALSE;
	}

	INFO(" [%s] = [%d]\n",
		VCONFKEY_CISSAPPL_OUTGOING_CALL_TYPE_INT, *callapp_type);
#endif
	/* The vconf value does not include in platform. */
	*callapp_type = BT_VOICE_CALL;
	FN_END;
	return TRUE;
#endif
}

static gboolean __bt_get_outgoing_call_condition(int *condition)
{
	FN_START;

	if (NULL == condition)
		return FALSE;

#if defined(TIZEN_WEARABLE) && defined(TIZEN_BT_HFP_AG_ENABLE)
	*condition = BT_MO_ONLY_UNLOCKED;
	FN_END;
	return TRUE;
#else
#if 0
	int ret;
	ret = vconf_get_int(
			VCONFKEY_CISSAPPL_OUTGOING_CALL_CONDITIONS_INT,
			condition);
	if (ret != 0) {
		ERR("Failed to read  [%s]\n",
			VCONFKEY_CISSAPPL_OUTGOING_CALL_CONDITIONS_INT);
		return FALSE;
	}
#endif
	/* The vconf value does not include in platform. */
	*condition = BT_MO_ONLY_UNLOCKED;
	FN_END;
	return TRUE;
#endif
}

#if defined(TIZEN_WEARABLE) && defined(TIZEN_TELEPHONY_ENABLED)
static gboolean  __bt_ag_agent_launch_call_app(const char *number)
{
	FN_START;
	bundle *b;

	DBG_SECURE("number(%s)", number);

	b = bundle_create();
	if (NULL == b) {
		ERR("bundle_create() Failed");
		return FALSE;
	}

	bundle_add(b, "launch-type", "MO");
	bundle_add(b, "dial-type", "HEADSET");

	if (strlen(number) != 0)
		bundle_add(b, "number", number);

	aul_launch_app_async(CALL_APP_ID, b);
	bundle_free(b);

	FN_END;
	return TRUE;
}
#endif

static void *__bt_ag_agent_launch_call_req(void *arg)
{
	FN_START;
	bundle *b = (bundle *)arg;
	if (appsvc_run_service(b, 0, NULL, NULL) < 0)
		ERR("Unable to run app svc");
	bundle_free(b);
	call_launch_requested = FALSE;
	FN_END;
	return NULL;
}

static gboolean __bt_ag_agent_make_call(const char *number)
{
	FN_START;

#if defined(TIZEN_WEARABLE) && defined(TIZEN_BT_HFP_AG_ENABLE)
	FN_END;
	return __bt_ag_agent_launch_call_app(number);
#else
	char telnum[BT_MAX_TEL_NUM_STRING];
	bundle *b;
	pthread_t thread_id;

	if (call_launch_requested == TRUE) {
		DBG("Launch request is in progress");
		return TRUE;
	}

	b = bundle_create();
	if (NULL == b)
		return FALSE;

	appsvc_set_operation(b, APPSVC_OPERATION_CALL);
	snprintf(telnum, sizeof(telnum), "tel:%s", number);
	appsvc_set_uri(b, telnum);
	appsvc_add_data(b, "ctindex", "-1");

	call_launch_requested = TRUE;
	if (pthread_create(&thread_id, NULL,
			(void *)&__bt_ag_agent_launch_call_req,
					(void *)b) < 0) {
		ERR("pthread_create() is failed");
		call_launch_requested = FALSE;
		return FALSE;
	}
	if (pthread_detach(thread_id) < 0)
		ERR("pthread_detach() is failed");

	FN_END;
	return TRUE;
#endif
}

static gboolean __bt_ag_agent_make_video_call(const char *mo_number)
{
	FN_START;
	bundle *kb;

	kb = bundle_create();
	if (NULL == kb)
		return FALSE;

	bundle_add(kb, "KEY_CALL_TYPE", "MO");
	bundle_add(kb, "number", mo_number);
	aul_launch_app("org.tizen.vtmain", kb);
	bundle_free(kb);

	FN_END;
	return TRUE;
}

gboolean _bt_ag_agent_answer_call(unsigned int call_id,
				const gchar *path, const gchar *sender)
{
	FN_START;

	if (path == NULL || sender == NULL) {
		DBG("Invalid Arguments");
		return FALSE;
	}

	DBG("Application path = %s", path);
	DBG("Call Id = %d", call_id);
	DBG("Sender = %s", sender);

	_bt_ag_agent_emit_signal(ag_dbus_conn, path,
					BT_AG_SERVICE_NAME, "Answer",
					g_variant_new("(u)", call_id));
	FN_END;
	return TRUE;
}

gboolean _bt_ag_agent_reject_call(unsigned int call_id,
				const gchar *path, const gchar *sender)
{
	FN_START;

	if (path == NULL || sender == NULL) {
		DBG("Invalid Arguments");
		return FALSE;
	}

	DBG("Application path = %s", path);
	DBG("Call Id = %d", call_id);
	DBG("Sender = %s", sender);

	_bt_ag_agent_emit_signal(ag_dbus_conn, path,
					BT_AG_SERVICE_NAME, "Reject",
					g_variant_new("(u)", call_id));
	FN_END;
	return TRUE;
}

gboolean _bt_ag_agent_release_call(unsigned int call_id,
				const gchar *path, const gchar *sender)
{
	FN_START;

	if (path == NULL || sender == NULL) {
		DBG("Invalid Arguments");
		return FALSE;
	}

	DBG("Application path = %s", path);
	DBG("Call Id = %d", call_id);
	DBG("Sender = %s", sender);

	_bt_ag_agent_emit_signal(ag_dbus_conn, path,
					BT_AG_SERVICE_NAME, "Release",
					g_variant_new("(u)", call_id));

	FN_END;
	return TRUE;
}

bt_hfp_agent_error_t _bt_ag_agent_dial_num(const gchar *number, guint flags)
{
	bt_hfp_agent_error_t error_code = BT_HFP_AGENT_ERROR_NONE;
	int callapp_type;
	int phone_lock_state;
	int condition;

	FN_START;

	if (number == NULL) {
		ERR("Invalid Argument");
		error_code = BT_HFP_AGENT_ERROR_INVALID_PARAM;
		goto fail;
	}

	DBG("Number = %s", number);
	DBG("flags = %d", flags);

	if (!__bt_is_phone_locked(&phone_lock_state)) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto fail;
	}

	if (!__bt_get_outgoing_callapp_type(&callapp_type)) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto fail;
	}

	if (!__bt_get_outgoing_call_condition(&condition)) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto fail;
	}

	if (condition == BT_MO_ONLY_UNLOCKED && phone_lock_state ==
		VCONFKEY_IDLE_LOCK) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto fail;
	}

	if (callapp_type == BT_VIDEO_CALL) {
		if (!__bt_ag_agent_make_video_call(number)) {
			ERR("Problem launching application");
			error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}
	} else {
		if (!__bt_ag_agent_make_call(number)) {
			ERR("Problem launching application");
			error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}
	}

fail:
	FN_END;
	return error_code;
}

bt_hfp_agent_error_t _bt_ag_agent_dial_memory(unsigned int location)
{
	bt_hfp_agent_error_t error_code = BT_HFP_AGENT_ERROR_NONE;
	char *number = NULL;
	contacts_filter_h filter = NULL;
	contacts_query_h query = NULL;
	contacts_list_h list = NULL;
	contacts_record_h record = NULL;
	unsigned int projections[] = {
		_contacts_speeddial.number,
	};

	FN_START;

	DBG("location = %d", location);

	/*Get number from contacts location*/
	if (contacts_connect() != CONTACTS_ERROR_NONE) {
		ERR(" contacts_connect failed");
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	contacts_filter_create(_contacts_speeddial._uri, &filter);

	if (filter == NULL)
		goto done;

	if (contacts_filter_add_int(filter,
		_contacts_speeddial.speeddial_number,
		CONTACTS_MATCH_EQUAL, location) !=
		CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	contacts_query_create(_contacts_speeddial._uri, &query);

	if (query == NULL)
		goto done;

	contacts_query_set_filter(query, filter);

	if (contacts_query_set_projection(query, projections,
				sizeof(projections)/sizeof(unsigned int)) !=
				CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_db_get_records_with_query(query, 0, 1, &list) !=
				CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_list_first(list) != CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_list_get_current_record_p(list, &record) !=
				CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (record == NULL)
		goto done;

	if (contacts_record_get_str(record, _contacts_speeddial.number, &number)
		!= CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (number == NULL) {
		ERR("No number at the location");
		error_code = BT_HFP_AGENT_ERROR_INVALID_MEMORY_INDEX;
		goto done;
	}

	DBG("number %s", number);

	/*Make Voice call*/
	if (!__bt_ag_agent_make_call(number)) {
		ERR("Problem launching application");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
	}

	g_free(number);
done:
	if (list != NULL)
		contacts_list_destroy(list, TRUE);

	if (filter != NULL)
		contacts_filter_destroy(filter);

	if (query != NULL)
		contacts_query_destroy(query);

	contacts_disconnect();

	FN_END;

	return error_code;
}

bt_hfp_agent_error_t _bt_ag_agent_send_dtmf(const gchar *dtmf,
				const gchar *path, const gchar *sender)
{
	bt_hfp_agent_error_t ret;

	FN_START;

	if (dtmf == NULL || path == NULL || sender == NULL) {
		ERR("Invalid Argument");
		return FALSE;
	}

	DBG("Dtmf = %s", dtmf);
	DBG("Application path = %s", path);
	DBG("Sender = %s", sender);

	ret = __bt_ag_agent_gdbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"SendDtmf", FALSE,
				g_variant_new("(s)", dtmf));

	return ret;
}

gboolean _bt_ag_agent_threeway_call(unsigned int chld_value,
				const gchar *path, const gchar *sender)
{
	FN_START;

	if (path == NULL || sender == NULL) {
		DBG("Invalid Arguments");
		return FALSE;
	}

	DBG("Application path = %s", path);
	DBG("Value = %d", chld_value);
	DBG("Sender = %s", sender);

#if defined(TIZEN_WEARABLE) && defined(TIZEN_BT_HFP_AG_ENABLE)
	/* Check if AG supports (i.e. ag_chld_str = "0,1,2") the requested CHLD;
	if not return FALSE */
	if (chld_value != 0 && chld_value != 1 && chld_value != 2)
		return FALSE;
#else
	if (chld_value != 0 && chld_value != 1 && chld_value != 2 &&
				chld_value != 3)
		return FALSE;
#endif
	_bt_ag_agent_emit_signal(ag_dbus_conn, path,
					BT_AG_SERVICE_NAME, "Threeway",
					g_variant_new("(u)", chld_value));
	FN_END;
	return TRUE;
}

bt_hfp_agent_error_t _bt_ag_agent_dial_last_num(void *device)
{
	bt_hfp_agent_error_t err_code = BT_HFP_AGENT_ERROR_NONE;
	char *last_num = NULL;
	int type;
	int callapp_type;
	int phone_lock_state;
	int condition;
	contacts_list_h list = NULL;
	contacts_query_h query = NULL;
	contacts_filter_h filter = NULL;
	contacts_record_h record = NULL;
	unsigned int projections[] = {
		_contacts_phone_log.address,
		_contacts_phone_log.log_type,
	};

	FN_START;

	if (contacts_connect() != CONTACTS_ERROR_NONE) {
		ERR(" contacts_connect failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		return err_code;
	}

	contacts_filter_create(_contacts_phone_log._uri, &filter);

	if (filter == NULL)
		goto done;

	if (contacts_filter_add_int(filter, _contacts_phone_log.log_type,
				CONTACTS_MATCH_EQUAL,
				CONTACTS_PLOG_TYPE_VOICE_OUTGOING) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_filter_add_int failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_filter_add_operator(filter, CONTACTS_FILTER_OPERATOR_OR) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_filter_add_operator failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_filter_add_int(filter, _contacts_phone_log.log_type,
				CONTACTS_MATCH_EQUAL,
				CONTACTS_PLOG_TYPE_VIDEO_OUTGOING) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_filter_add_int failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	contacts_query_create(_contacts_phone_log._uri, &query);

	if (query == NULL)
		goto done;

	contacts_query_set_filter(query, filter);

	if (contacts_query_set_projection(query, projections,
				sizeof(projections)/sizeof(unsigned int)) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_query_set_projection failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_query_set_sort(query, _contacts_phone_log.log_time, false)
		!= CONTACTS_ERROR_NONE) {
		ERR(" contacts_query_set_sort failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_db_get_records_with_query(query, 0, 1, &list)  !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_db_get_records_with_query failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_list_first(list)  != CONTACTS_ERROR_NONE) {
		ERR(" contacts_list_first failed");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_list_get_current_record_p(list, &record)  !=
				CONTACTS_ERROR_NONE) {
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (record == NULL)
		goto done;

	if (contacts_record_get_str(record, _contacts_phone_log.address,
				&last_num) != CONTACTS_ERROR_NONE) {
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (last_num == NULL) {
		ERR("No last number");
		err_code = BT_HFP_AGENT_ERROR_NO_CALL_LOGS;
		goto done;
	}

	if (!__bt_is_phone_locked(&phone_lock_state)) {
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (!__bt_get_outgoing_callapp_type(&callapp_type)) {
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (!__bt_get_outgoing_call_condition(&condition)) {
		ERR(" Failed to get the call condition");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (condition == BT_MO_ONLY_UNLOCKED &&
		phone_lock_state == VCONFKEY_IDLE_LOCK) {
		ERR(" call condition and phone lock state check fail");
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	switch (callapp_type) {
	case BT_VOICE_CALL:
		if (!__bt_ag_agent_make_call(last_num)) {
			ERR("Problem launching application");
			err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		}
		break;
	case BT_VIDEO_CALL:
		if (!__bt_ag_agent_make_video_call(last_num)) {
			ERR("Problem launching application");
			err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		}
		break;
	case BT_FOLLOW_CALL_LOG:
		if (contacts_record_get_int(record,
			_contacts_phone_log.log_type,
			&type) != CONTACTS_ERROR_NONE) {
			err_code = BT_HFP_AGENT_ERROR_INTERNAL;
			break;
		}
		if (type == CONTACTS_PLOG_TYPE_VOICE_OUTGOING) {
			if (!__bt_ag_agent_make_call(last_num)) {
				ERR("Problem launching application");
				err_code = BT_HFP_AGENT_ERROR_INTERNAL;
			}
		} else if (type == CONTACTS_PLOG_TYPE_VIDEO_OUTGOING) {
			if (!__bt_ag_agent_make_video_call(last_num)) {
				ERR("Problem launching application");
				err_code = BT_HFP_AGENT_ERROR_INTERNAL;
			}
		} else {
				err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		}
		break;
	default:
		err_code = BT_HFP_AGENT_ERROR_INTERNAL;
		break;
	}

done:

	if (list != NULL)
		contacts_list_destroy(list, TRUE);

	if (filter != NULL)
		contacts_filter_destroy(filter);

	if (query != NULL)
		contacts_query_destroy(query);

	contacts_disconnect();

	if (last_num != NULL)
		g_free(last_num);

	FN_END;

	return err_code;
}

bt_hfp_agent_error_t _bt_ag_agent_vendor_cmd(const gchar *cmd,
		const gchar *path, const gchar *sender)
{
	bt_hfp_agent_error_t ret;

	FN_START;

	if (cmd == NULL || path == NULL || sender == NULL) {
		ERR("Invalid Argument");
		return BT_HFP_AGENT_ERROR_INVALID_PARAM;
	}

	DBG("cmd = %s", cmd);
	DBG("Application path = %s", path);
	DBG("Sender = %s", sender);

	ret = __bt_ag_agent_gdbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"VendorCmd", FALSE,
				g_variant_new("(s)", cmd));
	FN_END;
	return ret;
}

gboolean _bt_ag_agent_get_signal_quality(void *device)
{
	gint rssi;

	FN_START;

	if (vconf_get_int(VCONFKEY_TELEPHONY_RSSI, &rssi)) {
		DBG("VCONFKEY_TELEPHONY_RSSI failed\n");
		goto fail;
	}

	DBG("RSSI : %d", rssi);

	_bt_hfp_signal_quality_reply(rssi, BT_SIGNAL_QUALITY_BER,
		device);

	FN_END;
	return TRUE;
fail:
	FN_END;
	_bt_hfp_signal_quality_reply(-1, -1, device);
	return FALSE;
}

gboolean _bt_ag_agent_get_battery_status(void *device)
{
	gint battery_chrg_status;
	gint battery_capacity;

	FN_START;

	if (vconf_get_int(VCONFKEY_SYSMAN_BATTERY_CHARGE_NOW,
						&battery_chrg_status)) {
		DBG("VCONFKEY_SYSMAN_BATTERY_CHARGE_NOW failed\n");
		goto fail;
	}

	DBG("Status : %d\n", battery_chrg_status);

	if (vconf_get_int(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
						&battery_capacity)) {
		DBG("VCONFKEY_SYSMAN_BATTERY_CAPACITY failed\n");
		goto fail;
	}

	DBG("Capacity : %d\n", battery_capacity);

	_bt_hfp_battery_property_reply(device,
		battery_chrg_status, battery_capacity);
	FN_END;
	return TRUE;

fail:
	_bt_hfp_battery_property_reply(device, -1, -1);
	FN_END;
	return FALSE;
}

gboolean _bt_ag_agent_get_operator_name(void *device)
{
	char *operator_name;
	FN_START;

	operator_name = vconf_get_str(VCONFKEY_TELEPHONY_NWNAME);
	if (NULL == operator_name) {
		DBG("vconf_get_str failed");
		_bt_hfp_operator_reply(NULL, device);
		return FALSE;
	}

	DBG("operator_name  = [%s]", operator_name);

	_bt_hfp_operator_reply(operator_name, device);

	free(operator_name);

	FN_END;
	return TRUE;
}

gboolean _bt_hfp_agent_nrec_status(gboolean status,
			void *t_device)
{
	FN_START;
	bt_ag_info_t *hs = (bt_ag_info_t *)t_device;

	DBG("NREC status = %d", status);
	if (status)
		hs->nrec_status = FALSE;
	else
		hs->nrec_status = TRUE;

	_bt_ag_agent_emit_signal(ag_dbus_conn, hs->path,
					BT_AG_SERVICE_NAME, "NrecStatusChanged",
					g_variant_new("(b)", status));
	FN_END;
	return TRUE;
}

gboolean _bt_ag_agent_get_imei_number(void *device)
{
	FN_START;
	char *imei_number;

	imei_number = tel_get_misc_me_imei_sync(tapi_handle);
	if (NULL == imei_number) {
		ERR("tel_get_misc_me_imei_sync for imei_number failed");
		goto fail;
	}

	if (!g_utf8_validate(imei_number, -1, NULL)) {
		free(imei_number);
		ERR("get_imei_number : invalid UTF8");
		goto fail;
	}

	DBG_SECURE("imei_number  = [%s]", imei_number);
	_bt_hfp_get_imei_number_reply(imei_number, device);
	free(imei_number);
	FN_END;
	return TRUE;

fail:
	_bt_hfp_get_imei_number_reply(NULL, device);
	FN_END;
	return FALSE;
}

void _bt_ag_agent_get_manufacturer_name(void *device)
{
	FN_START;
	char *manufacturer_name;
	int ret;

	ret = system_info_get_platform_string("http://tizen.org/system/manufacturer",
						&manufacturer_name);
	if (SYSTEM_INFO_ERROR_NONE != ret) {
		ERR("Get manufacturer_name failed : %d", ret);
		if (NULL != manufacturer_name)
			free(manufacturer_name);

		manufacturer_name = g_strdup("Unknown");
	} else if (!g_utf8_validate(manufacturer_name, -1, NULL)) {
		free(manufacturer_name);
		manufacturer_name = g_strdup("Unknown");
		ERR("get_manufacturer_name : invalid UTF8");
	}

	DBG_SECURE("manufacturer_name  = [%s]", manufacturer_name);
	_bt_hfp_get_device_manufacturer_reply(manufacturer_name, device);
	free(manufacturer_name);
	FN_END;
}

void _bt_ag_agent_get_imsi(void *device)
{
	FN_START;
	TelSimImsiInfo_t imsi;
	memset (&imsi, 0, sizeof(TelSimImsiInfo_t));
	if (tel_get_sim_imsi(tapi_handle, &imsi) != TAPI_API_SUCCESS) {
		ERR("tel_get_sim_imsi failed");
		goto fail;
	}
	DBG_SECURE("tapi values %s %s %s", imsi.szMcc, imsi.szMnc, imsi.szMsin);

	_bt_hfp_get_imsi_reply(imsi.szMcc, imsi.szMnc, imsi.szMsin, device);
	FN_END;
	return;
fail:
	_bt_hfp_get_imsi_reply(NULL, NULL, NULL, device);
	FN_END;
}

int _bt_ag_agent_registration_status_convert(int result)
{
	switch (result) {
	case TAPI_NETWORK_SERVICE_LEVEL_NO:
		return BT_AGENT_NETWORK_REG_STATUS_NOT_REGISTER;
	case TAPI_NETWORK_SERVICE_LEVEL_EMERGENCY:
		return BT_AGENT_NETWORK_REG_STATUS_EMERGENCY;
	case TAPI_NETWORK_SERVICE_LEVEL_FULL:
		return BT_AGENT_NETWORK_REG_STATUS_REGISTER_HOME_NETWORK;
	case TAPI_NETWORK_SERVICE_LEVEL_SEARCH:
		return BT_AGENT_NETWORK_REG_STATUS_SEARCH;
	default:
		return BT_AGENT_NETWORK_REG_STATUS_UNKNOWN;
	}
	return result;
}

void _bt_ag_agent_get_creg_status(void *device)
{
	FN_START;
	int result = 0;
	int ret = 0;
	int n = 1;
	int registration_status = 0;
	int roam_status = 0;

	ret = tel_get_property_int(tapi_handle, TAPI_PROP_NETWORK_CIRCUIT_STATUS,
				&result);
	if (ret != TAPI_API_SUCCESS) {
		ERR("tel_get_property_int failed");
		return;
	}
	registration_status =
			_bt_ag_agent_registration_status_convert(result);

	DBG_SECURE("Registration status %d", result);
	DBG_SECURE("Mapped Status %d", registration_status);
	if (registration_status ==
			BT_AGENT_NETWORK_REG_STATUS_REGISTER_HOME_NETWORK) {
		ret = vconf_get_int(VCONFKEY_TELEPHONY_SVC_ROAM, &roam_status);
		if (ret != 0) {
			ERR("Get roaming status failed err = %d\n", ret);
			return;
		}
		DBG_SECURE("Roam status %d", roam_status);
		if (roam_status == 1) {
			registration_status =
					BT_AGENT_NETWORK_REG_STATUS_REGISTERED_ROAMING;
		}
	}

	_bt_hfp_get_creg_status_reply(n, registration_status, device);

	FN_END;
	return;
}

void _bt_ag_agent_get_model_name(void *device)
{
	FN_START;
	char *model_name;
	int ret;

	ret = system_info_get_platform_string("http://tizen.org/system/model_name", &model_name);
	if (SYSTEM_INFO_ERROR_NONE != ret) {
		ERR("Get model_name failed: %d", ret);
		if (NULL != model_name)
			free(model_name);

		model_name = g_strdup("Unknown");
	} else if (!g_utf8_validate(model_name, -1, NULL)) {
		free(model_name);
		model_name = g_strdup("Unknown");
		ERR("get_model_name : invalid UTF8");
	}

	DBG_SECURE("model_name  = [%s]", model_name);
	_bt_hfp_get_model_info_reply(model_name, device);
	free(model_name);
	FN_END;
}

void _bt_ag_agent_get_revision_information(void *device)
{
	FN_START;
	char *revision_info;
	int ret;

	ret = system_info_get_platform_string("http://tizen.org/system/build.string",
				&revision_info);
	if (SYSTEM_INFO_ERROR_NONE != ret) {
		ERR("Get revision_info failed: %d", ret);
		if (NULL != revision_info)
			free(revision_info);

		revision_info = g_strdup("Unknown");
	} else if (!g_utf8_validate(revision_info, -1, NULL)) {
			free(revision_info);
			revision_info = g_strdup("Unknown");
			ERR("get_revision_info: invalid UTF8");
		}

	DBG_SECURE("revision_info  = [%s]", revision_info);
	_bt_hfp_get_revision_info_reply(revision_info, device);
	free(revision_info);
	FN_END;
}

#if defined(TIZEN_WEARABLE) && defined(TIZEN_BT_HFP_AG_ENABLE)
static gboolean __bt_ag_agent_launch_voice_dial(gboolean activate)
{
	FN_START;
	bundle *b;

	b = bundle_create();
	if (NULL == b) {
		ERR("bundle_create() Failed");
		return FALSE;
	}

	bundle_add(b, "domain", "bt_headset");
	if (!activate)
		bundle_add(b, "action_type", "deactivate");

	aul_launch_app_async("org.tizen.svoice", b);
	bundle_free(b);
	FN_END;
	return TRUE;
}
#else
static gboolean __bt_ag_agent_launch_voice_dial(gboolean activate)
{
	FN_START;
	app_control_h service = NULL;

	app_control_create(&service);

	if (service == NULL) {
		ERR("Service create failed");
		return FALSE;
	}

	app_control_set_app_id(service, "org.tizen.svoice");
	app_control_set_operation(service, APP_CONTROL_OPERATION_DEFAULT);
	if (app_control_add_extra_data(service, "domain", "bt_headset")
					!= APP_CONTROL_ERROR_NONE) {
		ERR("app_control_add_extra_data failed");
		app_control_destroy(service);
		return FALSE;
	}

	if (!activate)
		if (app_control_add_extra_data(service, "action_type", "deactivate")
					!= APP_CONTROL_ERROR_NONE) {
			ERR("app_control_add_extra_data failed");
			app_control_destroy(service);
			return FALSE;
		}

	if (app_control_send_launch_request(service, NULL, NULL) !=
						APP_CONTROL_ERROR_NONE) {
		ERR("launch failed");
		app_control_destroy(service);
		return FALSE;
	}

	app_control_destroy(service);
	FN_END;
	return TRUE;
}
#endif

gboolean _bt_ag_agent_voice_dial(gboolean activate)
{
	DBG("Activate = %d", activate);

	return __bt_ag_agent_launch_voice_dial(activate);
}

static void __bt_ag_codec_negotiation_info_reset(bt_ag_info_t *hs,
					gboolean reset)
{
	hs->codec_info.is_negotiating = FALSE;
	hs->codec_info.requested_by_hf = FALSE;
	hs->codec_info.sending_codec = 0;
	if (reset) {
		hs->codec_info.remote_codecs = 0;
		hs->codec_info.final_codec = 0;
		hs->nrec_status = FALSE;
	}

	if (hs->codec_info.nego_timer) {
		g_source_remove(hs->codec_info.nego_timer);
		hs->codec_info.nego_timer = 0;
	}
	wbs_opts.wbs_enable = wbs_en;
}

static gboolean __bt_ag_codec_negotiation_finished(gpointer user_data)
{
	struct ag_codec *data = (struct ag_codec *)user_data;

	if (g_strcmp0(data->codec_status, "finish") == 0) {
		DBG("Codec negotiation finished");
		__bt_ag_sco_connect(data->bt_ag_info);
		__bt_ag_codec_negotiation_info_reset(data->bt_ag_info, FALSE);
		g_free (data->codec_status);
		g_free (data);
		return TRUE;
	} else if (g_strcmp0(data->codec_status, "timeout") == 0) {
		DBG("Timeout is occured in codec negotiation");
	}

	if (data->bt_ag_info->codec_info.requested_by_hf) {
		__bt_ag_codec_negotiation_info_reset(data->bt_ag_info, FALSE);
	} else {
		__bt_ag_sco_connect(data->bt_ag_info);
		__bt_ag_codec_negotiation_info_reset(data->bt_ag_info, FALSE);
	}
	g_free (data->codec_status);
	g_free (data);

	return FALSE;
}

static bt_hfp_agent_error_t __bt_ag_set_codec(void *device, char *method)
{
	GDBusProxy *proxy;
	GVariant *ret;
	GError *err = NULL;
	bt_ag_info_t *bt_ag_info = (bt_ag_info_t *)device;

	proxy =  g_dbus_proxy_new_sync(ag_dbus_conn,
			G_DBUS_PROXY_FLAGS_NONE, NULL,
			BLUEZ_SERVICE_NAME, DEFAULT_ADAPTER_OBJECT_PATH,
			BT_ADAPTER_INTERFACE, NULL, &err);

	if (!proxy) {
		if (err) {
			ERR("Unable to create proxy: %s", err->message);
			g_clear_error(&err);
		}
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	ret = g_dbus_proxy_call_sync(proxy, method,
			g_variant_new("(ss)", "Gateway", bt_ag_info->remote_addr),
			G_DBUS_CALL_FLAGS_NONE, -1,
			NULL, &err);
	if (ret == NULL) {
		/* dBUS-RPC is failed */
		ERR("dBUS-RPC is failed");
		if (err != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
			       err->code, err->message);

			g_clear_error(&err);
		}
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}
	g_variant_unref(ret);

	return BT_HFP_AGENT_ERROR_NONE;
}

static bt_hfp_agent_error_t __bt_ag_codec_selection_setup(bt_ag_info_t *hs,
			uint32_t codec)
{
	bt_hfp_agent_error_t err = BT_HFP_AGENT_ERROR_NONE;

	DBG("Codec setup [%x]", codec);

	/* 1. Compare sending codec & recieved code */
	if (hs->codec_info.sending_codec != codec)
		err = BT_HFP_AGENT_ERROR_INTERNAL;

	/* 2. Send WB or NB command */
	switch (codec) {
	case BT_CVSD_CODEC_ID:
		err = __bt_ag_set_codec(hs, "SetNbParameters");
		break;
	case BT_MSBC_CODEC_ID:
		err = __bt_ag_set_codec(hs, "SetWbsParameters");
		break;
	default:
		err = BT_HFP_AGENT_ERROR_INTERNAL;
		break;
	}

	/* If the vendor specific calling returns error or codec is not correct,
	 * we send CVSD Codec parameter to MM module. and also returns
	 * normal value to HF
	*/
	if (err != BT_HFP_AGENT_ERROR_NONE)
		codec = BT_CVSD_CODEC_ID;

	hs->codec_info.final_codec = codec;

	return err;
}

static bt_hfp_agent_error_t __bt_hfp_send_bcs_command(bt_ag_info_t *hs,
			gboolean init_by_hf)
{
	uint32_t codec;
	struct ag_codec *data = g_new0(struct ag_codec, 1);

#ifdef TIZEN_KIRAN
	codec = BT_CVSD_CODEC_ID;
#else
	if (hs->codec_info.remote_codecs & BT_MSBC_CODEC_MASK)
		codec = BT_MSBC_CODEC_ID;
	else
		codec = BT_CVSD_CODEC_ID;
#endif

	if (wbs_opts.wbs_enable == FALSE)
		codec = BT_CVSD_CODEC_ID;

	hs->codec = codec;

	if (_bt_ag_send_at(hs, "\r\n+BCS: %d\r\n", codec) < 0)
		return BT_HFP_AGENT_ERROR_INTERNAL;
	else
		DBG("Send +BCS:%d\n", codec);

	/* Send +BCS command to HF, and wait some times */
	hs->codec_info.is_negotiating = TRUE;
	hs->codec_info.sending_codec = codec;
	hs->codec_info.requested_by_hf = init_by_hf;
	hs->codec_info.final_codec = codec;

	data->bt_ag_info = hs;
	data->codec_status = g_strdup ("timeout");

	hs->codec_info.nego_timer = g_timeout_add_seconds(
			HFP_CODEC_NEGOTIATION_TIMEOUT,
			(GSourceFunc)__bt_ag_codec_negotiation_finished,
			data);

	return BT_HFP_AGENT_ERROR_NONE;
}


static bt_hfp_agent_error_t __bt_hfp_codec_connection_setup(
				bt_ag_info_t *hs, gboolean init_by_hf)
{
	DBG("Request to codec connection by %s", init_by_hf ? "HF" : "AG");

	if (hs->state < HEADSET_STATE_CONNECTED)
		return BT_HFP_AGENT_ERROR_NOT_CONNECTED;

	if (hs->codec_info.is_negotiating == TRUE) {
		/* In codec negotiation, return and wait */
		ERR("Codec nogotiation is in progress");
		return BT_HFP_AGENT_ERROR_BUSY;
	}

	/* Not support Codec Negotiation or Not recieved BAC command */
	if (!(ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION) ||
				hs->codec_info.remote_codecs == 0) {
		ERR("No support for Codec Negotiation or receive BAC command");
		if (init_by_hf) {
			return BT_HFP_AGENT_ERROR_INTERNAL;
		} else {
			__bt_ag_sco_connect(hs);
			return BT_HFP_AGENT_ERROR_INTERNAL;
		}
	}

	/* If HF initiated codec connection setup, it should send OK command
	 * before +BCS command transmission.
	 */
	if (init_by_hf)
		return HFP_STATE_MNGR_ERR_NONE;
	else
		return __bt_hfp_send_bcs_command(hs, init_by_hf);
}


static int __hfp_parse_available_codecs(const char *cmd, uint32_t *codecs)
{
	char *str = NULL;
	*codecs = 0x00000000;

	str = strchr(cmd, '=');
	if (str == NULL)
		return -EINVAL;

	while (str != NULL) {
		str++;

		if (atoi(str) == BT_CVSD_CODEC_ID)
			*codecs |= BT_CVSD_CODEC_MASK;
		else if (atoi(str) == BT_MSBC_CODEC_ID)
			*codecs |= BT_MSBC_CODEC_MASK;

		str = strchr(str, ',');
	}

	if (*codecs == 0x00000000)
		return -EINVAL;

	return 0;
}

/* AT+BAC (Bluetooth Available Codecs) */
static int __bt_hfp_available_codecs(bt_ag_info_t *hs, const char *buf)
{
	uint32_t codecs = 0x00000000;
	hfp_state_manager_err_t err = HFP_STATE_MNGR_ERR_NONE;

	if (!(ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION)) {
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;
	} else if (__hfp_parse_available_codecs(buf, &codecs) < 0) {
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;
	} else {
		DBG("Update remote available codecs [%x]", codecs);
		hs->codec_info.remote_codecs = codecs;
	}

	_bt_ag_send_response(hs, err);

	/* Reset codec information and
	 * restart codec connection setup by AG
	 */
	hs->codec_info.final_codec = 0;
	if (hs->codec_info.nego_timer) {
		hs->codec_info.is_negotiating = FALSE;
		hs->codec_info.requested_by_hf = FALSE;
		hs->codec_info.sending_codec = 0;
		g_source_remove(hs->codec_info.nego_timer);
		__bt_hfp_codec_connection_setup(hs, FALSE);
	}

	return 0;
}

/* AT+BCC (Bluetooth Codec Connection) */
static int __bt_hfp_codec_connection(bt_ag_info_t *hs, const char *buf)
{
	hfp_state_manager_err_t err = HFP_STATE_MNGR_ERR_NONE;

	err = __bt_hfp_codec_connection_setup(hs, TRUE);

	_bt_ag_send_response(hs, err);

	if (err == HFP_STATE_MNGR_ERR_NONE)
		err = __bt_hfp_send_bcs_command(hs, TRUE);

	if (err != HFP_STATE_MNGR_ERR_NONE)
		ERR("Fail to request codec connection setup");

	return 0;
}

/* AT+BCS (Bluetooth Codec Selection) */
static int __bt_hfp_codec_selection(bt_ag_info_t *hs, const char *buf)
{
	uint32_t codec = 0x00000000;
	hfp_state_manager_err_t err = HFP_STATE_MNGR_ERR_NONE;
	struct ag_codec *data = g_new0(struct ag_codec, 1);

	/* Timer reset */
	if (hs->codec_info.nego_timer) {
		g_source_remove(hs->codec_info.nego_timer);
		hs->codec_info.nego_timer = 0;
	}

	if (!(ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION))
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;
	else if (__hfp_parse_available_codecs(buf, &codec) < 0)
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;
	else if (__bt_ag_codec_selection_setup(hs, codec) !=
					BT_HFP_AGENT_ERROR_NONE)
		err = HFP_STATE_MNGR_ERR_AG_FAILURE;

	data->bt_ag_info = hs;
	data->codec_status = g_strdup ("finish");
	_bt_ag_send_response(hs, err);
	__bt_ag_codec_negotiation_finished(data);

	return 0;
}

static void __bt_ag_str2ba(const char *str, bt_addr *ba)
{
	int i;
	for (i = 5; i >= 0; i--, str += 3)
		ba->b[i] = strtol(str, NULL, 16);
}

static const char *__bt_ag_state2str(hs_state_t state)
{
	switch (state) {
	case HEADSET_STATE_DISCONNECTED:
		return "disconnected";
	case HEADSET_STATE_CONNECTING:
		return "connecting";
	case HEADSET_STATE_CONNECTED:
		return "connected";
	case HEADSET_STATE_PLAY_IN_PROGRESS:
		return "Play In Progress";
	case HEADSET_STATE_ON_CALL:
		return "On Call";
	}

	return NULL;
}

void _bt_convert_addr_string_to_type_rev(unsigned char *addr,
					const char *address)
{
        int i;
        char *ptr = NULL;

	ret_if(address == NULL);
	ret_if(addr == NULL);

        for (i = 0; i < 6; i++) {
                addr[5 - i] = strtol(address, &ptr, 16);
                if (ptr[0] != '\0') {
                        if (ptr[0] != ':')
                                return;

                        address = ptr + 1;
                }
        }
}

static gboolean __bt_ag_check_nval(GIOChannel *chan)
{
	struct pollfd file_desc;

	memset(&file_desc, 0, sizeof(file_desc));
	file_desc.fd = g_io_channel_unix_get_fd(chan);
	file_desc.events = POLLNVAL;

	if (poll(&file_desc, 1, 0) > 0 && (POLLNVAL & file_desc.revents))
		return TRUE;

	return FALSE;
}

static int __bt_ag_sco_connect(bt_ag_info_t *hs)
{
	struct sco_socket_addr sco_addr;
	int err;
	GIOChannel *io;
	int sco_skt;
	bt_voice bt_vo;
	bt_ag_slconn_t *slconn = hs->slc;
	/*guint watch_id;*/

	if (hs->state != HEADSET_STATE_CONNECTED)
		return BT_HFP_AGENT_ERROR_NOT_CONNECTED;
#ifdef TIZEN_MEDIA_ENHANCE
	_bt_ag_agent_check_transport_state();
#endif

	/* Create Sco socket */
	sco_skt = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BT_SCO_PRTCL);
	if (sco_skt < 0) {
		ERR("ERROR: Create socket failed.\n");
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	/* Bind Sco Socket to Local BD addr */
	memset(&sco_addr, 0, sizeof(sco_addr));
	sco_addr.sco_family = AF_BLUETOOTH;

	__bt_ag_str2ba(local_addr, &sco_addr.sco_bdaddr);
	DBG("Local BD address: %s", local_addr);

	err = bind(sco_skt, (struct sockaddr *) &sco_addr, sizeof(sco_addr));
	if (err < 0) {
		ERR("ERROR: sco socket binding failed");
		ERR("Close SCO skt");
		close(sco_skt);
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	DBG("Socket FD : %d", sco_skt);

	io = g_io_channel_unix_new(sco_skt);
	g_io_channel_set_close_on_unref(io, TRUE);
	/*g_io_channel_set_flags(io, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_buffered(io, FALSE);
	g_io_channel_set_encoding(io, NULL, NULL);*/

	if ((ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION) &&
		(slconn && (slconn->hs_features &
			BT_HF_FEATURE_CODEC_NEGOTIATION)) &&
			wbs_opts.wbs_enable == TRUE) {
		bt_vo.setting = (hs->codec == BT_MSBC_CODEC_ID) ?
				BT_HFP_MSBC_VOICE : BT_HFP_CVSD_VOICE;

		DBG("set Bluetooth voice: %d", bt_vo.setting);
		err = setsockopt(sco_skt, BT_SOCKET_LEVEL,
					BT_VOICE_NUM, &bt_vo, sizeof(bt_vo));
		if (err < 0) {
			ERR("ERROR: sco socket set socket option failed");
			ERR("Close SCO skt");
			close(sco_skt);
			return BT_HFP_AGENT_ERROR_INTERNAL;
		}
	} else {
		DBG("Set NB codec parameter");
		__bt_ag_set_codec(hs, "SetNbParameters");
	}

	memset(&sco_addr, 0, sizeof(sco_addr));
	sco_addr.sco_family = AF_BLUETOOTH;
	__bt_ag_str2ba(hs->remote_addr, &sco_addr.sco_bdaddr);
	DBG("remotel BD address: %s", hs->remote_addr);

	err = connect(sco_skt, (struct sockaddr *) &sco_addr, sizeof(sco_addr));
	if (err < 0 && !(errno == EINPROGRESS || errno == EAGAIN)) {
		ERR("ERROR: sco socket connect failed : %d", err);
		ERR("Close SCO skt");
		close(sco_skt);
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	/* Disabling the watch since SCO is connected */
	/*watch_id = __bt_ag_set_watch(io,
			(GIOFunc) __bt_ag_sco_connect_cb, hs);
	if (watch_id)
		DBG("SCO watch set Success");*/

	hs->sco = io;

	_bt_ag_set_headset_state(hs, HEADSET_STATE_ON_CALL);
	return BT_HFP_AGENT_ERROR_NONE;
}

static void __bt_ag_close_sco(bt_ag_info_t *hs)
{
	DBG("");
	if (hs->sco) {
		int sock = g_io_channel_unix_get_fd(hs->sco);
		shutdown(sock, SHUT_RDWR);
		g_io_channel_unref(hs->sco);
		hs->sco = NULL;
	}

	if (hs->sco_id)
		__bt_ag_agent_remove_watch(&hs->sco_id);
}

static gboolean __bt_ag_sco_server_conn_cb(GIOChannel *chan,
				GIOCondition cond, gpointer user_data)
{
	bt_ag_info_t *ag_info = user_data;

	DBG("");
	if (cond & G_IO_NVAL)
		return FALSE;

	if (cond & (G_IO_HUP | G_IO_ERR)) {
		ag_info->sco = NULL;
		if (ag_info->sco_id)
			__bt_ag_agent_remove_watch(&ag_info->sco_id);

		if (ag_info->watch_id)
			_bt_ag_set_headset_state(ag_info, HEADSET_STATE_CONNECTED);
		return FALSE;
	}
	return TRUE;
}

static gboolean __bt_ag_sco_server_cb(GIOChannel *chan,
				GIOCondition cond, gpointer user_data)
{
	bt_ag_info_t *ag_info = user_data;
	int sco_skt;
	int cli_sco_sock;
	GIOChannel *sco_io;
	bt_ag_slconn_t *slconn = ag_info->slc;
	bt_voice bt_vo;
	int err;

	if ((cond & (G_IO_NVAL | G_IO_HUP | G_IO_ERR)) ||
				__bt_ag_check_nval(chan)) {
		ERR("cond or chan is not valid");
		return FALSE;
	}

	INFO_C("Incoming SCO....");

	if (ag_info->state < HEADSET_STATE_CONNECTED)
		return BT_HFP_AGENT_ERROR_NOT_CONNECTED;

	sco_skt = g_io_channel_unix_get_fd(chan);

	cli_sco_sock = accept(sco_skt, NULL, NULL);
	if (cli_sco_sock < 0) {
		ERR("accept is failed");
		return TRUE;
	}

	sco_io = g_io_channel_unix_new(cli_sco_sock);
	g_io_channel_set_close_on_unref(sco_io, TRUE);
	g_io_channel_set_encoding(sco_io, NULL, NULL);
	g_io_channel_set_flags(sco_io, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_buffered(sco_io, FALSE);

	if ((ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION) &&
		(slconn && (slconn->hs_features &
			BT_HF_FEATURE_CODEC_NEGOTIATION)) &&
			wbs_opts.wbs_enable == TRUE) {
		bt_vo.setting = (ag_info->codec == BT_MSBC_CODEC_ID) ?
				BT_HFP_MSBC_VOICE : BT_HFP_CVSD_VOICE;

		DBG("set Bluetooth voice: %d", bt_vo.setting);
		err = setsockopt(cli_sco_sock, BT_SOCKET_LEVEL,
					BT_VOICE_NUM, &bt_vo, sizeof(bt_vo));
		if (err < 0) {
			ERR("Sco socket set socket option failed");
			close(cli_sco_sock);
			return FALSE;
		}
	}

	ag_info->sco = sco_io;
	ag_info->sco_id = g_io_add_watch(sco_io, G_IO_HUP | G_IO_ERR | G_IO_NVAL,
					__bt_ag_sco_server_conn_cb, ag_info);

	if (remote_dev_path)
		g_free(remote_dev_path);

	remote_dev_path = g_strdup(ag_info->path);

	_bt_ag_set_headset_state(ag_info, HEADSET_STATE_ON_CALL);

	return TRUE;
}

static int __bt_ag_start_sco_server(bt_ag_info_t *hs)
{
	DBG("Start SCO server");
	struct sco_socket_addr addr;
	GIOChannel *sco_io;
	int sco_skt;
	bdaddr_t bd_addr = {{0},};

	if (hs->sco_server_started) {
		DBG("Already exsist");
		return BT_HFP_AGENT_ERROR_ALREADY_EXSIST;
	}

	/* Create socket */
	sco_skt = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BT_SCO_PRTCL);
	if (sco_skt < 0) {
		ERR("Can't create socket:\n");
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	/* Bind to local address */
	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;

	_bt_convert_addr_string_to_type_rev(bd_addr.b, hs->remote_addr);
	DBG("Bind to address %s", hs->remote_addr);
	memcpy(&addr.sco_bdaddr, &bd_addr, sizeof(bdaddr_t));

	if (bind(sco_skt, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		ERR("Can't bind socket:\n");
		goto error;
	}

	if (listen(sco_skt, 1)) {
		ERR("Can not listen on the socket:\n");
		goto error;
	}

	sco_io = g_io_channel_unix_new(sco_skt);
	g_io_channel_set_close_on_unref(sco_io, TRUE);
	g_io_channel_set_encoding(sco_io, NULL, NULL);
	g_io_channel_set_flags(sco_io, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_buffered(sco_io, FALSE);

	hs->sco_server = sco_io;
	hs->sco_watch_id = g_io_add_watch(sco_io,
			G_IO_IN | G_IO_HUP | G_IO_ERR |
			G_IO_NVAL, __bt_ag_sco_server_cb, hs);

	hs->sco_server_started = TRUE;
	return BT_HFP_AGENT_ERROR_NONE;

error:
	close(sco_skt);
	return BT_HFP_AGENT_ERROR_INTERNAL;
}

void __bt_ag_stop_sco_server(bt_ag_info_t *hs)
{
	DBG("Stop SCO server");
	if (hs->sco_server) {
		g_io_channel_shutdown(hs->sco_server, TRUE, NULL);
		g_io_channel_unref(hs->sco_server);
		hs->sco_server = NULL;
	}
	hs->sco_server_started = FALSE;
}

static int __bt_ag_headset_close_rfcomm(bt_ag_info_t *hs)
{
	GIOChannel *rfcomm = hs->rfcomm;

	if (rfcomm) {
		g_io_channel_shutdown(rfcomm, TRUE, NULL);
		g_io_channel_unref(rfcomm);
		hs->rfcomm = NULL;
	}

	g_free(hs->slc);
	hs->slc = NULL;

	return 0;
}

static gboolean __bt_ag_sco_cb(GIOChannel *chan, GIOCondition cond,
			bt_ag_info_t *hs)
{
	if (cond & G_IO_NVAL)
		return FALSE;

	if (name_owner_sig_id != -1)
		g_dbus_connection_signal_unsubscribe(ag_dbus_conn,
					name_owner_sig_id);
	name_owner_sig_id = -1;
	g_free(sco_owner);
	sco_owner = NULL;

	DBG("Audio connection disconnected");
	_bt_ag_set_headset_state(hs, HEADSET_STATE_CONNECTED);

	return FALSE;
}

void _bt_ag_set_headset_state(bt_ag_info_t *hs, hs_state_t state)
{
	bt_ag_slconn_t *slconn = hs->slc;
	const char *hs_state;
	hs_state_t org_state = hs->state;
	gboolean val = FALSE;

	if (org_state == state)
		return;

	hs_state = __bt_ag_state2str(state);

	switch (state) {
	case HEADSET_STATE_CONNECTING:
		_bt_ag_agent_emit_property_changed(ag_dbus_conn,
					hs->path,
					BT_HEADSET_INTERFACE, "State",
					g_variant_new("s", hs_state));
		hs->state = state;
		break;

	case HEADSET_STATE_CONNECTED:
		if (hs->state != HEADSET_STATE_PLAY_IN_PROGRESS)
			_bt_ag_agent_emit_property_changed(ag_dbus_conn,
					hs->path,
					BT_HEADSET_INTERFACE, "State",
					g_variant_new("s", hs_state));

		if (hs->state < state) {
			val = TRUE;
			active_devices = g_slist_append(active_devices, hs);
			_bt_ag_agent_emit_property_changed(ag_dbus_conn,
						hs->path,
						BT_HEADSET_INTERFACE,
						"Connected",
						g_variant_new("b", val));

			DBG("Device %s connected\n", hs->remote_addr);
#if defined(TIZEN_SUPPORT_DUAL_HF)
			 if (!hs->is_companion_device)
				__bt_ag_start_sco_server(hs);
#else
			__bt_ag_start_sco_server(hs);
#endif

			/* Set default code as Gateway NB */
			__bt_ag_set_codec(hs, "SetNbParameters");
		} else if (hs->state == HEADSET_STATE_ON_CALL) {
			val = FALSE;
			_bt_ag_agent_emit_property_changed(ag_dbus_conn,
						hs->path,
						BT_HEADSET_INTERFACE,
						"Playing",
						g_variant_new("b", val));
		}
		hs->state = state;
		break;

	case HEADSET_STATE_DISCONNECTED:
		__bt_ag_close_sco(hs);
		__bt_ag_headset_close_rfcomm(hs);

		if (hs->state == HEADSET_STATE_ON_CALL) {
			val = FALSE;
			_bt_ag_agent_emit_property_changed(ag_dbus_conn,
						hs->path,
						BT_HEADSET_INTERFACE,
						"Playing",
						g_variant_new("b", val));
		}

		val = FALSE;
		_bt_ag_agent_emit_property_changed(ag_dbus_conn,
				hs->path,
				BT_HEADSET_INTERFACE,
				"Connected",
				g_variant_new("b", val));
		if (hs->state > HEADSET_STATE_CONNECTING)
			_bt_hfp_device_disconnected(hs);

		active_devices = g_slist_remove(active_devices, hs);

		__bt_ag_codec_negotiation_info_reset(hs, TRUE);
#if 0 /* SCO is crashed if below is called when SCO is opened by hf-agent */
		__bt_ag_set_codec(hs, "SetNbParameters");
#endif
		hs->codec = 0;

		/* Since SCO server is binded on remote address */
		/* Need to stop SCO server once heasdet disconencted*/
		if(hs->sco_server_started)
			__bt_ag_stop_sco_server(hs);

		g_free(hs->remote_addr);
		g_free(hs->slc);
		g_free(hs);
		break;

	case HEADSET_STATE_PLAY_IN_PROGRESS:
	case HEADSET_STATE_ON_CALL:
		val = TRUE;
		_bt_ag_agent_emit_property_changed(ag_dbus_conn,
					hs->path,
					BT_HEADSET_INTERFACE, "State",
					g_variant_new("s", hs_state));

		/*add watch for sco data */
		hs->sco_id = g_io_add_watch(hs->sco,
					G_IO_ERR | G_IO_NVAL,
					(GIOFunc) __bt_ag_sco_cb, hs);

		_bt_ag_agent_emit_property_changed(
				ag_dbus_conn, hs->path,
				BT_HEADSET_INTERFACE, "Playing",
				g_variant_new("b", val));

		if (slconn->microphone_gain >= 0)
			_bt_ag_send_at(hs, "\r\n+VGM=%u\r\n",
				slconn->microphone_gain);

		if (slconn->speaker_gain >= 0)
			_bt_ag_send_at(hs, "\r\n+VGS=%u\r\n",
				slconn->speaker_gain);

		hs->state = state;
		break;

	default:
		hs->state = state;
		break;
	}

	INFO("STATE CHANGED from [%s(%d)] to [%s(%d)]",
		__bt_ag_state2str(org_state), org_state, __bt_ag_state2str(state), state);
}

static struct event at_event_callbacks[] = {
	{ "AT+BRSF", _bt_hfp_supported_features },
	{ "AT+CIND", _bt_hfp_report_indicators },
	{ "AT+CMER", _bt_hfp_enable_indicators },
	{ "AT+CHLD", _bt_hfp_call_hold },
	{ "ATA", _bt_hfp_answer_call },
	{ "ATD", _bt_hfp_dial_number },
	{ "AT+VG", _bt_hfp_signal_gain_setting },
	{ "AT+CHUP", _bt_hfp_terminate_call },
	{ "AT+CKPD", _bt_hfp_key_press },
	{ "AT+CLIP", _bt_hfp_cli_notification },
	{ "AT+BTRH", _bt_hfp_response_and_hold },
	{ "AT+BLDN", _bt_hfp_last_dialed_number },
	{ "AT+VTS", _bt_hfp_dtmf_tone },
	{ "AT+CNUM", _bt_hfp_subscriber_number },
	{ "AT+CLCC", _bt_hfp_list_current_calls },
	{ "AT+CMEE", _bt_hfp_extended_errors },
	{ "AT+CCWA", _bt_hfp_call_waiting_notify },
	{ "AT+COPS", _bt_hfp_operator_selection },
	{ "AT+NREC", _bt_hfp_nr_and_ec },
	{ "AT+BVRA", _bt_hfp_voice_dial },
	{ "AT+XAPL", _bt_hfp_apl_command },
	{ "AT+IPHONEACCEV", _bt_hfp_apl_command },
	{ "AT+BIA", _bt_hfp_indicators_activation },
	{ "AT+CPBS", _bt_hfp_select_pb_memory },
	{ "AT+CPBR", _bt_hfp_read_pb_entries},
	{ "AT+CPBF", _bt_hfp_find_pb_entires },
	{ "AT+CSCS", _bt_hfp_select_character_set },
	{ "AT+CSQ", _bt_hfp_get_signal_quality },
	{ "AT+CBC", _bt_hfp_get_battery_charge_status },
	{ "AT+CPAS", _bt_hfp_get_activity_status },
	{ "AT+CGSN", _bt_hfp_get_equipment_identity },
	{ "AT+CGMM", _bt_hfp_get_model_information },
	{ "AT+CGMI", _bt_hfp_get_device_manufacturer },
	{ "AT+CGMR", _bt_hfp_get_revision_information },
	{ "AT+BAC", __bt_hfp_available_codecs },
	{ "AT+BCC", __bt_hfp_codec_connection },
	{ "AT+BCS", __bt_hfp_codec_selection },
	{ "AT+XSAT", _bt_hfp_vendor_cmd },
	{ "AT+CIMI", _bt_hfp_get_imsi },
	{ "AT+CREG", _bt_hfp_get_creg_status },
	{ 0 }
};

int num_of_secure_command = 4;
static const char* secure_command[] = {"CLCC", "CLIP", "CPBR", "CCWA"};

void __bt_ag_agent_print_at_buffer(char *message, const char *buf)
{

	int i = 0;
	char s[MAX_BUFFER_SIZE] = {0, };
	gboolean hide = FALSE;

	gboolean is_security_command = FALSE;
	char *xsat_ptr;

	strncpy(s, buf, MAX_BUFFER_SIZE - 1);

	for (i=0; i<num_of_secure_command; i++) {
		if (strstr(buf, secure_command[i])) {
			is_security_command = TRUE;
			break;
		}
	}

done:
	/* +XSAT: 11,DISC */
	xsat_ptr =  strstr(s, "11,DISC,");
	if (xsat_ptr) {
		xsat_ptr = xsat_ptr + 8;
		int x = 0;
		while (xsat_ptr[x] != '\0' && xsat_ptr[x] != '\r' && xsat_ptr[x] != '\n') {
			xsat_ptr[x] = 'X';
			x++;
		}
	}

	/* AT+XSAT=11,Q_CT,X,XXXX */
	xsat_ptr =  strstr(s, "11,Q_CT,");
	if (xsat_ptr) {
		xsat_ptr = xsat_ptr + 8;
		int x = 0;
		while (xsat_ptr[x] != '\0' && xsat_ptr[x] != '\r' && xsat_ptr[x] != '\n') {
			if (x > 1) /* ignore 0 and 1 position */
				xsat_ptr[x] = 'X';
			x++;
		}
	}

	i = 0;
	while (s[i] != '\0') {
		if (s[i] == '\r' || s[i] == '\n') {
			s[i] = '.';
		} else {
			if (s[i] == '\"')
				hide = hide ? FALSE : TRUE;
			else if (is_security_command && hide) {
				if (i % 2)
					s[i] = 'X';
			}
		}
		i++;
	}
	if (message)
		INFO("%s Buffer = [%s], Len(%d)", message, s, strlen(s));
	else
		INFO("[%s]", s);
}

static int __bt_ag_at_handler(bt_ag_info_t *hs, const char *buf)
{
	struct event *ev;

	__bt_ag_agent_print_at_buffer("[AG AT CMD][RCVD] :", buf);

	for (ev = at_event_callbacks; ev->cmd; ev++) {
		if (!strncmp(buf, ev->cmd, strlen(ev->cmd)))
			return ev->callback(hs, buf);
	}

	return -EINVAL;
}

static int __bt_ag_send_at_valist(bt_ag_info_t *hdset, va_list list,
			char *list_format)
{
	ssize_t final_written, count;
	char rsp_buffer[MAX_BUFFER_SIZE];
	int fd;
	int err;

	count = vsnprintf(rsp_buffer, sizeof(rsp_buffer), list_format, list);
	if (count < 0) {
		ERR("count is %d", count);
		return -EINVAL;
	}

	if (!hdset->io_chan) {
		ERR("__bt_ag_send_at_valist: headset not connected");
		return -EIO;
	}

	final_written = 0;

	fd = g_io_channel_unix_get_fd(hdset->io_chan);

	if (fd != 0) {
		while (final_written < count) {
			ssize_t written;

			do {
				written = write(fd, rsp_buffer + final_written,
						count - final_written);
			} while (written < 0 && errno == EINTR);

			if (written < 0) {
				err = -errno;
				ERR("write failed : %s (%d)", strerror(-err), -err);
				return -errno;
			}

			final_written += written;
		}

		/* Synchronize the sending buffer */
		sync();
		fsync(fd);
	} else {
		ERR("FD is 0. remote_addr : %s", hdset->remote_addr);
		return -1;
	}

	__bt_ag_agent_print_at_buffer("[AG AT CMD][SENT]", rsp_buffer);

	return 0;
}

int __attribute__((format(printf, 2, 3)))
			_bt_ag_send_at(bt_ag_info_t *hs, char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = __bt_ag_send_at_valist(hs, ap, format);
	va_end(ap);

	return ret;
}

void __attribute__((format(printf, 3, 4)))
		_bt_ag_send_foreach_headset(GSList *devices,
				int (*cmp) (bt_ag_info_t *hs),
				char *format, ...)
{
	GSList *l;
	va_list ap;

	for (l = devices; l != NULL; l = l->next) {
		bt_ag_info_t *hs = l->data;
		int ret;

		if (cmp && cmp(hs) != 0)
			continue;

		va_start(ap, format);
		ret = __bt_ag_send_at_valist(hs, ap, format);
		if (ret < 0)
			ERR("Failed to send to headset: %s (%d)",
					strerror(-ret), -ret);
		va_end(ap);
	}
}

int _bt_ag_send_response(bt_ag_info_t *hs, hfp_state_manager_err_t err)
{
	if ((err != HFP_STATE_MNGR_ERR_NONE) && hs->slc->is_cme_enabled)
		return _bt_ag_send_at(hs, "\r\n+CME ERROR: %d\r\n", err);

	switch (err) {
	case HFP_STATE_MNGR_ERR_NONE:
		return _bt_ag_send_at(hs, "\r\nOK\r\n");
	case HFP_STATE_MNGR_ERR_NO_NETWORK_SERVICE:
		return _bt_ag_send_at(hs, "\r\nNO CARRIER\r\n");
	default:
		return _bt_ag_send_at(hs, "\r\nERROR\r\n");
	}
}

static gboolean __bt_ag_event_handler(GIOChannel *channel,
				GIOCondition cond, void *user_data)
{
	bt_ag_slconn_t *slconn;
	unsigned char event_buf[MAX_BUFFER_SIZE];
	ssize_t len;
	size_t available_buffer;
	int fd;
	bt_ag_info_t *bt_ag_info = (bt_ag_info_t *)user_data;


	if (cond & G_IO_NVAL)
		return FALSE;

	slconn = bt_ag_info->slc;
	if (cond & (G_IO_ERR | G_IO_HUP)) {
		if (bt_ag_info->watch_id)
			__bt_ag_agent_remove_watch(&bt_ag_info->watch_id);

		ERR("ERR or HUP on RFCOMM socket");
		INFO_C("Disconnected [AG role] [Terminated by remote dev]");
		goto failed;
	}

	fd = g_io_channel_unix_get_fd(channel);
	len = read(fd, event_buf, sizeof(event_buf) - 1);

	if (len < 0)
		return FALSE;
	available_buffer = sizeof(slconn->buffer) - (slconn->start) -
				(slconn->length) - 1;
	if (available_buffer < (size_t) len) {
		ERR("Buffer over flow");
		goto failed;
	}

	memcpy(&slconn->buffer[slconn->start], event_buf, len);
	slconn->length += len;

	slconn->buffer[slconn->start + slconn->length] = '\0';

	while (slconn->length > 0) {
		char *get_cr;
		int err;
		off_t cmd_len;

		get_cr = strchr(&slconn->buffer[slconn->start], '\r');
		if (!get_cr)
			break;

		cmd_len = 1 + (off_t) get_cr -
			(off_t) &slconn->buffer[slconn->start];
		*get_cr = '\0';

		if (cmd_len > 1) {
			DBG("Call AT handler");
			err = __bt_ag_at_handler(bt_ag_info,
					&slconn->buffer[slconn->start]);
		} else {
			ERR("Failed to call AT handler");
			err = 0;
		}

		if (err == -EINVAL) {
			ERR("Unrecognized command: %s",
				&slconn->buffer[slconn->start]);
			err = _bt_ag_send_response(bt_ag_info,
					HFP_STATE_MNGR_ERR_NOT_SUPPORTED);
			if (err < 0)
				goto failed;
		} else if (err < 0)
			ERR("Error handling command %s: %s (%d)",
						&slconn->buffer[slconn->start],
						strerror(-err), -err);

		slconn->start += cmd_len;
		slconn->length -= cmd_len;

		if (!slconn->length)
			slconn->start = 0;
	}
	return TRUE;
failed:
	ERR("Failed in event handler - SLC Disconnect");
	_bt_ag_set_headset_state(bt_ag_info,
					HEADSET_STATE_DISCONNECTED);
	return FALSE;
}

static gboolean __bt_ag_agent_connection(gint32 fd, const gchar *device_path,
						const gchar *object_path)
{
	GIOFlags flags;

	bt_ag_info_t *bt_ag_info = g_new0(bt_ag_info_t, 1);
	struct sockaddr_remote address;
	socklen_t address_len;

	INFO_C("Connected [AG role]");
	bt_ag_info->rfcomm = NULL;
	bt_ag_info->slc = NULL;
	bt_ag_info->hfp_active = TRUE;
	bt_ag_info->vr_blacklisted = FALSE;
	bt_ag_info->state = HEADSET_STATE_DISCONNECTED;
	bt_ag_info->sco_server_started = FALSE;
	__bt_ag_codec_negotiation_info_reset(bt_ag_info, TRUE);

	bt_ag_info->path = device_path;
	DBG("device_path = [%s]", device_path);

	address_len = sizeof(address);
	if (getpeername(fd, (struct sockaddr *) &address, &address_len) != 0)
		ERR("BD_ADDR is NULL");

	DBG("RFCOMM connection for HFP/HSP is completed. Fd = [%d]", fd);
	bt_ag_info->fd = fd;
	bt_ag_info->io_chan = g_io_channel_unix_new(bt_ag_info->fd);
	flags = g_io_channel_get_flags(bt_ag_info->io_chan);

	flags &= ~G_IO_FLAG_NONBLOCK;
	flags &= G_IO_FLAG_MASK;
	g_io_channel_set_flags(bt_ag_info->io_chan, flags, NULL);
	g_io_channel_set_encoding(bt_ag_info->io_chan, NULL, NULL);
	g_io_channel_set_buffered(bt_ag_info->io_chan, FALSE);

	bt_ag_info->rfcomm = g_io_channel_ref(bt_ag_info->io_chan);

	bt_ag_info->remote_addr = g_malloc0(BT_ADDRESS_STRING_SIZE);
	__bt_convert_addr_type_to_rev_string(bt_ag_info->remote_addr,
						address.remote_bdaddr.b);

#if defined(TIZEN_SUPPORT_DUAL_HF)
	bt_ag_info->is_companion_device =
			__bt_ag_agent_is_companion_device(bt_ag_info->remote_addr);
#endif

	DBG("remote Device Address = [%s]", bt_ag_info->remote_addr);

	if (g_strcmp0(object_path, BT_HS_AG_AGENT_OBJECT_PATH) == 0) {
		DBG("HSP connection completed");
		_bt_ag_set_headset_state(bt_ag_info,
						HEADSET_STATE_CONNECTED);
	}
	else {
		DBG("HFP connection connecting");
		_bt_ag_set_headset_state(bt_ag_info,
						HEADSET_STATE_CONNECTING);
	}

	__bt_ag_agent_start_watch(bt_ag_info);

	bt_ag_info->slc = g_new0(bt_ag_slconn_t, 1);
	bt_ag_info->slc->speaker_gain = 15;
	bt_ag_info->slc->microphone_gain = 15;
	bt_ag_info->slc->is_nrec = TRUE;

	return TRUE;
}

static gboolean __bt_ag_agent_is_device_vr_blacklisted(const char *lap_addr)
{
	char *buffer;
	FILE *fp;
	long size;
	size_t result;
	char *token;
	char *saveptr;

	fp = fopen(AGENT_VR_BLACKLIST_FILE, "r");

	if (fp == NULL) {
		ERR("Unable to open VR blacklist file");
		return FALSE;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	if (size <= 0) {
		ERR("size is not a positive number");
		fclose(fp);
		return FALSE;
	}

	rewind(fp);

	buffer = g_malloc0(sizeof(char) * size);
	if (buffer == NULL) {
		ERR("g_malloc0 is failed");
		fclose(fp);
		return FALSE;
	}
	result = fread((char *)buffer, 1, size, fp);
	fclose(fp);
	if (result != size) {
		ERR("Read Error");
		g_free(buffer);
		return FALSE;
	}

	token= strtok_r(buffer, "=", &saveptr);
	if (token == NULL) {
		g_free(buffer);
		return FALSE;
	}

	while ((token = strtok_r(NULL, ",", &saveptr))) {
		if (strlen(token) > 8)
			token[8] = '\0';
		if (0 == g_strcmp0(token, lap_addr)) {
			INFO("Voice Recognition blacklisted");
			g_free(buffer);
			return TRUE;
		}
	}
	g_free(buffer);
	return FALSE;
}

static gboolean __bt_sco_open_delay_timeout_cb(gpointer user_data)
{
	bt_ag_info_t *bt_ag_info = (bt_ag_info_t *)user_data;
	sco_open_timer_id = 0;
	DBG("sco_open_request (%d)", sco_open_request);

	if (sco_open_request && bt_ag_info->state == HEADSET_STATE_CONNECTED) {
		bt_ag_slconn_t *slconn = bt_ag_info->slc;

		INFO("try to open SCO");
		sco_open_request = FALSE;

		if ((ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION) &&
				(slconn && (slconn->hs_features &
					BT_HF_FEATURE_CODEC_NEGOTIATION))) {
			switch (bt_ag_info->codec_info.final_codec) {
			case BT_CVSD_CODEC_ID:
				__bt_ag_set_codec(bt_ag_info, "SetNbParameters");
				__bt_ag_sco_connect(bt_ag_info);
				break;
			case BT_MSBC_CODEC_ID:
				__bt_ag_set_codec(bt_ag_info, "SetWbsParameters");
				__bt_ag_sco_connect(bt_ag_info);
				break;
			default:
				__bt_hfp_codec_connection_setup(bt_ag_info, FALSE);
				break;
			}
		} else
			__bt_ag_sco_connect(bt_ag_info);
	}

	return FALSE;
}

/*
* Service level connection complete
* indication and state management
*/
void _bt_ag_slconn_complete(bt_ag_info_t *hs)
{
	char lap_address[BT_LOWER_ADDRESS_LENGTH];

	DBG("HFP Service Level Connection established\n");

	/* Check device Voice Recognition blacklist status */
	g_strlcpy(lap_address, hs->remote_addr, sizeof(lap_address));
	hs->vr_blacklisted =
		__bt_ag_agent_is_device_vr_blacklisted(lap_address);

	if (sco_open_timer_id > 0) {
		g_source_remove(sco_open_timer_id);
		sco_open_timer_id = 0;
	}

	sco_open_request = FALSE;
	sco_open_timer_id = g_timeout_add(BT_SCO_OPEN_DELAY_TIMER,
						__bt_sco_open_delay_timeout_cb, hs);

	_bt_ag_set_headset_state(hs, HEADSET_STATE_CONNECTED);
}

static gboolean __bt_ag_agent_connection_release(bt_ag_info_t *hs)
{

	g_io_channel_shutdown(hs->io_chan, TRUE, NULL);
	g_io_channel_unref(hs->io_chan);
	hs->io_chan = NULL;

	if (hs->sco) {
		__bt_ag_close_sco(hs);
		_bt_ag_set_headset_state(hs, HEADSET_STATE_CONNECTED);
		hs->sco = NULL;
	}
	__bt_ag_agent_remove_watch(&hs->watch_id);

	_bt_ag_set_headset_state(hs, HEADSET_STATE_DISCONNECTED);
	return TRUE;
}

static GQuark __bt_ag_agent_error_quark(void)
{
	FN_START;

	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string("ag-agent");

	FN_END;
	return quark;
}

static GError *__bt_ag_agent_set_error(bt_hfp_agent_error_t error)
{
	FN_START;
	ERR("error[%d]\n", error);

	switch (error) {
	case BT_HFP_AGENT_ERROR_NOT_AVAILABLE:
		return g_error_new(BT_AG_AGENT_ERROR, error,
					BT_ERROR_NOT_AVAILABLE);
	case BT_HFP_AGENT_ERROR_NOT_CONNECTED:
	return g_error_new(BT_AG_AGENT_ERROR, error,
					BT_ERROR_NOT_CONNECTED);
	case BT_HFP_AGENT_ERROR_BUSY:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_BUSY);
	case BT_HFP_AGENT_ERROR_INVALID_PARAM:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_INVALID_PARAM);
	case BT_HFP_AGENT_ERROR_ALREADY_EXSIST:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_ALREADY_EXSIST);
	case BT_HFP_AGENT_ERROR_ALREADY_CONNECTED:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_ALREADY_CONNECTED);
	case BT_HFP_AGENT_ERROR_NO_MEMORY:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_NO_MEMORY);
	case BT_HFP_AGENT_ERROR_I_O_ERROR:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_I_O_ERROR);
	case BT_HFP_AGENT_ERROR_OPERATION_NOT_AVAILABLE:
		return g_error_new(BT_AG_AGENT_ERROR, error,
					BT_ERROR_OPERATION_NOT_AVAILABLE);
	case BT_HFP_AGENT_ERROR_BATTERY_STATUS:
		return g_error_new(BT_AG_AGENT_ERROR, error,
					BT_ERROR_BATTERY);
	case BT_HFP_AGENT_ERROR_SIGNAL_STATUS:
		return g_error_new(BT_AG_AGENT_ERROR, error,
					BT_ERROR_SIGNAL);
	case BT_HFP_AGENT_ERROR_NO_CALL_LOGS:
		return g_error_new(BT_AG_AGENT_ERROR, error,
					BT_ERROR_NO_CALL_LOG);
	case BT_HFP_AGENT_ERROR_INTERNAL:
	default:
		return g_error_new(BT_AG_AGENT_ERROR, error,
						BT_ERROR_INTERNAL);
	}
	FN_END;
}

static bt_ag_info_t *__bt_get_active_headset(const gchar *device_path)
{
	GSList *l;

	for (l = active_devices ; l; l = l->next) {
		bt_ag_info_t *data = l->data;
		if (device_path == NULL) /*just to avoid crash incase of "play" when dailed from device[NEEDS TO BE CHANGED]*/
			return data;
		if (g_strcmp0(data->path, device_path) == 0) {
			INFO("Active device found");
			return data;
		}
	}

	INFO("Active device not found");
	return NULL;
}

static void __bt_ag_agent_method(GDBusConnection *connection,
			const gchar *sender,
			const gchar *object_path,
			const gchar *interface_name,
			const gchar *method_name,
			GVariant *parameters,
			GDBusMethodInvocation *invocation,
			gpointer user_data)
{
	FN_START;

	INFO("method %s", method_name);
	INFO("object_path %s", object_path);
	int ret = BT_HFP_AGENT_ERROR_NONE;
	GError *err = NULL;
	const gchar *device_path = NULL;

	if (g_strcmp0(method_name, "NewConnection") == 0) {
		gint32 fd;
		int index = 0;
		GDBusMessage *msg;
		GUnixFDList *fd_list;
		GVariant *options = NULL;
		int device_count = 0;
		GSList *l;

		device_count = g_slist_length(active_devices);

		INFO("device_count %d", device_count);

		if (device_count >= MAX_CONNECTED_DEVICES) {
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		g_variant_get(parameters, "(oha{sv})",
						&device_path, &index, &options);
#if defined(TIZEN_SUPPORT_DUAL_HF)
		if (device_count &&
			__bt_ag_agent_check_dual_hf_condition(device_path) == FALSE) {
			INFO("not allow to connect 2nd HF connection");
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}
#endif
		msg = g_dbus_method_invocation_get_message(invocation);
		fd_list = g_dbus_message_get_unix_fd_list(msg);
		if (fd_list == NULL) {
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		fd = g_unix_fd_list_get(fd_list, index, NULL);
		if (fd == -1) {
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		DBG("FD is = [%d], device_path = [%s]\n", fd, device_path);

		if (!__bt_ag_agent_connection(fd, device_path, object_path)) {
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "RequestDisconnection") == 0) {
		GSList *l;

		g_variant_get(parameters, "(o)", &device_path);
		INFO("device_path %s", device_path);

		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;

			INFO("data->path %s", data->path);
			if (g_strcmp0(data->path, device_path) == 0) {
				if (!__bt_ag_agent_connection_release(data)) {
					ret = BT_HFP_AGENT_ERROR_INTERNAL;
					goto fail;
				}
				INFO_C("Disconnected [AG role] [Terminated by local host]");
				g_dbus_method_invocation_return_value(invocation, NULL);
			}
		}
	} else if (g_strcmp0(method_name, "RegisterApplication") == 0) {
		gchar *path = NULL;
		gchar *address = NULL;
		g_variant_get(parameters, "(&s&s)", &path, &address);
		/*local_addr = malloc(strlen(address));
		memcpy(local_addr, address, strlen(address));*/

		DBG("Sender = %s, Application path = %s\n", sender, path);
		ret = _bt_hfp_register_telephony_agent(TRUE, path, sender);
		if (ret)
			goto fail;

		if (local_addr)
			g_free(local_addr);

		local_addr = g_strdup(address);
		DBG("Address = %s\n", local_addr);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "UnregisterApplication") == 0) {
		gchar *path = NULL;
		g_variant_get(parameters, "(&s)", &path);

		DBG("Application path = %s\n", path);
		DBG("Sender = %s\n", sender);

		ret = _bt_hfp_register_telephony_agent(FALSE, path, sender);
		if (ret)
			goto fail;

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "IncomingCall") == 0) {
		gchar *path = NULL;
		gchar *number = NULL;
		gint call_id = 0;

		g_variant_get(parameters, "(&s&si)", &path, &number, &call_id);

		DBG("Application path = %s", path);
		DBG_SECURE("Phone number = %s", number);
		DBG("Call id = %d", call_id);

		DBG("Sender = %s", sender);

		ret = _bt_hfp_incoming_call(path, number, call_id, sender);
		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "OutgoingCall") == 0) {
		gchar *path = NULL;
		gchar *number = NULL;
		gint call_id = 0;

		g_variant_get(parameters, "(&s&si)", &path, &number, &call_id);

		DBG("Application path = %s", path);
		DBG_SECURE("Phone number = %s", number);
		DBG("Call id = %d", call_id);

		DBG("Sender = %s", sender);

		ret = _bt_hfp_outgoing_call(path, number, call_id, sender);
		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "ChangeCallStatus") == 0) {
		gchar *path = NULL;
		gchar *number = NULL;
		gint status = 0;
		gint call_id = 0;
		GSList *l;

		g_variant_get(parameters, "(&s&sii)",
					&path, &number, &status, &call_id);
		DBG("Application path = %s\n", path);
		DBG_SECURE("Number = %s\n", number);
		DBG("Status = %d\n", status);
		DBG("Call id = %d\n", call_id);
		DBG("Sender = %s\n", sender);

		ret = _bt_hfp_change_call_status(path,
					number, status, call_id, sender);

		if (_bt_hfp_is_call_exist() == FALSE) {
			for (l = active_devices; l; l = l->next) {
				bt_ag_info_t *data = l->data;

				if(data->state == HEADSET_STATE_ON_CALL) {
					__bt_ag_close_sco(data);
					_bt_ag_set_headset_state(data,
						HEADSET_STATE_CONNECTED);
				}
			}
		}

		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "GetProperties") == 0) {
		GVariantBuilder *builder;
		GVariant *var_data;
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);

		if (bt_ag_info) {
			gchar *codec = g_strdup("codec");
			gchar *nrec = g_strdup("nrec");

			builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

			g_variant_builder_add(builder, "{sv}",
					codec, g_variant_new("u", bt_ag_info->codec_info.final_codec));
			g_variant_builder_add(builder, "{sv}",
					nrec, g_variant_new("b", bt_ag_info->nrec_status));

			var_data = g_variant_new("(a{sv})", builder);
			g_variant_builder_unref(builder);
			g_dbus_method_invocation_return_value(invocation, var_data);

			g_free(codec);
			g_free(nrec);
		}
	} else if (g_strcmp0(method_name, "Disconnect") == 0) {
		char hdset_address[18] = { 0, };
		GSList *l;

		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;

			__bt_convert_addr_type_to_rev_string(hdset_address,
					(unsigned char *)data->remote_addr);

			DBG("Disconnect Headset %s, %s\n",
						hdset_address, data->path);
			_bt_ag_set_headset_state(data,
						HEADSET_STATE_DISCONNECTED);
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "IsConnected") == 0) {
		gboolean is_connected = FALSE;
		GSList *l;

		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;

			if(data->state == HEADSET_STATE_CONNECTED)
				is_connected = TRUE;
		}
		DBG("is_connected : %s",
				is_connected ? "Connected":"Disconnected");

		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(b)", is_connected));
	} else if (g_strcmp0(method_name, "IndicateCall") == 0) {
		GSList *l;

		if (0 == g_slist_length(active_devices)) {
			ret = BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			goto fail;
		}

		if (ag.ring_timer) {
			DBG("IndicateCall received when already indicating");
			g_dbus_method_invocation_return_value(invocation, NULL);
		}

		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;

			if(data->state >= HEADSET_STATE_CONNECTED)
				_bt_ag_send_at(data, "\r\nRING\r\n");
		}

		__bt_ring_timer_cb(NULL);
		ag.ring_timer = g_timeout_add(AG_RING_INTERVAL,
				__bt_ring_timer_cb, NULL);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "CancelCall") == 0) {
		if (0 == g_slist_length(active_devices)) {
			ret = BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			goto fail;
		}

		if (ag.ring_timer) {
			g_source_remove(ag.ring_timer);
			ag.ring_timer = 0;
		} else
			DBG("Got CancelCall method call but no call is active");

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "Play") == 0) {
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);
		bt_ag_slconn_t *slconn = NULL;

		if (bt_ag_info) {
			slconn = bt_ag_info->slc;
		} else {
			ret = BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			goto fail;
		}

#ifndef __TIZEN_OPEN__
#ifdef MDM_PHASE_2
		int mode;
		if ( slconn && FALSE == slconn->is_voice_recognition_running &&
				mdm_get_service() == MDM_RESULT_SUCCESS) {
			mode = mdm_get_allow_bluetooth_outgoing_call();
			mdm_release_service();

			if (mode == MDM_RESTRICTED) {
				ERR("[MDM] Not allow the outgoing call");
				ret = BT_HFP_AGENT_ERROR_OPERATION_NOT_AVAILABLE;
				goto fail;
			}
		}
#endif
#endif

		switch (bt_ag_info->state) {
		case HEADSET_STATE_CONNECTING:
		case HEADSET_STATE_DISCONNECTED:
			ERR("HEADSET_STATE  ERROR");
			ret = BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			break;
		case HEADSET_STATE_CONNECTED:
			break;
		case HEADSET_STATE_PLAY_IN_PROGRESS:
			ERR("Play In Progress");
			ret = BT_HFP_AGENT_ERROR_BUSY;
			break;
		default:
			break;
		}
		if (ret)
			goto fail;

		if (sco_open_timer_id > 0) {
			INFO("SCO open delay");
			sco_open_request = TRUE;
			g_dbus_method_invocation_return_value(invocation, NULL);
			return;
		}

		if ((ag.features & BT_AG_FEATURE_CODEC_NEGOTIATION) &&
				(slconn && (slconn->hs_features &
					BT_HF_FEATURE_CODEC_NEGOTIATION))) {
			switch (bt_ag_info->codec_info.final_codec) {
			case BT_CVSD_CODEC_ID:
				__bt_ag_set_codec(bt_ag_info, "SetNbParameters");
				ret = __bt_ag_sco_connect(bt_ag_info);
				break;
			case BT_MSBC_CODEC_ID:
				__bt_ag_set_codec(bt_ag_info, "SetWbsParameters");
				ret = __bt_ag_sco_connect(bt_ag_info);
				break;
			default:
				ret = __bt_hfp_codec_connection_setup(bt_ag_info, FALSE);
				break;
			}
		} else
			ret = __bt_ag_sco_connect(bt_ag_info);

		if (ret)
			goto fail;

		g_free(sco_owner);
		sco_owner = g_strdup(sender);

		g_dbus_method_invocation_return_value(invocation, NULL);

		name_owner_sig_id = g_dbus_connection_signal_subscribe(ag_dbus_conn,
					NULL, NULL, "NameOwnerChanged", NULL, NULL, 0,
					__bt_ag_name_owner_changed_cb, NULL, NULL);
	} else if (g_strcmp0(method_name, "Stop") == 0) {
		GSList *l;

		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;

			if(data->state > HEADSET_STATE_CONNECTED) {
				__bt_ag_close_sco(data);
				_bt_ag_set_headset_state(data,
					HEADSET_STATE_CONNECTED);
			}
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "IsPlaying") == 0) {
		gboolean is_playing = FALSE;
		GSList *l;

		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;

			if(data->state == HEADSET_STATE_ON_CALL)
				is_playing = TRUE;
		}
		DBG("is_playing : %s", is_playing ? "Playing":"Not Playing");

		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(b)", is_playing));
	} else if (g_strcmp0(method_name, "GetSpeakerGain") == 0) {
		bt_ag_slconn_t *slconn = NULL;
		guint16 gain_value = 0;
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);

		if (bt_ag_info == NULL) {
			ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}

		if (bt_ag_info->state < HEADSET_STATE_CONNECTED) {
			ret = BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			goto fail;
		}

		slconn = bt_ag_info->slc;
		if (slconn)
			gain_value = (guint16) slconn->speaker_gain;

		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(q)", gain_value));
	} else if (g_strcmp0(method_name, "SetSpeakerGain") == 0) {
		guint16 gain = 0;
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);

		g_variant_get(parameters, "(q)", &gain);
		DBG("Speaker gain = %d\n", gain);

		if (bt_ag_info == NULL) {
			ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}

		ret = _bt_hfp_set_speaker_gain(bt_ag_info, gain);
		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "GetMicrophoneGain") == 0) {
		bt_ag_slconn_t *slconn = NULL;
		guint16 gain_value = 0;
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);

		if (bt_ag_info == NULL) {
			ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}

		if (bt_ag_info->state < HEADSET_STATE_CONNECTED) {
			ret = BT_HFP_AGENT_ERROR_NOT_CONNECTED;
			goto fail;
		}

		slconn = bt_ag_info->slc;
		if (slconn)
			gain_value = (guint16) slconn->microphone_gain;

		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(q)", gain_value));
	} else if (g_strcmp0(method_name, "SetMicrophoneGain") == 0) {
		guint16 gain = 0;
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);

		g_variant_get(parameters, "(q)", &gain);
		DBG("Microphone gain = %d\n", gain);

		if (bt_ag_info == NULL) {
			ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}

		ret = _bt_hfp_set_microphone_gain(bt_ag_info, gain);
		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "SetVoiceDial") == 0) {
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);
		if (bt_ag_info == NULL) {
			ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}

		bt_ag_slconn_t *slconn = bt_ag_info->slc;
		gboolean enable;

		g_variant_get(parameters, "(b)", &enable);
		DBG("VoiceDail enable = %d\n", enable);

		if ((slconn && !(slconn->hs_features &
					BT_HF_FEATURE_VOICE_RECOGNITION)))
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
		else if (bt_ag_info->vr_blacklisted)
			ret = BT_HFP_AGENT_ERROR_INTERNAL;
		else
			ret = _bt_hfp_set_voice_dial(bt_ag_info, enable);

		if (slconn)
			slconn->is_voice_recognition_running = enable;

		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "SendVendorAtCmd") == 0) {
		gchar *cmd;
		bt_ag_info_t *bt_ag_info = __bt_get_active_headset(remote_dev_path);
		if (bt_ag_info == NULL) {
			ret = BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
			goto fail;
		}

		g_variant_get(parameters, "(&s)", &cmd);
		if (cmd == NULL){
			ret = BT_HFP_AGENT_ERROR_INVALID_PARAM;
			goto fail;
		}

		DBG("vendor cmd = %s", cmd);

		ret = _bt_hfp_send_vendor_cmd(bt_ag_info, cmd);
		if (ret)
			goto fail;
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "CheckPrivilege") == 0) {
		DBG("Already pass dbus SMACK for bt-service::platform");
		/* Return success */
		g_dbus_method_invocation_return_value(invocation, NULL);
	}

	INFO("-");
	return;

fail:
	err = __bt_ag_agent_set_error(ret);
	g_dbus_method_invocation_return_gerror(invocation, err);
	g_error_free(err);
	INFO("-");
}

static const GDBusInterfaceVTable method_table = {
	__bt_ag_agent_method,
	NULL,
	NULL,
};

static GDBusNodeInfo *__bt_ag_create_method_node_info
					(const gchar *introspection_data)
{
	GError *err = NULL;
	GDBusNodeInfo *node_info = NULL;

	if (introspection_data == NULL)
		return NULL;

	node_info = g_dbus_node_info_new_for_xml(introspection_data, &err);

	if (err) {
		ERR("Unable to create node: %s", err->message);
		g_clear_error(&err);
	}
	return node_info;
}

static GDBusConnection *__bt_ag_get_gdbus_connection(void)
{
	FN_START;

	GError *err = NULL;

	if (ag_dbus_conn == NULL)
		ag_dbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);

	if (!ag_dbus_conn) {
		if (err) {
			ERR("Unable to connect to dbus: %s", err->message);
			g_clear_error(&err);
		}
		return NULL;
	}
	FN_END;

	return ag_dbus_conn;
}

static gboolean __bt_ag_register_profile_methods(void)
{
	FN_START;
	GError *error = NULL;
	guint owner_id;
	GDBusNodeInfo *node_info;
	gchar *path;

	owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
				BT_AG_SERVICE_NAME,
				G_BUS_NAME_OWNER_FLAGS_NONE,
				NULL, NULL, NULL,
				NULL, NULL);

	DBG("owner_id is [%d]", owner_id);

	node_info = __bt_ag_create_method_node_info(
				ag_agent_bluez_introspection_xml);
	if (node_info == NULL)
		return FALSE;

	path = g_strdup(BT_AG_AGENT_OBJECT_PATH);
	DBG("path is [%s]", path);

	hf_bluez_id = g_dbus_connection_register_object(ag_dbus_conn, path,
					node_info->interfaces[0],
					&method_table,
					NULL, NULL, &error);
	if (hf_bluez_id == 0) {
		ERR("Failed to register: %s", error->message);
		g_error_free(error);
		g_free(path);
		return FALSE;
	}
	g_free(path);

	/* Ag register profile methods for HSP*/

	path = g_strdup(BT_HS_AG_AGENT_OBJECT_PATH);
	DBG("path is [%s]", path);

	hs_bluez_id = g_dbus_connection_register_object(ag_dbus_conn, path,
					node_info->interfaces[0],
					&method_table,
					NULL, NULL, &error);
	if (hs_bluez_id == 0) {
		ERR("Failed to register: %s", error->message);
		g_error_free(error);
		g_free(path);
		return FALSE;
	}
	g_free(path);

	node_info = __bt_ag_create_method_node_info
				(ag_agent_app_introspection_xml);
	if (node_info == NULL)
		return FALSE;

	path = g_strdup(BT_AG_AGENT_OBJECT_PATH);
	DBG("path is [%s]", path);

	app_id = g_dbus_connection_register_object(ag_dbus_conn, path,
						node_info->interfaces[0],
						&method_table,
						NULL, NULL, &error);
	if (app_id == 0) {
		ERR("Failed to register: %s", error->message);
		g_error_free(error);
		g_free(path);
		return FALSE;
	}
	g_free(path);

	FN_END;
	return TRUE;
}

static void __bt_ag_unregister_profile_methods(void)
{
	DBG("");

	if (hf_bluez_id > 0) {
		g_dbus_connection_unregister_object(ag_dbus_conn,
							hf_bluez_id);
		hf_bluez_id = 0;
	}

	if (hs_bluez_id > 0) {
		g_dbus_connection_unregister_object(ag_dbus_conn,
							hs_bluez_id);
		hs_bluez_id = 0;
	}

	if (app_id > 0) {
		g_dbus_connection_unregister_object(ag_dbus_conn,
							app_id);
		app_id = 0;
	}
}

static GDBusProxy *__bt_ag_gdbus_get_service_proxy(const gchar *service,
				const gchar *path, const gchar *interface)
{
	return (service_gproxy) ? service_gproxy :
			__bt_ag_gdbus_init_service_proxy(service,
					path, interface);
}

static void __bt_ag_agent_register(gchar *path, uint16_t profile_version,
				char *profile_uuid, const char* profile_name)
{
	FN_START;
	GDBusProxy *proxy;
	GVariant *ret;
	GError *error = NULL;
	GVariantBuilder *builder;

	proxy = __bt_ag_gdbus_get_service_proxy(BLUEZ_SERVICE_NAME,
		"/org/bluez", BLUEZ_PROFILE_MGMT_INTERFACE);
	if (proxy == NULL)
		return;


	builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	g_variant_builder_add(builder, "{sv}",
			"Name", g_variant_new("s",
			profile_name));
	g_variant_builder_add(builder, "{sv}",
			"Version", g_variant_new("q", profile_version));
	/*g_variant_builder_add(builder, "{sv}",
			"Role", g_variant_new("s","client"));*/
	if (g_strcmp0(path, BT_AG_AGENT_OBJECT_PATH) == 0) {
		g_variant_builder_add(builder, "{sv}",
				"features", g_variant_new("q", ag.sdp_features));
	}

	ret = g_dbus_proxy_call_sync(proxy, "RegisterProfile",
					g_variant_new("(osa{sv})", path,
							profile_uuid, builder),
					G_DBUS_CALL_FLAGS_NONE, -1,
					NULL, &error);
	g_variant_builder_unref(builder);
	/* set the name and role for the profile*/
	if (ret == NULL) {
		/* dBUS-RPC is failed */
		ERR("dBUS-RPC is failed");

		if (error != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
					error->code, error->message);

			g_clear_error(&error);
		}
		g_free(path);
		return;
	}
	g_variant_unref(ret);
	g_free(path);

	FN_END;
	return;
}

static void __bt_ag_agent_unregister(gchar *path)
{
	FN_START;
	GDBusProxy *proxy;
	GVariant *ret;
	GError *error = NULL;

	proxy = __bt_ag_gdbus_get_service_proxy(BLUEZ_SERVICE_NAME,
		"/org/bluez", BLUEZ_PROFILE_MGMT_INTERFACE);
	if (proxy == NULL)
		return;


	ret = g_dbus_proxy_call_sync(proxy, "UnregisterProfile",
					g_variant_new("(o)", path),
					G_DBUS_CALL_FLAGS_NONE, -1,
					NULL, &error);
	g_free(path);
	/* set the name and role for the profile*/
	if (ret == NULL) {
		/* dBUS-RPC is failed */
		ERR("dBUS-RPC is failed");

		if (error != NULL) {
			/* dBUS gives error cause */
			ERR("D-Bus API failure: errCode[%x], message[%s]",
					error->code, error->message);

			g_clear_error(&error);
		}
		return;
	}
	g_variant_unref(ret);

	if (local_addr) {
		g_free(local_addr);
		local_addr = NULL;
	}
	FN_END;
	return;
}

static void __bt_ag_agent_battery_status_cb(keynode_t *node)
{
	int batt = vconf_keynode_get_int(node);

	_bt_hfp_set_property_value("BatteryBarsChanged", batt);
}

static void __bt_ag_agent_network_signal_status_cb(keynode_t *node)
{
	int signal_bar = vconf_keynode_get_int(node);

	BT_CHECK_SIGNAL_STRENGTH(signal_bar);
	_bt_hfp_set_property_value("SignalBarsChanged", signal_bar);
}

#ifdef TIZEN_SUPPORT_LUNAR_DEVICE
static void __bt_ag_agent_lunar_connection_status_cb(keynode_t *node)
{
	gboolean status = vconf_keynode_get_bool(node);
	GSList *l;
	DBG("status = %d", status);

	if (status == TRUE) {
		for (l = active_devices; l; l = l->next) {
			bt_ag_info_t *data = l->data;
			_bt_ag_send_at(data, "\r\n+BSIR:1\r\n");
		}
	}
}
#endif

static void __bt_ag_agent_network_register_status_cb(keynode_t *node)
{
	int service = vconf_keynode_get_int(node);
	bt_hfp_agent_network_registration_status_t network_service;
	int roam_status;
	int ret;

	DBG("Current Signal Level = [%d] \n", service);

	switch (service) {
	case VCONFKEY_TELEPHONY_SVCTYPE_NONE:
	case VCONFKEY_TELEPHONY_SVCTYPE_NOSVC:
	case VCONFKEY_TELEPHONY_SVCTYPE_SEARCH:
		service = 0;
		break;
	default:
		service = 1;
		break;
	}

	ret = vconf_get_int(VCONFKEY_TELEPHONY_SVC_ROAM, &roam_status);
	if (ret != 0) {
		ERR("Get roaming status failed err = %d\n", ret);
		return;
	}

	if (roam_status == 0 && service == 1)
		network_service = BT_AGENT_NETWORK_REG_STATUS_HOME;
	else if (roam_status == 1 && service == 1)
		network_service = BT_AGENT_NETWORK_REG_STATUS_ROAMING;
	else
		network_service = BT_AGENT_NETWORK_REG_STATUS_UNKOWN;

	_bt_hfp_set_property_value("RegistrationChanged", network_service);
}

static void __bt_ag_agent_subscribe_vconf_updates(void)
{
	int ret;

	DBG("\n");

	ret = vconf_notify_key_changed(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
				(void *)__bt_ag_agent_battery_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to battery status failed err =  [%d]\n", ret);
	}

	ret = vconf_notify_key_changed(VCONFKEY_TELEPHONY_RSSI,
			(void *)__bt_ag_agent_network_signal_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to netowrk signal failed err =  [%d]\n", ret);
	}

	ret = vconf_notify_key_changed(VCONFKEY_TELEPHONY_SVCTYPE,
			(void *)__bt_ag_agent_network_register_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to network failed err =  [%d]\n", ret);
	}
#ifdef TIZEN_SUPPORT_LUNAR_DEVICE
	ret = vconf_notify_key_changed(VCONF_KEY_BT_LUNAR_ENABLED,
			(void *)__bt_ag_agent_lunar_connection_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to lunar connection failed err =  [%d]\n", ret);
	}
#endif
}

static void __bt_ag_agent_release_vconf_updates(void)
{
	int ret;

	DBG("\n");

	ret = vconf_ignore_key_changed(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
			(vconf_callback_fn)__bt_ag_agent_battery_status_cb);
	if (0 != ret) {
		ERR("vconf_ignore_key_changed failed\n");
	}

	ret = vconf_ignore_key_changed(VCONFKEY_TELEPHONY_RSSI,
		(vconf_callback_fn)__bt_ag_agent_network_signal_status_cb);
	if (0 != ret) {
		ERR("vconf_ignore_key_changed failed\n");
	}

	ret = vconf_ignore_key_changed(VCONFKEY_TELEPHONY_SVCTYPE,
		(vconf_callback_fn)__bt_ag_agent_network_register_status_cb);
	if (0 != ret) {
		ERR("vconf_ignore_key_changed failed\n");
	}
}

static gboolean __bt_ag_agent_send_subscriber_number_changed(
							const char *number)
{
	const char *property = g_strdup("SubscriberNumberChanged");

	FN_START;

	DBG("Number is %s", number);

	if (!_bt_hfp_set_property_name(property, number)) {
		DBG("Error- set property for subscriber no change  - ERROR\n");
		g_free((void *)property);
		return FALSE;
	}
	g_free((void *)property);
	FN_END;
	return TRUE;
}
static void __bt_ag_agent_sigterm_handler(int signo)
{
	int i;
	GSList *l;

	ERR_C("***** Signal handler came with signal %d *****", signo);

	for (l = active_devices ; l; l = l->next) {
		bt_ag_info_t *data = l->data;
		if (!__bt_ag_agent_connection_release(data))
			ERR("__bt_ag_agent_connection_release failed");
	}
	if (ag_dbus_conn)
		g_dbus_connection_flush(ag_dbus_conn, NULL, NULL, NULL);

	if (gmain_loop) {
		g_main_loop_quit(gmain_loop);
		DBG("Exiting");
		gmain_loop = NULL;
	} else {
		INFO("Terminating AG agent");
		exit(0);
	}

	if (signo == SIGTERM)
		return;

	for (i = 0; i < BT_AG_SIG_NUM; i++)
		sigaction(bt_ag_sig_to_handle[i], &(bt_ag_sigoldact[i]), NULL);

	raise(signo);
}

static void  __bt_ag_agent_tel_cb(TapiHandle *handle,
				int result,
				void *data,
				void *user_data)
{
	TelSimMsisdnList_t *number;
	gchar *subscriber_number;

	ERR("*********** result = %d", result);

	if (result == TAPI_API_SIM_LOCKED ||
		result == TAPI_API_SIM_NOT_INITIALIZED ||
		result == TAPI_API_SERVICE_NOT_READY) {
		DBG("initializing the tapi event for SIM status");
		__bt_ag_agent_reg_sim_event(handle, user_data);
		return;
	}

	if (data == NULL)
		return;

	number = (TelSimMsisdnList_t *)data;
	subscriber_number = g_strdup(number->list[0].num);
	__bt_ag_agent_send_subscriber_number_changed(subscriber_number);
	g_free(subscriber_number);
}

static void __bt_ag_agent_on_noti_sim_status (TapiHandle *handle,
		const char *noti_id, void *data, void *user_data)
{
	TelSimCardStatus_t *status = data;
	int tapi_result;

	DBG("event TAPI_NOTI_SIM_STATUS received!! status[%d]", *status);

	if (*status == TAPI_SIM_STATUS_SIM_INIT_COMPLETED) {
		__bt_ag_agent_dereg_sim_event(handle);
		tapi_result = tel_get_sim_msisdn(handle, __bt_ag_agent_tel_cb,
					user_data);
		if (tapi_result != TAPI_API_SUCCESS)
			ERR("Fail to get sim info: %d", tapi_result);
	}
}

static void __bt_ag_agent_reg_sim_event (TapiHandle *handle, void *user_data)
{
	int ret;
	ret = tel_register_noti_event(handle, TAPI_NOTI_SIM_STATUS,
		__bt_ag_agent_on_noti_sim_status, user_data);

	if (ret != TAPI_API_SUCCESS)
		ERR("event register failed(%d)", ret);
}

static void __bt_ag_agent_dereg_sim_event (TapiHandle *handle)
{
	int ret;
	ret = tel_deregister_noti_event(handle, TAPI_NOTI_SIM_STATUS);

	if (ret != TAPI_API_SUCCESS)
		ERR("event deregister failed(%d)", ret);
}

static void __bt_ag_name_owner_changed_cb(GDBusConnection *connection,
					const gchar *sender_name,
					const gchar *object_path,
					const gchar *interface_name,
					const gchar *signal_name,
					GVariant *parameters,
					gpointer user_data)
{
	FN_START;
	char *name_owner = NULL;
	char *old_owner = NULL;
	char *new_owner = NULL;

	if (strcasecmp(signal_name, "NameOwnerChanged") == 0) {
		GSList *l;

		g_variant_get(parameters, "(sss)", &name_owner, &old_owner, &new_owner);

		_bt_hfp_release_all_calls_by_sender(name_owner);

		if (sco_owner == NULL)
			return;

		if (strcasecmp(name_owner, sco_owner) == 0) {
			if (name_owner_sig_id != -1)
				g_dbus_connection_signal_unsubscribe(ag_dbus_conn,
							name_owner_sig_id);
			name_owner_sig_id = -1;
			g_free(sco_owner);
			sco_owner = NULL;

			for (l = active_devices ; l; l = l->next) {
				bt_ag_info_t *data = l->data;

				if (data->sco) {
					__bt_ag_close_sco(data);
					_bt_ag_set_headset_state(data,
						HEADSET_STATE_CONNECTED);
				}
			}
		}
	}
}

static void __bt_ag_agent_filter_cb(GDBusConnection *connection,
					const gchar *sender_name,
					const gchar *object_path,
					const gchar *interface_name,
					const gchar *signal_name,
					GVariant *parameters,
					gpointer user_data)
{
	FN_START;
	char *path = NULL;
	GVariant *optional_param;

	if (strcasecmp(signal_name, "InterfacesAdded") == 0) {

		g_variant_get(parameters, "(&o@a{sa{sv}})",
							&path, &optional_param);
		if (!path) {
			ERR("Invalid adapter path");
			return;
		}

		INFO("Adapter Path = [%s]", path);
		if (strcasecmp(path, DEFAULT_ADAPTER_OBJECT_PATH) == 0) {
			gchar *path = g_strdup(BT_AG_AGENT_OBJECT_PATH);
			__bt_ag_agent_register(path, hfp_ver,
				 HFP_AG_UUID, "Hands-Free Audio Gateway");

			path =  g_strdup(BT_HS_AG_AGENT_OBJECT_PATH);
			__bt_ag_agent_register(path, hsp_ver,
				HSP_AG_UUID, "Headset Audio Gateway");
		}
	} else if (strcasecmp(signal_name, "InterfacesRemoved") == 0) {
		g_variant_get(parameters, "(&o@as)", &path, &optional_param);
		if (!path) {
			ERR("Invalid adapter path");
			return;
		}

		INFO("Adapter Path = [%s]", path);
		if (strcasecmp(path, DEFAULT_ADAPTER_OBJECT_PATH) == 0) {
			gchar *path = g_strdup(BT_AG_AGENT_OBJECT_PATH);
			__bt_ag_agent_unregister(path);

			path =  g_strdup(BT_HS_AG_AGENT_OBJECT_PATH);
			__bt_ag_agent_unregister(path);
		}
	}

	FN_END;
}

static void __bt_ag_agent_dbus_deinit(void)
{

	if (service_gproxy) {
		g_object_unref(service_gproxy);
		service_gproxy = NULL;
	}

	if (app_gproxy) {
		g_object_unref(app_gproxy);
		app_gproxy = NULL;
	}

	if (ag_dbus_conn) {
		__bt_ag_unregister_profile_methods();

		if (owner_sig_id != -1)
			g_dbus_connection_signal_unsubscribe(ag_dbus_conn,
						owner_sig_id);

		if (name_owner_sig_id != -1)
			g_dbus_connection_signal_unsubscribe(ag_dbus_conn,
						name_owner_sig_id);
#ifdef TIZEN_MEDIA_ENHANCE
		if (media_sig_id != -1)
			g_dbus_connection_signal_unsubscribe(ag_dbus_conn,
						media_sig_id);

		if (media_state_sig_id != -1)
			g_dbus_connection_signal_unsubscribe(ag_dbus_conn,
						media_state_sig_id);
#endif
		name_owner_sig_id = -1;
		g_free(sco_owner);
		sco_owner = NULL;

		g_object_unref(ag_dbus_conn);
		ag_dbus_conn= NULL;
	}
	return;
}

static int __bt_ag_agent_get_adapter_path(GDBusConnection *conn, char *path)
{
	GError *err = NULL;
	GDBusProxy *manager_proxy = NULL;
	GVariant *result = NULL;
	char *adapter_path = NULL;

	if (conn == NULL)
		return BT_HFP_AGENT_ERROR_INTERNAL;

	manager_proxy =  g_dbus_proxy_new_sync(conn,
			G_DBUS_PROXY_FLAGS_NONE, NULL,
			BLUEZ_SERVICE_NAME,
			"/",
			BT_MANAGER_INTERFACE,
			NULL, &err);

	if (!manager_proxy) {
		ERR("Unable to create proxy: %s", err->message);
		goto fail;
	}

	result = g_dbus_proxy_call_sync(manager_proxy, "DefaultAdapter", NULL,
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
	if (!result) {
		if (err != NULL)
			ERR("Fail to get DefaultAdapter (Error: %s)", err->message);
		else
			ERR("Fail to get DefaultAdapter");

		goto fail;
	}

	if (g_strcmp0(g_variant_get_type_string(result), "(o)")) {
		ERR("Incorrect result\n");
		goto fail;
	}

	g_variant_get(result, "(&o)", &adapter_path);

	if (adapter_path == NULL ||
		strlen(adapter_path) >= BT_ADAPTER_OBJECT_PATH_MAX) {
		ERR("Adapter path is inproper\n");
		goto fail;
	}

	if (path)
		g_strlcpy(path, adapter_path, BT_ADAPTER_OBJECT_PATH_MAX);

	g_variant_unref(result);
	g_object_unref(manager_proxy);

	return 0;

fail:
	g_clear_error(&err);

	if (result)
		g_variant_unref(result);

	if (manager_proxy)
		g_object_unref(manager_proxy);

	return BT_HFP_AGENT_ERROR_INTERNAL;

}

#ifdef TIZEN_MEDIA_ENHANCE
void _bt_ag_agent_check_transport_state(void)
{
	FN_START;

	if (transport_state == MEDIA_TRANSPORT_STATE_PLAYING) {
		GDBusProxy *proxy;
		GVariant *ret;
		GError *err = NULL;

		proxy =  g_dbus_proxy_new_sync(ag_dbus_conn,
				G_DBUS_PROXY_FLAGS_NONE, NULL,
				"org.PulseAudio2", A2DP_SOURCE_ENDPOINT,
				BLUEZ_MEDIA_ENDPOINT_INTERFACE, NULL, &err);

		if (!proxy) {
			if (err) {
				ERR("Unable to create proxy: %s", err->message);
				g_clear_error(&err);
			}
			return;
		}
		INFO_C("SuspendMedia initiated");

		g_dbus_proxy_call(proxy,
					"SuspendMedia", NULL,
					G_DBUS_CALL_FLAGS_NONE, 2000,
					NULL, NULL, NULL);
		transport_state = MEDIA_TRANSPORT_STATE_IDLE;
	}

	FN_END;
}

static void __bt_ag_agent_transport_state_update(const char *value)
{

	if (!g_strcmp0(value, "idle"))
		transport_state = MEDIA_TRANSPORT_STATE_IDLE;
	else if (!g_strcmp0(value, "pending") || !g_strcmp0(value, "active"))
		transport_state = MEDIA_TRANSPORT_STATE_PLAYING;
	else
		transport_state =  MEDIA_TRANSPORT_STATE_DISCONNECTED;

	INFO_C("transport_state %d", transport_state);
}

static void __bt_ag_agent_media_filter_cb(GDBusConnection *connection,
					const gchar *sender_name,
					const gchar *object_path,
					const gchar *interface_name,
					const gchar *signal_name,
					GVariant *parameters,
					gpointer user_data)
{
	FN_START;
	char *inter = NULL;
	GVariant *dict_param = NULL;
	GVariant *optional_param = NULL;

	if (strcasecmp(signal_name, "PropertiesChanged") == 0) {
		if (g_strcmp0(g_variant_get_type_string(parameters),
							"(sa{sv}as)")) {
			ERR("Incorrect parameters\n");
			return;
		}

		g_variant_get(parameters, "(&s@a{sv}@as)",
					&inter, &dict_param, &optional_param);
		if (dict_param && (!g_strcmp0(inter,
				BLUEZ_MEDIA_TRANSPORT_INTERFACE))){
			GVariantIter *iter = NULL;
			const gchar *key = NULL;
			GVariant *value_var = NULL;
			gchar *value = NULL;
			g_variant_get(dict_param, "a{sv}", &iter);
			while (g_variant_iter_loop(
					iter, "{sv}", &key, &value_var)) {
				DBG("key %s", key);
				if (g_strcmp0(key, "State") == 0) {
					value = g_variant_get_string(
								value_var,
								NULL);
					DBG("value %s", value);
					__bt_ag_agent_transport_state_update(value);
					break;
				}
			}
			g_variant_iter_free(iter);
			g_variant_unref(dict_param);
		}
	} else if (strcasecmp(signal_name, "ProfileStateChanged") == 0) {
		char *profile_uuid = NULL;
		int state = 0;

		g_variant_get(parameters, "(&si)", &profile_uuid, &state);
		if ((g_strcmp0(profile_uuid, A2DP_SINK_UUID) == 0)  &&
			(state == BT_PROFILE_STATE_DISCONNECTED)) {
			DBG("Updating the transport state");
			__bt_ag_agent_transport_state_update("Disconnect");
		}
	}

	FN_END;
}
#endif
static void __bt_ag_agent_dbus_init(void)
{
	FN_START;

	if (__bt_ag_get_gdbus_connection() == NULL) {
		ERR("Error in creating the gdbus connection\n");
		return;
	}
	if (!__bt_ag_register_profile_methods()) {
		ERR("Error in HFP / HSP register_profile_methods\n");
		return;
	}

	if(!__bt_ag_agent_get_adapter_path(ag_dbus_conn , NULL)) {

		gchar *path = g_strdup(BT_AG_AGENT_OBJECT_PATH);
		__bt_ag_agent_register(path, hfp_ver,
			 HFP_AG_UUID, "Hands-Free Audio Gateway");

		path = g_strdup(BT_HS_AG_AGENT_OBJECT_PATH);
		__bt_ag_agent_register(path, hsp_ver,
			HSP_AG_UUID, "Headset Audio Gateway");
	}

	owner_sig_id = g_dbus_connection_signal_subscribe(ag_dbus_conn,
				NULL, BT_MANAGER_INTERFACE, NULL, NULL, NULL, 0,
				__bt_ag_agent_filter_cb, NULL, NULL);
#ifdef TIZEN_MEDIA_ENHANCE
	media_sig_id = g_dbus_connection_signal_subscribe(ag_dbus_conn,
				NULL, BT_PROPERTIES_INTERFACE, NULL, NULL,
				NULL, 0, __bt_ag_agent_media_filter_cb,
				NULL, NULL);

	media_state_sig_id = g_dbus_connection_signal_subscribe(ag_dbus_conn,
				NULL, BLUEZ_DEVICE_INTERFACE, NULL, NULL,
				NULL, 0, __bt_ag_agent_media_filter_cb,
				NULL, NULL);

	transport_state = MEDIA_TRANSPORT_STATE_DISCONNECTED;
#endif
	FN_END;
	return;
}

static uint32_t __bt_ag_agent_get_ag_features(void)
{

	uint32_t ag_features = BT_AG_FEATURE_EC_AND_NR |
				BT_AG_FEATURE_REJECT_CALL |
				BT_AG_FEATURE_ENHANCED_CALL_STATUS |
				BT_AG_FEATURE_THREE_WAY_CALL |
#ifndef TIZEN_KIRAN
				BT_AG_FEATURE_VOICE_RECOGNITION |
#endif
				BT_AG_FEATURE_EXTENDED_ERROR_RESULT_CODES;

	wbs_en = TRUE;
#if defined(TIZEN_BT_HFP_AG_ENABLE)
	hfp_ver = HFP_VERSION_1_6;
#else
	hfp_ver = HFP_VERSION_1_5;
#endif
	hsp_ver = HSP_VERSION_1_2;

	if (hfp_ver == HFP_VERSION_1_6)
		ag_features |= BT_AG_FEATURE_CODEC_NEGOTIATION;
	return ag_features;
}

void *__bt_ag_agent_telephony_init(void *arg) {

	int tapi_result;
	uint32_t ag_features = *((uint32_t *)arg);

	INFO_C("Initializing the telephony info");

	_bt_hfp_initialize_telephony_manager(ag_features);
	__bt_ag_agent_subscribe_vconf_updates();

	tapi_handle = tel_init(NULL);
	tapi_result = tel_get_sim_msisdn(tapi_handle, __bt_ag_agent_tel_cb,
					NULL);
	if (tapi_result != TAPI_API_SUCCESS)
		ERR("Fail to get sim info: %d", tapi_result);
	return NULL;
}

int main(void)
{
	int i;
	uint32_t ag_features;
	struct sigaction sa;
	pthread_t thread_id;

	INFO_C("Starting Bluetooth AG agent");

	g_type_init();

	ag_features = __bt_ag_agent_get_ag_features();

	ag.sdp_features = (uint16_t) ag_features & 0x1F;

	if (hfp_ver == HFP_VERSION_1_6 && wbs_en == TRUE)
		ag.sdp_features |= BT_AG_FEATURE_SDP_WIDEBAND_SPEECH;

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_NOCLDSTOP;
	sa.sa_handler = __bt_ag_agent_sigterm_handler;

	for (i = 0; i < BT_AG_SIG_NUM; i++)
		sigaction(bt_ag_sig_to_handle[i], &sa, &(bt_ag_sigoldact[i]));

	gmain_loop = g_main_loop_new(NULL, FALSE);

	if (gmain_loop == NULL) {
		ERR("GMainLoop create failed");
		return EXIT_FAILURE;
	}

	__bt_ag_agent_dbus_init();
	if (pthread_create(&thread_id, NULL,
			(void *)&__bt_ag_agent_telephony_init,
					&ag_features) < 0) {
		ERR("pthread_create() is failed");
		return EXIT_FAILURE;
	}

	if (pthread_detach(thread_id) < 0) {
		ERR("pthread_detach() is failed");
	}

	g_main_loop_run(gmain_loop);

	DBG("Cleaning up");

	tel_deinit(tapi_handle);

	__bt_ag_agent_dbus_deinit();
	_bt_hfp_deinitialize_telephony_manager();
	__bt_ag_agent_release_vconf_updates();

	if (remote_dev_path)
		g_free(remote_dev_path);

	if (gmain_loop)
		g_main_loop_unref(gmain_loop);

	INFO_C("Terminating Bluetooth AG agent");
	return 0;
}
