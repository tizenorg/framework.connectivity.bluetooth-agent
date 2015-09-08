/*
 * Bluetooth-agent
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:  Hocheol Seo <hocheol.seo@samsung.com>
 *		 Girishashok Joshi <girish.joshi@samsung.com>
 *		 Chanyeol Park <chanyeol.park@samsung.com>
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

#include <TelSms.h>
#include <TapiUtility.h>
#include <ITapiNetText.h>
#include <bluetooth_map_agent.h>
#include <map_bmessage.h>

#define OBEX_CLIENT_SERVICE "org.bluez.obex"
#define OBEX_CLIENT_INTERFACE "org.bluez.obex.Client1"
#define OBEX_CLIENT_PATH "/org/bluez/obex"

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
static TapiHandle *g_tapi_handle = NULL;
static TelSmsAddressInfo_t *g_sca_info = NULL;
static DBusGProxy *g_mns_proxy;

#define BT_MAP_NEW_MESSAGE "NewMessage"
#define BT_MAP_STATUS_CB "sent status callback"
#define BT_MAP_MSG_CB "sms message callback"
#define BT_MNS_OBJECT_PATH "/org/bluez/mns"
#define BT_MNS_INTERFACE "org.bluez.mns"
#define BT_MAP_SENT_FOLDER_NAME "SENT"
#define BT_MAP_MSG_TEMPLATE "TEMPLATE"
#define BT_MAP_DELETED_FOLDER_NAME "DELETED"
#define BT_MAP_MSG_INFO_MAX 256
#define BT_MAP_MSG_HANDLE_MAX 21
#define BT_MAP_TIMESTAMP_MAX_LEN 16
#define BT_MAP_SUBJECT_MAX_LEN 50
#define BT_MAP_MSG_BODY_MAX 1024
#define BT_MSG_UPDATE	0
#define BT_MSG_DELETE	1
#define BT_SMS 0

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
#define MSG_BODY_BEGIN "BEGIN:MSG\r\n"
#define MSG_BODY_END "\r\nEND:MSG\r\n"

GSList *id_list = NULL;
guint64 current_push_map_id;

typedef enum {
	SMS_TON_UNKNOWN = 0,		/* unknown */
	SMS_TON_INTERNATIONAL = 1,	/* international number */
	SMS_TON_NATIONAL = 2,		/* national number */
	SMS_TON_NETWORK_SPECIFIC = 3, /* network specific number */
	SMS_TON_DEDICATED_ACCESS = 4, /* subscriber number */
	SMS_TON_ALPHA_NUMERIC = 5,	/* alphanumeric, GSM 7-bit default */
	SMS_TON_ABBREVIATED_NUMBER = 6, /* abbreviated number */
	SMS_TON_RESERVED_FOR_EXT = 7 /* reserved for extension */
} bt_sim_type_of_num_t;

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
static gboolean bluetooth_map_destroy_agent(BluetoothMapAgent *agent,
					DBusGMethodInvocation *context);

#include "bluetooth_map_agent_glue.h"

static void bluetooth_map_agent_init(BluetoothMapAgent *obj)
{
	FN_START;
	g_assert(obj != NULL);
	FN_END;
}

static void bluetooth_map_agent_finalize(GObject *obj)
{
	FN_START;
	G_OBJECT_CLASS(bluetooth_map_agent_parent_class)->finalize(obj);
	FN_END;
}

static void bluetooth_map_agent_class_init(BluetoothMapAgentClass *klass)
{
	FN_START;
	GObjectClass *object_class = (GObjectClass *) klass;

	g_assert(klass != NULL);

	object_class->finalize = bluetooth_map_agent_finalize;

	dbus_g_object_type_install_info(BLUETOOTH_MAP_TYPE_AGENT,
					&dbus_glib_bluetooth_map_object_info);
	FN_END;
}

static GQuark __bt_map_agent_error_quark(void)
{
	FN_START;
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string("agent");

	FN_END;
	return quark;
}

static GError *__bt_map_agent_error(bt_map_agent_error_t error,
				     const char *err_msg)
{
	FN_START;
	return g_error_new(BT_MAP_AGENT_ERROR, error, err_msg, NULL);
}

static void __bt_mns_client_event_notify(gchar *event, guint64 handle,
					gchar *folder, gchar *old_folder,
					gchar *msg_type);

static char *__bt_get_truncated_utf8_string(char *src)
{
	FN_START;
	char *p = src;
	char *next;
	char dest[BT_MAP_SUBJECT_MAX_LEN] = {0,};
	int count;
	int i = 0;

	if (src == NULL)
		return FALSE;

	while (*p != '\0' && i < sizeof(dest)) {
		next = g_utf8_next_char(p);
		count = next - p;

		while (count > 0 && ((i + count) < sizeof(dest))) {
			dest[i++] = *p;
			p++;
			count --;
		}
		p = next;
	}

	FN_END;
	return g_strdup(dest);
}

static guint64 __bt_validate_uid(int uid)
{
	FN_START;
	struct id_info *info;
	int count;
	int i;

	count = g_slist_length(id_list);
	for (i = 0; i < count; i++) {
		info = (struct id_info *)g_slist_nth_data(id_list, i);
		if (!info)
			break;

		if (info->uid == uid) {
			DBG("uid = %d\n", uid);
			return info->map_id;
		}
	}

	FN_END;
	return 0;
}

static guint64 __bt_add_id(int uid)
{
	FN_START;
	static guint64 map_id;
	struct id_info *info;
	guint64 test;

	DBG("Add id: %d\n", uid);
	test = __bt_validate_uid(uid);
	DBG("test: %llx\n", test);
	if (test)
		return test;

	info = g_new0(struct id_info, 1);

	map_id++;

	info->map_id = map_id;
	info->uid = uid;
	DBG("map_id = %llx, uid = %d \n", info->map_id, info->uid);

	id_list = g_slist_append(id_list, info);

	FN_END;
	return map_id;
}

static int __bt_get_id(guint64 map_id)
{
	FN_START;
	struct id_info *info;
	int count;
	int i;

	count = g_slist_length(id_list);

	for (i = 0; i < count; i++) {
		info = (struct id_info *)g_slist_nth_data(id_list, i);

		if (info->map_id == map_id)
			return info->uid;
	}

	FN_END;
	return -1;
}

static int __bt_get_uid(gchar *handle)
{
	FN_START;
	guint64 map_id;
	int uid;

	if (NULL == handle)
		return -1;

	map_id = g_ascii_strtoull(handle, NULL, 16);
	if (!map_id)
		return -1;

	uid = __bt_get_id(map_id);

	FN_END;
	return uid;
}

static int __bt_update_id(guint64 map_id, int new_uid)
{
	FN_START;
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

	FN_END;
	return -1;
}

static void __bt_remove_list(GSList *id_list)
{
	FN_START;
	if (!id_list)
		return;

	DBG("Removing id list\n");
	g_slist_free_full(id_list, g_free);
	FN_END;
}

static int __bt_get_folder_id(char *folder_path)
{
	FN_START;
	int folder_id = -1;
	int i;
	char *folder;
	msg_struct_list_s folder_list = {0,};
	msg_error_t err;
	msg_struct_t p_folder;
	DBG_SECURE("folder_path %s\n", folder_path);

	folder = strrchr(folder_path, '/');
	if (NULL == folder)
		folder = folder_path;
	else
		folder++;

	err = msg_get_folder_list(g_msg_handle, &folder_list);
	if (err != MSG_SUCCESS)
		goto done;

	for (i = 0; i < folder_list.nCount; i++) {
		char folder_name[BT_MAP_MSG_INFO_MAX] = {0, };

		p_folder = folder_list.msg_struct_info[i];

		err = msg_get_str_value(p_folder, MSG_FOLDER_INFO_NAME_STR,
					folder_name, BT_MAP_MSG_INFO_MAX);
		if (err != MSG_SUCCESS)
			continue;

		DBG_SECURE("folder_name %s\n", folder_name);
		if (!g_ascii_strncasecmp(folder_name, folder, strlen(folder))) {
			err = msg_get_int_value(p_folder,
					MSG_FOLDER_INFO_ID_INT,
					&folder_id);
			if (err != MSG_SUCCESS)
				goto done;

			DBG("folder_id %d", folder_id);
			break;
		}
	}

done:
	if (folder_list.msg_struct_info)
		msg_release_list_struct(&folder_list);

	FN_END;
	return folder_id;

}

static void __bt_add_deleted_folder(void)
{
	FN_START;
	msg_error_t err;
	msg_struct_t folder_info = msg_create_struct(MSG_STRUCT_FOLDER_INFO);

	err = msg_set_int_value(folder_info, MSG_FOLDER_INFO_TYPE_INT,
						MSG_FOLDER_TYPE_USER_DEF);
	if (err != MSG_SUCCESS) {
		ERR("Failed adding type %d", err);
		msg_release_struct(&folder_info);
		return;
	}

	err = msg_set_str_value(folder_info, MSG_FOLDER_INFO_NAME_STR,
					"DELETED", MAX_FOLDER_NAME_SIZE);
	if (err != MSG_SUCCESS) {
		ERR("Failed adding str %d", err);
		msg_release_struct(&folder_info);
		return;
	}

	err = msg_add_folder(g_msg_handle, folder_info);
	if (err != MSG_SUCCESS) {
		ERR("Failed adding folder %d", err);
		msg_release_struct(&folder_info);
		return;
	}

	msg_release_struct(&folder_info);
	FN_END;
}

static gchar *__bt_get_folder_name(int id)
{
	FN_START;
	int ret;
	int i;
	int folder_id;
	gboolean path_found = FALSE;
	char folder_name[BT_MAP_MSG_INFO_MAX] = {0,};

	msg_struct_list_s folder_list = {0,};
	msg_struct_t p_folder;

	ret = msg_get_folder_list(g_msg_handle, &folder_list);
	if (ret != MSG_SUCCESS)
		return g_strdup("TELECOM/MSG");

	if (folder_list.msg_struct_info == NULL)
		return g_strdup("TELECOM/MSG");

	for (i = 0; i < folder_list.nCount; i++) {
		p_folder = folder_list.msg_struct_info[i];

		ret = msg_get_int_value(p_folder,
					MSG_FOLDER_INFO_ID_INT,
					&folder_id);
		if (ret != MSG_SUCCESS)
			break;
		DBG("folder_id %d, id = %d", folder_id, id);
		if (folder_id == id) {
			ret = msg_get_str_value(p_folder,
					MSG_FOLDER_INFO_NAME_STR,
					folder_name, BT_MAP_MSG_INFO_MAX);
			if (ret != MSG_SUCCESS)
				break;

			path_found = TRUE;
			DBG_SECURE("folder_name %s", folder_name);
			break;
		}
	}

	if (folder_list.msg_struct_info) {
		ret = msg_release_list_struct(&folder_list);
		ERR("Err %d", ret);
	}

	FN_END;
	if (path_found != TRUE)
		return g_strdup("TELECOM/MSG");
	else
		return g_strdup_printf("TELECOM/MSG/%s", folder_name);
}

static void __get_msg_timestamp(time_t *ltime, char *timestamp)
{
	FN_START;
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

	FN_END;
	return;
}

#define SET_TON_NPI(dest, ton, npi) {	\
	dest = 0x80;			\
	dest |= (ton & 0x07) << 4;	\
	dest |= npi & 0x0F;		\
}

static int __bt_ascii_to_upper(int ch)
{
	return (('a' <= (ch) && (ch) <= 'z') ? ((ch) - ('a'-'A')) : (ch));
}

static int __bt_sms_pack_gsm_code(gchar *p_out, const char *data, int in_len)
{
	FN_START;
	int i;
	int pos;
	int shift = 0;

	for (pos = 0, i = 0; i < in_len; pos++, i++) {
		/* pack the low bits */
		p_out[pos] = data[i] >> shift;

		if (i + 1 < in_len) {
			/* pack the high bits using the low bits
			   of the next character */
			p_out[pos] |= data[i+1] << (7 - shift);

			shift++;

			if (shift == 7) {
				shift = 0;
				i++;
			}
		}
	}

	FN_END;
	return pos;
}

static void __bt_sms_conv_digit_to_bcd(gchar *p_bcd, char *p_digits, int digit_len)
{
	FN_START;
	int i;
	int j;
	int digit;
	unsigned char higher;
	unsigned char lower;

	if (p_bcd == NULL || p_digits == NULL)
		return;

	/* 0123456789 -> 1032547698 */
	for (i = 0, j = 0; i < digit_len; i = i + 2, j++) {
		if (p_digits[i] == '*')
			digit = 0x0A;
		else if (p_digits[i] == '#')
			digit = 0x0B;
		else if (__bt_ascii_to_upper(p_digits[i]) == 'P')
			digit = 0x0C;
		else
			digit = (int) (p_digits[i] - '0');

		lower = digit & 0x0F;

		if (digit_len != i + 1) {
			if (p_digits[i+1] == '*')
				digit = 0x0A;
			else if (p_digits[i+1] == '#')
				digit = 0x0B;
			else if (__bt_ascii_to_upper(p_digits[i+1]) == 'P')
				digit = 0x0C;
			else
				digit = (int) (p_digits[i+1] - '0');

			higher = digit & 0x0F;
		} else {
			higher = 0xFF;
		}

		p_bcd[j] = (higher << 4) | lower;
	}
	FN_END;
}

static int  __bt_sms_encode_addr(gchar *addr_field, char *dial_num,
				int dial_num_len, int ton, int npi)
{
	FN_START;
	int index = 0;

	if (dial_num == NULL || addr_field == NULL)
		return -1;

	if (dial_num[0] == '+') {
		dial_num++;
		dial_num_len--;
		ton = SMS_TON_INTERNATIONAL;
	}

	if (ton != SMS_TON_ALPHA_NUMERIC) {
		/* Origination address length address length */
		addr_field[index++] = (unsigned char)dial_num_len;
	} else {
		addr_field[index] = (unsigned char)
					(((dial_num_len * 7 + 7) / 8) * 2);

		if (((dial_num_len * 7) % 8) <= 4)
			addr_field[index]--;

		index++;
	}

	SET_TON_NPI(addr_field[index], ton, npi);
	index++; /* SET_TON_NPI */

	if (ton != SMS_TON_ALPHA_NUMERIC) {
		__bt_sms_conv_digit_to_bcd(&addr_field[index],
					(char *)dial_num, dial_num_len);

		if (dial_num_len % 2)
			index += (dial_num_len / 2) + 1;
		else
			index += dial_num_len / 2;
	} else {
		index += __bt_sms_pack_gsm_code(&addr_field[index],
						dial_num, (int)dial_num_len);
	}

	FN_END;
	return index;
}

static int __bt_sms_encode_time(gchar *addr_field, time_t *tm)
{
	FN_START;
	int index = 0;
	struct tm ltime;
	int year;
	int month;

	if (!localtime_r(tm, &ltime))
		return index;

	year = ltime.tm_year + 1900; /* years since 1900 */
	year = year % 100;
	month = ltime.tm_mon + 1; /* months since January */

	addr_field[index++] = ((year % 10)  << 4) + (year / 10);
	addr_field[index++] = ((month % 10) << 4) + (month / 10);
	addr_field[index++] = ((ltime.tm_mday % 10) << 4) +
							(ltime.tm_mday / 10);
	addr_field[index++] = ((ltime.tm_hour % 10) << 4) +
							(ltime.tm_hour / 10);
	addr_field[index++] = ((ltime.tm_min % 10) << 4) + (ltime.tm_min / 10);
	addr_field[index++] = ((ltime.tm_sec % 10) << 4) + (ltime.tm_sec / 10);
	addr_field[index] = 0x00;

	FN_END;
	return index;
}

static gchar *__bt_get_sms_pdu_from_msg_data(gchar *number,
						char *msg, time_t tm,
						int *msg_pdu_len)
{
	FN_START;
	gchar packet[TAPI_NETTEXT_MSG_SIZE_MAX] = {0,};
	int index = 0;

	packet[index] = 0x00; /* Since SCA is unknown for stored messages */
	index++;

	/* TP-MTI : Type of message */
	packet[index] = 0x00;	/* SMS-DELIVER PDU */

	/* TP-MMS bit is set to 1 as we support only SMS */
	packet[index] |= 0x04;
	index++;

	/* TP-OA : Mobile originating address */
	index += __bt_sms_encode_addr(packet+index,
					number, strlen(number),
					g_sca_info->Ton, g_sca_info->Npi);

	/* TP-PID : Since we use only SMS so set to 0 */
	packet[index++] = 0x00;

	/* TP-DCS : Data Coding Scheme, default value set */
	packet[index++] = 0x00;

	/* TP-SCTS : Message timestamp */
	index += __bt_sms_encode_time(packet+index, &tm);
	index++;
	/* TP-UDL : Message body length */
	packet[index++] = strlen(msg);

	/* TP-UD : Message body */
	index += __bt_sms_pack_gsm_code(packet + index, msg, strlen(msg));

	*msg_pdu_len = index;

	FN_END;
	return g_memdup(packet, index);
}

static void __bt_get_sms_sca(TapiHandle *handle, int result, void *data,
							void *user_data)
{
	FN_START;
	TelSmsAddressInfo_t *scaInfo = data;

	DBG("__bt_get_sms_sca 0x%x", result);

	if (data == NULL) {
		g_sca_info = g_malloc0(sizeof(TelSmsAddressInfo_t));
		g_sca_info->Ton = 0;
		g_sca_info->Npi = 0;
		g_sca_info->DialNumLen = 0;
		return;
	}

	g_sca_info = g_malloc0(sizeof(TelSmsAddressInfo_t));
	g_sca_info->Ton = scaInfo->Ton;
	g_sca_info->Npi = scaInfo->Npi;
	g_sca_info->DialNumLen = scaInfo->DialNumLen;
	FN_END;
}

static char *__bt_prepare_msg_bmseg(msg_struct_t msg_info, gboolean attach,
							gboolean transcode)
{
	FN_START;
	int ret;
	int m_type = MSG_TYPE_SMS;
	int folder_id;
	int count;
	int dptime = 0;
	int j;
	bool read_status = false;
	char msg_body[BT_MAP_MSG_BODY_MAX] = {0,};
	char addr_value[MAX_ADDRESS_VAL_LEN] = {0,};
	char name_value[MAX_DISPLAY_NAME_LEN] = {0,};

	msg_list_handle_t addr_list = NULL;
	msg_struct_t addr_info = NULL;

	GString *msg;
	gchar *folder_path = NULL;
	gchar *msg_pdu;

	msg = g_string_new(BEGIN_BMSEG);
	g_string_append(msg, BMSEG_VERSION);

	ret = msg_get_bool_value(msg_info, MSG_MESSAGE_READ_BOOL, &read_status);
	if (ret == MSG_SUCCESS) {
		INFO("read_status %d\n", read_status);
	}

	if (read_status)
		g_string_append_printf(msg, MSEG_STATUS, "READ");
	else
		g_string_append_printf(msg, MSEG_STATUS, "UNREAD");

	ret = msg_get_int_value(msg_info, MSG_MESSAGE_TYPE_INT, &m_type);
	if (ret == MSG_SUCCESS) {
		INFO("m_type %d\n", m_type);
		 g_string_append_printf(msg, MSEG_TYPE, "SMS_GSM");
	}

	ret = msg_get_int_value(msg_info, MSG_MESSAGE_FOLDER_ID_INT,
							&folder_id);
	if (ret == MSG_SUCCESS) {
		DBG("folder_id %d\n", folder_id);

		folder_path = __bt_get_folder_name(folder_id);
		g_string_append_printf(msg, FOLDER_PATH, folder_path);
	}

	ret = msg_get_list_handle(msg_info, MSG_MESSAGE_ADDR_LIST_HND,
						(void **)&addr_list);
	if (ret == MSG_SUCCESS) {
		count = msg_list_length(addr_list);
		DBG("count %d \n", count);

		if (count > 0) {
			addr_info = (msg_struct_t)msg_list_nth_data(addr_list,
									0);

			msg_get_str_value(addr_info,
					MSG_ADDRESS_INFO_ADDRESS_VALUE_STR,
					addr_value, MAX_ADDRESS_VAL_LEN);
			DBG_SECURE("addr_value %s\n", addr_value);
			msg_get_str_value(addr_info,
					MSG_ADDRESS_INFO_DISPLAYNAME_STR,
					name_value, MAX_DISPLAY_NAME_LEN);
			if (!strlen(name_value))
				g_stpcpy(name_value, addr_value);

			DBG_SECURE("name_value %s\n", name_value);

			g_string_append_printf(msg, VCARD, name_value,
								addr_value);
		}
	}

	g_string_append(msg, BEGIN_BENV);
	g_string_append(msg, BEGIN_BBODY);

	if (transcode) {
		g_string_append_printf(msg, CHARSET, "UTF-8");


		ret = msg_get_str_value(msg_info,
					MSG_MESSAGE_SMS_DATA_STR,
					msg_body, BT_MAP_MSG_BODY_MAX);
		if (ret == MSG_SUCCESS) {
			g_string_append_printf(msg, LENGTH, strlen(msg_body));
			g_string_append_printf(msg, MSG_BODY, msg_body);
		}
	} else {
		g_string_append_printf(msg, ENCODING, "G-7BIT");
		g_string_append_printf(msg, CHARSET, "native");

		msg_get_int_value(msg_info,
				MSG_MESSAGE_DISPLAY_TIME_INT, &dptime);

		ret = msg_get_str_value(msg_info, MSG_MESSAGE_SMS_DATA_STR,
					msg_body, BT_MAP_MSG_BODY_MAX);
		if (ret == MSG_SUCCESS) {
			int msg_pdu_len = 0;
			msg_pdu = __bt_get_sms_pdu_from_msg_data(addr_value,
							msg_body, dptime,
							&msg_pdu_len);
			DBG("msg_pdu_len = %d", msg_pdu_len);

			g_string_append_printf(msg, LENGTH, msg_pdu_len);
			g_string_append(msg, MSG_BODY_BEGIN);
			for (j = 0; j < msg_pdu_len; j++)
				g_string_append_printf(msg, "%02x",
								msg_pdu[j]);

			g_string_append(msg, MSG_BODY_END);
			g_free(msg_pdu);
		}
	}

	g_string_append(msg, END_BBODY);
	g_string_append(msg, END_BENV);
	g_string_append(msg, END_BMSEG);
	g_free(folder_path);

	FN_END;
	return g_string_free(msg, FALSE);
}

static void __bt_message_info_free(struct message_info msg_info)
{
	FN_START;
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
	FN_END;
}

static struct message_info __bt_message_info_get(msg_struct_t msg_struct_handle)
{
	FN_START;
	struct message_info msg_info = {0,};
	int ret;
	int msg_id;
	guint64 uid;
	int dptime;
	int m_type = 0;
	int data_size;
	int priority;
	int direction_type;
	int count;
	bool protect_status = 0;
	bool read_status = 0;

	char msg_handle[BT_MAP_MSG_HANDLE_MAX] = {0,};
	char msg_datetime[BT_MAP_TIMESTAMP_MAX_LEN] = {0,};
	char msg_size[5] = {0,};
	char msg_body[BT_MAP_MSG_BODY_MAX] = {0,};
	char addr_value[MAX_ADDRESS_VAL_LEN] = {0,};
	char name_value[MAX_DISPLAY_NAME_LEN] = {0,};

	msg_info.text = FALSE;
	msg_info.protect = FALSE;
	msg_info.read = FALSE;
	msg_info.priority = FALSE;

	msg_struct_t msg = NULL;
	msg_struct_t send_opt = NULL;
	msg_list_handle_t addr_list = NULL;
	msg_struct_t addr_info = NULL;

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_ID_INT, &msg_id);
	if (ret == MSG_SUCCESS) {
		uid = __bt_add_id(msg_id);
		snprintf(msg_handle, sizeof(msg_handle), "%llx", uid);
	}
	msg_info.handle = g_strdup(msg_handle);

	msg = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
	if (msg == NULL)
		goto next;

	send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);
	if (send_opt == NULL)
		goto next;

	ret = msg_get_message(g_msg_handle,
						(msg_message_id_t)msg_id,
						msg, send_opt);
	if (ret != MSG_SUCCESS) {
		DBG("ret = %d\n", ret);
		goto next;
	}

	ret = msg_get_list_handle(msg, MSG_MESSAGE_ADDR_LIST_HND,
							(void **)&addr_list);
	if (ret != MSG_SUCCESS) {
		DBG("ret = %d\n", ret);
		goto next;
	}

	count = msg_list_length(addr_list);

	if (count != 0) {
		addr_info = (msg_struct_t)msg_list_nth_data(addr_list, 0);

		ret = msg_get_str_value(addr_info,
					MSG_ADDRESS_INFO_ADDRESS_VALUE_STR,
					addr_value, MAX_ADDRESS_VAL_LEN);
		if (ret == MSG_SUCCESS)
			DBG_SECURE("addr_value %s\n", addr_value);

		ret = msg_get_str_value(addr_info,
					MSG_ADDRESS_INFO_DISPLAYNAME_STR,
					name_value, MAX_DISPLAY_NAME_LEN);

		if (ret == MSG_SUCCESS)
			DBG_SECURE("name_value %s\n", name_value);

		if (!strlen(name_value))
			g_stpcpy(name_value, addr_value);

		DBG_SECURE("name_value %s\n", name_value);
	}

	ret = msg_get_int_value(msg, MSG_MESSAGE_DIRECTION_INT,
						&direction_type);
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

	g_free(msg_info.handle);

	ret = msg_get_int_value(msg_struct_handle,
				MSG_MESSAGE_DISPLAY_TIME_INT, &dptime);
	if (ret == MSG_SUCCESS) {
		__get_msg_timestamp((time_t *)&dptime, msg_datetime);
	}
	msg_info.datetime = g_strdup(msg_datetime);

	ret = msg_get_int_value(msg_struct_handle, MSG_MESSAGE_TYPE_INT,
								&m_type);
	if (ret == MSG_SUCCESS) {
		DBG("m_type %d\n", m_type);
	}

	msg_info.type = g_strdup("SMS_GSM");

	ret = msg_get_str_value(msg_struct_handle,
				MSG_MESSAGE_SMS_DATA_STR, msg_body,
				BT_MAP_MSG_BODY_MAX);
	if (ret == MSG_SUCCESS) {
		DBG_SECURE("SMS subject %s", msg_body);
		if (strlen(msg_body)) {
			msg_info.text = TRUE ;
			msg_info.subject = __bt_get_truncated_utf8_string(msg_body);
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

	FN_END;
	return msg_info;
}

static void __bluetooth_map_msg_incoming_status_cb(msg_handle_t handle,
							msg_struct_t msg,
							void *user_param)
{
	FN_START;
	int msg_id = 0;
	int msg_type = 0;
	int ret;

	guint64 uid;

	if (!g_mns_proxy) {
		INFO("MNS Client not connected");
		return;
	}

	ret = msg_get_int_value(msg, MSG_MESSAGE_TYPE_INT, &msg_type);
	if (ret != MSG_SUCCESS)
		return;

	if (msg_type != MSG_TYPE_SMS) {
		INFO("Not a SMS");
		return;
	}

	ret = msg_get_int_value(msg, MSG_MESSAGE_ID_INT, &msg_id);
	if (ret != MSG_SUCCESS)
		return;;

	uid = __bt_add_id(msg_id);

	__bt_mns_client_event_notify("NewMessage", uid,
						"TELECOM/MSG/INBOX", "",
						"SMS_GSM");

	FN_END;
	return;
}

static void __bluetooth_map_msg_sent_status_cb(msg_handle_t handle,
							msg_struct_t msg,
							void *user_param)
{
	FN_START;
	int ret;
	int status;

	if (!g_mns_proxy) {
		INFO("MNS Client not connected");
		return;
	}

	ret = msg_get_int_value(msg, MSG_SENT_STATUS_NETWORK_STATUS_INT,
								&status);
	if (ret != MSG_SUCCESS)
		return;

	if (status == MSG_NETWORK_SEND_SUCCESS) {
		INFO("MSG SENT SUCCESS !!! ");
		__bt_mns_client_event_notify("MessageShift",
					current_push_map_id,
					"TELECOM/MSG/SENT",
					"TELECOM/MSG/OUTBOX",
					"SMS_GSM");

		__bt_mns_client_event_notify("SendingSuccess",
					current_push_map_id,
					"TELECOM/MSG/SENT", "",
					"SMS_GSM");
	} else {
		ERR("MSG SENT FAIL !!! [%d]", status);
		__bt_mns_client_event_notify("SendingFailure",
					current_push_map_id,
					"TELECOM/MSG/OUTBOX", "",
					"SMS_GSM");
	}

	FN_END;
	return;
}

static gboolean __bluetooth_map_start_service()
{
	FN_START;
	msg_error_t err;

	err = msg_open_msg_handle(&g_msg_handle);
	if (err != MSG_SUCCESS) {
		ERR("msg_open_msg_handle error = %d\n", err);
		return FALSE;
	}

	if (-1 == __bt_get_folder_id(BT_MAP_DELETED_FOLDER_NAME))
		__bt_add_deleted_folder();

	err = msg_reg_sms_message_callback(g_msg_handle,
					__bluetooth_map_msg_incoming_status_cb,
					0, (void *)BT_MAP_MSG_CB);
	if (err != MSG_SUCCESS) {
		ERR("msg_reg_sms_message_callback error  = %d\n", err);
		return FALSE;
	}

	err = msg_reg_sent_status_callback(g_msg_handle,
					__bluetooth_map_msg_sent_status_cb,
					NULL);
	if (err != MSG_SUCCESS) {
		ERR("msg_reg_sent_status_callback error  = %d\n", err);
		return FALSE;
	}

	FN_END;
	return TRUE;
}

static void __bluetooth_map_stop_service()
{
	FN_START;
	msg_error_t err =  MSG_SUCCESS;
	int folder_id;

	folder_id = __bt_get_folder_id(BT_MAP_DELETED_FOLDER_NAME);
	if (-1 != folder_id) {
		err = msg_delete_folder(g_msg_handle, folder_id);
		if (err != MSG_SUCCESS)
			ERR("Delete folder failed");
	}

	if (NULL != g_msg_handle)
		msg_close_msg_handle(&g_msg_handle);

	g_msg_handle = NULL;

	FN_END;
	return;
}

static gboolean __bt_validate_utf8(char **text)
{
	FN_START;
	if (g_utf8_validate(*text, -1, NULL))
		return TRUE;

	FN_END;
	return FALSE;
}

static gboolean __bt_validate_msg_data(struct message_info *msg_info)
{
	FN_START;
	if (msg_info == NULL)
		return FALSE;

	if (msg_info->subject)
		return __bt_validate_utf8(&msg_info->subject);

	if (msg_info->sender_name)
		return __bt_validate_utf8(&msg_info->sender_name);

	if (msg_info->sender_addressing)
		return __bt_validate_utf8(&msg_info->sender_addressing);

	if (msg_info->replyto_addressing)
		return __bt_validate_utf8(&msg_info->replyto_addressing);

	if (msg_info->recipient_name)
		return __bt_validate_utf8(&msg_info->recipient_name);

	if (msg_info->recipient_addressing)
		return __bt_validate_utf8(&msg_info->recipient_addressing);

	FN_END;
	return TRUE;
}

msg_error_t __bt_send_sms(int msg_id, msg_struct_t p_msg, msg_struct_t p_send_opt)
{
	FN_START;
	msg_error_t err;
	msg_struct_t p_req;

	p_req = msg_create_struct(MSG_STRUCT_REQUEST_INFO);

	msg_set_int_value(p_msg, MSG_MESSAGE_ID_INT, msg_id);
	msg_set_struct_handle(p_req, MSG_REQUEST_MESSAGE_HND, p_msg);
	msg_set_struct_handle(p_req, MSG_REQUEST_SENDOPT_HND, p_send_opt);

	err = msg_sms_send_message(g_msg_handle, p_req);
	if (err != MSG_SUCCESS)
		ERR("Failed msg_sms_send_message %d", err);

	msg_release_struct(&p_req);
	FN_END;
	return err;
}

static int __bt_push_sms(gboolean send, int folder_id, char *body,
							GSList *recepients)
{
	FN_START;
	msg_struct_t msg_info = NULL;
	msg_struct_t send_opt = NULL;
	msg_error_t err;

	int count = 0;
	int i = 0;
	int msg_id = -1;

	msg_info = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
	if (msg_info == NULL)
		goto fail;

	err = msg_set_int_value(msg_info, MSG_MESSAGE_TYPE_INT, MSG_TYPE_SMS);
	if (err != MSG_SUCCESS)
		goto fail;

	if (body) {
		err = msg_set_str_value(msg_info,
					MSG_MESSAGE_SMS_DATA_STR,
					body, strlen(body));
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

		for (i = 0; i < count; i++) {
			msg_struct_t tmp_addr;
			char *address = (char *)g_slist_nth_data(recepients, i);
			if (address == NULL) {
				ERR("[ERROR] address is value NULL, skip");
				continue;
			}
			msg_list_add_item(msg_info,
				MSG_MESSAGE_ADDR_LIST_HND, &tmp_addr);

			msg_set_int_value(tmp_addr,
				MSG_ADDRESS_INFO_RECIPIENT_TYPE_INT,
				MSG_RECIPIENTS_TYPE_TO);

			msg_set_str_value(tmp_addr,
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

	if (send == TRUE)
		__bt_send_sms(msg_id, msg_info, send_opt);


fail:
	msg_release_struct(&msg_info);
	msg_release_struct(&send_opt);
	FN_END;
	return msg_id;
}

static void __bt_mns_client_connect(char *address)
{
	FN_START;
	GHashTable *hash;
	GValue *tgt_value;
	GError *error = NULL;
	const char *session_path = NULL;

	if (g_mns_proxy) {
		DBG_SECURE("MNS Client already connected to %s", address);
		return;
	}

	g_mns_proxy = dbus_g_proxy_new_for_name(g_connection,
						OBEX_CLIENT_SERVICE,
						OBEX_CLIENT_PATH,
						OBEX_CLIENT_INTERFACE);
	if (!g_mns_proxy) {
		ERR("Failed to get a proxy for D-Bus\n");
		return;
	}

	hash = g_hash_table_new_full(g_str_hash, g_str_equal,
				     NULL, (GDestroyNotify)g_free);

	tgt_value = g_new0(GValue, 1);
	g_value_init(tgt_value, G_TYPE_STRING);
	g_value_set_string(tgt_value, "MNS");
	g_hash_table_insert(hash, "Target", tgt_value);

	dbus_g_proxy_call(g_mns_proxy, "CreateSession", &error,
		G_TYPE_STRING,address,
		dbus_g_type_get_map("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
		hash, G_TYPE_INVALID,
		DBUS_TYPE_G_OBJECT_PATH, &session_path,
		G_TYPE_INVALID);
	if (error) {
		ERR("Error [%s]", error->message);
		g_error_free(error);
		g_hash_table_destroy(hash);
		g_object_unref(g_mns_proxy);
		g_mns_proxy = NULL;
		return;
	}

	g_mns_path = g_strdup(session_path);
	DBG("g_mns_path = %s\n", g_mns_path);

	g_hash_table_destroy(hash);

	FN_END;
	return;
}

static void __bt_mns_client_disconnect()
{
	FN_START;
	GError *error = NULL;

	if (!g_mns_proxy) {
		ERR("No proxy to disconnect");
		return;
	}

	dbus_g_proxy_call(g_mns_proxy, "RemoveSession", &error,
		DBUS_TYPE_G_OBJECT_PATH, g_mns_path,
		G_TYPE_INVALID, G_TYPE_INVALID);
	if (error) {
		ERR("Error [%s]", error->message);
		g_error_free(error);
	}

	g_free(g_mns_path);
	g_mns_path = NULL;

	g_object_unref(g_mns_proxy);
	g_mns_proxy = NULL;

	FN_END;
	return;
}

static void __bt_mns_client_event_notify(gchar *event, guint64 handle,
					gchar *folder, gchar *old_folder,
					gchar *msg_type)
{
	FN_START;
	GError *error = NULL;
	DBusGProxy *mns_proxy;

	if (!g_mns_proxy) {
		ERR("No client proxy");
		return;
	}

	mns_proxy = dbus_g_proxy_new_for_name(g_connection,
						OBEX_CLIENT_SERVICE,
						g_mns_path,
						MNS_CLIENT_INTERFACE);
	if (mns_proxy == NULL) {
		ERR("Failed to get a proxy for D-Bus\n");
		return;
	}

	dbus_g_proxy_call(mns_proxy, "SendEvent", &error,
		G_TYPE_STRING, event,
		G_TYPE_UINT64, handle,
		G_TYPE_STRING, folder,
		G_TYPE_STRING, old_folder,
		G_TYPE_STRING, msg_type,
		G_TYPE_INVALID, G_TYPE_INVALID);
	if (error) {
		ERR("Error [%s]", error->message);
		g_error_free(error);
	}

	g_object_unref(mns_proxy);
	FN_END;
}

static gboolean bluetooth_map_get_folder_tree(BluetoothMapAgent *agent,
						DBusGMethodInvocation *context)
{
	FN_START;
	GPtrArray *array = g_ptr_array_new();
	GValue value;
	GError *error = NULL;

	char name[BT_MAP_MSG_INFO_MAX] = {0,};
	char folder_name[BT_MAP_MSG_INFO_MAX] = {0,};
	int i;
	int ret;
	gboolean msg_ret = TRUE;

	msg_struct_list_s folder_list = {0,};
	msg_struct_t p_folder;

	if (g_msg_handle == NULL) {
		msg_ret = FALSE;
		goto done;
	}

	if (msg_get_folder_list(g_msg_handle, &folder_list) != MSG_SUCCESS) {
		msg_ret = FALSE;
		goto done;
	}

	for (i = 0; i < folder_list.nCount; i++) {
		p_folder = folder_list.msg_struct_info[i];
		memset(folder_name, 0x00, BT_MAP_MSG_INFO_MAX);

		ret = msg_get_str_value(p_folder, MSG_FOLDER_INFO_NAME_STR,
					folder_name, BT_MAP_MSG_INFO_MAX);
		if (ret != MSG_SUCCESS)
			continue;

		if (g_strstr_len(folder_name, -1, BT_MAP_MSG_TEMPLATE))
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

done:

	if (folder_list.msg_struct_info)
		msg_release_list_struct(&folder_list);

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
		FN_END;
		return TRUE;
	}
}

static gboolean bluetooth_map_get_message_list(BluetoothMapAgent *agent,
						gchar *folder_name, guint16 max,
						DBusGMethodInvocation *context)
{
	FN_START;
	GPtrArray *array = g_ptr_array_new();
	GValue value;
	GError *error = NULL;

	char *folder = NULL;
	int i = 0;
	int ret = 0;
	int folder_id = -1;
	int folder_len;
	bool read;
	guint64 count = 0;
	gboolean newmsg = FALSE;

	msg_struct_list_s folder_list = {0,};
	msg_struct_list_s msg_list = {0,};
	msg_struct_t list_cond;

	if (g_msg_handle == NULL)
		goto fail;

	if (!folder_name)
		goto fail;

	folder_len = strlen(folder_name);
	/* In case of parent folders send empty message listing */
	if (!g_ascii_strncasecmp(folder_name, "/", folder_len) ||
		!g_ascii_strncasecmp(folder_name, "/telecom", folder_len) ||
		!g_ascii_strncasecmp(folder_name, "/telecom/msg", folder_len))
		goto done;

	folder = strrchr(folder_name, '/');
	if (NULL == folder)
		folder = folder_name;
	else
		folder++;

	ret = msg_get_folder_list(g_msg_handle, &folder_list);
	if (ret != MSG_SUCCESS)
		goto fail;

	for (i = 0; i < folder_list.nCount; i++) {
		char f_name[BT_MAP_MSG_INFO_MAX] = {0, };
		msg_struct_t p_folder = folder_list.msg_struct_info[i];

		ret = msg_get_str_value(p_folder, MSG_FOLDER_INFO_NAME_STR,
					f_name, BT_MAP_MSG_INFO_MAX);
		if (ret  != MSG_SUCCESS)
			continue;

		if (!g_ascii_strncasecmp(f_name, folder, strlen(folder))) {
			ret = msg_get_int_value(p_folder, MSG_FOLDER_INFO_ID_INT,
								&folder_id);
			if (ret != MSG_SUCCESS)
				goto fail;

			DBG("folder_id %d \n", folder_id);

			break;
		}
	}

	if (folder_id == -1)
		goto fail;

	list_cond = msg_create_struct(MSG_STRUCT_MSG_LIST_CONDITION);
	ret = msg_set_int_value(list_cond,
				MSG_LIST_CONDITION_FOLDER_ID_INT,
				folder_id);
	if (ret != MSG_SUCCESS)
		goto fail;

	ret = msg_set_int_value(list_cond,
				MSG_LIST_CONDITION_MSGTYPE_INT, MSG_TYPE_SMS);
	if (ret != MSG_SUCCESS)
		goto fail;

	ret = msg_get_message_list2(g_msg_handle, list_cond, &msg_list);

	msg_release_struct(&list_cond);

	if (ret != MSG_SUCCESS)
		goto fail;

	count = msg_list.nCount;

	for (i = 0; i < count; i++) {
		msg_get_bool_value(msg_list.msg_struct_info[i],
					MSG_MESSAGE_READ_BOOL, &read);
		if (read == false) {
			newmsg = TRUE;
			break;
		}
	}

	DBG("count = %llx, newmsg = %d, max = %d", count, newmsg, max);

	if (max == 0)
		goto done;

	for (i = 0; i < msg_list.nCount; i++) {

		struct message_info msg_info;

		memset(&value, 0, sizeof(GValue));
		g_value_init(&value, DBUS_STRUCT_MESSAGE_LIST);
		g_value_take_boxed(&value, dbus_g_type_specialized_construct(
						DBUS_STRUCT_MESSAGE_LIST));

		msg_info = __bt_message_info_get(msg_list.msg_struct_info[i]);

		if (!__bt_validate_msg_data(&msg_info)) {
			__bt_message_info_free(msg_info);
			count--;
			continue;
		}

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
					16, msg_info.replyto_addressing,
					G_MAXUINT);
		g_ptr_array_add(array, g_value_get_boxed(&value));

		__bt_message_info_free(msg_info);
	}

done:
	if (folder_list.msg_struct_info)
		ret = msg_release_list_struct(&folder_list);

	if (msg_list.msg_struct_info)
		ret = msg_release_list_struct(&msg_list);

	dbus_g_method_return(context, newmsg, count, array);
	g_ptr_array_free(array, TRUE);
	FN_END;
	return TRUE;

fail:
	if (folder_list.msg_struct_info)
		ret = msg_release_list_struct(&folder_list);

	if (msg_list.msg_struct_info)
		ret = msg_release_list_struct(&msg_list);

	g_ptr_array_free(array, TRUE);
	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							  "InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	ERR("fail -");
	return FALSE;
}

static gboolean bluetooth_map_get_message(BluetoothMapAgent *agent,
						gchar *message_name,
						gboolean attach,
						gboolean transcode,
						gboolean first_request,
						DBusGMethodInvocation *context)
{
	FN_START;
	char *buf = NULL;
	int message_id = 0;

	GError *error = NULL;

	msg_error_t msg_err;
	msg_struct_t msg = NULL;
	msg_struct_t send_opt = NULL;

	message_id = __bt_get_uid(message_name);
	if (message_id == -1)
		goto fail;

	DBG("message_id %d \n", message_id);
	DBG("attach %d \n", attach);
	DBG("transcode %d \n", transcode);
	DBG("first_request %d \n", first_request);

	if (g_msg_handle == NULL)
		goto fail;

	msg = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
	if (!msg)
		goto fail;

	send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);
	if (!send_opt)
		goto fail;

	msg_err = msg_get_message(g_msg_handle,
					(msg_message_id_t)message_id,
					msg, send_opt);
	if (msg_err != MSG_SUCCESS)
		goto fail;

	buf = __bt_prepare_msg_bmseg(msg, attach, transcode);

	dbus_g_method_return(context, FALSE, buf);
	msg_release_struct(&msg);
	msg_release_struct(&send_opt);
	g_free(buf);

	FN_END;
	return TRUE;

fail:

	if (msg)
		msg_release_struct(&msg);

	if (send_opt)
		msg_release_struct(&send_opt);

	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	ERR("fail - \n");
	return FALSE;
}

static gboolean bluetooth_map_push_message(BluetoothMapAgent *agent,
					gboolean save_copy,
					gboolean retry_send,
					gboolean native,
					gchar *folder_name,
					DBusGMethodInvocation *context)
{
	FN_START;
	guint64 handle = 0;

	DBG_SECURE("folder_name = %s\n", folder_name);

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
	FN_END;
	return TRUE;
}

static gboolean bluetooth_map_push_message_data(BluetoothMapAgent *agent,
					gchar *bmsg,
					DBusGMethodInvocation *context)
{
	FN_START;
	int id = -1;
	int folder_id;
	char *body = NULL;
	GSList *recepients = NULL;
	gboolean send = FALSE;

	GError *error = NULL;

	INFO("BMSG is \n %s", bmsg);

	struct bmsg_data *bmsg_info = NULL;

	bmsg_info = bmsg_parse(bmsg);
	if (!bmsg_info)
		goto done;

	folder_id = __bt_get_folder_id(bmsg_info->folder);
	if (folder_id == -1)
		goto done;

	if (MSG_OUTBOX_ID == folder_id)
		send = TRUE;

	body = bmsg_get_msg_body(bmsg_info, opt.native);
	if (body == NULL)
		goto done;

	recepients = bmsg_get_msg_recepients(bmsg_info);

	id = __bt_push_sms(send, folder_id, body, recepients);
	if (id == -1)
		goto done;

	__bt_update_id(current_push_map_id, id);

done:
	g_free(body);
	g_slist_free(recepients);
	bmsg_free_bmsg(bmsg_info);

	if (id == -1) {
		error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
		dbus_g_method_return_error(context, error);
		g_error_free(error);
		FN_END;
		return FALSE;
	}

	dbus_g_method_return(context);
	FN_END;
	return TRUE;
}

static gboolean bluetooth_map_update_message(BluetoothMapAgent *agent,
						DBusGMethodInvocation *context)
{
	int err = TRUE;

	dbus_g_method_return(context, err);
	return TRUE;
}

static gboolean bluetooth_map_set_read_status(BluetoothMapAgent *agent,
						gchar *handle,
						gboolean read_status,
						DBusGMethodInvocation *context)
{
	FN_START;
	int msg_id;
	GError *error = NULL;
	msg_error_t msg_err;

	msg_id = __bt_get_uid(handle);
	if (msg_id == -1)
		goto fail;

	DBG("msg_id = %d,  read_status = %d\n", msg_id, read_status);

	msg_err = msg_update_read_status(g_msg_handle, msg_id,
							read_status);
	if (msg_err != MSG_SUCCESS)
		goto fail;

	dbus_g_method_return(context);
	FN_END;
	return TRUE;

fail:
	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);

	ERR("fail -\n");
	return FALSE;
}

static gboolean bluetooth_map_set_delete_status(BluetoothMapAgent *agent,
						gchar *handle,
						gboolean delete_status,
						DBusGMethodInvocation *context)
{
	FN_START;
	int msg_id = 0;
	int folder_id;
	int del_folder_id;
	gchar *folder_name = NULL;
	guint64 map_id;
	GError *error = NULL;
	msg_error_t err;
	msg_struct_t msg = NULL;
	msg_struct_t send_opt = NULL;

	msg_id = __bt_get_uid(handle);
	if (msg_id == -1)
		goto fail;

	if (g_msg_handle == NULL)
		goto fail;

	msg = msg_create_struct(MSG_STRUCT_MESSAGE_INFO);
	if (msg == NULL)
		goto fail;

	send_opt = msg_create_struct(MSG_STRUCT_SENDOPT);
	if (send_opt == NULL)
		goto fail;

	err = msg_get_message(g_msg_handle,
					(msg_message_id_t)msg_id,
					msg, send_opt);
	if (err != MSG_SUCCESS)
		goto fail;

	err = msg_get_int_value(msg, MSG_MESSAGE_FOLDER_ID_INT,
							&folder_id);
	if (err != MSG_SUCCESS)
		goto fail;

	folder_name = __bt_get_folder_name(folder_id);
	del_folder_id = __bt_get_folder_id(BT_MAP_DELETED_FOLDER_NAME);
	map_id = __bt_validate_uid(msg_id);

	DBG("msg_id = %d, delete_status = %d\n", msg_id, delete_status);

	if (-1 == del_folder_id) {
		ERR("Delete folder not present");
		if (delete_status == TRUE) {
			err = msg_delete_message(g_msg_handle, msg_id);
			if (err != MSG_SUCCESS)
				goto fail;
		}

	} else {
		if (delete_status == TRUE) {
			err = msg_move_msg_to_folder(g_msg_handle, msg_id, del_folder_id);
			if (err == MSG_SUCCESS) {
				__bt_mns_client_event_notify("MessageShift",
						map_id,
						"TELECOM/MSG/DELETED",
						folder_name,
						"SMS_GSM");
			}
		} else {
			if (folder_id != del_folder_id) {
				DBG("Message not in delete folder");
				goto fail;
			}

			err = msg_move_msg_to_folder(g_msg_handle, msg_id, MSG_INBOX_ID);
			if (err == MSG_SUCCESS) {
				__bt_mns_client_event_notify("MessageShift",
						map_id,
						"TELECOM/MSG/INBOX",
						"TELECOM/MSG/DELETED",
						"SMS_GSM");
			}
		}
	}

	g_free(folder_name);
	msg_release_struct(&msg);
	msg_release_struct(&send_opt);
	dbus_g_method_return(context);
	FN_END;
	return TRUE;

fail:
	g_free(folder_name);

	msg_release_struct(&msg);
	msg_release_struct(&send_opt);

	error = __bt_map_agent_error(BT_MAP_AGENT_ERROR_INTERNAL,
							"InternalError");
	dbus_g_method_return_error(context, error);
	g_error_free(error);
	ERR("fail -\n");
	return FALSE;
}

static gboolean bluetooth_map_noti_registration(BluetoothMapAgent *agent,
						gchar *remote_addr,
						gboolean status,
						DBusGMethodInvocation *context)
{
	FN_START;
	DBG_SECURE("remote_addr = %s \n", remote_addr);

	if (status == TRUE)
		__bt_mns_client_connect(remote_addr);
	else
		__bt_mns_client_disconnect();

	return TRUE;
}

static gboolean bluetooth_map_destroy_agent(BluetoothMapAgent *agent,
					DBusGMethodInvocation *context)
{
	FN_START;
	g_main_loop_quit(g_mainloop);
	return TRUE;
}

int main(void)
{
	FN_START;
	BluetoothMapAgent *bluetooth_map_obj = NULL;
	DBusGProxy *bus_proxy = NULL;
	guint result = 0;
	int ret;
	GError *error = NULL;
	DBG("Starting Bluetooth MAP agent");

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

	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		ERR("Failed to get the primary well-known name.\n");
		goto failure;
	}

	g_object_unref(bus_proxy);
	bus_proxy = NULL;

	bluetooth_map_obj = g_object_new(BLUETOOTH_MAP_TYPE_AGENT, NULL);

	/* Registering it on the D-Bus */
	dbus_g_connection_register_g_object(g_connection,
						BT_MAP_SERVICE_OBJECT_PATH,
						G_OBJECT(bluetooth_map_obj));

	if (__bluetooth_map_start_service() == FALSE)
		goto failure;

	g_tapi_handle = tel_init(NULL);
	if (!g_tapi_handle)
		goto failure;

	ret = tel_get_sms_sca(g_tapi_handle, 0, __bt_get_sms_sca, NULL);
	if (ret != TAPI_API_SUCCESS) {
		ERR("TAPI err = %d", ret);
		goto failure;
	}

	g_main_loop_run(g_mainloop);

 failure:

	__bt_remove_list(id_list);

	tel_deinit(g_tapi_handle);
	g_free(g_sca_info);

	__bt_mns_client_disconnect();

	if (bus_proxy)
		g_object_unref(bus_proxy);
	if (bluetooth_map_obj)
		g_object_unref(bluetooth_map_obj);
	if (g_connection)
		dbus_g_connection_unref(g_connection);

	__bluetooth_map_stop_service();
	DBG("Bluetooth MAP agent Terminated successfully\n");
	FN_END;
	return EXIT_FAILURE;
}
