// SPDX-License-Identifier: GPL-2.0
/*
 * mtk_camsv_video.c - V4L2 video node support
 *
 * Copyright (c) 2020 BayLibre
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mediabus.h>

#include "mtk_camsv.h"
#include "mtk_camsv_hw.h"
#include "mtk_camsv_reg.h"

static inline struct mtk_cam_video_device *
file_to_mtk_cam_video_device(struct file *__file)
{
	return container_of(video_devdata(__file),
			    struct mtk_cam_video_device, vdev);
}

static inline struct mtk_cam_video_device *
vb2_queue_to_mtk_cam_video_device(struct vb2_queue *vq)
{
	return container_of(vq, struct mtk_cam_video_device, vbq);
}

static inline struct mtk_cam_dev_buffer *
to_mtk_cam_dev_buffer(struct vb2_buffer *buf)
{
	return container_of(buf, struct mtk_cam_dev_buffer, v4l2_buf.vb2_buf);
}

/* -----------------------------------------------------------------------------
 * Format Information
 */

/*
 * Cosmo: seninf runs one pixel per clock, so no PAK_DBL_MODE_2_PIXELS.
 * - Sxxxx14 (16-bit words): PAK bypass (PAK_SEL) + 16-bit IMGO bus. The
 *   internal bus is 14 bits wide and MSB-aligned, so the 10-bit sensor data
 *   lands in bits 13:4 (colour bars: 1020 reads 0x3fc0). Verified 2026-11-19.
 * - MTISP 10-bit (default): PAK mode RAW_10, MediaTek packing (verified
 *   with the sensor colour bars).
 * - PAK disabled without PAK_SEL gives IMGO errors (no plain unpacked path).
 * - 8-bit: 10-bit input, PAK mode RAW_8 (EXPERIMENT: assumes the 8 MSBs).
 */
static const struct mtk_cam_format_info mtk_cam_format_info[] = {
	{
		.fourcc = V4L2_PIX_FMT_SRGGB14,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.bpp = 16,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_SEL,
		.pak = 0,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10
			| CAMSV_FMT_SEL_IMGO_BUS_SIZE_16,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_16,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR14,
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.bpp = 16,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_SEL,
		.pak = 0,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10
			| CAMSV_FMT_SEL_IMGO_BUS_SIZE_16,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_16,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG14,
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.bpp = 16,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_SEL,
		.pak = 0,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10
			| CAMSV_FMT_SEL_IMGO_BUS_SIZE_16,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_16,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG14,
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.bpp = 16,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_SEL,
		.pak = 0,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10
			| CAMSV_FMT_SEL_IMGO_BUS_SIZE_16,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_16,
	},
	{
		.fourcc = V4L2_PIX_FMT_MTISP_SRGGB10,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.bpp = 10,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_10,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_MTISP_SBGGR10,
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.bpp = 10,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_10,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_MTISP_SGBRG10,
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.bpp = 10,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_10,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_MTISP_SGRBG10,
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.bpp = 10,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_10,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.bpp = 8,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_8,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.bpp = 8,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_8,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.bpp = 8,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_8,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.bpp = 8,
		.w_factor = 1,
		.module_en_pak = CAMSV_MODULE_EN_PAK_EN,
		.pak = CAMSV_PAK_PAK_MODE_RAW_8,
		.fmt_sel = CAMSV_FMT_SEL_TG1_FMT_RAW_10,
		.imgo_stride = CAMSV_IMGO_SV_STRIDE_BUS_SIZE_EN
			| CAMSV_IMGO_SV_STRIDE_BUS_SIZE_32,
	},
};

static const struct mtk_cam_format_info *
mtk_cam_format_info_by_fourcc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mtk_cam_format_info); ++i) {
		const struct mtk_cam_format_info *info =
			&mtk_cam_format_info[i];

		if (info->fourcc == fourcc)
			return info;
	}

	return NULL;
}

const struct mtk_cam_format_info *
mtk_cam_format_info_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mtk_cam_format_info); ++i) {
		const struct mtk_cam_format_info *info =
			&mtk_cam_format_info[i];

		if (info->code == code)
			return info;
	}

	return NULL;
}

static bool mtk_cam_dev_find_fmt(const struct mtk_cam_vdev_desc *desc,
				 u32 format)
{
	unsigned int i;

	for (i = 0; i < desc->num_fmts; i++) {
		if (desc->fmts[i] == format)
			return true;
	}

	return false;
}

static void calc_bpl_size_pix_mp(const struct mtk_cam_format_info *fmtinfo,
				 struct v4l2_pix_format_mplane *pix_mp)
{
	unsigned int bpl;
	unsigned int i;

	bpl = ALIGN(DIV_ROUND_UP(pix_mp->width * fmtinfo->bpp, 8), 2);

	for (i = 0; i < pix_mp->num_planes; ++i) {
		pix_mp->plane_fmt[i].bytesperline = bpl;
		pix_mp->plane_fmt[i].sizeimage = bpl * pix_mp->height;
	}
}

static void mtk_cam_dev_load_default_fmt(struct mtk_cam_dev *cam)
{
	struct mtk_cam_video_device *vdev = &cam->vdev;
	struct v4l2_pix_format_mplane *fmt = &vdev->format;

	fmt->num_planes = MAX_SUPPORTED_NUM_PLANES;
	fmt->pixelformat = vdev->desc->fmts[0];
	fmt->width = IMG_MAX_WIDTH;
	fmt->height = IMG_MAX_HEIGHT;

	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->field = V4L2_FIELD_NONE;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	vdev->fmtinfo = mtk_cam_format_info_by_fourcc(fmt->pixelformat);

	calc_bpl_size_pix_mp(vdev->fmtinfo, fmt);
}

/* -----------------------------------------------------------------------------
 * VB2 Queue Operations
 */

static int mtk_cam_vb2_queue_setup(struct vb2_queue *vq,
				   unsigned int *num_buffers,
				   unsigned int *num_planes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct mtk_cam_video_device *vdev =
		vb2_queue_to_mtk_cam_video_device(vq);
	struct mtk_cam_dev *cam = vb2_get_drv_priv(vq);
	const struct v4l2_pix_format_mplane *fmt = &vdev->format;
	unsigned int size;

	size = fmt->plane_fmt[0].sizeimage;

	if (*num_planes == 0) {
		*num_planes = MAX_SUPPORTED_NUM_PLANES;
		sizes[0] = size;
	} else if (*num_planes != MAX_SUPPORTED_NUM_PLANES || sizes[0] < size) {
		return -EINVAL;
	}

	mtk_camsv_fbc_init(cam, *num_buffers);

	return 0;
}

static int mtk_cam_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_cam_video_device *vdev =
		vb2_queue_to_mtk_cam_video_device(vb->vb2_queue);
	struct mtk_cam_dev *cam = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_cam_dev_buffer *buf = to_mtk_cam_dev_buffer(vb);
	const struct v4l2_pix_format_mplane *fmt = &vdev->format;
	u32 size;
	unsigned int i;

	for (i = 0; i < vb->num_planes; i++) {
		size = fmt->plane_fmt[i].sizeimage;
		if (vb2_plane_size(vb, i) < size) {
			dev_err(cam->dev, "plane size is too small:%lu<%u\n",
				vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
	}

	buf->v4l2_buf.field = V4L2_FIELD_NONE;

	for (i = 0; i < vb->num_planes; i++) {
		size = fmt->plane_fmt[i].sizeimage;
		vb2_set_plane_payload(vb, i, size);
	}

	if (!buf->daddr)
		buf->daddr = vb2_dma_contig_plane_dma_addr(vb, 0);

	return 0;
}

static void mtk_cam_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_cam_dev *cam = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_cam_dev_buffer *buf = to_mtk_cam_dev_buffer(vb);
	unsigned long flags;

	/* Add the buffer into the tracking list */
	/*
	 * No FBC (see mtk_camsv_hw.c): while streaming, the PASS1_DON handler
	 * moves IMGO from the dummy to the next queued buffer. Writing the
	 * base here could race with a frame already going to the dummy.
	 */
	spin_lock_irqsave(&cam->buf_list_lock, flags);
	list_add_tail(&buf->list, &cam->buf_list);
	spin_unlock_irqrestore(&cam->buf_list_lock, flags);
}

void mtk_cam_vb2_return_all_buffers(struct mtk_cam_dev *cam,
				    enum vb2_buffer_state state)
{
	struct mtk_cam_dev_buffer *buf, *buf_prev;
	unsigned long flags;

	spin_lock_irqsave(&cam->buf_list_lock, flags);
	list_for_each_entry_safe(buf, buf_prev, &cam->buf_list, list) {
		buf->daddr = 0ULL;
		list_del(&buf->list);
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, state);
	}
	spin_unlock_irqrestore(&cam->buf_list_lock, flags);
}

static int mtk_cam_vb2_start_streaming(struct vb2_queue *vq,
				       unsigned int count)
{
	struct mtk_cam_dev *cam = vb2_get_drv_priv(vq);
	struct mtk_cam_video_device *vdev =
		vb2_queue_to_mtk_cam_video_device(vq);
	struct device *dev = cam->dev;
	const struct v4l2_pix_format_mplane *fmt = &vdev->format;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		dev_err(dev, "failed to get pm_runtime\n");
		return ret;
	}

	cam->dummy_size = fmt->plane_fmt[0].sizeimage;
	cam->dummy_vaddr = dma_alloc_coherent(dev, cam->dummy_size,
					      &cam->dummy_daddr, GFP_KERNEL);
	if (!cam->dummy_vaddr) {
		ret = -ENOMEM;
		goto fail_pm_put;
	}

	mtk_camsv_setup(cam, count, fmt->width, fmt->height,
			fmt->plane_fmt[0].bytesperline, vdev->fmtinfo);

	/* Enable CMOS and VF */
	mtk_camsv_cmos_vf_hw_enable(cam);

	/* Start streaming of the whole pipeline now. */
	ret = video_device_pipeline_start(&vdev->vdev, &cam->pipeline);
	if (ret)
		goto fail_pm_put;

	cam->sequence = (unsigned int)-1;

	/* Stream on seninf, then the sensor */
	ret = mtk_camsv_pipe_stream(cam, true);
	if (ret)
		goto fail_no_stream;

	return 0;

fail_no_stream:
	video_device_pipeline_stop(&vdev->vdev);
	mtk_cam_vb2_return_all_buffers(cam, VB2_BUF_STATE_QUEUED);
fail_pm_put:
	mtk_camsv_cmos_vf_hw_disable(cam);
	if (cam->dummy_vaddr) {
		dma_free_coherent(dev, cam->dummy_size, cam->dummy_vaddr,
				  cam->dummy_daddr);
		cam->dummy_vaddr = NULL;
	}
	pm_runtime_mark_last_busy(cam->dev);
	pm_runtime_put_autosuspend(cam->dev);

	return ret;
}

static void mtk_cam_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct mtk_cam_dev *cam = vb2_get_drv_priv(vq);
	struct mtk_cam_video_device *vdev =
		vb2_queue_to_mtk_cam_video_device(vq);

	/* Disable CMOS and VF */
	mtk_camsv_cmos_vf_hw_disable(cam);

	mtk_camsv_pipe_stream(cam, false);

	mtk_cam_vb2_return_all_buffers(cam, VB2_BUF_STATE_ERROR);

	if (cam->dummy_vaddr) {
		dma_free_coherent(cam->dev, cam->dummy_size, cam->dummy_vaddr,
				  cam->dummy_daddr);
		cam->dummy_vaddr = NULL;
	}

	video_device_pipeline_stop(&vdev->vdev);

	pm_runtime_mark_last_busy(cam->dev);
	pm_runtime_put_autosuspend(cam->dev);
}

static const struct vb2_ops mtk_cam_vb2_ops = {
	.queue_setup = mtk_cam_vb2_queue_setup,
	.buf_prepare = mtk_cam_vb2_buf_prepare,
	.buf_queue = mtk_cam_vb2_buf_queue,
	.start_streaming = mtk_cam_vb2_start_streaming,
	.stop_streaming = mtk_cam_vb2_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdev Operations
 */

static inline struct mtk_cam_dev *to_mtk_cam_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mtk_cam_dev, subdev);
}


static int mtk_cam_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state)
{
	static const struct v4l2_mbus_framefmt def_format = {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.width = IMG_MAX_WIDTH,
		.height = IMG_MAX_HEIGHT,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.xfer_func = V4L2_XFER_FUNC_DEFAULT,
		.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
		.quantization = V4L2_QUANTIZATION_DEFAULT,
	};
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	for (i = 0; i < sd->entity.num_pads; i++) {
		format = v4l2_subdev_state_get_format(sd_state, i);
		*format = def_format;
	}

	return 0;
}

static int mtk_cam_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(mtk_cam_format_info))
		return -EINVAL;

	code->code = mtk_cam_format_info[code->index].code;

	return 0;
}

static int mtk_cam_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;
	unsigned int i;

	/*
	 * We only support pass-through mode, the format on source pads can't
	 * be modified.
	 */
	if (fmt->pad != MTK_CAM_CIO_PAD_SENINF)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(mtk_cam_format_info); ++i) {
		if (mtk_cam_format_info[i].code == fmt->format.code)
			break;
	}

	if (i == ARRAY_SIZE(mtk_cam_format_info))
		fmt->format.code = mtk_cam_format_info[0].code;

	format = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	format->width = clamp_t(u32, fmt->format.width,
				IMG_MIN_WIDTH, IMG_MAX_WIDTH);
	format->height = clamp_t(u32, fmt->format.height,
				 IMG_MIN_HEIGHT, IMG_MAX_HEIGHT);
	format->code = fmt->format.code;

	fmt->format = *format;

	/* Propagate the format to the source pad. */
	format = v4l2_subdev_state_get_format(sd_state, MTK_CAM_CIO_PAD_VIDEO);
	format->width = fmt->format.width;
	format->height = fmt->format.height;
	format->code = fmt->format.code;

	return 0;
}

static int mtk_cam_subdev_registered(struct v4l2_subdev *sd)
{
	struct mtk_cam_dev *cam = to_mtk_cam_dev(sd);

	/* Create the video device and link. */
	return mtk_cam_video_register(cam);
}

static const struct v4l2_subdev_pad_ops mtk_cam_subdev_pad_ops = {
	.enum_mbus_code = mtk_cam_enum_mbus_code,
	.set_fmt = mtk_cam_set_fmt,
	.get_fmt = v4l2_subdev_get_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

static const struct v4l2_subdev_ops mtk_cam_subdev_ops = {
	.pad = &mtk_cam_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops mtk_cam_internal_ops = {
	.init_state = mtk_cam_init_state,
	.registered = mtk_cam_subdev_registered,
};

/* -----------------------------------------------------------------------------
 * Media Entity Operations
 */

static const struct media_entity_operations mtk_cam_media_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

/* -----------------------------------------------------------------------------
 * Init & Cleanup
 */

static int mtk_cam_v4l2_register(struct mtk_cam_dev *cam)
{
	struct device *dev = cam->dev;
	int ret;

	cam->subdev_pads[MTK_CAM_CIO_PAD_SENINF].flags = MEDIA_PAD_FL_SINK;
	cam->subdev_pads[MTK_CAM_CIO_PAD_VIDEO].flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize subdev pads */
	ret = media_entity_pads_init(&cam->subdev.entity,
				     ARRAY_SIZE(cam->subdev_pads),
				     cam->subdev_pads);
	if (ret) {
		dev_err(dev, "failed to initialize media pads:%d\n", ret);
		return ret;
	}

	/* Initialize subdev */
	v4l2_subdev_init(&cam->subdev, &mtk_cam_subdev_ops);

	cam->subdev.dev = dev;
	cam->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	cam->subdev.entity.ops = &mtk_cam_media_entity_ops;
	cam->subdev.internal_ops = &mtk_cam_internal_ops;
	cam->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(cam->subdev.name, dev_name(dev), sizeof(cam->subdev.name));
	v4l2_set_subdevdata(&cam->subdev, cam);

	v4l2_subdev_init_finalize(&cam->subdev);

	return 0;
}

static void mtk_cam_v4l2_unregister(struct mtk_cam_dev *cam)
{
	mtk_cam_video_unregister(&cam->vdev);

	media_entity_cleanup(&cam->subdev.entity);
	v4l2_subdev_cleanup(&cam->subdev);
}

int mtk_cam_dev_init(struct mtk_cam_dev *cam_dev)
{
	/* v4l2 sub-device registration */
	return mtk_cam_v4l2_register(cam_dev);
}

void mtk_cam_dev_cleanup(struct mtk_cam_dev *cam)
{
	mtk_cam_v4l2_unregister(cam);
}

/* -----------------------------------------------------------------------------
 * V4L2 Video IOCTLs
 */

static int mtk_cam_vidioc_querycap(struct file *file, void *fh,
				   struct v4l2_capability *cap)
{
	struct mtk_cam_dev *cam = video_drvdata(file);

	strscpy(cap->driver, dev_driver_string(cam->dev), sizeof(cap->driver));
	strscpy(cap->card, dev_driver_string(cam->dev), sizeof(cap->card));

	return 0;
}

/*
 * libcamera's simple pipeline prefers the highest bit depth it can process,
 * whatever the list order, so the 14-bit (10-bit data) formats are offered
 * only on request: enable_14bit=1 (module option or sysfs).
 */
static bool enable_14bit;
module_param(enable_14bit, bool, 0644);
MODULE_PARM_DESC(enable_14bit, "Offer the unpacked 14-bit Bayer formats (default: off, 8-bit)");

static bool mtk_cam_fmt_offered(const struct mtk_cam_format_info *info)
{
	return enable_14bit || info->bpp != 16;
}

/* The core has no names for the MediaTek packed formats; give them one. */
static void mtk_cam_fill_fmtdesc(struct v4l2_fmtdesc *f)
{
	static const struct {
		u32 fourcc;
		const char *desc;
	} names[] = {
		{ V4L2_PIX_FMT_MTISP_SRGGB10, "MTISP 10-bit packed RGGB" },
		{ V4L2_PIX_FMT_MTISP_SBGGR10, "MTISP 10-bit packed BGGR" },
		{ V4L2_PIX_FMT_MTISP_SGBRG10, "MTISP 10-bit packed GBRG" },
		{ V4L2_PIX_FMT_MTISP_SGRBG10, "MTISP 10-bit packed GRBG" },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); i++)
		if (names[i].fourcc == f->pixelformat)
			strscpy(f->description, names[i].desc,
				sizeof(f->description));
}

static int mtk_cam_vidioc_enum_fmt(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	struct mtk_cam_video_device *vdev = file_to_mtk_cam_video_device(file);
	const struct mtk_cam_format_info *fmtinfo;
	unsigned int i, n;

	/* If mbus_code is not set enumerate all supported formats. */
	if (!f->mbus_code) {
		if (f->index >= vdev->desc->num_fmts)
			return -EINVAL;

		/* f->description is filled in v4l_fill_fmtdesc function */
		f->pixelformat = vdev->desc->fmts[f->index];
		f->flags = 0;
		mtk_cam_fill_fmtdesc(f);

		return 0;
	}

	/*
	 * Otherwise enumerate the supported pixel formats for that bus code
	 * (several: MTISP 10-bit and 8-bit both come from a 10-bit bus).
	 */
	for (i = 0, n = 0; i < vdev->desc->num_fmts; ++i) {
		fmtinfo = mtk_cam_format_info_by_fourcc(vdev->desc->fmts[i]);
		if (!fmtinfo || fmtinfo->code != f->mbus_code ||
		    !mtk_cam_fmt_offered(fmtinfo))
			continue;
		if (n++ == f->index) {
			f->pixelformat = fmtinfo->fourcc;
			f->flags = 0;
			mtk_cam_fill_fmtdesc(f);
			return 0;
		}
	}

	return -EINVAL;
}

static int mtk_cam_vidioc_g_fmt(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct mtk_cam_video_device *vdev = file_to_mtk_cam_video_device(file);

	f->fmt.pix_mp = vdev->format;

	return 0;
}

static int mtk_cam_vidioc_try_fmt(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct mtk_cam_video_device *vdev = file_to_mtk_cam_video_device(file);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct mtk_cam_format_info *fmtinfo;

	/* Validate pixelformat */
	if (!mtk_cam_dev_find_fmt(vdev->desc, pix_mp->pixelformat))
		pix_mp->pixelformat = vdev->desc->fmts[0];

	pix_mp->width = clamp_val(pix_mp->width, IMG_MIN_WIDTH, IMG_MAX_WIDTH);
	pix_mp->height = clamp_val(pix_mp->height, IMG_MIN_HEIGHT,
				   IMG_MAX_HEIGHT);

	pix_mp->num_planes = 1;

	fmtinfo = mtk_cam_format_info_by_fourcc(pix_mp->pixelformat);
	calc_bpl_size_pix_mp(fmtinfo, pix_mp);

	/* Constant format fields */
	pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_mp->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix_mp->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int mtk_cam_vidioc_s_fmt(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct mtk_cam_video_device *vdev = file_to_mtk_cam_video_device(file);
	int ret;

	if (vb2_is_busy(vdev->vdev.queue))
		return -EBUSY;

	ret = mtk_cam_vidioc_try_fmt(file, fh, f);
	if (ret)
		return ret;

	/* Configure to video device */
	vdev->format = f->fmt.pix_mp;
	vdev->fmtinfo =
		mtk_cam_format_info_by_fourcc(f->fmt.pix_mp.pixelformat);

	return 0;
}

static int mtk_cam_vidioc_enum_framesizes(struct file *file, void *priv,
					  struct v4l2_frmsizeenum *sizes)
{
	struct mtk_cam_video_device *vdev = file_to_mtk_cam_video_device(file);

	if (sizes->index)
		return -EINVAL;

	if (!mtk_cam_dev_find_fmt(vdev->desc, sizes->pixel_format))
		return -EINVAL;

	sizes->type = vdev->desc->frmsizes->type;
	memcpy(&sizes->stepwise, &vdev->desc->frmsizes->stepwise,
	       sizeof(sizes->stepwise));

	return 0;
}

static const struct v4l2_ioctl_ops mtk_cam_v4l2_vcap_ioctl_ops = {
	.vidioc_querycap = mtk_cam_vidioc_querycap,
	.vidioc_enum_framesizes = mtk_cam_vidioc_enum_framesizes,
	.vidioc_enum_fmt_vid_cap = mtk_cam_vidioc_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = mtk_cam_vidioc_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane = mtk_cam_vidioc_s_fmt,
	.vidioc_try_fmt_vid_cap_mplane = mtk_cam_vidioc_try_fmt,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations mtk_cam_v4l2_fops = {
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = v4l2_compat_ioctl32,
#endif
};

/* -----------------------------------------------------------------------------
 * Init & Cleanup
 */

static const u32 stream_out_fmts[] = {
	/* The 1st entry is the default image format */
	V4L2_PIX_FMT_MTISP_SRGGB10,
	V4L2_PIX_FMT_MTISP_SBGGR10,
	V4L2_PIX_FMT_MTISP_SGBRG10,
	V4L2_PIX_FMT_MTISP_SGRBG10,
	/*
	 * 8-bit is the default (2026-11-19: 14-bit gave no visible gain at
	 * typical indoor light and cost ~43% more CPU in the software ISP);
	 * the 14-bit formats are offered only with enable_14bit=1.
	 */
	V4L2_PIX_FMT_SRGGB8,
	V4L2_PIX_FMT_SBGGR8,
	V4L2_PIX_FMT_SGBRG8,
	V4L2_PIX_FMT_SGRBG8,
	V4L2_PIX_FMT_SRGGB14,
	V4L2_PIX_FMT_SBGGR14,
	V4L2_PIX_FMT_SGBRG14,
	V4L2_PIX_FMT_SGRBG14,
};

static const struct mtk_cam_vdev_desc video_stream = {
	.fmts = stream_out_fmts,
	.num_fmts = ARRAY_SIZE(stream_out_fmts),
	.frmsizes =
		&(struct v4l2_frmsizeenum){
			.index = 0,
			.type = V4L2_FRMSIZE_TYPE_CONTINUOUS,
			.stepwise = {
				.max_width = IMG_MAX_WIDTH,
				.min_width = IMG_MIN_WIDTH,
				.max_height = IMG_MAX_HEIGHT,
				.min_height = IMG_MIN_HEIGHT,
				.step_height = 1,
				.step_width = 1,
			},
		},
};

int mtk_cam_video_register(struct mtk_cam_dev *cam)
{
	struct device *dev = cam->dev;
	struct mtk_cam_video_device *cam_vdev = &cam->vdev;
	struct video_device *vdev = &cam_vdev->vdev;
	struct vb2_queue *vbq = &cam_vdev->vbq;
	int ret;

	vb2_dma_contig_set_max_seg_size(cam->dev, DMA_BIT_MASK(32));

	cam_vdev->desc = &video_stream;

	/* Initialize mtk_cam_video_device */
	mtk_cam_dev_load_default_fmt(cam);

	cam_vdev->vdev_pad.flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize media entities */
	ret = media_entity_pads_init(&vdev->entity, 1, &cam_vdev->vdev_pad);
	if (ret) {
		dev_err(dev, "failed to initialize media pad:%d\n", ret);
		return ret;
	}
	cam_vdev->vdev_pad.flags = MEDIA_PAD_FL_SINK;

	vbq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	vbq->io_modes = VB2_MMAP | VB2_DMABUF;
	vbq->dev = dev;
	vbq->ops = &mtk_cam_vb2_ops;
	vbq->mem_ops = &vb2_dma_contig_memops;
	vbq->buf_struct_size = sizeof(struct mtk_cam_dev_buffer);
	/*
	 * TODO: The hardware supports SOF interrupts, switch to a SOF
	 * timestamp source would give better accuracy, but first requires
	 * extending the V4L2 API to support it.
	 */
	vbq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
			     | V4L2_BUF_FLAG_TSTAMP_SRC_EOF;

	/*
	 * Start only with a buffer queued: otherwise the first frame goes to
	 * the dummy buffer and uses sequence 0 (libcamera expects the first
	 * delivered frame to be 0).
	 */
	vbq->min_queued_buffers = 1;
	vbq->drv_priv = cam;

	vbq->lock = &cam_vdev->vdev_lock;
	ret = vb2_queue_init(vbq);
	if (ret) {
		dev_err(dev, "failed to init. vb2 queue:%d\n", ret);
		goto fail_media_clean;
	}

	/* Initialize vdev */
	snprintf(vdev->name, sizeof(vdev->name), "%s video stream",
		 dev_name(dev));

	/* Set cap/type/ioctl_ops of the video device */
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING
			  | V4L2_CAP_IO_MC;
	vdev->ioctl_ops = &mtk_cam_v4l2_vcap_ioctl_ops;
	vdev->fops = &mtk_cam_v4l2_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &cam_vdev->vdev_lock;
	vdev->v4l2_dev = cam->subdev.v4l2_dev;
	vdev->queue = &cam_vdev->vbq;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->entity.function = MEDIA_ENT_F_IO_V4L;
	video_set_drvdata(vdev, cam);

	/* Initialize miscellaneous variables */
	mutex_init(&cam_vdev->vdev_lock);
	INIT_LIST_HEAD(&cam->buf_list);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "failed to register vde:%d\n", ret);
		goto fail_vb2_rel;
	}

	/* Create link between the video pad and the subdev pad. */
	ret = media_create_pad_link(&cam->subdev.entity,
				    MTK_CAM_CIO_PAD_VIDEO,
				    &vdev->entity, 0,
				    MEDIA_LNK_FL_IMMUTABLE
				    | MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto fail_vdev_ureg;

	return 0;

fail_vdev_ureg:
	video_unregister_device(vdev);
fail_vb2_rel:
	mutex_destroy(&cam_vdev->vdev_lock);
	vb2_queue_release(vbq);
fail_media_clean:
	media_entity_cleanup(&vdev->entity);

	return ret;
}

void mtk_cam_video_unregister(struct mtk_cam_video_device *vdev)
{
	video_unregister_device(&vdev->vdev);
	vb2_queue_release(&vdev->vbq);
	media_entity_cleanup(&vdev->vdev.entity);
	mutex_destroy(&vdev->vdev_lock);
	vb2_dma_contig_clear_max_seg_size(&vdev->vdev.dev);
}
