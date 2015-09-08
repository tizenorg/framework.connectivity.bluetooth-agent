/*
 * bluetooth-ag-handler.c
 *
 * Copyright (c) 2014 - 2015 Samsung Electronics Co., Ltd. All rights reserved.
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
#include "bluetooth-ag-agent.h"
#include "bluetooth-ag-handler.h"
#include "vconf.h"
#include "vconf-keys.h"

extern bt_ag_status_t ag;
extern GSList *active_devices;
extern GDBusConnection *ag_dbus_conn;
extern gchar *remote_dev_path;

 /* AT+CSQ : Returns received signal strength indication.
     Command response: +CSQ: <rssi>,<ber>
    <ber> is not supported and has a constant value of 99, included for compatibility reasons.
*/
#define BT_SIGNAL_QUALITY_BER 99

wbs_options wbs_opts = {
	.wbs_enable = FALSE,
	.i2s_enable = 0x00,
	.is_master = 0x00,
	.clock_rate = 0x02,
	.pcm_interface_rate = 0x00,
};

/* AT+BRSF response */
int _bt_hfp_supported_features(bt_ag_info_t *hs, const char *buf)
{
	bt_ag_slconn_t *slconn = hs->slc;
	int err;
//	bt_hfp_agent_error_t ret = BT_HFP_AGENT_ERROR_NONE;

	DBG("AT + BRSF");
	if (strlen(buf) < 9)
		return -EINVAL;

	slconn->hs_features = strtoul(&buf[8], NULL, 10);
#if 0 /* SCO is crashed if below is called when SCO is opened by hf-agent */
	if (slconn->hs_features & BT_HF_FEATURE_CODEC_NEGOTIATION) {
		ret = _bt_ag_set_codec(hs, "SetWbsParameters");
		if (ret != BT_HFP_AGENT_ERROR_NONE)
			ERR("Unable to set the default WBC codec");
	} else {
		/* Default codec is NB */
		ret = _bt_ag_set_codec(hs, "SetNbParameters");
		if (ret != BT_HFP_AGENT_ERROR_NONE)
			ERR("Unable to set the default NBC codec");
	}
#endif
	err = _bt_ag_send_at(hs, "\r\n+BRSF: %u\r\n", ag.features);
	if (err < 0)
		return err;

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

static char *__bt_get_indicator_ranges(const bt_ag_indicators_t *indicators)
{
	int i;
	GString *gstr;

	DBG("__bt_get_indicator_ranges");
	gstr = g_string_new("\r\n+CIND: ");

	for (i = 0; indicators[i].indicator_desc != NULL; i++) {
		if (i == 0)
			g_string_append_printf(gstr, "(\"%s\",(%s))",
				indicators[i].indicator_desc,
				indicators[i].indicator_range);
		else
			g_string_append_printf(gstr, ",(\"%s\",(%s))",
				indicators[i].indicator_desc,
				indicators[i].indicator_range);
	}
	g_string_append(gstr, "\r\n");
	return g_string_free(gstr, FALSE);
}

static char *__bt_get_indicator_values(const bt_ag_indicators_t *indicators)
{
	int i;
	GString *gstr;

	gstr = g_string_new("\r\n+CIND: ");
	DBG("__bt_get_indicator_values");
	for (i = 0; indicators[i].indicator_range != NULL; i++) {
		if (i == 0)
			g_string_append_printf(gstr, "%d",
				indicators[i].hfp_value);
		else
			g_string_append_printf(gstr, ",%d",
				indicators[i].hfp_value);
	}
	g_string_append(gstr, "\r\n");

	return g_string_free(gstr, FALSE);
}

static int __bt_check_hdset(bt_ag_info_t *hdset)
{
	bt_ag_slconn_t *slconn = hdset->slc;

	if (!hdset->hfp_active)
		return -1;

	if (slconn->is_client_active)
		return 0;
	else
		return -1;
}

static int __bt_hfp_cmp(bt_ag_info_t *hs)
{
	if (hs->hfp_active)
		return 0;
	else
		return -1;
}

static int __bt_cwa_cmp(bt_ag_info_t *hs)
{
	if (!hs->hfp_active)
		return -1;

	if (hs->slc->is_cwa_enabled)
		return 0;
	else
		return -1;
}

gboolean __bt_ring_timer_cb(gpointer data)
{
	_bt_ag_send_foreach_headset(active_devices, NULL, "\r\nRING\r\n");

	if (ag.number)
		_bt_ag_send_foreach_headset(active_devices, __bt_check_hdset,
					"\r\n+CLIP: \"%s\",%d\r\n",
					ag.number, ag.number_type);

	return TRUE;
}

int _bt_incoming_call_indicator(const char *number, int type)
{
	bt_ag_info_t *hs;
	bt_ag_slconn_t *slconn;

	if (!active_devices)
		return -ENODEV;

	/* Get the updated list of connected devices */
	hs = active_devices->data;
	slconn = hs->slc;

	if (ag.ring_timer) {
		DBG("incoming_call_indicator: already calling....");
		return -EBUSY;
	}

	/*If inband ring supported then no need to send RING alert to HF */
	if (!hs->hfp_active && slconn->is_inband_ring) {
		DBG("Inband ring tone supported");
		return 0;
	}

	DBG("Inband ring tone not supported.. so send a RING to HF");
	g_free(ag.number);
	ag.number = g_strdup(number);
	ag.number_type = type;

	if (slconn->is_inband_ring &&
					hs->state != HEADSET_STATE_ON_CALL) {
		slconn->is_pending_ring = TRUE;
		return 0;
	}

	__bt_ring_timer_cb(NULL);
	ag.ring_timer = g_timeout_add(AG_RING_INTERVAL, __bt_ring_timer_cb, NULL);

	return 0;
}

int _bt_calling_stopped_indicator(void)
{
	bt_ag_info_t *dev;

	if (ag.ring_timer) {
		g_source_remove(ag.ring_timer);
		ag.ring_timer = 0;
	}

	if (!active_devices)
		return 0;

	/* In case SCO is in intermediate state to connect */
	dev = active_devices->data;

	if (!dev->slc->is_pending_ring && !ag.ring_timer)
		return -EINVAL;

	dev->slc->is_pending_ring = FALSE;

	return 0;
}

void _bt_hfp_set_ag_indicator(uint32_t ag_features,
			const bt_ag_indicators_t *ag_indicators, int rh,
			const char *chld)
{
	DBG("Set Ag Features");
	ag.telephony_ready = TRUE;
	ag.features = ag_features;
	ag.indicators = ag_indicators;
	ag.rh = rh;
	ag.chld = chld;
}

void _bt_hfp_deinitialize(void)
{
	g_free(ag.number);
	memset(&ag, 0, sizeof(ag));
	ag.rh = BT_RSP_HOLD_NOT_SUPPORTED;
	ag.er_mode = 3;
}

/* Send event indication call from Statemanager module */
bt_hfp_agent_error_t _bt_hfp_event_indicator(int index)
{
	if (!active_devices) {
		DBG("No Active devices present");
		return BT_HFP_AGENT_ERROR_NOT_AVAILABLE;
	}

	if (!ag.er_ind) {
		DBG("Indicate event called but event reporting is disabled");
		return BT_HFP_AGENT_ERROR_INTERNAL;
	}

	DBG("Sending event notification to hf....");

	_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
				"\r\n+CIEV: %d,%d\r\n", index + 1,
				ag.indicators[index].hfp_value);

	return BT_HFP_AGENT_ERROR_NONE;
}

/* AT+CIND response */
int _bt_hfp_report_indicators(bt_ag_info_t *hs, const char *buf)
{
	int err;
	char *str;

	if (strlen(buf) < 8)
		return -EINVAL;

	if (buf[7] == '=')
		str = __bt_get_indicator_ranges(ag.indicators);
	else
		str = __bt_get_indicator_values(ag.indicators);

	err = _bt_ag_send_at(hs, "%s", str);

	g_free(str);

	if (err < 0)
		return err;

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

/* AT+CMER response */
int _bt_event_reporting_response(void *t_device,
				bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hdset = t_device;
	bt_ag_slconn_t *slconn = hdset->slc;
	int ret_val;

	if (err != (bt_hfp_agent_error_t) HFP_STATE_MNGR_ERR_NONE)
		return _bt_ag_send_response(t_device, err);

	ret_val = _bt_ag_send_at(hdset, "\r\nOK\r\n");
	if (ret_val < 0)
		return ret_val;

	if (hdset->state != HEADSET_STATE_CONNECTING)
		return 0;

	if (slconn->hs_features & HANDSFREE_FEATURE_CALL_WAITING_AND_3WAY &&
			ag.features & BT_AG_FEATURE_THREE_WAY_CALL)
		return 0;

	_bt_ag_slconn_complete(hdset);

	return 0;
}

int _bt_hfp_enable_indicators(bt_ag_info_t *hdset, const char *buffer)
{
	if (strlen(buffer) < 13)
		return -EINVAL;

	/* tokenks can be <mode>,<keyp>,<disp>,<ind>,<bfr>*/
	char **ind_tokens = g_strsplit(&buffer[8], ",", 5);

	if (g_strv_length(ind_tokens) < 4) {
		g_strfreev(ind_tokens);
		return -EINVAL;
	}

	ag.er_mode = atoi(ind_tokens[0]);
	ag.er_ind = atoi(ind_tokens[3]);

	DBG("hfp_enable_indicators (CMER): indicator=%d, mode=%d",
		ag.er_ind, ag.er_mode);

	g_strfreev(ind_tokens);
	ind_tokens = NULL;

	switch (ag.er_ind) {
	case 0:
	case 1:
		_bt_hfp_update_event_request(ag.er_ind, hdset);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

	/* AT+CHLD response */
int _bt_hfp_call_hold(bt_ag_info_t *hs, const char *buf)
{
	int err;

	if (strlen(buf) < 9)
		return -EINVAL;

	if (buf[8] != '?') {
		_bt_hfp_call_hold_request(&buf[8], hs);
		return 0;
	}

	err = _bt_ag_send_at(hs, "\r\n+CHLD: (%s)\r\n", ag.chld);
	if (err < 0)
		return err;

	err = _bt_ag_send_at(hs, "\r\nOK\r\n");
	if (err < 0)
		return err;

	_bt_ag_slconn_complete(hs);

	return 0;
}

int _bt_key_press_response(void *t_device, bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_key_press(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 9)
		return -EINVAL;

	if (ag.ring_timer) {
		g_source_remove(ag.ring_timer);
		ag.ring_timer = 0;
	}

	_bt_hfp_key_press_request(&buf[8], hs);

	return 0;
}

int _bt_answer_call_response(void *hs, bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(hs, err);
}

int _bt_hfp_answer_call(bt_ag_info_t *hs, const char *buf)
{
	if (ag.ring_timer) {
		g_source_remove(ag.ring_timer);
		ag.ring_timer = 0;
	}

	if (ag.number) {
		g_free(ag.number);
		ag.number = NULL;
	}

	if (remote_dev_path)
		g_free(remote_dev_path);

	remote_dev_path = g_strdup(hs->path);

	_bt_hfp_answer_call_request(hs);

	return 0;
}
int _bt_terminate_call_response(void *t_device,
					hfp_state_manager_err_t err)
{
	bt_ag_info_t *hs = t_device;

	if (err != HFP_STATE_MNGR_ERR_NONE)
		return _bt_ag_send_response(hs, err);

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_hfp_terminate_call(bt_ag_info_t *hs, const char *buf)
{
	if (ag.number) {
		g_free(ag.number);
		ag.number = NULL;
	}

	if (ag.ring_timer) {
		g_source_remove(ag.ring_timer);
		ag.ring_timer = 0;
	}

	_bt_hfp_terminate_call_request(hs);

	return 0;
}

int _bt_hfp_cli_notification(bt_ag_info_t *hs, const char *buf)
{
	bt_ag_slconn_t *slconn = hs->slc;

	if (strlen(buf) < 9)
		return -EINVAL;

	slconn->is_client_active = buf[8] == '1' ? TRUE : FALSE;

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_response_and_hold_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_response_and_hold(bt_ag_info_t *hs, const char *buf)
{

	if (strlen(buf) < 8)
		return -EINVAL;

	if (ag.rh == BT_RSP_HOLD_NOT_SUPPORTED)
		return _bt_ag_send_response(hs,
			HFP_STATE_MNGR_ERR_NOT_SUPPORTED);

	if (buf[7] == '=') {
		_bt_hfp_response_and_hold_request(hs);
		return 0;
	}

	if (ag.rh >= 0)
		_bt_ag_send_at(hs, "\r\n+BTRH: %d\r\n", ag.rh);

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_hfp_last_dialed_number(bt_ag_info_t *hs, const char *buf)
{
	if (remote_dev_path)
		g_free(remote_dev_path);

	remote_dev_path = g_strdup(hs->path);
	_bt_hfp_last_dialed_number_request(hs);

	return 0;
}

int _bt_dial_number_response(void *t_device, bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_dial_number(bt_ag_info_t *hs, const char *buf)
{
	char number[MAX_BUFFER_SIZE];
	size_t buf_len;

	buf_len = strlen(buf);

	if (buf[buf_len - 1] != ';') {
		DBG("Reject the non-voice call dial number request");
		return -EINVAL;
	}

	memset(number, 0, sizeof(number));
	strncpy(number, &buf[3], buf_len - 4);

	if (remote_dev_path)
		g_free(remote_dev_path);

	remote_dev_path = g_strdup(hs->path);

	_bt_hfp_dial_number_request(number, hs);

	return 0;
}

static int __bt_headset_set_gain(bt_ag_info_t *hs, uint16_t gain, char type)
{
	bt_ag_slconn_t *slconn = hs->slc;
	const char *property;

	if (gain > 15) {
		DBG("Invalid gain value: %u", gain);
		return -EINVAL;
	}

	switch (type) {
	case BT_HFP_SPEAKER_GAIN:
		if (slconn->speaker_gain == gain) {
			DBG("Ignoring no-change in speaker gain");
			return -EALREADY;
		}
		property = "SpeakerGain";
		slconn->speaker_gain = gain;
		break;
	case BT_HFP_MICROPHONE_GAIN:
		if (slconn->microphone_gain == gain) {
			DBG("Ignoring no-change in microphone gain");
			return -EALREADY;
		}
		property = "MicrophoneGain";
		slconn->microphone_gain = gain;
		break;
	default:
		DBG("Unknown gain setting\n");
		return -EINVAL;
	}

	_bt_ag_agent_emit_property_changed(ag_dbus_conn, hs->path,
				BT_HEADSET_INTERFACE, property,
				g_variant_new("q", gain));
	return 0;
}

int _bt_hfp_signal_gain_setting(bt_ag_info_t *hs, const char *buf)
{
	uint16_t gain;
	int err;

	if (strlen(buf) < 8) {
		DBG("very short string to use for Gain setting\n");
		return -EINVAL;
	}

	gain = (uint16_t) strtol(&buf[7], NULL, 10);

	err = __bt_headset_set_gain(hs, gain, buf[5]);
	if (err < 0 && err != -EALREADY)
		return err;

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_transmit_dtmf_response(void *t_device,
			bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_dtmf_tone(bt_ag_info_t *hs, const char *buf)
{
	char tone;

	if (strlen(buf) < 8) {
		printf("Too short string for DTMF tone");
		return -EINVAL;
	}

	tone = buf[7];
	if (tone >= '#' && tone <= 'D')
		_bt_hfp_channel_dtmf_request(tone, hs);
	else
		return -EINVAL;

	return 0;
}

int _bt_hfp_set_speaker_gain(bt_ag_info_t *hs,
		uint16_t gain_value)
{
	int err;
	char type = BT_HFP_SPEAKER_GAIN;

	err = __bt_headset_set_gain(hs, gain_value, type);
	if (err < 0)
		return BT_HFP_AGENT_ERROR_INTERNAL;

	if (hs->state == HEADSET_STATE_ON_CALL) {
		err = _bt_ag_send_at(hs, "\r\n+VG%c=%u\r\n", type,
				gain_value);
		if (err < 0)
			return BT_HFP_AGENT_ERROR_INTERNAL;
	}
	return BT_HFP_AGENT_ERROR_NONE;
}

int _bt_hfp_set_microphone_gain(bt_ag_info_t *hs,
		uint16_t gain_value)
{
	int err;
	char type = BT_HFP_MICROPHONE_GAIN;

	err = __bt_headset_set_gain(hs, gain_value, type);
	if (err < 0)
		return BT_HFP_AGENT_ERROR_INTERNAL;

	if (hs->state == HEADSET_STATE_ON_CALL) {
		err = _bt_ag_send_at(hs, "\r\n+VG%c=%u\r\n", type,
				gain_value);
		if (err < 0)
			return BT_HFP_AGENT_ERROR_INTERNAL;
	}
	return BT_HFP_AGENT_ERROR_NONE;
}


int _bt_hfp_set_voice_dial(bt_ag_info_t *hs,
		gboolean enable)
{
	DBG("_bt_hfp_set_voice_dial = %d", enable);

	if (_bt_ag_send_at(hs, "\r\n+BVRA: %d\r\n", enable) < 0)
		return BT_HFP_AGENT_ERROR_INTERNAL;

	return BT_HFP_AGENT_ERROR_NONE;
}

int _bt_hfp_send_vendor_cmd(bt_ag_info_t *hs,
		const char *cmd)
{
	DBG("_bt_hfp_send_vendor_cmd = %d", cmd);

	if (_bt_ag_send_at(hs, "\r\n%s\r\n", cmd) < 0)
		return BT_HFP_AGENT_ERROR_INTERNAL;

	return BT_HFP_AGENT_ERROR_NONE;
}

int _bt_vendor_cmd_response(void *t_device,
			bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_vendor_cmd(bt_ag_info_t *hs, const char *buf)
{
	DBG("XSAT vendor command");

	_bt_hfp_vendor_cmd_request(buf, hs);

	return 0;
}

int _bt_list_current_call_indicator(bt_ag_info_t *hs, int index, int direction,
	int mode, int status, const char *call_num, int conference, int t_num)
{
	if (call_num && strlen(call_num) > 0) {
		_bt_ag_send_at(hs,
			"\r\n+CLCC: %d,%d,%d,%d,%d,\"%s\",%d\r\n",
			index, direction, status, mode, conference,
				call_num, t_num);
	} else {
		_bt_ag_send_at(hs,
			"\r\n+CLCC: %d,%d,%d,%d,%d\r\n",
			index, direction, status, mode, conference);
	}

	return 0;
}
int _bt_subscriber_number_indicator(const char *call_num, int type, int service)
{
	if (!active_devices)
		return -ENODEV;

	_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
				"\r\n+CNUM: ,%s,%d,,%d\r\n",
				call_num, type, service);
	return 0;
}

int _bt_subscriber_number_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_subscriber_number(bt_ag_info_t *hs, const char *buf)
{
	_bt_hfp_subscriber_number_request(hs);

	return 0;
}

int _bt_call_waiting_indicator(const char *number, int type)
{
	if (!active_devices)
		return -ENODEV;

	DBG("Call waiting indicator to hf");
	_bt_ag_send_foreach_headset(active_devices, __bt_cwa_cmp,
				"\r\n+CCWA: \"%s\",%d\r\n",
				number, type);
	return 0;
}

int _bt_list_current_calls_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_list_current_calls(bt_ag_info_t *hs, const char *buf)
{
	_bt_list_current_calls(hs);

	return 0;
}

int _bt_hfp_extended_errors(bt_ag_info_t *hs, const char *buf)
{
	bt_ag_slconn_t *slconn = hs->slc;

	if (strlen(buf) < 9)
		return -EINVAL;

	if (buf[8] == '1') {
		slconn->is_cme_enabled = TRUE;
		DBG("CME errors enabled for headset %p", hs);
	} else {
		slconn->is_cme_enabled = FALSE;
		DBG("CME errors disabled for headset %p", hs);
	}

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_hfp_call_waiting_notify(bt_ag_info_t *hs, const char *buf)
{
	bt_ag_slconn_t *slconn = hs->slc;

	if (strlen(buf) < 9)
		return -EINVAL;

	if (buf[8] == '1') {
		slconn->is_cwa_enabled = TRUE;
		DBG("Call waiting notification enabled for headset %p", hs);
	} else {
		slconn->is_cwa_enabled = FALSE;
		DBG("Call waiting notification disabled for headset %p", hs);
	}

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_operator_selection_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_call_hold_response(void *t_device, bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_nr_and_ec_response(void *t_device, bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;
	bt_ag_slconn_t *slconn = hs->slc;

	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		GSList *l;

		for (l = hs->nrec_cbs; l; l = l->next) {
			struct hs_nrec_callback *nrec_cb = l->data;

			nrec_cb->cb(hs, slconn->is_nrec_req,
				nrec_cb->user_data);
		}

		slconn->is_nrec = hs->slc->is_nrec_req;
	}

	return _bt_ag_send_response(t_device, err);
}

int _bt_voice_dial_response(void *t_device, bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_operator_selection_indicator(int mode, const char *oper)
{
	if (!active_devices)
		return -ENODEV;

	_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
				"\r\n+COPS: %d,0,\"%s\"\r\n",
				mode, oper);
	return 0;
}

int _bt_hfp_operator_selection(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 8)
		return -EINVAL;

	switch (buf[7]) {
	case '?':
		_bt_hfp_get_operator_selection_request(hs);
		break;
	case '=': {
		if (buf[8] == '?')
			return _bt_ag_send_at(hs, "\r\n+CME ERROR: %d\r\n",
				HFP_STATE_MNGR_ERR_NOT_SUPPORTED);
		else
			return _bt_ag_send_at(hs, "\r\nOK\r\n");
	}
	default:
		return -EINVAL;
	}

	return 0;
}

int _bt_hfp_nr_and_ec(bt_ag_info_t *hs, const char *buf)
{
	bt_ag_slconn_t *slconn = hs->slc;

	if (strlen(buf) < 9)
		return -EINVAL;

	if (buf[8] == '0')
		slconn->is_nrec_req = FALSE;
	else
		slconn->is_nrec_req = TRUE;

	_bt_hfp_noise_red_and_echo_cancel_request(slconn->is_nrec_req, hs);

	return 0;
}

int _bt_hfp_voice_dial(bt_ag_info_t *hs, const char *buf)
{
	bt_ag_slconn_t *slconn = hs->slc;
	gboolean enable;

	if (strlen(buf) < 9)
		return -EINVAL;

	if (buf[8] == '0')
		enable = FALSE;
	else
		enable = TRUE;

	_bt_hfp_voice_dial_request(enable, hs);

	slconn->is_voice_recognition_running = enable;

	return 0;
}

int _bt_hfp_indicators_activation(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 7) {
		printf("Invalid indicator activation request\n");
		return -EINVAL;
	}

	_bt_hfp_set_indicators(&buf[6], hs);
	return 0;
}

int _bt_indicators_activation_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_select_phonebook_memory_status_response(void *t_device,
						const char *path,
						uint32_t total, uint32_t used,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;
	bt_ag_slconn_t *slconn = hs->slc;

	if (err != (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		if (slconn->is_cme_enabled)
			return _bt_ag_send_at(hs,
					"\r\n+CME ERROR: %d\r\n", err);
		else
			return _bt_ag_send_at(hs, "\r\nERROR\r\n");
	}

	if (!active_devices)
		return -ENODEV;

	_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
			"\r\n+CPBS: %s,%d,%d\r\n",
			path, used, total);

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_select_phonebook_memory_list_response(void *t_device,
						const char *buf,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;
	bt_ag_slconn_t *slconn = hs->slc;

	if ((err != (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) ||
				(NULL == buf)) {
		if (slconn->is_cme_enabled)
			return _bt_ag_send_at(hs,
					"\r\n+CME ERROR: %d\r\n", err);
		else
			return _bt_ag_send_at(hs, "\r\nERROR\r\n");
	}

	if (NULL != buf) {
		if (!active_devices)
			return -ENODEV;

		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
					"\r\n+CPBS: %s\r\n", buf);

	}
	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_select_phonebook_memory_response(void *t_device,
						bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_select_pb_memory(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 8)
		return -EINVAL;

	if (buf[7] == '?') {
		_bt_hfp_select_phonebook_memory_status(hs);
		return 0;
	}

	if (buf[7] == '=') {
		if (buf[8] == '?') {
			_bt_hfp_select_phonebook_memory_list(hs);
			return 0;
		}
		_bt_hfp_select_phonebook_memory(hs, &buf[8]);
		return 0;
	}

	return -EINVAL;
}

int _bt_read_phonebook_entries_list_response(void *t_device,
						uint32_t used,
						uint32_t number_length,
						uint32_t name_length,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;
	bt_ag_slconn_t *slconn = hs->slc;

	int send_err = 0;
	int index = 1;

	if (err != (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		if (slconn->is_cme_enabled)
			return _bt_ag_send_at(hs,
					"\r\n+CME ERROR: %d\r\n", err);
		else
			return _bt_ag_send_at(hs, "\r\nERROR\r\n");
	}

	if (used < 1)
		index = 0;

	send_err = _bt_ag_send_at(hs, "\r\n+CPBR: (%d-%d),%d,%d\r\n",
			index, used, number_length, name_length);
	if (send_err < 0)
		return send_err;

	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_read_phonebook_entries_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_read_phonebook_entries_indicator(const char *name, const char *number,
					uint32_t handle)
{
	int type = 129;
	const char *pos = NULL;

	pos = number;
	while (*pos == ' ' || *pos == '\t')
		pos++;

	/* 145 means international access code, otherwise 129 is used */
	if (*pos == '+')
		type = 145;

	_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
			"\r\n+CPBR: %d,\"%s\",%d,\"%s\"\r\n",
			handle, number, type, name);
	return 0;
}

int _bt_hfp_read_pb_entries(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 8)
		return -EINVAL;

	if (buf[7] != '=')
		return -EINVAL;

	if (buf[8] == '?')
		_bt_hfp_read_phonebook_entries_list(hs);
	else
		_bt_hfp_read_phonebook_entries(hs, &buf[8]);

	return 0;
}

int _bt_find_phonebook_entries_status_response(void *t_device,
						bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_find_phonebook_entries_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_find_phonebook_entries_status_indicator(uint32_t number_length,
					uint32_t name_length)
{
	_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
			"\r\n+CPBF: %d,%d\r\n",
			number_length, name_length);

	return 0;
}

int _bt_hfp_find_pb_entires(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 8)
		return -EINVAL;

	if (buf[7] != '=')
		return -EINVAL;

	if (buf[8] == '?')
		_bt_hfp_find_phonebook_entries_status(hs);
	else
		_bt_hfp_find_phonebook_entries(hs, &buf[8]);

	return 0;
}

int _bt_supported_character_generic_response(void *t_device,
						char *character_set_list,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;
	bt_ag_slconn_t *slconn = hs->slc;

	if (err != (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		if (slconn->is_cme_enabled)
			return _bt_ag_send_at(hs,
					"\r\n+CME ERROR: %d\r\n", err);
		else
			return _bt_ag_send_at(hs, "\r\nERROR\r\n");
	}

	if (NULL != character_set_list) {
		if (!active_devices)
			return -ENODEV;

		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
			"\r\n+CSCS: %s\r\n", character_set_list);
	}
	return _bt_ag_send_at(hs, "\r\nOK\r\n");
}

int _bt_set_characterset_generic_response(void *t_device,
					bt_hfp_agent_error_t err)
{
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_select_character_set(bt_ag_info_t *hs, const char *buf)
{
	if (NULL != buf) {
		if (strlen(buf) < 7)
			return -EINVAL;

		if (buf[7] == '?') {
			_bt_hfp_get_character_set(hs);
			return 0;
		}

		if (buf[7] == '=') {
			if (buf[8] == '?')
				_bt_hfp_list_supported_character(hs);
			else
				_bt_hfp_set_character_set(hs, &buf[8]);
		}
	}
	return 0;

}

int _bt_battery_charge_status_response(void *t_device,
						int32_t bcs,
						int32_t bcl,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;

	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
					"\r\n+CBC: %d,%d\r\n", bcs, bcl);
	}

	return _bt_ag_send_response(hs, err);
}

int _bt_hfp_get_battery_charge_status(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 6)
		return -EINVAL;

	if (buf[6] == '=')
		return _bt_ag_send_response(hs, HFP_STATE_MNGR_ERR_NONE);

	_bt_hfp_get_battery_property(hs);
	return 0;
}

int _bt_hfp_apl_command(bt_ag_info_t *hs, const char *buf)
{
	DBG("Got Apple command: %s", buf);

	return _bt_ag_send_response(hs, HFP_STATE_MNGR_ERR_NONE);
}

/* convert signal strength to a RSSI level */
static int __bt_telephony_convert_signal_to_rssi(int signal)
{
	/* input  : BT signal strength (0~5) */
	/* output : RSSI strength (0~31) */
	switch (signal) {
	case 0: return 0;
	case 1: return 4;
	case 2: return 8;
	case 3: return 13;
	case 4: return 19;
	case 5: return 31;
	}

	if (signal > 5)
		return 31;

	return 0;
}

int _bt_signal_quality_response(void *t_device,
						int32_t rssi,
						int32_t ber,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *hs = t_device;

	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
			"\r\n+CSQ: %d,%d\r\n",
			__bt_telephony_convert_signal_to_rssi(rssi), ber);
	}
	return _bt_ag_send_response(hs, err);
}

int _bt_telephony_signal_quality_list_supported_response(void *t_device,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *device = t_device;

	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
					"\r\n+CSQ: (0-31,99),(99)\r\n");
	}
	return _bt_ag_send_response(device, err);
}

int _bt_hfp_get_signal_quality(bt_ag_info_t *hs, const char *buf)
{
	if (strlen(buf) < 6)
		return -EINVAL;

	if (buf[6] == '=')
		_bt_telephony_signal_quality_list_supported_response(hs,
					HFP_STATE_MNGR_ERR_NONE);
	else
		_bt_ag_agent_get_signal_quality(hs);

	return 0;
}

int _bt_hfp_get_activity_status_rsp(void *t_device,
						int status,
						bt_hfp_agent_error_t err)
{
	bt_ag_info_t *device = t_device;

	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE) {
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
					"\r\n+CPAS: %d\r\n", status);
	}

	return _bt_ag_send_response(device, err);
}

int _bt_hfp_get_activity_status(bt_ag_info_t *device, const char *buf)
{
	if (strlen(buf) < 7)
		return -EINVAL;

	if (buf[7] == '?') {
		return _bt_ag_send_response(device,
					HFP_STATE_MNGR_ERR_AG_FAILURE);
	} else if (buf[7] == '=') {
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
					"\r\n+CPAS: (0-4)\r\n");
		return _bt_ag_send_response(device, HFP_STATE_MNGR_ERR_NONE);
	}

	_bt_get_activity_status(device);
	return 0;
}

int _bt_hfp_get_equipment_identity_rsp(void *t_device,
				char *identity, bt_hfp_agent_error_t err)
{
	if (identity)
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
		"\r\n+CGSN: %s\r\n", identity);
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_get_imsi_rsp(void *t_device,
		char *mcc, char *mnc, char *msin, bt_hfp_agent_error_t err)
{
	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE)
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
				"\r\n%s%s%s\r\n", mcc,mnc,msin);
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_get_creg_status_rsp(void *t_device,
		int n, int status, bt_hfp_agent_error_t err)
{
	if (err == (bt_hfp_agent_error_t)HFP_STATE_MNGR_ERR_NONE)
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
				"\r\n+CREG: %d,%d\r\n", n, status);
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_get_equipment_identity(bt_ag_info_t *device, const char *buf)
{
	int len = strlen(buf);

	if (len == 9 && buf[7] == '=' && buf[8] == '?')  /* AT+CGSN=? */
		return _bt_ag_send_response(device, HFP_STATE_MNGR_ERR_NONE);

	else if (len > 7)
		return -EINVAL;

	_bt_hfp_get_equipment_identity_req(device); /* AT+CGSN */
	return 0;
}

int _bt_hfp_get_model_info_rsp(void *t_device, char *model,
						bt_hfp_agent_error_t err)
{
	if (model)
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
		"\r\n+CGMM: %s\r\n", model);
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_get_model_information(bt_ag_info_t *device, const char *buf)
{
	int len = strlen(buf);

	if (len == 9 && buf[7] == '=' && buf[8] == '?')  /* AT+CGMM=? */
		return _bt_ag_send_response(device, HFP_STATE_MNGR_ERR_NONE);

	else if (len > 7)
		return -EINVAL;

	_bt_hfp_get_model_info_req(device);/* AT+CGMM */
	return 0;
}

int _bt_hfp_get_device_manufacturer_rsp(void *t_device,
				char *manufacturer, bt_hfp_agent_error_t err)
{
	if (manufacturer)
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
		"\r\n+CGMI: %s\r\n", manufacturer);
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_get_device_manufacturer(bt_ag_info_t *device, const char *buf)
{
	int len = strlen(buf);

	if (len == 9 && buf[7] == '=' && buf[8] == '?')  /* AT+CGMI=? */
		return _bt_ag_send_response(device, HFP_STATE_MNGR_ERR_NONE);

	else if (len > 7)
		return -EINVAL;

	_bt_hfp_get_device_manufacturer_req(device);
	return 0;
}

int _bt_hfp_get_imsi(bt_ag_info_t *device, const char *buf)
{
	int len = strlen(buf);
	DBG_SECURE("Buf %s", buf);

	if (len == 7) {
		_bt_hfp_get_imsi_req(device);
	} else {
		_bt_ag_send_response(device, HFP_STATE_MNGR_ERR_INVALID_INDEX);
	}

	return 0;
}

int _bt_hfp_get_creg_status(bt_ag_info_t *device, const char *buf)
{
	int len = strlen(buf);
	DBG_SECURE("buf %s", buf);
	if (len < 7 || len > 9)
		return -EINVAL;
	else if (len == 7) {
		_bt_ag_send_response(device, HFP_STATE_MNGR_ERR_INVALID_INDEX);
	} else if (buf[7] == '=') {
		_bt_ag_send_response(device, HFP_STATE_MNGR_ERR_INVALID_INDEX);
	} else if (buf[7] == '?') {
		_bt_hfp_get_creg_status_req(device);
	}
	return 0;
}
int _bt_hfp_get_revision_info_rsp(void *t_device, char *revision,
						bt_hfp_agent_error_t err)
{
	if (revision)
		_bt_ag_send_foreach_headset(active_devices, __bt_hfp_cmp,
		"\r\n+CGMR: %s\r\n", revision);
	return _bt_ag_send_response(t_device, err);
}

int _bt_hfp_get_revision_information(bt_ag_info_t *device, const char *buf)
{
	int len = strlen(buf);

	if (len == 9 && buf[7] == '=' && buf[8] == '?')  /* AT+CGMR=? */
		return _bt_ag_send_response(device, HFP_STATE_MNGR_ERR_NONE);

	else if (len > 7)
		return -EINVAL;

	_bt_hfp_get_revision_info_req(device);
	return 0;
}
