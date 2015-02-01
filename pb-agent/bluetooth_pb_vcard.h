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

#ifndef __DEF_BT_PB_VCARD_H_
#define __DEF_BT_PB_VCARD_H_

#include <glib.h>

#define SIM_ADDRESSBOOK_PREFIX "http://tizen.samsung.com/addressbook/sim"

typedef enum {
	PBAP_ADDRESSBOOK_PHONE,
	PBAP_ADDRESSBOOK_SIM,
} bt_pbap_addressbook_e;

/* vcard */
gchar *_bluetooth_pb_vcard_contact(gint person_id,
				guint64 filter,
				guint8 format);

gchar *_bluetooth_pb_vcard_contact_owner(const gchar *number,
					guint64 filter,
					guint8 format);

gchar *_bluetooth_pb_vcard_call(gint phonelog_id,
				guint64 filter,
				guint8 format,
				const gchar *attr);

gchar *_bluetooth_pb_fn_from_person_id(gint person_id);

gchar *_bluetooth_pb_name_from_person_id(gint person_id);

gchar *_bluetooth_pb_number_from_person_id(gint person_id);

gchar *_bluetooth_pb_fn_from_phonelog_id(gint phonelog_id);

gchar *_bluetooth_pb_name_from_phonelog_id(gint phonelog_id);

gchar *_bluetooth_pb_number_from_phonelog_id(gint phonelog_id);

gchar *_bluetooth_pb_owner_name(void);

bool _bt_is_sim_addressbook(const char *addressbook);

#endif
