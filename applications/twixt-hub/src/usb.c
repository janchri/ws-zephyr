/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>

#include <zephyr/usb/usbd.h>

/* Zephyr's VID and a PID for testing; a product needs its own */
USBD_DEVICE_DEFINE(gw_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x0004);
USBD_DESC_LANG_DEFINE(gw_lang);
USBD_DESC_MANUFACTURER_DEFINE(gw_mfr, "twixt");
USBD_DESC_PRODUCT_DEFINE(gw_product, "twixt pico");
USBD_DESC_CONFIG_DEFINE(gw_cfg_desc, "FS Configuration");
USBD_CONFIGURATION_DEFINE(gw_config, 0, 125, &gw_cfg_desc);

/* Resolve each CDC class by its devicetree port, then register in host order.
 * Each CDC port occupies two USB interfaces: if00, if02, if04.
 */
#define DT_DRV_COMPAT zephyr_cdc_acm_uart
#define CDC_CLASS_FOR_INSTANCE(n)                                                                  \
	if (dev == DEVICE_DT_GET(DT_INST(n, zephyr_cdc_acm_uart))) {                               \
		return "cdc_acm_" STRINGIFY(n);                                                    \
	}

static const char *cdc_class(const struct device *dev)
{
	DT_INST_FOREACH_STATUS_OKAY(CDC_CLASS_FOR_INSTANCE)
	return NULL;
}

int twixt_usb_init(void)
{
	struct usbd_desc_node *const descs[] = {&gw_lang, &gw_mfr, &gw_product};
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(descs); i++) {
		err = usbd_add_descriptor(&gw_usbd, descs[i]);
		if (err) {
			return err;
		}
	}
	err = usbd_add_configuration(&gw_usbd, USBD_SPEED_FS, &gw_config);
	if (err) {
		return err;
	}
	const struct device *const ports[] = {
		DEVICE_DT_GET(DT_NODELABEL(host_gw)),
		DEVICE_DT_GET(DT_NODELABEL(host_nrf)),
		DEVICE_DT_GET(DT_NODELABEL(host_esp32)),
	};

	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		err = usbd_register_class(&gw_usbd, cdc_class(ports[i]), USBD_SPEED_FS, 1);
		if (err) {
			return err;
		}
	}
	usbd_device_set_code_triple(&gw_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	err = usbd_init(&gw_usbd);
	return err ? err : usbd_enable(&gw_usbd);
}
