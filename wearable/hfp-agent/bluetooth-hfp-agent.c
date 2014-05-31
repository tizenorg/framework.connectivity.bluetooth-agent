/*
 * Bluetooth-hfp-agent
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:	Hocheol Seo <hocheol.seo@samsung.com>
 *		Girishashok Joshi <girish.joshi@samsung.com>
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
#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <signal.h>

#include <TapiUtility.h>
#include <ITapiSim.h>
#include <app.h>
#include <aul.h>

#include "vconf.h"
#include "vconf-keys.h"
#include "contacts.h"
#include "appsvc.h"

#include "bluetooth-hfp-agent.h"

static GMainLoop *gmain_loop = NULL;
static DBusGConnection *g_connection = NULL;
static DBusConnection *gconn = NULL;
static gboolean nrec_status = FALSE;

#define BT_ERROR_INTERNAL "InternalError"
#define BT_ERROR_NOT_AVAILABLE "NotAvailable"
#define BT_ERROR_NOT_CONNECTED "NotConnected"
#define BT_ERROR_BUSY "InProgress"
#define BT_ERROR_INVALID_PARAM "InvalidArguments"
#define BT_ERROR_ALREADY_EXSIST "AlreadyExists"
#define BT_ERROR_ALREADY_CONNECTED "Already Connected"
#define BT_ERROR_NO_MEMORY "No memory"
#define BT_ERROR_I_O_ERROR "I/O error"
#define BT_ERROR_OPERATION_NOT_AVAILABLE "Operation currently not available"
#define BT_ERROR_BATTERY "Battery error "
#define BT_ERROR_SIGNAL "Signal error"
#define BT_ERROR_NO_CALL_LOG "No Call log"
#define BT_ERROR_INVLAID_DTMF "Invalid dtmf"

#define BLUEZ_SERVICE_NAME "org.bluez"
#define TELEPHONY_CSD_INTERFACE "org.tizen.telephony.csd"
#define TELEPHONY_CSD_OBJECT_PATH "/org/tizen/csd"
#define TELEPHONY_APP_INTERFACE "org.tizen.csd.Call.Instance"

#define BT_HFP_AGENT_SET_PROPERTY "SetProperty"

#define BT_HFP_AGENT_OBJECT "/org/bluez/agent/hfp_agent"
#define BT_HFP_AGENT_INTERFACE "org.bluez.hfp_agent"

#define BT_SIGNAL_ARRAY_MAX 2

/* AT+CSQ : Returns received signal strength indication.
     Command response: +CSQ: <rssi>,<ber>
    <ber> is not supported and has a constant value of 99, included for compatibility reasons.
*/
#define BT_SIGNAL_QUALITY_BER 99

/*Length of the string used to send telephone number to app-svc
   format: tel:<number>
*/
#define BT_MAX_TEL_NUM_STRING 20

/**
 * @brief Outgoing call type status
 *
 * 0 : Follow last call log \n
 * 1 : Voice call \n
 * 2 : Video call \n
 */
#define BT_FOLLOW_CALL_LOG 0
#define BT_VOICE_CALL 1
#define BT_VIDEO_CALL 2

/**
 * @brief The status of making outgoing calls with BT headsets
 *
 * 0 : Even when device locked \n
 * 1 : Only when device unlocked \n
 */
#define BT_MO_EVEN_LOCKED 0
#define BT_MO_ONLY_UNLOCKED 1

#define BT_CHECK_SIGNAL_STRENGTH(rssi) \
	if (rssi >= VCONFKEY_TELEPHONY_RSSI_4) \
		rssi = VCONFKEY_TELEPHONY_RSSI_5

typedef struct {
	GObject parent;
} BtHfpAgent;

typedef struct {
	GObjectClass parent;
} BtHfpAgentClass;

GType bt_hfp_agent_get_type(void);

#define BT_HFP_TYPE_AGENT (bt_hfp_agent_get_type())

#define BT_HFP_AGENT(object)(G_TYPE_CHECK_INSTANCE_CAST((object), \
			BT_HFP_TYPE_AGENT , BtHfpAgent))

#define BT_HFP_AGENT_CLASS(klass)(G_TYPE_CHECK_CLASS_CAST((klass), \
			BT_HFP_TYPE_AGENT , BtHfpAgentClass))

#define BT_IS_HFP_AGENT(object)(G_TYPE_CHECK_INSTANCE_TYPE((object), \
			BT_HFP_TYPE_AGENT))

#define BT_IS_HFP_AGENT_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), \
			BT_HFP_TYPE_AGENT))

#define BT_HFP_AGENT_GET_CLASS(obj)(G_TYPE_INSTANCE_GET_CLASS((obj), \
			BT_HFP_TYPE_AGENT , BtHfpAgentClass))

G_DEFINE_TYPE(BtHfpAgent, bt_hfp_agent, G_TYPE_OBJECT)

static gboolean bt_hfp_agent_register_application(BtHfpAgent *agent,
				const gchar *path, DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_unregister_application(BtHfpAgent *agent,
				const gchar *path, DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_incoming_call(BtHfpAgent *agent, const gchar *path,
				const gchar *number, gint call_id,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_outgoing_call(BtHfpAgent *agent, const gchar *path,
				const gchar *number, gint call_id,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_change_call_status(BtHfpAgent *agent,
				const gchar *path, gint status, gint call_id,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_answer_call(BtHfpAgent *agent, unsigned int call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_release_call(BtHfpAgent *agent, unsigned int call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_reject_call(BtHfpAgent *agent, unsigned int call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_threeway_call(BtHfpAgent *agent, gint call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_dial_last_num(BtHfpAgent *agent,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_dial_num(BtHfpAgent *agent,
				const gchar *number, guint flags,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_dial_memory(BtHfpAgent *agent, gint location,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_send_dtmf(BtHfpAgent *agent, const gchar *dtmf,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_voice_dial(BtHfpAgent *agent, gboolean activate,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_get_battery_status(BtHfpAgent *object,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_get_signal_quality(BtHfpAgent *object,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_get_properties(BtHfpAgent *agent,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_get_operator_name(BtHfpAgent *object,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_nrec_status(BtHfpAgent *agent, gboolean status,
				DBusGMethodInvocation *context);

static gboolean bt_hfp_agent_connection_status(BtHfpAgent *agent, gboolean status,
				DBusGMethodInvocation *context);

#include "bluetooth_hfp_agent_glue.h"

static void bt_hfp_agent_init(BtHfpAgent *obj)
{
	DBG("\n");

	g_assert(obj != NULL);
}

static void bt_hfp_agent_finalize(GObject *obj)
{
	DBG("\n");

	G_OBJECT_CLASS(bt_hfp_agent_parent_class)->finalize(obj);
}

static void bt_hfp_agent_class_init(BtHfpAgentClass *klass)
{
	DBG("\n");

	GObjectClass *object_class = (GObjectClass *)klass;

	g_assert(klass != NULL);

	object_class->finalize = bt_hfp_agent_finalize;

	dbus_g_object_type_install_info(BT_HFP_TYPE_AGENT,
					&dbus_glib_bt_hfp_agent_object_info);
}

static GQuark __bt_hfp_agent_error_quark(void)
{
	DBG("\n");

	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string("agent");

	return quark;
}

static GError *__bt_hfp_agent_set_error(bt_hfp_agent_error_t error)
{
	ERR("error[%d]\n", error);

	switch (error) {
	case BT_HFP_AGENT_ERROR_NOT_AVAILABLE:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_NOT_AVAILABLE);
	case BT_HFP_AGENT_ERROR_NOT_CONNECTED:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_NOT_CONNECTED);
	case BT_HFP_AGENT_ERROR_BUSY:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_BUSY);
	case BT_HFP_AGENT_ERROR_INVALID_PARAM:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_INVALID_PARAM);
	case BT_HFP_AGENT_ERROR_ALREADY_EXSIST:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_ALREADY_EXSIST);
	case BT_HFP_AGENT_ERROR_ALREADY_CONNECTED:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_ALREADY_CONNECTED);
	case BT_HFP_AGENT_ERROR_NO_MEMORY:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_NO_MEMORY);
	case BT_HFP_AGENT_ERROR_I_O_ERROR:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_I_O_ERROR);
	case BT_HFP_AGENT_ERROR_OPERATION_NOT_AVAILABLE:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
					BT_ERROR_OPERATION_NOT_AVAILABLE);
	case BT_HFP_AGENT_ERROR_BATTERY_STATUS:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
					BT_ERROR_BATTERY);
	case BT_HFP_AGENT_ERROR_SIGNAL_STATUS:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
					BT_ERROR_SIGNAL);
	case BT_HFP_AGENT_ERROR_NO_CALL_LOGS:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
					BT_ERROR_NO_CALL_LOG);
	case BT_HFP_AGENT_ERROR_INTERNAL:
	default:
		return g_error_new(BT_HFP_AGENT_ERROR, error,
						BT_ERROR_INTERNAL);
	}
}

static int __bt_hfp_agent_get_error(const char *error_message)
{
	if (error_message == NULL) {
		DBG("Error message NULL\n");
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	ERR("Error message = %s \n", error_message);

	if (g_strcmp0(error_message, BT_ERROR_NOT_AVAILABLE) == 0)
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
	else if (g_strcmp0(error_message, BT_ERROR_NOT_CONNECTED) == 0)
		return BT_HFP_AGENT_ERROR_NOT_CONNECTED;
	else if (g_strcmp0(error_message, BT_ERROR_BUSY) == 0)
		return BT_HFP_AGENT_ERROR_BUSY;
	else if (g_strcmp0(error_message, BT_ERROR_INVALID_PARAM) == 0)
		return BT_HFP_AGENT_ERROR_INVALID_PARAM;
	else if (g_strcmp0(error_message, BT_ERROR_ALREADY_EXSIST) == 0)
		return BT_HFP_AGENT_ERROR_ALREADY_EXSIST;
	else if (g_strcmp0(error_message, BT_ERROR_ALREADY_CONNECTED) == 0)
		return BT_HFP_AGENT_ERROR_ALREADY_CONNECTED;
	else if (g_strcmp0(error_message, BT_ERROR_NO_MEMORY) == 0)
		return BT_HFP_AGENT_ERROR_NO_MEMORY;
	else if (g_strcmp0(error_message, BT_ERROR_I_O_ERROR) == 0)
		return BT_HFP_AGENT_ERROR_I_O_ERROR;
	else if (g_strcmp0(error_message, BT_ERROR_OPERATION_NOT_AVAILABLE) == 0)
		return BT_HFP_AGENT_ERROR_OPERATION_NOT_AVAILABLE;
	else if (g_strcmp0(error_message, BT_ERROR_INVLAID_DTMF) == 0)
		return BT_HFP_AGENT_ERROR_INVALID_DTMF;
	else
		return BT_HFP_AGENT_ERROR_INTERNAL;
}

static int __bt_hfp_agent_dbus_method_send(const char *service,
				const char *path, const char *interface,
				const char *method, gboolean response,
				int type, ...)
{
	DBusMessage *msg;
	DBusMessage *reply;
	DBusError err;
	va_list args;
	int error;

	msg = dbus_message_new_method_call(service, path, interface,
								method);
	if (!msg) {
		ERR("Unable to allocate new D-Bus %s message \n", method);
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	va_start(args, type);

	if (!dbus_message_append_args_valist(msg, type, args)) {
		dbus_message_unref(msg);
		va_end(args);
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	va_end(args);

	dbus_error_init(&err);

	if (response) {
		reply = dbus_connection_send_with_reply_and_block(gconn,
							msg, -1, &err);
		dbus_message_unref(msg);

		if (!reply) {
			ERR("Error returned in method call\n");
			if (dbus_error_is_set(&err)) {
				error = __bt_hfp_agent_get_error(err.message);
				dbus_error_free(&err);
				return error;
			} else {
				ERR("Error is not set\n");
				return BT_HFP_AGENT_ERROR_INTERNAL;
			}
		}
		dbus_message_unref(reply);
	} else {
		dbus_connection_send(gconn, msg, NULL);
		dbus_message_unref(msg);
	}

	return BT_HFP_AGENT_ERROR_NONE;
}

/**
 * [Org.Hfp.App.Interface] interface functions
 */
static gboolean bt_hfp_agent_register_application(BtHfpAgent *agent,
				const gchar *path, DBusGMethodInvocation *context)
{
	gboolean flag = TRUE;
	char *sender;
	GError *error;
	int ret;

	if (path == NULL) {
		ERR("Invalid Argument path\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s\n", path);

	sender = dbus_g_method_get_sender(context);

	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(BLUEZ_SERVICE_NAME,
				TELEPHONY_CSD_OBJECT_PATH,
				TELEPHONY_CSD_INTERFACE,
				"RegisterTelephonyAgent", TRUE,
				DBUS_TYPE_BOOLEAN, &flag,
				DBUS_TYPE_STRING, &path,
				DBUS_TYPE_STRING, &sender, DBUS_TYPE_INVALID);
	g_free(sender);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_unregister_application(BtHfpAgent *agent,
				const gchar *path, DBusGMethodInvocation *context)
{
	gboolean flag = FALSE;
	char *sender;
	GError *error;
	int ret;

	if (path == NULL) {
		ERR("Invalid Argument path\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s \n", path);

	sender = dbus_g_method_get_sender(context);

	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(BLUEZ_SERVICE_NAME,
				TELEPHONY_CSD_OBJECT_PATH,
				TELEPHONY_CSD_INTERFACE,
				"RegisterTelephonyAgent", TRUE,
				DBUS_TYPE_BOOLEAN, &flag,
				DBUS_TYPE_STRING, &path,
				DBUS_TYPE_STRING, &sender, DBUS_TYPE_INVALID);
	g_free(sender);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_incoming_call(BtHfpAgent *agent, const gchar *path,
				const gchar *number, gint call_id,
				DBusGMethodInvocation *context)
{
	GError *error;
	char *sender;
	int ret;

	if (path == NULL || number == NULL) {
		ERR("Invalid Arguments\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s\n", path);
	DBG("Call id = %d\n", call_id);
	DBG_SECURE("Phone number = %s\n", number);

	sender = dbus_g_method_get_sender(context);

	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(BLUEZ_SERVICE_NAME,
				TELEPHONY_CSD_OBJECT_PATH,
				TELEPHONY_CSD_INTERFACE,
				"Incoming", TRUE,
				DBUS_TYPE_STRING, &path,
				DBUS_TYPE_STRING, &number,
				DBUS_TYPE_UINT32, &call_id,
				DBUS_TYPE_STRING, &sender,
				DBUS_TYPE_INVALID);
	g_free(sender);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_outgoing_call(BtHfpAgent *agent, const gchar *path,
				const gchar *number, gint call_id,
				DBusGMethodInvocation *context)
{
	GError *error;
	char *sender;
	int ret;

	if (path == NULL || number == NULL) {
		ERR("Invalid Arguments\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s\n", path);
	DBG("Call id = %d\n", call_id);
	DBG_SECURE("Phone number = %s\n", number);

	sender = dbus_g_method_get_sender(context);

	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(BLUEZ_SERVICE_NAME,
				TELEPHONY_CSD_OBJECT_PATH,
				TELEPHONY_CSD_INTERFACE,
				"Outgoing", TRUE,
				DBUS_TYPE_STRING, &path,
				DBUS_TYPE_STRING, &number,
				DBUS_TYPE_UINT32, &call_id,
				DBUS_TYPE_STRING, &sender,
				DBUS_TYPE_INVALID);
	g_free(sender);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_change_call_status(BtHfpAgent *agent,
				const gchar *path, gint status, gint call_id,
				DBusGMethodInvocation *context)
{
	GError *error;
	char *sender;
	int ret;

	if (path == NULL) {
		ERR("Invalid Argument path\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s\n", path);
	DBG("Call id[%d] :: Status[%d]\n", call_id, status);

	sender = dbus_g_method_get_sender(context);

	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(BLUEZ_SERVICE_NAME,
				TELEPHONY_CSD_OBJECT_PATH,
				TELEPHONY_CSD_INTERFACE,
				"SetCallStatus", TRUE,
				DBUS_TYPE_STRING, &path,
				DBUS_TYPE_UINT32, &status,
				DBUS_TYPE_UINT32, &call_id,
				DBUS_TYPE_STRING, &sender,
				DBUS_TYPE_INVALID);
	g_free(sender);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

void __bt_append_entry(DBusMessageIter *iter,
			const char *key, int type, void *val)
{
	DBusMessageIter entry;
	DBusMessageIter value;
	const char *str;
	char signal[BT_SIGNAL_ARRAY_MAX] = { type, '\0' };

	if (type == DBUS_TYPE_STRING) {
		str = *((const char **) val);
		if (str == NULL) {
			return;
		}
	}

	dbus_message_iter_open_container(iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
						signal, &value);
	dbus_message_iter_append_basic(&value, type, val);
	dbus_message_iter_close_container(&entry, &value);
	dbus_message_iter_close_container(iter, &entry);
}

static gboolean bt_hfp_agent_get_properties(BtHfpAgent *agent,
				DBusGMethodInvocation *context)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter dict;
	GError *error;

	reply = dbus_g_method_get_reply(context);
	if (!reply) {
		ERR("Internal error\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INTERNAL);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	DBG("NREC status [%d]\n", nrec_status);
	__bt_append_entry(&dict, "nrec", DBUS_TYPE_BOOLEAN, &nrec_status);
	dbus_message_iter_close_container(&iter, &dict);
	dbus_g_method_send_reply(context, reply);

	return TRUE;
}


/**
 * [Org.Hfp.Bluez.Interface] interface functions
 */
static gboolean bt_hfp_agent_answer_call(BtHfpAgent *agent, unsigned int call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context)
{
	int ret;
	GError *error;

	if (path == NULL || sender == NULL) {
		ERR("Invalid Arguments\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s \n", path);
	DBG("Call Id = %d", call_id);
	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"Answer", FALSE,
				DBUS_TYPE_UINT32, &call_id,
				DBUS_TYPE_INVALID);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_release_call(BtHfpAgent *agent, unsigned int call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context)
{
	int ret;
	GError *error;

	if (path == NULL || sender == NULL) {
		ERR("Invalid Arguments\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s \n", path);
	DBG("Call Id = %d", call_id);
	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"Release", FALSE,
				DBUS_TYPE_UINT32, &call_id,
				DBUS_TYPE_INVALID);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_reject_call(BtHfpAgent *agent, unsigned int call_id,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context)
{
	int ret;
	GError *error;

	if (path == NULL || sender == NULL) {
		ERR("Invalid Arguments\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s \n", path);
	DBG("Call Id = %d", call_id);
	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"Reject", FALSE,
				DBUS_TYPE_UINT32, &call_id,
				DBUS_TYPE_INVALID);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_threeway_call(BtHfpAgent *agent, gint value,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context)
{
	int ret;
	GError *error;

	if (path == NULL || sender == NULL) {
		ERR("Invalid Arguments\n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG("Application path = %s \n", path);
	DBG("Value = %d", value);
	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"Threeway", TRUE,
				DBUS_TYPE_UINT32, &value,
				DBUS_TYPE_INVALID);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	dbus_g_method_return(context);
	return TRUE;
}

static gboolean __bt_is_phone_locked(int *phone_lock_state)
{
	int ret;

	if (NULL == phone_lock_state)
		return FALSE;

	ret = vconf_get_int(VCONFKEY_IDLE_LOCK_STATE, phone_lock_state);
	if (ret != 0) {
		ERR("Failed to read  [%s]\n", VCONFKEY_IDLE_LOCK_STATE);
		return FALSE;
	}

	return TRUE;
}

static gboolean __bt_get_outgoing_callapp_type(int *callapp_type)
{
	int ret;

	if (NULL == callapp_type)
		return FALSE;

	ret = vconf_get_int(VCONFKEY_CISSAPPL_OUTGOING_CALL_TYPE_INT, callapp_type);
	if (ret != 0) {
		ERR("Failed to read  [%s]\n", VCONFKEY_CISSAPPL_OUTGOING_CALL_TYPE_INT);
		return FALSE;
	}

	DBG(" [%s] = [%d]\n", VCONFKEY_CISSAPPL_OUTGOING_CALL_TYPE_INT, *callapp_type);

	return TRUE;
}

static gboolean __bt_get_outgoing_call_condition(int *condition)
{
	int ret;

	if (NULL == condition)
		return FALSE;

	ret = vconf_get_int(VCONFKEY_CISSAPPL_OUTGOING_CALL_CONDITIONS_INT, condition);
	if (ret != 0) {
		ERR("Failed to read  [%s]\n", VCONFKEY_CISSAPPL_OUTGOING_CALL_CONDITIONS_INT);
		return FALSE;
	}

	return TRUE;
}

static gboolean __bt_hfp_agent_make_call(const char *number)
{
	bundle *b;
	char telnum[BT_MAX_TEL_NUM_STRING];

	b = bundle_create();
	if (NULL == b)
		return FALSE;

	appsvc_set_operation(b, APPSVC_OPERATION_CALL);
	snprintf(telnum, sizeof(telnum), "tel:%s", number);
	appsvc_set_uri(b, telnum);
	appsvc_add_data(b, "ctindex", "-1");
	appsvc_run_service(b, 0, NULL, NULL);
	bundle_free(b);

	return TRUE;
}

static gboolean __bt_hfp_agent_make_video_call(const char *mo_number)
{
	bundle *kb;

	kb = bundle_create();
	if (NULL == kb)
		return FALSE;

	bundle_add(kb, "KEY_CALL_TYPE", "MO");
	bundle_add(kb, "number", mo_number);
	aul_launch_app("org.tizen.vtmain", kb);
	bundle_free(kb);

	return TRUE;
}

static gboolean bt_hfp_agent_dial_last_num(BtHfpAgent *agent,
				DBusGMethodInvocation *context)
{
	GError *error;
	int error_code = BT_HFP_AGENT_ERROR_NONE;
	char *last_number = NULL;
	int log_type;
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

	DBG("\n");

	if (contacts_connect2() != CONTACTS_ERROR_NONE) {
		ERR(" contacts_connect2 failed \n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INTERNAL);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	contacts_filter_create(_contacts_phone_log._uri, &filter);

	if (filter == NULL)
		goto done;

	if (contacts_filter_add_int(filter, _contacts_phone_log.log_type,
				CONTACTS_MATCH_EQUAL,
				CONTACTS_PLOG_TYPE_VOICE_OUTGOING) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_filter_add_int failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_filter_add_operator(filter, CONTACTS_FILTER_OPERATOR_OR) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_filter_add_operator failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_filter_add_int(filter, _contacts_phone_log.log_type,
				CONTACTS_MATCH_EQUAL,
				CONTACTS_PLOG_TYPE_VIDEO_OUTGOING) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_filter_add_int failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	contacts_query_create(_contacts_phone_log._uri, &query);

	if (query == NULL)
		goto done;

	contacts_query_set_filter(query, filter);

	if (contacts_query_set_projection(query, projections,
				sizeof(projections)/sizeof(unsigned int)) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_query_set_projection failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_query_set_sort(query, _contacts_phone_log.log_time, false) !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_query_set_sort failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_db_get_records_with_query(query, 0, 1, &list)  !=
				CONTACTS_ERROR_NONE) {
		ERR(" contacts_db_get_records_with_query failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_list_first(list)  != CONTACTS_ERROR_NONE) {
		ERR(" contacts_list_first failed \n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (contacts_list_get_current_record_p(list, &record)  !=
				CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (record == NULL)
		goto done;

	if (contacts_record_get_str(record, _contacts_phone_log.address,
				&last_number)!= CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (last_number == NULL) {
		ERR("No last number \n");
		error_code = BT_HFP_AGENT_ERROR_NO_CALL_LOGS;
		goto done;
	}

	if (!__bt_is_phone_locked(&phone_lock_state)) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (!__bt_get_outgoing_callapp_type(&callapp_type)) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (!__bt_get_outgoing_call_condition(&condition)) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (condition == BT_MO_ONLY_UNLOCKED &&
			phone_lock_state == VCONFKEY_IDLE_LOCK) {
		ERR("Call failed. Phone is locked.\n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	switch (callapp_type) {
		case BT_VOICE_CALL:
			if (!__bt_hfp_agent_make_call(last_number)) {
				ERR("Problem launching application \n");
				error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			}
			break;
		case BT_VIDEO_CALL:
			if(!__bt_hfp_agent_make_video_call(last_number)) {
				ERR("Problem launching application \n");
				error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			}
			break;
		case BT_FOLLOW_CALL_LOG:
			if(contacts_record_get_int(record,
				_contacts_phone_log.log_type,
				&log_type) != CONTACTS_ERROR_NONE) {
				error_code = BT_HFP_AGENT_ERROR_INTERNAL;
				break;
			}
			if (log_type == CONTACTS_PLOG_TYPE_VOICE_OUTGOING) {
				if (!__bt_hfp_agent_make_call(last_number)) {
					ERR("Problem launching application \n");
					error_code = BT_HFP_AGENT_ERROR_INTERNAL;
				}
			}
			else if(log_type == CONTACTS_PLOG_TYPE_VIDEO_OUTGOING) {
				if(!__bt_hfp_agent_make_video_call(last_number)) {
					ERR("Problem launching application \n");
					error_code = BT_HFP_AGENT_ERROR_INTERNAL;
				}
			} else {
				error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			}
			break;
		default:
			error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			break;
	}

done:
	if (list != NULL)
		contacts_list_destroy(list, TRUE);

	if (filter != NULL)
		contacts_filter_destroy(filter);

	if (query != NULL)
		contacts_query_destroy(query);

	contacts_disconnect2();

	if (last_number != NULL)
		g_free(last_number);

	if (error_code == BT_HFP_AGENT_ERROR_NONE) {
		dbus_g_method_return(context);
		return TRUE;
	} else {
		error = __bt_hfp_agent_set_error(error_code);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}
}

static gboolean bt_hfp_agent_dial_num(BtHfpAgent *agent,
				const gchar *number, guint flags,
				DBusGMethodInvocation *context)
{
	GError *error;
	int error_code;
	int callapp_type;
	int phone_lock_state;
	int condition;

	if (number == NULL) {
		ERR("Invalid Argument\n");
		error_code = BT_HFP_AGENT_ERROR_INVALID_PARAM;
		goto fail;
	}

	DBG_SECURE("Number = %s \n", number);
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

	if (condition == BT_MO_ONLY_UNLOCKED &&
			phone_lock_state == VCONFKEY_IDLE_LOCK) {
		ERR("Call failed. Phone is locked.\n");
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto fail;
	}

	if (callapp_type == BT_VIDEO_CALL) {
		if(!__bt_hfp_agent_make_video_call(number)) {
			ERR("Problem launching application \n");
			error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}
	} else {
		if (!__bt_hfp_agent_make_call(number)) {
			ERR("Problem launching application \n");
			error_code = BT_HFP_AGENT_ERROR_INTERNAL;
			goto fail;
		}
	}

	dbus_g_method_return(context);
	return TRUE;
fail:
	error = __bt_hfp_agent_set_error(error_code);
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	return FALSE;
}

static gboolean bt_hfp_agent_dial_memory(BtHfpAgent *agent, gint location,
				DBusGMethodInvocation *context)
{
	GError *error;
	int error_code = BT_HFP_AGENT_ERROR_NONE;
	char *number = NULL;
	contacts_filter_h filter = NULL;
	contacts_query_h query = NULL;
	contacts_list_h list = NULL;
	contacts_record_h record = NULL;
	unsigned int projections[] = {
		_contacts_speeddial.number,
	};

	DBG("location = %d \n", location);

	/*Get number from contacts location*/
	if (contacts_connect2() != CONTACTS_ERROR_NONE) {
		ERR(" contacts_connect2 failed \n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INTERNAL);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	contacts_filter_create(_contacts_speeddial._uri, &filter);

	if (filter == NULL)
		goto done;

	if (contacts_filter_add_int(filter, _contacts_speeddial.speeddial_number,
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

	if (contacts_record_get_str(record, _contacts_speeddial.number,
							&number)!= CONTACTS_ERROR_NONE) {
		error_code = BT_HFP_AGENT_ERROR_INTERNAL;
		goto done;
	}

	if (number == NULL) {
		ERR("No number at the location \n");
		error_code = BT_HFP_AGENT_ERROR_INVALID_MEMORY_INDEX;
		goto done;
	}

	DBG_SECURE("number %s\n", number);

	/*Make Voice call*/
	if (!__bt_hfp_agent_make_call(number)) {
		ERR("Problem launching application \n");
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

	contacts_disconnect2();

	if (error_code == BT_HFP_AGENT_ERROR_NONE) {
		dbus_g_method_return(context);
		return TRUE;
	} else {
		error = __bt_hfp_agent_set_error(error_code);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}
}

static gboolean bt_hfp_agent_send_dtmf(BtHfpAgent *agent, const gchar *dtmf,
				const gchar *path, const gchar *sender,
				DBusGMethodInvocation *context)
{
	GError *error;
	int ret;

	if (dtmf == NULL || path == NULL || sender == NULL) {
		ERR("Invalid Argument\n");
		error = __bt_hfp_agent_set_error(
					BT_HFP_AGENT_ERROR_INVALID_PARAM);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	DBG_SECURE("Dtmf = %s \n", dtmf);
	DBG("Application path = %s \n", path);
	DBG("Sender = %s\n", sender);

	ret = __bt_hfp_agent_dbus_method_send(sender,
				path, TELEPHONY_APP_INTERFACE,
				"SendDtmf", FALSE,
				DBUS_TYPE_STRING, &dtmf,
				DBUS_TYPE_INVALID);

	if (ret != BT_HFP_AGENT_ERROR_NONE) {
		error = __bt_hfp_agent_set_error(ret);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	/*App Selector code here needed*/
	dbus_g_method_return(context);
	return TRUE;
}

static void __bt_hfp_agent_launch_voice_dial(gboolean activate)
{
	service_h service = NULL;

	service_create(&service);

	if (service == NULL) {
		ERR("Service create failed");
		return;
	}

	service_set_package(service, "org.tizen.svoice");
	service_set_operation(service, SERVICE_OPERATION_DEFAULT);
	service_add_extra_data(service, "domain", "bt_headset");

	if (!activate)
		service_add_extra_data(service, "action_type", "deactivate");

	if (service_send_launch_request(service, NULL, NULL) !=
						SERVICE_ERROR_NONE)
		ERR("launch failed");

	service_destroy(service);
	return;
}

static gboolean bt_hfp_agent_voice_dial(BtHfpAgent *agent, gboolean activate,
				DBusGMethodInvocation *context)
{
	DBG("Activate = %d \n", activate);

	__bt_hfp_agent_launch_voice_dial(activate);
	dbus_g_method_return(context);
	return TRUE;
}

static gboolean bt_hfp_agent_get_battery_status(BtHfpAgent *object,
				DBusGMethodInvocation *context)
{
	gint battery_chrg_status;
	gint battery_capacity;
	GError *error;

	if (vconf_get_int(VCONFKEY_SYSMAN_BATTERY_CHARGE_NOW,
						&battery_chrg_status)) {
		ERR("VCONFKEY_SYSMAN_BATTERY_CHARGE_NOW failed\n");
		goto fail;
	}

	if (vconf_get_int(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
						&battery_capacity)) {
		ERR("VCONFKEY_SYSMAN_BATTERY_CAPACITY failed\n");
		goto fail;
	}

	DBG("Status:[%d]  Capacity:[%d]\n", battery_chrg_status, battery_capacity);

	dbus_g_method_return(context, battery_chrg_status, battery_capacity);
	return TRUE;

fail:
	error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_BATTERY_STATUS);
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	return FALSE;
}

static gboolean bt_hfp_agent_get_signal_quality(BtHfpAgent *object,
					DBusGMethodInvocation *context)
{
	gint rssi;
	GError *error;

	if (vconf_get_int(VCONFKEY_TELEPHONY_RSSI, &rssi)) {
		ERR("VCONFKEY_TELEPHONY_RSSI failed\n");
		goto fail;
	}

	BT_CHECK_SIGNAL_STRENGTH(rssi);
	DBG("RSSI : %d \n", rssi);

	dbus_g_method_return(context, rssi, BT_SIGNAL_QUALITY_BER);
	return TRUE;
fail:
	error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_SIGNAL_STATUS);
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	return FALSE;
}

static gboolean bt_hfp_agent_get_operator_name(BtHfpAgent *object,
				DBusGMethodInvocation *context)
{
	char *operator_name;
	GError *error;

	operator_name = vconf_get_str(VCONFKEY_TELEPHONY_NWNAME);
	if (NULL == operator_name) {
		ERR("vconf_get_str failed \n");
		error = __bt_hfp_agent_set_error(BT_HFP_AGENT_ERROR_INTERNAL);
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	}

	if (!g_utf8_validate(operator_name, -1, NULL)) {
		free(operator_name);
		operator_name = strdup("Unknown");
	}

	DBG_SECURE("operator_name  = [%s] \n", operator_name);

	dbus_g_method_return(context, operator_name);
	free(operator_name);

	return TRUE;
}

static gboolean bt_hfp_agent_nrec_status(BtHfpAgent *agent, gboolean status,
				DBusGMethodInvocation *context)
{
	DBusMessage *signal;

	DBG("NREC status = %d \n", status);

	if (status)
		nrec_status = FALSE;
	else
		nrec_status = TRUE;

	dbus_g_method_return(context);

	/*Emit NREC Status change signal with value*/
	signal = dbus_message_new_signal(BT_HFP_AGENT_OBJECT,
					BT_HFP_AGENT_INTERFACE,
					"NrecStatusChanged");
	if (!signal) {
		ERR("Signal is NULL\n");
		return FALSE;
	}

	if (!dbus_message_append_args(signal,
				DBUS_TYPE_BOOLEAN, &nrec_status,
				DBUS_TYPE_INVALID)) {
		ERR("Signal appending failed\n");
		dbus_message_unref(signal);
		return FALSE;
	}

	dbus_connection_send(gconn, signal, NULL);
	dbus_message_unref(signal);
	return TRUE;
}

static void __bt_hfp_agent_append_variant(DBusMessageIter *iter,
			int type, void *val)
{
	DBusMessageIter value_iter;
	const char *variant;

	switch (type) {
	case DBUS_TYPE_BOOLEAN:
		variant = DBUS_TYPE_BOOLEAN_AS_STRING;
		break;
	case DBUS_TYPE_STRING:
		variant = DBUS_TYPE_STRING_AS_STRING;
		break;
	case DBUS_TYPE_BYTE:
		variant = DBUS_TYPE_BYTE_AS_STRING;
		break;
	case DBUS_TYPE_UINT16:
		variant = DBUS_TYPE_UINT16_AS_STRING;
		break;
	case DBUS_TYPE_UINT32:
		variant = DBUS_TYPE_UINT32_AS_STRING;
		break;
	case DBUS_TYPE_INT16:
		variant = DBUS_TYPE_INT16_AS_STRING;
		break;
	case DBUS_TYPE_INT32:
		variant = DBUS_TYPE_INT32_AS_STRING;
		break;
	case DBUS_TYPE_OBJECT_PATH:
		variant = DBUS_TYPE_OBJECT_PATH_AS_STRING;
		break;
	default:
		variant = DBUS_TYPE_VARIANT_AS_STRING;
		break;
	}

	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, variant,
							&value_iter);
	dbus_message_iter_append_basic(&value_iter, type, val);
	dbus_message_iter_close_container(iter, &value_iter);
}

static gboolean __bt_hfp_agent_dbus_method_variant_send(const char *path,
		const char *interface, const char *method, const char *name,
		int type, void *value)
{
	DBusMessage *msg;
	DBusMessage *reply;
	DBusError err;
	DBusMessageIter iter;

	msg = dbus_message_new_method_call(BLUEZ_SERVICE_NAME,
			path, interface, method);

	if (!msg) {
		ERR("Unable to allocate new D-Bus %s message", method);
		return FALSE;
	}

	dbus_message_iter_init_append(msg, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name);

	__bt_hfp_agent_append_variant(&iter, type, value);

	dbus_error_init(&err);

	reply = dbus_connection_send_with_reply_and_block(gconn,
				msg, -1, &err);

	dbus_message_unref(msg);

	if (!reply) {
		if (dbus_error_is_set(&err)) {
			if (err.message != NULL) {
				ERR("Error message = %s\n", err.message);
			}
			dbus_error_free(&err);
		}
		return FALSE;
	}

	dbus_message_unref(reply);
	return TRUE;
}

static gboolean __bt_hfp_agent_send_registration_status_changed(
		bt_hfp_agent_network_registration_status_t status)
{
	const char *property = g_strdup("RegistrationChanged");

	DBG("status = [%d]", status);

	if (!__bt_hfp_agent_dbus_method_variant_send(TELEPHONY_CSD_OBJECT_PATH,
			TELEPHONY_CSD_INTERFACE,
			BT_HFP_AGENT_SET_PROPERTY,
			property, DBUS_TYPE_BYTE, &status)) {
		ERR("__bt_hfp_agent_dbus_method_variant_send failed\n");
		g_free((void *)property);
		return FALSE;
	}

	g_free((void *)property);
	return TRUE;
}

static gboolean __bt_hfp_agent_send_subscriber_number_changed(
							const char *number)
{
	const char *property = g_strdup("SubscriberNumberChanged");

	DBG_SECURE("number = [%s]", number);

	if (!__bt_hfp_agent_dbus_method_variant_send(TELEPHONY_CSD_OBJECT_PATH,
			TELEPHONY_CSD_INTERFACE,
			BT_HFP_AGENT_SET_PROPERTY,
			property,
			DBUS_TYPE_STRING, &number)) {
		ERR("__bt_hfp_agent_dbus_method_variant_send failed\n");
		g_free((void *)property);
		return FALSE;
	}

	g_free((void *)property);
	return TRUE;
}

static gboolean __bt_hfp_agent_send_signal_bar_changed(int signal_bar)
{
	const char *property = g_strdup("SignalBarsChanged");

	DBG("Level = [%d]", signal_bar);

	if (!__bt_hfp_agent_dbus_method_variant_send(TELEPHONY_CSD_OBJECT_PATH,
			TELEPHONY_CSD_INTERFACE,
			BT_HFP_AGENT_SET_PROPERTY,
			property, DBUS_TYPE_INT32, &signal_bar)) {
		ERR("__bt_hfp_agent_dbus_method_variant_send failed\n");
		g_free((void *)property);
		return FALSE;
	}

	g_free((void *)property);
	return TRUE;
}

static gboolean __bt_hfp_agent_send_battery_level_changed(int battery_level)
{
	const char *property = g_strdup("BatteryBarsChanged");
	int battery_status;

	DBG("Level = [%d] \n", battery_level);

	/* We need to send battery status ranging from 0-5 */
	if (battery_level < 5)
		 battery_status = 0;
	else if (battery_level >= 100)
		battery_status = 5;
	else
		battery_status = battery_level / 20 + 1;

	if (!__bt_hfp_agent_dbus_method_variant_send(TELEPHONY_CSD_OBJECT_PATH,
			TELEPHONY_CSD_INTERFACE,
			BT_HFP_AGENT_SET_PROPERTY,
			property,
			DBUS_TYPE_INT32, &battery_status)) {
		ERR("__bt_hfp_agent_dbus_method_variant_send failed\n");
		g_free((void *)property);
		return FALSE;
	}

	g_free((void *)property);
	return TRUE;
}

static void __bt_hfp_agent_send_battery_level(void)
{
	int ret;
	int batt;

	ret = vconf_get_int(VCONFKEY_SYSMAN_BATTERY_CAPACITY, &batt);
	if (ret != 0) {
		ERR("vconf_get_int failed err = %d \n", ret);
		return;
	}

	__bt_hfp_agent_send_battery_level_changed(batt);
}

static void __bt_hfp_agent_send_signal_status(void)
{
	int ret;
	int signal_level;

	ret = vconf_get_int(VCONFKEY_TELEPHONY_RSSI, &signal_level);
	if (ret != 0) {
		ERR("vconf_get_int failed err = %d \n", ret);
		return;
	}

	BT_CHECK_SIGNAL_STRENGTH(signal_level);
	__bt_hfp_agent_send_signal_bar_changed(signal_level);
}

static void __bt_hfp_agent_network_send( int service, int roam_status)
{
	int ret;
	bt_hfp_agent_network_registration_status_t network_service;

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
		network_service = BT_HFP_AGENT_NETWORK_REG_STATUS_HOME;
	else if (roam_status == 1 && service == 1)
		network_service = BT_HFP_AGENT_NETWORK_REG_STATUS_ROAMING;
	else
		network_service = BT_HFP_AGENT_NETWORK_REG_STATUS_UNKOWN;

	__bt_hfp_agent_send_registration_status_changed(network_service);
}

static void __bt_hfp_agent_send_network_status(void)
{
	int ret;
	int roam_status;
	int service;

	ret = vconf_get_int(VCONFKEY_TELEPHONY_SVC_ROAM, &roam_status);
	if (ret != 0) {
		ERR("vconf_get_int failed for \n");
		return;
	}

	DBG("roam_status  = [%d] \n", roam_status);

	ret = vconf_get_int(VCONFKEY_TELEPHONY_SVCTYPE, &service);
	if (ret != 0) {
		ERR("vconf_get_int failed\n");
		return;
	}

	DBG("service  = [%d] \n", service);

	__bt_hfp_agent_network_send(service, roam_status);
}

static void __bt_hfp_agent_send_vconf_values(void)
{
	__bt_hfp_agent_send_battery_level();
	__bt_hfp_agent_send_signal_status();
	__bt_hfp_agent_send_network_status();
}

static void __bt_hfp_agent_battery_status_cb(keynode_t *node)
{
	int batt = vconf_keynode_get_int(node);

	__bt_hfp_agent_send_battery_level_changed(batt);
}

static void __bt_hfp_agent_network_signal_status_cb(keynode_t *node)
{
	int signal_bar = vconf_keynode_get_int(node);

	BT_CHECK_SIGNAL_STRENGTH(signal_bar);
	__bt_hfp_agent_send_signal_bar_changed(signal_bar);
}

static void __bt_hfp_agent_network_register_status_cb(keynode_t *node)
{
	int service = vconf_keynode_get_int(node);
	int roam_status;
	int ret;

	DBG("Current Signal Level = [%d] \n", service);

	ret = vconf_get_int(VCONFKEY_TELEPHONY_SVC_ROAM, &roam_status);
	if (ret != 0) {
		ERR("Get roaming status failed err = %d\n", ret);
		return;
	}

	__bt_hfp_agent_network_send(service, roam_status);
}

static void __bt_hfp_agent_subscribe_vconf_updates(void)
{
	int ret;

	DBG("\n");

	ret = vconf_notify_key_changed(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
				(void *)__bt_hfp_agent_battery_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to battery status failed err =  [%d]\n", ret);
	}

	ret = vconf_notify_key_changed(VCONFKEY_TELEPHONY_RSSI,
			(void *)__bt_hfp_agent_network_signal_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to netowrk signal failed err =  [%d]\n", ret);
	}

	ret = vconf_notify_key_changed(VCONFKEY_TELEPHONY_SVCTYPE,
			(void *)__bt_hfp_agent_network_register_status_cb, NULL);
	if (0 != ret) {
		ERR("Subsrciption to network failed err =  [%d]\n", ret);
	}
}

static void __bt_hfp_agent_release_vconf_updates(void)
{
	int ret;

	DBG("\n");

	ret = vconf_ignore_key_changed(VCONFKEY_SYSMAN_BATTERY_CAPACITY,
			(vconf_callback_fn)__bt_hfp_agent_battery_status_cb);
	if (0 != ret) {
		ERR("vconf_ignore_key_changed failed\n");
	}

	ret = vconf_ignore_key_changed(VCONFKEY_TELEPHONY_RSSI,
		(vconf_callback_fn)__bt_hfp_agent_network_signal_status_cb);
	if (0 != ret) {
		ERR("vconf_ignore_key_changed failed\n");
	}

	ret = vconf_ignore_key_changed(VCONFKEY_TELEPHONY_SVCTYPE,
		(vconf_callback_fn)__bt_hfp_agent_network_register_status_cb);
	if (0 != ret) {
		ERR("vconf_ignore_key_changed failed\n");
	}
}

static gboolean bt_hfp_agent_connection_status(BtHfpAgent *agent, gboolean status,
				DBusGMethodInvocation *context)
{
	DBG("HF device is %s\n", status ? "Connected" : "Disconnected");

	if (status) {
		__bt_hfp_agent_send_vconf_values();
		__bt_hfp_agent_subscribe_vconf_updates();
	} else {
		__bt_hfp_agent_release_vconf_updates();
		nrec_status = FALSE;
	}

	dbus_g_method_return(context);

	return TRUE;
}


static void __bt_hfp_agent_dbus_init(BtHfpAgent *agent)
{
	GError *error = NULL;
	DBusGProxy *bus_proxy = NULL;
	guint result;

	g_connection = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		ERR("Failed connection to system bus[%s] \n", error->message);
		g_error_free(error);
		return;
	}

	bus_proxy = dbus_g_proxy_new_for_name(g_connection,
					DBUS_SERVICE_DBUS,
					DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);
	if (bus_proxy == NULL) {
		ERR("Failed to get a proxy for D-Bus\n");
		return;
	}

	if (!dbus_g_proxy_call(bus_proxy, "RequestName", &error, G_TYPE_STRING,
			BT_HFP_SERVICE, G_TYPE_UINT, 0, G_TYPE_INVALID,
			G_TYPE_UINT, &result, G_TYPE_INVALID)) {
		if (error != NULL) {
			ERR("RequestName RPC failed[%s]\n", error->message);
			g_error_free(error);
		}

		g_object_unref(bus_proxy);
		return;
	}

	DBG("result : %d %d\n", result, DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		ERR("Failed to get the primary well-known name.\n");
		g_object_unref(bus_proxy);
		return;
	}

	g_object_unref(bus_proxy);

	dbus_g_connection_register_g_object(g_connection,
		BT_HFP_SERVICE_OBJECT_PATH, G_OBJECT(agent));

	gconn = dbus_g_connection_get_connection(g_connection);
	if (gconn == NULL) {
		ERR("Failed to get connection \n");
		return;
	}
}

static void  __bt_hfp_agent_tel_cb(TapiHandle *handle,
				int result,
				void *data,
				void *user_data)
{
	TelSimMsisdnList_t *number;
	gchar *subscriber_number;

	if (data == NULL)
	{
		ERR("data == NULL \n");
		return;
	}

	number = (TelSimMsisdnList_t *)data;
	subscriber_number = g_strdup(number->list[0].num);
	__bt_hfp_agent_send_subscriber_number_changed(subscriber_number);
	g_free(subscriber_number);
}

static void __bt_hfp_agent_sigterm_handler(int signo)
{
	if (gmain_loop) {
		g_main_loop_quit(gmain_loop);
	} else {
		DBG("Terminating HFP agent");
		exit(0);
	}
}

int main(void)
{
	TapiHandle *handle;
	BtHfpAgent *bt_hfp_obj = NULL;
	struct sigaction sa;
	int tapi_result;

	DBG("Starting Bluetooth HFP agent");

	g_type_init();

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_NOCLDSTOP;
	sa.sa_handler = __bt_hfp_agent_sigterm_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	gmain_loop = g_main_loop_new(NULL, FALSE);

	if (gmain_loop == NULL) {
		ERR("GMainLoop create failed \n");
		return EXIT_FAILURE;
	}

	bt_hfp_obj = g_object_new(BT_HFP_TYPE_AGENT, NULL);

	handle = tel_init(NULL);
	tapi_result = tel_get_sim_msisdn(handle, __bt_hfp_agent_tel_cb,
					bt_hfp_obj);

	if(tapi_result != TAPI_API_SUCCESS)
		ERR("Fail to get sim info: %d", tapi_result);

	__bt_hfp_agent_dbus_init(bt_hfp_obj);

	g_main_loop_run(gmain_loop);

	tel_deinit(handle);

	if (bt_hfp_obj) {
		dbus_g_connection_unregister_g_object(g_connection,
						G_OBJECT(bt_hfp_obj));
		g_object_unref(bt_hfp_obj);
	}

	if (g_connection)
		dbus_g_connection_unref(g_connection);

	if (gmain_loop)
		g_main_loop_unref(gmain_loop);

	DBG("Terminating Bluetooth HFP agent");
	return 0;
}
