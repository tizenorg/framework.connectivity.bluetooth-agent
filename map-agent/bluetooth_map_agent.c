/*
 * bluetooth-agent
 *
 * Copyright (c) 2012-2013 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
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
#include <time.h>
#include "vconf.h"
#include "vconf-keys.h"

#include <sys/types.h>
#include <fcntl.h>

/*Messaging Header Files*/
#include "msg.h"
#include "msg_storage.h"
#include "msg_storage_types.h"
#include "msg_transport.h"
#include "msg_transport_types.h"
#include "msg_types.h"

#ifdef SUPPORT_EMAIL
/*Email Header Files*/
#include "email-types.h"
#include "email-api-init.h"
#include "email-api-account.h"
#include "email-api-mailbox.h"
#include "email-api-mail.h"
#include "email-api-network.h"
#endif

#include <bluetooth_map_agent.h>

#include <map_bmessage.h>

#define OBEX_CLIENT_SERVICE "org.openobex.client"
#define OBEX_CLIENT_INTERFACE "org.openobex.Client"
#define OBEX_CLIENT_PATH "/"
#define MNS_CLIENT_INTERFACE "org.openobex.MessageNotification"

#define DBUS_STRUCT_STRING_STRING_UINT (dbus_g_type_get_struct("GValueArray", \
		G_TYPE_STRING, G_TYPE_STRING,  G_TYPE_STRING, G_TYPE_INVALID))

#define DBUS_STRUCT_MESSAGE_LIST (dbus_g_type_get_struct("GValueArray", \
		G_TYPE_STRING, G_TYPE_STRING,  G_TYPE_STRING, G_TYPE_STRING, \
		G_TYPE_STRING, G_TYPE_STRING,  G_TYPE_STRING, G_TYPE_STRING, \
		G_TYPE_STRING, G_TYPE_STRING,   G_TYPE_BOOLEAN, G_TYPE_STRING, \
		G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, \
		G_TYPE_BOOLEAN, G_TYPE_STRING, \
		G_TYPE_INVALID))

static msg_handle_t g_msg_handle = NULL;

#define BT_MAP_NEW_MESSAGE "NewMessage"
#define BT_MAP_STATUS_CB "sent status callback"
#define BT_MAP_MSG_CB "sms message callback"
#define BT_MAP_EMAIL_DEFAULTACCOUNT "db/email/defaultaccount"
#define BT_MNS_OBJECT_PATH "/org/bluez/mns"
#define BT_MNS_INTERFACE "org.bluez.mns"
#define BT_MAIL_TEMP_BODY "/tmp/bt_mail.txt"
#define BT_MAP_SENT_FOLDER_NAME "SENT"
#define BT_MAP_MSG_INFO_MAX 256
#define BT_MAP_MSG_HANDLE_MAX 21
#define BT_MAP_TIMESTAMP_MAX_LEN 16
#define BT_MAP_SUBJECT_MAX_LEN 20
#define BT_MAP_MSG_BODY_MAX 1024
#define BT_MSG_UPDATE	0
#define BT_MSG_DELETE	1
#define BT_SMS 0
#define BT_EMAIL 1
#define BT_EMAIL_HANDLE_BASE (G_MAXUINT64 / 2)
#define BT_MAIL_ID_MAX_LENGTH 50

#define BEGIN_BMSEG "BEGIN:BMSG\r\n"
#define END_BMSEG "END:BMSG\r\n"
#define BMSEG_VERSION "VERSION:1.0\r\n"
#define MSEG_STATUS "STATUS:%s\r\n"
#define MSEG_TYPE "TYPE:%s\r\n"
#define FOLDER_PATH "FOLDER:%s\r\n"
#define VCARD "BEGIN:VCARD\r\nVERSION:2.1\r\nN:%s\r\nTEL:%s\r\nEND:VCARD\r\n"
#define BEGIN_BENV "BEGIN:BENV\r\n"
#define END_BENV "END:BENV\r\n"
#define BEGIN_BBODY "BEGIN:BBODY\r\n"
#define END_BBODY "END:BBODY\r\n"
#define ENCODING "ENCODING:%s\r\n"
#define CHARSET "CHARSET:%s\r\n"
#define LANGUAGE "LANGUAGE:%s\r\n"
#define LENGTH "LENGTH:%d\r\n"
#define MSG_BODY "BEGIN:MSG\r\n%s\r\nEND:MSG\r\n"

/* This has been added for testing purpose, will be removed when SMS APIs
    are available. */
#define TEST_PDU "06810000000000040681567777000021017101750261A05"\
		 "376BA0D8297E5F3B73BCC4ED3F3A030FB1ECECF41613A"\
		 "5D1E1ED3E7A0B2BD2CCF8362AEA4195407C941ECF77C9"\
		 "E769F41753968FC769BD3E4B27B5C0691EB6510FD0D7AD"\
		 "BCBF27B397D46D343A163990E42BFDB6590BCDC4E93D3"\
		 "E539889E86CF41F437485E26D7C765D0DB5E96DFCBE933"\
		 "9A1E9A36A72063900AA2BF41B5DBED760385E920E9DC357B35A9"

GSList* id_list = NULL;
guint64 current_push_map_id;

struct id_info {
	guint64 map_id;
	int uid;
};

struct msg_send_option {
	bool save_copy;
	bool retry_send;
	bool native;
};

struct message_info {
	char *handle;
	char *subject;
	char *datetime;
	char *sender_name;
	char *sender_addressing;
	char *recipient_name;
	char *recipient_addressing;
	char *type;
	char *size;
	char *reception_status;
	char *attachment_size;
	char *replyto_addressing;
	gboolean text;
	gboolean priority;
	gboolean read;
	gboolean sent;
	gboolean protect;
};

typedef struct {
	GObject parent;
} BluetoothMapAgent;

typedef struct {
	GObjectClass parent;
} BluetoothMapAgentClass;

GType bluetooth_map_agent_get_type(void);

#define BLUETOOTH_MAP_TYPE_AGENT (bluetooth_map_agent_get_type())

#define BLUETOOTH_MAP_AGENT(object) \
	(G_TYPE_CHECK_INSTANCE_CAST((object), \
	BLUETOOTH_MAP_TYPE_AGENT , BluetoothMapAgent))
#define BLUETOOTH_MAP_AGENT_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	BLUETOOTH_MAP_TYPE_AGENT , BluetoothMapAgentClass))
#define BLUETOOTH_MAP_IS_AGENT(object) \
	(G_TYPE_CHECK_INSTANCE_TYPE((object), \
	BLUETOOTH_MAP_TYPE_AGENT))
#define BLUETOOTH_MAP_IS_AGENT_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), \
	BLUETOOTH_MAP_TYPE_AGENT))
#define BLUETOOTH_MAP_AGENT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), \
	BLUETOOTH_MAP_TYPE_AGENT , BluetoothMapAgentClass))

G_DEFINE_TYPE(BluetoothMapAgent, bluetooth_map_agent, G_TYPE_OBJECT)

GMainLoop *g_mainloop = NULL;
static DBusGConnection *g_connection = NULL;
static char *g_mns_path = NULL;
static struct msg_send_option opt;

static gboolean bluetooth_map_get_folder_tree(BluetoothMapAgent *agent,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_get_message_list(BluetoothMapAgent *agent,
					gchar *folder_name, guint16 max,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_get_message(BluetoothMapAgent *agent,
					gchar *message_name,
					gboolean attach, gboolean transcode,
					gboolean first_request,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_push_message(BluetoothMapAgent *agent,
					gboolean save_copy,
					gboolean retry_send,
					gboolean native,
					gchar *folder_name,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_push_message_data(BluetoothMapAgent *agent,
					gchar *bmsg,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_update_message(BluetoothMapAgent *agent,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_set_read_status(BluetoothMapAgent *agent,
					gchar *handle, gboolean read_status,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_set_delete_status(BluetoothMapAgent *agent,
					gchar *handle, gboolean delete_status,
					DBusGMethodInvocation *context);
static gboolean bluetooth_map_noti_registration(BluetoothMapAgent *agent,
					gchar *remote_addr,
					gboolean status,
					DBusGMethodInvocation *context);


#include "bluetooth_map_agent_glue.h"

static void bluetooth_map_agent_init(BluetoothMapAgent *obj)
{
	DBG("+\n");

	g_assert(obj != NULL);
}

static void bluetooth_map_agent_finalize(GObject *obj)
{
	DBG("+\n");

	G_OBJECT_CLASS(bluetooth_map_agent_parent_class)->finalize(obj);
}

static void bluetooth_map_agent_class_init(BluetoothMapAgentClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;

	g_assert(klass != NULL);

	object_class->finalize = bluetooth_map_agent_finalize;

	dbus_g_object_type_install_info(BLUETOOTH_MAP_TYPE_AGENT,
					&dbus_glib_bluetooth_map_object_info);
}

static GQuark __bt_map_agent_error_quark(void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string("agent");

	return quark;
}

static GError *__bt_map_agent_error(bt_map_agent_error_t error,
				     const char *err_msg)
{
	return g_error_new(BT_MAP_AGENT_ERROR, error, err_msg, NULL);
}

static guint64 _bt_validate_uid(int uid)
{
        DBG("Validate uid");
        struct id_info *info;
        int count;
        int i;

        count = g_slist_length(id_list);
        for (i = 0; i < count; i++) {
                info = (struct id_info *)g_slist_nth_data(id_list, i);
		if (!info)
			break;

                if (info->uid == uid) {
                        printf("uid = %d\n", uid);
                        return info->map_id;
                }
        }

        return 0;
}

static guint64 __bt_add_id(int uid)
{
        DBG("Add id: %d\n", uid);
        static guint64 map_id;
        struct id_info *info;
        guint64 test;

        test = _bt_validate_uid(uid);
        DBG("test: %llx\n", test);
        if (test)
                return test;

        info = g_new0(struct id_info, 1);

        map_id++;

        info->map_id = map_id;
        info->uid = uid;
        DBG("map_id = %llx, uid = %d \n", info->map_id, info->uid);

        id_list = g_slist_append(id_list, info);

        return map_id;
}

static int __bt_get_id(guint64 map_id)
{
        DBG("get id\n");
        struct id_info *info;
        int count;
	int i;

        count = g_slist_length(id_list);

        for (i = 0; i < count; i++) {
                info = (struct id_info *)g_slist_nth_data(id_list, i);

                if (info->map_id == map_id)
                        return info->uid;
        }

        return -1;
}

static int __bt_get_uid(gchar *handle)
{
	guint64 map_id;
	int uid;

	if (NULL == handle)
		return -1;

	map_id = g_ascii_strtoull(handle, NULL, 16);
	if (!map_id)
		return -1;

	uid = __bt_get_id(map_id);

	return uid;
}

static int __bt_update_id(guint64 map_id, int new_uid)
{
	DBG("update id\n");
	struct id_info *info;
	int i;
	int count;

	count = g_slist_length(id_list);

	for (i = 0; i < count; i++) {
		info = g_slist_nth_data(id_list, i);

		if (info->map_id == map_id) {
			info->uid = new_uid;
			return map_id;
		}
	}

        return -1;
}

static void __bt_remove_list(GSList *id_list)
{
	if (!id_list)
		return;

        DBG("Removing id list\n");
        g_slist_free_full(id_list, g_free);
}


static gchar *__bt_get_folder_name(int id)
{
	int ret;
	char folder_name[BT_MAP_MSG_INFO_MAX] = {0,};

	msg_struct_list_s g_folderList;
	msg_struct_t p_folder;

	ret = msg_get_folder_list(g_msg_handle, &g_folderList);
	if (ret != MSG_SUCCESS)
		goto done;

	p_folder = g_folderList.msg_struct_info[id];

	ret = msg_get_str_value(p_folder, MSG_FOLDER_INFO_NAME_STR,
					folder_name, BT_MAP_MSG_INFO_MAX);
	if (ret != MSG_SUCCESS)
		goto done;

	return g_strdup_printf("TELECOM/MSG/%s", folder_name);

done:
	return g_strdup("TELECOM/MSG");
}

static void __get_msg_timestamp(time_t *ltime, char *timestamp)
{
	struct tm local_time;
	int year;
	int month;

	if (!localtime_r(ltime, &local_time))
		return;

	year = local_time.tm_year + 1900; /* years since 1900 */
	month = local_time.tm_mon + 1; /* months since January */
	snprintf(timestamp, 16, "%04d%02d%02dT%02d%02d%02d", year, month,
					local_time.tm_mday, local_time.tm_hour,
					local_time.tm_min, local_time.tm_sec);

	return;
}

static char *__bt_prepare_msg_bmseg(msg_struct_t msg_info, gboolean attach,
							gboolean transcode)
{
	int ret;
	int m_type = MSG_TYPE_SMS;
	int folder_id;
	int count;
	bool read_status = false;
	char msg_body[BT_MAP_MSG_BODY_MAX] = {0,};
	char addr_value[MAX_ADDRESS_VAL_LEN] = {0,};
	char name_value[MAX_ADDRESS_VAL_LEN] = {0,};
	gchar *folder_path;

	msg_struct_list_s *addr_list = NULL;
	GString *msg;

	msg = g_string_new(BEGIN_BMSEG);
	g_string_append(msg, BMSEG_VERSION);

	ret = msg_get_bool_value(msg_info, MSG_MESSAGE_READ_BOOL, &read_status);
	if (ret == MSG_SUCCESS) {
		DBG("read_status %d\n", read_status);
	}

	if (read_status)
		g_string_append_printf(msg, MSEG_STATUS, "READ");
	else
		g_string_append_printf(msg, MSEG_STATUS, "UNREAD");

	ret = msg_get_int_value(msg_info, MSG_MESSAGE_TYPE_INT, &m_type);
	if (ret == MSG_SUCCESS) {
		DBG("m_type %d\n", m_type);
	}

	switch (m_type) {
	case MSG_TYPE_MMS:
	case MSG_TYPE_MMS_JAVA:
	case MSG_TYPE_MMS_NOTI:
		g_string_append_printf(msg, MSEG_TYPE, "MMS");
		break;

	default:
		 g_string_append_printf(msg, MSEG_TYPE, "SMS_GSM");
		break;
	}

	ret = msg_get_int_value(msg_info, MSG_MESSAGE_FOLDER_ID_INT,
							&folder_id);
	if (ret == MSG_SUCCESS) {
		DBG("folder_id %d\n", folder_id);
	}

	folder_path = __bt_get_folder_name(folder_id);
	g_string_append_printf(msg, FOLDER_PATH, folder_path);


	ret = msg_get_list_handle(msg_info, MSG_MESSAGE_ADDR_LIST_STRUCT,
							(void **)&addr_list);
	if (ret == MSG_SUCCESS) {
		count = addr_list->nCount;
		DBG("count %d \n", count);
		while (count > 0) {
			msg_struct_t addr_info = NULL;
			addr_info = addr_list->msg_struct_info[count - 1];

			msg_get_str_value(addr_info,
					MSG_ADDRESS_INFO_ADDRESS_VALUE_STR,
					addr_value, MAX_ADDRESS_VAL_LEN);
			DBG("addr_value %s\n", addr_value);
			msg_get_str_value(addr_info,
					MSG_ADDRESS_INFO_DISPLAYNAME_STR,
					name_value, MAX_ADDRESS_VAL_LEN);
			if (!strlen(name_value))
				g_stpcpy(name_value, addr_value);

			DBG("name_value %s\n", name_value);

			g_string_append_printf(msg, VCARD, name_value,
								addr_value);
			count--;
		}
	}

	g_string_append(msg, BEGIN_BENV);
	g_string_append(msg, BEGIN_BBODY);

	if (transcode) {
		g_string_append_printf(msg, CHARSET, "UTF-8");

		if (m_type == MSG_TYPE_MMS)
			ret = msg_get_str_value(msg_info,
						MSG_MESSAGE_MMS_TEXT_STR,
						msg_body, BT_MAP_SUBJECT_MAX_LEN);
		else
			ret = msg_get_str_value(msg_info,
						MSG_MESSAGE_SMS_DATA_STR,
						msg_body, BT_MAP_MSG_BODY_MAX);

		if (ret == MSG_SUCCESS) {
			g_string_append_printf(msg, LENGTH, strlen(msg_body));
			g_string_append_printf(msg, MSG_BODY, msg_body);
		}
	} else {
		gchar *msg_pdu;
		g_string_append_printf(msg, ENCODING, "G-7BIT");
		g_string_append_printf(msg, CHARSET, "native");
		/* The below line has been added for testing purpose,
		    will be removed when SMS APIs are available. */
		msg_pdu = g_strdup(TEST_PDU);
		g_string_append_printf(msg, LENGTH, strlen(msg_pdu));
		g_string_append_printf(msg, MSG_BODY, msg_pdu);
		g_free(msg_pdu);
	}

	g_string_append(msg, END_BBODY);
	g_string_append(msg, END_BENV);
	g_string_append(msg, END_BMSEG);

	return g_string_free(msg, FALSE);
}

static void __bt_message_info_free(struct message_info msg_info)
{
	g_free(msg_info.handle);
	g_free(msg_info.subject);
	g_free(msg_info.datetime);
	g_free(msg_info.sender_name);
	g_free(msg_info.sender_addressing);
	g_free(msg_info.replyto_addressing);
	g_free(msg_info.recipient_name);
	g_free(msg_info.recipient_addressing);
	g_free(msg_info.type);
	g_free(msg_info.reception_status);
	g_free(msg_info.size);
	g_free(msg_info.attachment_size);
}

static struct message_info __bt_message_info_get(msg_struct_t msg_struct_handle)
{
	struct message_info msg_info = {0,};
	int ret;
	int msg_id;
	guint64 uid;
	int dptime;
	int m_type = 0;
	int data_size;
	int priority;
	int direction_type;
	bool protect_status = 0;
	bool read_status = 0;

	char msg_handle[BT_MAP_MSG_HANDLE_MAX] = {0,};
	char msg_subject[BT_MAP_SUBJECT_MAX_LEN] = {0,};
	char msg_datetime[BT_MAP_TIMESTAMP_MAX_LEN] = {0,};
	char msg_size[5] = {0,};
	char msg_body[BT_MAP_MSG_BODY_MAX] = {0,};
	char addr_value[MAX_ADDRESS_VAL_LEN] = {0,};
	char name_value[MAX_ADDRESS_VAL_LEN] = {0,};

	msg_info.text = FALSE;
	msg_info.protect = FALSE;
	msg_info.read = FALSE;
	msg_info.priority = FALSE;

	msg_struct_t msg = NULL;
	msg_struct_t send_opt = NULL;
	msg_struct_list_s *addr_list = NULL;
	msg_struct_t addr_info = NULL;

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_ID_INT, &msg_id);
	if (ret == MSG_SUCCESS) {
		uid = __bt_add_id(msg_id);
		snprintf(msg_handle, sizeof(msg_handle), "%llx", uid);
	}
	msg_info.handle = g_strdup(msg_handle);

	msg = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
	send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);

	ret = msg_get_message(g_msg_handle,
						(msg_message_id_t)msg_id,
						msg, send_opt);
	if (ret != MSG_SUCCESS) {
		DBG("ret = %d\n", ret);
		goto next;
	}

	ret = msg_get_list_handle(msg, MSG_MESSAGE_ADDR_LIST_STRUCT,
							(void **)&addr_list);
	if (ret != MSG_SUCCESS) {
		DBG("ret = %d\n", ret);
		goto next;
	}

	addr_info = addr_list->msg_struct_info[0];

	ret = msg_get_str_value(addr_info, MSG_ADDRESS_INFO_ADDRESS_VALUE_STR,
				addr_value, MAX_ADDRESS_VAL_LEN);
	if (ret == MSG_SUCCESS)
		DBG("addr_value %s\n", addr_value);

	ret = msg_get_str_value(addr_info, MSG_ADDRESS_INFO_DISPLAYNAME_STR,
			name_value, MAX_ADDRESS_VAL_LEN);
	if (ret == MSG_SUCCESS)
		DBG("name_value %s\n", name_value);

	if (!strlen(name_value))
		g_stpcpy(name_value, addr_value);

	DBG("name_value %s\n", name_value);

	ret = msg_get_int_value(msg, MSG_MESSAGE_DIRECTION_INT, &direction_type);
	if (ret != MSG_SUCCESS)
		goto next;

	if (direction_type == MSG_DIRECTION_TYPE_MT) {
		msg_info.sender_name = g_strdup(name_value);
		msg_info.sender_addressing = g_strdup(addr_value);
		msg_info.recipient_name = g_strdup("Unknown");
		msg_info.recipient_addressing = g_strdup("0000");
	} else {
		msg_info.sender_name = g_strdup("Unknown");
		msg_info.sender_addressing = g_strdup("0000");
		msg_info.recipient_name = g_strdup(name_value);
		msg_info.recipient_addressing = g_strdup(addr_value);
	}

next:
	msg_release_struct(&msg);
	msg_release_struct(&send_opt);

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_DISPLAY_TIME_INT, &dptime);
	if (ret == MSG_SUCCESS) {
		__get_msg_timestamp((time_t *)&dptime, msg_datetime);
	}
	msg_info.datetime = g_strdup(msg_datetime);

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_TYPE_INT,
								&m_type);
	if (ret == MSG_SUCCESS) {
		DBG("m_type %d\n", m_type);
	}

	switch (m_type) {
	case MSG_TYPE_MMS:
	case MSG_TYPE_MMS_JAVA:
	case MSG_TYPE_MMS_NOTI:
		msg_info.type = g_strdup("MMS");
		break;

	default:
		msg_info.type = g_strdup("SMS_GSM");
		break;
	}

	if (m_type == MSG_TYPE_MMS) {
		ret = msg_get_str_value(msg_struct_handle,
					MSG_MESSAGE_SUBJECT_STR, msg_subject,
					BT_MAP_SUBJECT_MAX_LEN);
		if (ret == MSG_SUCCESS) {
			DBG("MMS subject %s", msg_subject);
		}

		msg_info.subject = g_strdup(msg_subject);

		ret = msg_get_str_value(msg_struct_handle,
					MSG_MESSAGE_MMS_TEXT_STR, msg_body,
					BT_MAP_MSG_BODY_MAX);
		if (ret == MSG_SUCCESS) {
			DBG("msg_body %s", msg_body);
			if (strlen(msg_body))
				msg_info.text = TRUE ;
		}

	} else if (m_type == MSG_TYPE_SMS) {
		ret = msg_get_str_value(msg_struct_handle,
					MSG_MESSAGE_SMS_DATA_STR, msg_body,
					BT_MAP_MSG_BODY_MAX);
		if (ret == MSG_SUCCESS) {
			DBG("SMS subject %s", msg_body);
			if (strlen(msg_body)) {
				msg_info.text = TRUE ;
				msg_info.subject = g_strndup(msg_body,
							BT_MAP_SUBJECT_MAX_LEN);
			}
		}
	}

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_DATA_SIZE_INT,
								&data_size);
	if (ret == MSG_SUCCESS)
		snprintf(msg_size, sizeof(msg_size), "%d", data_size);

	msg_info.size = g_strdup(msg_size);

	msg_info.reception_status = g_strdup("complete");
	msg_info.attachment_size = g_strdup("0");

	ret = msg_get_bool_value(msg_struct_handle, MSG_MESSAGE_PROTECTED_BOOL,
							&protect_status);
	if (ret == MSG_SUCCESS) {
		if (protect_status)
			msg_info.protect = TRUE;
	}

	ret = msg_get_bool_value(msg_struct_handle, MSG_MESSAGE_READ_BOOL,
								&read_status);
	if (ret == MSG_SUCCESS) {
		if (read_status)
			msg_info.read = TRUE;
	}

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_PRIORITY_INT,
								&priority);
	if (ret == MSG_SUCCESS) {
		if (priority == MSG_MESSAGE_PRIORITY_HIGH)
			msg_info.priority = TRUE;
	}

	return msg_info;
}

static void __bluetooth_map_msg_incoming_status_cb(msg_handle_t handle,
							msg_struct_t msg,
							void *user_param)
{
	DBusGProxy *mns_proxy;
	GError *error = NULL;

	int msg_id = 0;
	int msg_type = 0;
	int ret = MSG_SUCCESS;

	char *message_type = NULL;

	guint64 uid;

	DBG("+\n");

	ret = msg_get_int_value(msg, MSG_MESSAGE_ID_INT, &msg_id);
	if (ret != MSG_SUCCESS)
		return;

	uid = __bt_add_id(msg_id);

	ret = msg_get_int_value(msg, MSG_MESSAGE_TYPE_INT, &msg_type);
	if (ret != MSG_SUCCESS)
		return;

	switch (msg_type) {
	case MSG_TYPE_SMS:
		message_type = g_strdup("SMS_GSM");
		break;
	case MSG_TYPE_MMS:
		message_type = g_strdup("MMS");
		break;
	default:
		return;
	}

	mns_proxy = dbus_g_proxy_new_for_name(g_connection, OBEX_CLIENT_SERVICE,
							g_mns_path,
							MNS_CLIENT_INTERFACE);
	if (mns_proxy == NULL) {
		ERR("Failed to get a proxy for D-Bus\n");
		g_free(message_type);
		return;
	}

	dbus_g_proxy_call(mns_proxy, "SendEvent", &error,
		G_TYPE_STRING, "NewMessage",
		G_TYPE_UINT64, uid,
		G_TYPE_STRING, "TELECOM/MSG/INBOX",
		G_TYPE_STRING, "",
		G_TYPE_STRING, message_type,
		G_TYPE_INVALID, G_TYPE_INVALID);
	if (error) {
		DBG("Error [%s]", error->message);
		g_error_free(error);
	}

	DBG("-\n");
	g_free(message_type);
	g_object_unref(mns_proxy);
	return;
}

static gboolean __bluetooth_map_start_service()
{
	msg_error_t err = MSG_SUCCESS;
	gboolean msg_ret = TRUE;
#ifdef SUPPORT_EMAIL
	int email_err = EMAIL_ERROR_NONE;
	gboolean email_ret = TRUE;
#endif

	err = msg_open_msg_handle(&g_msg_handle);
	if (err != MSG_SUCCESS) {
		ERR("msg_open_msg_handle error = %d\n", err);
		msg_ret = FALSE;
		goto done;
	}

	err = msg_reg_sms_message_callback(g_msg_handle,
					__bluetooth_map_msg_incoming_status_cb,
					0, (void *)BT_MAP_MSG_CB);
	if (err != MSG_SUCCESS) {
		ERR("msg_reg_sms_message_callback error  = %d\n", err);
		msg_ret = FALSE;
	}

done:

#ifdef SUPPORT_EMAIL

	email_err = email_service_begin();
	if (email_err != EMAIL_ERROR_NONE) {
		ERR("email_service_begin fail  error = %d\n", email_err);
		email_ret = FALSE;
	}

	if (msg_ret || email_ret)
		return TRUE;
	else
		return FALSE;

#else

	return msg_ret;

#endif
}

static void __bluetooth_map_stop_service()
{
	if (NULL != g_msg_handle)
		msg_close_msg_handle(&g_msg_handle);

	g_msg_handle = NULL;

#ifdef SUPPORT_EMAIL
	if (EMAIL_ERROR_NONE != email_service_end())
		ERR("email_service_end fail \n");
#endif
	return;
}

#ifdef SUPPORT_EMAIL
static int __bt_store_mail(email_mailbox_type_e type, char *subject,
						char *body, char *recepients)
{
	int account_id;
	int mail_id;
	int err;
	char from_address[BT_MAIL_ID_MAX_LENGTH] = { 0, };
	FILE *body_file;
	struct stat st_buf;

	email_account_t *account_data = NULL;
	email_mailbox_t *mailbox_data = NULL;
	email_mail_data_t *mail_data = NULL;

	err = email_load_default_account_id(&account_id);
	if (EMAIL_ERROR_NONE != err)
		goto fail;

	err = email_get_account(account_id, GET_FULL_DATA_WITHOUT_PASSWORD,
								&account_data);
	if (EMAIL_ERROR_NONE != err)
		goto fail;

	err = email_get_mailbox_by_mailbox_type(account_id, type,
								&mailbox_data);
	if (EMAIL_ERROR_NONE != err)
		goto fail;

	snprintf(from_address, BT_MAIL_ID_MAX_LENGTH, "<%s>",
					account_data->user_email_address);
	email_free_account(&account_data, 1);

	mail_data = calloc(1, sizeof(email_mail_data_t));
	if (NULL == mail_data) {
		email_free_mailbox(&mailbox_data, 1);
		goto fail;
	}

	DBG("\n account_id %d\n", account_id);
	mail_data->account_id = account_id;
	mail_data->save_status = 1;
	mail_data->body_download_status = 1;
	/* mail_data->flags_draft_field = 1; */
	mail_data->flags_seen_field = 1;
	mail_data->file_path_plain = g_strdup(BT_MAIL_TEMP_BODY);

	mail_data->mailbox_id = mailbox_data->mailbox_id;
	mail_data->mailbox_type = mailbox_data->mailbox_type;
	email_free_mailbox(&mailbox_data, 1);

	mail_data->full_address_from = g_strdup(from_address);
	mail_data->full_address_to = g_strdup(recepients);
	mail_data->subject = g_strdup(subject);
	mail_data->report_status = EMAIL_MAIL_REQUEST_DSN |
							EMAIL_MAIL_REQUEST_MDN;

	body_file = fopen(BT_MAIL_TEMP_BODY, "w");
	if (body_file == NULL) {
		DBG("\n fopen [%s]failed\n", BT_MAIL_TEMP_BODY);
		email_free_mail_data(&mail_data, 1);
		goto fail;
	}

	fprintf(body_file, body);
	fflush(body_file);
	fclose(body_file);

	err = email_add_mail(mail_data, NULL, 0, NULL, 0);
	if (err != EMAIL_ERROR_NONE) {
		DBG("email_add_mail failed. [%d]\n", err);
		if (!stat(mail_data->file_path_plain, &st_buf))
			remove(mail_data->file_path_plain);

		email_free_mail_data(&mail_data, 1);
		goto fail;
	}

	DBG("saved mail id = [%d]\n", mail_data->mail_id);

	mail_id = mail_data->mail_id;

	email_free_mail_data(&mail_data, 1);

	return mail_id;

fail:
	return 0;
}

static int __bt_email_send(char *subject, char *body, char* recepients)
{
	int err;
	int mail_id;
	int handle;

	mail_id = __bt_store_mail(EMAIL_MAILBOX_TYPE_OUTBOX, subject,
							body, recepients);
	if (mail_id) {
		DBG("mail_id = %d\n", mail_id);
		err = email_send_mail(mail_id, &handle);
		if (err != EMAIL_ERROR_NONE)
			DBG("Sending failed[%d]\n", err);
	}

	return mail_id;
}
#endif

static int __bt_get_folder_id(char *folder_path)
{
	int folder_id = -1;
	int i;
	char *folder;
	msg_struct_list_s folder_list;
	msg_error_t err;
	msg_struct_t p_folder;
	DBG("__bt_get_folder_id\n");

	folder = strrchr(folder_path, '/');
	if (NULL == folder)
		return -1;

	folder++;

	DBG("folderName %s\n", folder);

	err = msg_get_folder_list(g_msg_handle, &folder_list);
	if (err != MSG_SUCCESS)
		return -1;

	for (i = 0; i < folder_list.nCount; i++) {
		p_folder = folder_list.msg_struct_info[i];
		char folder_name[BT_MAP_MSG_INFO_MAX] = {0, };

		err = msg_get_str_value(p_folder, MSG_FOLDER_INFO_NAME_STR,
					folder_name, BT_MAP_MSG_INFO_MAX);
		if (err != MSG_SUCCESS)
			continue;

		DBG("folderName %s\n", folder_name);
		if (!g_ascii_strncasecmp(folder_name, folder, strlen(folder))) {
			err = msg_get_int_value(p_folder, MSG_FOLDER_INFO_ID_INT,
								&folder_id);
			if (err != MSG_SUCCESS)
				return -1;

			break;
		}
	}

	return folder_id;

}

msg_error_t __bt_send_sms(int msg_id, msg_struct_t pMsg, msg_struct_t pSendOpt)
{
	msg_error_t err;
	msg_struct_t pReq;

	pReq = msg_create_struct(MSG_STRUCT_REQUEST_INFO);

	msg_set_int_value(pMsg, MSG_MESSAGE_ID_INT, msg_id);
	msg_set_struct_handle(pReq, MSG_REQUEST_MESSAGE_HND, pMsg);
	msg_set_struct_handle(pReq, MSG_REQUEST_SENDOPT_HND, pSendOpt);

	err = msg_sms_send_message(g_msg_handle, pReq);
	if (err == MSG_SUCCESS)
		DBG("Sending Message is successful!!!");
	else
		DBG("Sending Message is failed!!! %d", err);

	msg_release_struct(&pReq);
	return err;
}

static int __bt_push_sms(gboolean send, int folder_id, char *body,
							GSList *recepients)
{
	DBG("+ \n");
	msg_struct_t msg_info = NULL;
	msg_struct_t send_opt = NULL;
	msg_struct_list_s *addr_list;
	msg_error_t err;

	int count = 0;
	int i = 0;
	int msg_id;
	guint64 uid = -1;

	msg_info = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
	if (msg_info == NULL)
		goto fail;

	err = msg_set_int_value(msg_info, MSG_MESSAGE_TYPE_INT, MSG_TYPE_SMS);
	if (err != MSG_SUCCESS)
		goto fail;

	if (body) {
		err = msg_set_str_value(msg_info, MSG_MESSAGE_SMS_DATA_STR, body,
									strlen(body));
		if (err != MSG_SUCCESS)
			goto fail;
	} else {
		err = msg_set_str_value(msg_info, MSG_MESSAGE_SMS_DATA_STR,
								NULL, 0);
		if (err != MSG_SUCCESS)
			goto fail;
	}

	DBG("folder_id  %d\n", folder_id);
	err = msg_set_int_value(msg_info, MSG_MESSAGE_FOLDER_ID_INT,
								folder_id);
	if (err != MSG_SUCCESS)
		goto fail;

	if (recepients) {
		count = g_slist_length(recepients);
		DBG("Count = %d\n", count);
		msg_get_list_handle(msg_info, MSG_MESSAGE_ADDR_LIST_STRUCT,
							(void**)&addr_list);

		addr_list->nCount = count;
		for (i = 0; i < count; i++) {
			char *address = (char *)g_slist_nth_data(recepients, i);
			if (address == NULL) {
				DBG("[ERROR] address is value NULL, skip");
				continue;
			}
			msg_set_int_value(addr_list->msg_struct_info[i],
					MSG_ADDRESS_INFO_ADDRESS_TYPE_INT,
					MSG_ADDRESS_TYPE_PLMN);

			msg_set_int_value(addr_list->msg_struct_info[i],
					MSG_ADDRESS_INFO_RECIPIENT_TYPE_INT,
					MSG_RECIPIENTS_TYPE_TO);

			msg_set_str_value(addr_list->msg_struct_info[i],
					MSG_ADDRESS_INFO_ADDRESS_VALUE_STR,
					address, strlen(address));
		}
	}

	send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);

	err = msg_set_bool_value(send_opt, MSG_SEND_OPT_SETTING_BOOL, true);
	if (err != MSG_SUCCESS)
		goto fail;

	/* Do not keep a copy */
	err = msg_set_bool_value(send_opt, MSG_SEND_OPT_KEEPCOPY_BOOL,
							opt.save_copy);
	if (err != MSG_SUCCESS)
		goto fail;

	msg_id = msg_add_message(g_msg_handle, msg_info, send_opt);
	DBG("msg_id = %d\n", msg_id);
	uid = __bt_add_id(msg_id);

	if (send == TRUE) {
		err = __bt_send_sms(msg_id, msg_info, send_opt);
		if (err != MSG_SUCCESS) {
			uid = -1;
			goto fail;
		}
	}

fail:
	msg_release_struct(&msg_info);
	msg_release_struct(&send_opt);
	DBG("-\n");
	return uid;
}

static gboolean __bt_msg_is_mms(int msg_type)
{
	gboolean result = FALSE;

	switch (msg_type) {
	case MSG_TYPE_MMS_NOTI:
	case MSG_TYPE_MMS_JAVA:
	case MSG_TYPE_MMS:
		result = TRUE;
		break;
	default:
		break;
	}

	return result;
}

static void __bt_mns_client_connect(char *address)
{
	DBusGProxy *mns_proxy;
	GHashTable *hash;
	GValue *addr_value;
	GValue *tgt_value;
	GError *error = NULL;
	const char *session_path = NULL;

	DBG("+ address %s\n", address);

	mns_proxy = dbus_g_proxy_new_for_name(g_connection, OBEX_CLIENT_SERVICE,
						OBEX_CLIENT_PATH,
						OBEX_CLIENT_INTERFACE);
	if (mns_proxy == NULL) {
		ERR("Failed to get a proxy for D-Bus\n");
		return;
	}

	hash = g_hash_table_new_full(g_str_hash, g_str_equal,
				     NULL, (GDestroyNotify)g_free);

	addr_value = g_new0(GValue, 1);
	g_value_init(addr_value, G_TYPE_STRING);
	g_value_set_string(addr_value, address);
	g_hash_table_insert(hash, "Destination", addr_value);

	tgt_value = g_new0(GValue, 1);
	g_value_init(tgt_value, G_TYPE_STRING);
	g_value_set_string(tgt_value, "MNS");
	g_hash_table_insert(hash, "Target", tgt_value);

	dbus_g_proxy_call(mns_proxy, "CreateSession", &error,
		dbus_g_type_get_map("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
		hash, G_TYPE_INVALID,
		DBUS_TYPE_G_OBJECT_PATH, &session_path,
		G_TYPE_INVALID);
	if (error) {
		DBG("Error [%s]", error->message);
		g_error_free(error);
		g_hash_table_destroy(hash);
		g_object_unref(mns_proxy);
		return;
	}

	g_mns_path = g_strdup(session_path);
	DBG("g_mns_path = %s\n", g_mns_path);

	g_hash_table_destroy(hash);
	g_object_unref(mns_proxy);

	DBG("-\n");
	return;
}

static void __bt_mns_client_disconnect()
{
	DBusGProxy *mns_proxy;
	GError *error = NULL;

	if (!g_mns_path)
		return;

	mns_proxy = dbus_g_proxy_new_for_name(g_connection, OBEX_CLIENT_SERVICE,
						OBEX_CLIENT_PATH,
						OBEX_CLIENT_INTERFACE);
	if (mns_proxy == NULL) {
		DBG("Failed to get a proxy for D-Bus\n");
		return;
	}

	dbus_g_proxy_call(mns_proxy, "RemoveSession", &error,
		DBUS_TYPE_G_OBJECT_PATH, g_mns_path,
		G_TYPE_INVALID, G_TYPE_INVALID);
	if (error) {
		DBG("Error [%s]", error->message);
		g_error_free(error);
		g_object_unref(mns_proxy);
		return;
	}

	g_free(g_mns_path);
	g_mns_path = NULL;

	g_object_unref(mns_proxy);

	DBG("-\n");
	return;
}

static gboolean bluetooth_map_get_folder_tree(BluetoothMapAgent *agent,
						DBusGMethodInvocation *context)
{
	GPtrArray *array = g_ptr_array_new();
	GValue value;
	GError *error = NULL;

	char name[BT_MAP_MSG_INFO_MAX] = {0,};
	char folder_name[BT_MAP_MSG_INFO_MAX] = {0,};
	int i;
	int ret;
	gboolean msg_ret = TRUE;

	msg_struct_list_s g_folderList;
	msg_struct_t p_folder;

#ifdef SUPPORT_EMAIL
	int j;
	int account_id = 0;
	int mailbox_count = 0;
	gboolean flag = FALSE;
	email_mailbox_t *mailbox_list = NULL;
#endif

	if (g_msg_handle == NULL) {
		msg_ret = FALSE;
		goto done;
	}

	if (msg_get_folder_list(g_msg_handle, &g_folderList) != MSG_SUCCESS) {
		msg_ret = FALSE;
		goto done;
	}

	for (i = 0; i < g_folderList.nCount; i++) {
		p_folder = g_folderList.msg_struct_info[i];
		memset(folder_name, 0x00, BT_MAP_MSG_INFO_MAX);

		ret = msg_get_str_value(p_folder, MSG_FOLDER_INFO_NAME_STR,
					folder_name, BT_MAP_MSG_INFO_MAX);
		if (ret != MSG_SUCCESS)
			continue;

		if (!g_ascii_strncasecmp(folder_name, BT_MAP_SENT_FOLDER_NAME,
					strlen(BT_MAP_SENT_FOLDER_NAME))) {
			memset(folder_name, 0, sizeof(folder_name));
			g_strlcpy(folder_name, BT_MAP_SENT_FOLDER_NAME,
							sizeof(folder_name));
		}

		g_strlcpy(name, folder_name, sizeof(name));
		memset(&value, 0, sizeof(GValue));
		g_value_init(&value, DBUS_STRUCT_STRING_STRING_UINT);
		g_value_take_boxed(&value, dbus_g_type_specialized_construct(
					DBUS_STRUCT_STRING_STRING_UINT));
		dbus_g_type_struct_set(&value, 0, name, G_MAXUINT);
		g_ptr_array_add(array, g_value_get_boxed(&value));
	}

#ifdef SUPPORT_EMAIL
email:
	if (EMAIL_ERROR_NONE != email_load_default_account_id(&account_id))
		goto done;

	if (EMAIL_ERROR_NONE != email_get_mailbox_list(account_id,
							EMAIL_MAILBOX_ALL,
							&mailbox_list,
							&mailbox_count)) {
		goto done;
	}

	msg_ret = TRUE;

	for (i = 0; i < mailbox_count; i++) {
		flag = FALSE;
		for (j = 0; j < g_folderList.nCount; j++) {

			p_folder = g_folderList.msg_struct_info[j];
			memset(folder_name, 0x00, BT_MAP_MSG_INFO_MAX);

			ret = msg_get_str_value(p_folder,
						MSG_FOLDER_INFO_NAME_STR,
						folder_name,
						BT_MAP_MSG_INFO_MAX);
			if (ret != MSG_SUCCESS)
				continue;

			if (!g_ascii_strncasecmp(mailbox_list[i].alias,
				folder_name, strlen(mailbox_list[i].alias))) {
				flag = TRUE;
				break;
			}
		}

		if (!flag) {
			g_strlcpy(name, mailbox_list[i].alias, sizeof(name));

			if (!g_ascii_strncasecmp(name, BT_MAP_SENT_FOLDER_NAME,
					strlen(BT_MAP_SENT_FOLDER_NAME)))
				continue;

			memset(&value, 0, sizeof(GValue));
			g_value_init(&value, DBUS_STRUCT_STRING_STRING_UINT);
			g_value_take_boxed(&value,
				dbus_g_type_specialized_construct(
				DBUS_STRUCT_STRING_STRING_UINT));
			dbus_g_type_struct_set(&value, 0, name, G_MAXUINT);
			g_ptr_array_add(array, g_value_get_boxed(&value));
		}
	}

	if (mailbox_list != NULL)
		 email_free_mailbox(&mailbox_list, mailbox_count);
#endif

done:

	if (msg_ret == FALSE) {
		g_ptr_array_free(array, TRUE);

		error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
						"InternalError");
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		return FALSE;
	} else {
		dbus_g_method_return(context, array);
		g_ptr_array_free(array, TRUE);
		return TRUE;
	}
}

static gboolean bluetooth_map_get_message_list(BluetoothMapAgent *agent,
						gchar *folder_name, guint16 max,
						DBusGMethodInvocation *context)
{
	GPtrArray *array = g_ptr_array_new();
	GValue value;
	GError *error = NULL;

	char *folder = NULL;
	int i = 0;
	int ret = 0;
	int folder_id = 0;
	int unread_cnt;
	guint64 count;
	gboolean newmsg;

	msg_struct_list_s g_folderList;
	msg_struct_list_s msg_list;
	msg_struct_t count_info;

#ifdef SUPPORT_EMAIL
	int total = 0;
	int account_id = 0;
	int mailbox_count = 0;
	int mail_count = 0;
	char *type = NULL;
	char msg_datetime[BT_MAP_TIMESTAMP_MAX_LEN] = {0,};
	email_mailbox_t *mailbox_list = NULL;
	email_mail_list_item_t *mail_list = NULL;
	email_list_filter_t *filter_list = NULL;
	email_list_sorting_rule_t *sorting_rule_list = NULL;
#endif

	if (g_msg_handle == NULL)
		goto fail;

	folder = strrchr(folder_name, '/');
	if (NULL == folder)
		folder = folder_name;
	else
		folder++;

	ret = msg_get_folder_list(g_msg_handle, &g_folderList);
	if (ret != MSG_SUCCESS)
		goto fail;

	for (i = 0; i < g_folderList.nCount; i++) {
		msg_struct_t pFolder = g_folderList.msg_struct_info[i];
		char folderName[BT_MAP_MSG_INFO_MAX] = {0, };

		ret = msg_get_str_value(pFolder, MSG_FOLDER_INFO_NAME_STR,
					folderName, BT_MAP_MSG_INFO_MAX);
		if (ret  != MSG_SUCCESS)
			continue;

		if (!g_ascii_strncasecmp(folderName, folder, strlen(folder))) {
			ret = msg_get_int_value(pFolder, MSG_FOLDER_INFO_ID_INT,
								&folder_id);
			if (ret != MSG_SUCCESS)
				goto fail;
			else
				DBG("folder_id %d \n", folder_id);

			break;
		}
	}

	ret = msg_get_folder_view_list(g_msg_handle, folder_id,
							NULL, &msg_list);
	if (ret  != MSG_SUCCESS)
		goto fail;

	count = msg_list.nCount;

	count_info = msg_create_struct(MSG_STRUCT_COUNT_INFO);
	ret = msg_count_message(g_msg_handle, folder_id, count_info);
	if (ret != MSG_SUCCESS) {
		msg_release_struct(&count_info);
		goto fail;
	}

	ret = msg_get_int_value(count_info, MSG_COUNT_INFO_UNREAD_INT,
								&unread_cnt);
	if (ret != MSG_SUCCESS) {
		msg_release_struct(&count_info);
		goto fail;
	}

	if (unread_cnt != 0)
		newmsg = TRUE;
	else
		newmsg = FALSE;

	msg_release_struct(&count_info);

	DBG("MaxlistCount %d \n", max);
	if (max == 0)
		goto done;

	for (i = 0; i < msg_list.nCount; i++) {

		struct message_info msg_info;

		memset(&value, 0, sizeof(GValue));
		g_value_init(&value, DBUS_STRUCT_MESSAGE_LIST);
		g_value_take_boxed(&value, dbus_g_type_specialized_construct(
						DBUS_STRUCT_MESSAGE_LIST));

		msg_info = __bt_message_info_get(msg_list.msg_struct_info[i]);

/* Keeping the bleow debug till stabilization is done. */

/*
	DBG("msg_info.handle = %s\n", msg_info.handle);
	DBG("msg_info.subject = %s\n", msg_info.subject);
	DBG("msg_info.datetime = %s\n", msg_info.datetime);
	DBG("msg_info.sender_name = %s\n", msg_info.sender_name);
	DBG("msg_info.sender_addressing = %s\n", msg_info.sender_addressing);
	DBG("msg_info.replyto_addressing = %s\n", msg_info.replyto_addressing);
	DBG("msg_info.recipient_name = %s\n", msg_info.recipient_name);
	DBG("msg_info.recipient_addressing = %s\n",
					msg_info.recipient_addressing);
	DBG("msg_info.type = %s\n", msg_info.type);
	DBG("msg_info.reception_status = %s\n", msg_info.reception_status);
	DBG("msg_info.size = %s\n", msg_info.size);
	DBG("msg_info.attachment_size = %s\n", msg_info.attachment_size);
	DBG("msg_info.text = %d\n", msg_info.text);
	DBG("msg_info.read = %d\n", msg_info.read);
	DBG("msg_info.sent = %d\n", msg_info.sent);
	DBG("msg_info.protect = %d\n", msg_info.protect);
	DBG("msg_info.priority = %d\n", msg_info.priority);
*/

		dbus_g_type_struct_set(&value, 0, msg_info.handle,
					1, msg_info.subject,
					2, msg_info.datetime,
					3, msg_info.sender_name,
					4, msg_info.sender_addressing,
					5, msg_info.recipient_name,
					6, msg_info.recipient_addressing,
					7, msg_info.type,
					8, msg_info.size,
					9, msg_info.reception_status,
					10, msg_info.text,
					11, msg_info.attachment_size,
					12, msg_info.priority,
					13, msg_info.read,
					14, msg_info.sent,
					15, msg_info.protect,
					16, msg_info.replyto_addressing, G_MAXUINT);
		g_ptr_array_add(array, g_value_get_boxed(&value));

		__bt_message_info_free(msg_info);
	}

#ifdef SUPPORT_EMAIL
email:
	if (EMAIL_ERROR_NONE != email_load_default_account_id(&account_id)) {
		if (!msg_ret)
			goto fail;
	}

	if (EMAIL_ERROR_NONE != email_get_mailbox_list(account_id,
							EMAIL_MAILBOX_ALL,
							&mailbox_list,
							&mailbox_count)) {
		if (!msg_ret)
			goto fail;
	}

	if (mailbox_list == NULL)
		goto fail;

	for (i = 0; i < mailbox_count; i++) {
		DBG("mailbox alias = %s \n", mailbox_list[i].alias);
		if (!g_ascii_strncasecmp(mailbox_list[i].alias, folder,
			strlen(folder))) {
			total = mailbox_list[i].total_mail_count_on_server;
			DBG("Total mail on sever:%d\n", total);
			DBG("mailbox name:%s\n", mailbox_list[i].mailbox_name);

			break;
		}

		if (!msg_ret)
			goto fail;
		else
			goto done;
	}

	/* Need to modify the filter code, have to make it dynamic
	   based on remote device request Also to check whether it needs
	   to be done in agent or in obexd */

	filter_list = g_new0(email_list_filter_t, 3);
	filter_list[0].list_filter_item_type = EMAIL_LIST_FILTER_ITEM_RULE;
	filter_list[0].list_filter_item.rule.target_attribute =
						EMAIL_MAIL_ATTRIBUTE_ACCOUNT_ID;
	filter_list[0].list_filter_item.rule.rule_type =
						EMAIL_LIST_FILTER_RULE_EQUAL;
	filter_list[0].list_filter_item.rule.key_value.integer_type_value =
								account_id;

	filter_list[1].list_filter_item_type = EMAIL_LIST_FILTER_ITEM_OPERATOR;
	filter_list[1].list_filter_item.operator_type =
						EMAIL_LIST_FILTER_OPERATOR_AND;

	filter_list[2].list_filter_item_type = EMAIL_LIST_FILTER_ITEM_RULE;
	filter_list[2].list_filter_item.rule.target_attribute =
					EMAIL_MAIL_ATTRIBUTE_MAILBOX_NAME;
	filter_list[2].list_filter_item.rule.rule_type =
						EMAIL_LIST_FILTER_RULE_EQUAL;
	type = g_strdup(mailbox_list[i].mailbox_name);
	filter_list[2].list_filter_item.rule.key_value.string_type_value = type;
	filter_list[2].list_filter_item.rule.case_sensitivity = true;

	sorting_rule_list = g_new0(email_list_sorting_rule_t, 1);
	sorting_rule_list->target_attribute = EMAIL_MAIL_ATTRIBUTE_DATE_TIME;
	sorting_rule_list->sort_order = EMAIL_SORT_ORDER_ASCEND;

	ret = email_get_mail_list_ex(filter_list, 3,
					sorting_rule_list, 1, 0, total - 1,
					&mail_list, &mail_count);

	DBG("email API ret %d  \n", ret);
	if (ret != EMAIL_ERROR_NONE) {
		if (!msg_ret) {
			g_free(type);
			g_free(filter_list);
			g_free(sorting_rule_list);
			goto fail;
		} else
			goto done;
	}

	for (i = 0; i < mail_count; ++i) {
		time_t time = {0,};
		memset(&value, 0, sizeof(GValue));
		g_value_init(&value, DBUS_STRUCT_MESSAGE_LIST);
		g_value_take_boxed(&value, dbus_g_type_specialized_construct(
					DBUS_STRUCT_MESSAGE_LIST));

		uid = __bt_add_id(mail_list[i].mail_id);
		snprintf(msg_handle, sizeof(msg_handle), "%llx", uid);

		g_strlcpy(msg_type,  "EMAIL", sizeof(msg_type));

		time = mail_list[i].date_time;
		__get_msg_timestamp(&time, msg_datetime);

		dbus_g_type_struct_set(&value, 0, msg_handle, 1, msg_type,
					2, msg_datetime, G_MAXUINT);
		g_ptr_array_add(array, g_value_get_boxed(&value));
	}

	if (mailbox_list != NULL)
		 email_free_mailbox(&mailbox_list, mailbox_count);
	if (mail_list != NULL)
		g_free(mail_list);

	g_free(filter_list);
	g_free(sorting_rule_list);
	g_free(type);
#endif

done:
	DBG("Request completed \n");
	dbus_g_method_return(context, newmsg, count, array);
	g_ptr_array_free(array, TRUE);
	DBG("Request completed successfully \n");
	return TRUE;

fail:
	g_ptr_array_free(array, TRUE);
	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							  "InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	return FALSE;
}

static gboolean bluetooth_map_get_message(BluetoothMapAgent *agent,
						gchar *message_name,
						gboolean attach,
						gboolean transcode,
						gboolean first_request,
						DBusGMethodInvocation *context)
{
	DBG("+ \n");
	char *buf = NULL;
	int message_id = 0;
	int msg_type = BT_SMS;

	GError *error = NULL;

	msg_struct_t msg = NULL;
	msg_struct_t send_opt = NULL;
#ifdef SUPPORT_EMAIL
	email_mail_data_t *mail_data = NULL;
#endif
	message_id = __bt_get_uid(message_name);
	if (message_id == -1)
		goto fail;

	DBG("message_id %d \n", message_id);
	DBG("msg_type %d \n", msg_type);
	DBG("attach %d \n", attach);
	DBG("transcode %d \n", transcode);
	DBG("first_request %d \n", first_request);

	if (msg_type == BT_SMS) {
		if (g_msg_handle == NULL)
			goto fail;

		msg_error_t msg_err;

		msg = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
		send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);

		msg_err = msg_get_message(g_msg_handle,
						(msg_message_id_t)message_id,
						msg, send_opt);
		if (msg_err != MSG_SUCCESS)
			goto fail;

		buf = __bt_prepare_msg_bmseg(msg, attach, transcode);

		msg_release_struct(&msg);
		msg_release_struct(&send_opt);
#ifdef SUPPORT_EMAIL
	} else if (msg_type == BT_EMAIL) {

		FILE *body_file;
		int account_id;
		long read_size;
		long email_size;

		if (EMAIL_ERROR_NONE !=
				email_load_default_account_id(&account_id))
			goto fail;

		if (EMAIL_ERROR_NONE !=
				email_get_mail_data(message_id, &mail_data))
			goto fail;

		body_file = fopen(mail_data->file_path_plain, "r");
		if (body_file == NULL)
			body_file = fopen(mail_data->file_path_html, "rb");

		if (body_file != NULL) {
			fseek(body_file , 0, SEEK_END);
			email_size = ftell(body_file);
			rewind(body_file);

			buf = (char *)g_malloc0(sizeof(char) * email_size);

			read_size = fread(buf, 1, email_size, body_file);

			fclose(body_file);

			if (read_size != email_size)
				goto fail;
		} else
			buf = (char *)g_strdup("");

		email_free_mail_data(&mail_data, 1);
#endif
	} else {
		DBG("msg_type not supported %d \n", msg_type);
		goto fail;
	}

	dbus_g_method_return(context, FALSE, buf);
	g_free(buf);

	DBG("- \n");
	return TRUE;

fail:
	g_free(buf);

	if (msg)
		msg_release_struct(&msg);

	if (send_opt)
		msg_release_struct(&send_opt);

#ifdef SUPPORT_EMAIL
	if (mail_data)
		email_free_mail_data(&mail_data, 1);
#endif

	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	return FALSE;
}

static gboolean bluetooth_map_push_message(BluetoothMapAgent *agent,
					gboolean save_copy,
					gboolean retry_send,
					gboolean native,
					gchar *folder_name,
					DBusGMethodInvocation *context)
{
	DBG("+\n");
	GError *error = NULL;
	guint64 handle = 0;
	int folder_id;

	DBG("folder_name = %s\n", folder_name);

	folder_id = __bt_get_folder_id(folder_name);
	if (folder_id == -1)
		goto fail;

	handle = __bt_add_id(-1);
	current_push_map_id = handle;

	/* FALSE : Keep messages in Sent folder */
	/* TRUE : don't keep messages in sent folder */
	opt.save_copy = save_copy;
	DBG("opt.save_copy = %d\n", opt.save_copy);

	/* FALSE : don't retry */
	/* TRUE  : retry */
	opt.retry_send = retry_send;
	DBG("opt.retry_send = %d\n", opt.retry_send);

	/* FALSE : native */
	/* TRUE : UTF-8 */
	opt.native = native;
	DBG("opt.native = %d\n", opt.native);

	dbus_g_method_return(context, handle);
	DBG("-\n");
	return TRUE;
fail:
	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
						"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);

	return FALSE;
}

static gboolean bluetooth_map_push_message_data(BluetoothMapAgent *agent,
					gchar *bmsg,
					DBusGMethodInvocation *context)
{
	DBG("+\n");
	int id = -1;
	int folder_id;
	char *folder = NULL;
	char *body = NULL;
	GSList *recepients = NULL;
	gboolean send = FALSE;

	GError *error = NULL;

	DBG("BMSG is \n %s", bmsg);

	struct bmsg_data *bmsg_info = NULL;

	bmsg_info = bmsg_parse(bmsg);
	if (!bmsg_info)
		goto done;

	folder = bmsg_get_msg_folder(bmsg_info);
	if (folder == NULL)
		goto done;

	folder_id = __bt_get_folder_id(bmsg_info->folder);
	if (folder_id == -1)
		goto done;

	if (MSG_OUTBOX_ID == folder_id)
		send = TRUE;

	body = bmsg_get_msg_body(bmsg_info);
	if (body == NULL)
		goto done;

	recepients = bmsg_get_msg_recepients(bmsg_info);

	id = __bt_push_sms(send, folder_id, body, recepients);
	if (id == -1)
		goto done;

	__bt_update_id(current_push_map_id, id);

done:
	g_free(folder);
	g_free(body);
	g_slist_free(recepients);
	g_free(bmsg_info);

	if (id == -1) {
		error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		DBG("-\n");
		return FALSE;
	}

	dbus_g_method_return(context);
	DBG("-\n");
	return TRUE;
}

static gboolean bluetooth_map_update_message(BluetoothMapAgent *agent,
						DBusGMethodInvocation *context)
{
	int err = TRUE;
#ifdef SUPPORT_EMAIL
	int handle;
	err = email_sync_header_for_all_account(&handle);

	if (err == EMAIL_ERROR_NONE) {
		DBG("Handle to stop download = %d \n", handle);
	} else {
		ERR("Message Update failed \n");
	}

	dbus_g_method_return(context, err);
	return (err == EMAIL_ERROR_NONE) ? TRUE : FALSE;
#else
	dbus_g_method_return(context, err);
	return TRUE;
#endif
}

static gboolean bluetooth_map_set_read_status(BluetoothMapAgent *agent,
						gchar *handle,
						gboolean read_status,
						DBusGMethodInvocation *context)
{
	int message_id = 0;
	int msg_type = BT_SMS;
#ifdef SUPPORT_EMAIL
	email_mail_data_t *mail_data = NULL;
#endif
	GError *error = NULL;

	DBG("+\n");

	message_id = __bt_get_uid(handle);
	if (message_id == -1)
		goto fail;

	DBG("message_id = %d,  read_status = %d\n", message_id, read_status);

	if (msg_type == BT_SMS) {
		msg_error_t msg_err;
		msg_struct_t msg = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
		msg_struct_t send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);
		int msg_type = 0;

		msg_err = msg_get_message(g_msg_handle,
						(msg_message_id_t)message_id,
						msg, send_opt);
		if (msg_err != MSG_SUCCESS) {
			msg_release_struct(&msg);
			msg_release_struct(&send_opt);
			goto fail;
		}

		msg_err = msg_get_int_value(msg, MSG_MESSAGE_TYPE_INT,
								&msg_type);
		if (msg_err != MSG_SUCCESS) {
			msg_release_struct(&msg);
			msg_release_struct(&send_opt);
			goto fail;
		}

		msg_err = msg_update_read_status(g_msg_handle, message_id,
								read_status);
		if (msg_err != MSG_SUCCESS) {
			msg_release_struct(&msg);
			msg_release_struct(&send_opt);
			goto fail;
		}

		if (__bt_msg_is_mms(msg_type)) {
			if (read_status == TRUE)
				msg_err = msg_mms_send_read_report(g_msg_handle,
						message_id,
						MSG_READ_REPORT_IS_READ);
			else
				msg_err = msg_mms_send_read_report(g_msg_handle,
						message_id,
						MSG_READ_REPORT_NONE);
		}

		msg_release_struct(&msg);
		msg_release_struct(&send_opt);

		if (msg_err != MSG_SUCCESS)
			goto fail;
#ifdef SUPPORT_EMAIL
	} else if (msg_type == BT_EMAIL) {

		if (email_get_mail_data(message_id, &mail_data) !=
							EMAIL_ERROR_NONE) {
			ERR("email_get_mail_data failed\n");
			goto fail;
		}

		if (email_set_flags_field(mail_data->account_id, &message_id, 1,
			EMAIL_FLAGS_SEEN_FIELD, read_status, 0) !=
							EMAIL_ERROR_NONE) {
			email_free_mail_data(&mail_data, 1);
			goto fail;
		}

		email_free_mail_data(&mail_data, 1);
	} else {
		goto fail;
	}
#else
	}
#endif

	dbus_g_method_return(context);
	DBG("-\n");
	return TRUE;

fail:
	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);

	return FALSE;
}

static gboolean bluetooth_map_set_delete_status(BluetoothMapAgent *agent,
						gchar *handle,
						gboolean delete_status,
						DBusGMethodInvocation *context)
{
	int message_id = 0;
	int msg_type = BT_SMS;
#ifdef SUPPORT_EMAIL
	email_mail_data_t *mail_data = NULL;
#endif
	GError *error = NULL;

	DBG("+\n");

	message_id = __bt_get_uid(handle);
	if (message_id == -1)
		goto fail;

	DBG("message_id = %d, delete_status = %d\n", message_id, delete_status);

	if (msg_type == BT_SMS) {
		if (msg_delete_message(g_msg_handle, message_id) !=
								MSG_SUCCESS) {
			goto fail;
		}
#ifdef SUPPORT_EMAIL
	} else if (msg_type == BT_EMAIL) {

		if (email_get_mail_data(message_id, &mail_data) !=
							EMAIL_ERROR_NONE)
			goto fail;

		if (email_delete_mail(mail_data->mailbox_id, &message_id,
						1, 1) != EMAIL_ERROR_NONE) {
			email_free_mail_data(&mail_data, 1);
			goto fail;
		}

		email_free_mail_data(&mail_data, 1);
	} else {
		goto fail;
	}
#else
	}
#endif

	dbus_g_method_return(context);
	DBG("-\n");
	return TRUE;

fail:
	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	return FALSE;
}

static gboolean bluetooth_map_noti_registration(BluetoothMapAgent *agent,
						gchar *remote_addr,
						gboolean status,
						DBusGMethodInvocation *context)
{
	DBG("remote_addr = %s \n", remote_addr);

	if (status == TRUE)
		__bt_mns_client_connect(remote_addr);
	else
		__bt_mns_client_disconnect();

	return TRUE;
}

int main(int argc, char **argv)
{
	BluetoothMapAgent *bluetooth_map_obj = NULL;
	DBusGProxy *bus_proxy = NULL;
	guint result = 0;
	GError *error = NULL;

	g_type_init();

	g_mainloop = g_main_loop_new(NULL, FALSE);

	if (g_mainloop == NULL) {
		ERR("Couldn't create GMainLoop\n");
		return EXIT_FAILURE;
	}

	g_connection = dbus_g_bus_get(DBUS_BUS_SESSION, &error);

	if (error != NULL) {
		ERR("Couldn't connect to system bus[%s]\n", error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}

	bus_proxy = dbus_g_proxy_new_for_name(g_connection, DBUS_SERVICE_DBUS,
						DBUS_PATH_DBUS,
						DBUS_INTERFACE_DBUS);
	if (bus_proxy == NULL) {
		ERR("Failed to get a proxy for D-Bus\n");
		goto failure;
	}

	if (!dbus_g_proxy_call(bus_proxy, "RequestName", &error, G_TYPE_STRING,
					BT_MAP_SERVICE_NAME, G_TYPE_UINT, 0,
					G_TYPE_INVALID, G_TYPE_UINT, &result,
					G_TYPE_INVALID)) {
		if (error != NULL) {
			ERR("RequestName RPC failed[%s]\n", error->message);
			g_error_free(error);
		}
		goto failure;
	}
	DBG("result : %d %d\n", result, DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		ERR("Failed to get the primary well-known name.\n");
		goto failure;
	}

	g_object_unref(bus_proxy);
	bus_proxy = NULL;

	bluetooth_map_obj = g_object_new(BLUETOOTH_MAP_TYPE_AGENT, NULL);
	if (bluetooth_map_obj == NULL) {
		ERR("Failed to create one BluetoothMapAgent instance.\n");
		goto failure;
	}

	/* Registering it on the D-Bus */
	dbus_g_connection_register_g_object(g_connection,
						BT_MAP_SERVICE_OBJECT_PATH,
						G_OBJECT(bluetooth_map_obj));

	if (__bluetooth_map_start_service() == FALSE)
		goto failure;

	g_main_loop_run(g_mainloop);

 failure:
	DBG("Terminate the bluetooth-map-agent\n");

	__bt_remove_list(id_list);

	if (g_mns_path)
		__bt_mns_client_disconnect();
	if (bus_proxy)
		g_object_unref(bus_proxy);
	if (bluetooth_map_obj)
		g_object_unref(bluetooth_map_obj);
	if (g_connection)
		dbus_g_connection_unref(g_connection);


	__bluetooth_map_stop_service();
	return EXIT_FAILURE;
}
