/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <asm/unaligned.h>
#include <linux/list.h>
#include <dm/device.h>
#include <dm.h>

#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"

static const struct rockchip_crtc rk3328_vop_data = {
	.funcs = &rockchip_vop_funcs,
	.data = &rk3328_vop,
};

static const struct udevice_id rockchip_vop_ids[] = {
	{
		.compatible = "rockchip,rk3328-vop",
		.data = (ulong)&rk3328_vop_data,
	}, { }
};

static int rockchip_vop_probe(struct udevice *dev)
{
	printf("rockchip_vop_probe\n");
	return 0;
}

static int rockchip_vop_bind(struct udevice *dev)
{
	printf("rockchip_vop_bind\n");
	return 0;
}

U_BOOT_DRIVER(rockchip_vop) = {
	.name	= "rockchip-vop",
	.id	= UCLASS_VIDEO_CRTC,
	.of_match = rockchip_vop_ids,
	.bind	= rockchip_vop_bind,
	.probe	= rockchip_vop_probe,
};

UCLASS_DRIVER(rockchip_crtc) = {
	.id		= UCLASS_VIDEO_CRTC,
	.name		= "CRTC",
};
