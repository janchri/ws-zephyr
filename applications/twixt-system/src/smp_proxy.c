/*
 * Copyright (c) 2026 Christoph Jans
 * SPDX-License-Identifier: Apache-2.0
 */

/* MCUmgr over BLE: one identity and advertising set for each MCU.
 * Local requests go to MCUmgr; remote requests and responses use twixt SMP.
 */

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/handlers.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt_defines.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/sys/byteorder.h>

#include <mgmt/mcumgr/transport/smp_internal.h>

#include <twixt/service/smp.h>
#include <twixt/twixt.h>

LOG_MODULE_REGISTER(smp_proxy, LOG_LEVEL_INF);

#define SMP_HDR_LEN 8
#define NOTIFY_HDR  3

struct route {
	const char *name;
	uint8_t addr;
	struct bt_le_ext_adv *adv;
	struct bt_conn *conn;
	struct net_buf *rx; /* the packet the client is writing */
};

/* Index = Bluetooth identity; the first is this board */
static struct route routes[] = {
	{.name = "twixt-nrf", .addr = DT_REG_ADDR(DT_NODELABEL(mcu_nrf))},
	{.name = "twixt-pico", .addr = DT_REG_ADDR(DT_NODELABEL(mcu_pico))},
	{.name = "twixt-esp32", .addr = DT_REG_ADDR(DT_NODELABEL(mcu_esp32))},
};

BUILD_ASSERT(ARRAY_SIZE(routes) <= CONFIG_BT_ID_MAX, "an identity per route");

static struct smp_transport local_transport;
static K_FIFO_DEFINE(remote_fifo);

static ssize_t smp_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 uint16_t len, uint16_t offset, uint8_t flags);

BT_GATT_SERVICE_DEFINE(smp_proxy_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(SMP_BT_SVC_UUID_VAL)),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(SMP_BT_CHR_UUID_VAL),
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE, NULL, smp_write, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static struct route *route_of(struct bt_conn *conn)
{
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) != 0 || info.id >= ARRAY_SIZE(routes)) {
		return NULL;
	}
	return &routes[info.id];
}

static int notify(struct route *r, const uint8_t *data, size_t len)
{
	struct bt_conn *conn = r->conn;
	size_t off = 0;

	if (conn == NULL) {
		return -ENOTCONN;
	}
	while (off < len) {
		uint16_t chunk = MIN(len - off, bt_gatt_get_mtu(conn) - NOTIFY_HDR);
		int err = bt_gatt_notify(conn, &smp_proxy_svc.attrs[2], &data[off], chunk);

		if (err == -ENOMEM) {
			k_msleep(1);
			continue;
		}
		if (err) {
			return err;
		}
		off += chunk;
	}
	return 0;
}

/* The packet is whole once its header and as many bytes as the header says are in */
static ssize_t smp_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			 uint16_t len, uint16_t offset, uint8_t flags)
{
	struct route *r = route_of(conn);
	size_t want;

	if (r == NULL) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}
	if (r->rx == NULL) {
		r->rx = smp_packet_alloc();
		if (r->rx == NULL) {
			return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
		}
	}
	if (net_buf_tailroom(r->rx) < len) {
		smp_packet_free(r->rx);
		r->rx = NULL;
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}
	net_buf_add_mem(r->rx, buf, len);
	if (r->rx->len < SMP_HDR_LEN) {
		return len;
	}
	want = SMP_HDR_LEN + sys_get_be16(&r->rx->data[2]);
	if (r->rx->len < want) {
		return len;
	}
	if (r->rx->len > want) {
		LOG_WRN("%s: %u bytes past the packet dropped", r->name, r->rx->len - want);
		r->rx->len = want;
	}

	if (r->addr == twixt_self_addr()) {
		smp_rx_req(&local_transport, r->rx);
	} else {
		*(uint8_t *)net_buf_user_data(r->rx) = r - routes;
		k_fifo_put(&remote_fifo, r->rx);
	}
	r->rx = NULL;
	return len;
}

static int local_output(struct net_buf *nb)
{
	int err = notify(&routes[0], nb->data, nb->len);

	smp_packet_free(nb);
	return err ? MGMT_ERR_EUNKNOWN : MGMT_ERR_EOK;
}

static void local_setup(void)
{
	local_transport.functions.output = local_output;
	if (smp_transport_init(&local_transport) != 0) {
		LOG_ERR("SMP transport init failed");
	}
}

MCUMGR_HANDLER_DEFINE(smp_proxy, local_setup);

/* Packets to the other MCUs go out here */
static void sender(void *p1, void *p2, void *p3)
{
	for (;;) {
		struct net_buf *nb = k_fifo_get(&remote_fifo, K_FOREVER);
		struct route *r = &routes[*(uint8_t *)net_buf_user_data(nb)];
		int err = twixt_smp_send(r->addr, nb->data, nb->len);

		if (err) {
			LOG_WRN("%s: %d", r->name, err);
		}
		smp_packet_free(nb);
	}
}

K_THREAD_DEFINE(smp_proxy_tx, 1024, sender, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
		0, 0);

/* Their answers come back here */
static void receiver(void *p1, void *p2, void *p3)
{
	for (;;) {
		uint8_t src;
		struct net_buf *nb = twixt_smp_rsp_get(&src, K_FOREVER);

		for (size_t i = 0; i < ARRAY_SIZE(routes); i++) {
			if (routes[i].addr == src) {
				(void)notify(&routes[i], nb->data, nb->len);
			}
		}
		smp_packet_free(nb);
	}
}

K_THREAD_DEFINE(smp_proxy_rx, 1024, receiver, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
		0, 0);

static void advertise(struct k_work *work)
{
	for (size_t i = 0; i < ARRAY_SIZE(routes); i++) {
		int err;

		if (routes[i].adv == NULL || routes[i].conn != NULL) {
			continue;
		}
		err = bt_le_ext_adv_start(routes[i].adv, BT_LE_EXT_ADV_START_DEFAULT);
		if (err && err != -EALREADY) {
			LOG_ERR("%s: advertising: %d", routes[i].name, err);
		}
	}
}

static K_WORK_DEFINE(advertise_work, advertise);

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct route *r = err ? NULL : route_of(conn);

	if (r == NULL) {
		return;
	}
	if (r->conn != NULL) {
		(void)bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}
	r->conn = bt_conn_ref(conn);
	LOG_INF("%s: connected", r->name);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct route *r = route_of(conn);

	if (r == NULL || r->conn != conn) {
		return;
	}
	bt_conn_unref(r->conn);
	r->conn = NULL;
	if (r->rx != NULL) {
		smp_packet_free(r->rx);
		r->rx = NULL;
	}
	LOG_INF("%s: disconnected, 0x%02x", r->name, reason);
}

static void recycled(void)
{
	k_work_submit(&advertise_work);
}

BT_CONN_CB_DEFINE(smp_proxy_conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

/* Identity 0 is the controller's; the others differ from it in the lowest address byte */
static int create_identities(void)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	bt_id_get(addrs, &count);
	for (size_t i = count; i < ARRAY_SIZE(routes); i++) {
		bt_addr_le_t addr = addrs[0];
		int id;

		addr.a.val[0] ^= i;
		id = bt_id_create(&addr, NULL);
		if (id != i) {
			return id < 0 ? id : -EINVAL;
		}
	}
	return 0;
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static int smp_proxy_init(void)
{
	int err = bt_enable(NULL);

	if (err == 0) {
		err = create_identities();
	}
	for (size_t i = 0; err == 0 && i < ARRAY_SIZE(routes); i++) {
		struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
			BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);
		const struct bt_data sd[] = {
			BT_DATA(BT_DATA_NAME_COMPLETE, routes[i].name, strlen(routes[i].name)),
		};

		param.id = i;
		err = bt_le_ext_adv_create(&param, NULL, &routes[i].adv);
		if (err == 0) {
			err = bt_le_ext_adv_set_data(routes[i].adv, ad, ARRAY_SIZE(ad), sd,
						     ARRAY_SIZE(sd));
		}
	}
	if (err) {
		LOG_ERR("Bluetooth: %d", err);
		return 0;
	}
	k_work_submit(&advertise_work);
	return 0;
}

SYS_INIT(smp_proxy_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
