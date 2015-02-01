/*
 * bluetoth-ag-handler.h
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <glib.h>
#include "bluetooth-ag-agent.h"

#define BT_HFP_SPEAKER_GAIN 'S'
#define BT_HFP_MICROPHONE_GAIN 'M'

#define AG_RING_INTERVAL 1500

typedef struct {
	gboolean wbs_enable;
	uint8_t i2s_enable;
	uint8_t is_master;
	uint8_t clock_rate;
	uint8_t pcm_interface_rate;
} wbs_options;

/* BD Address */
typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bt_addr;

int _bt_hfp_supported_features(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_report_indicators(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_enable_indicators(bt_ag_info_t *hdset, const char *buffer);
int _bt_hfp_call_hold(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_answer_call(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_dial_number(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_signal_gain_setting(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_terminate_call(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_key_press(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_last_dialed_number(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_response_and_hold(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_cli_notification(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_dtmf_tone(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_subscriber_number(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_list_current_calls(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_extended_errors(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_call_waiting_notify(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_operator_selection(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_nr_and_ec(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_voice_dial(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_apl_command(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_apl_command(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_indicators_activation(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_select_pb_memory(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_read_pb_entries(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_select_character_set(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_find_pb_entires(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_get_signal_quality(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_get_battery_charge_status(bt_ag_info_t *hs, const char *buf);
int _bt_calling_stopped_indicator(void);
int _bt_incoming_call_indicator(const char *number, int type);
int _bt_call_waiting_indicator(const char *number, int type);
void _bt_hfp_deinitialize(void);
gboolean __bt_ring_timer_cb(gpointer data);
bt_hfp_agent_error_t _bt_hfp_event_indicator(int index);
void _bt_hfp_set_ag_indicator(uint32_t ag_features,
				const bt_ag_indicators_t *ag_indicators,
				int rh, const char *chld);
int _bt_calling_stopped_indicator(void);
int _bt_incoming_call_indicator(const char *number, int type);
int _bt_dial_number_response(void *t_device, bt_hfp_agent_error_t err);
int _bt_event_reporting_response(void *t_device,
				bt_hfp_agent_error_t err);
int _bt_terminate_call_response(void *t_device,
				hfp_state_manager_err_t err);
int _bt_call_hold_response(void *t_device, bt_hfp_agent_error_t err);
int _bt_key_press_response(void *t_device, bt_hfp_agent_error_t err);
int _bt_subscriber_number_indicator(const char *call_num,
				int type, int service);
int _bt_subscriber_number_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_list_current_call_indicator(int index, int direction, int mode,
		int status, const char *call_num, int conference, int t_num);
int _bt_list_current_calls_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_nr_and_ec_response(void *t_device, bt_hfp_agent_error_t err);
int _bt_voice_dial_response(void *t_device, bt_hfp_agent_error_t err);
int _bt_indicators_activation_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_select_phonebook_memory_status_response(void *t_device,
						const char *path,
						uint32_t total, uint32_t used,
						bt_hfp_agent_error_t err);
int _bt_select_phonebook_memory_list_response(void *t_device,
						const char *buf,
						bt_hfp_agent_error_t err);
int _bt_select_phonebook_memory_response(void *t_device,
						bt_hfp_agent_error_t err);
int _bt_read_phonebook_entries_list_response(void *t_device,
						uint32_t used,
						uint32_t number_length,
						uint32_t name_length,
						bt_hfp_agent_error_t err);
int _bt_read_phonebook_entries_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_find_phonebook_entries_status_indicator(uint32_t number_length,
					uint32_t name_length);
int _bt_find_phonebook_entries_status_response(void *t_device,
						bt_hfp_agent_error_t err);
int _bt_supported_character_generic_response(void *t_device,
						char *character_set_list,
						bt_hfp_agent_error_t err);
int _bt_set_characterset_generic_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_signal_quality_response(void *t_device,
						int32_t rssi,
						int32_t ber,
						bt_hfp_agent_error_t err);
int _bt_battery_charge_status_response(void *t_device,
						int32_t bcs,
						int32_t bcl,
						bt_hfp_agent_error_t err);
int _bt_operator_selection_indicator(int mode, const char *oper);
int _bt_operator_selection_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_transmit_dtmf_response(void *t_device,
			bt_hfp_agent_error_t err);
int _bt_find_phonebook_entries_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_response_and_hold_response(void *t_device,
					bt_hfp_agent_error_t err);
int _bt_answer_call_response(void *hs, bt_hfp_agent_error_t err);
int _bt_hfp_get_activity_status_rsp(void *t_device,
						int status,
						bt_hfp_agent_error_t err);
int _bt_hfp_get_activity_status(bt_ag_info_t *device, const char *buf);
int _bt_hfp_set_speaker_gain(bt_ag_info_t *hs,
		uint16_t gain_value);
int _bt_hfp_set_microphone_gain(bt_ag_info_t *hs,
		uint16_t gain_value);
int _bt_hfp_set_voice_dial(bt_ag_info_t *hs,
		gboolean enable);
int _bt_hfp_get_equipment_identity_rsp(void *t_device,
				char *identity, bt_hfp_agent_error_t err);
int _bt_hfp_get_imsi_rsp(void *t_device,
		char *mcc, char *mnc, char *msin, bt_hfp_agent_error_t err);
int _bt_hfp_get_creg_status_rsp(void *t_device,
		int n, int status, bt_hfp_agent_error_t err);
int _bt_hfp_get_model_info_rsp(void *t_device,
				char *model, bt_hfp_agent_error_t err);
int _bt_hfp_get_device_manufacturer_rsp(void *t_device,
				char *manufacturer, bt_hfp_agent_error_t err);
int _bt_hfp_get_revision_info_rsp(void *t_device,
				char *revision, bt_hfp_agent_error_t err);
int _bt_hfp_vendor_cmd(bt_ag_info_t *hs, const char *buf);
int _bt_hfp_send_vendor_cmd(bt_ag_info_t *hs,
		const char *cmd);
int _bt_vendor_cmd_response(void *t_device,
			bt_hfp_agent_error_t err);
