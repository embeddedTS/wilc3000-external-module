// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include "netdev.h"

#define WILC_HIF_SCAN_TIMEOUT_MS                5000
#define WILC_HIF_CONNECT_TIMEOUT_MS             9500

#define WILC_FALSE_FRMWR_CHANNEL		    100

#if KERNEL_VERSION(3, 17, 0) > LINUX_VERSION_CODE
struct ieee80211_wmm_ac_param {
	u8 aci_aifsn; /* AIFSN, ACM, ACI */
	u8 cw; /* ECWmin, ECWmax (CW = 2^ECW - 1) */
	__le16 txop_limit;
} __packed;

struct ieee80211_wmm_param_ie {
	u8 element_id; /* Element ID: 221 (0xdd); */
	u8 len; /* Length: 24 */
	u8 oui[3]; /* 00:50:f2 */
	u8 oui_type; /* 2 */
	u8 oui_subtype; /* 1 */
	u8 version; /* 1 for WMM version 1.0 */
	u8 qos_info; /* AP/STA specific QoS info */
	u8 reserved; /* 0 */
	/* AC_BE, AC_BK, AC_VI, AC_VO */
	struct ieee80211_wmm_ac_param ac[4];
} __packed;
#endif

struct send_buffered_eap {
	void (*deliver_to_stack)(struct wilc_vif *vif, u8 *buff, u32 size,
			      u32 pkt_offset, u8 status);
	void (*eap_buf_param)(void *priv);
	u8 *buff;
	unsigned int size;
	unsigned int pkt_offset;
	void *user_arg;
};

#define WILC_SCAN_WID_LIST_SIZE		6

struct wilc_rcvd_mac_info {
	u8 status;
};

struct wilc_set_multicast {
	u32 enabled;
	u32 cnt;
	u8 *mc_list;
};

struct host_if_wowlan_trigger {
	u8 wowlan_trigger;
};

struct host_if_set_ant {
	u8 mode;
	u8 antenna1;
	u8 antenna2;
	u8 gpio_mode;
};

struct tx_power {
	u8 tx_pwr;
};

struct power_mgmt_param {
	bool enabled;
	u32 timeout;
};

struct wilc_del_all_sta {
	u8 assoc_sta;
	u8 mac[WILC_MAX_NUM_STA][ETH_ALEN];
};

struct add_sta_param {
	u8 bssid[ETH_ALEN];
	u16 aid;
	u8 supported_rates_len;
	const u8 *supported_rates;
	bool ht_supported;
	struct ieee80211_ht_cap ht_capa;
	u16 flags_mask;
	u16 flags_set;
};

union wilc_message_body {
	struct wilc_rcvd_net_info net_info;
	struct wilc_rcvd_mac_info mac_info;
	struct wilc_set_multicast mc_info;
	struct wilc_remain_ch remain_on_ch;
	char *data;
	struct host_if_wowlan_trigger wow_trigger;
	struct send_buffered_eap send_buff_eap;
	struct host_if_set_ant set_ant;
	struct tx_power tx_power;
	struct power_mgmt_param pwr_mgmt_info;
	struct add_sta_param add_sta_info;
	struct add_sta_param edit_sta_info;
};

struct host_if_msg {
	union wilc_message_body body;
	struct wilc_vif *vif;
	struct work_struct work;
	void (*fn)(struct work_struct *ws);
	struct completion work_comp;
	bool is_sync;
};

/* 'msg' should be free by the caller for syc */
static struct host_if_msg*
wilc_alloc_work(struct wilc_vif *vif, void (*work_fun)(struct work_struct *),
		bool is_sync)
{
	struct host_if_msg *msg;

	if (!work_fun)
		return ERR_PTR(-EINVAL);

	msg = kzalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return ERR_PTR(-ENOMEM);
	msg->fn = work_fun;
	msg->vif = vif;
	msg->is_sync = is_sync;
	if (is_sync)
		init_completion(&msg->work_comp);

	return msg;
}

static int wilc_enqueue_work(struct host_if_msg *msg)
{
	INIT_WORK(&msg->work, msg->fn);

	if (!msg->vif || !msg->vif->wilc || !msg->vif->wilc->hif_workqueue)
		return -EINVAL;

	if (!queue_work(msg->vif->wilc->hif_workqueue, &msg->work))
		return -EINVAL;

	return 0;
}

/* The idx starts from 0 to (NUM_CONCURRENT_IFC - 1), but 0 index used as
 * special purpose in wilc device, so we add 1 to the index to starts from 1.
 * As a result, the returned index will be 1 to NUM_CONCURRENT_IFC.
 */
int wilc_get_vif_idx(struct wilc_vif *vif)
{
	return vif->idx + 1;
}

/* We need to minus 1 from idx which is from wilc device to get real index
 * of wilc->vif[], because we add 1 when pass to wilc device in the function
 * wilc_get_vif_idx.
 * As a result, the index should be between 0 and (NUM_CONCURRENT_IFC - 1).
 */
static struct wilc_vif *wilc_get_vif_from_idx(struct wilc *wilc, int idx)
{
	int index = idx - 1;
	struct wilc_vif *vif;

	if (index < 0 || index >= WILC_NUM_CONCURRENT_IFC)
		return NULL;

	list_for_each_entry_rcu(vif, &wilc->vif_list, list) {
		if (vif->idx == index)
			return vif;
	}

	return NULL;
}

int handle_scan_done(struct wilc_vif *vif, enum scan_event evt)
{
	int result = 0;
	u8 abort_running_scan;
	struct wid wid;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_user_scan_req *scan_req;
	u8 null_bssid[6] = {0};

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "handling scan done\n");

	if (!hif_drv) {
		PRINT_ER(vif->ndev, "hif driver is NULL\n");
		return result;
	}

	if (evt == SCAN_EVENT_DONE) {
		if (memcmp(hif_drv->assoc_bssid, null_bssid, ETH_ALEN) == 0)
			hif_drv->hif_state = HOST_IF_IDLE;
		else
			hif_drv->hif_state = HOST_IF_CONNECTED;
	} else if (evt == SCAN_EVENT_ABORTED) {
		PRINT_INFO(vif->ndev, GENERIC_DBG, "Abort running scan\n");
		abort_running_scan = 1;
		wid.id = WID_ABORT_RUNNING_SCAN;
		wid.type = WID_CHAR;
		wid.val = (s8 *)&abort_running_scan;
		wid.size = sizeof(char);

		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
		if (result) {
			PRINT_ER(vif->ndev, "Failed to set abort running\n");
			result = -EFAULT;
		}
	}

	scan_req = &hif_drv->usr_scan_req;
	if (scan_req->scan_result) {
		scan_req->scan_result(evt, NULL, scan_req->arg);
		scan_req->scan_result = NULL;
	}

	return result;
}

static void handle_send_buffered_eap(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct send_buffered_eap *hif_buff_eap = &msg->body.send_buff_eap;

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Sending bufferd eapol to WPAS\n");
	if (!hif_buff_eap->buff)
		goto out;

	if (hif_buff_eap->deliver_to_stack)
		hif_buff_eap->deliver_to_stack(vif, hif_buff_eap->buff,
					       hif_buff_eap->size,
					       hif_buff_eap->pkt_offset,
					       PKT_STATUS_BUFFERED);
	if (hif_buff_eap->eap_buf_param)
		hif_buff_eap->eap_buf_param(hif_buff_eap->user_arg);

	if (hif_buff_eap->buff != NULL) {
		kfree(hif_buff_eap->buff);
		hif_buff_eap->buff = NULL;
	}

out:
	kfree(msg);
}

int wilc_scan(struct wilc_vif *vif, u8 scan_source, u8 scan_type,
	      u8 *ch_freq_list, u8 ch_list_len,
	      void (*scan_result_fn)(enum scan_event,
				     struct wilc_rcvd_net_info *, void *),
	      void *user_arg, struct cfg80211_scan_request *request)
{
	int result = 0;
	struct wid wid_list[WILC_SCAN_WID_LIST_SIZE];
	u32 index = 0;
	u32 i, scan_timeout;
	u8 *buffer;
	u8 valuesize = 0;
	u8 *search_ssid_vals = NULL;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_vif *vif_tmp;
	int srcu_idx;

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Setting SCAN params\n");
	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Scanning: In [%d] state\n",
		   hif_drv->hif_state);

	srcu_idx = srcu_read_lock(&vif->wilc->srcu);
	list_for_each_entry_rcu(vif_tmp, &vif->wilc->vif_list, list) {
		struct host_if_drv *hif_drv_tmp;

		if (vif_tmp == NULL || vif_tmp->hif_drv == NULL)
			continue;

		hif_drv_tmp = vif_tmp->hif_drv;

		if (hif_drv_tmp->hif_state != HOST_IF_IDLE &&
		    hif_drv_tmp->hif_state != HOST_IF_CONNECTED) {
			PRINT_INFO(vif_tmp->ndev, GENERIC_DBG,
				   "Abort scan. In state [%d]\n",
				   hif_drv_tmp->hif_state);
			result = -EBUSY;
			srcu_read_unlock(&vif->wilc->srcu, srcu_idx);
			goto error;
		}
	}
	srcu_read_unlock(&vif->wilc->srcu, srcu_idx);

	if (vif->connecting) {
		PRINT_INFO(vif->ndev, GENERIC_DBG,
			   "Don't do scan in (CONNECTING) state\n");
		result = -EBUSY;
		goto error;
	}

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Setting SCAN params\n");
	hif_drv->usr_scan_req.ch_cnt = 0;

	if (request->n_ssids) {
		for (i = 0; i < request->n_ssids; i++)
			valuesize += ((request->ssids[i].ssid_len) + 1);
		search_ssid_vals = kmalloc(valuesize + 1, GFP_KERNEL);
		if (search_ssid_vals) {
			wid_list[index].id = WID_SSID_PROBE_REQ;
			wid_list[index].type = WID_STR;
			wid_list[index].val = search_ssid_vals;
			buffer = wid_list[index].val;

			*buffer++ = request->n_ssids;

		PRINT_INFO(vif->ndev, HOSTINF_DBG,
			   "In Handle_ProbeRequest number of ssid %d\n",
			 request->n_ssids);
			for (i = 0; i < request->n_ssids; i++) {
				*buffer++ = request->ssids[i].ssid_len;
				memcpy(buffer, request->ssids[i].ssid,
				       request->ssids[i].ssid_len);
				buffer += request->ssids[i].ssid_len;
			}
			wid_list[index].size = (s32)(valuesize + 1);
			index++;
		}
	}

	wid_list[index].id = WID_INFO_ELEMENT_PROBE;
	wid_list[index].type = WID_BIN_DATA;
	wid_list[index].val = (s8 *)request->ie;
	wid_list[index].size = request->ie_len;
	index++;

	wid_list[index].id = WID_SCAN_TYPE;
	wid_list[index].type = WID_CHAR;
	wid_list[index].size = sizeof(char);
	wid_list[index].val = (s8 *)&scan_type;
	index++;

#if KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE
	scan_timeout = WILC_HIF_SCAN_TIMEOUT_MS;
#else
	if (scan_type == WILC_FW_PASSIVE_SCAN && request->duration) {
		wid_list[index].id = WID_PASSIVE_SCAN_TIME;
		wid_list[index].type = WID_SHORT;
		wid_list[index].size = sizeof(u16);
		wid_list[index].val = (s8 *)&request->duration;
		index++;

		scan_timeout = (request->duration * ch_list_len) + 500;
	} else {
		scan_timeout = WILC_HIF_SCAN_TIMEOUT_MS;
	}
#endif
	wid_list[index].id = WID_SCAN_CHANNEL_LIST;
	wid_list[index].type = WID_BIN_DATA;

	if (ch_freq_list && ch_list_len > 0) {
		for (i = 0; i < ch_list_len; i++) {
			if (ch_freq_list[i] > 0)
				ch_freq_list[i] -= 1;
		}
	}

	wid_list[index].val = ch_freq_list;
	wid_list[index].size = ch_list_len;
	index++;

	wid_list[index].id = WID_START_SCAN_REQ;
	wid_list[index].type = WID_CHAR;
	wid_list[index].size = sizeof(char);
	wid_list[index].val = (s8 *)&scan_source;
	index++;

	hif_drv->usr_scan_req.scan_result = scan_result_fn;
	hif_drv->usr_scan_req.arg = user_arg;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list, index);
	if (result) {
		netdev_err(vif->ndev, "Failed to send scan parameters\n");
		goto error;
	} else {
		hif_drv->scan_timer_vif = vif;
		PRINT_INFO(vif->ndev, HOSTINF_DBG,
			   ">> Starting the SCAN timer\n");
#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
		hif_drv->scan_timer.data = (unsigned long)hif_drv;
#endif
		mod_timer(&hif_drv->scan_timer,
			  jiffies + msecs_to_jiffies(scan_timeout));
	}

error:

	kfree(search_ssid_vals);

	return result;
}

static int wilc_send_connect_wid(struct wilc_vif *vif)
{
	int result = 0;
	struct wid wid_list[5];
	u32 wid_cnt = 0;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_conn_info *conn_attr = &hif_drv->conn_info;
	struct wilc_join_bss_param *bss_param = conn_attr->param;
	struct wilc_vif *vif_tmp;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&vif->wilc->srcu);
	list_for_each_entry_rcu(vif_tmp, &vif->wilc->vif_list, list) {
		struct host_if_drv *hif_drv_tmp;

		if (vif_tmp == NULL || vif_tmp->hif_drv == NULL)
			continue;

		hif_drv_tmp = vif_tmp->hif_drv;

		if (hif_drv_tmp->hif_state == HOST_IF_SCANNING) {
			PRINT_INFO(vif_tmp->ndev, GENERIC_DBG,
				   "Abort connect in state [%d]\n",
				   hif_drv_tmp->hif_state);
			result = -EBUSY;
			srcu_read_unlock(&vif->wilc->srcu, srcu_idx);
			goto error;
		}
	}
	srcu_read_unlock(&vif->wilc->srcu, srcu_idx);

	wid_list[wid_cnt].id = WID_SET_MFP;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&conn_attr->mfp_type;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_INFO_ELEMENT_ASSOCIATE;
	wid_list[wid_cnt].type = WID_BIN_DATA;
	wid_list[wid_cnt].val = conn_attr->req_ies;
	wid_list[wid_cnt].size = conn_attr->req_ies_len;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_11I_MODE;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&conn_attr->security;
	wid_cnt++;

	PRINT_D(vif->ndev, HOSTINF_DBG, "Encrypt Mode = %x\n",
		conn_attr->security);
	wid_list[wid_cnt].id = WID_AUTH_TYPE;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&conn_attr->auth_type;
	wid_cnt++;

	PRINT_D(vif->ndev, HOSTINF_DBG, "Authentication Type = %x\n",
		conn_attr->auth_type);
	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Connecting to network on channel %d\n", conn_attr->ch);

	wid_list[wid_cnt].id = WID_JOIN_REQ_EXTENDED;
	wid_list[wid_cnt].type = WID_STR;
	wid_list[wid_cnt].size = sizeof(*bss_param);
	wid_list[wid_cnt].val = (u8 *)bss_param;
	wid_cnt++;

	PRINT_D(vif->ndev, HOSTINF_DBG, "Management Frame Protection type = %x\n",
		conn_attr->mfp_type);
	PRINT_INFO(vif->ndev, GENERIC_DBG, "send HOST_IF_WAITING_CONN_RESP\n");

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list, wid_cnt);
	if (result) {
		netdev_err(vif->ndev, "failed to send config packet\n");
		goto error;
	} else {
		if (conn_attr->auth_type == WILC_FW_AUTH_SAE)
			hif_drv->hif_state = HOST_IF_EXTERNAL_AUTH;
		else
			hif_drv->hif_state = HOST_IF_WAITING_CONN_RESP;
		PRINT_INFO(vif->ndev, GENERIC_DBG,
			   "set state [%d]\n", hif_drv->hif_state);
	}

	return 0;

error:

	kfree(conn_attr->req_ies);
	conn_attr->req_ies = NULL;

	return result;
}

void handle_connect_cancel(struct wilc_vif *vif)
{
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (hif_drv->conn_info.conn_result) {
		hif_drv->conn_info.conn_result(CONN_DISCONN_EVENT_DISCONN_NOTIF,
					       0, hif_drv->conn_info.arg);
	}

	eth_zero_addr(hif_drv->assoc_bssid);

	hif_drv->conn_info.req_ies_len = 0;
	kfree(hif_drv->conn_info.req_ies);
	hif_drv->conn_info.req_ies = NULL;
	hif_drv->hif_state = HOST_IF_IDLE;
}

static void handle_connect_timeout(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	int result;
	struct wid wid;
	u16 dummy_reason_code = 0;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL\n", __func__);
		goto out;
	}

	hif_drv->hif_state = HOST_IF_IDLE;

	if (hif_drv->conn_info.conn_result) {
		hif_drv->conn_info.conn_result(CONN_DISCONN_EVENT_CONN_RESP,
					       WILC_MAC_STATUS_DISCONNECTED,
					       hif_drv->conn_info.arg);

	} else {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
	}

	wid.id = WID_DISCONNECT;
	wid.type = WID_CHAR;
	wid.val = (s8 *)&dummy_reason_code;
	wid.size = sizeof(char);

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Sending disconnect request\n");
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send disconnect\n");

	hif_drv->conn_info.req_ies_len = 0;
	kfree(hif_drv->conn_info.req_ies);
	hif_drv->conn_info.req_ies = NULL;

out:
	kfree(msg);
}

void *wilc_parse_join_bss_param(struct cfg80211_bss *bss,
				struct cfg80211_crypto_settings *crypto)
{
	struct wilc_join_bss_param *param;
	struct ieee80211_p2p_noa_attr noa_attr;
	u8 rates_len = 0;
	const u8 *tim_elm, *ssid_elm, *rates_ie, *supp_rates_ie;
	const u8 *ht_ie, *wpa_ie, *wmm_ie, *rsn_ie;
	int ret;
	const struct cfg80211_bss_ies *ies = rcu_dereference(bss->ies);

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		return NULL;

	param->beacon_period = cpu_to_le16(bss->beacon_interval);
	param->cap_info = cpu_to_le16(bss->capability);
	param->bss_type = WILC_FW_BSS_TYPE_INFRA;
	param->ch = ieee80211_frequency_to_channel(bss->channel->center_freq);
	ether_addr_copy(param->bssid, bss->bssid);

	ssid_elm = cfg80211_find_ie(WLAN_EID_SSID, ies->data, ies->len);
	if (ssid_elm) {
		if (ssid_elm[1] <= IEEE80211_MAX_SSID_LEN)
			memcpy(param->ssid, ssid_elm + 2, ssid_elm[1]);
	}

	tim_elm = cfg80211_find_ie(WLAN_EID_TIM, ies->data, ies->len);
	if (tim_elm && tim_elm[1] >= 2)
		param->dtim_period = tim_elm[3];

	memset(param->p_suites, 0xFF, 3);
	memset(param->akm_suites, 0xFF, 3);

	rates_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, ies->data, ies->len);
	if (rates_ie) {
		rates_len = rates_ie[1];
		if (rates_len > WILC_MAX_RATES_SUPPORTED)
			rates_len = WILC_MAX_RATES_SUPPORTED;
		param->supp_rates[0] = rates_len;
		memcpy(&param->supp_rates[1], rates_ie + 2, rates_len);
	}

	if (rates_len < WILC_MAX_RATES_SUPPORTED) {
		supp_rates_ie = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES,
						 ies->data, ies->len);
		if (supp_rates_ie) {
			u8 ext_rates = supp_rates_ie[1];

			if (ext_rates > (WILC_MAX_RATES_SUPPORTED - rates_len))
				param->supp_rates[0] = WILC_MAX_RATES_SUPPORTED;
			else
				param->supp_rates[0] += ext_rates;

			memcpy(&param->supp_rates[rates_len + 1],
			       supp_rates_ie + 2,
			       (param->supp_rates[0] - rates_len));
		}
	}

	ht_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, ies->data, ies->len);
	if (ht_ie)
		param->ht_capable = true;

	ret = cfg80211_get_p2p_attr(ies->data, ies->len,
				    IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
				    (u8 *)&noa_attr, sizeof(noa_attr));
	if (ret > 0) {
		param->tsf_lo = cpu_to_le32(ies->tsf);
		param->noa_enabled = 1;
		param->idx = noa_attr.index;
		if (noa_attr.oppps_ctwindow & IEEE80211_P2P_OPPPS_ENABLE_BIT) {
			param->opp_enabled = 1;
			param->opp_en.ct_window = noa_attr.oppps_ctwindow;
			param->opp_en.cnt = noa_attr.desc[0].count;
			param->opp_en.duration = noa_attr.desc[0].duration;
			param->opp_en.interval = noa_attr.desc[0].interval;
			param->opp_en.start_time = noa_attr.desc[0].start_time;
		} else {
			param->opp_enabled = 0;
			param->opp_dis.cnt = noa_attr.desc[0].count;
			param->opp_dis.duration = noa_attr.desc[0].duration;
			param->opp_dis.interval = noa_attr.desc[0].interval;
			param->opp_dis.start_time = noa_attr.desc[0].start_time;
		}
	}
	wmm_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					 WLAN_OUI_TYPE_MICROSOFT_WMM,
					 ies->data, ies->len);
	if (wmm_ie) {
		struct ieee80211_wmm_param_ie *ie;

		ie = (struct ieee80211_wmm_param_ie *)wmm_ie;
		if ((ie->oui_subtype == 0 || ie->oui_subtype == 1) &&
		    ie->version == 1) {
			param->wmm_cap = true;
			if (ie->qos_info & BIT(7))
				param->uapsd_cap = true;
		}
	}

	wpa_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					 WLAN_OUI_TYPE_MICROSOFT_WPA,
					 ies->data, ies->len);
	if (wpa_ie) {
		param->mode_802_11i = 1;
		param->rsn_found = true;
	}

	rsn_ie = cfg80211_find_ie(WLAN_EID_RSN, ies->data, ies->len);
	if (rsn_ie) {
		int offset = 8;

		param->mode_802_11i = 2;
		param->rsn_found = true;
		/* extract RSN capabilities */
		offset += (rsn_ie[offset] * 4) + 2;
		offset += (rsn_ie[offset] * 4) + 2;
		memcpy(param->rsn_cap, &rsn_ie[offset], 2);
	}

	if (param->rsn_found) {
		int i;

		param->rsn_grp_policy = crypto->cipher_group & 0xFF;
		for (i = 0; i < crypto->n_ciphers_pairwise && i < 3; i++)
			param->p_suites[i] = crypto->ciphers_pairwise[i] & 0xFF;

		for (i = 0; i < crypto->n_akm_suites && i < 3; i++)
			param->akm_suites[i] = crypto->akm_suites[i] & 0xFF;
	}

	return (void *)param;
}

static void handle_rcvd_ntwrk_info(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_rcvd_net_info *rcvd_info = &msg->body.net_info;
	struct wilc_user_scan_req *scan_req = &msg->vif->hif_drv->usr_scan_req;
	const u8 *ch_elm;
	u8 *ies;
	int ies_len;
	size_t offset;

	PRINT_D(msg->vif->ndev, HOSTINF_DBG,
		"Handling received network info\n");

	if (ieee80211_is_probe_resp(rcvd_info->mgmt->frame_control))
		offset = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	else if (ieee80211_is_beacon(rcvd_info->mgmt->frame_control))
		offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	else
		goto done;

	ies = rcvd_info->mgmt->u.beacon.variable;
	ies_len = rcvd_info->frame_len - offset;
	if (ies_len <= 0)
		goto done;

	PRINT_INFO(msg->vif->ndev, HOSTINF_DBG, "New network found\n");
	/* extract the channel from received mgmt frame */
	ch_elm = cfg80211_find_ie(WLAN_EID_DS_PARAMS, ies, ies_len);
	if (ch_elm && ch_elm[1] > 0)
		rcvd_info->ch = ch_elm[2];

	if (scan_req->scan_result)
		scan_req->scan_result(SCAN_EVENT_NETWORK_FOUND, rcvd_info,
				      scan_req->arg);

done:
	kfree(rcvd_info->mgmt);
	kfree(msg);
}

static void host_int_get_assoc_res_info(struct wilc_vif *vif,
					u8 *assoc_resp_info,
					u32 max_assoc_resp_info_len,
					u32 *rcvd_assoc_resp_info_len)
{
	int result;
	struct wid wid;

	wid.id = WID_ASSOC_RES_INFO;
	wid.type = WID_STR;
	wid.val = assoc_resp_info;
	wid.size = max_assoc_resp_info_len;

	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result) {
		*rcvd_assoc_resp_info_len = 0;
		netdev_err(vif->ndev, "Failed to send association response\n");
		return;
	}

	*rcvd_assoc_resp_info_len = wid.size;
}

static s32 wilc_parse_assoc_resp_info(u8 *buffer, u32 buffer_len,
				      struct wilc_conn_info *ret_conn_info)
{
	u8 *ies;
	u16 ies_len;
	struct wilc_assoc_resp *res = (struct wilc_assoc_resp *)buffer;

	ret_conn_info->status = le16_to_cpu(res->status_code);
	if (ret_conn_info->status == WLAN_STATUS_SUCCESS) {
		ies = &buffer[sizeof(*res)];
		ies_len = buffer_len - sizeof(*res);

		ret_conn_info->resp_ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!ret_conn_info->resp_ies)
			return -ENOMEM;

		ret_conn_info->resp_ies_len = ies_len;
	}

	return 0;
}

static inline void host_int_parse_assoc_resp_info(struct wilc_vif *vif,
						  u8 mac_status)
{
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_conn_info *conn_info = &hif_drv->conn_info;

	if (mac_status == WILC_MAC_STATUS_CONNECTED) {
		u32 assoc_resp_info_len;

		memset(hif_drv->assoc_resp, 0, WILC_MAX_ASSOC_RESP_FRAME_SIZE);

		host_int_get_assoc_res_info(vif, hif_drv->assoc_resp,
					    WILC_MAX_ASSOC_RESP_FRAME_SIZE,
					    &assoc_resp_info_len);

		PRINT_D(vif->ndev, HOSTINF_DBG,
			"Received association response = %d\n",
			assoc_resp_info_len);
		if (assoc_resp_info_len != 0) {
			s32 err = 0;

			PRINT_INFO(vif->ndev, HOSTINF_DBG,
				   "Parsing association response\n");
			err = wilc_parse_assoc_resp_info(hif_drv->assoc_resp,
							 assoc_resp_info_len,
							 conn_info);
			if (err)
				netdev_err(vif->ndev,
					   "wilc_parse_assoc_resp_info() returned error %d\n",
					   err);
		}
	}

	del_timer(&hif_drv->connect_timer);
	conn_info->conn_result(CONN_DISCONN_EVENT_CONN_RESP, mac_status, conn_info->arg);

	if (mac_status == WILC_MAC_STATUS_CONNECTED &&
	    conn_info->status == WLAN_STATUS_SUCCESS) {
		PRINT_INFO(vif->ndev, HOSTINF_DBG,
			   "MAC status : CONNECTED and Connect Status : Successful\n");
		ether_addr_copy(hif_drv->assoc_bssid, conn_info->bssid);
		hif_drv->hif_state = HOST_IF_CONNECTED;
	} else {
		PRINT_INFO(vif->ndev, HOSTINF_DBG,
			   "MAC status : %d and Connect Status : %d\n",
			   mac_status, conn_info->status);
		hif_drv->hif_state = HOST_IF_IDLE;
	}

	kfree(conn_info->resp_ies);
	conn_info->resp_ies = NULL;
	conn_info->resp_ies_len = 0;

	kfree(conn_info->req_ies);
	conn_info->req_ies = NULL;
	conn_info->req_ies_len = 0;
}

static inline void host_int_handle_disconnect(struct wilc_vif *vif)
{
	struct host_if_drv *hif_drv = vif->hif_drv;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Received WILC_MAC_STATUS_DISCONNECTED from the FW\n");
	if (hif_drv->usr_scan_req.scan_result) {
		PRINT_INFO(vif->ndev, HOSTINF_DBG,
			   "\n\n<< Abort the running OBSS Scan >>\n\n");
		del_timer(&hif_drv->scan_timer);
		handle_scan_done(vif, SCAN_EVENT_ABORTED);
	}

	if (hif_drv->conn_info.conn_result)
		hif_drv->conn_info.conn_result(CONN_DISCONN_EVENT_DISCONN_NOTIF,
					       0, hif_drv->conn_info.arg);
	else
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);

	eth_zero_addr(hif_drv->assoc_bssid);

	hif_drv->conn_info.req_ies_len = 0;
	kfree(hif_drv->conn_info.req_ies);
	hif_drv->conn_info.req_ies = NULL;
	hif_drv->hif_state = HOST_IF_IDLE;
}

static void handle_rcvd_gnrl_async_info(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct wilc_rcvd_mac_info *mac_info = &msg->body.mac_info;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL\n", __func__);
		goto free_msg;
	}

	PRINT_INFO(vif->ndev, GENERIC_DBG,
		   "Current State = %d,Received state = %d\n",
		   hif_drv->hif_state, mac_info->status);

	if (!hif_drv->conn_info.conn_result) {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
		goto free_msg;
	}

	if (hif_drv->hif_state == HOST_IF_EXTERNAL_AUTH) {
		int ret;

		pr_debug("%s: external SAE processing: bss=%pM akm=%u\n",
			 __func__, vif->auth.bssid, vif->auth.key_mgmt_suite);
		ret = cfg80211_external_auth_request(vif->ndev, &vif->auth,
						     GFP_KERNEL);
		hif_drv->hif_state = HOST_IF_WAITING_CONN_RESP;
	} else if (hif_drv->hif_state == HOST_IF_WAITING_CONN_RESP) {
		host_int_parse_assoc_resp_info(vif, mac_info->status);
	} else if (mac_info->status == WILC_MAC_STATUS_DISCONNECTED) {
		if (hif_drv->hif_state == HOST_IF_CONNECTED) {
			host_int_handle_disconnect(vif);
		} else if (hif_drv->usr_scan_req.scan_result) {
			PRINT_WRN(vif->ndev, HOSTINF_DBG,
				  "Received WILC_MAC_STATUS_DISCONNECTED. Abort the running Scan");
			del_timer(&hif_drv->scan_timer);
			handle_scan_done(vif, SCAN_EVENT_ABORTED);
		}
	}

free_msg:
	kfree(msg);
}

int wilc_disconnect(struct wilc_vif *vif)
{
	struct wid wid;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_user_scan_req *scan_req;
	struct wilc_conn_info *conn_info;
	int result;
	u16 dummy_reason_code = 0;
	struct wilc_vif *vif_tmp;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&vif->wilc->srcu);
	list_for_each_entry_rcu(vif_tmp, &vif->wilc->vif_list, list) {
		struct host_if_drv *hif_drv_tmp;

		if (vif_tmp == NULL || vif_tmp->hif_drv == NULL)
			continue;

		hif_drv_tmp = vif_tmp->hif_drv;

		if (hif_drv_tmp->hif_state == HOST_IF_SCANNING) {
			PRINT_INFO(vif_tmp->ndev, GENERIC_DBG,
				   "Abort scan from disconnect. state [%d]\n",
				   hif_drv_tmp->hif_state);
			del_timer(&hif_drv_tmp->scan_timer);
			handle_scan_done(vif_tmp, SCAN_EVENT_ABORTED);
		}
	}
	srcu_read_unlock(&vif->wilc->srcu, srcu_idx);

	wid.id = WID_DISCONNECT;
	wid.type = WID_CHAR;
	wid.val = (s8 *)&dummy_reason_code;
	wid.size = sizeof(char);

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Sending disconnect request\n");

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result) {
		netdev_err(vif->ndev, "Failed to send disconnect\n");
		return result;
	}

	scan_req = &hif_drv->usr_scan_req;
	conn_info = &hif_drv->conn_info;

	if (scan_req->scan_result) {
		del_timer(&hif_drv->scan_timer);
		scan_req->scan_result(SCAN_EVENT_ABORTED, NULL, scan_req->arg);
		scan_req->scan_result = NULL;
	}

	if (conn_info->conn_result) {
		if (hif_drv->hif_state == HOST_IF_WAITING_CONN_RESP ||
		    hif_drv->hif_state == HOST_IF_EXTERNAL_AUTH) {
			PRINT_INFO(vif->ndev, HOSTINF_DBG,
				   "supplicant requested disconnection\n");
			del_timer(&hif_drv->connect_timer);
			conn_info->conn_result(CONN_DISCONN_EVENT_CONN_RESP,
					       WILC_MAC_STATUS_DISCONNECTED,
					       conn_info->arg);

		} else if (hif_drv->hif_state == HOST_IF_CONNECTED) {
			conn_info->conn_result(CONN_DISCONN_EVENT_DISCONN_NOTIF,
					       WILC_MAC_STATUS_DISCONNECTED,
					       conn_info->arg);
		}
	} else {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
	}

	hif_drv->hif_state = HOST_IF_IDLE;

	eth_zero_addr(hif_drv->assoc_bssid);

	conn_info->req_ies_len = 0;
	kfree(conn_info->req_ies);
	conn_info->req_ies = NULL;
	conn_info->conn_result = NULL;

	return 0;
}

int wilc_get_statistics(struct wilc_vif *vif, struct rf_info *stats)
{
	struct wid wid_list[5];
	u32 wid_cnt = 0, result;

	wid_list[wid_cnt].id = WID_LINKSPEED;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&stats->link_speed;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_RSSI;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&stats->rssi;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_SUCCESS_FRAME_COUNT;
	wid_list[wid_cnt].type = WID_INT;
	wid_list[wid_cnt].size = sizeof(u32);
	wid_list[wid_cnt].val = (s8 *)&stats->tx_cnt;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_RECEIVED_FRAGMENT_COUNT;
	wid_list[wid_cnt].type = WID_INT;
	wid_list[wid_cnt].size = sizeof(u32);
	wid_list[wid_cnt].val = (s8 *)&stats->rx_cnt;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_FAILED_COUNT;
	wid_list[wid_cnt].type = WID_INT;
	wid_list[wid_cnt].size = sizeof(u32);
	wid_list[wid_cnt].val = (s8 *)&stats->tx_fail_cnt;
	wid_cnt++;

	result = wilc_send_config_pkt(vif, WILC_GET_CFG, wid_list, wid_cnt);
	if (result) {
		netdev_err(vif->ndev, "Failed to send scan parameters\n");
		return result;
	}

	if (stats->link_speed > TCP_ACK_FILTER_LINK_SPEED_THRESH &&
	    stats->link_speed != DEFAULT_LINK_SPEED) {
		PRINT_INFO(vif->ndev, HOSTINF_DBG, "Enable TCP filter\n");
		wilc_enable_tcp_ack_filter(vif, true);
	} else if (stats->link_speed != DEFAULT_LINK_SPEED) {
		PRINT_INFO(vif->ndev, HOSTINF_DBG, "Disable TCP filter %d\n",
			   stats->link_speed);
		wilc_enable_tcp_ack_filter(vif, false);
	}

	return result;
}

static void handle_get_statistics(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct rf_info *stats = (struct rf_info *)msg->body.data;

	wilc_get_statistics(vif, stats);

	kfree(msg);
}

static void wilc_hif_pack_sta_param(u8 *cur_byte, struct add_sta_param *params)
{
	ether_addr_copy(cur_byte, params->bssid);
	cur_byte += ETH_ALEN;

	put_unaligned_le16(params->aid, cur_byte);
	cur_byte += 2;

	*cur_byte++ = params->supported_rates_len;
	if (params->supported_rates_len > 0)
		memcpy(cur_byte, params->supported_rates,
		       params->supported_rates_len);
	cur_byte += params->supported_rates_len;

	if (params->ht_supported) {
		*cur_byte++ = true;
		memcpy(cur_byte, &params->ht_capa,
		       sizeof(struct ieee80211_ht_cap));
	} else {
		*cur_byte++ = false;
	}
	cur_byte += sizeof(struct ieee80211_ht_cap);

	put_unaligned_le16(params->flags_mask, cur_byte);
	cur_byte += 2;
	put_unaligned_le16(params->flags_set, cur_byte);
}

static int handle_remain_on_chan(struct wilc_vif *vif,
				 struct wilc_remain_ch *hif_remain_ch)
{
	int result;
	u8 remain_on_chan_flag;
	struct wid wid;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_vif *vif_tmp;
	int srcu_idx;

	if (!hif_drv) {
		PRINT_ER(vif->ndev, "Driver is null\n");
		return -EFAULT;
	}

	srcu_idx = srcu_read_lock(&vif->wilc->srcu);
	list_for_each_entry_rcu(vif_tmp, &vif->wilc->vif_list, list) {
		struct host_if_drv *hif_drv_tmp;

		if (vif_tmp == NULL || vif_tmp->hif_drv == NULL)
			continue;

		hif_drv_tmp = vif_tmp->hif_drv;

		if (hif_drv_tmp->hif_state == HOST_IF_SCANNING) {
			PRINT_INFO(vif_tmp->ndev, GENERIC_DBG,
				   "IFC busy scanning. WLAN_IFC state %d\n",
				   hif_drv_tmp->hif_state);
			srcu_read_unlock(&vif->wilc->srcu, srcu_idx);
			return -EBUSY;
		} else if (hif_drv_tmp->hif_state != HOST_IF_IDLE &&
			   hif_drv_tmp->hif_state != HOST_IF_CONNECTED) {
			PRINT_INFO(vif_tmp->ndev, GENERIC_DBG,
				   "IFC busy connecting. WLAN_IFC %d\n",
				   hif_drv_tmp->hif_state);
			srcu_read_unlock(&vif->wilc->srcu, srcu_idx);
			return -EBUSY;
		}
	}
	srcu_read_unlock(&vif->wilc->srcu, srcu_idx);

	if (vif->connecting) {
		PRINT_INFO(vif->ndev, GENERIC_DBG,
			   "Don't do scan in (CONNECTING) state\n");
		return -EBUSY;
	}

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting channel [%d] duration[%d] [%llu]\n",
		   hif_remain_ch->ch, hif_remain_ch->duration,
		   hif_remain_ch->cookie);
	remain_on_chan_flag = true;
	wid.id = WID_REMAIN_ON_CHAN;
	wid.type = WID_STR;
	wid.size = 2;
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	wid.val[0] = remain_on_chan_flag;
	wid.val[1] = (s8)hif_remain_ch->ch;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	kfree(wid.val);
	if (result) {
		PRINT_ER(vif->ndev, "Failed to set remain on channel\n");
		return -EBUSY;
	}

	hif_drv->remain_on_ch.arg = hif_remain_ch->arg;
	hif_drv->remain_on_ch.expired = hif_remain_ch->expired;
	hif_drv->remain_on_ch.ch = hif_remain_ch->ch;
	hif_drv->remain_on_ch.cookie = hif_remain_ch->cookie;
	hif_drv->hif_state = HOST_IF_P2P_LISTEN;

	hif_drv->remain_on_ch_timer_vif = vif;

	return result;
}

static int wilc_handle_roc_expired(struct wilc_vif *vif, u64 cookie)
{
	u8 remain_on_chan_flag;
	struct wid wid;
	int result;
	struct host_if_drv *hif_drv = vif->hif_drv;
	u8 null_bssid[6] = {0};

	if (hif_drv->hif_state == HOST_IF_P2P_LISTEN) {
		remain_on_chan_flag = false;
		wid.id = WID_REMAIN_ON_CHAN;
		wid.type = WID_STR;
		wid.size = 2;

		wid.val = kmalloc(wid.size, GFP_KERNEL);
		if (!wid.val) {
			PRINT_ER(vif->ndev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		wid.val[0] = remain_on_chan_flag;
		wid.val[1] = WILC_FALSE_FRMWR_CHANNEL;

		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
		kfree(wid.val);
		if (result != 0) {
			netdev_err(vif->ndev, "Failed to set remain channel\n");
			return -EINVAL;
		}

		if (hif_drv->remain_on_ch.expired)
			hif_drv->remain_on_ch.expired(hif_drv->remain_on_ch.arg,
						      cookie);

		if (memcmp(hif_drv->assoc_bssid, null_bssid, ETH_ALEN) == 0)
			hif_drv->hif_state = HOST_IF_IDLE;
		else
			hif_drv->hif_state = HOST_IF_CONNECTED;
	} else {
		netdev_dbg(vif->ndev, "Not in listen state\n");
	}

	return 0;
}

static void wilc_handle_listen_state_expired(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct wilc_remain_ch *hif_remain_ch = &msg->body.remain_on_ch;

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "CANCEL REMAIN ON CHAN\n");

	wilc_handle_roc_expired(vif, hif_remain_ch->cookie);

	kfree(msg);
}

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
static void listen_timer_cb(struct timer_list *t)
#else
static void listen_timer_cb(unsigned long arg)
#endif
{
#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	struct host_if_drv *hif_drv = from_timer(hif_drv, t,
						      remain_on_ch_timer);
#else
	struct host_if_drv *hif_drv = (struct host_if_drv *)arg;
#endif
	struct wilc_vif *vif = hif_drv->remain_on_ch_timer_vif;
	int result;
	struct host_if_msg *msg;

	del_timer(&vif->hif_drv->remain_on_ch_timer);

	msg = wilc_alloc_work(vif, wilc_handle_listen_state_expired, false);
	if (IS_ERR(msg))
		return;

	msg->body.remain_on_ch.cookie = vif->hif_drv->remain_on_ch.cookie;

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
	}
}

static void handle_set_mcast_filter(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct wilc_set_multicast *set_mc = &msg->body.mc_info;
	int result;
	struct wid wid;
	u8 *cur_byte;

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Setup Multicast Filter\n");

	wid.id = WID_SETUP_MULTICAST_FILTER;
	wid.type = WID_BIN;
	wid.size = sizeof(struct wilc_set_multicast) + (set_mc->cnt * ETH_ALEN);
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		goto error;

	cur_byte = wid.val;
	put_unaligned_le32(set_mc->enabled, cur_byte);
	cur_byte += 4;

	put_unaligned_le32(set_mc->cnt, cur_byte);
	cur_byte += 4;

	if (set_mc->cnt > 0 && set_mc->mc_list)
		memcpy(cur_byte, set_mc->mc_list, set_mc->cnt * ETH_ALEN);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send setup multicast\n");

error:
	kfree(set_mc->mc_list);
	kfree(wid.val);
	kfree(msg);
}

void wilc_set_wowlan_trigger(struct wilc_vif *vif, bool enabled)
{
	int ret;
	struct wid wid;
	u8 wowlan_trigger = 0;

	if (enabled)
		wowlan_trigger = 1;

	wid.id = WID_WOWLAN_TRIGGER;
	wid.type = WID_CHAR;
	wid.val = &wowlan_trigger;
	wid.size = sizeof(char);

	ret = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (ret)
		PRINT_ER(vif->ndev,
			 "Failed to send wowlan trigger config packet\n");
}

int wilc_set_external_auth_param(struct wilc_vif *vif,
				 struct cfg80211_external_auth_params *auth)
{
	int ret;
	struct wid wid;
	struct wilc_external_auth_param *param;

	wid.id = WID_EXTERNAL_AUTH_PARAM;
	wid.type = WID_BIN_DATA;
	wid.size = sizeof(*param);
	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		return -EINVAL;
	wid.val = (u8 *)param;
	param->action = auth->action;
	ether_addr_copy(param->bssid, auth->bssid);
	memcpy(param->ssid, auth->ssid.ssid, auth->ssid.ssid_len);
	param->ssid_len = auth->ssid.ssid_len;
	ret = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (ret)
		PRINT_ER(vif->ndev, "failed to set external auth param\n");

	kfree(param);
	return ret;
}

static void handle_scan_timer(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	int ret;

	PRINT_INFO(msg->vif->ndev, HOSTINF_DBG, "handling scan timer\n");
	ret = handle_scan_done(msg->vif, SCAN_EVENT_ABORTED);
	if (ret)
		PRINT_ER(msg->vif->ndev, "Failed to handle scan done\n");
	kfree(msg);
}

static void handle_scan_complete(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);

	del_timer(&msg->vif->hif_drv->scan_timer);
	PRINT_INFO(msg->vif->ndev, HOSTINF_DBG, "scan completed\n");

	handle_scan_done(msg->vif, SCAN_EVENT_DONE);

	kfree(msg);
}

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
static void timer_scan_cb(struct timer_list *t)
#else
static void timer_scan_cb(unsigned long arg)
#endif
{
#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	struct host_if_drv *hif_drv = from_timer(hif_drv, t, scan_timer);
#else
	struct host_if_drv *hif_drv = (struct host_if_drv *)arg;
#endif
	struct wilc_vif *vif = hif_drv->scan_timer_vif;
	struct host_if_msg *msg;
	int result;

	msg = wilc_alloc_work(vif, handle_scan_timer, false);
	if (IS_ERR(msg))
		return;

	result = wilc_enqueue_work(msg);
	if (result)
		kfree(msg);
}

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
static void timer_connect_cb(struct timer_list *t)
#else
static void timer_connect_cb(unsigned long arg)
#endif
{
#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	struct host_if_drv *hif_drv = from_timer(hif_drv, t, connect_timer);
#else
	struct host_if_drv *hif_drv = (struct host_if_drv *)arg;
#endif
	struct wilc_vif *vif = hif_drv->connect_timer_vif;
	struct host_if_msg *msg;
	int result;

	msg = wilc_alloc_work(vif, handle_connect_timeout, false);
	if (IS_ERR(msg))
		return;

	result = wilc_enqueue_work(msg);
	if (result)
		kfree(msg);
}

signed int wilc_send_buffered_eap(struct wilc_vif *vif,
				  void (*deliver_to_stack)(struct wilc_vif *,
							   u8 *, u32, u32, u8),
				  void (*eap_buf_param)(void *), u8 *buff,
				  unsigned int size, unsigned int pkt_offset,
				  void *user_arg)
{
	int result;
	struct host_if_msg *msg;

	if (!vif || !deliver_to_stack || !eap_buf_param)
		return -EFAULT;

	msg = wilc_alloc_work(vif, handle_send_buffered_eap, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);
	msg->body.send_buff_eap.deliver_to_stack = deliver_to_stack;
	msg->body.send_buff_eap.eap_buf_param = eap_buf_param;
	msg->body.send_buff_eap.size = size;
	msg->body.send_buff_eap.pkt_offset = pkt_offset;
	msg->body.send_buff_eap.buff = kmalloc(size + pkt_offset,
					       GFP_ATOMIC);
	memcpy(msg->body.send_buff_eap.buff, buff, size + pkt_offset);
	msg->body.send_buff_eap.user_arg = user_arg;

	result = wilc_enqueue_work(msg);
	if (result) {
		PRINT_ER(vif->ndev, "enqueue work failed\n");
		kfree(msg->body.send_buff_eap.buff);
		kfree(msg);
	}
	return result;
}

int wilc_add_ptk(struct wilc_vif *vif, const u8 *ptk, u8 ptk_key_len,
		 const u8 *mac_addr, const u8 *rx_mic, const u8 *tx_mic,
		 u8 mode, u8 cipher_mode, u8 index)
{
	int result = 0;
	u8 t_key_len  = ptk_key_len + WILC_RX_MIC_KEY_LEN + WILC_TX_MIC_KEY_LEN;

	if (mode == WILC_AP_MODE) {
		struct wid wid_list[2];
		struct wilc_ap_wpa_ptk *key_buf;

		wid_list[0].id = WID_11I_MODE;
		wid_list[0].type = WID_CHAR;
		wid_list[0].size = sizeof(char);
		wid_list[0].val = (s8 *)&cipher_mode;

		key_buf = kzalloc(sizeof(*key_buf) + t_key_len, GFP_KERNEL);
		if (!key_buf) {
			PRINT_ER(vif->ndev,
				 "NO buffer to keep Key buffer - AP\n");
			return -ENOMEM;
		}
		ether_addr_copy(key_buf->mac_addr, mac_addr);
		key_buf->index = index;
		key_buf->key_len = t_key_len;
		memcpy(&key_buf->key[0], ptk, ptk_key_len);

		if (rx_mic)
			memcpy(&key_buf->key[ptk_key_len], rx_mic,
			       WILC_RX_MIC_KEY_LEN);

		if (tx_mic)
			memcpy(&key_buf->key[ptk_key_len + WILC_RX_MIC_KEY_LEN],
			       tx_mic, WILC_TX_MIC_KEY_LEN);

		wid_list[1].id = WID_ADD_PTK;
		wid_list[1].type = WID_STR;
		wid_list[1].size = sizeof(*key_buf) + t_key_len;
		wid_list[1].val = (u8 *)key_buf;
		result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list,
					      ARRAY_SIZE(wid_list));
		kfree(key_buf);
	} else if (mode == WILC_STATION_MODE) {
		struct wid wid;
		struct wilc_sta_wpa_ptk *key_buf;

		key_buf = kzalloc(sizeof(*key_buf) + t_key_len, GFP_KERNEL);
		if (!key_buf) {
			PRINT_ER(vif->ndev,
				 "No buffer to keep Key buffer - Station\n");
			return -ENOMEM;
		}

		ether_addr_copy(key_buf->mac_addr, mac_addr);
		key_buf->key_len = t_key_len;
		memcpy(&key_buf->key[0], ptk, ptk_key_len);

		if (rx_mic)
			memcpy(&key_buf->key[ptk_key_len], rx_mic,
			       WILC_RX_MIC_KEY_LEN);

		if (tx_mic)
			memcpy(&key_buf->key[ptk_key_len + WILC_RX_MIC_KEY_LEN],
			       tx_mic, WILC_TX_MIC_KEY_LEN);

		wid.id = WID_ADD_PTK;
		wid.type = WID_STR;
		wid.size = sizeof(*key_buf) + t_key_len;
		wid.val = (s8 *)key_buf;
		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
		kfree(key_buf);
	}

	return result;
}

int wilc_add_igtk(struct wilc_vif *vif, const u8 *igtk, u8 igtk_key_len,
		  const u8 *pn, u8 pn_len, const u8 *mac_addr, u8 mode, u8 index)
{
	int result = 0;
	u8 t_key_len = igtk_key_len;
	struct wid wid;
	struct wilc_wpa_igtk *key_buf;

	key_buf = kzalloc(sizeof(*key_buf) + t_key_len, GFP_KERNEL);
	if (!key_buf) {
		PRINT_ER(vif->ndev, "No buffer to keep Key buffer - Station\n");
		return -ENOMEM;
	}

	key_buf->index = index;

	memcpy(&key_buf->pn[0], pn, pn_len);
	key_buf->pn_len = pn_len;

	memcpy(&key_buf->key[0], igtk, igtk_key_len);
	key_buf->key_len = t_key_len;

	wid.id = WID_ADD_IGTK;
	wid.type = WID_STR;
	wid.size = sizeof(*key_buf) + t_key_len;
	wid.val = (s8 *)key_buf;
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	kfree(key_buf);
	return result;
}

int wilc_add_rx_gtk(struct wilc_vif *vif, const u8 *rx_gtk, u8 gtk_key_len,
		    u8 index, u32 key_rsc_len, const u8 *key_rsc,
		    const u8 *rx_mic, const u8 *tx_mic, u8 mode,
		    u8 cipher_mode)
{
	int result = 0;
	struct wilc_gtk_key *gtk_key;
	int t_key_len = gtk_key_len + WILC_RX_MIC_KEY_LEN + WILC_TX_MIC_KEY_LEN;

	gtk_key = kzalloc(sizeof(*gtk_key) + t_key_len, GFP_KERNEL);
	if (!gtk_key) {
		PRINT_ER(vif->ndev, "No buffer to send GTK Key\n");
		return -ENOMEM;
	}

	/* fill bssid value only in station mode */
	if (mode == WILC_STATION_MODE &&
	    vif->hif_drv->hif_state == HOST_IF_CONNECTED)
		memcpy(gtk_key->mac_addr, vif->hif_drv->assoc_bssid, ETH_ALEN);

	if (key_rsc)
		memcpy(gtk_key->rsc, key_rsc, 8);
	gtk_key->index = index;
	gtk_key->key_len = t_key_len;
	memcpy(&gtk_key->key[0], rx_gtk, gtk_key_len);

	if (rx_mic)
		memcpy(&gtk_key->key[gtk_key_len], rx_mic, WILC_RX_MIC_KEY_LEN);

	if (tx_mic)
		memcpy(&gtk_key->key[gtk_key_len + WILC_RX_MIC_KEY_LEN],
		       tx_mic, WILC_TX_MIC_KEY_LEN);

	if (mode == WILC_AP_MODE) {
		struct wid wid_list[2];

		wid_list[0].id = WID_11I_MODE;
		wid_list[0].type = WID_CHAR;
		wid_list[0].size = sizeof(char);
		wid_list[0].val = (s8 *)&cipher_mode;

		wid_list[1].id = WID_ADD_RX_GTK;
		wid_list[1].type = WID_STR;
		wid_list[1].size = sizeof(*gtk_key) + t_key_len;
		wid_list[1].val = (u8 *)gtk_key;

		result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list,
					      ARRAY_SIZE(wid_list));
	} else if (mode == WILC_STATION_MODE) {
		struct wid wid;

		wid.id = WID_ADD_RX_GTK;
		wid.type = WID_STR;
		wid.size = sizeof(*gtk_key) + t_key_len;
		wid.val = (u8 *)gtk_key;
		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	}

	kfree(gtk_key);
	return result;
}

int wilc_set_pmkid_info(struct wilc_vif *vif, struct wilc_pmkid_attr *pmkid)
{
	struct wid wid;

	wid.id = WID_PMKID_INFO;
	wid.type = WID_STR;
	wid.size = (pmkid->numpmkid * sizeof(struct wilc_pmkid)) + 1;
	wid.val = (u8 *)pmkid;

	return wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
}

int wilc_get_mac_address(struct wilc_vif *vif, u8 *mac_addr)
{
	int result;
	struct wid wid;

	wid.id = WID_MAC_ADDR;
	wid.type = WID_STR;
	wid.size = ETH_ALEN;
	wid.val = mac_addr;

	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to get mac address\n");

	return result;
}

int wilc_set_mac_address(struct wilc_vif *vif, u8 *mac_addr)
{
	struct wid wid;
	int result;

	wid.id = WID_MAC_ADDR;
	wid.type = WID_STR;
	wid.size = ETH_ALEN;
	wid.val = mac_addr;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		PRINT_ER(vif->ndev, "Failed to set mac address\n");

	return result;
}

int wilc_set_join_req(struct wilc_vif *vif, u8 *bssid, const u8 *ies,
		      size_t ies_len)
{
	int result;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_conn_info *conn_info = &hif_drv->conn_info;

	if (bssid)
		ether_addr_copy(conn_info->bssid, bssid);

	if (ies) {
		conn_info->req_ies_len = ies_len;
		conn_info->req_ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!conn_info->req_ies)
			return -ENOMEM;
	}

	result = wilc_send_connect_wid(vif);
	if (result) {
		PRINT_ER(vif->ndev, "Failed to send connect wid\n");
		goto free_ies;
	}

#if KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE
	hif_drv->connect_timer.data = (unsigned long)hif_drv;
#endif
	hif_drv->connect_timer_vif = vif;
	mod_timer(&hif_drv->connect_timer,
		  jiffies + msecs_to_jiffies(WILC_HIF_CONNECT_TIMEOUT_MS));

	return 0;

free_ies:
	kfree(conn_info->req_ies);

	return result;
}

int wilc_set_mac_chnl_num(struct wilc_vif *vif, u8 channel)
{
	struct wid wid;
	int result;

	wid.id = WID_CURRENT_CHANNEL;
	wid.type = WID_CHAR;
	wid.size = sizeof(char);
	wid.val = &channel;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to set channel\n");

	return result;
}

int wilc_set_operation_mode(struct wilc_vif *vif, int index, u8 mode,
			    u8 ifc_id)
{
	struct wid wid;
	int result;
	struct wilc_drv_handler drv;

	wid.id = WID_SET_OPERATION_MODE;
	wid.type = WID_STR;
	wid.size = sizeof(drv);
	wid.val = (u8 *)&drv;

	drv.handler = cpu_to_le32(index);
	drv.mode = (ifc_id | (mode << 1));

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to set driver handler\n");

	return result;
}

s32 wilc_get_inactive_time(struct wilc_vif *vif, const u8 *mac, u32 *out_val)
{
	struct wid wid;
	s32 result;

	wid.id = WID_SET_STA_MAC_INACTIVE_TIME;
	wid.type = WID_STR;
	wid.size = ETH_ALEN;
	wid.val = kzalloc(wid.size, GFP_KERNEL);
	if (!wid.val) {
		PRINT_ER(vif->ndev, "Failed to allocate buffer\n");
		return -ENOMEM;
	}

	ether_addr_copy(wid.val, mac);
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	kfree(wid.val);
	if (result) {
		netdev_err(vif->ndev, "Failed to set inactive mac\n");
		return result;
	}

	wid.id = WID_GET_INACTIVE_TIME;
	wid.type = WID_INT;
	wid.val = (s8 *)out_val;
	wid.size = sizeof(u32);
	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to get inactive time\n");

	PRINT_INFO(vif->ndev, CFG80211_DBG, "Getting inactive time : %d\n",
		   *out_val);

	return result;
}

int wilc_get_rssi(struct wilc_vif *vif, s8 *rssi_level)
{
	struct wid wid;
	int result;

	if (!rssi_level) {
		netdev_err(vif->ndev, "%s: RSSI level is NULL\n", __func__);
		return -EFAULT;
	}

	wid.id = WID_RSSI;
	wid.type = WID_CHAR;
	wid.size = sizeof(char);
	wid.val = rssi_level;
	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to get RSSI value\n");

	return result;
}

static int wilc_get_stats_async(struct wilc_vif *vif, struct rf_info *stats)
{
	int result;
	struct host_if_msg *msg;

	PRINT_INFO(vif->ndev, HOSTINF_DBG, " getting async statistics\n");
	msg = wilc_alloc_work(vif, handle_get_statistics, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->body.data = (char *)stats;

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
		return result;
	}

	return result;
}

int wilc_hif_set_cfg(struct wilc_vif *vif, struct cfg_param_attr *param)
{
	struct wid wid_list[4];
	int i = 0;

	if (param->flag & WILC_CFG_PARAM_RETRY_SHORT) {
		wid_list[i].id = WID_SHORT_RETRY_LIMIT;
		wid_list[i].val = (s8 *)&param->short_retry_limit;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}
	if (param->flag & WILC_CFG_PARAM_RETRY_LONG) {
		wid_list[i].id = WID_LONG_RETRY_LIMIT;
		wid_list[i].val = (s8 *)&param->long_retry_limit;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}
	if (param->flag & WILC_CFG_PARAM_FRAG_THRESHOLD) {
		wid_list[i].id = WID_FRAG_THRESHOLD;
		wid_list[i].val = (s8 *)&param->frag_threshold;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}
	if (param->flag & WILC_CFG_PARAM_RTS_THRESHOLD) {
		wid_list[i].id = WID_RTS_THRESHOLD;
		wid_list[i].val = (s8 *)&param->rts_threshold;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}

	return wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list, i);
}

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
static void get_periodic_rssi(struct timer_list *t)
#else
static void get_periodic_rssi(unsigned long arg)
#endif
{
#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	struct wilc_vif *vif = from_timer(vif, t, periodic_rssi);
#else
	struct wilc_vif *vif = (struct wilc_vif *)arg;
#endif

	if (!vif->hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL", __func__);
		return;
	}

	if (vif->hif_drv->hif_state == HOST_IF_CONNECTED)
		wilc_get_stats_async(vif, &vif->periodic_stat);

	mod_timer(&vif->periodic_rssi, jiffies + msecs_to_jiffies(5000));
}

int wilc_init(struct net_device *dev, struct host_if_drv **hif_drv_handler)
{
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif = netdev_priv(dev);

	hif_drv = kzalloc(sizeof(*hif_drv), GFP_KERNEL);
	if (!hif_drv) {
		PRINT_ER(dev, "hif driver is NULL\n");
		return -ENOMEM;
	}
	*hif_drv_handler = hif_drv;
	vif->hif_drv = hif_drv;

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	timer_setup(&hif_drv->scan_timer, timer_scan_cb, 0);
	timer_setup(&hif_drv->connect_timer, timer_connect_cb, 0);
	timer_setup(&hif_drv->remain_on_ch_timer, listen_timer_cb, 0);
	timer_setup(&vif->periodic_rssi, get_periodic_rssi, 0);
#else
	setup_timer(&hif_drv->scan_timer, timer_scan_cb, 0);
	setup_timer(&hif_drv->connect_timer, timer_connect_cb, 0);
	setup_timer(&hif_drv->remain_on_ch_timer, listen_timer_cb, 0);
	setup_timer(&vif->periodic_rssi, get_periodic_rssi,
		    (unsigned long)vif);
#endif
	mod_timer(&vif->periodic_rssi, jiffies + msecs_to_jiffies(5000));

	hif_drv->hif_state = HOST_IF_IDLE;

	hif_drv->p2p_timeout = 0;

	return 0;
}

int wilc_deinit(struct wilc_vif *vif)
{
	int result = 0;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL", __func__);
		return -EFAULT;
	}

	mutex_lock(&vif->wilc->deinit_lock);

	del_timer_sync(&hif_drv->scan_timer);
	del_timer_sync(&hif_drv->connect_timer);
	del_timer_sync(&vif->periodic_rssi);
	del_timer_sync(&hif_drv->remain_on_ch_timer);

	if (hif_drv->usr_scan_req.scan_result) {
		hif_drv->usr_scan_req.scan_result(SCAN_EVENT_ABORTED, NULL,
						  hif_drv->usr_scan_req.arg);
		hif_drv->usr_scan_req.scan_result = NULL;
	}

	hif_drv->hif_state = HOST_IF_IDLE;

	kfree(hif_drv);
	vif->hif_drv = NULL;
	mutex_unlock(&vif->wilc->deinit_lock);
	return result;
}

void wilc_network_info_received(struct wilc *wilc, u8 *buffer, u32 length)
{
	int result;
	struct host_if_msg *msg;
	int id;
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif;
	int srcu_idx;

	id = get_unaligned_le32(&buffer[length - 4]);
	srcu_idx = srcu_read_lock(&wilc->srcu);
	vif = wilc_get_vif_from_idx(wilc, id);
	if (!vif)
		goto out;

	hif_drv = vif->hif_drv;
	if (!hif_drv) {
		netdev_err(vif->ndev, "driver not init[%p]\n", hif_drv);
		goto out;
	}

	msg = wilc_alloc_work(vif, handle_rcvd_ntwrk_info, false);
	if (IS_ERR(msg))
		goto out;

	msg->body.net_info.frame_len = get_unaligned_le16(&buffer[6]) - 1;
	msg->body.net_info.rssi = buffer[8];
	msg->body.net_info.mgmt = kmemdup(&buffer[9],
					  msg->body.net_info.frame_len,
					  GFP_KERNEL);
	if (!msg->body.net_info.mgmt) {
		kfree(msg);
		goto out;
	}

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg->body.net_info.mgmt);
		kfree(msg);
	}
out:
	srcu_read_unlock(&wilc->srcu, srcu_idx);
}

void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *buffer, u32 length)
{
	int result;
	struct host_if_msg *msg;
	int id;
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif;
	int srcu_idx;

	mutex_lock(&wilc->deinit_lock);

	id = get_unaligned_le32(&buffer[length - 4]);
	srcu_idx = srcu_read_lock(&wilc->srcu);
	vif = wilc_get_vif_from_idx(wilc, id);
	if (!vif)
		goto out;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "General asynchronous info packet received\n");

	hif_drv = vif->hif_drv;

	if (!hif_drv) {
		PRINT_ER(vif->ndev, "hif driver is NULL\n");
		goto out;
	}

	if (!hif_drv->conn_info.conn_result) {
		PRINT_ER(vif->ndev, "there is no current Connect Request\n");
		goto out;
	}

	msg = wilc_alloc_work(vif, handle_rcvd_gnrl_async_info, false);
	if (IS_ERR(msg))
		goto out;

	msg->body.mac_info.status = buffer[7];
	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Received MAC status= %d Reason= %d Info = %d\n",
		   buffer[7], buffer[8], buffer[9]);
	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
	}
out:
	mutex_unlock(&wilc->deinit_lock);
	srcu_read_unlock(&wilc->srcu, srcu_idx);
}

void wilc_scan_complete_received(struct wilc *wilc, u8 *buffer, u32 length)
{
	int result;
	int id;
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif;
	int srcu_idx;

	id = get_unaligned_le32(&buffer[length - 4]);
	srcu_idx = srcu_read_lock(&wilc->srcu);
	vif = wilc_get_vif_from_idx(wilc, id);
	if (!vif)
		goto out;

	PRINT_INFO(vif->ndev, GENERIC_DBG, "Scan notification received\n");

	hif_drv = vif->hif_drv;
	if (!hif_drv) {
		PRINT_ER(vif->ndev, "hif driver is NULL\n");
		goto out;
	}

	if (hif_drv->usr_scan_req.scan_result) {
		struct host_if_msg *msg;

		msg = wilc_alloc_work(vif, handle_scan_complete, false);
		if (IS_ERR(msg))
			goto out;

		result = wilc_enqueue_work(msg);
		if (result) {
			PRINT_ER(vif->ndev, "enqueue work failed\n");
			kfree(msg);
		}
	}
out:
	srcu_read_unlock(&wilc->srcu, srcu_idx);
}

int wilc_remain_on_channel(struct wilc_vif *vif, u64 cookie,
			   u32 duration, u16 chan,
			   void (*expired)(void *, u64),
			   void *user_arg)
{
	struct wilc_remain_ch roc;
	int result;

	PRINT_INFO(vif->ndev, CFG80211_DBG, "called\n");
	roc.ch = chan;
	roc.expired = expired;
	roc.arg = user_arg;
	roc.duration = duration;
	roc.cookie = cookie;
	result = handle_remain_on_chan(vif, &roc);
	if (result)
		netdev_err(vif->ndev, "%s: failed to set remain on channel\n",
			   __func__);

	return result;
}

int wilc_listen_state_expired(struct wilc_vif *vif, u64 cookie)
{
	if (!vif->hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL", __func__);
		return -EFAULT;
	}

	del_timer(&vif->hif_drv->remain_on_ch_timer);

	return wilc_handle_roc_expired(vif, cookie);
}

void wilc_frame_register(struct wilc_vif *vif, u16 frame_type, bool reg)
{
	struct wid wid;
	int result;
	struct wilc_reg_frame reg_frame;

	wid.id = WID_REGISTER_FRAME;
	wid.type = WID_STR;
	wid.size = sizeof(reg_frame);
	wid.val = (u8 *)&reg_frame;

	memset(&reg_frame, 0x0, sizeof(reg_frame));

	if (reg)
		reg_frame.reg = 1;

	switch (frame_type) {
	case IEEE80211_STYPE_ACTION:
		PRINT_INFO(vif->ndev, HOSTINF_DBG, "ACTION\n");
		reg_frame.reg_id = WILC_FW_ACTION_FRM_IDX;
		break;

	case IEEE80211_STYPE_PROBE_REQ:
		PRINT_INFO(vif->ndev, HOSTINF_DBG, "PROBE REQ\n");
		reg_frame.reg_id = WILC_FW_PROBE_REQ_IDX;
		break;

	case IEEE80211_STYPE_AUTH:
		PRINT_INFO(vif->ndev, HOSTINF_DBG, "AUTH\n");
		reg_frame.reg_id = WILC_FW_AUTH_REQ_IDX;
		break;

	default:
		PRINT_INFO(vif->ndev, HOSTINF_DBG, "Not valid frame type\n");
		break;
	}
	reg_frame.frame_type = cpu_to_le16(frame_type);
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to frame register\n");
}

int wilc_add_beacon(struct wilc_vif *vif, u32 interval, u32 dtim_period,
		    struct cfg80211_beacon_data *params)
{
	struct wid wid;
	int result;
	u8 *cur_byte;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting adding beacon\n");

	wid.id = WID_ADD_BEACON;
	wid.type = WID_BIN;
	wid.size = params->head_len + params->tail_len + 16;
	wid.val = kzalloc(wid.size, GFP_KERNEL);
	if (!wid.val) {
		PRINT_ER(vif->ndev, "Failed to allocate buffer\n");
		return -ENOMEM;
	}

	cur_byte = wid.val;
	put_unaligned_le32(interval, cur_byte);
	cur_byte += 4;
	put_unaligned_le32(dtim_period, cur_byte);
	cur_byte += 4;
	put_unaligned_le32(params->head_len, cur_byte);
	cur_byte += 4;

	if (params->head_len > 0)
		memcpy(cur_byte, params->head, params->head_len);
	cur_byte += params->head_len;

	put_unaligned_le32(params->tail_len, cur_byte);
	cur_byte += 4;

	if (params->tail_len > 0)
		memcpy(cur_byte, params->tail, params->tail_len);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send add beacon\n");

	kfree(wid.val);

	return result;
}

int wilc_del_beacon(struct wilc_vif *vif)
{
	int result;
	struct wid wid;
	u8 del_beacon = 0;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting deleting beacon message queue params\n");

	wid.id = WID_DEL_BEACON;
	wid.type = WID_CHAR;
	wid.size = sizeof(char);
	wid.val = &del_beacon;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send delete beacon\n");

	return result;
}

static void handle_add_station(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct add_sta_param *params = &msg->body.add_sta_info;
	int result;
	struct wid wid;

	wid.id = WID_ADD_STA;
	wid.type = WID_BIN;
	wid.size = WILC_ADD_STA_LENGTH + params->supported_rates_len;
	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Handling add station\n");
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		goto error;

	wilc_hif_pack_sta_param(wid.val, params);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		PRINT_ER(vif->ndev, "Failed to send add station\n");

	kfree(wid.val);
error:
	kfree(params->supported_rates);
	kfree(msg);
}

int wilc_add_station(struct wilc_vif *vif, const u8 *mac,
		     struct station_parameters *params)
{
	int result;
	struct host_if_msg *msg;
	struct add_sta_param *sta_params;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	struct link_station_parameters *link_sta_params = &params->link_sta_params;
	const struct ieee80211_ht_cap *ht_capa = link_sta_params->ht_capa;
	u8 supported_rates_len = link_sta_params->supported_rates_len;
	const u8 *supported_rates = link_sta_params->supported_rates;
#else
	const struct ieee80211_ht_cap *ht_capa = params->ht_capa;
	u8 supported_rates_len = params->supported_rates_len;
	const u8 *supported_rates = params->supported_rates;
#endif

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting adding station message queue params\n");

	msg = wilc_alloc_work(vif, handle_add_station, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	sta_params = &msg->body.add_sta_info;
	memcpy(sta_params->bssid, mac, ETH_ALEN);
	sta_params->aid = params->aid;
	if (!ht_capa) {
		sta_params->ht_supported = false;
	} else {
		sta_params->ht_supported = true;
		memcpy(&sta_params->ht_capa, ht_capa,
		       sizeof(struct ieee80211_ht_cap));
	}
	sta_params->flags_mask = params->sta_flags_mask;
	sta_params->flags_set = params->sta_flags_set;

	sta_params->supported_rates_len = supported_rates_len;
	if (supported_rates_len > 0) {
		sta_params->supported_rates = kmemdup(supported_rates,
					    supported_rates_len,
					    GFP_KERNEL);
		if (!sta_params->supported_rates) {
			kfree(msg);
			return -ENOMEM;
		}
	}

	result = wilc_enqueue_work(msg);
	if (result) {
		PRINT_ER(vif->ndev, "enqueue work failed\n");
		kfree(sta_params->supported_rates);
		kfree(msg);
	}

	return result;
}

int wilc_del_station(struct wilc_vif *vif, const u8 *mac_addr)
{
	struct wid wid;
	int result;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting deleting station message queue params\n");

	wid.id = WID_REMOVE_STA;
	wid.type = WID_BIN;
	wid.size = ETH_ALEN;
	wid.val = kzalloc(wid.size, GFP_KERNEL);
	if (!wid.val) {
		PRINT_ER(vif->ndev, "Failed to allocate buffer\n");
		return -ENOMEM;
	}

	if (!mac_addr)
		eth_broadcast_addr(wid.val);
	else
		ether_addr_copy(wid.val, mac_addr);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to del station\n");

	kfree(wid.val);

	return result;
}

int wilc_del_allstation(struct wilc_vif *vif, u8 mac_addr[][ETH_ALEN])
{
	struct wid wid;
	int result;
	int i;
	u8 assoc_sta = 0;
	struct wilc_del_all_sta del_sta;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting deauthenticating station message queue params\n");
	memset(&del_sta, 0x0, sizeof(del_sta));
	for (i = 0; i < WILC_MAX_NUM_STA; i++) {
		if (!is_zero_ether_addr(mac_addr[i])) {
			PRINT_INFO(vif->ndev,
				   CFG80211_DBG, "BSSID = %x%x%x%x%x%x\n",
				   mac_addr[i][0], mac_addr[i][1],
				   mac_addr[i][2], mac_addr[i][3],
				   mac_addr[i][4], mac_addr[i][5]);
			assoc_sta++;
			ether_addr_copy(del_sta.mac[i], mac_addr[i]);
		}
	}

	if (!assoc_sta) {
		PRINT_INFO(vif->ndev, CFG80211_DBG, "NO ASSOCIATED STAS\n");
		return 0;
	}
	del_sta.assoc_sta = assoc_sta;

	wid.id = WID_DEL_ALL_STA;
	wid.type = WID_STR;
	wid.size = (assoc_sta * ETH_ALEN) + 1;
	wid.val = (u8 *)&del_sta;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send delete all station\n");

	return result;
}

static void handle_edit_station(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct add_sta_param *params = &msg->body.edit_sta_info;
	int result;
	struct wid wid;

	wid.id = WID_EDIT_STA;
	wid.type = WID_BIN;
	wid.size = WILC_ADD_STA_LENGTH + params->supported_rates_len;
	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Handling edit station\n");
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		goto error;

	wilc_hif_pack_sta_param(wid.val, params);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		PRINT_ER(vif->ndev, "Failed to send edit station\n");

	kfree(wid.val);
error:
	kfree(params->supported_rates);
	kfree(msg);
}

int wilc_edit_station(struct wilc_vif *vif, const u8 *mac,
		      struct station_parameters *params)
{
	int result;
	struct host_if_msg *msg;
	struct add_sta_param *sta_params;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	struct link_station_parameters *link_sta_params = &params->link_sta_params;
	const struct ieee80211_ht_cap *ht_capa = link_sta_params->ht_capa;
	u8 supported_rates_len = link_sta_params->supported_rates_len;
	const u8 *supported_rates = link_sta_params->supported_rates;
#else
	const struct ieee80211_ht_cap *ht_capa = params->ht_capa;
	u8 supported_rates_len = params->supported_rates_len;
	const u8 *supported_rates = params->supported_rates;
#endif

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting editing station message queue params\n");

	msg = wilc_alloc_work(vif, handle_edit_station, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	sta_params = &msg->body.edit_sta_info;
	memcpy(sta_params->bssid, mac, ETH_ALEN);
	sta_params->aid = params->aid;
	if (!ht_capa) {
		sta_params->ht_supported = false;
	} else {
		sta_params->ht_supported = true;
		memcpy(&sta_params->ht_capa, ht_capa,
		       sizeof(struct ieee80211_ht_cap));
	}
	sta_params->flags_mask = params->sta_flags_mask;
	sta_params->flags_set = params->sta_flags_set;

	sta_params->supported_rates_len = supported_rates_len;
	if (supported_rates_len > 0) {
		sta_params->supported_rates = kmemdup(supported_rates,
					    supported_rates_len,
					    GFP_KERNEL);
		if (!sta_params->supported_rates) {
			kfree(msg);
			return -ENOMEM;
		}
	}

	result = wilc_enqueue_work(msg);
	if (result) {
		PRINT_ER(vif->ndev, "enqueue work failed\n");
		kfree(sta_params->supported_rates);
		kfree(msg);
	}

	return result;
}

static void handle_power_management(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct power_mgmt_param *pm_param = &msg->body.pwr_mgmt_info;
	int result;
	struct wid wid;
	s8 power_mode;

	wid.id = WID_POWER_MANAGEMENT;

	if (pm_param->enabled)
		power_mode = WILC_FW_MIN_FAST_PS;
	else
		power_mode = WILC_FW_NO_POWERSAVE;
	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Handling power mgmt to %d\n",
		   power_mode);
	wid.val = &power_mode;
	wid.size = sizeof(char);

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "Handling Power Management\n");
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		PRINT_ER(vif->ndev, "Failed to send power management\n");

	kfree(msg);
}

int wilc_set_power_mgmt(struct wilc_vif *vif, bool enabled, u32 timeout)
{
	int result;
	struct host_if_msg *msg;

	PRINT_INFO(vif->ndev, HOSTINF_DBG, "\n\n>> Setting PS to %d <<\n\n",
		   enabled);
	msg = wilc_alloc_work(vif, handle_power_management, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->body.pwr_mgmt_info.enabled = enabled;
	msg->body.pwr_mgmt_info.timeout = timeout;

	result = wilc_enqueue_work(msg);
	if (result) {
		PRINT_ER(vif->ndev, "enqueue work failed\n");
		kfree(msg);
	}
	return result;
}

int wilc_setup_multicast_filter(struct wilc_vif *vif, u32 enabled, u32 count,
				u8 *mc_list)
{
	int result;
	struct host_if_msg *msg;

	PRINT_INFO(vif->ndev, HOSTINF_DBG,
		   "Setting Multicast Filter params\n");
	msg = wilc_alloc_work(vif, handle_set_mcast_filter, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->body.mc_info.enabled = enabled;
	msg->body.mc_info.cnt = count;
	msg->body.mc_info.mc_list = mc_list;

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
	}
	return result;
}

int wilc_set_tx_power(struct wilc_vif *vif, u8 tx_power)
{
	struct wid wid;

	wid.id = WID_TX_POWER;
	wid.type = WID_CHAR;
	wid.val = &tx_power;
	wid.size = sizeof(char);

	return wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
}

static void handle_get_tx_pwr(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	u8 *tx_pwr = &msg->body.tx_power.tx_pwr;
	int ret;
	struct wid wid;

	wid.id = WID_TX_POWER;
	wid.type = WID_CHAR;
	wid.val = tx_pwr;
	wid.size = sizeof(char);

	ret = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (ret)
		PRINT_ER(vif->ndev, "Failed to get TX PWR\n");

	complete(&msg->work_comp);
}

int wilc_get_tx_power(struct wilc_vif *vif, u8 *tx_power)
{
	int ret;
	struct host_if_msg *msg;

	msg = wilc_alloc_work(vif, handle_get_tx_pwr, true);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	ret = wilc_enqueue_work(msg);
	if (ret) {
		PRINT_ER(vif->ndev, "enqueue work failed\n");
	} else {
		wait_for_completion(&msg->work_comp);
		*tx_power = msg->body.tx_power.tx_pwr;
	}

	/* free 'msg' after copying data */
	kfree(msg);
	return ret;
}

static bool is_valid_gpio(struct wilc_vif *vif, u8 gpio)
{
	switch (vif->wilc->chip) {
	case WILC_1000:
		if (gpio == 0 || gpio == 1 || gpio == 4 || gpio == 6)
			return true;
		else
			return false;
	case WILC_3000:
		if (gpio == 0 || gpio == 3 || gpio == 4 ||
		    (gpio >= 17 && gpio <= 20))
			return true;
		else
			return false;
	default:
		return false;
	}
}

int wilc_set_antenna(struct wilc_vif *vif, u8 mode)
{
	struct wid wid;
	int ret;
	struct sysfs_attr_group *attr_sysfs_p = &vif->wilc->attr_sysfs;
	struct host_if_set_ant set_ant;

	set_ant.mode = mode;

	if (attr_sysfs_p->ant_swtch_mode == ANT_SWTCH_INVALID_GPIO_CTRL) {
		pr_err("Ant switch GPIO mode is invalid.\n");
		pr_err("Set it using /sys/wilc/ant_swtch_mode\n");
		return -EINVAL;
	}

	if (is_valid_gpio(vif, attr_sysfs_p->antenna1)) {
		set_ant.antenna1 = attr_sysfs_p->antenna1;
	} else  {
		pr_err("Invalid GPIO %d\n", attr_sysfs_p->antenna1);
		return -EINVAL;
	}

	if (attr_sysfs_p->ant_swtch_mode == ANT_SWTCH_DUAL_GPIO_CTRL) {
		if (attr_sysfs_p->antenna2 != attr_sysfs_p->antenna1 &&
		    is_valid_gpio(vif, attr_sysfs_p->antenna2)) {
			set_ant.antenna2 = attr_sysfs_p->antenna2;
		} else {
			pr_err("Invalid GPIO %d\n", attr_sysfs_p->antenna2);
			return -EINVAL;
		}
	}

	set_ant.gpio_mode = attr_sysfs_p->ant_swtch_mode;

	wid.id = WID_ANTENNA_SELECTION;
	wid.type = WID_BIN;
	wid.val = (u8 *)&set_ant;
	wid.size = sizeof(struct host_if_set_ant);
	if (attr_sysfs_p->ant_swtch_mode == ANT_SWTCH_SNGL_GPIO_CTRL)
		PRINT_INFO(vif->ndev, CFG80211_DBG,
			   "set antenna %d on GPIO %d\n", set_ant.mode,
			   set_ant.antenna1);
	else if (attr_sysfs_p->ant_swtch_mode == ANT_SWTCH_DUAL_GPIO_CTRL)
		PRINT_INFO(vif->ndev, CFG80211_DBG,
			   "set antenna %d on GPIOs %d and %d\n",
			   set_ant.mode, set_ant.antenna1,
			   set_ant.antenna2);

	ret = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (ret)
		PRINT_ER(vif->ndev, "Failed to set antenna mode\n");

	return ret;
}
