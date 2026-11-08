/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

#include <twixt/twixt.h>

#define ADD_SERVICE TWIXT_SERVICE_APP

static int add(const struct twixt_req *req, uint8_t *rsp_data)
{
	if (req->len != 8) {
		return -EINVAL;
	}
	sys_put_le32(sys_get_le32(&req->data[0]) + sys_get_le32(&req->data[4]), rsp_data);
	return 4;
}

TWIXT_SERVICE_DEFINE(add_service, ADD_SERVICE, add);

#ifdef CONFIG_TWIXT_SHELL
static int cmd_add(const struct shell *sh, size_t argc, char **argv)
{
	int err = 0;
	unsigned long addr = shell_strtoul(argv[1], 0, &err);

	if (err || addr > 0x7f) {
		shell_error(sh, "invalid address: %s", argv[1]);
		return -EINVAL;
	}
	long a = shell_strtol(argv[2], 0, &err);

	if (err) {
		shell_error(sh, "invalid number: %s", argv[2]);
		return -EINVAL;
	}
	long b = shell_strtol(argv[3], 0, &err);

	if (err) {
		shell_error(sh, "invalid number: %s", argv[3]);
		return -EINVAL;
	}

	uint8_t req[8];
	uint8_t rsp[4];

	sys_put_le32(a, &req[0]);
	sys_put_le32(b, &req[4]);
	int ret = twixt_request(addr, ADD_SERVICE, req, sizeof(req), rsp, sizeof(rsp));

	if (ret) {
		shell_error(sh, "0x%02lx: %d", addr, ret);
		return ret;
	}
	shell_print(sh, "%d", (int32_t)sys_get_le32(rsp));
	return 0;
}
SHELL_SUBCMD_ADD((twixt), add, NULL, "<addr> <a> <b>: add two numbers on the MCU at <addr>",
		 cmd_add, 4, 0);
#endif /* CONFIG_TWIXT_SHELL */
