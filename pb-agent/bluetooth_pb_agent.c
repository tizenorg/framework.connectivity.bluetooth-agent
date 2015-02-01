/*
 * Bluetooth-agent
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:  Hocheol Seo <hocheol.seo@samsung.com>
 *		 Girishashok Joshi <girish.joshi@samsung.com>
 *		 Chanyeol Park <chanyeol.park@samsung.com>
 *		 Jaekyun Lee <jkyun.leek@samsung.com>
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
#include <signal.h>
#include <glib.h>
#include <dbus/dbus-glib.h>


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <contacts.h>

#include <TapiUtility.h>
#include <ITapiSim.h>

#include "bluetooth_pb_agent.h"
#include "bluetooth_pb_vcard.h"

#define BLUETOOTH_PB_AGENT_TIMEOUT 600

typedef struct {
	GObject parent;

	DBusGConnection *bus;
	DBusGProxy *proxy;

	TapiHandle *tapi_handle;
	gchar *tel_number;
	guint timeout_id;

	PhoneBookType pb_type;
} BluetoothPbAgent;

typedef struct {
	GObjectClass parent;

	void (*clear) (BluetoothPbAgent *agent);
} BluetoothPbAgentClass;

enum {
	CLEAR,
	LAST_SIGNAL
};

GType bluetooth_pb_agent_get_type(void);

#define BLUETOOTH_PB_TYPE_AGENT (bluetooth_pb_agent_get_type())

#define BLUETOOTH_PB_AGENT(object) \
	(G_TYPE_CHECK_INSTANCE_CAST((object), \
	BLUETOOTH_PB_TYPE_AGENT , BluetoothPbAgent))
#define BLUETOOTH_PB_AGENT_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), \
	BLUETOOTH_PB_TYPE_AGENT , BluetoothPbAgentClass))
#define BLUETOOTH_IS_PB_AGENT(object) \
	(G_TYPE_CHECK_INSTANCE_TYPE((object), \
	BLUETOOTH_PB_TYPE_AGENT))
#define BLUETOOTH_IS_PB_AGENT_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), \
	BLUETOOTH_PB_TYPE_AGENT))
#define BLUETOOTH_PB_AGENT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), \
	BLUETOOTH_PB_TYPE_AGENT , BluetoothPbAgentClass))

G_DEFINE_TYPE(BluetoothPbAgent, bluetooth_pb_agent, G_TYPE_OBJECT)

#define DBUS_STRUCT_STRING_STRING_UINT (dbus_g_type_get_struct("GValueArray", G_TYPE_STRING, \
							G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))

static guint signals[LAST_SIGNAL] = { 0 };
static guint total_missed_call_count = 0;
static guint unnotified_missed_call_count = 0;

static GMainLoop *mainloop = NULL;

static void bluetooth_pb_agent_finalize(GObject *obj);

static void bluetooth_pb_agent_clear(BluetoothPbAgent *agent);

/* Dbus messages */
static gboolean bluetooth_pb_get_phonebook_folder_list(BluetoothPbAgent *agent,
						const gchar ***folder_list,
						GError **error);

static gboolean bluetooth_pb_get_phonebook(BluetoothPbAgent *agent,
					const char *name,
					guint64 filter,
					guint8 format,
					guint16 max_list_count,
					guint16 list_start_offset,
					DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_phonebook_size(BluetoothPbAgent *agent,
						const char *name,
						DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_phonebook_list(BluetoothPbAgent *agent,
						const char *name,
						DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_phonebook_entry(BluetoothPbAgent *agent,
						const gchar *folder,
						const gchar *id,
						guint64 filter,
						guint8 format,
						DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_phonebook_size_at(BluetoothPbAgent *agent,
					const gchar *command,
					DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_phonebook_entries_at(BluetoothPbAgent *agent,
					const gchar *command,
					gint32 start_index,
					gint32 end_index,
					DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_phonebook_entries_find_at(BluetoothPbAgent *agent,
							const gchar *command,
							const gchar *find_text,
							DBusGMethodInvocation *context);

static gboolean bluetooth_pb_get_total_object_count(BluetoothPbAgent *agent,
						gchar *path,
						DBusGMethodInvocation *context);

static gboolean bluetooth_pb_add_contact (BluetoothPbAgent *agent,
					const char *filename,
					GError **error);

static gboolean bluetooth_pb_destroy_agent(BluetoothPbAgent *agent,
					DBusGMethodInvocation *context);

static void __bluetooth_pb_dbus_return_error(DBusGMethodInvocation *context,
					gint code,
					const gchar *message);

static PhoneBookType __bluetooth_pb_get_pb_type(const char *name);

static PhoneBookType __bluetooth_pb_get_storage_pb_type(const char *name);

static gint __bluetooth_pb_phone_log_filter_append(contacts_filter_h filter,
						gint *match,
						gint size);

static contacts_query_h __bluetooth_pb_query_phone_log(gint *match,
						gint size);

static contacts_query_h __bluetooth_pb_query_person(int addressbook);

static contacts_query_h __bluetooth_pb_query_person_number(void);

static contacts_query_h __bluetooth_pb_query_phone_log_incoming(void);

static contacts_query_h __bluetooth_pb_query_phone_log_outgoing(void);

static contacts_query_h __bluetooth_pb_query_phone_log_missed(void);

static contacts_query_h __bluetooth_pb_query_phone_log_combined(void);

static gboolean __bluetooth_pb_get_count(PhoneBookType pb_type,
				guint *count);

static gboolean __bluetooth_pb_get_count_new_missed_call(guint *count);

static const char *__bluetooth_pb_phone_log_get_log_type(contacts_record_h record);

static void __bluetooth_pb_get_vcards(BluetoothPbAgent *agent,
				PhoneBookType pb_type,
				guint64 filter,
				guint8 format,
				guint16 max_list_count,
				guint16 list_start_offset,
				GPtrArray *vcards);

static void __bluetooth_pb_get_contact_list(BluetoothPbAgent *agent,
					contacts_query_h query,
					GPtrArray *ptr_array);

static void __bluetooth_pb_get_phone_log_list(BluetoothPbAgent *agent,
					contacts_query_h query,
					GPtrArray *ptr_array);

static void __bluetooth_pb_get_list(BluetoothPbAgent *agent,
				PhoneBookType pb_type,
				GPtrArray *ptr_array);

static void __bluetooth_pb_get_contact_list_number(BluetoothPbAgent *agent,
						contacts_query_h query,
						gint start_index,
						gint end_index,
						GPtrArray *ptr_array);

static void __bluetooth_pb_get_phone_log_list_number(BluetoothPbAgent *agent,
						contacts_query_h query,
						gint start_index,
						gint end_index,
						GPtrArray *ptr_array);

static void __bluetooth_pb_get_list_number(BluetoothPbAgent *agent,
						PhoneBookType pb_type,
						gint start_index,
						gint end_index,
						GPtrArray *ptr_array);

static void __bluetooth_pb_get_contact_list_name(BluetoothPbAgent *agent,
						contacts_query_h query,
						const gchar *find_text,
						GPtrArray *ptr_array);

static void __bluetooth_pb_get_phone_log_list_name(BluetoothPbAgent *agent,
						contacts_query_h query,
						const gchar *find_text,
						GPtrArray *ptr_array);

static void __bluetooth_pb_get_list_name(BluetoothPbAgent *agent,
					PhoneBookType pb_type,
					const gchar *find_text,
					GPtrArray *ptr_array);

static void __bluetooth_pb_list_ptr_array_add(GPtrArray *ptr_array,
						const gchar *name,
						const gchar *number,
						gint handle);

static void __bluetooth_pb_list_ptr_array_free(gpointer data);

static void __bluetooth_pb_agent_signal_handler(int signum);

static void __bluetooth_pb_contact_changed(const gchar *view_uri,
					void *user_data);

static void __bluetooth_pb_agent_timeout_add_seconds(BluetoothPbAgent *agent);

static gboolean __bluetooth_pb_agent_timeout_calback(gpointer user_data);

static void __bluetooth_pb_tel_callback(TapiHandle *handle,
					int result,
					void *data,
					void *user_data);

static void __bluetooth_pb_agent_dbus_init(BluetoothPbAgent *agent);

#include "bluetooth_pb_agent_glue.h"

static void bluetooth_pb_agent_init(BluetoothPbAgent *agent)
{
	FN_START;
	agent->bus = NULL;
	agent->proxy = NULL;
	agent->tapi_handle = NULL;
	agent->tel_number = NULL;
	agent->timeout_id = 0;
	agent->pb_type = TELECOM_NONE;
	FN_END;
}

static void bluetooth_pb_agent_class_init(BluetoothPbAgentClass *klass)
{
	FN_START;
	GObjectClass *object_class = (GObjectClass *) klass;

	klass->clear = bluetooth_pb_agent_clear;

	object_class->finalize = bluetooth_pb_agent_finalize;

	signals[CLEAR] = g_signal_new("clear",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(BluetoothPbAgentClass, clear),
			NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	dbus_g_object_type_install_info(BLUETOOTH_PB_TYPE_AGENT,
					&dbus_glib_bluetooth_pb_object_info);
	FN_END;
}

static void bluetooth_pb_agent_finalize(GObject *obj)
{
	FN_START;
	BluetoothPbAgent *agent  = BLUETOOTH_PB_AGENT(obj);

	if (agent->tapi_handle) {
		tel_deinit(agent->tapi_handle);
		agent->tapi_handle = NULL;
	}

	if (agent->tel_number) {
		g_free(agent->tel_number);
		agent->tel_number = NULL;
	}

	if(agent->timeout_id) {
		g_source_remove(agent->timeout_id);
		agent->timeout_id = 0;
	}

	if (agent->proxy) {
		g_object_unref(agent->proxy);
		agent->proxy = NULL;
	}

	if (agent->bus) {
		dbus_g_connection_unref(agent->bus);
		agent->bus = NULL;
	}


	G_OBJECT_CLASS(bluetooth_pb_agent_parent_class)->finalize(obj);
	FN_END;
}

static void bluetooth_pb_agent_clear(BluetoothPbAgent *agent)
{
	FN_START;
	agent->pb_type = TELECOM_NONE;
	FN_END;
}

static gboolean bluetooth_pb_get_phonebook_folder_list(BluetoothPbAgent *agent,
						const gchar ***folder_list,
						GError **error)
{
	FN_START;
	gint size;
	gint i;
	gchar **folder;

	size = G_N_ELEMENTS(bluetooth_pb_agent_folder_list);
	folder = g_new0(gchar *, size);

	for (i = 0; i < size; i++)
		folder[i] = g_strdup(bluetooth_pb_agent_folder_list[i]);

	*folder_list = (const gchar **)folder;

	FN_END;
	return TRUE;
}

static gboolean bluetooth_pb_get_phonebook(BluetoothPbAgent *agent,
					const char *name,
					guint64 filter,
					guint8 format,
					guint16 max_list_count,
					guint16 list_start_offset,
					DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;
	GPtrArray *vcards = NULL;
	gchar **vcards_str = NULL;

	DBG_SECURE("name: %s filter: %lld format: %d max_list_count: %d list_start_offset: %d\n",
			name, filter, format, max_list_count, list_start_offset);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_pb_type(name);

	if (pb_type == TELECOM_NONE) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	vcards = g_ptr_array_new();

	if (max_list_count > 0) {
		__bluetooth_pb_get_vcards(agent, pb_type,
				filter, format,
				max_list_count, list_start_offset,
				vcards);

	}

	g_ptr_array_add(vcards, NULL);

	vcards_str = (gchar **) g_ptr_array_free(vcards, FALSE);

	if (pb_type == TELECOM_MCH) {
		dbus_g_method_return(context, vcards_str, unnotified_missed_call_count);
		INFO("Notified [%d] missed call count", unnotified_missed_call_count);
		unnotified_missed_call_count = 0;
	} else {
		dbus_g_method_return(context, vcards_str, 0);
	}

	g_strfreev(vcards_str);

	FN_END;
	return TRUE;
}

static gboolean bluetooth_pb_get_phonebook_size(BluetoothPbAgent *agent,
						const char *name,
						DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;
	guint count = 0;

	DBG_SECURE("name: %s\n", name);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_pb_type(name);

	if (__bluetooth_pb_get_count(pb_type, &count) == FALSE) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	/* for owner */
#ifdef PBAP_SIM_ENABLE
	if (pb_type == TELECOM_PB || pb_type == SIM_PB)
		count++;
#else
	if (pb_type == TELECOM_PB)
		count++;
#endif
	if (pb_type == TELECOM_MCH) {
		dbus_g_method_return(context, count, unnotified_missed_call_count);
		INFO("Notified [%d] missed call count", unnotified_missed_call_count);
		unnotified_missed_call_count = 0;
	} else {
		dbus_g_method_return(context, count, 0);
	}

	FN_END;
	return TRUE;
}


static gboolean bluetooth_pb_get_phonebook_list(BluetoothPbAgent *agent,
						const char *name,
						DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;

	GPtrArray *ptr_array;

	DBG_SECURE("name: %s\n", name);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_pb_type(name);

	if (pb_type == TELECOM_NONE) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	ptr_array = g_ptr_array_new_with_free_func(__bluetooth_pb_list_ptr_array_free);

	__bluetooth_pb_get_list(agent, pb_type, ptr_array);

//	__bluetooth_pb_get_count_new_missed_call(&new_missed_call);
	INFO("pb_type[%d] / number of missed_call[%d]", pb_type, unnotified_missed_call_count);

	if (pb_type == TELECOM_MCH) {
		dbus_g_method_return(context, ptr_array, unnotified_missed_call_count);
		INFO("Notified [%d] missed call count", unnotified_missed_call_count);
		unnotified_missed_call_count = 0;
	} else {
		dbus_g_method_return(context, ptr_array, 0);
	}

	if (ptr_array)
		g_ptr_array_free(ptr_array, TRUE);

	FN_END;
	return TRUE;
}


static gboolean bluetooth_pb_get_phonebook_entry(BluetoothPbAgent *agent,
						const gchar *folder,
						const gchar *id,
						guint64 filter,
						guint8 format,
						DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;

	gint handle = 0;
	gchar *str = NULL;

	const gchar *attr = NULL;

	DBG_SECURE("folder: %s id: %s filter: %ld format: %d\n",
			folder, id, filter, format);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	if (!g_str_has_suffix(id, ".vcf")) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"invalid vcf file");
		return FALSE;
	}

	handle = (gint)g_ascii_strtoll(id, NULL, 10);

	pb_type = __bluetooth_pb_get_pb_type(folder);

	if (pb_type == TELECOM_NONE) {
		__bluetooth_pb_dbus_return_error(context,
				G_FILE_ERROR_INVAL,
				"unsupported name defined");
		return FALSE;
	}

	/* create index cache */
	__bluetooth_pb_get_list(agent, pb_type, NULL);

	switch(pb_type) {
	case TELECOM_PB:
		if (handle == 0) {
			str = _bluetooth_pb_vcard_contact_owner(agent->tel_number,
								filter, format);
		} else {
			if (_bluetooth_get_contact_addressbook(handle) == PBAP_ADDRESSBOOK_PHONE)
				str = _bluetooth_pb_vcard_contact(handle, filter, format);
		}
		break;

	case TELECOM_ICH:
		str = _bluetooth_pb_vcard_call(handle, filter, format, "RECEIVED");
		break;
	case TELECOM_OCH:
		str = _bluetooth_pb_vcard_call(handle, filter, format, "DIALED");
		break;
	case TELECOM_MCH:
		str = _bluetooth_pb_vcard_call(handle, filter, format, "MISSED");
		break;
	case TELECOM_CCH: {
		contacts_record_h record = NULL;

		gint status;

		status = contacts_db_get_record(_contacts_phone_log._uri,
				handle, &record);

		if (status != CONTACTS_ERROR_NONE)
			break;

		attr = __bluetooth_pb_phone_log_get_log_type(record);
		str = _bluetooth_pb_vcard_call(handle, filter, format, attr);

		contacts_record_destroy(record, TRUE);
		break;
	}
#ifdef PBAP_SIM_ENABLE
	case SIM_PB:
		if (handle == 0) {
			str = _bluetooth_pb_vcard_contact_owner(agent->tel_number,
								filter, format);
		} else {
			if (_bluetooth_get_contact_addressbook(handle) == PBAP_ADDRESSBOOK_SIM)
				str = _bluetooth_pb_vcard_contact(handle, filter, format);
		}
		break;
#endif
	default:
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	dbus_g_method_return(context, str);
	g_free(str);

	FN_END;
	return TRUE;
}

static gboolean bluetooth_pb_get_phonebook_size_at(BluetoothPbAgent *agent,
					const gchar *command,
					DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;
	guint count = 0;

	DBG("command: %s\n", command);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_storage_pb_type(command);

	if (__bluetooth_pb_get_count(pb_type, &count) == FALSE) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	dbus_g_method_return(context, count);

	FN_END;
	return TRUE;
}

static gboolean bluetooth_pb_get_phonebook_entries_at(BluetoothPbAgent *agent,
					const gchar *command,
					gint start_index,
					gint end_index,
					DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;

	GPtrArray *ptr_array = NULL;

	DBG("command: %s, start_index: %d, end_index: %d\n",
			command, start_index, end_index);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_storage_pb_type(command);

	if (pb_type == TELECOM_NONE || pb_type == TELECOM_CCH) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	ptr_array = g_ptr_array_new_with_free_func(__bluetooth_pb_list_ptr_array_free);

	__bluetooth_pb_get_list_number(agent, pb_type,
			start_index, end_index,
			ptr_array);

	dbus_g_method_return(context, ptr_array);

	if (ptr_array)
		g_ptr_array_free(ptr_array, TRUE);

	FN_END;
	return TRUE;
}

static gboolean bluetooth_pb_get_phonebook_entries_find_at(BluetoothPbAgent *agent,
							const gchar *command,
							const gchar *find_text,
							DBusGMethodInvocation *context)
{
	FN_START;
	PhoneBookType pb_type = TELECOM_NONE;

	GPtrArray *ptr_array = NULL;

	DBG("command: %s, find text: %s\n", command, find_text);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_storage_pb_type(command);

	if (pb_type == TELECOM_NONE || pb_type == TELECOM_CCH) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	ptr_array = g_ptr_array_new_with_free_func(__bluetooth_pb_list_ptr_array_free);

	__bluetooth_pb_get_list_name(agent, pb_type,
			find_text, ptr_array);

	dbus_g_method_return(context, ptr_array);

	if (ptr_array)
		g_ptr_array_free(ptr_array, TRUE);

	FN_END;
	return TRUE;
}

static gboolean bluetooth_pb_get_total_object_count(BluetoothPbAgent *agent,
					gchar *path, DBusGMethodInvocation *context)
{
	FN_START;
	guint count = 0;
	PhoneBookType pb_type = TELECOM_NONE;

	DBG("%s() %d\n", __FUNCTION__, __LINE__);

	__bluetooth_pb_agent_timeout_add_seconds(agent);

	pb_type = __bluetooth_pb_get_storage_pb_type(path);

	if (__bluetooth_pb_get_count(pb_type, &count) == FALSE) {
		__bluetooth_pb_dbus_return_error(context,
					G_FILE_ERROR_INVAL,
					"unsupported name defined");
		return FALSE;
	}

	dbus_g_method_return(context, count);

	DBG("%s() %d\n", __FUNCTION__, __LINE__);

	FN_END;
	return TRUE;
}


#if 0
static int __bluetooth_pb_agent_read_file(const char *file_path, char **stream)
{
	FN_START;
	FILE *fp = NULL;
	int read_len = -1;
	int received_file_size = 0;
	struct stat file_attr;

	if (file_path == NULL || stream == NULL) {
		ERR("Invalid data \n");
		return -1;
	}

	DBG_SECURE("file_path = %s\n", file_path);

	if ((fp = fopen(file_path, "r+")) == NULL) {
		ERR_SECURE("Cannot open %s\n", file_path);
		return -1;
	}

	if (fstat(fileno(fp), &file_attr) == 0) {
		received_file_size = file_attr.st_size;
		DBG("file_attr.st_size = %d, size = %d\n", file_attr.st_size, received_file_size);

		if (received_file_size <= 0) {
			ERR_SECURE("Some problem in the file size [%s]  \n", file_path);
			fclose(fp);
			fp = NULL;
			return -1;
		}

		*stream = (char *)malloc(sizeof(char) * received_file_size);
		if (NULL == *stream) {
			fclose(fp);
			fp = NULL;
			return -1;
		}
	} else {
		ERR_SECURE("Some problem in the file [%s]  \n", file_path);
		fclose(fp);
		fp = NULL;
		return -1;
	}

	read_len = fread(*stream, 1, received_file_size, fp);

	if (read_len == 0) {
		if (fp != NULL) {
			fclose(fp);
			fp = NULL;
		}
		DBG_SECURE("Cannot open %s\n", file_path);
		return -1;
	}

	if (fp != NULL) {
		fclose(fp);
		fp = NULL;
	}
	FN_END;
	return 0;
}
#endif

static gboolean bluetooth_pb_add_contact(BluetoothPbAgent *agent, const char *filename,
					 GError **error)
{
	FN_START;
	/* Contact API is changed, Temporary blocked */
#if 0
	CTSstruct *contact_record = NULL;
	GSList *numbers_list = NULL, *cursor;
	int is_success = 0;
	int is_duplicated = 0;
	int err = 0;
	char *stream = NULL;

	DBG_SECURE("file_path = %s\n", filename);

	err = contacts_svc_connect();
	ERR("contact_db_service_connect fucntion call [error] = %d \n", err);

	err = __bluetooth_pb_agent_read_file(filename, &stream);

	if (err != 0) {
		contacts_svc_disconnect();
		ERR("contacts_svc_disconnect fucntion call [error] = %d \n", err);

		if (NULL != stream) {
			free(stream);
			stream = NULL;
		}
		return FALSE;
	}

	is_success = contacts_svc_get_contact_from_vcard((const void *)stream, &contact_record);

	DBG("contacts_svc_get_contact_from_vcard fucntion call [is_success] = %d \n", is_success);

	if (0 == is_success) {
		contacts_svc_struct_get_list(contact_record, CTS_CF_NUMBER_LIST, &numbers_list);
		cursor = numbers_list;

		for (; cursor; cursor = g_slist_next(cursor)) {
			if (contacts_svc_find_contact_by(CTS_FIND_BY_NUMBER,
							contacts_svc_value_get_str(cursor->data,
								CTS_NUM_VAL_NUMBER_STR)) > 0) {
				DBG("is_duplicated\n");
				is_duplicated = TRUE;
			}
		}

		if (is_duplicated == FALSE) {
			contacts_svc_insert_contact(0, contact_record);
		}
	} else {
		ERR("Fail \n");
	}

	err = contacts_svc_disconnect();
	ERR("contacts_svc_disconnect fucntion call [error] = %d \n", err);

	if (NULL != stream) {
		free(stream);
		stream = NULL;
	}
#endif
	FN_END;
	return TRUE;
}

static void __bluetooth_pb_dbus_return_error(DBusGMethodInvocation *context,
					gint code,
					const gchar *message)
{
	FN_START;
	GQuark quark;
	GError *error = NULL;

	quark = g_type_qname(bluetooth_pb_agent_get_type());
	error = g_error_new_literal(quark, code, message);

	DBG("%s\n", message);

	dbus_g_method_return_error(context, error);
	g_error_free(error);
	FN_END;
}

static PhoneBookType __bluetooth_pb_get_pb_type(const char *name)
{
	FN_START;
	gchar *suffix = ".vcf";
	gint len;
	gint size;
	gint i;

	if (name == NULL)
		return TELECOM_NONE;

	len = strlen(name);

	if (g_str_has_suffix(name, suffix))
		len -= strlen(suffix);

	size = G_N_ELEMENTS(bluetooth_pb_agent_folder_list) - 1;
	for (i = 0; i < size; i++) {
		if (strncmp(name, bluetooth_pb_agent_folder_list[i], len) == 0)
			return i;
	}

	FN_END;
	return TELECOM_NONE;
}

static PhoneBookType __bluetooth_pb_get_storage_pb_type(const char *name)
{
	FN_START;
	if (name == NULL)
		return TELECOM_NONE;

	if (g_strcmp0(name, "\"ME\"") == 0 )
		return TELECOM_PB;

	if (g_strcmp0(name, "\"RC\"") == 0)
		return TELECOM_ICH;

	if (g_strcmp0(name, "\"DC\"") == 0)
		return TELECOM_OCH;

	if (g_strcmp0(name, "\"MC\"") == 0)
		return TELECOM_MCH;

	FN_END;
	return TELECOM_NONE;
}

static gint __bluetooth_pb_phone_log_filter_append(contacts_filter_h filter,
						gint *match,
						gint size)
{
	FN_START;
	gint i;
	gint status;

	for (i = 0; i < size; i++) {

		if ( i > 0) {
			status = contacts_filter_add_operator(filter,
					CONTACTS_FILTER_OPERATOR_OR);

			if (status != CONTACTS_ERROR_NONE)
				return status;
		}

		status = contacts_filter_add_int(filter,
				_contacts_phone_log.log_type,
				CONTACTS_MATCH_EQUAL,
				match[i]);

		if (status != CONTACTS_ERROR_NONE)
			return status;
	}

	FN_END;
	return CONTACTS_ERROR_NONE;
}

static contacts_query_h __bluetooth_pb_query_phone_log(gint *match,
						gint size)
{
	FN_START;
	contacts_query_h query = NULL;
	contacts_filter_h filter = NULL;

	gint status;

	status = contacts_query_create(_contacts_phone_log._uri,
				&query);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_filter_create(_contacts_phone_log._uri, &filter);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_query_destroy(query);
		return NULL;
	}

	status = __bluetooth_pb_phone_log_filter_append(filter, match, size);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_filter_destroy(filter);
		contacts_query_destroy(query);
		return NULL;
	}

	status = contacts_query_set_filter(query, filter);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_filter_destroy(filter);
		contacts_query_destroy(query);
		return NULL;
	}

	status = contacts_query_set_sort(query,
			_contacts_phone_log.log_time,
			false);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_filter_destroy(filter);
		contacts_query_destroy(query);
		return NULL;
	}

	contacts_filter_destroy(filter);

	FN_END;
	return query;
}

bool __bt_is_matching_addressbook(const char *addressbook_name, int addressbook)
{
	bool is_sim_addressbook = _bt_is_sim_addressbook(addressbook_name);

	if ((is_sim_addressbook == false
			&& addressbook == PBAP_ADDRESSBOOK_PHONE) ||
		(is_sim_addressbook == true
			&& addressbook == PBAP_ADDRESSBOOK_SIM))
		return true;

	return false;
}

static contacts_query_h __bluetooth_pb_query_person(int addressbook)
{
	FN_START;
	contacts_query_h query = NULL;
	contacts_filter_h filter = NULL;
	contacts_list_h recordList = NULL;
	contacts_record_h record = NULL;

	char* addressbook_name = NULL;
	int address_book_id = 0;
	int count = 0;
	unsigned int i = 0;
	gint status;
	bool is_first_condition = true;
	DBG("Addressbook [%d]", addressbook);
	/* Create query*/
	status = contacts_query_create(_contacts_person_contact._uri, &query);
	if (status != 0) {
		ERR("Could not create query");
		return NULL;
	}

	/* Create addressbook Filter*/
	contacts_db_get_all_records(_contacts_address_book._uri, 0, 0, &recordList);
	contacts_filter_create(_contacts_person_contact._uri, &filter);
	contacts_list_get_count(recordList, &count);

	for (i = 0; i < count; i++) {
		contacts_list_get_current_record_p(recordList, &record);
		contacts_record_get_str_p(record, _contacts_address_book.name,
					&addressbook_name);
		contacts_record_get_int(record, _contacts_address_book.id,
					&address_book_id);

		DBG("Addressbook ID: [%d] Addressbook Name: [%s]",
				address_book_id, addressbook_name);

		if (__bt_is_matching_addressbook(addressbook_name,
				addressbook)) {
			if (is_first_condition)
				is_first_condition = false;
			else
				contacts_filter_add_operator(filter,
						CONTACTS_FILTER_OPERATOR_OR);
			DBG("SELECTED Addressbook ID: [%d] Addressbook Name: [%s]",
					address_book_id, addressbook_name);
			contacts_filter_add_int(filter,
					_contacts_person_contact.address_book_id,
					CONTACTS_MATCH_EQUAL, address_book_id);
		}
		contacts_list_next(recordList);
	}

	contacts_list_destroy(recordList, true);

	status = contacts_query_set_filter(query, filter);
	if (status != CONTACTS_ERROR_NONE)
		ERR("Could not Apply Filter");

	contacts_filter_destroy(filter);
	FN_END;
	return query;
}

static contacts_query_h __bluetooth_pb_query_person_number(void)
{
	FN_START;
	contacts_query_h query = NULL;

	gint status;

	status = contacts_query_create(_contacts_person_number._uri,
				&query);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	FN_END;
	return query;
}

static contacts_query_h __bluetooth_pb_query_phone_log_incoming(void)
{
	FN_START;
	gint size = 4;
	gint match[] = {
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING,
		CONTACTS_PLOG_TYPE_VOICE_REJECT,
		CONTACTS_PLOG_TYPE_VIDEO_REJECT
	};

	FN_END;
	return __bluetooth_pb_query_phone_log(match, size);
}

static contacts_query_h __bluetooth_pb_query_phone_log_outgoing(void)
{
	FN_START;
	gint size = 2;
	gint match[] = {
		CONTACTS_PLOG_TYPE_VOICE_OUTGOING,
		CONTACTS_PLOG_TYPE_VIDEO_OUTGOING
	};

	FN_END;
	return __bluetooth_pb_query_phone_log(match, size);
}

static contacts_query_h __bluetooth_pb_query_phone_log_missed(void)
{
	FN_START;
	gint size = 4;
	gint match[] = {
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING_UNSEEN,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_UNSEEN,
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING_SEEN,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_SEEN
	};

	FN_END;
	return __bluetooth_pb_query_phone_log(match, size);
}

static contacts_query_h __bluetooth_pb_query_phone_log_combined(void)
{
	FN_START;
	gint size = 10;
	gint match[] = {
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING,
		CONTACTS_PLOG_TYPE_VOICE_OUTGOING,
		CONTACTS_PLOG_TYPE_VIDEO_OUTGOING,
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING_UNSEEN,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_UNSEEN,
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING_SEEN,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_SEEN,
		CONTACTS_PLOG_TYPE_VOICE_REJECT,
		CONTACTS_PLOG_TYPE_VIDEO_REJECT
	};

	FN_END;
	return __bluetooth_pb_query_phone_log(match, size);
}

static gboolean __bluetooth_pb_get_count(PhoneBookType pb_type,
				guint *count)
{
	FN_START;
	contacts_query_h query = NULL;

	gint status;
	gint signed_count;

	switch (pb_type) {
	case TELECOM_PB:
		query = __bluetooth_pb_query_person(PBAP_ADDRESSBOOK_PHONE);
		break;
	case TELECOM_ICH:
		query = __bluetooth_pb_query_phone_log_incoming();
		break;
	case TELECOM_OCH:
		query = __bluetooth_pb_query_phone_log_outgoing();
		break;
	case TELECOM_MCH:
		query = __bluetooth_pb_query_phone_log_missed();
		break;
	case TELECOM_CCH:
		query = __bluetooth_pb_query_phone_log_combined();
		break;
#ifdef PBAP_SIM_ENABLE
	case SIM_PB:
		query = __bluetooth_pb_query_person(PBAP_ADDRESSBOOK_SIM);
		break;
#endif
	default:
		return FALSE;
	}

	if (query == NULL)
		return FALSE;

	status = contacts_db_get_count_with_query(query, &signed_count);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_query_destroy(query);
		return FALSE;
	}

	contacts_query_destroy(query);

	if (signed_count < 0)
		signed_count = 0;

	*count = (gint) signed_count;

	FN_END;
	return TRUE;
}

static gboolean __bluetooth_pb_get_count_new_missed_call(guint *count)
{
	FN_START;
	contacts_query_h query = NULL;

	gint status;
	gint signed_count;

	gint size = 2;
	gint match[] = {
		CONTACTS_PLOG_TYPE_VOICE_INCOMMING_UNSEEN,
		CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_UNSEEN
	};

	query = __bluetooth_pb_query_phone_log(match, size);

	if (query == NULL)
		return FALSE;

	status = contacts_db_get_count_with_query(query, &signed_count);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_query_destroy(query);
		return FALSE;
	}

	contacts_query_destroy(query);

	if (signed_count < 0)
		signed_count = 0;

	*count = (guint)signed_count;

	FN_END;
	return TRUE;
}

static const char *__bluetooth_pb_phone_log_get_log_type(contacts_record_h record)
{
	FN_START;
	gint status;
	gint log_type;

	status = contacts_record_get_int(record,
			_contacts_phone_log.log_type,
			&log_type);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	switch (log_type) {
	case CONTACTS_PLOG_TYPE_VOICE_INCOMMING:
	case CONTACTS_PLOG_TYPE_VIDEO_INCOMMING:
	case CONTACTS_PLOG_TYPE_VOICE_REJECT:
	case CONTACTS_PLOG_TYPE_VIDEO_REJECT:
		return "RECEIVED";
	case CONTACTS_PLOG_TYPE_VOICE_OUTGOING:
	case CONTACTS_PLOG_TYPE_VIDEO_OUTGOING:
		return "DIALED";
	case CONTACTS_PLOG_TYPE_VOICE_INCOMMING_UNSEEN:
	case CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_UNSEEN:
	case CONTACTS_PLOG_TYPE_VOICE_INCOMMING_SEEN:
	case CONTACTS_PLOG_TYPE_VIDEO_INCOMMING_SEEN:
		return "MISSED";
	default:
		return NULL;
	}
	FN_END;
}

static void __bluetooth_pb_get_vcards(BluetoothPbAgent *agent,
				PhoneBookType pb_type,
				guint64 filter,
				guint8 format,
				guint16 max_list_count,
				guint16 list_start_offset,
				GPtrArray *vcards)
{
	FN_START;
	contacts_list_h record_list = NULL;
	contacts_query_h query = NULL;

	gint status;

	gint limit;
	gint offset;

	guint property_id = 0;

	const char *attr = NULL;

	gboolean get_log = FALSE;

	/* contact offset is n - 1 of PBAP */
	offset = (gint)list_start_offset - 1;

	if ( max_list_count >= 65535)
		limit = -1;	/* contact limit -1 means unrestricted */
	else
		limit = (gint)max_list_count;

	switch (pb_type) {
	case TELECOM_PB:
#ifdef PBAP_SIM_ENABLE
	case SIM_PB:
#endif
		/* for owner */
		if (list_start_offset == 0) {
			char *vcard;

			vcard = _bluetooth_pb_vcard_contact_owner(agent->tel_number,
								filter, format);
			if (vcard)
				g_ptr_array_add(vcards, vcard);

			offset = 0;

			if (limit == 1)
				return;
			else if (limit > 1)
				limit--;
		}

		if (pb_type == TELECOM_PB)
			query = __bluetooth_pb_query_person(PBAP_ADDRESSBOOK_PHONE);
#ifdef PBAP_SIM_ENABLE
		else if(pb_type == SIM_PB)
			query = __bluetooth_pb_query_person(PBAP_ADDRESSBOOK_SIM);
#endif

		property_id = _contacts_person.id;
		break;
	case TELECOM_ICH:
		query = __bluetooth_pb_query_phone_log_incoming();
		property_id = _contacts_phone_log.id;
		attr = "RECEIVED";
		break;
	case TELECOM_OCH:
		query = __bluetooth_pb_query_phone_log_outgoing();
		property_id = _contacts_phone_log.id;
		attr = "DIALED";
		break;
	case TELECOM_MCH:
		query = __bluetooth_pb_query_phone_log_missed();
		property_id = _contacts_phone_log.id;
		attr = "MISSED";
		break;
	case TELECOM_CCH:
		query = __bluetooth_pb_query_phone_log_combined();
		property_id = _contacts_phone_log.id;
		get_log = TRUE;
		break;
	default:
		return;
	}
	DBG("Limit is = %d and offset is =%d\n", limit, offset);

	/* When limit is passed as ZERO to contacts_db_get_records_with_query API
	 * then this API will provide all available contacts in its database (unrestricted).
	 * Now consider a case when client requests for maxlistcount of 1 and start offset as 0
	 * then we have already read the owner card in above switch case and when it reads owner
	 * card it decrements the limit by 1.
	 */
	if(limit != 0)
	{
		status = contacts_db_get_records_with_query(query, offset, limit, &record_list);

		if (status != CONTACTS_ERROR_NONE) {
			contacts_list_destroy(record_list, TRUE);
			contacts_query_destroy(query);
			return;
		}

		status = contacts_list_first(record_list);

		if (status != CONTACTS_ERROR_NONE) {
			contacts_list_destroy(record_list, TRUE);
			contacts_query_destroy(query);
			return;
		}

		do {
			contacts_record_h record;

			gint id;

			gchar *vcard = NULL;

			record = NULL;
			status = contacts_list_get_current_record_p(record_list, &record);

			if (status != CONTACTS_ERROR_NONE)
				continue;
			id = 0;
			status = contacts_record_get_int(record, property_id, &id);

			if (status != CONTACTS_ERROR_NONE)
				continue;

			if (property_id == _contacts_person.id)
				vcard = _bluetooth_pb_vcard_contact(id, filter, format);
			else {
				if (get_log)
					attr = __bluetooth_pb_phone_log_get_log_type(record);

				vcard = _bluetooth_pb_vcard_call(id, filter, format, attr);
			}

			if (vcard)
				g_ptr_array_add(vcards, vcard);

		} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);
		contacts_list_destroy(record_list, TRUE);
	}

	contacts_query_destroy(query);

	FN_END;
}

static void __bluetooth_pb_get_contact_list(BluetoothPbAgent *agent,
					contacts_query_h query,
					GPtrArray *ptr_array)
{
	FN_START;
	contacts_list_h record_list = NULL;

	gint status;

	/* Add owner */
	if (ptr_array) {
		gchar *tmp;
		gchar *name;

		tmp = _bluetooth_pb_owner_name();
		name = g_strdup_printf("%s;;;;", tmp);
		g_free(tmp);

		__bluetooth_pb_list_ptr_array_add(ptr_array,
				name, agent->tel_number, 0);

		g_free(name);
	}

	status = contacts_db_get_records_with_query(query,
			-1, -1, &record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	status = contacts_list_first(record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	do {
		contacts_record_h record;

		gint id;

		record = NULL;
		status = contacts_list_get_current_record_p(record_list,
				&record);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		id = 0;
		status = contacts_record_get_int(record,
				_contacts_person_contact.person_id,
				&id);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		/* create list */
		if (ptr_array) {
			gchar *name;
			gchar *number;

			name = _bluetooth_pb_name_from_person_id(id);
			number = _bluetooth_pb_number_from_person_id(id);

			__bluetooth_pb_list_ptr_array_add(ptr_array,
					name, number, id);

			g_free(name);
			g_free(number);
		}

	} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);

	contacts_list_destroy(record_list, TRUE);
	FN_END;
}

static void __bluetooth_pb_get_phone_log_list(BluetoothPbAgent *agent,
					contacts_query_h query,
					GPtrArray *ptr_array)
{
	FN_START;
	contacts_list_h record_list = NULL;

	gint status;

	status = contacts_db_get_records_with_query(query,
			-1, -1, &record_list);

	if (status != CONTACTS_ERROR_NONE)
		return;

	status = contacts_list_first(record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	do {
		contacts_record_h record;

		gint id;

		record = NULL;
		status = contacts_list_get_current_record_p(record_list,
				&record);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		id = 0;
		status = contacts_record_get_int(record,
				_contacts_phone_log.id,
				&id);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		/* create list */
		if (ptr_array) {
			gchar *name;
			gchar *number;

			name = _bluetooth_pb_name_from_phonelog_id(id);

			number = NULL;
			contacts_record_get_str_p(record,
					_contacts_phone_log.address,
					&number);

			__bluetooth_pb_list_ptr_array_add(ptr_array,
					name, number, id);

			g_free(name);
		}

	} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);

	contacts_list_destroy(record_list, TRUE);
	FN_END;
}


static void __bluetooth_pb_get_list(BluetoothPbAgent *agent,
				PhoneBookType pb_type,
				GPtrArray *ptr_array)
{
	FN_START;
	contacts_query_h query;

	/* no requires refresh cache */
	if (ptr_array == NULL && agent->pb_type == pb_type)
		return;

	switch (pb_type) {
	case TELECOM_PB:
		query = __bluetooth_pb_query_person(PBAP_ADDRESSBOOK_PHONE);
		__bluetooth_pb_get_contact_list(agent, query, ptr_array);
		break;
	case TELECOM_ICH:
		query = __bluetooth_pb_query_phone_log_incoming();
		__bluetooth_pb_get_phone_log_list(agent, query, ptr_array);
		break;
	case TELECOM_OCH:
		query = __bluetooth_pb_query_phone_log_outgoing();
		__bluetooth_pb_get_phone_log_list(agent, query, ptr_array);
		break;
	case TELECOM_MCH:
		query = __bluetooth_pb_query_phone_log_missed();
		__bluetooth_pb_get_phone_log_list(agent, query, ptr_array);
		break;
	case TELECOM_CCH:
		query = __bluetooth_pb_query_phone_log_combined();
		__bluetooth_pb_get_phone_log_list(agent, query, ptr_array);
		break;
#ifdef PBAP_SIM_ENABLE
	case SIM_PB:
		query = __bluetooth_pb_query_person(PBAP_ADDRESSBOOK_SIM);
		__bluetooth_pb_get_contact_list(agent, query, ptr_array);
		break;
#endif
	default:
		return;
	}

	agent->pb_type = pb_type;

	if (query)
		contacts_query_destroy(query);
	FN_END;
}

static void __bluetooth_pb_get_contact_list_number(BluetoothPbAgent *agent,
						contacts_query_h query,
						gint start_index,
						gint end_index,
						GPtrArray *ptr_array)
{
	FN_START;
	contacts_list_h record_list = NULL;
	gint status;
	gint i;
	gint from;
	gint to;
	gint offset;

	from = start_index;
	to = end_index;

	if (from < 1)
		from = 1;

	if (to < 1)
		to = 1;

	offset = to - from + 1;
	if (offset <= 0)
		return;

	i = from;

	status = contacts_db_get_records_with_query(query,
			from - 1 , offset,
			&record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	status = contacts_list_first(record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	do {
		contacts_record_h record;

		gchar *display_name;
		gchar *number;

		record = NULL;
		status = contacts_list_get_current_record_p(record_list,
				&record);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		display_name = NULL;
		number = NULL;

		contacts_record_get_str_p(record,
				_contacts_person_number.display_name,
				&display_name);
		contacts_record_get_str_p(record,
				_contacts_person_number.number,
				&number);

		__bluetooth_pb_list_ptr_array_add(ptr_array,
				display_name, number, i);

		i++;
	} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);

	contacts_list_destroy(record_list, TRUE);
	FN_END;
}

static void __bluetooth_pb_get_phone_log_list_number(BluetoothPbAgent *agent,
						contacts_query_h query,
						gint start_index,
						gint end_index,
						GPtrArray *ptr_array)
{
	FN_START;
	contacts_list_h record_list = NULL;

	gint status;

	gint i;

	gint from;
	gint to;
	gint offset;

	from = start_index;
	to = end_index;

	if (from < 1)
		from = 1;

	if (to < 1)
		to = 1;

	offset = to - from + 1;
	if (offset <= 0)
		return;

	i = from;

	status = contacts_db_get_records_with_query(query,
			from - 1 , offset,
			&record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	status = contacts_list_first(record_list);
	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	do {
		contacts_record_h record = NULL;

		gint id;

		gchar *display_name;
		gchar *number;

		record = NULL;
		status = contacts_list_get_current_record_p(record_list,
				&record);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		id = 0;
		status = contacts_record_get_int(record,
				_contacts_phone_log.id,
				&id);

		display_name = _bluetooth_pb_fn_from_phonelog_id(id);

		number = NULL;
		contacts_record_get_str_p(record,
				_contacts_phone_log.address,
				&number);


		__bluetooth_pb_list_ptr_array_add(ptr_array,
				display_name, number, i);

		i++;

		g_free(display_name);

	} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);

	contacts_list_destroy(record_list, TRUE);
	FN_END;
}

static void __bluetooth_pb_get_list_number(BluetoothPbAgent *agent,
						PhoneBookType pb_type,
						gint start_index,
						gint end_index,
						GPtrArray *ptr_array)
{
	FN_START;
	contacts_query_h query;

	switch (pb_type) {
	case TELECOM_PB:
		query = __bluetooth_pb_query_person_number();
		__bluetooth_pb_get_contact_list_number(agent, query,
				start_index, end_index, ptr_array);
		break;
	case TELECOM_ICH:
		query = __bluetooth_pb_query_phone_log_incoming();
		__bluetooth_pb_get_phone_log_list_number(agent, query,
				start_index, end_index, ptr_array);
		break;
	case TELECOM_OCH:
		query = __bluetooth_pb_query_phone_log_outgoing();
		__bluetooth_pb_get_phone_log_list_number(agent, query,
				start_index, end_index, ptr_array);
		break;
	case TELECOM_MCH:
		query = __bluetooth_pb_query_phone_log_missed();
		__bluetooth_pb_get_phone_log_list_number(agent, query,
				start_index, end_index, ptr_array);
		break;
	case TELECOM_CCH:
		query = __bluetooth_pb_query_phone_log_combined();
		__bluetooth_pb_get_phone_log_list_number(agent, query,
				start_index, end_index, ptr_array);
		break;
	default:
		return;
	}

	if (query)
		contacts_query_destroy(query);
	FN_END;
}

static void __bluetooth_pb_get_contact_list_name(BluetoothPbAgent *agent,
						contacts_query_h query,
						const gchar *find_text,
						GPtrArray *ptr_array)
{
	FN_START;
	contacts_list_h record_list = NULL;

	gint status;
	gint i = 1;

	status = contacts_db_get_records_with_query(query,
			-1, -1, &record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	status = contacts_list_first(record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	do {
		contacts_record_h record;

		gchar *display_name;

		record = NULL;
		status = contacts_list_get_current_record_p(record_list,
				&record);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		display_name = NULL;
		contacts_record_get_str_p(record,
				_contacts_person_number.display_name,
				&display_name);

		if (g_str_has_prefix(display_name, find_text)) {
			gchar *number;

			number = NULL;
			contacts_record_get_str_p(record,
					_contacts_person_number.number,
					&number);

			__bluetooth_pb_list_ptr_array_add(ptr_array,
					display_name, number, i);
		}

		i++;
	} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);
	contacts_list_destroy(record_list, TRUE);
	FN_END;
}

static void __bluetooth_pb_get_phone_log_list_name(BluetoothPbAgent *agent,
						contacts_query_h query,
						const gchar *find_text,
						GPtrArray *ptr_array)
{
	FN_START;
	contacts_list_h record_list = NULL;

	gint status;

	gint i = 1;

	status = contacts_db_get_records_with_query(query,
			-1, -1,
			&record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	status = contacts_list_first(record_list);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_list_destroy(record_list, TRUE);
		return;
	}

	do {
		contacts_record_h record = NULL;

		gint id;

		gchar *display_name;

		record = NULL;
		status = contacts_list_get_current_record_p(record_list,
				&record);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		id = 0;
		status = contacts_record_get_int(record,
				_contacts_phone_log.id,
				&id);

		display_name = _bluetooth_pb_fn_from_phonelog_id(id);

		if (g_str_has_prefix(display_name, find_text)) {
			gchar *number = NULL;

			number = NULL;
			contacts_record_get_str_p(record,
					_contacts_phone_log.address,
					&number);

			__bluetooth_pb_list_ptr_array_add(ptr_array,
					display_name, number, i);
		}

		i++;

		g_free(display_name);

	} while (contacts_list_next(record_list) == CONTACTS_ERROR_NONE);

	contacts_list_destroy(record_list, TRUE);
	FN_END;
}

static void __bluetooth_pb_get_list_name(BluetoothPbAgent *agent,
					PhoneBookType pb_type,
					const gchar *find_text,
					GPtrArray *ptr_array)
{
	FN_START;
	contacts_query_h query;

	switch (pb_type) {
	case TELECOM_PB:
		query = __bluetooth_pb_query_person_number();
		__bluetooth_pb_get_contact_list_name(agent, query,
				find_text, ptr_array);
		break;
	case TELECOM_ICH:
		query = __bluetooth_pb_query_phone_log_incoming();
		__bluetooth_pb_get_phone_log_list_name(agent, query,
				find_text, ptr_array);
		break;
	case TELECOM_OCH:
		query = __bluetooth_pb_query_phone_log_outgoing();
		__bluetooth_pb_get_phone_log_list_name(agent, query,
				find_text, ptr_array);
		break;
	case TELECOM_MCH:
		query = __bluetooth_pb_query_phone_log_missed();
		__bluetooth_pb_get_phone_log_list_name(agent, query,
				find_text, ptr_array);
		break;
	case TELECOM_CCH:
		query = __bluetooth_pb_query_phone_log_combined();
		__bluetooth_pb_get_phone_log_list_name(agent, query,
				find_text, ptr_array);
		break;
	default:
		return;
	}

	if (query)
		contacts_query_destroy(query);
	FN_END;
}

static void __bluetooth_pb_list_ptr_array_add(GPtrArray *ptr_array,
						const gchar *name,
						const gchar *number,
						gint handle)
{
	FN_START;
	GValue value = { 0, };

	g_value_init(&value, DBUS_STRUCT_STRING_STRING_UINT);
	g_value_take_boxed(&value,
			dbus_g_type_specialized_construct(DBUS_STRUCT_STRING_STRING_UINT));

	dbus_g_type_struct_set(&value,
				0, g_strdup(name),
				1, g_strdup(number),
				2, handle,
				G_MAXUINT);

	g_ptr_array_add(ptr_array, g_value_get_boxed(&value));
	FN_END;
}

static void __bluetooth_pb_list_ptr_array_free(gpointer data)
{
	FN_START;
	GValue value = { 0, };

	gchar *name = NULL;
	gchar *number = NULL;

	if(data == NULL)
		return;

	g_value_init(&value, DBUS_STRUCT_STRING_STRING_UINT);
	g_value_set_boxed(&value, data);

	dbus_g_type_struct_get(&value,
			0, &name,
			1, &number,
			G_MAXUINT);

	g_free(name);
	g_free(number);
	FN_END;
}

static void __bluetooth_pb_agent_signal_handler(int signum)
{
	FN_START;
	if (mainloop) {
		g_main_loop_quit(mainloop);
	} else {
		DBG("Terminate Bluetooth PBAP agent");
		exit(0);
	}
}


static void __bluetooth_pb_contact_changed(const gchar *view_uri,
					void *user_data)
{
	FN_START;
	BluetoothPbAgent *agent;
	guint new_missed_call;

	DBG("Received contact changed cb");
	g_return_if_fail(BLUETOOTH_IS_PB_AGENT(user_data));
	agent = BLUETOOTH_PB_AGENT(user_data);

	g_object_ref(agent);
	g_signal_emit(agent, signals[CLEAR], 0);
	g_object_unref(agent);

	__bluetooth_pb_get_count_new_missed_call(&new_missed_call);

	if (new_missed_call > total_missed_call_count)
		unnotified_missed_call_count += new_missed_call - total_missed_call_count;

	INFO("Missed call count : #prev[%d], #current[%d], #unnotified[%d]",
		total_missed_call_count, new_missed_call, unnotified_missed_call_count);

	total_missed_call_count = new_missed_call;
	FN_END;
}

static void __bluetooth_pb_agent_timeout_add_seconds(BluetoothPbAgent *agent)
{
	FN_START;
	g_return_if_fail(BLUETOOTH_IS_PB_AGENT(agent));

	if(agent->timeout_id)
		g_source_remove(agent->timeout_id);

	agent->timeout_id = g_timeout_add_seconds(BLUETOOTH_PB_AGENT_TIMEOUT,
				__bluetooth_pb_agent_timeout_calback,
				agent);
	FN_END;
}

static gboolean __bluetooth_pb_agent_timeout_calback(gpointer user_data)
{
	FN_START;
	BluetoothPbAgent *agent;

	g_return_val_if_fail(BLUETOOTH_IS_PB_AGENT(user_data), FALSE);

	agent = BLUETOOTH_PB_AGENT(user_data);
	agent->timeout_id = 0;

	if (mainloop)
		g_main_loop_quit(mainloop);

	FN_END;
	return FALSE;
}

static void __bluetooth_pb_tel_callback(TapiHandle *handle,
					int result,
					void *data,
					void *user_data)
{
	FN_START;
	BluetoothPbAgent *agent;
	TelSimMsisdnList_t *number;

	g_return_if_fail(BLUETOOTH_IS_PB_AGENT(user_data));

	agent = BLUETOOTH_PB_AGENT(user_data);

	__bluetooth_pb_agent_dbus_init(agent);

	if (data != NULL) {
		number = (TelSimMsisdnList_t *)data;
		agent->tel_number = g_strdup(number->list[0].num);
	}

	tel_deinit(agent->tapi_handle);
	agent->tapi_handle = NULL;
	FN_END;
}

static void __bluetooth_pb_agent_dbus_init(BluetoothPbAgent *agent)
{
	FN_START;
	guint result = 0;
	GError *error = NULL;

	agent->bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);

	if (error != NULL) {
		ERR("Couldn't connect to system bus[%s]\n", error->message);
		g_error_free(error);
		return;
	}

	agent->proxy = dbus_g_proxy_new_for_name(agent->bus,
			DBUS_SERVICE_DBUS,
			DBUS_PATH_DBUS,
			DBUS_INTERFACE_DBUS);

	if (agent->proxy == NULL) {
		ERR("Failed to get a proxy for D-Bus\n");
		return;
	}

	if (!dbus_g_proxy_call(agent->proxy,
				"RequestName", &error,
				G_TYPE_STRING, BT_PB_SERVICE_NAME,
				G_TYPE_UINT, 0,
				G_TYPE_INVALID,
				G_TYPE_UINT, &result,
				G_TYPE_INVALID)) {
		if (error != NULL) {
			ERR("RequestName RPC failed[%s]\n", error->message);
			g_error_free(error);
		}

		g_object_unref(agent->proxy);
		agent->proxy = NULL;

		return;
	}
	DBG("result : %d %d\n", result, DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		ERR("Failed to get the primary well-known name.\n");

		g_object_unref(agent->proxy);
		agent->proxy = NULL;

		return;
	}

	g_object_unref(agent->proxy);
	agent->proxy = NULL;

	dbus_g_connection_register_g_object(agent->bus,
			BT_PB_SERVICE_OBJECT_PATH,
			G_OBJECT(agent));
	FN_END;
}

static gboolean bluetooth_pb_destroy_agent(BluetoothPbAgent *agent,
					DBusGMethodInvocation *context)
{
	FN_START;
	g_main_loop_quit(mainloop);
	FN_END;
	return TRUE;
}

int main(void)
{
	FN_START;
	BluetoothPbAgent *agent;

	gint ret = EXIT_SUCCESS;
	gint tapi_result;

	struct sigaction sa;
	DBG("Starting Bluetooth PBAP agent");

	g_type_init();

	mainloop = g_main_loop_new(NULL, FALSE);
	if (mainloop == NULL) {
		ERR("Couldn't create GMainLoop\n");
		return EXIT_FAILURE;
	}

	agent = g_object_new(BLUETOOTH_PB_TYPE_AGENT, NULL);

	/* connect contact */
	if (contacts_connect() != CONTACTS_ERROR_NONE) {
		ERR("Can not connect contacts server\n");
		g_object_unref(agent);
		return EXIT_FAILURE;
	}

	__bluetooth_pb_get_count_new_missed_call(&total_missed_call_count);

	if (contacts_db_add_changed_cb(_contacts_contact._uri,
			__bluetooth_pb_contact_changed,
			(void *)agent) != CONTACTS_ERROR_NONE) {
		ERR("Can not add changed callback");
	}

	if (contacts_db_add_changed_cb(_contacts_phone_log._uri,
			__bluetooth_pb_contact_changed,
			(void *)agent) != CONTACTS_ERROR_NONE) {
		ERR("Can not add changed callback");
	}

	/* set signal */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = __bluetooth_pb_agent_signal_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	/* init tapi */
	agent->tapi_handle = tel_init(NULL);
	tapi_result = tel_get_sim_msisdn(agent->tapi_handle,
			__bluetooth_pb_tel_callback, agent);

	if (tapi_result != TAPI_API_SUCCESS) {
		__bluetooth_pb_agent_dbus_init(agent);
	}


	__bluetooth_pb_agent_timeout_add_seconds(agent);

	g_main_loop_run(mainloop);

	if (contacts_db_remove_changed_cb(_contacts_phone_log._uri,
			__bluetooth_pb_contact_changed,
			(void *)agent) != CONTACTS_ERROR_NONE) {
		ERR("Cannot remove changed callback");
	}

	if (contacts_db_remove_changed_cb(_contacts_contact._uri,
			__bluetooth_pb_contact_changed,
			(void *)agent) != CONTACTS_ERROR_NONE) {
		ERR("Cannot remove changed callback");
	}

	if (contacts_disconnect() != CONTACTS_ERROR_NONE)
		ERR("contacts_disconnect failed \n");

	g_signal_emit(agent, signals[CLEAR], 0);

	g_object_unref(agent);

	DBG("Terminate Bluetooth PBAP agent");
	FN_END;
	return ret;
}
