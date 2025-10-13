#include <zephyr/logging/log.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <string.h>
#include <errno.h>

#include <zephyr/random/random.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/zbus/zbus.h>

#include "w1_ds18b20.h"
#include "credentials.h"

#define MQTT_POLL_MSEC	100
#define MQTT_PORT 1883
#define MQTT_TOPIC "zephyr/sensor"

#define CONFIG_APPLICATION_MQTT_CONN_INIT_PRIORITY 92

LOG_MODULE_REGISTER(mqtt_conn, CONFIG_LOG_DEFAULT_LEVEL);

ZBUS_CHAN_DECLARE(ds18b20_value_chan);
ZBUS_SUBSCRIBER_DEFINE(mqtt_conn_sub, 4);

static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];
static struct mqtt_client client_ctx;

static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];
static struct net_mgmt_event_callback dhcp_cb;
static int nfds;

K_SEM_DEFINE(netif_ready, 0, 1);

static void prepare_fds(struct mqtt_client *client)
{
	fds[0].fd = client->transport.tcp.sock;
	fds[0].events = ZSOCK_POLLIN;
	nfds = 1;
}

static int poll_socks(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = zsock_poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

void app_mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}
		LOG_INF("MQTT client connected!");
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");

		break;

	default:
		break;
	}
}

static int app_mqtt_publish(struct mqtt_client *client, const char *topic, uint8_t *payload)
{
	LOG_INF("app_mqtt_publish [%s]: %s °C", topic, payload);
	struct mqtt_publish_param param;

	param.message.payload.data = payload;
	param.message.payload.len = strlen(payload);

	param.message.topic.topic.utf8 =  topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;

	param.message_id = sys_rand16_get();

	param.dup_flag = 0u;
	param.retain_flag = 0u;

	return mqtt_publish(client, &param);
}

static void app_mqtt_broker_init(void)
{
	LOG_INF("app_mqtt_broker_init");
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(MQTT_PORT);

	zsock_inet_pton(AF_INET, MQTT_SERVER, &broker4->sin_addr);
}

static void app_mqtt_client_init(struct mqtt_client *client)
{
	LOG_INF("app_mqtt_client_init");
	mqtt_client_init(client);

	app_mqtt_broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = app_mqtt_evt_handler;
	client->client_id.utf8 = (const uint8_t *)MQTT_CLIENT;
	client->client_id.size = strlen(client->client_id.utf8);
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
}

static int app_mqtt_connect(struct mqtt_client *client)
{
	LOG_INF("app_mqtt_connect");
	int rc;

	app_mqtt_client_init(client);

	rc = mqtt_connect(client);
	if (rc != 0) {
		return rc;
	}

	prepare_fds(client);

	if (poll_socks(MQTT_POLL_MSEC)) {
		mqtt_input(client);
	}

	return rc;
}

static int app_mqtt_process_mqtt(struct mqtt_client *client)
{
	int rc;

	if (poll_socks(MQTT_POLL_MSEC)) {
		rc = mqtt_input(client);
		if (rc != 0) {
			return rc;
		}
	}

	rc = mqtt_live(client);
	if (rc != 0 && rc != -EAGAIN) {
		return rc;
	} else if (rc == 0) {
		rc = mqtt_input(client);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static void handler_cb(struct net_mgmt_event_callback *cb,
		    uint64_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}
	k_sem_give(&netif_ready);
}

static void wifi_interface_init(void)
{
	LOG_INF("wifi_interface_init");
	struct net_if *iface;

	net_mgmt_init_event_callback(&dhcp_cb, handler_cb,
				     NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&dhcp_cb);

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("wifi interface not available");
		return;
	}

	net_dhcpv4_start(iface);
	k_sem_take(&netif_ready, K_FOREVER);
}

static int mqtt_conn_init(void)
{
	LOG_INF("MQTT Init");

	wifi_interface_init();
	app_mqtt_connect(&client_ctx);

	const struct zbus_channel *chan;
	struct ds18b20_value *ds18b20_value_msg;
	uint8_t temp_str[16];
	char rom_str[40];
	while(1) {
		app_mqtt_process_mqtt(&client_ctx);

		while(!zbus_sub_wait(&mqtt_conn_sub, &chan, K_MSEC(200))){
			ds18b20_value_msg = zbus_chan_msg(&ds18b20_value_chan);

			snprintf((char *)temp_str, sizeof(temp_str), "%.3f", (double)ds18b20_value_msg->tempf);
			snprintk(rom_str, sizeof(rom_str),
					"sensor/%02X%02X%02X%02X%02X%02X%02X%02X",
         			ds18b20_value_msg->rom.family,
					ds18b20_value_msg->rom.serial[0],
					ds18b20_value_msg->rom.serial[1],
					ds18b20_value_msg->rom.serial[2],
					ds18b20_value_msg->rom.serial[3],
					ds18b20_value_msg->rom.serial[4],
					ds18b20_value_msg->rom.serial[5],
         			ds18b20_value_msg->rom.crc);

			app_mqtt_publish(&client_ctx, rom_str, temp_str);
		}
	}
    return 0;
}
SYS_INIT(mqtt_conn_init, APPLICATION, CONFIG_APPLICATION_MQTT_CONN_INIT_PRIORITY);