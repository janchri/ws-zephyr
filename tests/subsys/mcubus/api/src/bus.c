/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The mcubus under faults, and its ping service. The emulated port can address
 * its own target (`loopback`), so these tests send raw transfers to this MCU's target, over
 * the bus and through its interrupt: what a broken or foreign controller could
 * send, and what a controller that lost its place leaves behind. A test service
 * counts how often a request really ran; another one echoes what it got.
 */

#include <string.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <drivers/i2c/i2c_mm_emul.h>
#include <mcubus/mcubus.h>
#include <mcubus/ping.h>

#define ADDR       DT_REG_ADDR(MCUBUS_SELF)
#define EMPTY_ADDR 0x55
#define CMD_COUNT  0x7e /* the test service */
#define CMD_NONE   0x7d /* no service */
#define CMD_ECHO   0x7c /* src, data */
#define SRC        0x42 /* any sender */

static atomic_t runs;

/* The payload of the response is how often it ran, this time included */
static int count(const struct mcubus_req *req, uint8_t *rsp)
{
	rsp[0] = atomic_inc(&runs) + 1;
	return 1;
}

MCUBUS_SERVICE_DEFINE(test_count_service, CMD_COUNT, count);

/* The payload of the response is src, data */
static int echo(const struct mcubus_req *req, uint8_t *rsp)
{
	rsp[0] = req->src;
	memcpy(&rsp[1], req->data, req->len);
	return 1 + req->len;
}

MCUBUS_SERVICE_DEFINE(test_echo_service, CMD_ECHO, echo);

/* Raw transfers to this MCU's own target, over the bus: the request is cmd, src, data; the
 * response is status, payload
 */
static int request(const uint8_t *req, size_t req_len, uint8_t *rsp, size_t rsp_len)
{
	return i2c_write_read(mcubus_link()->i2c, ADDR, req, req_len, rsp, rsp_len);
}

static const uint8_t count_req[] = {CMD_COUNT, SRC};

ZTEST(mcubus_faults, test_request)
{
	atomic_val_t before = atomic_get(&runs);
	uint8_t rsp[2];

	zassert_ok(request(count_req, sizeof(count_req), rsp, sizeof(rsp)));
	zassert_equal(rsp[0], 0);
	zassert_equal(rsp[1], before + 1);
	zassert_equal(atomic_get(&runs), before + 1);
}

/* A read without a request (a bus scan) gets -EINVAL, and does not run the last request */
ZTEST(mcubus_faults, test_read_alone)
{
	uint8_t rsp[2];
	atomic_val_t before;

	zassert_ok(request(count_req, sizeof(count_req), rsp, sizeof(rsp)));
	before = atomic_get(&runs);

	zassert_ok(i2c_read(mcubus_link()->i2c, rsp, sizeof(rsp), ADDR));
	zassert_equal((int8_t)rsp[0], -EINVAL);
	zassert_equal(atomic_get(&runs), before, "the last request ran again");
}

/* A request whose controller gave up before the read does not run, not even later */
ZTEST(mcubus_faults, test_write_alone)
{
	atomic_val_t before = atomic_get(&runs);
	uint8_t rsp[2];

	zassert_ok(i2c_write(mcubus_link()->i2c, count_req, sizeof(count_req), ADDR));
	zassert_ok(i2c_read(mcubus_link()->i2c, rsp, sizeof(rsp), ADDR));
	zassert_equal((int8_t)rsp[0], -EINVAL);
	zassert_equal(atomic_get(&runs), before);
}

/*
 * The controller reads only part of the response (it was reset during the read):
 * the request ran once, and the next one gets its own fresh response
 */
ZTEST(mcubus_faults, test_lost_response)
{
	atomic_val_t before = atomic_get(&runs);
	uint8_t rsp[2];

	zassert_ok(request(count_req, sizeof(count_req), rsp, 1));
	zassert_equal(atomic_get(&runs), before + 1);

	zassert_ok(request(count_req, sizeof(count_req), rsp, sizeof(rsp)));
	zassert_equal(rsp[0], 0);
	zassert_equal(rsp[1], before + 2);
}

/* A request longer than MCUBUS_MSG_MAX is NACKed and not run; the next one works */
ZTEST(mcubus_faults, test_too_long)
{
	uint8_t req[MCUBUS_MSG_MAX + 1] = {CMD_COUNT, SRC};
	atomic_val_t before = atomic_get(&runs);
	uint8_t rsp[2];

	zassert_equal(request(req, sizeof(req), rsp, sizeof(rsp)), -EIO);
	zassert_equal(atomic_get(&runs), before);

	zassert_ok(request(count_req, sizeof(count_req), rsp, sizeof(rsp)));
	zassert_equal(rsp[1], before + 1);
}

ZTEST(mcubus_faults, test_unknown_cmd)
{
	const uint8_t req[] = {CMD_NONE, SRC};
	uint8_t rsp[2];

	zassert_ok(request(req, sizeof(req), rsp, sizeof(rsp)));
	zassert_equal((int8_t)rsp[0], -ENOTSUP);
}

/* A cmd without src is no request */
ZTEST(mcubus_faults, test_no_src)
{
	const uint8_t req[] = {CMD_COUNT};
	atomic_val_t before = atomic_get(&runs);
	uint8_t rsp[2];

	zassert_ok(request(req, sizeof(req), rsp, sizeof(rsp)));
	zassert_equal((int8_t)rsp[0], -EINVAL);
	zassert_equal(atomic_get(&runs), before);
}

/* The service gets src and data as they were sent, and its payload comes back whole */
ZTEST(mcubus_faults, test_echo)
{
	const uint8_t req[] = {CMD_ECHO, SRC, 1, 2, 3};
	const uint8_t want[] = {0, SRC, 1, 2, 3};
	uint8_t rsp[sizeof(want)];

	zassert_ok(request(req, sizeof(req), rsp, sizeof(rsp)));
	zassert_mem_equal(rsp, want, sizeof(want));
}

/* mcubus_request() puts this MCU in as src; to itself, past the payload reads as 0xff */
ZTEST(mcubus_faults, test_echo_request)
{
	const uint8_t data[] = {4, 5};
	const uint8_t want[] = {ADDR, 4, 5, 0xff};
	uint8_t rsp[sizeof(want)];

	zassert_ok(mcubus_request(ADDR, CMD_ECHO, data, sizeof(data), rsp, sizeof(rsp)));
	zassert_mem_equal(rsp, want, sizeof(want));
}

/* A target holds SDA low: the driver recovers the bus, and the request gets through */
ZTEST(mcubus_faults, test_stuck_bus)
{
	struct i2c_mm_emul_stats before, after;
	struct mcubus_info info;

	i2c_mm_emul_get_stats(mcubus_link()->i2c, &before);
	i2c_mm_emul_stick_bus(mcubus_link()->i2c);

	zassert_equal(mcubus_ping(EMPTY_ADDR, &info), -EIO, "not through to the NACK");
	i2c_mm_emul_get_stats(mcubus_link()->i2c, &after);
	zassert_equal(after.recoveries, before.recoveries + 1);
}

ZTEST_SUITE(mcubus_faults, NULL, NULL, NULL, NULL, NULL);

ZTEST(mcubus_ping, test_ping)
{
	struct mcubus_info info;

	zassert_ok(mcubus_ping(ADDR, &info));
	zassert_equal(info.version, MCUBUS_PROTOCOL_VERSION);
	zassert_equal(info.msg_max, MCUBUS_MSG_MAX);
}

/* The same over the bus: cmd 0x00, src; response status, version, MCUBUS_MSG_MAX */
ZTEST(mcubus_ping, test_ping_on_the_wire)
{
	const uint8_t req[] = {MCUBUS_PING_CMD, SRC};
	uint8_t rsp[3];

	zassert_ok(request(req, sizeof(req), rsp, sizeof(rsp)));
	zassert_equal(rsp[0], 0);
	zassert_equal(rsp[1], MCUBUS_PROTOCOL_VERSION);
	zassert_equal(rsp[2], MCUBUS_MSG_MAX);
}

ZTEST(mcubus_ping, test_ping_nobody)
{
	struct mcubus_info info;

	zassert_equal(mcubus_ping(EMPTY_ADDR, &info), -EIO);
}

ZTEST_SUITE(mcubus_ping, NULL, NULL, NULL, NULL, NULL);
