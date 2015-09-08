/*
 * Bluetooth-ag-agent.h
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:	Hocheol Seo <hocheol.seo@samsung.com>
 *		Chethan TN <chethan.tn@samsung.com>
 *		Chanyeol Park <chanyeol.park@samsung.com>
 *		Rakesh MK <rakesh.mk@samsung.com>
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

#ifndef __DEF_BT_AG_AGENT_H_
#define __DEF_BT_AG_AGENT_H_

#undef LOG_TAG
#define LOG_TAG "BLUETOOTH_AG_AGENT"

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
#define ERR_SECURE(fmt, args...) SECURE_SLOGE(fmt, ##args)
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
#include <glib.h>
#include <gio/gio.h>
#include <errno.h>

#include "vconf.h"
#include "vconf-keys.h"

#define BT_AG_SERVICE_NAME "org.bluez.ag_agent"
#define BT_AG_AGENT_OBJECT_PATH "/org/bluez/hfp_agent"
#define BT_HS_AG_AGENT_OBJECT_PATH "/org/bluez/hsp_agent"
#define BLUEZ_AG_INTERFACE_NAME "Hands-Free Audio Gateway"
#define BLUEZ_SERVICE_NAME "org.bluez"
#define BLUEZ_PROFILE_MGMT_INTERFACE "org.bluez.ProfileManager1"
#define BT_MANAGER_INTERFACE "org.freedesktop.DBus.ObjectManager"
#define HFP_APP_INTERFACE "Org.Hfp.App.Interface"
#define TELEPHONY_APP_INTERFACE "org.tizen.csd.Call.Instance"
#define BT_HEADSET_INTERFACE "org.bluez.Headset"
#define BT_ADAPTER_INTERFACE	"org.bluez.Adapter1"
#ifdef TIZEN_MEDIA_ENHANCE
#define BT_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define BLUEZ_MEDIA_TRANSPORT_INTERFACE "org.bluez.MediaTransport1"
#define BLUEZ_MEDIA_ENDPOINT_INTERFACE "org.bluez.MediaEndpoint1"
#define BLUEZ_DEVICE_INTERFACE	"org.bluez.Device1"
#define A2DP_SOURCE_ENDPOINT "/MediaEndpoint/A2DPSource"
#endif

#define BT_ADAPTER_OBJECT_PATH_MAX 50

#define BT_ADDRESS_STRING_SIZE 18
#define MAX_BUFFER_SIZE 1024

/* Response and hold values */
#define BT_RSP_HOLD_NOT_SUPPORTED	-2
#define HANDSFREE_FEATURE_CALL_WAITING_AND_3WAY 0x0002

/* HFP Agent Indicator event values */
#define INDICATOR_EVENT_SERVICE_NONE			0
#define INDICATOR_EVENT_SERVICE_PRESENT		1

#define INDICATOR_EVENT_CALL_INACTIVE			0
#define INDICATOR_EVENT_CALL_ACTIVE			1

#define INDICATOR_EVENT_CALLSETUP_INACTIVE		 0
#define INDICATOR_EVENT_CALLSETUP_INCOMING		 1
#define INDICATOR_EVENT_CALLSETUP_OUTGOING		 2
#define INDICATOR_EVENT_CALLSETUP_ALERTING		 3

#define INDICATOR_EVENT_CALLHELD_NONE			0
#define INDICATOR_EVENT_CALLHELD_MULTIPLE		1
#define INDICATOR_EVENT_CALLHELD_ON_HOLD		2

#define INDICATOR_EVENT_ROAM_INACTIVE			0
#define INDICATOR_EVENT_ROAM_ACTIVE			1

/* Telephony number types */
#define AGENT_NUMBER_TYPE_TELEPHONY		129
#define AGENT_NUMBER_TYPE_INTERNATIONAL	145

/* Call direction parameters */
#define AGENT_CALL_DIRECTION_OUTGOING	0
#define AGENT_CALL_DIRECTION_INCOMING		1

#define AGENT_CALL_STATUS_ACTIVE		0
#define AGENT_CALL_STATUS_HELD		1
#define AGENT_CALL_STATUS_DIALING		2
#define AGENT_CALL_STATUS_ALERTING	3
#define AGENT_CALL_STATUS_INCOMING	4
#define AGENT_CALL_STATUS_WAITING		5

#define AGENT_CALL_MODE_VOICE		0
#define AGENT_CALL_MODE_DATA		1
#define AGENT_CALL_MODE_FAX		2

#define AGENT_CALL_MULTIPARTY_NO		0
#define AGENT_CALL_MULTIPARTY_YES		1

/* Subscriber number parameters*/
#define AGENT_SUBSCRIBER_SERVICE_VOICE	4

/* Operator selection mode values */
#define AGENT_OPERATOR_MODE_AUTO			0
#define HSP_VERSION_1_2  0x0102

/* Voice recognition blacklist file */
#define AGENT_VR_BLACKLIST_FILE (APP_SYSCONFDIR"/voice-recognition-blacklist")

#define BT_LOWER_ADDRESS_LENGTH 9

enum hfp_version {
	HFP_VERSION_1_5 = 0x0105,
	HFP_VERSION_1_6 = 0x0106,
	HFP_VERSION_LATEST = HFP_VERSION_1_6,
};

/* BD Address */
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bt_addrs;

/**
 * @brief Outgoing call type status
 *
 * 0 : Follow last call log \n
 * 1 : Voice call \n
 * 2 : Video call \n
 */
#define BT_FOLLOW_CALL_LOG 0
#define BT_VOICE_CALL 1
#define BT_VIDEO_CALL 2

/**
 * @brief The status of making outgoing calls with BT headsets
 *
 * 0 : Even when device locked \n
 * 1 : Only when device unlocked \n
 */
#define BT_MO_EVEN_LOCKED 0
#define BT_MO_ONLY_UNLOCKED 1

#define BT_CVSD_CODEC_ID 1
#define BT_MSBC_CODEC_ID 2

#define BT_CVSD_CODEC_MASK 0x0001
#define BT_MSBC_CODEC_MASK 0x0002

#define BT_HFP_MSBC_VOICE			0x0063
#define BT_HFP_CVSD_VOICE			0x0060

#define BT_SOCKET_LEVEL			274
#define BT_VOICE_NUM			11

#define BT_SCO_PRTCL	2

#define HFP_CODEC_NEGOTIATION_TIMEOUT 3 /* 3 seconds */

#define BT_SCO_OPEN_DELAY_TIMER 1000 /*1000 milliseconds*/

/* AT+CSQ : Returns received signal strength indication.
     Command response: +CSQ: <rssi>,<ber>
    <ber> is not supported and has a constant value of 99, included for compatibility reasons.
*/
#define BT_SIGNAL_QUALITY_BER 99

/*Length of the string used to send telephone number*/
#define BT_MAX_TEL_NUM_STRING 30

#define FUCNTION_CALLS
#ifdef FUCNTION_CALLS
#define FN_START	DBG("ENTER==>")
#define FN_END		DBG("EXIT===>")
#else
#define FN_START
#define FN_END
#endif

/* HS states */
typedef enum {
	HEADSET_STATE_DISCONNECTED,
	HEADSET_STATE_CONNECTING,
	HEADSET_STATE_CONNECTED,
	HEADSET_STATE_PLAY_IN_PROGRESS,
	HEADSET_STATE_ON_CALL
} hs_state_t;

typedef enum {
	HFP_STATE_MNGR_ERR_AG_FAILURE = 0,
	HFP_STATE_MNGR_NO_PHONE_CONNECTION = 1,
	HFP_STATE_MNGR_ERR_NOT_ALLOWED	= 3,
	HFP_STATE_MNGR_ERR_NOT_SUPPORTED	= 4,
	HFP_STATE_MNGR_ERR_SIM_BUSY	= 14,
	HFP_STATE_MNGR_ERR_INVALID_INDEX	= 21,
	HFP_STATE_MNGR_ERR_INVALID_CHAR_IN_STRING	= 25,
	HFP_STATE_MNGR_ERR_NO_NETWORK_SERVICE	= 30,
	HFP_STATE_MNGR_ERR_NONE = 0x8000
} hfp_state_manager_err_t;

typedef enum {
	BT_AG_FEATURE_THREE_WAY_CALL			= 0x0001,
	BT_AG_FEATURE_EC_AND_NR				= 0x0002,
	BT_AG_FEATURE_VOICE_RECOGNITION			= 0x0004,
	BT_AG_FEATURE_INBAND_RINGTONE			= 0x0008,
	BT_AG_FEATURE_ATTACH_NUMBER_TO_VOICETAG		= 0x0010,
	BT_AG_FEATURE_REJECT_CALL			= 0x0020,
	BT_AG_FEATURE_ENHANCED_CALL_STATUS		= 0x0040,
	BT_AG_FEATURE_ENHANCED_CALL_CONTROL		= 0x0080,
	BT_AG_FEATURE_EXTENDED_ERROR_RESULT_CODES	= 0x0100,
	BT_AG_FEATURE_CODEC_NEGOTIATION			= 0x0200,
} bt_ag_agent_feature_t;

typedef enum {
	BT_HF_FEATURE_EC_ANDOR_NR			= 0x0001,
	BT_HF_FEATURE_CALL_WAITING_AND_3WAY	= 0x0002,
	BT_HF_FEATURE_CLI_PRESENTATION		= 0x0004,
	BT_HF_FEATURE_VOICE_RECOGNITION		= 0x0008,
	BT_HF_FEATURE_REMOTE_VOLUME_CONTROL	= 0x0010,
	BT_HF_FEATURE_ENHANCED_CALL_STATUS		= 0x0020,
	BT_HF_FEATURE_ENHANCED_CALL_CONTROL	= 0x0040,
	BT_HF_FEATURE_CODEC_NEGOTIATION	= 0x0080,
} bt_hf_agent_feature_t;

/* HFP AG service record bitmap. Bluetooth HFP 1.6 spec page 95 */
#define BT_AG_FEATURE_SDP_3WAY			0x1
#define BT_AG_FEATURE_SDP_ECNR			0x2
#define BT_AG_FEATURE_SDP_VOICE_RECOG		0x4
#define BT_AG_FEATURE_SDP_IN_BAND_RING_TONE	0x8
#define BT_AG_FEATURE_SDP_ATTACH_VOICE_TAG		0x10
#define BT_AG_FEATURE_SDP_WIDEBAND_SPEECH		0x20

#define BT_AG_AGENT_ERROR (__bt_ag_agent_error_quark())

#define BT_ERROR_INTERNAL "InternalError"
#define BT_ERROR_NOT_AVAILABLE "NotAvailable"
#define BT_ERROR_NOT_CONNECTED "NotConnected"
#define BT_ERROR_BUSY "InProgress"
#define BT_ERROR_INVALID_PARAM "InvalidArguments"
#define BT_ERROR_ALREADY_EXSIST "AlreadyExists"
#define BT_ERROR_ALREADY_CONNECTED "Already Connected"
#define BT_ERROR_NO_MEMORY "No memory"
#define BT_ERROR_I_O_ERROR "I/O error"
#define BT_ERROR_OPERATION_NOT_AVAILABLE "Operation currently not available"
#define BT_ERROR_BATTERY "Battery error "
#define BT_ERROR_SIGNAL "Signal error"
#define BT_ERROR_NO_CALL_LOG "No Call log"
#define BT_ERROR_INVLAID_DTMF "Invalid dtmf"

#define BT_CHECK_SIGNAL_STRENGTH(rssi) \
	if (rssi >= VCONFKEY_TELEPHONY_RSSI_4) \
		rssi = VCONFKEY_TELEPHONY_RSSI_5

typedef enum {
	BT_HFP_AGENT_ERROR_NONE,
	BT_HFP_AGENT_ERROR_INTERNAL,
	BT_HFP_AGENT_ERROR_NOT_AVAILABLE,
	BT_HFP_AGENT_ERROR_NOT_CONNECTED,
	BT_HFP_AGENT_ERROR_BUSY,
	BT_HFP_AGENT_ERROR_INVALID_PARAM,
	BT_HFP_AGENT_ERROR_ALREADY_EXSIST,
	BT_HFP_AGENT_ERROR_ALREADY_CONNECTED,
	BT_HFP_AGENT_ERROR_NO_MEMORY,
	BT_HFP_AGENT_ERROR_I_O_ERROR,
	BT_HFP_AGENT_ERROR_OPERATION_NOT_AVAILABLE,
	BT_HFP_AGENT_ERROR_NO_CALL_LOGS,
	BT_HFP_AGENT_ERROR_INVALID_MEMORY_INDEX,
	BT_HFP_AGENT_ERROR_INVALID_CHLD_INDEX,
	BT_HFP_AGENT_ERROR_BATTERY_STATUS,
	BT_HFP_AGENT_ERROR_SIGNAL_STATUS,
	BT_HFP_AGENT_ERROR_NOT_SUPPORTED,
	BT_HFP_AGENT_ERROR_INVALID_NUMBER,
	BT_HFP_AGENT_ERROR_APPLICATION,
	BT_HFP_AGENT_ERROR_INVALID_DTMF,
} bt_hfp_agent_error_t;

typedef enum {
	BT_AGENT_NETWORK_REG_STATUS_HOME,
	BT_AGENT_NETWORK_REG_STATUS_ROAMING,
	BT_AGENT_NETWORK_REG_STATUS_OFFLINE,
	BT_AGENT_NETWORK_REG_STATUS_SEARCHING,
	BT_AGENT_NETWORK_REG_STATUS_NO_SIM,
	BT_AGENT_NETWORK_REG_STATUS_POWEROFF,
	BT_AGENT_NETWORK_REG_STATUS_POWERSAFE,
	BT_AGENT_NETWORK_REG_STATUS_NO_COVERAGE,
	BT_AGENT_NETWORK_REG_STATUS_REJECTED,
	BT_AGENT_NETWORK_REG_STATUS_UNKOWN,
} bt_hfp_agent_network_registration_status_t;

typedef enum {
	BT_AGENT_NETWORK_REG_STATUS_NOT_REGISTER,
	BT_AGENT_NETWORK_REG_STATUS_REGISTER_HOME_NETWORK,
	BT_AGENT_NETWORK_REG_STATUS_SEARCH,
	BT_AGENT_NETWORK_REG_STATUS_REGISTRATION_DENIED,
	BT_AGENT_NETWORK_REG_STATUS_UNKNOWN,
	BT_AGENT_NETWORK_REG_STATUS_REGISTERED_ROAMING,
	BT_AGENT_NETWORK_REG_STATUS_REGISTERED_SMS_HOME,
	BT_AGENT_NETWORK_REG_STATUS_REGISTERED_SMS_ROAMING,
	BT_AGENT_NETWORK_REG_STATUS_EMERGENCY,
	BT_AGENT_NETWORK_REG_STATUS_REGISTERED_CSFB_HOME,
	BT_AGENT_NETWORK_REG_STATUS_REGISTERED_CSFB_ROAMING,
} bt_hfp_agent_reg_status_t;

#ifdef TIZEN_MEDIA_ENHANCE
typedef enum media_transport_state {
    MEDIA_TRANSPORT_STATE_DISCONNECTED,
    MEDIA_TRANSPORT_STATE_IDLE,
    MEDIA_TRANSPORT_STATE_PLAYING
} bt_ag_media_transport_state_t;

/* Profile states matched to btd_service_state_t of bluez service.h */
typedef enum {
	BT_PROFILE_STATE_UNAVAILABLE,
	BT_PROFILE_STATE_DISCONNECTED,
	BT_PROFILE_STATE_CONNECTING,
	BT_PROFILE_STATE_CONNECTED,
	BT_PROFILE_STATE_DISCONNECTING,
} bt_profile_state_t;
#endif

#define retv_if(expr, val) \
	do { \
		if (expr) { \
			ERR("(%s) return", #expr); \
			return (val); \
		} \
	} while (0)

#define ret_if(expr) \
	do { \
		if (expr) { \
			ERR("(%s) return", #expr); \
			return; \
		} \
	} while (0)

typedef struct {
	unsigned char b[6];
} __attribute__((packed)) bdaddr_t;

/* Remote socket address */
struct sockaddr_remote {
	sa_family_t	family;
	bdaddr_t	remote_bdaddr;
	uint8_t		channel;
};

typedef struct {
	const char *indicator_desc;
	const char *indicator_range;
	int hfp_value;
	gboolean ignore;
	gboolean is_activated;
} bt_ag_indicators_t;

typedef struct {
	gboolean telephony_ready;       /* plugin initialized */
	uint32_t features;	      /* AG features */
	const bt_ag_indicators_t *indicators;     /* Supported indicators */
	int er_mode;		   /* Event reporting mode */
	int er_ind;		    /* Event reporting for indicators */
	int rh;			/* Response and Hold state */
	char *number;		  /* Incoming phone number */
	int number_type;		/* Incoming number type */
	guint ring_timer;		/* For incoming call indication */
	const char *chld;		/* Response to AT+CHLD=? */
	uint32_t sdp_features; /* SDP features */
} bt_ag_status_t;

typedef struct {
	char buffer[MAX_BUFFER_SIZE];

	int start;
	int length;

	gboolean is_nrec;
	gboolean is_nrec_req;
	gboolean is_pending_ring;
	gboolean is_inband_ring;
	gboolean is_cme_enabled;
	gboolean is_cwa_enabled;
	gboolean is_client_active;
	gboolean is_voice_recognition_running;

	int speaker_gain;
	int microphone_gain;

	unsigned int hs_features;
} bt_ag_slconn_t;

typedef struct {
/*	DBusMessage *msg;
	DBusPendingCall *call;*/
	GIOChannel *io;
	int err;
	hs_state_t target_state;
	GSList *callbacks;
	uint16_t svclass;
} hs_connecting_t;

typedef struct {
	char *object_path;
	gboolean is_negotiating;
	gboolean requested_by_hf;
	guint nego_timer;
	unsigned int remote_codecs;
	unsigned int sending_codec;
	unsigned int final_codec;
} bt_negotiation_info_t;

typedef struct {
	const char *path;
	guint32 fd;

	gboolean auto_connect;
	GIOChannel *io_chan;
	char *remote_addr;
	guint watch_id;
	GIOChannel *sco_server;
	guint sco_watch_id;

	GIOChannel *rfcomm;
	GIOChannel *sco;
	guint sco_id;
	guint codec;

	gboolean auto_dc;

	guint dc_timer;

	gboolean hfp_active;
	gboolean search_hfp;
	gboolean rfcomm_initiator;
	gboolean vr_blacklisted;

	hs_state_t state;
	bt_ag_slconn_t *slc;
	hs_connecting_t *pending;
	GSList *nrec_cbs;
	gboolean sco_server_started;
	gboolean nrec_status;
	bt_negotiation_info_t codec_info;
#if defined(TIZEN_SUPPORT_DUAL_HF)
	gboolean is_companion_device;
#endif
} bt_ag_info_t;

typedef void (*headset_nrec_cb) (bt_ag_info_t *hs,
					gboolean nrec,
					void *user_data);

struct hs_nrec_callback {
	unsigned int id;
	headset_nrec_cb cb;
	void *user_data;
};

typedef void (*hs_state_cb) (bt_ag_info_t *hs,
		hs_state_t old_state,
		hs_state_t new_state,
		void *user_data);

struct hs_state_callback {
		hs_state_cb cb;
		void *user_data;
		unsigned int id;
};

int __attribute__((format(printf, 2, 3)))
			_bt_ag_send_at(bt_ag_info_t *hs, char *format, ...);
void __attribute__((format(printf, 3, 4)))
		_bt_ag_send_foreach_headset(GSList *devices,
		int (*cmp) (bt_ag_info_t *hs),
		char *format, ...);
void _bt_ag_slconn_complete(bt_ag_info_t *hs);
int _bt_ag_send_response(bt_ag_info_t *hs, hfp_state_manager_err_t err);
void _bt_hfp_call_hold_request(const char *t_cmd, void *t_device);
void _bt_hfp_key_press_request(const char *t_key_press, void *t_device);
void _bt_hfp_terminate_call_request(void *t_device);
void _bt_hfp_answer_call_request(void *t_device);
void _bt_hfp_update_event_request(int indicator, void *t_device);
void _bt_hfp_response_and_hold_request(void *t_device);
void _bt_hfp_last_dialed_number_request(void *t_device);
void _bt_hfp_dial_number_request(const char *dial_number, void *t_device);
void _bt_hfp_channel_dtmf_request(char t_tone, void *t_device);
void _bt_hfp_subscriber_number_request(void *t_device);
void _bt_hfp_get_operator_selection_request(void *t_device);
void _bt_hfp_noise_red_and_echo_cancel_request(gboolean t_enable,
			void *t_device);
void _bt_hfp_voice_dial_request(gboolean t_enable, void *t_device);
void _bt_hfp_set_indicators(const char *t_command, void *t_device);
void _bt_hfp_select_phonebook_memory_status(void *t_device);
void _bt_hfp_select_phonebook_memory_list(void *t_device);
void _bt_hfp_select_phonebook_memory(void *t_device, const gchar *pb_path);
void _bt_hfp_read_phonebook_entries_list(void *t_device);
void _bt_hfp_read_phonebook_entries(void *t_device, const char *cmd);
void _bt_hfp_find_phonebook_entries_status(void *t_device);
void _bt_hfp_find_phonebook_entries(void *t_device, const char *cmd);
void _bt_hfp_get_character_set(void *t_device);
void _bt_hfp_list_supported_character(void *t_device);
void _bt_hfp_set_character_set(void *t_device, const char *cmd);
void _bt_hfp_get_battery_property(void *t_device);
void _bt_hfp_signal_quality_reply(int32_t rssi, int32_t ber,
	void *t_device);
void _bt_hfp_battery_property_reply(void *data, int32_t bcs,
			int32_t bcl);
void _bt_hfp_operator_reply(char *operator_name,  void *t_device);
bt_hfp_agent_error_t _bt_ag_agent_dial_num(const gchar *number, guint flags);
bt_hfp_agent_error_t _bt_ag_agent_dial_last_num(void *device);
bt_hfp_agent_error_t _bt_ag_agent_send_dtmf(const gchar *dtmf,
				const gchar *path, const gchar *sender);
bt_hfp_agent_error_t _bt_ag_agent_dial_memory(unsigned int location);
gboolean _bt_ag_agent_get_signal_quality(void *device);
gboolean _bt_ag_agent_get_battery_status(void *device);
gboolean _bt_ag_agent_get_operator_name(void *device);
gboolean _bt_hfp_agent_nrec_status(gboolean status,
	void *t_device);
gboolean _bt_ag_agent_voice_dial(gboolean activate);
gboolean _bt_ag_agent_answer_call(unsigned int call_id,
				const gchar *path, const gchar *sender);
gboolean _bt_ag_agent_reject_call(unsigned int call_id,
				const gchar *path, const gchar *sender);
gboolean _bt_ag_agent_release_call(unsigned int call_id,
				const gchar *path, const gchar *sender);
gboolean _bt_ag_agent_threeway_call(unsigned int chld_value,
				const gchar *path, const gchar *sender);
void _bt_list_current_calls(void *t_device);
void _bt_get_activity_status(void *t_device);
int _bt_hfp_set_property_name(const char *property, const char *operator_name);
void _bt_hfp_get_imei_number_reply(char *imei_number,  void *t_device);
void _bt_hfp_get_model_info_reply(char *model,  void *t_device);
void _bt_hfp_get_device_manufacturer_reply(char *manufacturer,  void *t_device);
void _bt_hfp_get_revision_info_reply(char *revision,  void *t_device);
void _bt_hfp_device_disconnected(void *t_device);
int _bt_hfp_get_equipment_identity(bt_ag_info_t *device, const char *buf);
int _bt_hfp_get_model_information(bt_ag_info_t *device, const char *buf);
int _bt_hfp_get_device_manufacturer(bt_ag_info_t *device, const char *buf);
int _bt_hfp_get_imsi(bt_ag_info_t *device, const char *buf);
int _bt_hfp_get_creg_status(bt_ag_info_t *device, const char *buf);
int _bt_hfp_get_revision_information(bt_ag_info_t *device, const char *buf);
void _bt_hfp_get_equipment_identity_req(void *t_device);
bt_hfp_agent_error_t _bt_hfp_register_telephony_agent(gboolean register_flag,
		const char *path_to_register,
		const char *sender);
bt_hfp_agent_error_t _bt_hfp_incoming_call(const char *call_path,
		const char *incoming_number,
		uint32_t incoming_call_id,
		const char *sender);
bt_hfp_agent_error_t _bt_hfp_outgoing_call(const char *call_path,
		const char *number,
		uint32_t call_id, const char *sender);
bt_hfp_agent_error_t _bt_hfp_change_call_status(const char *call_path,
		const char *number,
		uint32_t call_status,
		uint32_t call_id,
		const char *sender);
void _bt_hfp_initialize_telephony_manager(uint32_t ag_features);
void _bt_hfp_deinitialize_telephony_manager(void);
gboolean _bt_ag_agent_emit_property_changed(
				GDBusConnection *connection,
				const char *path,
				const char *interface,
				const char *name,
				GVariant *property);
void _bt_hfp_get_model_info_req(void *t_device);
void _bt_hfp_get_device_manufacturer_req(void *t_device);
void _bt_hfp_get_imsi_req(void *t_device);
void _bt_hfp_get_creg_status_req(void *t_device);
void _bt_hfp_get_revision_info_req(void *t_device);
gboolean _bt_hfp_is_call_exist(void);
void _bt_hfp_release_all_calls_by_sender(const char *sender);
void _bt_hfp_get_imsi_reply(char *mcc, char *mnc, char *msin, void *t_device);
void _bt_hfp_get_creg_status_reply(int n, int status, void *t_device);
int _bt_hfp_set_property_value(const char *property, int value);
void _bt_hfp_vendor_cmd_request(const char *cmd,
						void *t_device);

gboolean _bt_ag_agent_get_imei_number(void *device);
void _bt_ag_agent_get_model_name(void *device);
void _bt_ag_agent_get_manufacturer_name(void *device);
void _bt_ag_agent_get_imsi(void *device);
void _bt_ag_agent_get_creg_status(void *device);
void _bt_ag_agent_get_revision_information(void *device);
bt_hfp_agent_error_t _bt_ag_agent_vendor_cmd(const gchar *cmd,
		const gchar *path, const gchar *sender);
#ifdef TIZEN_MEDIA_ENHANCE
void _bt_ag_agent_check_transport_state(void);
#endif

#endif /* __DEF_BT_AG_AGENT_H_ */
