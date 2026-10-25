/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART over the multi-master I2C bus: a byte stream to one other MCU, `peer`.
 *
 * Sending is pushing: the bytes written to the UART are sent to the peer in
 * requests of the mcubus, UART_MCUBUS_CMD: up to CONFIG_UART_MCUBUS_CHUNK
 * bytes. The response is how many of them the peer
 * took; the rest stays and is sent again. So a full receive buffer holds the
 * sender back instead of losing bytes. If nobody answers at the peer's address,
 * or it has no UART for this MCU, the bytes are dropped, like on a UART without
 * a cable.
 *
 * Receiving runs in the I2C target interrupt (mcubus service): the bytes go to
 * the receive buffer of the UART whose peer sent them (`src` of the request).
 *
 * One thread does the bus transfers of all UARTs of this MCU, and plays the
 * UART interrupt: it calls the interrupt-driven API callback while RX data is
 * there or TX room is, with that interrupt enabled.
 */

#define DT_DRV_COMPAT zephyr_mcubus_uart

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/ring_buffer.h>

#include <mcubus/mcubus.h>

LOG_MODULE_REGISTER(uart_mcubus, CONFIG_UART_LOG_LEVEL);

/* The cmd of the service, see <mcubus/mcubus.h> */
#define UART_MCUBUS_CMD 0x10

#define CHUNK CONFIG_UART_MCUBUS_CHUNK

BUILD_ASSERT(CHUNK <= MCUBUS_DATA_MAX, "a chunk must fit CONFIG_I2C_MM_MSG_MAX");

/* Blocking uart_poll_out() gives up on a full buffer after this long */
#define POLL_OUT_TIMEOUT_MS 100

struct mu_config {
	uint8_t peer;
};

struct mu_data {
	struct k_spinlock lock;
	struct ring_buf rx;
	struct ring_buf tx;
	uint8_t rx_buf[CONFIG_UART_MCUBUS_RX_BUF_SIZE];
	uint8_t tx_buf[CONFIG_UART_MCUBUS_TX_BUF_SIZE];
	uart_irq_callback_user_data_t cb;
	void *cb_data;
	bool rx_irq;
	bool tx_irq;
};

/* Wakes the thread: there is something to send, or a callback to run */
static K_SEM_DEFINE(kick, 0, 1);

extern const k_tid_t uart_mcubus_thread;

/* ---- interrupt-driven API, played by the thread ------------------------------ */

static int mu_fifo_fill(const struct device *dev, const uint8_t *buf, int size)
{
	struct mu_data *d = dev->data;
	uint32_t n;

	K_SPINLOCK(&d->lock) {
		n = ring_buf_put(&d->tx, buf, size);
	}
	if (n > 0) {
		k_sem_give(&kick);
	}
	return n;
}

static int mu_fifo_read(const struct device *dev, uint8_t *buf, const int size)
{
	struct mu_data *d = dev->data;
	uint32_t n;

	K_SPINLOCK(&d->lock) {
		n = ring_buf_get(&d->rx, buf, size);
	}
	return n;
}

static int mu_poll_in(const struct device *dev, unsigned char *c)
{
	return mu_fifo_read(dev, c, 1) == 1 ? 0 : -1;
}

/*
 * Waits for room while it can; in an interrupt, and in the thread that makes the
 * room, a full buffer drops the byte
 */
static void mu_poll_out(const struct device *dev, unsigned char c)
{
	if (k_is_in_isr() || k_current_get() == uart_mcubus_thread) {
		mu_fifo_fill(dev, &c, 1);
		return;
	}
	(void)WAIT_FOR(mu_fifo_fill(dev, &c, 1) == 1, POLL_OUT_TIMEOUT_MS * USEC_PER_MSEC,
		       k_msleep(1));
}

static void mu_irq_tx_enable(const struct device *dev)
{
	struct mu_data *d = dev->data;

	d->tx_irq = true;
	k_sem_give(&kick);
}

static void mu_irq_tx_disable(const struct device *dev)
{
	struct mu_data *d = dev->data;

	d->tx_irq = false;
}

static int mu_irq_tx_ready(const struct device *dev)
{
	struct mu_data *d = dev->data;

	return d->tx_irq && ring_buf_space_get(&d->tx) > 0;
}

static int mu_irq_tx_complete(const struct device *dev)
{
	struct mu_data *d = dev->data;

	return ring_buf_is_empty(&d->tx);
}

static void mu_irq_rx_enable(const struct device *dev)
{
	struct mu_data *d = dev->data;

	d->rx_irq = true;
	k_sem_give(&kick);
}

static void mu_irq_rx_disable(const struct device *dev)
{
	struct mu_data *d = dev->data;

	d->rx_irq = false;
}

static int mu_irq_rx_ready(const struct device *dev)
{
	struct mu_data *d = dev->data;

	return !ring_buf_is_empty(&d->rx);
}

static int mu_irq_is_pending(const struct device *dev)
{
	struct mu_data *d = dev->data;

	return mu_irq_tx_ready(dev) || (d->rx_irq && mu_irq_rx_ready(dev));
}

static int mu_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 1;
}

static void mu_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
				void *user_data)
{
	struct mu_data *d = dev->data;

	d->cb = cb;
	d->cb_data = user_data;
}

/* The "interrupt": run the callback if it has something to do */
static void mu_irq_run(const struct device *dev)
{
	struct mu_data *d = dev->data;

	if (d->cb != NULL && mu_irq_is_pending(dev)) {
		d->cb(dev, d->cb_data);
	}
}

static DEVICE_API(uart, mu_api) = {
	.poll_in = mu_poll_in,
	.poll_out = mu_poll_out,
	.fifo_fill = mu_fifo_fill,
	.fifo_read = mu_fifo_read,
	.irq_tx_enable = mu_irq_tx_enable,
	.irq_tx_disable = mu_irq_tx_disable,
	.irq_tx_ready = mu_irq_tx_ready,
	.irq_tx_complete = mu_irq_tx_complete,
	.irq_rx_enable = mu_irq_rx_enable,
	.irq_rx_disable = mu_irq_rx_disable,
	.irq_rx_ready = mu_irq_rx_ready,
	.irq_is_pending = mu_irq_is_pending,
	.irq_update = mu_irq_update,
	.irq_callback_set = mu_irq_callback_set,
};

static int mu_init(const struct device *dev)
{
	struct mu_data *d = dev->data;

	ring_buf_init(&d->rx, sizeof(d->rx_buf), d->rx_buf);
	ring_buf_init(&d->tx, sizeof(d->tx_buf), d->tx_buf);
	return 0;
}

#define MU_INST(n)                                                                                 \
	static struct mu_data mu_data_##n;                                                         \
	static const struct mu_config mu_config_##n = {                                            \
		.peer = DT_REG_ADDR(DT_INST_PHANDLE(n, peer)),                                     \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, mu_init, NULL, &mu_data_##n, &mu_config_##n, POST_KERNEL,         \
			      CONFIG_SERIAL_INIT_PRIORITY, &mu_api);

DT_INST_FOREACH_STATUS_OKAY(MU_INST)

#define MU_DEV(n) DEVICE_DT_INST_GET(n),

static const struct device *const uarts[] = {DT_INST_FOREACH_STATUS_OKAY(MU_DEV)};

/* ---- receiving: the mcubus service, in the I2C target interrupt -------------- */

/* The data is the bytes; the payload of the response is how many were taken */
static int mu_receive(const struct mcubus_req *req, uint8_t *rsp)
{
	for (size_t i = 0; i < ARRAY_SIZE(uarts); i++) {
		const struct mu_config *cfg = uarts[i]->config;
		struct mu_data *d = uarts[i]->data;
		uint32_t n;

		if (cfg->peer != req->src) {
			continue;
		}
		K_SPINLOCK(&d->lock) {
			n = ring_buf_put(&d->rx, req->data, req->len);
		}
		if (n > 0) {
			k_sem_give(&kick);
		}
		rsp[0] = n;
		return 1;
	}
	return -ENODEV; /* no UART for that MCU */
}

MCUBUS_SERVICE_DEFINE(uart_mcubus_service, UART_MCUBUS_CMD, mu_receive);

/* ---- sending ------------------------------------------------------------------- */

/* Send one chunk to the peer. Returns 1 if bytes went, 0 if there were none, -EAGAIN. */
static int mu_send(const struct device *dev)
{
	const struct mu_config *cfg = dev->config;
	struct mu_data *d = dev->data;
	uint8_t chunk[CHUNK];
	uint8_t taken = 0;
	uint32_t n;
	int ret;

	K_SPINLOCK(&d->lock) {
		n = ring_buf_peek(&d->tx, chunk, CHUNK);
	}
	if (n == 0) {
		return 0;
	}

	ret = mcubus_request(cfg->peer, UART_MCUBUS_CMD, chunk, n, &taken, 1);
	if (ret == -EAGAIN || ret == -EBUSY) {
		return -EAGAIN;
	}
	if (ret) {
		/* nobody there, or no UART for us there: like a UART without a cable */
		taken = n;
	}
	K_SPINLOCK(&d->lock) {
		ring_buf_get(&d->tx, NULL, MIN(taken, n));
	}
	return taken > 0 ? 1 : -EAGAIN;
}

static void mu_thread(void *p1, void *p2, void *p3)
{
	k_timeout_t wait = K_FOREVER;

	for (;;) {
		k_sem_take(&kick, wait);
		wait = K_FOREVER;

		for (size_t i = 0; i < ARRAY_SIZE(uarts); i++) {
			int ret;

			mu_irq_run(uarts[i]);
			ret = mu_send(uarts[i]);
			if (ret > 0) {
				mu_irq_run(uarts[i]); /* room again in the TX buffer */
				wait = K_NO_WAIT;
			} else if (ret < 0 && K_TIMEOUT_EQ(wait, K_FOREVER)) {
				wait = K_MSEC(CONFIG_UART_MCUBUS_POLL_MS);
			}
		}
	}
}

K_THREAD_DEFINE(uart_mcubus_thread, CONFIG_UART_MCUBUS_THREAD_STACK_SIZE, mu_thread, NULL, NULL,
		NULL, CONFIG_UART_MCUBUS_THREAD_PRIORITY, 0, 0);
