/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 BayLibre
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __MTK_CAMSV_H__
#define __MTK_CAMSV_H__

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/media-device.h>

/* MediaTek packed Bayer (MTISP fourccs of the ChromeOS MT8183 ISP drivers) */
#ifndef V4L2_PIX_FMT_MTISP_SRGGB10
#define V4L2_PIX_FMT_MTISP_SBGGR10  v4l2_fourcc('M', 'B', 'B', 'A')
#define V4L2_PIX_FMT_MTISP_SGBRG10  v4l2_fourcc('M', 'B', 'G', 'A')
#define V4L2_PIX_FMT_MTISP_SGRBG10  v4l2_fourcc('M', 'B', 'g', 'A')
#define V4L2_PIX_FMT_MTISP_SRGGB10  v4l2_fourcc('M', 'B', 'R', 'A')
#endif

#define IMG_MAX_WIDTH			5376U
#define IMG_MAX_HEIGHT			4032U
#define IMG_MIN_WIDTH			80U
#define IMG_MIN_HEIGHT			60U
#define MAX_SUPPORTED_NUM_PLANES	1U

#define MTK_CAM_CIO_PAD_SENINF		0U
#define MTK_CAM_CIO_PAD_VIDEO		1U
#define MTK_CAM_CIO_NUM_PADS		2U

struct mtk_cam_format_info {
	u32 code;
	u32 fourcc;
	u8 bpp;
	u32 w_factor;
	u32 module_en_pak;
	u32 fmt_sel;
	u32 pak;
	u32 imgo_stride;
};

const struct mtk_cam_format_info *mtk_cam_format_info_by_code(u32 code);


struct mtk_cam_dev_buffer {
	struct vb2_v4l2_buffer v4l2_buf;
	struct list_head list;
	dma_addr_t daddr;
};

/**
 * struct mtk_cam_vdev_desc - MTK camera device descriptor
 * @num_fmts: the number of supported node formats
 * @fmts: supported format
 * @frmsizes: supported V4L2 frame size number
 */
struct mtk_cam_vdev_desc {
	u8 num_fmts;
	const u32 *fmts;
	const struct v4l2_frmsizeenum *frmsizes;
};

/**
 * struct mtk_cam_video_device - MediaTek video device structure
 * @desc: The node description of video device
 * @vdev_pad: The media pad graph object of video device
 * @vdev: The video device instance
 * @vbq: A videobuf queue of video device
 * @vdev_lock: Serializes vb2 queue and video device operations
 * @format: The V4L2 format of video device
 * @fmtinfo: Information about the current format
 */
struct mtk_cam_video_device {
	const struct mtk_cam_vdev_desc *desc;

	struct media_pad vdev_pad;
	struct video_device vdev;
	struct vb2_queue vbq;

	/* Serializes vb2 queue and video device operations */
	struct mutex vdev_lock;

	struct v4l2_pix_format_mplane format;
	const struct mtk_cam_format_info *fmtinfo;
};

/**
 * struct mtk_cam_dev - MediaTek camera device structure.
 * @dev: Pointer to device.
 * @regs: Base address of CAMSV.
 * @regs_img0: Base address of CAMSV IMG0.
 * @regs_tg: Base address of CAMSV TG.
 * @num_clks: Number of clocks.
 * @clks: The clocks.
 * @irq: Irq fired when buffer is ready.
 * @conf: soc specific driver data.
 * @pipeline: Media pipeline information.
 * @subdev: The V4L2 sub-device instance.
 * @subdev_pads: Media pads of this sub-device.
 * @vdev: The video device node.
 * @seninf: Pointer to the seninf pad.
 * @sequence: Buffer sequence number.
 * @buf_list_lock: Protects the buffer list.
 * @buf_list: List head for the buffer list.
 */
struct mtk_cam_dev {
	struct device *dev;
	void __iomem *regs;
	void __iomem *regs_img0;
	void __iomem *regs_tg;

	unsigned int num_clks;
	struct clk_bulk_data *clks;
	unsigned int irq;
	const struct mtk_cam_conf *conf;

	struct media_pipeline pipeline;
	struct v4l2_subdev subdev;
	struct media_pad subdev_pads[MTK_CAM_CIO_NUM_PADS];
	struct mtk_cam_video_device vdev;
	struct media_pad *seninf;

	/*
	 * Cosmo (MT6771): CAMSV is the media master (no SENINF-side master as
	 * on MT8365): own v4l2/media device, async bind to the ChromeOS
	 * seninf, start seninf then the sensor (seninf does not start it).
	 */
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *seninf_sd;
	struct v4l2_subdev *sensor;
	void __iomem *base;
	void __iomem *larb6;

	/* FBC is not used on ISP 5.0 yet: frames with no queued buffer go here */
	void *dummy_vaddr;
	dma_addr_t dummy_daddr;
	size_t dummy_size;
	bool dummy_in_use;

	unsigned int imgo_err_count;
	unsigned int sequence;

	spinlock_t buf_list_lock;
	struct list_head buf_list;
};

/**
 * struct mtk_cam_conf - MediaTek camera configuration structure
 * @tg_sen_mode: TG sensor mode
 * @module_en: module enable
 * @imgo_con: dma control register
 * @imgo_con2: dma control register 2
 */
struct mtk_cam_conf {
	u32 camsv_ofs;
	u32 imgo_ofs;
	u32 tg_ofs;
	u32 tg_sen_mode;
	u32 module_en;
	u32 imgo_con;
	u32 imgo_con2;
};

int mtk_cam_dev_init(struct mtk_cam_dev *cam_dev);
void mtk_cam_dev_cleanup(struct mtk_cam_dev *cam_dev);
int mtk_cam_video_register(struct mtk_cam_dev *cam_dev);
void mtk_cam_video_unregister(struct mtk_cam_video_device *vdev);
void mtk_cam_vb2_return_all_buffers(struct mtk_cam_dev *cam,
				    enum vb2_buffer_state state);


#endif /* __MTK_CAMSV_H__ */
