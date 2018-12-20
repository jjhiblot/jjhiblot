// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2007 by Texas Instruments
 * Some code has been taken from tusb6010.c
 * Copyrights for that are attributable to:
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * This file is part of the Inventra Controller Driver for Linux.
 */
#include <common.h>
#include <dm.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <linux/usb/otg.h>
#include <asm/omap_common.h>
#include <asm/omap_musb.h>
#include <twl4030.h>
#include <twl6030.h>
#include "linux-compat.h"
#include "musb_core.h"
#include "omap2430.h"
#include "musb_uboot.h"

static inline void omap2430_low_level_exit(struct musb *musb)
{
	u32 l;

	/* in any role */
	l = musb_readl(musb->mregs, OTG_FORCESTDBY);
	l |= ENABLEFORCE;	/* enable MSTANDBY */
	musb_writel(musb->mregs, OTG_FORCESTDBY, l);
}

static inline void omap2430_low_level_init(struct musb *musb)
{
	u32 l;

	l = musb_readl(musb->mregs, OTG_FORCESTDBY);
	l &= ~ENABLEFORCE;	/* disable MSTANDBY */
	musb_writel(musb->mregs, OTG_FORCESTDBY, l);
}


static int omap2430_musb_init(struct musb *musb)
{
	u32 l;
	int status = 0;
	unsigned long int start;

	struct omap_musb_board_data *data =
		(struct omap_musb_board_data *)musb->controller;

	/* Reset the controller */
	musb_writel(musb->mregs, OTG_SYSCONFIG, SOFTRST);

	start = get_timer(0);

	while (1) {
		l = musb_readl(musb->mregs, OTG_SYSCONFIG);
		if ((l & SOFTRST) == 0)
			break;

		if (get_timer(start) > (CONFIG_SYS_HZ / 1000)) {
			dev_err(musb->controller, "MUSB reset is taking too long\n");
			return -ENODEV;
		}
	}

	l = musb_readl(musb->mregs, OTG_INTERFSEL);

	if (data->interface_type == MUSB_INTERFACE_UTMI) {
		/* OMAP4 uses Internal PHY GS70 which uses UTMI interface */
		l &= ~ULPI_12PIN;       /* Disable ULPI */
		l |= UTMI_8BIT;         /* Enable UTMI  */
	} else {
		l |= ULPI_12PIN;
	}

	musb_writel(musb->mregs, OTG_INTERFSEL, l);

	pr_debug("HS USB OTG: revision 0x%x, sysconfig 0x%02x, "
			"sysstatus 0x%x, intrfsel 0x%x, simenable  0x%x\n",
			musb_readl(musb->mregs, OTG_REVISION),
			musb_readl(musb->mregs, OTG_SYSCONFIG),
			musb_readl(musb->mregs, OTG_SYSSTATUS),
			musb_readl(musb->mregs, OTG_INTERFSEL),
			musb_readl(musb->mregs, OTG_SIMENABLE));
	return 0;

err1:
	return status;
}

static int omap2430_musb_enable(struct musb *musb)
{
#ifdef CONFIG_TWL4030_USB
	if (twl4030_usb_ulpi_init()) {
		serial_printf("ERROR: %s Could not initialize PHY\n",
				__PRETTY_FUNCTION__);
	}
#endif

#ifdef CONFIG_TWL6030_POWER
	twl6030_usb_device_settings();
#endif

#ifdef CONFIG_OMAP44XX
	u32 *usbotghs_control = (u32 *)((*ctrl)->control_usbotghs_ctrl);
	*usbotghs_control = USBOTGHS_CONTROL_AVALID |
		USBOTGHS_CONTROL_VBUSVALID | USBOTGHS_CONTROL_IDDIG;
#endif

	return 0;
}

static void omap2430_musb_disable(struct musb *musb)
{

}

static int omap2430_musb_exit(struct musb *musb)
{
	del_timer_sync(&musb_idle_timer);

	omap2430_low_level_exit(musb);

	return 0;
}

const struct musb_platform_ops omap2430_ops = {
	.init		= omap2430_musb_init,
	.exit		= omap2430_musb_exit,
	.enable		= omap2430_musb_enable,
	.disable	= omap2430_musb_disable,
};

#if CONFIG_IS_ENABLED(DM_USB)

static const struct udevice_id omap2430_musb_ids[] = {
	{ .compatible = "ti,omap3-musb" },
	{ .compatible = "ti,omap4-musb" },
	{ }
};

struct omap2430_musb_platdata {
	void *base;
	void *ctrl_mod_base;
	struct musb_hdrc_platform_data plat;
	struct musb_hdrc_config musb_config;
	struct omap_musb_board_data otg_board_data;
};

static int omap2430_musb_ofdata_to_platdata(struct udevice *dev)
{
	struct omap2430_musb_platdata *platdata = dev_get_platdata(dev);
	const void *fdt = gd->fdt_blob;
	int node = dev_of_offset(dev);

	platdata->base = (void *)dev_read_addr_ptr(dev);

	platdata->musb_config.multipoint = fdtdec_get_int(fdt, node,
							  "multipoint",
							  -1);
	if (platdata->musb_config.multipoint < 0) {
		pr_err("MUSB multipoint DT entry missing\n");
		return -ENOENT;
	}

	platdata->musb_config.dyn_fifo = 1;
	platdata->musb_config.num_eps = fdtdec_get_int(fdt, node,
						       "num-eps", -1);
	if (platdata->musb_config.num_eps < 0) {
		pr_err("MUSB num-eps DT entry missing\n");
		return -ENOENT;
	}

	platdata->musb_config.ram_bits = fdtdec_get_int(fdt, node,
							"ram-bits", -1);
	if (platdata->musb_config.ram_bits < 0) {
		pr_err("MUSB ram-bits DT entry missing\n");
		return -ENOENT;
	}

	platdata->plat.power = fdtdec_get_int(fdt, node,
								"power", -1);
	if (platdata->plat.power < 0) {
		pr_err("MUSB power DT entry missing\n");
		return -ENOENT;
	}

	platdata->otg_board_data.interface_type = fdtdec_get_int(fdt, node,
									"interface-type", -1);
	if (platdata->otg_board_data.interface_type < 0) {
		pr_err("MUSB interface-type DT entry missing\n");
		return -ENOENT;
	}

	platdata->otg_board_data.dev = dev;
	platdata->plat.config = &platdata->musb_config;
	platdata->plat.platform_ops = &omap2430_ops;
	platdata->plat.board_data = &platdata->otg_board_data;
	return 0;
}

#ifndef CONFIG_USB_MUSB_GADGET
static int omap2430_musb_probe(struct udevice *dev)
{
	struct musb_host_data *host = dev_get_priv(dev);
	struct omap2430_musb_platdata *platdata = dev_get_platdata(dev);
	struct usb_bus_priv *priv = dev_get_uclass_priv(dev);
	struct omap_musb_board_data *otg_board_data;
	int ret;
	void *base = dev_read_addr_ptr(dev);

	priv->desc_before_addr = true;

	otg_board_data = &platdata->otg_board_data;

	host->host = musb_init_controller(&platdata->plat,
					  (struct device *)otg_board_data,
					  platdata->base);
	if (!host->host) {
		return -EIO;
	}

	ret = musb_lowlevel_init(host);

	return ret;
}

static int omap2430_musb_remove(struct udevice *dev)
{
	struct musb_host_data *host = dev_get_priv(dev);

	musb_stop(host->host);

	return 0;
}

#if CONFIG_IS_ENABLED(OF_CONTROL)
static int omap2430_musb_host_ofdata_to_platdata(struct udevice *dev)
{
	struct omap2430_musb_platdata *platdata = dev_get_platdata(dev);
	const void *fdt = gd->fdt_blob;
	int node = dev_of_offset(dev);
	int ret;

	ret = omap2430_musb_ofdata_to_platdata(dev);
	if (ret) {
		pr_err("platdata dt parse error\n");
		return ret;
	}

	platdata->plat.mode = MUSB_HOST;

	return 0;
}
#endif

U_BOOT_DRIVER(omap2430_musb) = {
	.name	= "omap2430-musb",
	.id		= UCLASS_USB,
	.of_match = omap2430_musb_ids,
#if CONFIG_IS_ENABLED(OF_CONTROL)
	.ofdata_to_platdata = omap2430_musb_host_ofdata_to_platdata,
#endif
	.probe = omap2430_musb_probe,
	.remove = omap2430_musb_remove,
	.ops = &musb_usb_ops,
	.platdata_auto_alloc_size = sizeof(struct omap2430_musb_platdata),
	.priv_auto_alloc_size = sizeof(struct musb_host_data),
};

#else

struct omap2430_musb_peripheral {
	struct musb *periph;
};

#if CONFIG_IS_ENABLED(OF_CONTROL)
static int omap2430_musb_peripheral_ofdata_to_platdata(struct udevice *dev)
{
	struct ti_musb_platdata *platdata = dev_get_platdata(dev);
	const void *fdt = gd->fdt_blob;
	int node = dev_of_offset(dev);
	int ret;

	ret = omap2430_musb_ofdata_to_platdata(dev);
	if (ret) {
		pr_err("platdata dt parse error\n");
		return ret;
	}
	platdata->plat.mode = MUSB_PERIPHERAL;

	return 0;
}
#endif

int dm_usb_gadget_handle_interrupts(struct udevice *dev)
{
	struct omap2430_musb_peripheral *priv = dev_get_priv(dev);

	priv->periph->isr(0, priv->periph);

	return 0;
}

static int omap2430_musb_peripheral_probe(struct udevice *dev)
{
	struct omap2430_musb_peripheral *priv = dev_get_priv(dev);
	struct omap2430_musb_platdata *platdata = dev_get_platdata(dev);
	struct omap_musb_board_data *otg_board_data;
	int ret;

	otg_board_data = &platdata->otg_board_data;
	priv->periph = musb_init_controller(&platdata->plat,
					    (struct device *)otg_board_data,
					    platdata->base);
	if (!priv->periph)
		return -EIO;

	/* ti_musb_set_phy_power(dev, 1); */
	musb_gadget_setup(priv->periph);
	return usb_add_gadget_udc((struct device *)dev, &priv->periph->g);
}

static int omap2430_musb_peripheral_remove(struct udevice *dev)
{
	struct omap2430_musb_peripheral *priv = dev_get_priv(dev);

	usb_del_gadget_udc(&priv->periph->g);
	/* ti_musb_set_phy_power(dev, 0); */

	return 0;
}

U_BOOT_DRIVER(omap2430_musb_peripheral) = {
	.name	= "ti-musb-peripheral",
	.id	= UCLASS_USB_GADGET_GENERIC,
	.of_match = omap2430_musb_ids,
#if CONFIG_IS_ENABLED(OF_CONTROL)
	.ofdata_to_platdata = omap2430_musb_peripheral_ofdata_to_platdata,
#endif
	.probe = omap2430_musb_peripheral_probe,
	.remove = omap2430_musb_peripheral_remove,
	.ops	= &musb_usb_ops,
	.platdata_auto_alloc_size = sizeof(struct omap2430_musb_platdata),
	.priv_auto_alloc_size = sizeof(struct omap2430_musb_peripheral),
	.flags = DM_FLAG_PRE_RELOC,
};
#endif

#endif /* CONFIG_IS_ENABLED(DM_USB) */
