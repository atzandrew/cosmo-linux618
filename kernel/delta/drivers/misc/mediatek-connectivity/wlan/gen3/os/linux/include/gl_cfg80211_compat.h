/* SPDX-License-Identifier: GPL-2.0 */
/* 6.18 signature adapters for the stock gen3 cfg80211 / netdev ops. */
#ifndef _GL_CFG80211_COMPAT_H
#define _GL_CFG80211_COMPAT_H

#include <net/cfg80211.h>
#include <linux/netdevice.h>

int wcn_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			 u8 key_index, bool pairwise, const u8 *mac_addr,
			 struct key_params *params);
int wcn_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			 u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
			 void (*callback)(void *cookie, struct key_params *));
int wcn_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			 u8 key_index, bool pairwise, const u8 *mac_addr);
int wcn_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
				 u8 key_index, bool unicast, bool multicast);
void wcn_cfg80211_update_mgmt_frame_registrations(struct wiphy *wiphy,
						  struct wireless_dev *wdev,
						  struct mgmt_frame_regs *upd);
int wcn_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev, const u8 *peer,
			   int link_id, u8 action_code, u8 dialog_token, u16 status_code,
			   u32 peer_capability, bool initiator, const u8 *buf, size_t len);

int wcn_p2p_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			     u8 key_index, bool pairwise, const u8 *mac_addr,
			     struct key_params *params);
int wcn_p2p_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			     u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
			     void (*callback)(void *cookie, struct key_params *));
int wcn_p2p_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev, int link_id,
			     u8 key_index, bool pairwise, const u8 *mac_addr);
int wcn_p2p_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *ndev,
				     int link_id, u8 key_index, bool unicast, bool multicast);
int wcn_p2p_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_ap_update *info);
int wcn_p2p_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev,
			     unsigned int link_id);
int wcn_p2p_cfg80211_set_wiphy_params(struct wiphy *wiphy, int radio_idx, u32 changed);
int wcn_p2p_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *dev,
				      unsigned int link_id, const u8 *peer,
				      const struct cfg80211_bitrate_mask *mask);
void wcn_p2p_cfg80211_update_mgmt_frame_registrations(struct wiphy *wiphy,
						      struct wireless_dev *wdev,
						      struct mgmt_frame_regs *upd);
int wcn_p2p_cfg80211_set_txpower(struct wiphy *wiphy, struct wireless_dev *wdev,
				 int radio_idx, enum nl80211_tx_power_setting type, int mbm);
int wcn_p2p_cfg80211_get_txpower(struct wiphy *wiphy, struct wireless_dev *wdev,
				 int radio_idx, unsigned int link_id, int *dbm);

u16 wcn_select_queue(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev);

#endif
