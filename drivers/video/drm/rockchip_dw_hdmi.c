/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <boot_rkimg.h>
#include <asm/io.h>
#include <dm/device.h>
#include <linux/dw_hdmi.h>
#include <linux/hdmi.h>
#include <linux/media-bus-format.h>
#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "dw_hdmi.h"
#include "rockchip_dw_hdmi.h"

#define HDMI_SEL_LCDC(x, bit)  ((((x) & 1) << bit) | (1 << (16 + bit)))
#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define RK3228_IO_3V_DOMAIN              ((7 << 4) | (7 << (4 + 16)))
#define RK3328_IO_3V_DOMAIN              (7 << (9 + 16))
#define RK3328_IO_5V_DOMAIN              ((7 << 9) | (3 << (9 + 16)))
#define RK3328_IO_CTRL_BY_HDMI           ((1 << 13) | (1 << (13 + 16)))
#define RK3328_IO_DDC_IN_MSK             ((3 << 10) | (3 << (10 + 16)))
#define RK3228_IO_DDC_IN_MSK             ((3 << 13) | (3 << (13 + 16)))
#define RK3228_GRF_SOC_CON2              0x0408
#define RK3228_GRF_SOC_CON6              0x0418
#define RK3328_GRF_SOC_CON2              0x0408
#define RK3328_GRF_SOC_CON3              0x040c
#define RK3328_GRF_SOC_CON4              0x0410

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	},  {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	},  {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	},  {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg_420[] = {
	{
		30666000, {
			{ 0x00b7, 0x0000 },
			{ 0x2157, 0x0000 },
			{ 0x40f7, 0x0000 },
		},
	},  {
		92000000, {
			{ 0x00b7, 0x0000 },
			{ 0x2143, 0x0001 },
			{ 0x40a3, 0x0001 },
		},
	},  {
		184000000, {
			{ 0x0073, 0x0001 },
			{ 0x2146, 0x0002 },
			{ 0x4062, 0x0002 },
		},
	},  {
		340000000, {
			{ 0x0052, 0x0003 },
			{ 0x214d, 0x0003 },
			{ 0x4065, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x0041, 0x0003 },
			{ 0x3b4d, 0x0003 },
			{ 0x5a65, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl rockchip_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		600000000, { 0x0000, 0x0000, 0x0000 },
	},  {
		~0UL,      { 0x0000, 0x0000, 0x0000},
	}
};

static const struct dw_hdmi_phy_config rockchip_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 165000000, 0x802b, 0x0004, 0x0209},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ 594000000, 0x8039, 0x0000, 0x019d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static unsigned int drm_rk_select_color(struct hdmi_edid_data *edid_data,
					struct base_screen_info *screen_info,
					enum dw_hdmi_devtype dev_type)
{

	struct drm_display_info *info = &edid_data->display_info;
	struct drm_display_mode *mode = edid_data->preferred_mode;
	int max_tmds_clock = info->max_tmds_clock;
	bool support_dc = false;
	bool mode_420 = drm_mode_is_420(info, mode);
	unsigned int color_depth = 8;
	unsigned int base_color = DRM_HDMI_OUTPUT_YCBCR444;
	unsigned int color_format = DRM_HDMI_OUTPUT_DEFAULT_RGB;
	unsigned long tmdsclock, pixclock = mode->clock;

	if (screen_info)
		base_color = screen_info->format;

	switch (base_color) {
	case DRM_HDMI_OUTPUT_YCBCR_HQ:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			color_format = DRM_HDMI_OUTPUT_YCBCR444;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			color_format = DRM_HDMI_OUTPUT_YCBCR422;
		else if (mode_420)
			color_format = DRM_HDMI_OUTPUT_YCBCR420;
		break;
	case DRM_HDMI_OUTPUT_YCBCR_LQ:
		if (mode_420)
			color_format = DRM_HDMI_OUTPUT_YCBCR420;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			color_format = DRM_HDMI_OUTPUT_YCBCR422;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			color_format = DRM_HDMI_OUTPUT_YCBCR444;
		break;
	case DRM_HDMI_OUTPUT_YCBCR420:
		if (mode_420)
			color_format = DRM_HDMI_OUTPUT_YCBCR420;
		break;
	case DRM_HDMI_OUTPUT_YCBCR422:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			color_format = DRM_HDMI_OUTPUT_YCBCR422;
		break;
	case DRM_HDMI_OUTPUT_YCBCR444:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			color_format = DRM_HDMI_OUTPUT_YCBCR444;
		break;
	case DRM_HDMI_OUTPUT_DEFAULT_RGB:
	default:
		break;
	}

	if (color_format == DRM_HDMI_OUTPUT_DEFAULT_RGB &&
	    info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_30)
		support_dc = true;
	if (color_format == DRM_HDMI_OUTPUT_YCBCR444 &&
	    (info->edid_hdmi_dc_modes &
	     (DRM_EDID_HDMI_DC_Y444 | DRM_EDID_HDMI_DC_30)))
		support_dc = true;
	if (color_format == DRM_HDMI_OUTPUT_YCBCR422)
		support_dc = true;
	if (color_format == DRM_HDMI_OUTPUT_YCBCR420 &&
	    info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
		support_dc = true;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		pixclock *= 2;

	if (screen_info && screen_info->depth == 10)
		color_depth = screen_info->depth;

	if (color_format == DRM_HDMI_OUTPUT_YCBCR422 || color_depth == 8)
		tmdsclock = pixclock;
	else
		tmdsclock = pixclock * color_depth / 8;

	if (color_format == DRM_HDMI_OUTPUT_YCBCR420)
		tmdsclock /= 2;

	if (!max_tmds_clock)
		max_tmds_clock = 340000;

	switch (dev_type) {
	case RK3368_HDMI:
		max_tmds_clock = min(max_tmds_clock, 340000);
		break;
	case RK3328_HDMI:
	case RK3228_HDMI:
		max_tmds_clock = min(max_tmds_clock, 371250);
		break;
	default:
		max_tmds_clock = min(max_tmds_clock, 594000);
		break;
	}

	if (tmdsclock > max_tmds_clock) {
		if (max_tmds_clock >= 594000) {
			color_depth = 8;
		} else if (max_tmds_clock > 340000) {
			if (drm_mode_is_420(info, mode))
				color_format = DRM_HDMI_OUTPUT_YCBCR420;
		} else {
			color_depth = 8;
			if (drm_mode_is_420(info, mode))
				color_format = DRM_HDMI_OUTPUT_YCBCR420;
		}
	}

	if (color_depth > 8 && support_dc) {
		if (dev_type == RK3288_HDMI)
			return MEDIA_BUS_FMT_RGB101010_1X30;
		switch (color_format) {
		case DRM_HDMI_OUTPUT_YCBCR444:
			return MEDIA_BUS_FMT_YUV10_1X30;
		case DRM_HDMI_OUTPUT_YCBCR422:
			return MEDIA_BUS_FMT_UYVY10_1X20;
		case DRM_HDMI_OUTPUT_YCBCR420:
			return MEDIA_BUS_FMT_UYYVYY10_0_5X30;
		default:
			return MEDIA_BUS_FMT_RGB101010_1X30;
		}
	} else {
		if (dev_type == RK3288_HDMI)
			return MEDIA_BUS_FMT_RGB888_1X24;
		switch (color_format) {
		case DRM_HDMI_OUTPUT_YCBCR444:
			return MEDIA_BUS_FMT_YUV8_1X24;
		case DRM_HDMI_OUTPUT_YCBCR422:
			return MEDIA_BUS_FMT_UYVY8_1X16;
		case DRM_HDMI_OUTPUT_YCBCR420:
			return MEDIA_BUS_FMT_UYYVYY8_0_5X24;
		default:
			return MEDIA_BUS_FMT_RGB888_1X24;
		}
	}
	return 0;
}

void drm_rk_selete_output(struct hdmi_edid_data *edid_data,
			  unsigned int *bus_format,
			  struct overscan *overscan,
			  enum dw_hdmi_devtype dev_type)
{

	int ret, i, screen_size;
//	struct base_disp_info base_parameter;
	const struct base_overscan *scan;
	struct base_screen_info *screen_info = NULL;
	int max_scan = 100;
	int min_scan = 51;
//	struct blk_desc *dev_desc;
//	disk_partition_t part_info;
//	char baseparameter_buf[8 * RK_BLK_SIZE] __aligned(ARCH_DMA_MINALIGN);

	printf("in drm_rk_selete_output\n");
	overscan->left_margin = max_scan;
	overscan->right_margin = max_scan;
	overscan->top_margin = max_scan;
	overscan->bottom_margin = max_scan;

	if (dev_type == RK3288_HDMI)
		*bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	else
		*bus_format = MEDIA_BUS_FMT_YUV8_1X24;

	*bus_format = MEDIA_BUS_FMT_RGB888_1X24;
#if 0
	dev_desc = rockchip_get_bootdev();
	if (!dev_desc) {
		printf("%s: Could not find device\n", __func__);
		return;
	}

	if (part_get_info_by_name(dev_desc, "baseparameter", &part_info) < 0) {
		printf("Could not find baseparameter partition\n");
		return;
	}
#endif
	return;
}

void inno_dw_hdmi_set_domain(void *grf, int status)
{
	if (status) {
		writel(RK3328_IO_5V_DOMAIN, grf + RK3328_GRF_SOC_CON4);
		printf("if inno_dw_hdmi_set_domain\n");
	} else {
		writel(RK3328_IO_3V_DOMAIN, grf + RK3328_GRF_SOC_CON4);
		printf("else inno_dw_hdmi_set_domain\n");
	}
}

void dw_hdmi_set_iomux(void *grf, int dev_type)
{
	switch (dev_type) {
	case RK3328_HDMI:
		printf("dw_hdmi_set_iomux: RK3328_HDMI\n");
		writel(RK3328_IO_DDC_IN_MSK, grf + RK3328_GRF_SOC_CON2);
		writel(RK3328_IO_CTRL_BY_HDMI, grf + RK3328_GRF_SOC_CON3);
		break;
	case RK3228_HDMI:
		writel(RK3228_IO_3V_DOMAIN, grf + RK3228_GRF_SOC_CON6);
		writel(RK3228_IO_DDC_IN_MSK, grf + RK3228_GRF_SOC_CON2);
		break;
	default:
		break;
	}
}

static const struct dw_hdmi_phy_ops inno_dw_hdmi_phy_ops = {
	.init = inno_dw_hdmi_phy_init,
	.disable = inno_dw_hdmi_phy_disable,
	.read_hpd = inno_dw_hdmi_phy_read_hpd,
	.mode_valid = inno_dw_hdmi_mode_valid,
};

static const struct rockchip_connector_funcs rockchip_dw_hdmi_funcs = {
	.init = rockchip_dw_hdmi_init,
	.deinit = rockchip_dw_hdmi_deinit,
	.prepare = rockchip_dw_hdmi_prepare,
	.enable = rockchip_dw_hdmi_enable,
	.disable = rockchip_dw_hdmi_disable,
	.get_timing = rockchip_dw_hdmi_get_timing,
	.detect = rockchip_dw_hdmi_detect,
	.get_edid = rockchip_dw_hdmi_get_edid,
};

const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.vop_sel_bit = 0,
	.grf_vop_sel_reg = 0,
	.phy_ops    = &inno_dw_hdmi_phy_ops,
	.phy_name   = "inno_dw_hdmi_phy2",
	.dev_type   = RK3328_HDMI,
};

static int rockchip_dw_hdmi_probe(struct udevice *dev)
{
	printf("in rockchip_dw_hdmi_probe\n");
	return 0;
}

static const struct rockchip_connector rk3328_dw_hdmi_data = {
	.funcs = &rockchip_dw_hdmi_funcs,
	.data = &rk3328_hdmi_drv_data,
};

static const struct udevice_id rockchip_dw_hdmi_ids[] = {
	{
	 .compatible = "rockchip,rk3328-dw-hdmi",
	 .data = (ulong)&rk3328_dw_hdmi_data,
	}, {}
};

U_BOOT_DRIVER(rockchip_dw_hdmi) = {
	.name = "rockchip_dw_hdmi",
	.id = UCLASS_DISPLAY,
	.of_match = rockchip_dw_hdmi_ids,
	.probe	= rockchip_dw_hdmi_probe,
};
