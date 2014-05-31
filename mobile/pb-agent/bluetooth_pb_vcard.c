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

#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <dbus/dbus-glib.h>
#include <vconf.h>
#include <dlog.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <contacts.h>
#include <image_util.h>
#include <libexif/exif-data.h>
#include <unistd.h>

#include "bluetooth_pb_vcard.h"

#define BT_PB_AGENT "BT_PB_AGENT"
#define DBG(fmt, args...) SLOG(LOG_DEBUG, BT_PB_AGENT, "%s():%d "fmt, __func__, __LINE__, ##args)
#define ERR(fmt, args...) SLOG(LOG_ERROR, BT_PB_AGENT, "%s():%d "fmt, __func__, __LINE__, ##args)

#define DBG_SECURE(fmt, args...) SECURE_SLOGD(fmt, ##args)
#define ERR_SECURE(fmt, args...) SECURE_SLOGE(fmt, ##args)

#define VCARD_FORMAT_2_1 0x0
#define VCARD_FORMAT_3_0 0x1

#define VCARD_VERSION	(0x1)
#define VCARD_FN	(0x1 << 1)
#define VCARD_N		(0x1 << 2)
#define VCARD_PHOTO	(0x1 << 3)
#define VCARD_BDAY	(0x1 << 4)
#define VCARD_ADR	(0x1 << 5)
#define VCARD_LABEL	(0x1 << 6)	/* not supported */
#define VCARD_TEL	(0x1 << 7)
#define VCARD_EMAIL	(0x1 << 8)
#define VCARD_MAILER	(0x1 << 9)	/* not supported */
#define VCARD_TZ	(0x1 << 10)	/* not supported */
#define VCARD_GEO	(0x1 << 11)	/* not supported */
#define VCARD_TITLE	(0x1 << 12)
#define VCARD_ROLE	(0x1 << 13)
#define VCARD_LOGO	(0x1 << 14)	/* not supported */
#define VCARD_AGENT	(0x1 << 15)	/* not supported */
#define VCARD_ORG	(0x1 << 16)
#define VCARD_NOTE	(0x1 << 17)
#define VCARD_REV	(0x1 << 18)
#define VCARD_SOUND	(0x1 << 19)	/* not supported */
#define VCARD_URL	(0x1 << 20)
#define VCARD_UID	(0x1 << 21)
#define VCARD_KEY	(0x1 << 22)	/* not supported */
#define VCARD_NICKNAME	(0x1 << 23)
#define VCARD_CATEGORIES	(0x1 << 24)	/* not supported */
#define VCARD_PROID	(0x1 << 25)	/* not supported */
#define VCARD_CLASS	(0x1 << 26)	/* not supported */
#define VCARD_SORT_STRING	(0x1 << 27)	/* not supported */
#define VCARD_X_IRMC_CALL_DATETIME	(0x1 << 28)

#define QP_ENC_LEN	3
#define LINEBREAK_LEN	75

typedef struct {
	const char *src;
	const char *dest;
	int ret;
	contacts_record_h person;
	GString *string;
} bt_image_info_t;

gchar dest_thumb_path[255];
#define PBAP_IMAGE_THUMB_SIZE 48
#define PBAP_THMB_PATH "_thumb"
#define PBAP_THUMB_FILE_SIZE 4096
#define PBAP_IMAGE_ENCODE_QUALITY	90
#define PBAP_SECURITY_FILE_GROUP 6005
#define PBAP_SECURITY_DEFAULT_PERMISSION 0660
#define PBAP_SECURITY_DIR_DEFAULT_PERMISSION 0770
#define PBAP_SECURITY_IMAGE_PERMISSION 0440

static gchar *__bluetooth_pb_vcard_escape(const gchar *str);

static gchar *__bluetooth_pb_vcard_strv_concat(gchar **strv,
					const gchar *delimeter);

static gboolean __bluetooth_pb_vcard_qp_encode_check(const gchar *str);

static gint __bluetooth_pb_vcard_qp_encode_strlen(const gchar *str,
						gint len);

static void __bluetooth_pb_vcard_qp_encode_append_to_hex(GString *string,
							const gchar *str,
							gint len,
							gint *line_pos);

static void __bluetooth_pb_vcard_qp_encode_append_printable_c(GString *string,
							gchar ch,
							gint *line_pos);

static void __bluetooth_pb_vcard_qp_encode_append(GString *string,
						const gchar *str,
						gint len,
						gint *line_pos);

static gchar *__bluetooth_pb_vcard_qp_encode(const gchar *str);

static void __bluetooth_pb_vcard_append_param_v21(GString *string,
						 const gchar *param);

static void __bluetooth_pb_vcard_append_qp_encode_v21(GString *string,
					const gchar *name,
					const gchar *param,
					const gchar *value);

static void __bluetooth_pb_vcard_append_base64_encode_v21(GString *string,
							const gchar *name,
							const gchar *param,
							const gchar *value,
							gsize len,
							gboolean folding);

static void __bluetooth_pb_vcard_append_n_v21(GString *string,
					contacts_record_h contact);

static void __bluetooth_pb_vcard_append_phonetic_first_v21(GString *string,
					contacts_record_h contact);

static void __bluetooth_pb_vcard_append_tel_v21(GString *string,
						contacts_record_h conatct);

static void __bluetooth_pb_vcard_append_fn_v21(GString *string,
					contacts_record_h person);

static void __bluetooth_pb_vcard_append_photo_v21(GString *string,
						contacts_record_h person);

static void __bluetooth_pb_vcard_append_bday_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_adr_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_email_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_title_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_role_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_org_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_note_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_rev_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_url_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_uid_v21(GString *string,
						contacts_record_h contact);

static void __bluetooth_pb_vcard_append_v30(GString *string,
					const gchar *name,
					const gchar *param,
					const gchar *value);

static void __bluetooth_pb_vcard_remove_v30(GString *string,
					const gchar *property_name);

static gchar *__bluetooth_pb_vcard_filter_v30(const gchar *vcard,
					guint64 filter);

static gchar *__bluetooth_pb_vcard_real_contact_valist_v21(gint person_id,
							gint phonelog_id,
							guint64 filter,
							const gchar *first_name,
							va_list args);

static gchar *__bluetooth_pb_vcard_real_contact_valist_v30(gint person_id,
							gint phonelog_id,
							guint64 filter,
							const gchar *first_name,
							va_list args);

static gchar *__bluetooth_pb_vcard_real_contact_with_properties(gint person_id,
							gint phonelog_id,
							guint64 filter,
							guint8 format,
							const gchar *first_name,
							...);

static gchar *__bluetooth_pb_vcard_real_call_v21(gint phonelog_id,
						guint filter,
						const gchar *attr);

static gchar *__bluetooth_pb_vcard_real_call_v30(gint phonelog_id,
						guint filter,
						const gchar *attr);

static gchar *__bluetooth_pb_vcard_real_call(gint phonelog_id,
					guint filter,
					guint8 format,
					const gchar *attr);

static gchar **__bluetooth_pb_contact_add_field_str(contacts_record_h record,
						int *field,
						gint field_size);

static gchar **__bluetooth_pb_contact_tel_param(contacts_record_h number);

static gchar *__bluetooth_pb_contact_photo_type(const gchar *filename);

static gchar **__bluetooth_pb_contact_addr(contacts_record_h address);

static gchar **__bluetooth_pb_contact_addr_param(contacts_record_h address);

static gchar *__bluetooth_pb_phonelog_datetime(gint phonelog_id);

static gchar *__bluetooth_pb_name_from_contact(contacts_record_h contact);

static gchar *__bluetooth_pb_phonetic_name_from_contact(contacts_record_h contact);

static gchar *__bluetooth_pb_number_from_contact(contacts_record_h contact);

static gint __bluetooth_pb_person_id_from_phonelog_id(gint phonelog_id);


static gchar *__bluetooth_pb_vcard_escape(const gchar *str)
{
	GString *escaped;

	gchar *st = NULL;
	gchar *pos = NULL;

	if (str == NULL)
		return NULL;

	escaped = g_string_new(NULL);

	st = (gchar *)str;
	pos = st;

	while (*pos != '\0') {
		if (*pos == ';') {
			g_string_append_len(escaped, st, (pos - st));
			g_string_append(escaped, "\\;");

			pos++;
			st = pos;
		} else {
			pos++;
		}
	}

	g_string_append_len(escaped, st, (pos - st));
	return g_string_free(escaped, FALSE);
}

static gchar *__bluetooth_pb_vcard_strv_concat(gchar **strv,
					const gchar *separator)
{
	GString *string = g_string_new(NULL);
	gint i;

	for (i = 0; strv[i] != NULL; i++) {
		if (i > 0)
			g_string_append(string, ";");

		g_string_append(string, strv[i]);
	}

	return g_string_free(string, FALSE);
}

static gboolean __bluetooth_pb_vcard_qp_encode_check(const gchar *str)
{
	gchar *pos = NULL;

	if (str == NULL)
		return FALSE;

	pos = (gchar *)str;
	while (*pos != '\0') {
		/* ascii code ' ' : 32, '~' : 126 */
		if ((guchar)*pos < ' ' || (guchar)*pos > '~')
			return TRUE;

		pos++;
	}
	return FALSE;
}

/* get string length, which convert to quoted-printable encoding */
static gint __bluetooth_pb_vcard_qp_encode_strlen(const gchar *str,
						gint len)
{
	gchar *pos;

	gint count = 0;
	gint length = len;

	if (str == NULL)
		return 0;

	if (strlen(str) < len)
		length = -1;

	pos = (gchar *)str;

	while (*pos != '\0' && (((pos - str) < length) || length < 0)) {
		if ((guchar)*pos == '\t') {
			count++;
			pos++;
			continue;
		}

		if ((guchar)*pos < ' ' || (guchar)*pos == '=') {
			count += QP_ENC_LEN;
			pos++;
			continue;
		}

		/* check no-ascii utf-8 character */
		if ((guchar)*pos > '~') {

			gchar *next;

			next = g_utf8_next_char(pos);

			count += QP_ENC_LEN * (next - pos);
			pos = next;
			continue;
		}

		pos++;
		count++;
	}

	return count;
}

/* convert to quoted printable code */
static void __bluetooth_pb_vcard_qp_encode_append_to_hex(GString *string,
							const gchar *str,
							gint len,
							gint *line_pos)
{
	int i;

	if (str == NULL || len == 0)
		return;

	/* add soft linebreak when it exceed */
	if ((*line_pos + (QP_ENC_LEN * len) > LINEBREAK_LEN)) {
		g_string_append(string, "=\r\n");
		*line_pos = 0;
	}

	for (i = 0; i < len; i++) {
		g_string_append_printf(string, "=%02X", (guchar)*(str+i));
		*line_pos += QP_ENC_LEN;
	}
}

/* append plain visiable ascii character */
static void __bluetooth_pb_vcard_qp_encode_append_printable_c(GString *string,
							gchar ch,
							gint *line_pos)
{
	/* add soft linebreak when it exceed */
	if (*line_pos + 1 > LINEBREAK_LEN) {
		g_string_append(string, "=\r\n");
		*line_pos = 0;
	}
	g_string_append_c(string, ch);
	(*line_pos)++;
}

static void __bluetooth_pb_vcard_qp_encode_append(GString *string,
						const gchar *str,
						gint len,
						gint *line_pos)
{
	gint length;
	gint encode_len;

	gint i = 0;

	if (string == NULL)
		return;

	encode_len = __bluetooth_pb_vcard_qp_encode_strlen(str, len);

	/* add soft linebreak when it exceed */
	if (((*line_pos + encode_len) > LINEBREAK_LEN) && (*line_pos > 1)) {
		g_string_append(string, "=\r\n");
		*line_pos = 0;
	}

	length = strlen(str);
	if (length > len)
		length = len;

	while (i < len) {
		gchar *pos;

		pos = ((gchar *)str) + i;

		/* converts invisiable character and escape character '='
			to quoted-printable */
		if ((guchar)*pos != '\t' &&
				((guchar)*pos < ' ' || (guchar)*pos == '=')) {
			__bluetooth_pb_vcard_qp_encode_append_to_hex(string,
							pos, 1, line_pos);
			i++;

			continue;
		}

		/* converts non-ascii utf-8 character to quoted-printable */
		if ((guchar)*pos > '~') {
			gchar *next;
			int ch_len;

			next = g_utf8_next_char(pos);

			ch_len = next - pos;
			__bluetooth_pb_vcard_qp_encode_append_to_hex(string,
							pos, ch_len, line_pos);
			i += ch_len;

			continue;
		}

		__bluetooth_pb_vcard_qp_encode_append_printable_c(string, *pos,
								line_pos);
		i++;
	}
}

static gchar *__bluetooth_pb_vcard_qp_encode(const gchar *str)
{
	GString *enc;

	gchar *st_pos;
	gchar *pos;

	gint line_pos = 0;

	if (str == NULL)
		return NULL;

	enc = g_string_new(NULL);

	st_pos = (gchar *)str;
	pos = (gchar *)str;

	while (*pos != '\0') {
		/* split string with given delimeter ' ' or '\t' */
		if (*pos == ' ' || *pos == '\t') {
			__bluetooth_pb_vcard_qp_encode_append(enc, st_pos,
					(pos - st_pos), &line_pos);

			st_pos = pos;
			pos++;

			continue;
		}

		/* split string with given delimeter '\r', '\n' or
			'\r\n' - newline */
		if (*pos == '\r' || *pos == '\n') {
			__bluetooth_pb_vcard_qp_encode_append(enc, st_pos,
					(pos - st_pos), &line_pos);

			/* converts newline to qp_encode with soft linebreak
			 for example, converts \r\n to =0D=0A=\r\n */
			__bluetooth_pb_vcard_qp_encode_append_to_hex(enc,
							"\r\n", 2, &line_pos);
			g_string_append(enc, "=\r\n ");

			line_pos = 1;

			if (*pos == '\r' && *(pos + 1) == '\n')
				pos += 2;
			else
				pos++;

			st_pos = pos;

			continue;
		}

		pos++;
	}

	__bluetooth_pb_vcard_qp_encode_append(enc, st_pos,
			(pos - st_pos), &line_pos);

	return g_string_free(enc, FALSE);
}

static void __bluetooth_pb_vcard_append_param_v21(GString *string,
						 const gchar *param)
{
	gchar *pos = NULL;

	if (param == NULL)
		return;

	pos = (gchar *)param;

	/* trim ';' on first */
	while (*pos != '\0') {
		if (*pos != ';')
			break;

		pos++;
	}

	if (*pos != '\0')
		g_string_append_printf(string, ";%s", pos);
}

static void __bluetooth_pb_vcard_append_qp_encode_v21(GString *string,
						const gchar *name,
						const gchar *param,
						const gchar *value)
{
	GString *property = NULL;

	if (name == NULL)
		return;

	property = g_string_new(name);
	__bluetooth_pb_vcard_append_param_v21(property, param);

	if (__bluetooth_pb_vcard_qp_encode_check(value)) {
		gchar *enc = NULL;

		__bluetooth_pb_vcard_append_param_v21(property,
				"CHARSET=UTF-8");
		__bluetooth_pb_vcard_append_param_v21(property,
				"ENCODING=QUOTED-PRINTABLE");
		g_string_append(property, ":");

		enc = __bluetooth_pb_vcard_qp_encode(value);

		if (enc) {
			g_string_append(property, enc);
			g_free(enc);
		}
	} else {
		g_string_append(property, ":");
		if (value)
			g_string_append(property , value);
	}

	g_string_append_printf(string, "%s\r\n", property->str);

	g_string_free(property, TRUE);
}


static void __bluetooth_pb_vcard_append_base64_encode_v21(GString *string,
							const gchar *name,
							const gchar *param,
							const gchar *value,
							gsize len,
							gboolean folding)
{
	gchar *enc = NULL;

	if (name == NULL)
		return;
	DBG("base 64 encoding\n");

	g_string_append(string, name);

	__bluetooth_pb_vcard_append_param_v21(string, param);
	__bluetooth_pb_vcard_append_param_v21(string, "ENCODING=BASE64");

	g_string_append(string, ":");

	if (value == NULL)
		return;

	enc = g_base64_encode((const guchar *)value, len);

	if (folding == FALSE) {
		g_string_append(string, enc);
	} else {
		gint enc_len = strlen(enc);
		gint i = 0;

		/* count ' ' size for folding */
		gint fline_len = LINEBREAK_LEN - 1;

		for (i = 0; (i * fline_len) < enc_len; i++) {
			g_string_append(string, "\r\n ");
			if ((i * fline_len) + fline_len > enc_len)
				g_string_append(string, enc + (i * fline_len));
			else
				g_string_append_len(string, enc +
						(i * fline_len), fline_len);
		}

		/* some application requires more \r\n */
		g_string_append(string, "\r\n");
	}
	g_string_append(string, "\r\n");
	DBG("base 64 encoding\n");

	g_free(enc);
}

static void __bluetooth_pb_vcard_append_n_v21(GString *string,
					contacts_record_h contact)
{
	gchar *str;

	str = __bluetooth_pb_name_from_contact(contact);
	__bluetooth_pb_vcard_append_qp_encode_v21(string, "N", NULL, str);

	g_free(str);
}

static void __bluetooth_pb_vcard_append_phonetic_first_v21(GString *string,
					contacts_record_h contact)
{
	gchar *str;

	str = __bluetooth_pb_phonetic_name_from_contact(contact);

	if (str != NULL) {
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "SOUND", "X-IRMC-N", str);
		g_free(str);
	}
}

static void __bluetooth_pb_vcard_append_tel_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint i;
	gint status;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.number,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h number = NULL;

		gchar **paramv = NULL;
		gchar *param = NULL;

		gchar *tel = NULL;
		gchar *escaped = NULL;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.number, i, &number);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_str_p(number,
				_contacts_number.number,
				&tel);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escaped = __bluetooth_pb_vcard_escape(tel);

		paramv = __bluetooth_pb_contact_tel_param(number);
		param = __bluetooth_pb_vcard_strv_concat(paramv, ";");

		g_strfreev(paramv);

		__bluetooth_pb_vcard_append_qp_encode_v21(string, "TEL", param,
								escaped);

		g_free(escaped);
		g_free(param);
	}
}

static void __bluetooth_pb_vcard_append_fn_v21(GString *string,
					contacts_record_h person)
{
	gint status;

	gchar *fn = NULL;
	gchar *tmp = NULL;

	status = contacts_record_get_str_p(person,
			_contacts_person.display_name,
			&tmp);

	if (status != CONTACTS_ERROR_NONE)
		return;

	fn = __bluetooth_pb_vcard_escape(tmp);

	__bluetooth_pb_vcard_append_qp_encode_v21(string, "FN", NULL, fn);

	g_free(fn);
}

static image_util_rotation_e __bt_pbap_get_rotation_info(const char *path)
{
	ExifData *ed = NULL;
	ExifEntry *entry;
	image_util_rotation_e rotation = IMAGE_UTIL_ROTATION_NONE;
	int orientation = 0;

	ed = exif_data_new_from_file(path);
	if (ed == NULL) {
		DBG("exif_data_new_from_file : ExifData is NULL");
		return IMAGE_UTIL_ROTATION_NONE;
	}

	entry = exif_data_get_entry(ed, EXIF_TAG_ORIENTATION);
	if (entry) {
		ExifByteOrder mByteOrder = exif_data_get_byte_order(ed);
		orientation = (int)exif_get_short(entry->data, mByteOrder);
		if (orientation < 0 || orientation > 8)
			orientation = 0;
	}

	exif_data_unref(ed);

	switch (orientation) {
	case 1:	/* Top-left */
		rotation = IMAGE_UTIL_ROTATION_NONE;
		break;
	case 2:	/* Top-right */
		rotation = IMAGE_UTIL_ROTATION_FLIP_HORZ;
		break;
	case 3:	/* Bottom-right */
		rotation = IMAGE_UTIL_ROTATION_180;
		break;
	case 4:	/* Bottom-left */
		rotation = IMAGE_UTIL_ROTATION_FLIP_VERT;
		break;
	case 6:	/* Right-top */
		rotation = IMAGE_UTIL_ROTATION_90;
		break;
	case 8:	/* Left-bottom */
		rotation = IMAGE_UTIL_ROTATION_270;
		break;
	case 5:	/* Left-top */
	case 7:	/* Right-bottom */
	case 0:
	default:
		break;
	};

	return rotation;
}


static bool __bt_pbap_image_util_supported_jpeg_colorspace_cb(
			image_util_colorspace_e colorspace, void *user_data)
{
	unsigned char *img_target = 0;
	unsigned char *img_source = 0;
	gchar *type = NULL;
	gchar *param = NULL;
	gchar *contents = NULL;
	int width = 0;
	int height = 0;
	int resized_width = 0;
	int resized_height = 0;
	int dest_fd = 0;
	unsigned int size_decode = 0;
	image_util_rotation_e rotation;
	image_util_error_e ret = 0;
	gsize len = 0;

	bt_image_info_t *info = (bt_image_info_t *)user_data;
	rotation = __bt_pbap_get_rotation_info(info->src);
	ret = image_util_decode_jpeg(info->src, colorspace, &img_source, &width,
							&height, &size_decode);
	if (ret != IMAGE_UTIL_ERROR_NONE) {
		ERR("Can not decode");
		memset(info, 0x00, sizeof(bt_image_info_t));
		return true;
	}

	DBG("decoding completed width = %d, height = %d, size = %d\n", width,
							height, size_decode);
	if (width > PBAP_IMAGE_THUMB_SIZE  || height > PBAP_IMAGE_THUMB_SIZE) {
		if (width <= 0 || height <= 0) {
			free(img_source);
			ERR("image size error(%d)", PBAP_IMAGE_THUMB_SIZE);
			memset(info, 0x00, sizeof(bt_image_info_t));
			return false;
		}

		if (width > height) {
			resized_width = PBAP_IMAGE_THUMB_SIZE ;
			resized_height = height * PBAP_IMAGE_THUMB_SIZE / width;
		} else {
			resized_height = PBAP_IMAGE_THUMB_SIZE;
			resized_width = width * PBAP_IMAGE_THUMB_SIZE / height;
		}

		if (resized_height % 8)
			resized_height += 8 - (resized_height % 8);
		if (resized_width % 8)
			resized_width += 8 - (resized_width % 8);

		DBG("original size[%d, %d] changed to resize[%d,%d]", width,
					height, resized_width, resized_height);

		ret = image_util_calculate_buffer_size(resized_width,
							resized_height,
							colorspace ,
							&size_decode);

		img_target = g_malloc0(size_decode);

		/* do resize */
		ret = image_util_resize(img_target, &resized_width,
					&resized_height, img_source, width,
					height, colorspace);
		if (ret != IMAGE_UTIL_ERROR_NONE) {
			ERR("image_util_resize failed(%d)", ret);
			g_free(img_target);
			free(img_source);
			memset(info, 0x00, sizeof(bt_image_info_t));
			return false;
		}
		free(img_source);
	} else {
		resized_width = width;
		resized_height = height;
		img_target = img_source;
	}
	DBG("Resized w = %d, ht = %d, size = %d\n", width, height, size_decode);

	if (IMAGE_UTIL_ROTATION_NONE != rotation) {
		int rotated_width, rotated_height;
		unsigned char *img_rotate = 0;
		img_rotate = g_malloc0(size_decode);
		image_util_rotate(img_rotate, &rotated_width, &rotated_height,
					rotation, img_target, resized_width,
					resized_height, colorspace);
		resized_width = rotated_width;
		resized_height = rotated_height;
		g_free(img_target);
		img_target = img_rotate;
	}

	/* image encode */
	ret = image_util_encode_jpeg(img_target, resized_width, resized_height,
					colorspace, PBAP_IMAGE_ENCODE_QUALITY,
					info->dest);
	g_free(img_target);
	if (ret != IMAGE_UTIL_ERROR_NONE) {
		ERR("image_util_encode_jpeg failed(%d)", ret);
		info->ret = CONTACTS_ERROR_INTERNAL;
		goto done;
	}
	DBG("Encoding done\n");

	dest_fd = open(info->dest, O_RDONLY);
	if (dest_fd < 0) {
		ERR("System : Open Failed(%d)", errno);
		ERR_SECURE("Open : dest path(%s)", info->dest);
		goto done;
	}

	ret = fchown(dest_fd, getuid(), PBAP_SECURITY_FILE_GROUP);
	if (0 != ret) {
		DBG("fchown Failed(%d)", errno);
		DBG_SECURE("fchown : dest path(%s)", info->dest);
		close(dest_fd);
		goto done;
	}

	ret = fchmod(dest_fd, PBAP_SECURITY_IMAGE_PERMISSION);
	if (0 != ret) {
		ERR("fchmod Failed(%d)", errno);
		ERR_SECURE("fchmod : dest path(%s)", info->dest);
		close(dest_fd);
		goto done;
	}
	close(dest_fd);

	info->ret = CONTACTS_ERROR_NONE;
	type = __bluetooth_pb_contact_photo_type(info->dest);
	DBG("Cintact image thumb type is = %s\n", type);

	if (type) {
		param = g_strdup_printf("TYPE=%s", type);
		g_free(type);
	}

	if (g_file_get_contents(info->dest, &contents, &len, NULL) == FALSE) {
		ERR("can not read file contents:%s\n", info->dest);
		goto done;
	}

	__bluetooth_pb_vcard_append_base64_encode_v21(info->string,
			"PHOTO", param, contents, len, TRUE);

done:
	g_free(param);
	g_free(contents);
	remove(info->dest);
	memset(info, 0x00, sizeof(bt_image_info_t));
	DBG("Cintact image thumb created successfuly\n");
	return false;
}

static void __bluetooth_pb_vcard_append_photo_v21(GString *string,
						contacts_record_h person)
{
	gint status;
	gchar *filename = NULL;
	int res = 0;
	bt_image_info_t img_info;
	struct stat stat_info;
	gchar *type = NULL;
	gchar *param = NULL;
	gchar *contents = NULL;
	gsize len = 0;
	char *ptr = NULL;

	DBG("PBAP +\n");

	status = contacts_record_get_str_p(person,
			_contacts_person.image_thumbnail_path,
			&filename);

	if (status != CONTACTS_ERROR_NONE || NULL == filename)
		return;

	stat_info.st_size = 0;

	if (0 > stat(filename, &stat_info)) {
		ERR("fstat failed, file does not exist %s", filename);
	}

	if (PBAP_THUMB_FILE_SIZE > stat_info.st_size) {
		DBG_SECURE("File size small, so use thubnail %s\n", filename);

		type = __bluetooth_pb_contact_photo_type(filename);
		if (type) {
			param = g_strdup_printf("TYPE=%s", type);
			g_free(type);
		}

		if (g_file_get_contents(filename, &contents, &len, NULL) ==
									FALSE) {
			ERR("can not read file contents:%s\n", filename);
			g_free(param);
			return;
		}
		DBG("Retrieved the contents of the file \n");
		__bluetooth_pb_vcard_append_base64_encode_v21(string,
				"PHOTO", param, contents, len, TRUE);

		g_free(param);
		g_free(contents);
		return;
	}

	ptr = strrchr(filename, '.');
	if (NULL != ptr) {
		memset(dest_thumb_path, 0x00, sizeof(dest_thumb_path));
		g_strlcpy(dest_thumb_path, filename, ptr - filename);
		g_strlcat(dest_thumb_path, PBAP_THMB_PATH,
						sizeof(dest_thumb_path));
		g_strlcat(dest_thumb_path, ptr, sizeof(dest_thumb_path));
		DBG("Thumbnail path is = %s", dest_thumb_path);
	}
	DBG_SECURE("filename = %s Thumbnail path is = %s", filename, dest_thumb_path);
	img_info.src = filename;
	img_info.dest = dest_thumb_path;
	img_info.ret = CONTACTS_ERROR_INTERNAL;
	img_info.person = person;
	img_info.string = string;

	res = image_util_foreach_supported_jpeg_colorspace(
		__bt_pbap_image_util_supported_jpeg_colorspace_cb, &img_info);
	if (res != IMAGE_UTIL_ERROR_NONE) {
		ERR("Image resizing is failed");
	}
	DBG("PBAP -\n");
	return;
}

static void __bluetooth_pb_vcard_append_bday_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.event, &count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h event = NULL;

		gint date;

		gchar *bday;

		contacts_event_type_e type;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.event, i, &event);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_int(event,
				_contacts_event.type,
				(gint *) &type);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		if (type != CONTACTS_EVENT_TYPE_BIRTH)
			continue;

		status = contacts_record_get_int(event,
				_contacts_event.date,
				&date);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		if (date <= 0)
			continue;

		bday = g_strdup_printf("%04d-%02d-%02d",
				(date/10000), (date/100)%100, date%100);
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "BDAY",
				NULL, bday);
		g_free(bday);
	}
}

static void __bluetooth_pb_vcard_append_adr_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.address,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h address = NULL;

		gchar **addrv;
		gchar **paramv;

		gchar *addr;
		gchar *param;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.address, i, &address);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		addrv = __bluetooth_pb_contact_addr(address);

		if (addrv == NULL)
			continue;

		addr = __bluetooth_pb_vcard_strv_concat(addrv, ";");
		g_strfreev(addrv);

		paramv = __bluetooth_pb_contact_addr_param(address);
		param = __bluetooth_pb_vcard_strv_concat(paramv, ";");
		g_strfreev(paramv);

		__bluetooth_pb_vcard_append_qp_encode_v21(string, "ADR",
				param, addr);

		g_free(param);
		g_free(addr);
	}
}

static void __bluetooth_pb_vcard_append_email_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.email,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h email = NULL;

		gchar *tmp = NULL;
		gchar *escaped;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.email, i, &email);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_str_p(email,
				_contacts_email.email,
				&tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escaped = __bluetooth_pb_vcard_escape(tmp);
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "EMAIL", NULL,
								escaped);

		g_free(escaped);
	}
}

static void __bluetooth_pb_vcard_append_title_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.company,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h company = NULL;

		char *title = NULL;
		gchar *escaped;

		status = contacts_record_get_child_record_at_p(contact,
					_contacts_contact.company, i, &company);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_str_p(company,
				_contacts_company.job_title,
				&title);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escaped = __bluetooth_pb_vcard_escape(title);
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "TITLE", NULL,
								escaped);

		g_free(escaped);
	}
}

static void __bluetooth_pb_vcard_append_role_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.company,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h company = NULL;

		char *role = NULL;
		gchar *escaped;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.company, i, &company);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_str_p(company,
				_contacts_company.role,
				&role);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escaped = __bluetooth_pb_vcard_escape(role);
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "ROLE", NULL,
								escaped);

		g_free(escaped);
	}
}

static void __bluetooth_pb_vcard_append_org_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.company,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h company = NULL;

		GString *str;

		gchar *name = NULL;
		gchar *department = NULL;

		gint name_status;
		gint department_status;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.company, i, &company);

		if (status != CONTACTS_ERROR_NONE)
			continue;


		name_status = contacts_record_get_str_p(company,
				_contacts_company.name,
				&name);

		department_status = contacts_record_get_str_p(company,
				_contacts_company.department,
				&department);

		if ((name_status != CONTACTS_ERROR_NONE) &&
				(department_status != CONTACTS_ERROR_NONE))
			continue;

		str = g_string_new(NULL);

		if (name) {
			gchar *escaped;

			escaped = __bluetooth_pb_vcard_escape(name);
			g_string_append(str, escaped);
			g_free(escaped);
		}

		g_string_append(str, ";");

		if (department) {
			gchar *escaped;

			escaped = __bluetooth_pb_vcard_escape(department);
			g_string_append(str, escaped);
			g_free(escaped);
		}

		__bluetooth_pb_vcard_append_qp_encode_v21(string, "ORG", NULL,
								str->str);

		g_string_free(str, TRUE);

	}
}

static void __bluetooth_pb_vcard_append_note_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.note,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h note = NULL;

		char *tmp = NULL;
		gchar *escaped;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.note, i, &note);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_str_p(note,
				_contacts_note.note,
				&tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escaped = __bluetooth_pb_vcard_escape(tmp);
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "NOTE", NULL,
								escaped);

		g_free(escaped);
	}
}

static void __bluetooth_pb_vcard_append_rev_v21(GString *string,
						contacts_record_h contact)
{
	gint time = 0;
	gint status;

	gchar *rev;
	struct tm result;

	status = contacts_record_get_int(contact,
			_contacts_contact.changed_time,
			&time);

	if (status != CONTACTS_ERROR_NONE)
		return;

	if (time <= 0)
		return;

	gmtime_r((const time_t *)(&time), &result);

	rev = g_strdup_printf("%04d-%02d-%02dT%02d:%02d:%02dZ",
			(1900 + result.tm_year), (1 + result.tm_mon),
			result.tm_mday, result.tm_hour, result.tm_min,
			result.tm_sec);

	__bluetooth_pb_vcard_append_qp_encode_v21(string, "REV", NULL, rev);

	g_free(rev);
}

static void __bluetooth_pb_vcard_append_url_v21(GString *string,
						contacts_record_h contact)
{
	guint count = 0;

	gint i;
	gint status;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.url,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return;

	for (i = 0; i < count; i++) {
		contacts_record_h url = NULL;

		gchar *tmp = NULL;
		gchar *escaped;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.url, i, &url);

		if (status != CONTACTS_ERROR_NONE)
			return;

		if (url == NULL)
			continue;

		status = contacts_record_get_str_p(url,
				_contacts_url.url,
				&tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escaped = __bluetooth_pb_vcard_escape(tmp);
		__bluetooth_pb_vcard_append_qp_encode_v21(string, "URL", NULL,
								escaped);

		g_free(escaped);
	}
}

static void __bluetooth_pb_vcard_append_uid_v21(GString *string,
						contacts_record_h contact)
{
	int status;

	gchar *uid = NULL;
	gchar *escaped;

	status = contacts_record_get_str_p(contact,
			_contacts_contact.uid,
			&uid);

	if (status != CONTACTS_ERROR_NONE)
		return;

	escaped = __bluetooth_pb_vcard_escape(uid);
	__bluetooth_pb_vcard_append_qp_encode_v21(string, "UID", NULL, escaped);

	g_free(escaped);
}

static void __bluetooth_pb_vcard_append_v30(GString *string,
					const gchar *name,
					const gchar *param,
					const gchar *value)
{
	if (string == NULL)
		return;
	if (name == NULL)
		return;

	g_string_append(string, name);

	if (param)
		g_string_append_printf(string, ";%s", param);

	g_string_append(string, ":");

	if (value)
		g_string_append(string, value);

	g_string_append(string, "\r\n");
}

static void __bluetooth_pb_vcard_remove_v30(GString *string,
					const gchar *property_name)
{
	gchar *pos = NULL;
	gchar *st_pos = NULL;

	gboolean matched = FALSE;

	if (string == NULL || property_name == NULL)
		return;

	pos = string->str;

	while (*pos != '\0') {
		if (matched == FALSE) {
			if (g_ascii_strncasecmp(pos, "\r\n", 2) == 0) {
				gint attrlen = 0;

				st_pos = pos;
				pos += 2;

				attrlen = strlen(property_name);
				if (g_ascii_strncasecmp(pos, property_name,
								attrlen) == 0) {
					pos += attrlen;

					if (*pos == ':' || *pos == ';') {
						matched = TRUE;
						pos++;
					}
				}
				continue;
			}
		} else {
			if (g_ascii_strncasecmp(pos, "\r\n", 2) == 0) {
				pos += 2;

				if (*pos != ' ' && *pos != '\t') {
					/* +2 means move over \r\n */
					g_string_erase(string,
						(st_pos+2)-(string->str),
						pos-(st_pos+2));

					pos = st_pos;
					matched = FALSE;
				}
				continue;
			}
		}

		pos++;
	}
}

static gchar *__bluetooth_pb_vcard_filter_v30(const gchar *vcard,
					guint64 filter)
{
	GString *string = NULL;

	if (vcard == NULL)
		return NULL;

	string = g_string_new(vcard);

	if ((filter & VCARD_PHOTO) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "PHOTO");

	if ((filter & VCARD_BDAY) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "BDAY");

	if ((filter & VCARD_ADR) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "ADR");

	if ((filter & VCARD_EMAIL) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "EMAIL");

	if ((filter & VCARD_TITLE) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "TITLE");

	if ((filter & VCARD_ROLE) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "ROLE");

	if ((filter & VCARD_ORG) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "ORG");

	if ((filter & VCARD_NOTE) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "NOTE");

	if ((filter & VCARD_REV) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "REV");

	if ((filter & VCARD_URL) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "URL");

	if ((filter & VCARD_UID) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "UID");

	if ((filter & VCARD_NICKNAME) == 0)
		__bluetooth_pb_vcard_remove_v30(string, "NICKNAME");

	return g_string_free(string, FALSE);
}

static gchar *__bluetooth_pb_vcard_real_contact_valist_v21(gint person_id,
							gint phonelog_id,
							guint64 filter,
							const gchar *first_name,
							va_list args)
{
	contacts_record_h person = NULL;
	contacts_record_h contact = NULL;

	GString *str = NULL;

	gint contact_id = 0;
	gint status;

	guint64 f = filter;

	const gchar *name = first_name;

	status = contacts_db_get_record(_contacts_person._uri,
			person_id,
			&person);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_record_get_int(person,
			_contacts_person.display_contact_id,
			&contact_id);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_db_get_record(_contacts_contact._uri,
			contact_id,
			&contact);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(person, TRUE);
		return NULL;;
	}

	if (f == 0)
		f = ~f;

	DBG("filter[%x]\n", filter);

	str = g_string_new("BEGIN:VCARD\r\nVERSION:2.1\r\n");

	/* N, TEL is default */
	__bluetooth_pb_vcard_append_n_v21(str, contact);

	if (phonelog_id > 0) {
		gchar *number;

		number = _bluetooth_pb_number_from_phonelog_id(phonelog_id);
		__bluetooth_pb_vcard_append_qp_encode_v21(str, "TEL", "X-0",
									number);
		g_free(number);


	} else {
		__bluetooth_pb_vcard_append_tel_v21(str, contact);
	}

	if (f & VCARD_FN)
		__bluetooth_pb_vcard_append_fn_v21(str, person);
/*	Need to check filter
	if (f & VCARD_SOUND)*/
		__bluetooth_pb_vcard_append_phonetic_first_v21(str, contact);
	if (f & VCARD_PHOTO)
		__bluetooth_pb_vcard_append_photo_v21(str, person);
	if (f & VCARD_BDAY)
		__bluetooth_pb_vcard_append_bday_v21(str, contact);
	if (f & VCARD_ADR)
		__bluetooth_pb_vcard_append_adr_v21(str, contact);
	if (f & VCARD_EMAIL)
		__bluetooth_pb_vcard_append_email_v21(str, contact);
	if (f & VCARD_TITLE)
		__bluetooth_pb_vcard_append_title_v21(str, contact);
	if (f & VCARD_ROLE)
		__bluetooth_pb_vcard_append_role_v21(str, contact);
	if (f & VCARD_ORG)
		__bluetooth_pb_vcard_append_org_v21(str, contact);
	if (f & VCARD_NOTE)
		__bluetooth_pb_vcard_append_note_v21(str, contact);
	if (f & VCARD_REV)
		__bluetooth_pb_vcard_append_rev_v21(str, contact);
	if (f & VCARD_URL)
		__bluetooth_pb_vcard_append_url_v21(str, contact);
	if (f & VCARD_UID)
		__bluetooth_pb_vcard_append_uid_v21(str, contact);

	while (name) {
		const gchar *param = va_arg(args, const gchar *);
		const gchar *value = va_arg(args, const gchar *);

		if (value) {
			gchar *escaped = NULL;

			escaped = __bluetooth_pb_vcard_escape(value);
			__bluetooth_pb_vcard_append_qp_encode_v21(str, name,
								param, escaped);

			g_free(escaped);
		}

		name = va_arg(args, const gchar *);
	}

	g_string_append(str, "END:VCARD\r\n");

	contacts_record_destroy(contact, TRUE);
	contacts_record_destroy(person, TRUE);

	return g_string_free(str, FALSE);
}


static gchar *__bluetooth_pb_vcard_real_contact_valist_v30(gint person_id,
							gint phonelog_id,
							guint64 filter,
							const gchar *first_name,
							va_list args)
{
	contacts_record_h person = NULL;

	GString *str = NULL;

	gint status;

	const gchar *name = first_name;
	gchar *vcard = NULL;

	status = contacts_db_get_record(_contacts_person._uri,
			person_id,
			&person);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_vcard_make_from_person(person, &vcard);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_record_destroy(person, TRUE);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	str = g_string_new(vcard);
	g_free(vcard);

	/* append contents on vcard */
	while (name) {
		const gchar *param = va_arg(args, const gchar *);
		const gchar *value = va_arg(args, const gchar *);

		__bluetooth_pb_vcard_append_v30(str, name, param, value);

		name = va_arg(args, const gchar *);
	}

	/* if phonelog_id exist, we shall show only the phone number, which was
	   used for that phone log */
	if (phonelog_id > 0) {
		gchar *number;

		__bluetooth_pb_vcard_remove_v30(str, "TEL");

		number = _bluetooth_pb_number_from_phonelog_id(phonelog_id);
		__bluetooth_pb_vcard_append_v30(str, "TEL", NULL, number);
		g_free(number);
	}

	vcard = g_string_free(str, FALSE);

	/* temporary fixed for some application crash */
	if (filter == 0)
		filter = ~VCARD_NOTE;

	if (filter) {
		gchar *new_vcard = NULL;

		new_vcard = __bluetooth_pb_vcard_filter_v30(vcard, filter);

		if (new_vcard) {
			g_free(vcard);
			vcard = new_vcard;
		}
	}

	return vcard;
}


static gchar *__bluetooth_pb_vcard_real_contact_with_properties(gint person_id,
							gint phonelog_id,
							guint64 filter,
							guint8 format,
							const gchar *first_name,
							...)
{
	DBG(" format [%d]\n", format);
	gchar *vcard = NULL;
	va_list args;

	va_start(args, first_name);

	switch (format) {
	case VCARD_FORMAT_3_0:
		vcard = __bluetooth_pb_vcard_real_contact_valist_v30(person_id,
				phonelog_id, filter,
				first_name, args);
		break;
	case VCARD_FORMAT_2_1:
	default:
		vcard = __bluetooth_pb_vcard_real_contact_valist_v21(person_id,
				phonelog_id, filter,
				first_name, args);
		break;
	}

	va_end(args);

	return vcard;
}

static gchar *__bluetooth_pb_vcard_real_call_v21(gint phonelog_id,
						guint filter,
						const char *attr)
{
	GString *str;
	gchar *number;

	str = g_string_new("BEGIN:VCARD\r\nVERSION:2.1\r\n");

	__bluetooth_pb_vcard_append_qp_encode_v21(str, "N", NULL, NULL);

	number = _bluetooth_pb_number_from_phonelog_id(phonelog_id);
	__bluetooth_pb_vcard_append_qp_encode_v21(str, "TEL", "X-0", number);
	g_free(number);

	if (((filter == 0) || (filter & VCARD_X_IRMC_CALL_DATETIME))
			&& attr) {
		gchar *datetime = NULL;

		datetime = __bluetooth_pb_phonelog_datetime(phonelog_id);
		__bluetooth_pb_vcard_append_qp_encode_v21(str,
							"X-IRMC-CALL-DATETIME",
							attr, datetime);
		g_free(datetime);
	}

	g_string_append(str, "END:VCARD\r\n");

	return g_string_free(str, FALSE);
}

static gchar *__bluetooth_pb_vcard_real_call_v30(gint phonelog_id,
						guint filter,
						const gchar *attr)
{
	GString *str;
	gchar *number;

	str = g_string_new("BEGIN:VCARD\r\nVERSION:3.0\r\n");

	__bluetooth_pb_vcard_append_v30(str, "N", NULL, NULL);
	__bluetooth_pb_vcard_append_v30(str, "FN", NULL, NULL);

	number = _bluetooth_pb_number_from_phonelog_id(phonelog_id);
	__bluetooth_pb_vcard_append_v30(str, "TEL", NULL, number);
	g_free(number);

	if (((filter == 0) || (filter & VCARD_X_IRMC_CALL_DATETIME))
			&& attr) {
		gchar *datetime = NULL;

		datetime = __bluetooth_pb_phonelog_datetime(phonelog_id);
		__bluetooth_pb_vcard_append_v30(str,
				"X-IRMC-CALL-DATETIME", attr, datetime);
		g_free(datetime);
	}

	g_string_append(str, "END:VCARD\r\n");

	return g_string_free(str, FALSE);
}

static gchar *__bluetooth_pb_vcard_real_call(gint phonelog_id,
					guint filter,
					guint8 format,
					const gchar *attr)
{
	DBG("\n");
	gchar *vcard = NULL;

	switch (format) {
	case VCARD_FORMAT_3_0:
		vcard = __bluetooth_pb_vcard_real_call_v30(phonelog_id,
				filter, attr);
		break;
	case VCARD_FORMAT_2_1:
	default:
		vcard = __bluetooth_pb_vcard_real_call_v21(phonelog_id,
				filter, attr);
		break;
	}

	return vcard;
}

static gchar **__bluetooth_pb_contact_add_field_str(contacts_record_h record,
						int *field,
						gint field_size)
{
	gchar **strv;

	gint status;
	gint i;

	gboolean valid = FALSE;

	/* check empty field */
	for (i = 0; i < field_size; i++) {
		gchar *tmp = NULL;

		status = contacts_record_get_str_p(record, field[i], &tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		if (tmp) {
			valid = TRUE;
			break;
		}
	}

	if (valid == FALSE)
		return NULL;

	strv = g_new0(gchar *, field_size + 1);

	for (i = 0; i < field_size; i++) {
		gchar *tmp;

		status = contacts_record_get_str_p(record, field[i], &tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		if (tmp == NULL)
			strv[i] = g_strdup("");
		else
			strv[i] = __bluetooth_pb_vcard_escape(tmp);
	}

	return strv;
}

static gchar **__bluetooth_pb_contact_tel_param(contacts_record_h number)
{
	gchar **strv = NULL;

	const gint TEL_PARAM_LEN = 13;

	gint status;
	gint i = 0;

	contacts_number_type_e type;

	bool is_default = false;

	status = contacts_record_get_bool(number, _contacts_number.is_default,
			&is_default);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	strv = g_new0(char *, TEL_PARAM_LEN + 1);/* tel param max size is 13 */

	if (is_default) {
		strv[i] = g_strdup("PREF");
		i++;
	}

	status = contacts_record_get_int(number,
			_contacts_number.type,
			(gint *)&type);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	if (type & CONTACTS_NUMBER_TYPE_HOME) {
		strv[i] = g_strdup("HOME");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_WORK) {
		strv[i] = g_strdup("WORK");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_VOICE) {
		strv[i] = g_strdup("VOICE");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_FAX) {
		strv[i] = g_strdup("FAX");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_MSG) {
		strv[i] = g_strdup("MSG");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_CELL) {
		strv[i] = g_strdup("CELL");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_PAGER) {
		strv[i] = g_strdup("PAGER");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_BBS) {
		strv[i] = g_strdup("BBS");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_MODEM) {
		strv[i] = g_strdup("MODEM");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_CAR) {
		strv[i] = g_strdup("CAR");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_ISDN) {
		strv[i] = g_strdup("ISDN");
		i++;
	}

	if (type & CONTACTS_NUMBER_TYPE_VIDEO) {
		strv[i] = g_strdup("VIDEO");
		i++;
	}

	/* CTS_NUM_TYPE_PCS is not part of vcard2.1 */
	return strv;
}


static gchar *__bluetooth_pb_contact_photo_type(const gchar *filename)
{
	gchar *filetype = NULL;
	gchar *ext = NULL;

	if (g_file_test(filename, G_FILE_TEST_IS_REGULAR) == FALSE) {
		ERR_SECURE("file does not regular:%s\n", filename);
		return NULL;
	}

	ext = strrchr(filename, '.');
	if (ext == NULL) {
		ERR("file doesn't have extension\n");
		return NULL;
	}

	ext++;

	if (g_ascii_strcasecmp(ext, "gif") == 0)
		filetype = "GIF";
	else if (g_ascii_strcasecmp(ext, "cgm") == 0)
		filetype = "CGM";
	else if (g_ascii_strcasecmp(ext, "wmf") == 0)
		filetype = "WMF";
	else if (g_ascii_strcasecmp(ext, "bmp") == 0)
		filetype = "BMP";
	else if (g_ascii_strcasecmp(ext, "met") == 0)
		filetype = "MET";
	else if (g_ascii_strcasecmp(ext, "dib") == 0)
		filetype = "DIB";
	else if (g_ascii_strcasecmp(ext, "pict") == 0 ||
			g_ascii_strcasecmp(ext, "pct") == 0 ||
			g_ascii_strcasecmp(ext, "pic") == 0)
		filetype = "PICT";
	else if (g_ascii_strcasecmp(ext, "tiff") == 0 ||
			g_ascii_strcasecmp(ext, "tif") == 0)
		filetype = "TIFF";
	else if (g_ascii_strcasecmp(ext, "ps") == 0)
		filetype = "PS";
	else if (g_ascii_strcasecmp(ext, "pdf") == 0)
		filetype = "PDF";
	else if (g_ascii_strcasecmp(ext, "jpeg") == 0 ||
			g_ascii_strcasecmp(ext, "jpg") == 0 ||
			g_ascii_strcasecmp(ext, "jpe") == 0)
		filetype = "JPEG";
	else if (g_ascii_strcasecmp(ext, "mpeg") == 0 ||
			g_ascii_strcasecmp(ext, "mpg") == 0)
		filetype = "MPEG";
	else if (g_ascii_strcasecmp(ext, "m2v") == 0)
		filetype = "MPEG2";
	else if (g_ascii_strcasecmp(ext, "avi") == 0)
		filetype = "AVI";
	else if (g_ascii_strcasecmp(ext, "mov") == 0)
		filetype = "QTIME";
	else if (g_ascii_strcasecmp(ext, "png") == 0)
		filetype = "PNG";

	return g_strdup(filetype);
}

static gchar **__bluetooth_pb_contact_addr(contacts_record_h address)
{
	const gint ADDR_LEN = 7;

	gchar **strv = NULL;

	gint addr[] = { _contacts_address.postbox,
			_contacts_address.extended,
			_contacts_address.street,
			_contacts_address.locality,
			_contacts_address.region,
			_contacts_address.postal_code,
			_contacts_address.country };

	strv = __bluetooth_pb_contact_add_field_str(address,
			addr, ADDR_LEN);
	return strv;
}

static gchar **__bluetooth_pb_contact_addr_param(contacts_record_h address)
{

	contacts_address_type_e type;

	gint status;
	gint i = 0;

	gchar **strv = NULL;

	status = contacts_record_get_int(address,
			_contacts_address.type,
			(gint *)&type);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	strv = g_new0(gchar *, 7);	/* ADDR param max size is 6 */

	if (type & CONTACTS_ADDRESS_TYPE_HOME) {
		strv[i] = g_strdup("HOME");
		i++;
	}
	if (type & CONTACTS_ADDRESS_TYPE_WORK) {
		strv[i] = g_strdup("WORK");
		i++;
	}
	if (type & CONTACTS_ADDRESS_TYPE_DOM) {
		strv[i] = g_strdup("DOM");
		i++;
	}
	if (type & CONTACTS_ADDRESS_TYPE_INTL) {
		strv[i] = g_strdup("INTL");
		i++;
	}
	if (type & CONTACTS_ADDRESS_TYPE_POSTAL) {
		strv[i] = g_strdup("POSTAL");
		i++;
	}
	if (type & CONTACTS_ADDRESS_TYPE_PARCEL) {
		strv[i] = g_strdup("PARCEL");
		i++;
	}
	return strv;
}

static gchar *__bluetooth_pb_phonelog_datetime(gint phonelog_id)
{
	contacts_record_h phone_log;

	char time_str[32] = {0,};

	gint status;
	gint time = 0;

	struct tm time_info;

	status = contacts_db_get_record(_contacts_phone_log._uri,
			phonelog_id,
			&phone_log);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_record_get_int(phone_log,
			_contacts_phone_log.log_time,
			&time);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	if (time <= 0)
		return NULL;

	localtime_r((time_t *)&time, &time_info);

	strftime(time_str, sizeof(time_str),
			"%Y%m%dT%H%M%S", &time_info);

	contacts_record_destroy(phone_log, TRUE);

	return g_strdup(time_str);
}

static gchar *__bluetooth_pb_name_from_contact(contacts_record_h contact)
{
	contacts_record_h name = NULL;

	GString *str = g_string_new(NULL);

	gint status;
	gint i;

	gint name_size = 5;
	gint name_val[] = { _contacts_name.last,
			_contacts_name.first,
			_contacts_name.addition,
			_contacts_name.prefix,
			_contacts_name.suffix };


	status = contacts_record_get_child_record_at_p(contact,
			_contacts_contact.name, 0, &name);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	for (i = 0; i < name_size; i++) {
		gchar *tmp = NULL;
		gchar *escape = NULL;

		if (i > 0)
			g_string_append_c(str, ';');

		status = contacts_record_get_str_p(name, name_val[i], &tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		escape = __bluetooth_pb_vcard_escape(tmp);
		g_string_append(str, escape);

		g_free(escape);
	}

	return g_string_free(str, FALSE);
}

static gchar *__bluetooth_pb_phonetic_name_from_contact(contacts_record_h contact)
{
	contacts_record_h name = NULL;

	GString *str = g_string_new(NULL);

	gint status;

	gchar *phonetic_first = NULL;
	gchar *phonetic_last = NULL;
	gchar *escape = NULL;

	status = contacts_record_get_child_record_at_p(contact,
			_contacts_contact.name, 0, &name);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_record_get_str_p(name, _contacts_name.phonetic_first, &phonetic_first);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	if (phonetic_first == NULL)
		return NULL;

	status = contacts_record_get_str_p(name, _contacts_name.phonetic_last, &phonetic_last);

	if ((status == CONTACTS_ERROR_NONE) && (phonetic_last != NULL)) {
		escape = __bluetooth_pb_vcard_escape(phonetic_last);
		g_string_append(str, escape);
		g_free(escape);
		g_string_append_c(str, ' ');
	}

	escape = __bluetooth_pb_vcard_escape(phonetic_first);
	g_string_append(str, escape);

	g_free(escape);
	g_string_append_c(str, ';');
	g_string_append_c(str, ';');
	g_string_append_c(str, ';');
	g_string_append_c(str, ';');

	return g_string_free(str, FALSE);
}


static gchar *__bluetooth_pb_number_from_contact(contacts_record_h contact)
{
	guint count = 0;

	gint status;
	gint i;

	gchar *str = NULL;

	status = contacts_record_get_child_record_count(contact,
			_contacts_contact.number,
			&count);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	for (i = 0; i < count; i++) {
		contacts_record_h number = NULL;

		gchar *tmp = NULL;

		bool is_default = false;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.number, i, &number);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		status = contacts_record_get_bool(number,
				_contacts_number.is_default,
				&is_default);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		if (is_default == FALSE)
			continue;

		status = contacts_record_get_str_p(number,
				_contacts_number.number,
				&tmp);

		if (status != CONTACTS_ERROR_NONE)
			continue;

		if (tmp) {
			str = g_strdup(tmp);
			break;
		}
	}

	/* get first number */
	if (str == NULL) {
		gchar *tmp = NULL;

		contacts_record_h number = NULL;

		status = contacts_record_get_child_record_at_p(contact,
				_contacts_contact.number, 0, &number);

		if (status != CONTACTS_ERROR_NONE)
			return NULL;

		status = contacts_record_get_str_p(number,
				_contacts_number.number,
				&tmp);

		if (status != CONTACTS_ERROR_NONE)
			return NULL;

		str = g_strdup(tmp);
	}

	return str;
}

static gint __bluetooth_pb_person_id_from_phonelog_id(gint phonelog_id)
{
	contacts_query_h query = NULL;
	contacts_filter_h filter = NULL;
	contacts_list_h record_list = NULL;

	contacts_record_h phone_log = NULL;
	contacts_record_h record = NULL;

	gint status;
	gint person_id = 0;

	status = contacts_db_get_record(_contacts_phone_log._uri,
			phonelog_id,
			&phone_log);

	if (status != CONTACTS_ERROR_NONE)
		return 0;

	status = contacts_record_get_int(phone_log,
			_contacts_phone_log.person_id,
			&person_id);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(phone_log, TRUE);
		return 0;
	}

	contacts_record_destroy(phone_log, TRUE);

	if (person_id)
		return person_id;

	status = contacts_filter_create(_contacts_person_phone_log._uri,
			&filter);

	if (status != CONTACTS_ERROR_NONE)
		return 0;


	status = contacts_filter_add_int(filter,
			_contacts_person_phone_log.log_id,
			CONTACTS_MATCH_EQUAL,
			phonelog_id);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

	status = contacts_query_create(_contacts_person_phone_log._uri, &query);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

	status = contacts_query_set_filter(query, filter);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

	status = contacts_db_get_records_with_query(query, -1, -1,
								&record_list);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

	status = contacts_list_first(record_list);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

	status = contacts_list_get_current_record_p(record_list, &record);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

	status = contacts_record_get_int(record,
			_contacts_person_phone_log.person_id,
			&person_id);

	if (status != CONTACTS_ERROR_NONE)
		goto done;

done:
	if (record_list != NULL)
		contacts_list_destroy(record_list, TRUE);

	contacts_filter_destroy(filter);

	if (query != NULL)
		contacts_query_destroy(query);

	return person_id;
}

/* API for vcard */
gchar *_bluetooth_pb_vcard_contact(gint person_id,
				guint64 filter,
				guint8 format)
{
	gchar *str = NULL;

	if (person_id <= 0)
		return NULL;

	str = __bluetooth_pb_vcard_real_contact_with_properties(person_id, 0,
			filter, format,
			NULL);
	return str;
}

gchar *_bluetooth_pb_vcard_contact_owner(const gchar *number,
					guint64 filter,
					guint8 format)
{
	GString *str = g_string_new("BEGIN:VCARD\r\n");
	gchar *fn;
	gchar *name;

	fn = _bluetooth_pb_owner_name();
	name = g_strdup_printf("%s;;;;", fn);

	switch (format) {
	case VCARD_FORMAT_3_0:
		g_string_append(str, "VERSION:3.0\r\n");

		__bluetooth_pb_vcard_append_v30(str, "N", NULL, name);
		__bluetooth_pb_vcard_append_v30(str, "FN", NULL, fn);
		__bluetooth_pb_vcard_append_v30(str, "TEL", "TYPE=CELL",
									number);
		break;
	case VCARD_FORMAT_2_1:
	default:
		g_string_append(str, "VERSION:2.1\r\n");

		__bluetooth_pb_vcard_append_qp_encode_v21(str, "N", NULL, name);

		if (filter == 0 || (filter & VCARD_FN))
			__bluetooth_pb_vcard_append_qp_encode_v21(str, "FN",
								NULL, fn);

		__bluetooth_pb_vcard_append_qp_encode_v21(str, "TEL", "CELL",
									number);
		break;

	}

	g_string_append(str, "END:VCARD\r\n");

	g_free(fn);
	g_free(name);

	return g_string_free(str, FALSE);
}

gchar *_bluetooth_pb_vcard_call(gint phonelog_id,
				guint64 filter,
				guint8 format,
				const gchar *attr)
{
	gint person_id = 0;

	gchar *str = NULL;

	if (attr == NULL) {
		DBG("Unknown attribute type ignored\n");
		return NULL;
	}

	person_id = __bluetooth_pb_person_id_from_phonelog_id(phonelog_id);

	DBG("person_id %d\n", person_id);

	if (person_id) {
		if (filter == 0 || (filter & VCARD_X_IRMC_CALL_DATETIME)) {
			gchar *datetime = NULL;

			datetime = __bluetooth_pb_phonelog_datetime(
								phonelog_id);

			str = __bluetooth_pb_vcard_real_contact_with_properties(
					person_id,
					phonelog_id,
					filter, format,
					"X-IRMC-CALL-DATETIME", attr, datetime,
					NULL);

			if (datetime)
				g_free(datetime);
		} else {
			str = __bluetooth_pb_vcard_real_contact_with_properties(
					person_id,
					phonelog_id,
					filter, format,
					NULL);
		}
	} else
		str = __bluetooth_pb_vcard_real_call(phonelog_id, filter,
								format, attr);

	return str;
}

gchar *_bluetooth_pb_fn_from_person_id(gint person_id)
{
	contacts_record_h person = NULL;

	gint status;

	gchar *str = NULL;

	status = contacts_db_get_record(_contacts_person._uri,
			person_id,
			&person);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_record_get_str(person,
			_contacts_person.display_name,
			&str);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	contacts_record_destroy(person, TRUE);

	return str;
}

gchar *_bluetooth_pb_name_from_person_id(gint person_id)
{
	contacts_record_h person = NULL;
	contacts_record_h contact = NULL;

	gint status;
	gint contact_id = 0;

	gchar *str;

	status = contacts_db_get_record(_contacts_person._uri,
			person_id,
			&person);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	status = contacts_record_get_int(person,
			_contacts_person.display_contact_id,
			&contact_id);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(person, TRUE);
		return NULL;
	}

	status = contacts_db_get_record(_contacts_contact._uri,
			contact_id,
			&contact);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(person, TRUE);
		return NULL;
	}

	str = __bluetooth_pb_name_from_contact(contact);

	contacts_record_destroy(contact, TRUE);
	contacts_record_destroy(person, TRUE);

	return str;
}

gchar *_bluetooth_pb_number_from_person_id(gint person_id)
{
	contacts_record_h person = NULL;
	contacts_record_h contact = NULL;

	gint status;
	gint contact_id = 0;

	gchar *str;

	status = contacts_db_get_record(_contacts_person._uri,
			person_id,
			&person);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;


	status = contacts_record_get_int(person,
			_contacts_person.display_contact_id,
			&contact_id);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(person, TRUE);
		return NULL;
	}

	status = contacts_db_get_record(_contacts_contact._uri,
			contact_id,
			&contact);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(person, TRUE);
		return NULL;
	}

	str = __bluetooth_pb_number_from_contact(contact);

	contacts_record_destroy(contact, TRUE);
	contacts_record_destroy(person, TRUE);

	return str;
}

gchar *_bluetooth_pb_fn_from_phonelog_id(gint phonelog_id)
{
	gint person_id = 0;
	gchar *str = NULL;

	person_id = __bluetooth_pb_person_id_from_phonelog_id(phonelog_id);

	if (person_id > 0)
		str = _bluetooth_pb_fn_from_person_id(person_id);
	else
		str = g_strdup("");

	return str;
}

gchar *_bluetooth_pb_name_from_phonelog_id(gint phonelog_id)
{
	gint person_id = 0;
	gchar *str = NULL;

	person_id = __bluetooth_pb_person_id_from_phonelog_id(phonelog_id);

	if (person_id > 0)
		str = _bluetooth_pb_name_from_person_id(person_id);
	else {
		gchar *tmp;

		tmp = _bluetooth_pb_number_from_phonelog_id(phonelog_id);
		str = g_strdup_printf("%s;;;;", tmp);

		g_free(tmp);
	}

	return str;
}

gchar *_bluetooth_pb_number_from_phonelog_id(gint phonelog_id)
{
	contacts_record_h phone_log;

	gint status;

	gchar *str;
	gchar *tmp = NULL;

	status = contacts_db_get_record(_contacts_phone_log._uri,
			phonelog_id, &phone_log);

	if (status != CONTACTS_ERROR_NONE)
		return NULL;

	contacts_record_get_str_p(phone_log,
			_contacts_phone_log.address,
			&tmp);

	if (status != CONTACTS_ERROR_NONE) {
		contacts_record_destroy(phone_log, TRUE);
		return NULL;
	}

	str = g_strdup(tmp);

	contacts_record_destroy(phone_log, TRUE);

	return str;
}

gchar *_bluetooth_pb_owner_name(void)
{
	gchar *name;

	name = vconf_get_str(VCONFKEY_SETAPPL_DEVICE_NAME_STR);

	if (name == NULL)
		name = g_strdup("My Name");

	return name;
}
