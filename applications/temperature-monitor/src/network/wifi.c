/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>

#include "credentials.h"

LOG_MODULE_REGISTER(wifi, CONFIG_LOG_DEFAULT_LEVEL);

#define CONFIG_APPLICATION_WIFI_INIT_PRIORITY 91

#define RECONNECT_DELAY K_SECONDS(5)
#define NET_EVENT_WIFI_MASK (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_if *sta_iface;
static struct wifi_connect_req_params sta_config;

static struct net_mgmt_event_callback cb;

static int connect_to_wifi(void);

static void reconnect_work_fn(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(reconnect_work, reconnect_work_fn);

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		LOG_INF("Connected to %s", CONFIG_WIFI_SSID);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		LOG_INF("Disconnected from %s", CONFIG_WIFI_SSID);

        k_work_cancel_delayable(&reconnect_work);
        k_work_schedule(&reconnect_work, RECONNECT_DELAY);
		break;
	}
	default:
		break;
	}
}

static void reconnect_work_fn(struct k_work *work) {
    connect_to_wifi();
}

static int connect_to_wifi(void)
{
	if (!sta_iface) {
		LOG_INF("STA: interface not initialized");
		return -EIO;
	}

	sta_config.ssid = (const uint8_t *)CONFIG_WIFI_SSID;
	sta_config.ssid_length = sizeof(CONFIG_WIFI_SSID) - 1;
	sta_config.psk = (const uint8_t *)CONFIG_WIFI_PSK;
	sta_config.psk_length = sizeof(CONFIG_WIFI_PSK) - 1;
	sta_config.security = WIFI_SECURITY_TYPE_PSK;
	sta_config.channel = WIFI_CHANNEL_ANY;
	sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;

	LOG_INF("Connecting to SSID: %s\n", sta_config.ssid);

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_config,
			   sizeof(struct wifi_connect_req_params));
	if (ret) {
		LOG_ERR("Unable to Connect to (%s)", CONFIG_WIFI_SSID);
	}

	return ret;
}

int wifi_init(void)
{
	net_mgmt_init_event_callback(&cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
	net_mgmt_add_event_callback(&cb);

	sta_iface = net_if_get_wifi_sta();
	connect_to_wifi();

	return 0;
}
SYS_INIT(wifi_init, APPLICATION, CONFIG_APPLICATION_WIFI_INIT_PRIORITY);