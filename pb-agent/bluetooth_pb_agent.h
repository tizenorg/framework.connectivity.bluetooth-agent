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

#ifndef __DEF_BT_PB_AGENT_H_
#define __DEF_BT_PB_AGENT_H_

#include <unistd.h>
#include <dlog.h>

#include <stdio.h>

#include <dbus/dbus-glib.h>

#define BT_PB_SERVICE_OBJECT_PATH	"/org/bluez/pb_agent"
#define BT_PB_SERVICE_NAME		"org.bluez.pb_agent"
#define BT_PB_SERVICE_INTERFACE		"org.bluez.PbAgent"

#undef LOG_TAG
#define LOG_TAG "BLUETOOTH_AGENT_PB"
#define INFO(fmt, args...) SLOGI(fmt, ##args)
#define DBG(fmt, args...) SLOGD(fmt, ##args)
#define ERR(fmt, args...) SLOGE(fmt, ##args)

#define DBG_SECURE(fmt, args...) SECURE_SLOGD(fmt, ##args)
#define ERR_SECURE(fmt, args...) SECURE_SLOGE(fmt, ##args)

#define FUCNTION_CALLS
#ifdef FUCNTION_CALLS
#define FN_START	DBG("ENTER==>")
#define FN_END		DBG("EXIT===>")
#else
#define FN_START
#define FN_END
#endif

static gchar *bluetooth_pb_agent_folder_list[] = {
	"/telecom/pb",
	"/telecom/ich",
	"/telecom/och",
	"/telecom/mch",
	"/telecom/cch",
#ifdef PBAP_SIM_ENABLE
	"/SIM1/telecom/pb",
#endif
	NULL
};

typedef enum {
	TELECOM_PB = 0,
	TELECOM_ICH,
	TELECOM_OCH,
	TELECOM_MCH,
	TELECOM_CCH,
#ifdef PBAP_SIM_ENABLE
	SIM_PB,
#endif
	TELECOM_NONE
} PhoneBookType;

#endif				/* __DEF_BT_AGENT_H_ */
