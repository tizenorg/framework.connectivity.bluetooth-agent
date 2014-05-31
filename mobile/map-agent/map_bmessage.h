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

 #ifndef __BMSG_H
#define __BMSG_H

#ifdef __cplusplus
extern "C" {
#endif

struct bmsg_vcard {
	gchar *version;
	gchar *n;
	gchar *fn;
	gchar *tel;
	gchar *email;
};

struct bmsg_bbody {
	guint16 part_id;
	gchar *encoding;
	gchar *charset;
	gchar *language;
	guint64 length;
	gchar *msg;
};

struct benv_data {
	guint8 encapsulation_level;
	GSList *recipient_vcard;
	struct bmsg_bbody *body_content;
};

struct bmsg_envelope {
	GSList *env_data;	/* Add benv_data here*/
};

struct bmsg_data {
	gchar *version;
	gchar *status;
	gchar *type;
	gchar *folder;
	struct bmsg_vcard *originator_vcard_data;
	struct bmsg_envelope *envelope_data;
};

struct bmsg_data * bmsg_parse(gchar *buf);
char *bmsg_get_msg_folder(struct bmsg_data *bmsg);
char *bmsg_get_msg_body(struct bmsg_data *bmsg);
GSList *bmsg_get_msg_recepients(struct bmsg_data *bmsg);

#ifdef __cplusplus
}
#endif

#endif /* __BMSG_H */
