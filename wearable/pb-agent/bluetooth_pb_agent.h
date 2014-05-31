/*
 * Bluetooth-frwk
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
#define LOG_TAG "BT_PB_AGENT"
#define DBG(fmt, args...) SLOGD(fmt, ##args)
#define ERR(fmt, args...) SLOGE(fmt, ##args)

#ifdef _SECURE_LOG
#define DBG_SECURE(fmt, args...) SECURE_SLOGD(fmt, ##args)
#define ERR_SECURE(fmt, args...) SECURE_SLOGE(fmt, ##args)
#else /* _SECURE_LOG */
#define DBG_SECURE(...) (0)
#define ERR_SECURE(...) (0)
#endif /* _SECURE_LOG */

#endif				/* __DEF_BT_AGENT_H_ */
