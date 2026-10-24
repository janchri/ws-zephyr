/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * WiFi hotspot: if no tag is on the box shortly after startup, the box opens
 * an access point (like the Arduino version: "music.Box") with a DHCP
 * server, and the web interface is reachable at http://192.168.4.1.
 * Starting with a tag on the box skips it, so normal playback stays free of
 * WiFi load.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>

#include "controller.h"

LOG_MODULE_DECLARE(ww, CONFIG_WW_LOG_LEVEL);

#define SSID CONFIG_WW_WIFI_AP_SSID
#define PSK  CONFIG_WW_WIFI_AP_PSK

BUILD_ASSERT(sizeof(PSK) == 1 || (sizeof(PSK) - 1 >= 8 && sizeof(PSK) - 1 <= 63),
	     "WW_WIFI_AP_PSK must be empty (open) or 8..63 characters");

static int setup_ip(struct net_if *iface)
{
	struct net_in_addr addr, netmask, pool;

	if (net_addr_pton(NET_AF_INET, CONFIG_WW_WIFI_AP_IP, &addr) ||
	    net_addr_pton(NET_AF_INET, "255.255.255.0", &netmask)) {
		LOG_ERR("Invalid hotspot address %s", CONFIG_WW_WIFI_AP_IP);
		return -EINVAL;
	}

	net_if_ipv4_set_gw(iface, &addr);
	if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == NULL ||
	    !net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask)) {
		LOG_ERR("Cannot set the hotspot address");
		return -EIO;
	}

	/* Clients get addresses from .10 on */
	pool = addr;
	pool.s4_addr[3] = 10;

	return net_dhcpv4_server_start(iface, &pool);
}

static int start_hotspot(void)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)SSID,
		.ssid_length = sizeof(SSID) - 1,
		.psk = (const uint8_t *)PSK,
		.psk_length = sizeof(PSK) - 1,
		.security = (sizeof(PSK) == 1) ? WIFI_SECURITY_TYPE_NONE : WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
	};
	struct net_if *iface = net_if_get_wifi_sap();
	int ret;

	if (iface == NULL) {
		/* Without AP/STA coexistence the single WiFi interface does AP */
		iface = net_if_get_first_wifi();
	}
	if (iface == NULL) {
		LOG_ERR("No WiFi interface");
		return -ENODEV;
	}

	ret = setup_ip(iface);
	if (ret) {
		LOG_ERR("DHCP server start failed: %d", ret);
		return ret;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &params, sizeof(params));
	if (ret) {
		LOG_ERR("Hotspot start failed: %d", ret);
		return ret;
	}

	LOG_INF("Hotspot \"%s\" (%s), web interface on http://%s", SSID,
		sizeof(PSK) == 1 ? "open" : "WPA2", CONFIG_WW_WIFI_AP_IP);

	return 0;
}

static void hotspot_thread(void *p1, void *p2, void *p3)
{
	struct ww_status st;

	/* Started CONFIG_WW_WIFI_AP_DELAY_MS after boot: the tag reader had its chance */
	ww_controller_get_status(&st);
	if (st.tag_present) {
		LOG_INF("Tag on the box at startup, no hotspot");
		return;
	}

	start_hotspot();
}

K_THREAD_DEFINE(wifi_hotspot, 3072, hotspot_thread, NULL, NULL, NULL, 8, 0,
		CONFIG_WW_WIFI_AP_DELAY_MS);
