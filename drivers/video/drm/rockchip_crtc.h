/*
 * (C) Copyright 2008-2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef _ROCKCHIP_CRTC_H_
#define _ROCKCHIP_CRTC_H_

struct rockchip_crtc {
	const struct rockchip_crtc_funcs *funcs;
	const void *data;
	struct drm_display_mode active_mode;
	bool hdmi_hpd : 1;
	bool active : 1;
};

struct rockchip_crtc_funcs {
	int (*preinit)(struct display_state *state);
	int (*init)(struct display_state *state);
	void (*deinit)(struct display_state *state);
	int (*set_plane)(struct display_state *state);
	int (*prepare)(struct display_state *state);
	int (*enable)(struct display_state *state);
	int (*disable)(struct display_state *state);
	void (*unprepare)(struct display_state *state);
	int (*fixup_dts)(struct display_state *state, void *blob);
	int (*send_mcu_cmd)(struct display_state *state, u32 type, u32 value);
};

struct vop_data;
extern const struct rockchip_crtc_funcs rockchip_vop_funcs;
extern const struct vop_data rk322x_vop;
extern const struct vop_data rk3328_vop;
#endif
