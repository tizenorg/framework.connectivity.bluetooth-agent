/*
 * Bluetooth-hf-agent
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

#ifndef __DEF_BT_HF_AGENT_H_
#define __DEF_BT_HF_AGENT_H_

#undef LOG_TAG
#define LOG_TAG "BLUETOOTH_HF_AGENT"

#define LOG_COLOR_RESET    "\033[0m"
#define LOG_COLOR_RED      "\033[31m"
#define LOG_COLOR_YELLOW   "\033[33m"
#define LOG_COLOR_GREEN         "\033[32m"
#define LOG_COLOR_BLUE          "\033[36m"
#define LOG_COLOR_PURPLE   "\033[35m"

#define DBG(fmt, args...) SLOGD(fmt, ##args)
#define INFO(fmt, args...) SLOGI(fmt, ##args)
#define ERR(fmt, args...) SLOGE(fmt, ##args)
#define DBG_SECURE(fmt, args...) SECURE_SLOGD(fmt, ##args)
#define INFO_SECURE(fmt, args...) SECURE_SLOGI(fmt, ##args)
#define ERR_SECURE(fmt, args...) SECURE_SLOGE(fmt, ##args)
#define DBG_SECURE(fmt, args...) SECURE_SLOGD(fmt, ##args)
#define INFO_C(fmt, arg...) \
	SLOGI_IF(TRUE,  LOG_COLOR_BLUE" "fmt" "LOG_COLOR_RESET, ##arg)
#define ERR_C(fmt, arg...) \
	SLOGI_IF(TRUE,  LOG_COLOR_RED" "fmt" "LOG_COLOR_RESET, ##arg)


#include <unistd.h>
#include <dlog.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/socket.h>

#define BTPROTO_SCO	2

#define BT_HF_DATA_BUF_SIZE 1024
#define BT_HF_CMD_BUF_SIZE 32
#define BT_HF_INDICATOR_DESCR_SIZE 20
#define BT_HF_CALLER_NUM_SIZE 64	/* size of number + type */
#define BT_HF_FMT_STR_SIZE 32

#define BT_HF_AGENT_ERROR (__bt_hf_agent_error_quark())

#define BT_ERROR_INTERNAL "InternalError"
#define BT_ERROR_NOT_AVAILABLE "NotAvailable"
#define BT_ERROR_NOT_CONNECTED "NotConnected"
#define BT_ERROR_NOT_CONNECTION_FAILED "ConnectionFailed"
#define BT_ERROR_BUSY "InProgress"
#define BT_ERROR_INVALID_PARAM "InvalidArguments"
#define BT_ERROR_ALREADY_EXIST "AlreadyExists"
#define BT_ERROR_ALREADY_CONNECTED "Already Connected"
#define BT_ERROR_NO_MEMORY "No memory"
#define BT_ERROR_I_O_ERROR "I/O error"
#define BT_ERROR_OPERATION_NOT_AVAILABLE "Operation currently not available"
#define BT_ERROR_OPERATION_NOT_ALLOWED "Operation not allowed"
#define BT_ERROR_OPERATION_NOT_SUPPORTED "Operation not supported"
#define BT_ERROR_INVALID_FILE_DESCRIPTOR "Invalid File Descriptor"

typedef enum {
	BT_HF_AGENT_ERROR_NONE,
	BT_HF_AGENT_ERROR_INTERNAL,
	BT_HF_AGENT_ERROR_NOT_AVAILABLE,
	BT_HF_AGENT_ERROR_NOT_CONNECTED,
	BT_HF_AGENT_ERROR_CONNECTION_FAILED,
	BT_HF_AGENT_ERROR_BUSY,
	BT_HF_AGENT_ERROR_INVALID_PARAM,
	BT_HF_AGENT_ERROR_ALREADY_EXIST,
	BT_HF_AGENT_ERROR_ALREADY_CONNECTED,
	BT_HF_AGENT_ERROR_NO_MEMORY,
	BT_HF_AGENT_ERROR_I_O_ERROR,
	BT_HF_AGENT_ERROR_APPLICATION,
	BT_HF_AGENT_ERROR_NOT_ALLOWED,
	BT_HF_AGENT_ERROR_NOT_SUPPORTED,
	BT_HF_AGENT_ERROR_INVALID_FILE_DESCRIPTOR,
} bt_hf_agent_error_t;

/* Extended Audio Gateway Error Result Codes */
typedef enum {
	BT_AG_CME_ERROR_NONE			= -1,
	BT_AG_CME_ERROR_AG_FAILURE		= 0,
	BT_AG_CME_ERROR_NO_PHONE_CONNECTION	= 1,
	BT_AG_CME_ERROR_NOT_ALLOWED		= 3,
	BT_AG_CME_ERROR_NOT_SUPPORTED		= 4,
	BT_AG_CME_ERROR_PH_SIM_PIN_REQUIRED	= 5,
	BT_AG_CME_ERROR_SIM_NOT_INSERTED	= 10,
	BT_AG_CME_ERROR_SIM_PIN_REQUIRED	= 11,
	BT_AG_CME_ERROR_SIM_PUK_REQUIRED	= 12,
	BT_AG_CME_ERROR_SIM_FAILURE		= 13,
	BT_AG_CME_ERROR_SIM_BUSY		= 14,
	BT_AG_CME_ERROR_INCORRECT_PASSWORD	= 16,
	BT_AG_CME_ERROR_SIM_PIN2_REQUIRED	= 17,
	BT_AG_CME_ERROR_SIM_PUK2_REQUIRED	= 18,
	BT_AG_CME_ERROR_MEMORY_FULL		= 20,
	BT_AG_CME_ERROR_INVALID_INDEX		= 21,
	BT_AG_CME_ERROR_MEMORY_FAILURE		= 23,
	BT_AG_CME_ERROR_TEXT_STRING_TOO_LONG	= 24,
	BT_AG_CME_ERROR_INVALID_TEXT_STRING	= 25,
	BT_AG_CME_ERROR_DIAL_STRING_TOO_LONG	= 26,
	BT_AG_CME_ERROR_INVALID_DIAL_STRING	= 27,
	BT_AG_CME_ERROR_NO_NETWORK_SERVICE	= 30,
	BT_AG_CME_ERROR_NETWORK_TIMEOUT		= 31,
	BT_AG_CME_ERROR_NETWORK_NOT_ALLOWED	= 32,
} bt_ag_cme_error_t;

typedef enum {
	BT_HF_CALL_DIR_OUTGOING,
	BT_HF_CALL_DIR_INCOMING,
} bt_hf_call_direction_t;

/* Call status as per spec */
typedef enum {
	BT_HF_CALL_STAT_ACTIVE,
	BT_HF_CALL_STAT_HELD,
	BT_HF_CALL_STAT_DIALING,
	BT_HF_CALL_STAT_ALERTING,
	BT_HF_CALL_STAT_INCOMING,
	BT_HF_CALL_STAT_WAITING,
} bt_hf_call_status_t;

enum hfp_version {
	HFP_VERSION_1_5 = 0x0105,
	HFP_VERSION_1_6 = 0x0106,
	HFP_VERSION_LATEST = HFP_VERSION_1_6,
};

/*Handsfree supported features*/
#define BT_HF_FEATURE_EC_ANDOR_NR			0x0001
#define BT_HF_FEATURE_CALL_WAITING_AND_3WAY	0x0002
#define BT_HF_FEATURE_CLI_PRESENTATION		0x0004
#define BT_HF_FEATURE_VOICE_RECOGNITION		0x0008
#define BT_HF_FEATURE_REMOTE_VOLUME_CONTROL	0x0010
#define BT_HF_FEATURE_ENHANCED_CALL_STATUS	0x0020
#define BT_HF_FEATURE_ENHANCED_CALL_CONTROL	0x0040
#define BT_HF_FEATURE_CODEC_NEGOTIATION		0x0080

/*AG suported feature*/
#define BT_AG_FEATURE_3WAY 0x1
#define BT_AG_FEATURE_NREC	0x0002
#define BT_AG_FEATURE_EXTENDED_RES_CODE 0x100
#define BT_AG_FEATURE_CODEC_NEGOTIATION	0x0200

#define BT_HF_CODEC_ID_CVSD 1
#define BT_HF_CODEC_ID_MSBC 2

#define BT_HF_AUDIO_DISCONNECTED 0
#define BT_HF_AUDIO_CONNECTED 1

#define BT_MAX_TEL_NUM_STR 100

#define BT_HF_FEATURES "AT+BRSF=%d\r"     /* = 0x7F = All features supported */
#define BT_HF_INDICATORS_SUPP "AT+CIND=?\r"
#define BT_HF_INDICATORS_VAL "AT+CIND?\r"
#define BT_HF_INDICATORS_ENABLE "AT+CMER=3,0,0,1\r"
#define BT_HF_HOLD_MPTY_SUPP "AT+CHLD=?\r"
#define BT_HF_CALLER_IDENT_ENABLE "AT+CLIP=1\r"
#define BT_HF_CARRIER_FORMAT "AT+COPS=3,0\r"
#define BT_HF_EXTENDED_RESULT_CODE "AT+CMEE=1\r"
#define BT_HF_INDICATORS_ACTIVATION "AT+BIA="
#define BT_HF_ANSWER_CALL "ATA\r"
#define BT_HF_END_CALL "AT+CHUP\r"
#define BT_HF_REDIAL "AT+BLDN\r"
#define BT_HF_DIAL_NO "ATD%.100s;\r"
#define BT_HF_VOICE_RECOGNITION "AT+BVRA=%d\r"
#define BT_HF_XSAT "AT+XSAT=00,TY,WA\r"
#define BT_HF_BSSF "AT+BSSF=8\r"
#define BT_HF_CALLLIST "AT+CLCC\r"
#define BT_HF_AVAILABLE_CODEC "AT+BAC=%d,%d\r"
#define BT_HF_CODEC_SELECT "AT+BCS=%d\r"
#define BT_HF_SPEAKER_GAIN "AT+VGS=%d\r"
#define BT_HF_DTMF "AT+VTS=%s\r"
#define BT_HF_NREC "AT+NREC=0\r"
#define BT_HF_CALLWAIT_NOTI_ENABLE "AT+CCWA=1\r"
#define BT_HF_RELEASE_ALL "AT+CHLD=0\r"
#define BT_HF_RELEASE_AND_ACCEPT "AT+CHLD=1\r"
#define BT_HF_ACCEPT_AND_HOLD "AT+CHLD=2\r"
#define BT_HF_JOIN_CALL "AT+CHLD=3\r"

#define BT_MAX_EVENT_STR_LENGTH	 50
#define BT_AGENT_SYSPOPUP_TIMEOUT_FOR_MULTIPLE_POPUPS 200

#define BT_HF_MAX_SPEAKER_GAIN 15
#define BT_HF_MIN_SPEAKER_GAIN 0


typedef enum {
	BT_AGENT_EVENT_HANDSFREE_CONNECT = 0x1100,
	BT_AGENT_EVENT_HANDSFREE_DISCONNECT = 0x1200,
} bt_hfp_agent_event_type_t;

/* Hold and multipary AG features.
 * Comments below are copied from hands-free spec for reference */
/* Releases all held calls or sets User Determined User Busy (UDUB)
 * for a waiting call */
#define BT_HF_CHLD_0 0x01
/* Releases all active calls (if any exist) and accepts the other
 * (held or waiting) call */
#define BT_HF_CHLD_1 0x02
/* Releases specified active call only <x> */
#define BT_HF_CHLD_1x 0x04
/* Places all active calls (if any exist) on hold and accepts the other
 * (held or waiting) call */
#define BT_HF_CHLD_2 0x08
/* Request private consultation mode with specified call <x> (Place all
 * calls on hold EXCEPT the call <x>) */
#define BT_HF_CHLD_2x 0x10
/* Adds a held call to the conversation */
#define BT_HF_CHLD_3 0x20
/* Connects the two calls and disconnects the subscriber from both calls
 * (Explicit Call Transfer). Support for this value and its associated
 * functionality is optional for the HF. */
#define BT_HF_CHLD_4 0x40

#define BT_HF_OK_RESPONSE "\r\nOK\r\n"
#define BT_HF_ERROR_RESPONSE "ERROR"
#define BT_HF_SEC_ERROR_RESPONSE "SERR"

#define BT_HF_SERVICE_NAME "org.bluez.hf_agent"
#define BT_HF_AGENT_OBJECT_PATH "/org/bluez/handsfree_agent"
#define BT_HF_SERVICE_INTERFACE "org.tizen.HfApp"

#define BT_HF_BLUEZ_OBJECT_PATH "/org/tizen/handsfree"
#define BT_HF_BLUEZ_INTERFACE	"org.bluez.HandsfreeAgent"

#define BLUEZ_SERVICE_NAME "org.bluez"
#define BLUEZ_HF_INTERFACE_NAME "org.bluez.HandsfreeGateway"

#define PM_SERVICE_NAME "org.tizen.system.deviced"
#define PM_OBJECT_PATH "/Org/Tizen/System/DeviceD/Display"
#define PM_INTERFACE_NAME "org.tizen.system.deviced.display"
#define AT_CMD_BUFF_SIZE 500
#define BLUEZ_PROFILE_MGMT_INTERFACE "org.bluez.ProfileManager1"
#define BT_MANAGER_INTERFACE "org.freedesktop.DBus.ObjectManager"
#define BT_ADAPTER_INTERFACE	"org.bluez.Adapter1"

#define retv_if(expr, val) \
	do { \
		if (expr) { \
			ERR("(%s) return", #expr); \
			return (val); \
		} \
	} while (0)

typedef enum {
	BT_HF_STATE_DISCONNECTED,
	BT_HF_STATE_CONNECTED
} bt_hf_state_t;

typedef struct {
	guint32 fd;
	gint sco_fd;
	gint cli_sco_fd;

	GIOChannel *io_chan;
	GIOChannel *sco_io_chan;
	bt_hf_state_t state;

	guint watch_id;
	guint sco_watch_id;
	guint cli_sco_watch_id;

	guint ag_features;
	guint hold_multiparty_features;
	GSList *indies;

	gboolean is_dialing;
	gboolean call_active;

	guint ciev_call_status;
	guint ciev_call_setup_status;

	guint32 feature;

	char *remote_addr;
	int slc;
	GSList *cmd_list;
	GSList *cmd_send_queue;

	GDBusMethodInvocation *context;
	char *path;
}bt_hf_agent_info_t;

typedef struct {
	int id;
	char at_cmd[AT_CMD_BUFF_SIZE];
	int count;
	GDBusMethodInvocation *context;
	int pending;
	int timer_id;
} bt_hf_agent_send_at_info;

struct hf_event {
	const char *cmd;
	int (*callback) (bt_hf_agent_info_t *bt_hf_info, const char *buf);
};

typedef struct {
	unsigned char b[6];
} __attribute__((packed)) bdaddr_t;

/* Remote socket address */
struct sockaddr_remote {
	sa_family_t	family;
	bdaddr_t	remote_bdaddr;
	uint8_t		channel;
};

/* SCO socket address */
struct sockaddr_sco {
	sa_family_t	sco_family;
	bdaddr_t	sco_bdaddr;
};

#endif /* __DEF_BT_HF_AGENT_H_ */
