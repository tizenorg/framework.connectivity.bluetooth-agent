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
#include <string.h>
#include <glib.h>

#include <map_bmessage.h>

#include <bluetooth_map_agent.h>

#define CRLF_LEN 2

#define BMSG_TAG "BEGIN:BMSG\r\n"
#define VER_TAG "VERSION:"
#define STATUS_TAG "STATUS:"
#define TYPE_TAG "TYPE:"
#define FOLDER_TAG "FOLDER:"
#define VCARD_BEGIN_TAG "BEGIN:VCARD\r\n"
#define VCARD_END_TAG "END:VCARD\r\n"
#define VCARD_N_TAG "N:"
#define VCARD_FN_TAG "FN:"
#define VCARD_TEL_TAG "TEL:"
#define VCARD_EMAIL_TAG "EMAIL:"
#define BENV_TAG "BEGIN:BENV\r\n"
#define BBODY_TAG "BEGIN:BBODY\r\n"
#define MSG_TAG "BEGIN:MSG\r\n"
#define PARTID_TAG "PARTID:"
#define ENCODING_TAG "ENCODING:"
#define CHARSET_TAG "CHARSET:"
#define LANGUAGE_TAG "LANGUAGE:"
#define LENGTH_TAG "LENGTH:"


void print_bmsg(struct bmsg_data *bmsg)
{
	if (bmsg == NULL)
		return;

	struct benv_data *env_data = NULL;

	DBG("bmsg->version = %s", bmsg->version);
	DBG("bmsg->status = %s", bmsg->status);
	DBG("bmsg->type = %s", bmsg->type);
	DBG("bmsg->folder = %s", bmsg->folder);
	DBG("bmsg->originator_vcard_data->version = %s",
					bmsg->originator_vcard_data->version);
	DBG("bmsg->originator_vcard_data->n = %s",
					bmsg->originator_vcard_data->n);

	int i = 0;
	env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);
	while (env_data != NULL) {

		DBG("env_data = %d", env_data->encapsulation_level);
		int k = 0;
		struct bmsg_vcard *rvcard;

		rvcard = g_slist_nth_data(env_data->recipient_vcard, k);

		while (rvcard != NULL) {
			k++;

			if (rvcard->version != NULL)
				DBG("vcard->version = %s\n", rvcard->version);
			if (rvcard->n != NULL)
				DBG("vcard->n = %s\n", rvcard->n);
			if (rvcard->fn != NULL)
				DBG("vcard->fn = %s\n", rvcard->fn);
			if (rvcard->tel != NULL)
				DBG("vcard->tel = %s\n", rvcard->tel);
			if (rvcard->email != NULL)
				DBG("vcard->email = %s\n", rvcard->email);

			rvcard = g_slist_nth_data(env_data->recipient_vcard, k);
		}

		if (env_data->body_content != NULL) {
			DBG("env_data->body_content->length = %"
						G_GUINT64_FORMAT "\n",
						env_data->body_content->length);
			DBG("env_data->body_content->msg = %s\n",
						env_data->body_content->msg);
		}

		i++;

		if (i > 2)
			break;

		env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);
	}
}

char *bmsg_get_msg_folder(struct bmsg_data *bmsg)
{
	return g_strdup(bmsg->folder);
}

char *bmsg_get_msg_body(struct bmsg_data *bmsg)
{
	struct benv_data *env_data;
	int i = 0;

	env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);

	while (env_data != NULL) {
		if (env_data->body_content != NULL) {
			DBG("env_data->body_content->msg = %s\n",
						env_data->body_content->msg);
			DBG("env_data->body_content->length = %"
						G_GUINT64_FORMAT "\n",
						env_data->body_content->length);
			return g_strndup(env_data->body_content->msg,
								env_data->body_content->length);
		}

		i++;
		if (i > 2)
			break;

		env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);
	}

	return NULL;
}

GSList *bmsg_get_msg_recepients(struct bmsg_data *bmsg)
{
	struct benv_data *env_data;
	GSList *receiver = NULL;
	int i = 0;

	env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);

	while (env_data != NULL) {

		DBG("env_data = %d", env_data->encapsulation_level);
		int k = 0;
		struct bmsg_vcard *rvcard;

		rvcard = g_slist_nth_data(env_data->recipient_vcard, k);
		while (rvcard != NULL) {
			k++;

			if (rvcard->tel != NULL) {
				DBG("vcard->tel = %s\n", rvcard->tel);
				receiver = g_slist_append(receiver, rvcard->tel);
			}

			rvcard = g_slist_nth_data(env_data->recipient_vcard, k);
		}

		i++;
		if (i > 2)
			break;

		env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);
	}

	return receiver;
}

void bmsg_free_vcard_data(struct bmsg_vcard *vcard_data)
{
	if (vcard_data == NULL)
		return;

	g_free(vcard_data->version);
	g_free(vcard_data->n);
	g_free(vcard_data->fn);
	g_free(vcard_data->tel);
	g_free(vcard_data->email);
	g_free(vcard_data);

	return;
}

void bmsg_free_bmsg(struct bmsg_data *bmsg)
{
	struct benv_data *env_data;
	int i = 0;

	if (bmsg == NULL)
		return;

	g_free(bmsg->version);
	g_free(bmsg->status);
	g_free(bmsg->type);
	g_free(bmsg->folder);
	bmsg_free_vcard_data(bmsg->originator_vcard_data);

	if (bmsg->envelope_data == NULL)
		goto done;

	if (bmsg->envelope_data->env_data == NULL)
		goto done;

	env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);
	while (env_data != NULL) {

		DBG("env_data = %d", env_data->encapsulation_level);
		int k = 0;
		struct bmsg_vcard *rvcard;

		rvcard = g_slist_nth_data(env_data->recipient_vcard, k);

		while (rvcard != NULL) {
			k++;
			bmsg_free_vcard_data(rvcard);
			rvcard = g_slist_nth_data(env_data->recipient_vcard, k);
		}

		if (env_data->body_content != NULL) {
			g_free(env_data->body_content->encoding);
			g_free(env_data->body_content->charset);
			g_free(env_data->body_content->language);
			g_free(env_data->body_content->msg);
			g_free(env_data->body_content);
		}

		g_free(env_data);
		i++;

		env_data = g_slist_nth_data(bmsg->envelope_data->env_data, i);
	}

done:
	g_free(bmsg);
}

gchar *bmsg_get_parse_sub_block(char **sub_block_data, char *element)
{
	gchar *start;
	gchar *end;
	gchar *block_start;
	gchar *block_end;
	gchar *sub_block = NULL;
	size_t offset;
	size_t len;

	DBG("");

	start = g_strdup_printf("BEGIN:%s\r\n", element);
	end = g_strdup_printf("END:%s\r\n", element);
	offset = strlen(start);

	block_start = g_strstr_len(*sub_block_data, offset, start);
	if (block_start == NULL)
		goto done;

	if (!g_strcmp0(start, VCARD_BEGIN_TAG))
		block_end = g_strstr_len(*sub_block_data, -1,  end);
	else
		block_end = g_strrstr(*sub_block_data, end);

	if (block_end == NULL)
		goto done;

	len = block_end - block_start - offset;
	sub_block = g_strndup(block_start + offset, len);
	*sub_block_data = *sub_block_data + strlen(sub_block) + strlen(start) +
								 strlen(end);
done:
	g_free(start);
	g_free(end);
	return sub_block;
}

gchar *bmsg_get_tag_data(char **block_data, char *element)
{
	gchar *end = "\r\n";
	gchar *block_start;
	gchar *block_end;
	gchar *sub_block;
	size_t offset;
	size_t len;

	DBG("");

	if (*block_data == NULL || element == NULL)
		return NULL;

	block_start = g_strstr_len(*block_data, -1, element);
	if (block_start == NULL)
		return NULL;

	offset = strlen(element);

	block_end = g_strstr_len(block_start+offset, -1, end);
	if (block_end == NULL)
		return NULL;

	len = block_end - block_start - offset;
	sub_block = g_strndup(block_start + offset, len);
	*block_data = *block_data + offset + len + CRLF_LEN;

	return sub_block;
}

struct bmsg_bbody *bmsg_get_bbody_data(gchar *block_data)
{
	gchar *bbody_block_data_start;
	gchar *temp;
	struct bmsg_bbody *bbody;
	DBG("");

	bbody_block_data_start = block_data;

	bbody = g_new0(struct bmsg_bbody, 1);

	temp = bmsg_get_tag_data(&block_data, PARTID_TAG);
	if (temp != NULL) {
		bbody->part_id = (guint16)g_ascii_strtoull(temp, NULL, 10);
		g_free(temp);
	}

	bbody->encoding = bmsg_get_tag_data(&block_data, ENCODING_TAG);
	bbody->charset = bmsg_get_tag_data(&block_data, CHARSET_TAG);
	bbody->language = bmsg_get_tag_data(&block_data, LANGUAGE_TAG);

	temp = bmsg_get_tag_data(&block_data, LENGTH_TAG);

	if (temp != NULL) {
		bbody->length = g_ascii_strtoull(temp, NULL, 10);
		g_free(temp);
	}

	bbody->msg = bmsg_get_parse_sub_block(&block_data, "MSG");

	g_free(bbody_block_data_start);

	return bbody;
}

struct bmsg_vcard *bmsg_get_vcard_data(gchar *sub_block_data)
{
	gchar *vcard_block_data_start;
	struct bmsg_vcard *vcard;
	DBG("");

	vcard_block_data_start = sub_block_data;

	vcard = g_new0(struct bmsg_vcard, 1);

	vcard->version = bmsg_get_tag_data(&sub_block_data, VER_TAG);
	vcard->n = bmsg_get_tag_data(&sub_block_data, VCARD_N_TAG);
	vcard->fn = bmsg_get_tag_data(&sub_block_data, VCARD_FN_TAG);
	vcard->tel = bmsg_get_tag_data(&sub_block_data, VCARD_TEL_TAG);
	vcard->email = bmsg_get_tag_data(&sub_block_data, VCARD_EMAIL_TAG);

	g_free(vcard_block_data_start);

	return vcard;
}

struct benv_data *bmsg_get_env_encapsulation_data(gchar **sub_block_data)
{
	gchar *is_valid;
	gchar *bbody_data = NULL;
	static guint8 lvl = 1;

	is_valid = g_strstr_len(*sub_block_data, strlen(VCARD_BEGIN_TAG),
							VCARD_BEGIN_TAG);
	if (is_valid == NULL)
		return NULL;

	if (lvl > 3)
		return NULL;

	struct benv_data *rec_data = g_new0(struct benv_data, 1);

	rec_data->encapsulation_level = lvl;
	lvl++;

	while (is_valid != NULL) {
		gchar *vcard_data = NULL;
		struct bmsg_vcard *vcard;

		vcard_data = bmsg_get_parse_sub_block(sub_block_data, "VCARD");
		if (vcard_data == NULL) {
			DBG("parse error\n");
			g_free(rec_data);
			return NULL;
		}
		vcard = bmsg_get_vcard_data(vcard_data);

		rec_data->recipient_vcard = g_slist_append(
						rec_data->recipient_vcard,
						vcard);

		is_valid = g_strstr_len(*sub_block_data,
						strlen(VCARD_BEGIN_TAG),
						VCARD_BEGIN_TAG);
	}

	is_valid = g_strstr_len(*sub_block_data, strlen(BBODY_TAG), BBODY_TAG);

	if (!is_valid)
		return rec_data;

	bbody_data = bmsg_get_parse_sub_block(sub_block_data, "BBODY");
	if (bbody_data == NULL) {
		DBG("parse error\n");
		return rec_data;
	}

	rec_data->body_content = bmsg_get_bbody_data(bbody_data);

	return rec_data;
}

struct bmsg_envelope *bmsg_get_envelope_data(gchar **block_data)
{
	gchar *env_block_data_start;
	gchar *sub_block_data;
	struct bmsg_envelope *envelope_data;
	struct benv_data *rec_data;

	env_block_data_start = *block_data;

	envelope_data = g_new0(struct bmsg_envelope, 1);

	sub_block_data = bmsg_get_parse_sub_block(block_data, "BENV");

	while (sub_block_data) {

		rec_data = bmsg_get_env_encapsulation_data(&sub_block_data);

		while (rec_data) {
			envelope_data->env_data = g_slist_append(
							envelope_data->env_data,
							rec_data);

			rec_data = bmsg_get_env_encapsulation_data(
							&sub_block_data);
		}
		sub_block_data = bmsg_get_parse_sub_block(&sub_block_data,
									"BENV");
	}
	g_free(sub_block_data);

	return envelope_data;
}

struct bmsg_data * bmsg_parse(gchar *buf)
{
	gchar *block_data;
	gchar *sub_block_data;
	gchar *block_data_start;
	struct bmsg_data *bmsg;

	DBG("");

	block_data = bmsg_get_parse_sub_block(&buf, "BMSG");
	if (block_data == NULL)
		return NULL;

	block_data_start = block_data;

	bmsg = g_new0(struct bmsg_data, 1);

	bmsg->version = bmsg_get_tag_data(&block_data, VER_TAG);
	if (bmsg->version == NULL)
		goto parse_fail;

	bmsg->status = bmsg_get_tag_data(&block_data, STATUS_TAG);
	if (bmsg->status == NULL)
		goto parse_fail;

	bmsg->type = bmsg_get_tag_data(&block_data, TYPE_TAG);
	if (bmsg->type == NULL)
		goto parse_fail;

	bmsg->folder = bmsg_get_tag_data(&block_data, FOLDER_TAG);
	if (bmsg->folder == NULL)
		goto parse_fail;

	sub_block_data = bmsg_get_parse_sub_block(&block_data, "VCARD");
	if (sub_block_data == NULL)
		goto parse_fail;

	bmsg->originator_vcard_data = bmsg_get_vcard_data(sub_block_data);
	if (bmsg->originator_vcard_data == NULL)
		goto parse_fail;

	bmsg->envelope_data = bmsg_get_envelope_data(&block_data);
	if (bmsg->envelope_data == NULL)
		goto parse_fail;

	g_free(block_data_start);

	DBG("Parse done");
	print_bmsg(bmsg);

	return bmsg;

parse_fail:
	DBG("Parse fail");
	bmsg_free_bmsg(bmsg);

	return NULL;
}

