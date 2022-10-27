/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <asm/unaligned.h>
#include <config.h>
#include <common.h>
#include <clk.h>
#include <display.h>
#include <errno.h>
#include <linux/libfdt.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <linux/hdmi.h>
#include <linux/list.h>
#include <linux/compat.h>
#include <linux/media-bus-format.h>
#include <malloc.h>
#include <video.h>
#include <video_rockchip.h>
#include <video_bridge.h>
#include <dm/device.h>
#include <dm/device_compat.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <asm/arch-rockchip/resource_img.h>

#include <log.h>
#include <regmap.h>
#include <reset.h>
#include <syscon.h>
#include <video.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch-rockchip/clock.h>
#include <linux/bitops.h>
#include <linux/err.h>

#include "bmp_helper.h"
#include "rockchip_display.h"
#include "rockchip_crtc.h"
#include "rockchip_connector.h"
#include "rockchip_bridge.h"
#include "rockchip_phy.h"
#include "rockchip_panel.h"
#include <dm.h>
#include <dm/of_access.h>
#include <dm/of_addr.h>
#include <dm/ofnode.h>
#include "logo.h"
#include <boot_rkimg.h>
#include <fs.h>
#include <asm/arch-rockchip/vop_rk3288.h>

#define DRIVER_VERSION	"v1.0.1"

/***********************************************************************
 *  Rockchip UBOOT DRM driver version
 *
 *  v1.0.0	: add basic version for rockchip drm driver(hjc)
 *  v1.0.1	: add much dsi update(hjc)
 *
 **********************************************************************/

#define RK_BLK_SIZE 512
#define BMP_PROCESSED_FLAG 8399

DECLARE_GLOBAL_DATA_PTR;

static LIST_HEAD(rockchip_display_list);
static LIST_HEAD(logo_cache_list);

static unsigned long memory_start;
static unsigned long memory_end;

/*
 * the phy types are used by different connectors in public.
 * The current version only has inno hdmi phy for hdmi and tve.
 */
enum public_use_phy {
	NONE,
	INNO_HDMI_PHY
};

enum {
        PORT_DIR_IN,
        PORT_DIR_OUT,
};

/* save public phy data */
struct public_phy_data {
	const struct rockchip_phy *phy_drv;
	int phy_node;
	int public_phy_type;
	bool phy_init;
};

struct rk3328_vop_priv {
        void *grf;
        void *regs;
};

/**
 * drm_mode_set_crtcinfo - set CRTC modesetting timing parameters
 * @p: mode
 * @adjust_flags: a combination of adjustment flags
 *
 * Setup the CRTC modesetting timing parameters for @p, adjusting if necessary.
 *
 * - The CRTC_INTERLACE_HALVE_V flag can be used to halve vertical timings of
 *   interlaced modes.
 * - The CRTC_STEREO_DOUBLE flag can be used to compute the timings for
 *   buffers containing two eyes (only adjust the timings when needed, eg. for
 *   "frame packing" or "side by side full").
 * - The CRTC_NO_DBLSCAN and CRTC_NO_VSCAN flags request that adjustment *not*
 *   be performed for doublescan and vscan > 1 modes respectively.
 */
void drm_mode_set_crtcinfo(struct drm_display_mode *p, int adjust_flags)
{
        printf("in drm_mode_set_crtcinfo\n");
        if ((p == NULL) || ((p->type & DRM_MODE_TYPE_CRTC_C) == DRM_MODE_TYPE_BUILTIN))
                return;

        if (p->flags & DRM_MODE_FLAG_DBLCLK)
                p->crtc_clock = 2 * p->clock;
        else
                p->crtc_clock = p->clock;
        p->crtc_hdisplay = p->hdisplay;
        p->crtc_hsync_start = p->hsync_start;
        p->crtc_hsync_end = p->hsync_end;
        p->crtc_htotal = p->htotal;
        p->crtc_hskew = p->hskew;
        p->crtc_vdisplay = p->vdisplay;
        p->crtc_vsync_start = p->vsync_start;
        p->crtc_vsync_end = p->vsync_end;
        p->crtc_vtotal = p->vtotal;

	if (p->flags & DRM_MODE_FLAG_INTERLACE) {
                if (adjust_flags & CRTC_INTERLACE_HALVE_V) {
                        p->crtc_vdisplay /= 2;
                        p->crtc_vsync_start /= 2;
                        p->crtc_vsync_end /= 2;
                        p->crtc_vtotal /= 2;
                }
        }

        if (!(adjust_flags & CRTC_NO_DBLSCAN)) {
                if (p->flags & DRM_MODE_FLAG_DBLSCAN) {
                        p->crtc_vdisplay *= 2;
                        p->crtc_vsync_start *= 2;
                        p->crtc_vsync_end *= 2;
                        p->crtc_vtotal *= 2;
                }
        }

        if (!(adjust_flags & CRTC_NO_VSCAN)) {
                if (p->vscan > 1) {
                        p->crtc_vdisplay *= p->vscan;
                        p->crtc_vsync_start *= p->vscan;
                        p->crtc_vsync_end *= p->vscan;
                        p->crtc_vtotal *= p->vscan;
                }
        }

        if (adjust_flags & CRTC_STEREO_DOUBLE) {
                unsigned int layout = p->flags & DRM_MODE_FLAG_3D_MASK;

                switch (layout) {
                case DRM_MODE_FLAG_3D_FRAME_PACKING:
                        p->crtc_clock *= 2;
                        p->crtc_vdisplay += p->crtc_vtotal;
                        p->crtc_vsync_start += p->crtc_vtotal;
                        p->crtc_vsync_end += p->crtc_vtotal;
                        p->crtc_vtotal += p->crtc_vtotal;
                        break;
                }
        }

        p->crtc_vblank_start = min(p->crtc_vsync_start, p->crtc_vdisplay);
        p->crtc_vblank_end = max(p->crtc_vsync_end, p->crtc_vtotal);
        p->crtc_hblank_start = min(p->crtc_hsync_start, p->crtc_hdisplay);
        p->crtc_hblank_end = max(p->crtc_hsync_end, p->crtc_htotal);
}

static int display_get_timing_from_dts(struct panel_state *panel_state,
                                       struct drm_display_mode *mode)
{
        struct rockchip_panel *panel = panel_state->panel;
        int phandle;
        int hactive, vactive, pixelclock;
        int hfront_porch, hback_porch, hsync_len;
        int vfront_porch, vback_porch, vsync_len;
        int val, flags = 0;
        ofnode timing, native_mode;

        printf("in display_get_timing_from_dts\n");
        timing = dev_read_subnode(panel->dev, "display-timings");
        if (!ofnode_valid(timing))
                return -ENODEV;

        native_mode = ofnode_find_subnode(timing, "timing");
        if (!ofnode_valid(native_mode)) {
                phandle = ofnode_read_u32_default(timing, "native-mode", -1);
                native_mode = np_to_ofnode(of_find_node_by_phandle(phandle));
                if (!ofnode_valid(native_mode)) {
                        printf("failed to get display timings from DT\n");
                        return -ENXIO;
                }
        }

#define FDT_GET_INT(val, name) \
        val = ofnode_read_s32_default(native_mode, name, -1); \
        if (val < 0) { \
                printf("Can't get %s\n", name); \
                return -ENXIO; \
        }

#define FDT_GET_INT_DEFAULT(val, name, default) \
        val = ofnode_read_s32_default(native_mode, name, default);

        FDT_GET_INT(hactive, "hactive");
        FDT_GET_INT(vactive, "vactive");
        FDT_GET_INT(pixelclock, "clock-frequency");
        FDT_GET_INT(hsync_len, "hsync-len");
        FDT_GET_INT(hfront_porch, "hfront-porch");
        FDT_GET_INT(hback_porch, "hback-porch");
        FDT_GET_INT(vsync_len, "vsync-len");
        FDT_GET_INT(vfront_porch, "vfront-porch");
        FDT_GET_INT(vback_porch, "vback-porch");
        FDT_GET_INT(val, "hsync-active");
        flags |= val ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
        FDT_GET_INT(val, "vsync-active");
        flags |= val ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
        FDT_GET_INT(val, "pixelclk-active");
        flags |= val ? DRM_MODE_FLAG_PPIXDATA : 0;

	FDT_GET_INT_DEFAULT(val, "screen-rotate", 0);
        if (val == DRM_MODE_FLAG_XMIRROR) {
                flags |= DRM_MODE_FLAG_XMIRROR;
        } else if (val == DRM_MODE_FLAG_YMIRROR) {
                flags |= DRM_MODE_FLAG_YMIRROR;
        } else if (val == DRM_MODE_FLAG_XYMIRROR) {
                flags |= DRM_MODE_FLAG_XMIRROR;
                flags |= DRM_MODE_FLAG_YMIRROR;
        }
        mode->hdisplay = hactive;
        mode->hsync_start = mode->hdisplay + hfront_porch;
        mode->hsync_end = mode->hsync_start + hsync_len;
        mode->htotal = mode->hsync_end + hback_porch;

        mode->vdisplay = vactive;
        mode->vsync_start = mode->vdisplay + vfront_porch;
        mode->vsync_end = mode->vsync_start + vsync_len;
        mode->vtotal = mode->vsync_end + vback_porch;

        mode->clock = pixelclock / 1000;
        mode->flags = flags;

        return 0;
}

static int display_get_timing(struct display_state *state)
{
        struct connector_state *conn_state = &state->conn_state;
        struct drm_display_mode *mode = &conn_state->mode;
        const struct drm_display_mode *m;
        struct panel_state *panel_state = &state->panel_state;
        const struct rockchip_panel *panel = panel_state->panel;

        printf("in display_get_timing\n");
        if (dev_of_valid(panel->dev) &&
            !display_get_timing_from_dts(panel_state, mode)) {
                printf("Using display timing dts\n");
                return 0;
        }

        if (panel->data) {
                m = (const struct drm_display_mode *)panel->data;
                memcpy(mode, m, sizeof(*m));
                printf("Using display timing from compatible panel driver\n");
                return 0;
        }

        return -ENODEV;
}

static int display_set_plane(struct display_state *state)
{
        struct crtc_state *crtc_state = &state->crtc_state;
        const struct rockchip_crtc *crtc = crtc_state->crtc;
        const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
        int ret;

        if (!state->is_init)
                return -EINVAL;
        printf("in display_set_plane\n");

        if (crtc_funcs->set_plane) {
		printf("before crtc_funcs->set_plane\n");
                ret = crtc_funcs->set_plane(state);
                if (ret)
                        return ret;
        }

        return 0;
}

static int rk3328_display_enable(struct display_state *state)
{
        struct connector_state *conn_state = &state->conn_state;
        const struct rockchip_connector *conn = conn_state->connector;
        const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
        struct crtc_state *crtc_state = &state->crtc_state;
        const struct rockchip_crtc *crtc = crtc_state->crtc;
        const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
        struct panel_state *panel_state = &state->panel_state;

        printf("in display_enable\n");
        if (!state->is_init)
                return -EINVAL;

        if (state->is_enable)
                return 0;

        if (crtc_funcs->prepare)
                crtc_funcs->prepare(state);

        if (conn_funcs->prepare)
                conn_funcs->prepare(state);
#if 0
        if (conn_state->bridge)
                rockchip_bridge_pre_enable(conn_state->bridge);

        if (panel_state->panel)
                rockchip_panel_prepare(panel_state->panel);
#endif
        if (crtc_funcs->enable)
                crtc_funcs->enable(state);

        if (conn_funcs->enable)
                conn_funcs->enable(state);
#if 0
        if (conn_state->bridge)
                rockchip_bridge_enable(conn_state->bridge);

        if (panel_state->panel)
                rockchip_panel_enable(panel_state->panel);
#endif
        state->is_enable = true;

        return 0;
}

static int rockchip_display_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
        struct panel_state *panel_state = &state->panel_state;
        const struct rockchip_connector *conn = conn_state->connector;
        const struct rockchip_connector_funcs *conn_funcs = conn->funcs;
        struct crtc_state *crtc_state = &state->crtc_state;
        struct rockchip_crtc *crtc = crtc_state->crtc;
        const struct rockchip_crtc_funcs *crtc_funcs = crtc->funcs;
        struct drm_display_mode *mode = &conn_state->mode;
        int ret = 0;
        static bool __print_once = false;
#if defined(CONFIG_I2C_EDID)
        int bpc;
#endif

	printf("rockchip_display_init\n");
	if (!__print_once) {
                __print_once = true;
                printf("Rockchip UBOOT DRM driver version: %s\n", DRIVER_VERSION);
        }

	if (state->is_init)
                return 0;

	if (!crtc_funcs) {
                printf("failed to find crtc functions\n");
                return -ENXIO;
        }

	if (!conn_funcs) {
                printf("failed to find connector functions\n");
                return -ENXIO;
        }

	if (crtc_state->crtc->active &&
            memcmp(&crtc_state->crtc->active_mode, &conn_state->mode,
                   sizeof(struct drm_display_mode))) {
                printf("%s has been used for output type: %d, mode: %dx%dp%d\n",
                        crtc_state->dev->name,
                        crtc_state->crtc->active_mode.type,
                        crtc_state->crtc->active_mode.hdisplay,
                        crtc_state->crtc->active_mode.vdisplay,
                        crtc_state->crtc->active_mode.vrefresh);
                return -ENODEV;
        }

	if (crtc_funcs->preinit) {
		printf("crtc_funcs->preinit\n");
                ret = crtc_funcs->preinit(state);
                if (ret)
                        return ret;
	}

	if (panel_state->panel) {
		printf("panel_state->panel\n");
                rockchip_panel_init(panel_state->panel);
	}

	if (conn_funcs->init) {
                printf("conn_funcs->init\n");
                ret = conn_funcs->init(state);
                if (ret)
                        goto deinit;
        }

	if (conn_state->phy) {
		printf("conn_state->phy\n");
                rockchip_phy_init(conn_state->phy);
	}

	if (conn_funcs->detect) {
                printf("conn_funcs->detect\n");
                ret = conn_funcs->detect(state);
#if defined(CONFIG_ROCKCHIP_DRM_TVE) || defined(CONFIG_DRM_ROCKCHIP_RK1000)
                if (conn_state->type == DRM_MODE_CONNECTOR_HDMIA)
                        crtc->hdmi_hpd = ret;
#endif
                if (!ret)
                        goto deinit;
        }

	if (panel_state->panel) {
                printf("down panel_state->panel\n");
                ret = display_get_timing(state);
#if defined(CONFIG_I2C_EDID)
                if (ret < 0 && conn_funcs->get_edid) {
                        rockchip_panel_prepare(panel_state->panel);

                        ret = conn_funcs->get_edid(state);
                        if (!ret) {
                                ret = edid_get_drm_mode((void *)&conn_state->edid,
                                                        sizeof(conn_state->edid),
                                                        mode, &bpc);
                                if (!ret)
                                        edid_print_info((void *)&conn_state->edid);
                        }
                }
#endif
        } else if (conn_state->bridge) {
                printf("down else if panel_state->panel\n");
                ret = video_bridge_read_edid(conn_state->bridge->dev,
                                             conn_state->edid, EDID_SIZE);
                if (ret > 0) {
#if defined(CONFIG_I2C_EDID)
                        ret = edid_get_drm_mode(conn_state->edid, ret, mode,
                                                &bpc);
                        if (!ret)
                                edid_print_info((void *)&conn_state->edid);
#endif
                } else {
                        printf("before video_bridge_get_timing\n");
                        ret = video_bridge_get_timing(conn_state->bridge->dev);
                }
        } else if (conn_funcs->get_timing) {
                printf("before conn_funcs->get_timing\n");
                ret = conn_funcs->get_timing(state);
        } else if (conn_funcs->get_edid) {
                printf("conn_funcs->get_edid\n");
                ret = conn_funcs->get_edid(state);
#if defined(CONFIG_I2C_EDID)
                if (!ret) {
                        printf("CONFIG_I2C_EDID\n");
                        ret = edid_get_drm_mode((void *)&conn_state->edid,
                                                sizeof(conn_state->edid), mode,
                                                &bpc);
                        if (!ret)
                                edid_print_info((void *)&conn_state->edid);
                }
#endif
        }

	if (ret) {
                printf("going to deinit\n");
                goto deinit;
        }

        printf("Detailed mode clock %u kHz, flags[%x]\n"
               "    H: %04d %04d %04d %04d\n"
               "    V: %04d %04d %04d %04d\n"
               "bus_format: %x\n",
               mode->clock, mode->flags,
               mode->hdisplay, mode->hsync_start,
               mode->hsync_end, mode->htotal,
               mode->vdisplay, mode->vsync_start,
               mode->vsync_end, mode->vtotal,
               conn_state->bus_format);

                printf("before drm_mode_set_crtcinfo\n");
        drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);

        if (crtc_funcs->init) {
                printf("down crtc_funcs->init\n");
                ret = crtc_funcs->init(state);
                if (ret)
                        goto deinit;
        }
        state->is_init = 1;

        crtc_state->crtc->active = true;
        memcpy(&crtc_state->crtc->active_mode,
               &conn_state->mode, sizeof(struct drm_display_mode));

                printf("returning 0\n");
        return 0;

deinit:
	if (conn_funcs->deinit)
                conn_funcs->deinit(state);

	return 0;
}

int rockchip_enable_display(void)
{
        struct display_state *state;
	struct crtc_state *crtc_state;
	struct connector_state *conn_state;
	int hdisplay, vdisplay, ret;

        printf("in rockchip_enable_display\n");
        list_for_each_entry(state, &rockchip_display_list, head) {
                        ret = rockchip_display_init(state);
        

	if (!state->is_init || ret)
                return -ENODEV;

	crtc_state = &state->crtc_state;
	conn_state = &state->conn_state;

	crtc_state->format = ROCKCHIP_FMT_RGB565;

	hdisplay = conn_state->mode.hdisplay;
        vdisplay = conn_state->mode.vdisplay;

	crtc_state->src_x = 0;
        crtc_state->src_y = 0;

	printf("in else logo>mode == ROCKCHIP_DISPLAY_FULLSCREEN\n");

	if (crtc_state->src_w >= hdisplay) {
		printf("in crtc_state->src_w >= hdisplay\n");
		crtc_state->crtc_x = 0;
		crtc_state->crtc_w = hdisplay;
	} else {
		printf("in else crtc_state->src_w >= hdisplay\n");
		crtc_state->crtc_x = (hdisplay - crtc_state->src_w) / 2;
		crtc_state->crtc_w = crtc_state->src_w;
	}

	if (crtc_state->src_h >= vdisplay) {
		printf("in crtc_state->src_h >= vdisplay\n");
		crtc_state->crtc_y = 0;
		crtc_state->crtc_h = vdisplay;
	} else {
		printf("in else crtc_state->src_w >= hdisplay\n");
		crtc_state->crtc_y = (vdisplay - crtc_state->src_h) / 2;
		crtc_state->crtc_h = crtc_state->src_h;
	}

	printf("before display_set_plane\n");
        display_set_plane(state);
        printf("before display_enable\n");
        rk3328_display_enable(state);
	}

	return 0;
}

/* check which kind of public phy does connector use */
static int check_public_use_phy(struct display_state *state)
{
        int ret = NONE;
        printf("check_public_use_phy\n");
#ifdef CONFIG_ROCKCHIP_INNO_HDMI_PHY
        struct connector_state *conn_state = &state->conn_state;

        if (!strncmp(dev_read_name(conn_state->dev), "tve", 3) ||
            !strncmp(dev_read_name(conn_state->dev), "hdmi", 4))
                ret = INNO_HDMI_PHY;
#endif
        printf("check_public_use_phy:%d\n", ret);
        return ret;
}

/**
 * drm_mode_max_resolution_filter - mark modes out of vop max resolution
 * @edid_data: structure store mode list
 * @max_output: vop max output resolution
 */
void drm_mode_max_resolution_filter(struct hdmi_edid_data *edid_data,
                                    struct vop_rect *max_output)
{
        int i;

        printf("in drm_mode_max_resolution_filter\n");
        for (i = 0; i < edid_data->modes; i++) {
                if (edid_data->mode_buf[i].hdisplay > max_output->width ||
                    edid_data->mode_buf[i].vdisplay > max_output->height)
                        edid_data->mode_buf[i].invalid = true;
        }
}

int drm_mode_vrefresh(const struct drm_display_mode *mode)
{
        int refresh = 0;
        unsigned int calc_val;

        printf("drm_mode_vrefresh\n");
        if (mode->vrefresh > 0) {
                refresh = mode->vrefresh;
        } else if (mode->htotal > 0 && mode->vtotal > 0) {
                int vtotal;

                vtotal = mode->vtotal;
                /* work out vrefresh the value will be x1000 */
                calc_val = (mode->clock * 1000);
                calc_val /= mode->htotal;
                refresh = (calc_val + vtotal / 2) / vtotal;

                if (mode->flags & DRM_MODE_FLAG_INTERLACE)
                        refresh *= 2;
                if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
                        refresh /= 2;
                if (mode->vscan > 1)
                        refresh /= mode->vscan;
        }
        return refresh;
}

static void init_display_buffer(ulong base)
{
        printf("init_display_buffer\n");
        memory_start = base + DRM_ROCKCHIP_FB_SIZE;
        memory_end = memory_start;
}

/*
 * get public phy driver and initialize it.
 * The current version only has inno hdmi phy for hdmi and tve.
 */
static int get_public_phy(struct display_state *state,
                          struct public_phy_data *data)
{
        struct connector_state *conn_state = &state->conn_state;
        struct rockchip_phy *phy;
        struct udevice *dev;
        int ret = 0;

        printf("get_public_phy\n");
        switch (data->public_phy_type) {
        case INNO_HDMI_PHY:
#if defined(CONFIG_ROCKCHIP_RK3328)
                ret = uclass_get_device_by_name(UCLASS_PHY,
                                                "phy@ff430000", &dev);
#elif defined(CONFIG_ROCKCHIP_RK322X)
                ret = uclass_get_device_by_name(UCLASS_PHY,
                                                "hdmi-phy@12030000", &dev);
#else
                ret = -EINVAL;
#endif
                if (ret) {
                        printf("Warn: can't find phy driver\n");
                        return 0;
                }

                phy = (struct rockchip_phy *)dev_get_driver_data(dev);
                if (!phy) {
                        printf("failed to get phy driver\n");
                        return 0;
                }

                ret = rockchip_phy_init(phy);
                if (ret) {
                        printf("failed to init phy driver\n");
                        return ret;
                }
                conn_state->phy = phy;

                printf("inno hdmi phy init success, save it\n");
                data->phy_drv = conn_state->phy;
                data->phy_init = true;
                return 0;
        default:
                return -EINVAL;
        }
}

static int connector_phy_init(struct display_state *state,
                              struct public_phy_data *data)
{
        struct connector_state *conn_state = &state->conn_state;
        int type;

        printf("connector_phy_init\n");
        /* does this connector use public phy with others */
        type = check_public_use_phy(state);
        if (type == INNO_HDMI_PHY) {
                /* there is no public phy was initialized */
                if (!data->phy_init) {
                        printf("start get public phy\n");
                        data->public_phy_type = type;
                        if (get_public_phy(state, data)) {
                                printf("can't find correct public phy type\n");
                                free(data);
                                return -EINVAL;
                        }
                        return 0;
                }

                /* if this phy has been initialized, get it directly */
                conn_state->phy = (struct rockchip_phy *)data->phy_drv;
                return 0;
        }

        return 0;
}

static int get_crtc_id(ofnode connect)
{
        int phandle;
        struct device_node *remote;
        int val;

        printf("in get_crtc_id\n");
        phandle = ofnode_read_u32_default(connect, "remote-endpoint", -1);
        if (phandle < 0)
                goto err;
        remote = of_find_node_by_phandle(phandle);
        val = ofnode_read_u32_default(np_to_ofnode(remote), "reg", -1);
        if (val < 0)
                goto err;

        return val;
err:
        printf("Can't get crtc id, default set to id = 0\n");
        return 0;
}


static struct rockchip_bridge *rockchip_of_find_bridge(struct udevice *conn_dev)
{
        ofnode node, ports, port, ep;
        struct udevice *dev;
        int ret;

        printf("in rockchip_of_find_bridge\n");
        ports = dev_read_subnode(conn_dev, "ports");
        if (!ofnode_valid(ports))
                return NULL;

        ofnode_for_each_subnode(port, ports) {
                u32 reg;

                if (ofnode_read_u32(port, "reg", &reg))
                        continue;

                if (reg != PORT_DIR_OUT)
                        continue;

                ofnode_for_each_subnode(ep, port) {
                        ofnode _ep, _port, _ports;
                        uint phandle;

                        if (ofnode_read_u32(ep, "remote-endpoint", &phandle))
                                continue;

                        _ep = ofnode_get_by_phandle(phandle);
                        if (!ofnode_valid(_ep))
                                continue;

                        _port = ofnode_get_parent(_ep);
                        if (!ofnode_valid(_port))
                                continue;

                        _ports = ofnode_get_parent(_port);
                        if (!ofnode_valid(_ports))
                                continue;

                        node = ofnode_get_parent(_ports);
                        if (!ofnode_valid(node))
                                continue;

                        ret = uclass_get_device_by_ofnode(UCLASS_VIDEO_BRIDGE,
                                                          node, &dev);
                        if (!ret)
                                goto found;
                }
        }

	return NULL;

found:
        return (struct rockchip_bridge *)dev_get_driver_data(dev);
}

static struct udevice *rockchip_of_find_connector(ofnode endpoint)
{
	ofnode ep, port, ports, conn;
	uint phandle;
	struct udevice *dev;
	int ret;

	printf("in rockchip_of_find_connector\n");
	if (ofnode_read_u32(endpoint, "remote-endpoint", &phandle))
		return NULL;

	ep = ofnode_get_by_phandle(phandle);
	if (!ofnode_valid(ep) || !ofnode_is_available(ep))
		return NULL;

	port = ofnode_get_parent(ep);
	if (!ofnode_valid(port))
		return NULL;

	ports = ofnode_get_parent(port);
	if (!ofnode_valid(ports))
		return NULL;

	conn = ofnode_get_parent(ports);
	if (!ofnode_valid(conn) || !ofnode_is_available(conn))
		return NULL;

	printf("rockchip_of_find_connector: before UCLASS_DISPLAY\n");
	ret = uclass_get_device_by_ofnode(UCLASS_DISPLAY, conn, &dev);
	if (ret)
		return NULL;

	return dev;
}

static struct rockchip_phy *rockchip_of_find_phy(struct udevice *dev)
{
	struct udevice *phy_dev;
	int ret;

	printf("rockchip_of_find_phy\n");
	ret = uclass_get_device_by_phandle(UCLASS_PHY, dev, "phys", &phy_dev);
	if (ret)
		return NULL;

	return (struct rockchip_phy *)dev_get_driver_data(phy_dev);
	return 0;
}

static int rockchip_display_probe(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
        struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	int phandle;
	struct udevice *crtc_dev, *conn_dev;
	struct rockchip_crtc *crtc;
	const struct rockchip_connector *conn;
	struct rockchip_bridge *bridge = NULL;
	struct rockchip_phy *phy = NULL;
	struct display_state *s;
        int ret;
        ofnode node, route_node;
        struct device_node *port_node, *vop_node, *ep_node;
        struct public_phy_data *data;

        printf("in rockchip_display_probe\n");
        /* Before relocation we don't need to do anything */
        if (!(gd->flags & GD_FLG_RELOC))
                return 0;

        data = malloc(sizeof(struct public_phy_data));
        if (!data) {
                printf("failed to alloc phy data\n");
                return -ENOMEM;
        }
        data->phy_init = false;

	init_display_buffer(plat->base);

	route_node = dev_read_subnode_rk3328(dev, "route");
        if (!ofnode_valid(route_node)) {
		printf("returning\n");
                return -ENODEV;
	}

	ofnode_for_each_subnode(node, route_node) {
		printf("ofnode_for_each_subnode\n");
                if (!ofnode_is_available(node))
                        continue;
		phandle = ofnode_read_u32_default(node, "connect", -1);
                if (phandle < 0) {
                        printf("Warn: can't find connect node's handle\n");
                        continue;
                }
		ep_node = of_find_node_by_phandle(phandle);
                printf("ep_node:name: %s\n", ep_node->name);
                printf("ep_node:type: %s\n", ep_node->type);
                printf("ep_node:fullname: %s\n", ep_node->full_name);
                if (!ofnode_valid(np_to_ofnode(ep_node))) {
                        printf("Warn: can't find endpoint node from phandle\n");
                        continue;
                }
		port_node = of_get_parent(ep_node);
                printf("port_node:name: %s\n", port_node->name);
                printf("port_node:type: %s\n", port_node->type);
                printf("port_node:fullname: %s\n", port_node->full_name);
                if (!ofnode_valid(np_to_ofnode(port_node))) {
                        printf("Warn: can't find port node from phandle\n");
                        continue;
                }
                vop_node = of_get_parent(port_node);
                printf("vop_node:name: %s\n", vop_node->name);
                printf("vop_node:type: %s\n", vop_node->type);
                printf("vop_node:fullname: %s\n", vop_node->full_name);
                if (!ofnode_valid(np_to_ofnode(vop_node))) {
                        printf("Warn: can't find crtc node from phandle\n");
                        continue;
                }
		printf("rockchip_display_probe: before UCLASS_VIDEO_CRTC\n");
                ret = uclass_get_device_by_ofnode(UCLASS_VIDEO_CRTC,
                                                  np_to_ofnode(vop_node),
                                                  &crtc_dev);
		printf("rockchip_display_probe: after UCLASS_VIDEO_CRTC\n");
                if (ret) {
                        printf("Warn: can't find crtc driver %d\n", ret);
                        continue;
                }

		crtc = (struct rockchip_crtc *)dev_get_driver_data(crtc_dev);

		if (crtc)
                        printf("got rockchip_crtc\n");

		printf("rockchip_display_probe: before rockchip_of_find_connector\n");
		conn_dev = rockchip_of_find_connector(np_to_ofnode(ep_node));
                if (!conn_dev) {
                        printf("Warn: can't find connect driver\n");
                        continue;
                }

		conn = (const struct rockchip_connector *)dev_get_driver_data(conn_dev);

		if (conn) {
			printf("got rockchip connector: %lx\n", conn);
			printf("got rockchip connector fn: %lx\n", conn->funcs);
		}

		phy = rockchip_of_find_phy(conn_dev);

		bridge = rockchip_of_find_bridge(conn_dev);

		s = malloc(sizeof(*s));
                if (!s)
                        continue;

                memset(s, 0, sizeof(*s));

		INIT_LIST_HEAD(&s->head);

		s->conn_state.node = conn_dev->node_;
                s->conn_state.dev = conn_dev;
                s->conn_state.connector = conn;
		s->conn_state.phy = phy;
                s->conn_state.bridge = bridge;
                s->conn_state.overscan.left_margin = 100;
                s->conn_state.overscan.right_margin = 100;
                s->conn_state.overscan.top_margin = 100;
                s->conn_state.overscan.bottom_margin = 100;
                s->crtc_state.node = np_to_ofnode(vop_node);
                s->crtc_state.dev = crtc_dev;
                s->crtc_state.crtc = crtc;
                s->crtc_state.crtc_id = get_crtc_id(np_to_ofnode(ep_node));
                s->node = node;

		if (connector_phy_init(s, data)) {
                        printf("Warn: Failed to init phy drivers\n");
                        free(s);
                        continue;
                }
		list_add_tail(&s->head, &rockchip_display_list);
	}

	if (list_empty(&rockchip_display_list)) {
                printf("Failed to found available display route\n");
                return -ENODEV;
        }

        uc_priv->xsize = DRM_ROCKCHIP_FB_WIDTH;
        uc_priv->ysize = DRM_ROCKCHIP_FB_HEIGHT;
        uc_priv->bpix = VIDEO_BPP32;

        #ifdef CONFIG_DRM_ROCKCHIP_VIDEO_FRAMEBUFFER
        rockchip_show_fbbase(plat->base);
        video_set_flush_dcache(dev, true);
        #endif

        return 0;
}

int rockchip_display_bind(struct udevice *dev)
{
	printf("rockchip_display_bind \n");
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = DRM_ROCKCHIP_FB_SIZE + MEMORY_POOL_SIZE;

	return 0;
}

static const struct udevice_id rockchip_display_ids[] = {
	{ .compatible = "rockchip,display-subsystem" },
	{ }
};

U_BOOT_DRIVER(rockchip_display) = {
	.name	= "rockchip_display",
	.id	= UCLASS_VIDEO,
	.of_match = rockchip_display_ids,
	.bind	= rockchip_display_bind,
	.probe	= rockchip_display_probe,
};
