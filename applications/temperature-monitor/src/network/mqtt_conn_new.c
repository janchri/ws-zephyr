#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/random/random.h>

#include "credentials.h"

LOG_MODULE_REGISTER(simple_mqtt, LOG_LEVEL_INF);

#define CONFIG_APPLICATION_MQTT_CONN_INIT_PRIORITY 91

static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];
static struct mqtt_client client;
static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];

static void mqtt_event_handler(struct mqtt_client *c,
                               const struct mqtt_evt *evt)
{
    if (evt->type == MQTT_EVT_CONNACK && evt->result == 0) {
        LOG_INF("MQTT connected");
    }
}

static void prepare_fds(struct mqtt_client *client)
{
    fds[0].fd = client->transport.tcp.sock;
    fds[0].events = ZSOCK_POLLIN;
}

static void mqtt_broker_init(void)
{
    struct sockaddr_in *b = (struct sockaddr_in *)&broker;

    b->sin_family = AF_INET;
    b->sin_port = htons(MQTT_PORT);
    zsock_inet_pton(AF_INET, MQTT_SERVER, &b->sin_addr);
}

static void mqtt_client_setup(struct mqtt_client *client)
{
    mqtt_client_init(client);
    mqtt_broker_init();

    client->broker = &broker;
    client->evt_cb = mqtt_event_handler;
    client->client_id.utf8 = (uint8_t *)MQTT_CLIENT;
    client->client_id.size = strlen(MQTT_CLIENT);
    client->protocol_version = MQTT_VERSION_3_1_1;

    client->rx_buf = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);
    client->tx_buf = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);

    client->transport.type = MQTT_TRANSPORT_NON_SECURE;
}

static int mqtt_publish_simple(struct mqtt_client *client, const char *topic, const char *msg)
{
    struct mqtt_publish_param param;

    param.message.topic.topic.utf8 = (uint8_t *)topic;
    param.message.topic.topic.size = strlen(topic);
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;

    param.message.payload.data = (uint8_t *)msg;
    param.message.payload.len = strlen(msg);

    param.message_id = sys_rand16_get();
    param.dup_flag = 0;
    param.retain_flag = 0;

    return mqtt_publish(client, &param);
}

//ZBUS_CHAN_DEFINE(ds18b20_value_chan, struct ds18b20_value, NULL, NULL, ZBUS_OBSERVERS(mqtt_conn_sub), ZBUS_MSG_INIT(0));
static int mqtt_conn_init(void)
{
    LOG_INF("Simple MQTT demo starting");

    mqtt_client_setup(&client);

    if (mqtt_connect(&client) != 0) {
        LOG_ERR("MQTT connect failed");
        return -1;
    }

    prepare_fds(&client);

    /* Wait for CONNACK */
    zsock_poll(fds, 1, 1000);
    mqtt_input(&client);

    /* ---- SIMPLE WRITE TO TOPIC ---- */
    mqtt_publish_simple(&client, MQTT_TOPIC_TEST, "Hello From Zephyr!");

    LOG_INF("Message published!");

    mqtt_disconnect(&client, NULL);
    LOG_INF("MQTT disconnected.");

    return 0;
}

SYS_INIT(mqtt_conn_init, APPLICATION, CONFIG_APPLICATION_MQTT_CONN_INIT_PRIORITY);