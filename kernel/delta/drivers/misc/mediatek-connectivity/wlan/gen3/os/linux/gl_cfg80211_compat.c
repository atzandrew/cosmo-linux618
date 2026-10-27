// SPDX-License-Identifier: GPL-2.0
/*
 * cfg80211 / netdev op adapters for the stock 4.4 wlan gen3 driver on 6.18.
 *
 * The ops tables (gl_init.c, gl_p2p.c) point here. Each adapter has the 6.18
 * signature and calls the unchanged stock function. The added arguments are
 * not used by this single-link, single-radio device: link_id is always 0 or
 * -1 (no MLO), radio_idx -1 (one radio).
 */

#include "precomp.h"
#include "gl_cfg80211.h"
#include "gl_p2p_ioctl.h"
#include "gl_cfg80211_compat.h"
#include <net/iw_handler.h>

/* mgmt_frame_regs: interface_stypes is indexed by management subtype. */
#define WCN_STYPE_BIT(frame)	BIT(((frame) & IEEE80211_FCTL_STYPE) >> 4)

/* ---- station (wlan0) ---- */

int wcn_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			 u8 key_index, bool pairwise, const u8 *mac_addr,
			 struct key_params *params)
{
	return mtk_cfg80211_add_key(wiphy, ndev, key_index, pairwise, mac_addr, params);
}

int wcn_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			 u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
			 void (*callback)(void *cookie, struct key_params *))
{
	return mtk_cfg80211_get_key(wiphy, ndev, key_index, pairwise, mac_addr, cookie, callback);
}

int wcn_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			 u8 key_index, bool pairwise, const u8 *mac_addr)
{
	return mtk_cfg80211_del_key(wiphy, ndev, key_index, pairwise, mac_addr);
}

int wcn_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
				 u8 key_index, bool unicast, bool multicast)
{
	return mtk_cfg80211_set_default_key(wiphy, ndev, key_index, unicast, multicast);
}

void wcn_cfg80211_update_mgmt_frame_registrations(struct wiphy *wiphy,
						  struct wireless_dev *wdev,
						  struct mgmt_frame_regs *upd)
{
	u32 stypes = upd ? upd->interface_stypes : 0;

	mtk_cfg80211_mgmt_frame_register(wiphy, wdev, MAC_FRAME_PROBE_REQ,
					 !!(stypes & WCN_STYPE_BIT(MAC_FRAME_PROBE_REQ)));
	mtk_cfg80211_mgmt_frame_register(wiphy, wdev, MAC_FRAME_ACTION,
					 !!(stypes & WCN_STYPE_BIT(MAC_FRAME_ACTION)));
}

#if CFG_SUPPORT_TDLS
int wcn_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev, const u8 *peer,
			   int link_id, u8 action_code, u8 dialog_token, u16 status_code,
			   u32 peer_capability, bool initiator, const u8 *buf, size_t len)
{
	return mtk_cfg80211_tdls_mgmt(wiphy, dev, peer, action_code, dialog_token,
				      status_code, peer_capability, initiator, buf, len);
}
#endif

/* ---- P2P ---- */

int wcn_p2p_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			     u8 key_index, bool pairwise, const u8 *mac_addr,
			     struct key_params *params)
{
	return mtk_p2p_cfg80211_add_key(wiphy, ndev, key_index, pairwise, mac_addr, params);
}

int wcn_p2p_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			     u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
			     void (*callback)(void *cookie, struct key_params *))
{
	return mtk_p2p_cfg80211_get_key(wiphy, ndev, key_index, pairwise, mac_addr, cookie,
					callback);
}

int wcn_p2p_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			     u8 key_index, bool pairwise, const u8 *mac_addr)
{
	return mtk_p2p_cfg80211_del_key(wiphy, ndev, key_index, pairwise, mac_addr);
}

int wcn_p2p_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev,
				     int link_id, u8 key_index, bool unicast, bool multicast)
{
	return mtk_p2p_cfg80211_set_default_key(wiphy, ndev, key_index, unicast, multicast);
}

int wcn_p2p_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_ap_update *info)
{
	return mtk_p2p_cfg80211_change_beacon(wiphy, dev, &info->beacon);
}

int wcn_p2p_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev,
			     unsigned int link_id)
{
	return mtk_p2p_cfg80211_stop_ap(wiphy, dev);
}

int wcn_p2p_cfg80211_set_wiphy_params(struct wiphy *wiphy, int radio_idx, u32 changed)
{
	return mtk_p2p_cfg80211_set_wiphy_params(wiphy, changed);
}

int wcn_p2p_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *dev,
				      unsigned int link_id, const u8 *peer,
				      const struct cfg80211_bitrate_mask *mask)
{
	return mtk_p2p_cfg80211_set_bitrate_mask(wiphy, dev, peer, mask);
}

void wcn_p2p_cfg80211_update_mgmt_frame_registrations(struct wiphy *wiphy,
						      struct wireless_dev *wdev,
						      struct mgmt_frame_regs *upd)
{
	u32 stypes = upd ? upd->interface_stypes : 0;

	mtk_p2p_cfg80211_mgmt_frame_register(wiphy, wdev, MAC_FRAME_PROBE_REQ,
					     !!(stypes & WCN_STYPE_BIT(MAC_FRAME_PROBE_REQ)));
	mtk_p2p_cfg80211_mgmt_frame_register(wiphy, wdev, MAC_FRAME_ACTION,
					     !!(stypes & WCN_STYPE_BIT(MAC_FRAME_ACTION)));
}

int wcn_p2p_cfg80211_set_txpower(struct wiphy *wiphy, struct wireless_dev *wdev,
				 int radio_idx, enum nl80211_tx_power_setting type, int mbm)
{
	return mtk_p2p_cfg80211_set_txpower(wiphy, wdev, type, mbm);
}

int wcn_p2p_cfg80211_get_txpower(struct wiphy *wiphy, struct wireless_dev *wdev,
				 int radio_idx, unsigned int link_id, int *dbm)
{
	return mtk_p2p_cfg80211_get_txpower(wiphy, wdev, dbm);
}

/* ---- netdev ---- */

u16 wcn_select_queue(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev)
{
	/* the stock function ignores accel_priv and fallback */
	return wlanSelectQueue(dev, skb, NULL, NULL);
}

/*
 * Wireless-extensions events (legacy Android userspace hooks). The kernel
 * provides wireless_send_event() only with CONFIG_WIRELESS_EXT, which also
 * changes struct net_device for the whole build; keep it off and drop the
 * events here. nl80211 carries the real connect/scan/roam events.
 */
#ifndef CONFIG_WEXT_CORE
void wireless_send_event(struct net_device *dev, unsigned int cmd,
			 union iwreq_data *wrqu, const char *extra)
{
}
#endif
