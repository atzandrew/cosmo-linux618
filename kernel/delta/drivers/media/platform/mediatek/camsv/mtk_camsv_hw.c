// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 BayLibre
 * Copyright (c) 2022 MediaTek Inc.
 *
 * MT6771 (Cosmo Communicator) port of the MT8365 ISP3.0 CAMSV driver
 * (BayLibre "Add Mediatek ISP3.0" v8, 2025-01). Register blocks are the
 * same family on ISP 5.0 (MT6771/MT8183); see mtk_camsv_reg.h for the
 * offsets and which of them are derived rather than read from stock.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <media/v4l2-fwnode.h>

#include "mtk_camsv.h"
#include "mtk_camsv_hw.h"
#include "mtk_camsv_reg.h"

#define INT_ST_MASK_CAMSV (CAMSV_IRQ_PASS1_DON)

#define INT_ST_MASK_CAMSV_ERR \
	(CAMSV_IRQ_TG_ERR | CAMSV_IRQ_TG_GBERR | CAMSV_IRQ_IMGO_ERR)

#define MTK_CAMSV_AUTOSUSPEND_DELAY_MS 100

/* IMGO errors in one stream before the DMA is switched off (no flood) */
#define MTK_CAMSV_MAX_IMGO_ERR	8

/* seninf source pad 4 = cam mux 0 (ChromeOS mtk_seninf CAM_MUX_IDX_MIN) */
#define MTK_SENINF_PAD_CAM_MUX0	4

/*
 * MT6771 isp_50 CAMSV: one 0x1000 block per instance with the sub-blocks
 * IMGO at 0x020, TG at 0x220 and CAMSV (top) at 0x510 (stock cam_regs.h:
 * IMGO_BASE_ADDR 0x020, TG_SEN_MODE 0x230, MODULE_EN 0x510). Our seninf
 * runs its mux in one-pixel mode, so no double data bus / 2-pixel PAK.
 */
static const struct mtk_cam_conf camsv_conf_mt6771 = {
	.camsv_ofs = 0x510,
	.imgo_ofs = 0x020,
	.tg_ofs = 0x220,
	.tg_sen_mode = 0,
	.module_en = CAMSV_MODULE_EN_DB_EN | CAMSV_MODULE_EN_TG_EN,
	.imgo_con = ((0x8  << CAMSV_IMGO_SV_CON_MAX_BURST_LEN_SHIFT) &
		CAMSV_IMGO_SV_CON_MAX_BURST_LEN_MASK) |
		((0x80 << CAMSV_IMGO_SV_CON_FIFO_SIZE_SHIFT) &
		CAMSV_IMGO_SV_CON_FIFO_SIZE_MASK),
	.imgo_con2 = ((0x2 << CAMSV_IMGO_SV_CON2_FIFO_PREP_PRI_THRL_SHIFT) &
		CAMSV_IMGO_SV_CON2_FIFO_PREP_PRI_THRL_MASK) |
		((0x2 << CAMSV_IMGO_SV_CON2_FIFO_PREP_PRI_THRH_SHIFT) &
		CAMSV_IMGO_SV_CON2_FIFO_PREP_PRI_THRH_MASK),
};

static u32 mtk_camsv_read(struct mtk_cam_dev *priv, u32 reg)
{
	return readl(priv->regs + reg);
}

static void mtk_camsv_write(struct mtk_cam_dev *priv, u32 reg, u32 value)
{
	writel(value, priv->regs + reg);
}

static void mtk_camsv_img0_write(struct mtk_cam_dev *priv, u32 reg, u32 value)
{
	writel(value, priv->regs_img0 + reg);
}

static u32 mtk_camsv_tg_read(struct mtk_cam_dev *priv, u32 reg)
{
	return readl(priv->regs_tg + reg);
}

static void mtk_camsv_tg_write(struct mtk_cam_dev *priv, u32 reg, u32 value)
{
	writel(value, priv->regs_tg + reg);
}

void mtk_camsv_update_buffers_add(struct mtk_cam_dev *cam_dev,
				  struct mtk_cam_dev_buffer *buf)
{
	mtk_camsv_img0_write(cam_dev, CAMSV_IMGO_SV_BASE_ADDR, buf->daddr);
	cam_dev->dummy_in_use = false;
}

static void mtk_camsv_use_dummy(struct mtk_cam_dev *cam_dev)
{
	if (!cam_dev->dummy_vaddr)
		return;
	mtk_camsv_img0_write(cam_dev, CAMSV_IMGO_SV_BASE_ADDR,
			     cam_dev->dummy_daddr);
	cam_dev->dummy_in_use = true;
}

void mtk_camsv_cmos_vf_hw_enable(struct mtk_cam_dev *cam_dev)
{
	u32 clk_en = CAMSV_TG_DP_CLK_EN | CAMSV_DMA_DP_CLK_EN |
		     CAMSV_PAK_DP_CLK_EN;

	mtk_camsv_write(cam_dev, CAMSV_CLK_EN, clk_en);
	mtk_camsv_tg_write(cam_dev, CAMSV_TG_SEN_MODE,
			   mtk_camsv_tg_read(cam_dev, CAMSV_TG_SEN_MODE) |
			   CAMSV_TG_SEN_MODE_CMOS_EN);
	mtk_camsv_tg_write(cam_dev, CAMSV_TG_VF_CON,
			   mtk_camsv_tg_read(cam_dev, CAMSV_TG_VF_CON) |
			   CAMSV_TG_VF_CON_VFDATA_EN);
}

void mtk_camsv_cmos_vf_hw_disable(struct mtk_cam_dev *cam_dev)
{
	mtk_camsv_tg_write(cam_dev, CAMSV_TG_SEN_MODE,
			   mtk_camsv_tg_read(cam_dev, CAMSV_TG_SEN_MODE) &
			   ~CAMSV_TG_SEN_MODE_CMOS_EN);
	mtk_camsv_tg_write(cam_dev, CAMSV_TG_VF_CON,
			   mtk_camsv_tg_read(cam_dev, CAMSV_TG_VF_CON) &
			   ~CAMSV_TG_VF_CON_VFDATA_EN);
}

/*
 * MT8365 drives the IMGO frame-buffer counter (FBC) in the CAMSV block.
 * ISP 5.0 has FBC_IMGO_CTL1/2 at 0x110/0x114 with a different layout and
 * the read-count increment is not located yet, so the FBC stays off: the
 * DMA writes every frame to IMGO_BASE_ADDR and the PASS1_DON handler
 * moves it on (to the dummy buffer when no buffer is queued).
 */
void mtk_camsv_fbc_init(struct mtk_cam_dev *cam_dev, unsigned int num_buffers)
{
}

void mtk_camsv_fbc_inc(struct mtk_cam_dev *cam_dev)
{
}

void mtk_camsv_setup(struct mtk_cam_dev *cam_dev, unsigned int count,
		     u32 w, u32 h, u32 bpl,
		     const struct mtk_cam_format_info *fmtinfo)
{
	const struct mtk_cam_conf *conf = cam_dev->conf;
	struct mtk_cam_dev_buffer *buf;
	unsigned long flags;
	u32 tmp;
	int ret;

	cam_dev->imgo_err_count = 0;

	mtk_camsv_tg_write(cam_dev, CAMSV_TG_SEN_MODE, conf->tg_sen_mode);

	mtk_camsv_tg_write(cam_dev, CAMSV_TG_SEN_GRAB_PXL,
			   (w * fmtinfo->w_factor) << 16U);

	mtk_camsv_tg_write(cam_dev, CAMSV_TG_SEN_GRAB_LIN, h << 16U);

	/* YUV_U2S_DIS: disable YUV sensor unsigned to signed */
	mtk_camsv_tg_write(cam_dev, CAMSV_TG_PATH_CFG, 0x1000U);

	/* Reset (stock camera_isp.c:1341-1347 is the same sequence) */
	mtk_camsv_write(cam_dev, CAMSV_SW_CTL, CAMSV_SW_RST);
	mtk_camsv_write(cam_dev, CAMSV_SW_CTL, 0x0U);
	mtk_camsv_write(cam_dev, CAMSV_SW_CTL, CAMSV_IMGO_RST_TRIG);

	ret = readl_poll_timeout_atomic(cam_dev->regs + CAMSV_SW_CTL, tmp,
					(tmp == (CAMSV_IMGO_RST_TRIG |
						 CAMSV_IMGO_RST_ST)), 10, 200);
	if (ret)
		dev_warn(cam_dev->dev, "IMGO reset timeout, SW_CTL 0x%x\n", tmp);

	mtk_camsv_write(cam_dev, CAMSV_SW_CTL, 0x0U);

	mtk_camsv_write(cam_dev, CAMSV_INT_EN,
			INT_ST_MASK_CAMSV | INT_ST_MASK_CAMSV_ERR);

	mtk_camsv_write(cam_dev, CAMSV_MODULE_EN,
			conf->module_en | fmtinfo->module_en_pak);
	/* FMT_SEL resets to 0x100 here: keep the bits we do not know */
	mtk_camsv_write(cam_dev, CAMSV_FMT_SEL,
			(mtk_camsv_read(cam_dev, CAMSV_FMT_SEL) &
			 ~(GENMASK(1, 0) | CAMSV_FMT_SEL_IMGO_BUS_SIZE_16)) |
			fmtinfo->fmt_sel);
	mtk_camsv_write(cam_dev, CAMSV_PAK, fmtinfo->pak);

	mtk_camsv_img0_write(cam_dev, CAMSV_IMGO_SV_XSIZE, bpl - 1U);
	mtk_camsv_img0_write(cam_dev, CAMSV_IMGO_SV_YSIZE, h - 1U);

	mtk_camsv_img0_write(cam_dev, CAMSV_IMGO_SV_STRIDE,
			     fmtinfo->imgo_stride | bpl);

	/*
	 * CON = 0x80000080 equals the MT6771 reset value. CON2 keeps its reset
	 * value (0x00800080): MT8365's FIFO thresholds need not apply here.
	 */
	mtk_camsv_img0_write(cam_dev, CAMSV_IMGO_SV_CON, conf->imgo_con);

	/* Set buf addr */
	spin_lock_irqsave(&cam_dev->buf_list_lock, flags);
	buf = list_first_entry_or_null(&cam_dev->buf_list,
				       struct mtk_cam_dev_buffer, list);
	if (buf)
		mtk_camsv_update_buffers_add(cam_dev, buf);
	else
		mtk_camsv_use_dummy(cam_dev);
	spin_unlock_irqrestore(&cam_dev->buf_list_lock, flags);

	/* CMOS_EN first */
	mtk_camsv_tg_write(cam_dev, CAMSV_TG_SEN_MODE,
			   mtk_camsv_tg_read(cam_dev, CAMSV_TG_SEN_MODE) |
			   CAMSV_TG_SEN_MODE_CMOS_EN);

	/* finally, CAMSV_MODULE_EN : IMGO_EN */
	mtk_camsv_write(cam_dev, CAMSV_MODULE_EN,
			mtk_camsv_read(cam_dev, CAMSV_MODULE_EN) |
			CAMSV_MODULE_EN_IMGO_EN);
}

static irqreturn_t isp_irq_camsv(int irq, void *data)
{
	struct mtk_cam_dev *cam_dev = (struct mtk_cam_dev *)data;
	struct mtk_cam_dev_buffer *processed_buf = NULL, *buf;
	unsigned int irq_status;

	irq_status = mtk_camsv_read(cam_dev, CAMSV_INT_STATUS);

	if (irq_status & INT_ST_MASK_CAMSV_ERR)
		dev_err_ratelimited(cam_dev->dev, "irq error 0x%lx\n",
				    irq_status & INT_ST_MASK_CAMSV_ERR);

	/*
	 * A line-length mismatch (format vs what the TG delivers) makes IMGO
	 * error on every line; on 2026-11-19 such a flood ended in a hard
	 * hang. Stop the write DMA and the IRQs after a few errors.
	 */
	if ((irq_status & CAMSV_IRQ_IMGO_ERR) &&
	    ++cam_dev->imgo_err_count == MTK_CAMSV_MAX_IMGO_ERR) {
		mtk_camsv_write(cam_dev, CAMSV_MODULE_EN,
				mtk_camsv_read(cam_dev, CAMSV_MODULE_EN) &
				~CAMSV_MODULE_EN_IMGO_EN);
		mtk_camsv_write(cam_dev, CAMSV_INT_EN, 0);
		dev_err(cam_dev->dev, "IMGO errors, write DMA stopped\n");
		return IRQ_HANDLED;
	}

	/* De-queue frame */
	if (irq_status & CAMSV_IRQ_PASS1_DON) {
		spin_lock(&cam_dev->buf_list_lock);
		cam_dev->sequence++;

		/* The finished frame went to the dummy: nothing to complete */
		if (!cam_dev->dummy_in_use) {
			processed_buf = list_first_entry_or_null(&cam_dev->buf_list,
								 struct mtk_cam_dev_buffer,
								 list);
			if (processed_buf)
				list_del(&processed_buf->list);
		}

		buf = list_first_entry_or_null(&cam_dev->buf_list,
					       struct mtk_cam_dev_buffer, list);
		if (buf)
			mtk_camsv_update_buffers_add(cam_dev, buf);
		else
			mtk_camsv_use_dummy(cam_dev);

		spin_unlock(&cam_dev->buf_list_lock);

		if (processed_buf) {
			processed_buf->v4l2_buf.sequence = cam_dev->sequence;
			processed_buf->v4l2_buf.vb2_buf.timestamp =
				ktime_get_ns();
			vb2_buffer_done(&processed_buf->v4l2_buf.vb2_buf,
					VB2_BUF_STATE_DONE);
		}
	}

	return IRQ_HANDLED;
}

/*
 * The M4U stays as LK left it on the Cosmo, with larb6's M4U enable bit set
 * on its ports (no SMI/M4U driver on this board). Clear it so the
 * CAMSV write DMA uses physical addresses.
 */
#define MTK_CAMSV_LARB6_PORTS		31
#define SMI_LARB_NONSEC_CON(port)	(0x380 + (port) * 4)
#define SMI_LARB_F_MMU_EN		BIT(0)

static void mtk_camsv_larb6_physical(struct mtk_cam_dev *cam_dev)
{
	unsigned int i;
	u32 val;

	for (i = 0; i < MTK_CAMSV_LARB6_PORTS; i++) {
		val = readl(cam_dev->larb6 + SMI_LARB_NONSEC_CON(i));
		writel(val & ~SMI_LARB_F_MMU_EN,
		       cam_dev->larb6 + SMI_LARB_NONSEC_CON(i));
	}
}

/* ---------------------------------------------------------------------------
 * Media master glue: CAMSV owns the media device and binds the ChromeOS seninf
 * driver asynchronously (seninf does not start the sensor itself)
 */

static struct v4l2_subdev *mtk_camsv_get_sensor(struct mtk_cam_dev *cam)
{
	struct media_entity *seninf = &cam->seninf_sd->entity;
	unsigned int i;

	/* The sensor is the entity behind seninf's enabled sink link. */
	for (i = 0; i < seninf->num_pads; i++) {
		struct media_pad *remote;

		if (!(seninf->pads[i].flags & MEDIA_PAD_FL_SINK))
			continue;
		remote = media_pad_remote_pad_first(&seninf->pads[i]);
		if (remote &&
		    remote->entity->function == MEDIA_ENT_F_CAM_SENSOR)
			return media_entity_to_v4l2_subdev(remote->entity);
	}

	dev_err(cam->dev, "no sensor linked to seninf\n");
	return NULL;
}

int mtk_camsv_pipe_stream(struct mtk_cam_dev *cam, bool on)
{
	int ret;

	if (!cam->seninf_sd)
		return -ENOLINK;

	if (!on) {
		if (cam->sensor)
			v4l2_subdev_call(cam->sensor, video, s_stream, 0);
		v4l2_subdev_call(cam->seninf_sd, video, s_stream, 0);
		cam->sensor = NULL;
		return 0;
	}

	cam->sensor = mtk_camsv_get_sensor(cam);
	if (!cam->sensor)
		return -ENOLINK;

	ret = v4l2_subdev_call(cam->seninf_sd, video, s_stream, 1);
	if (ret) {
		dev_err(cam->dev, "seninf stream on failed:%d\n", ret);
		cam->sensor = NULL;
		return ret;
	}

	ret = v4l2_subdev_call(cam->sensor, video, s_stream, 1);
	if (ret) {
		dev_err(cam->dev, "sensor stream on failed:%d\n", ret);
		v4l2_subdev_call(cam->seninf_sd, video, s_stream, 0);
		cam->sensor = NULL;
		return ret;
	}

	return 0;
}

static int mtk_camsv_notifier_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *sd,
				    struct v4l2_async_connection *asd)
{
	struct mtk_cam_dev *cam =
		container_of(notifier, struct mtk_cam_dev, notifier);

	if (!(sd->entity.function & MEDIA_ENT_F_VID_IF_BRIDGE))
		return -ENODEV;

	cam->seninf_sd = sd;
	return 0;
}

static void mtk_camsv_notifier_unbind(struct v4l2_async_notifier *notifier,
				      struct v4l2_subdev *sd,
				      struct v4l2_async_connection *asd)
{
	struct mtk_cam_dev *cam =
		container_of(notifier, struct mtk_cam_dev, notifier);

	cam->seninf_sd = NULL;
}

static int mtk_camsv_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct mtk_cam_dev *cam =
		container_of(notifier, struct mtk_cam_dev, notifier);
	int ret;

	if (!cam->seninf_sd)
		return -ENODEV;

	ret = media_create_pad_link(&cam->seninf_sd->entity,
				    MTK_SENINF_PAD_CAM_MUX0,
				    &cam->subdev.entity, MTK_CAM_CIO_PAD_SENINF,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(cam->dev, "failed to link seninf:%d\n", ret);
		return ret;
	}

	return v4l2_device_register_subdev_nodes(&cam->v4l2_dev);
}

static const struct v4l2_async_notifier_operations mtk_camsv_async_ops = {
	.bound = mtk_camsv_notifier_bound,
	.unbind = mtk_camsv_notifier_unbind,
	.complete = mtk_camsv_notifier_complete,
};

static int mtk_camsv_async_register(struct mtk_cam_dev *cam)
{
	struct v4l2_async_connection *asc;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_nf_init(&cam->notifier, &cam->v4l2_dev);
	ep = fwnode_graph_get_next_endpoint(dev_fwnode(cam->dev), NULL);
	if (!ep) {
		dev_err(cam->dev, "no input endpoint\n");
		return -ENODEV;
	}
	asc = v4l2_async_nf_add_fwnode_remote(&cam->notifier, ep,
					      struct v4l2_async_connection);
	fwnode_handle_put(ep);
	if (IS_ERR(asc)) {
		v4l2_async_nf_cleanup(&cam->notifier);
		return PTR_ERR(asc);
	}

	cam->notifier.ops = &mtk_camsv_async_ops;
	ret = v4l2_async_nf_register(&cam->notifier);
	if (ret)
		v4l2_async_nf_cleanup(&cam->notifier);

	return ret;
}

static int mtk_camsv_media_register(struct mtk_cam_dev *cam)
{
	struct media_device *mdev = &cam->media_dev;
	int ret;

	mdev->dev = cam->dev;
	strscpy(mdev->model, "mtk-camsv", sizeof(mdev->model));
	media_device_init(mdev);

	cam->v4l2_dev.mdev = mdev;
	ret = v4l2_device_register(cam->dev, &cam->v4l2_dev);
	if (ret)
		goto err_media_cleanup;

	ret = media_device_register(mdev);
	if (ret)
		goto err_v4l2_unreg;

	/* subdev + video node (video node is created in .registered) */
	ret = mtk_cam_dev_init(cam);
	if (ret)
		goto err_media_unreg;

	ret = v4l2_device_register_subdev(&cam->v4l2_dev, &cam->subdev);
	if (ret)
		goto err_dev_cleanup;

	ret = mtk_camsv_async_register(cam);
	if (ret)
		goto err_subdev_unreg;

	return 0;

err_subdev_unreg:
	v4l2_device_unregister_subdev(&cam->subdev);
err_dev_cleanup:
	mtk_cam_dev_cleanup(cam);
err_media_unreg:
	media_device_unregister(mdev);
err_v4l2_unreg:
	v4l2_device_unregister(&cam->v4l2_dev);
err_media_cleanup:
	media_device_cleanup(mdev);
	return ret;
}

static void mtk_camsv_media_unregister(struct mtk_cam_dev *cam)
{
	v4l2_async_nf_unregister(&cam->notifier);
	v4l2_async_nf_cleanup(&cam->notifier);
	mtk_cam_dev_cleanup(cam);
	v4l2_device_unregister_subdev(&cam->subdev);
	media_device_unregister(&cam->media_dev);
	v4l2_device_unregister(&cam->v4l2_dev);
	media_device_cleanup(&cam->media_dev);
}

static int mtk_camsv_probe(struct platform_device *pdev)
{
	static const char * const clk_names[] = { "cam", "camtg", "camsv"};

	struct mtk_cam_dev *cam_dev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	unsigned int i;
	int ret;

	cam_dev = devm_kzalloc(dev, sizeof(*cam_dev), GFP_KERNEL);
	if (!cam_dev)
		return -ENOMEM;

	cam_dev->conf = device_get_match_data(dev);
	if (!cam_dev->conf)
		return -ENODEV;

	cam_dev->dev = dev;
	dev_set_drvdata(dev, cam_dev);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(dev, ret, "no 32-bit DMA\n");

	cam_dev->base = devm_platform_ioremap_resource_byname(pdev, "camsv");
	if (IS_ERR(cam_dev->base))
		return dev_err_probe(dev, PTR_ERR(cam_dev->base),
				     "failed to map register base\n");
	cam_dev->regs = cam_dev->base + cam_dev->conf->camsv_ofs;
	cam_dev->regs_img0 = cam_dev->base + cam_dev->conf->imgo_ofs;
	cam_dev->regs_tg = cam_dev->base + cam_dev->conf->tg_ofs;

	/* Optional; shared with P1, so no request_mem_region */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "larb6");
	if (res) {
		cam_dev->larb6 = devm_ioremap(dev, res->start,
					      resource_size(res));
		if (!cam_dev->larb6)
			return -ENOMEM;
	}

	cam_dev->num_clks = ARRAY_SIZE(clk_names);
	cam_dev->clks = devm_kcalloc(dev, cam_dev->num_clks,
				     sizeof(*cam_dev->clks), GFP_KERNEL);
	if (!cam_dev->clks)
		return -ENOMEM;

	for (i = 0; i < cam_dev->num_clks; ++i)
		cam_dev->clks[i].id = clk_names[i];

	ret = devm_clk_bulk_get(dev, cam_dev->num_clks, cam_dev->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get clocks\n");

	cam_dev->irq = platform_get_irq(pdev, 0);
	if ((int)cam_dev->irq < 0)
		return cam_dev->irq;
	ret = devm_request_irq(dev, cam_dev->irq, isp_irq_camsv, 0,
			       dev_name(dev), cam_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq=%d\n",
				     cam_dev->irq);

	spin_lock_init(&cam_dev->buf_list_lock);
	INIT_LIST_HEAD(&cam_dev->buf_list);

	/* initialise runtime power management */
	pm_runtime_set_autosuspend_delay(dev, MTK_CAMSV_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_suspended(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = mtk_camsv_media_register(cam_dev);
	if (ret)
		return ret;

	return 0;
}

static void mtk_camsv_remove(struct platform_device *pdev)
{
	struct mtk_cam_dev *cam_dev = dev_get_drvdata(&pdev->dev);

	mtk_camsv_media_unregister(cam_dev);
	pm_runtime_dont_use_autosuspend(cam_dev->dev);
}

static int mtk_camsv_runtime_suspend(struct device *dev)
{
	struct mtk_cam_dev *cam_dev = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(cam_dev->num_clks, cam_dev->clks);

	return 0;
}

static int mtk_camsv_runtime_resume(struct device *dev)
{
	struct mtk_cam_dev *cam_dev = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(cam_dev->num_clks, cam_dev->clks);
	if (ret) {
		dev_err(dev, "failed to enable clock:%d\n", ret);
		return ret;
	}

	if (cam_dev->larb6)
		mtk_camsv_larb6_physical(cam_dev);

	return 0;
}

/*
 * System sleep: not supported while streaming yet (test driver); idle
 * suspend only needs the clocks off, which runtime PM already did.
 */
static const struct dev_pm_ops mtk_camsv_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_camsv_runtime_suspend,
			   mtk_camsv_runtime_resume, NULL)
};

static const struct of_device_id mtk_camsv_of_ids[] = {
	{ .compatible = "mediatek,mt6771-camsv", .data = &camsv_conf_mt6771 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_camsv_of_ids);

static struct platform_driver mtk_camsv_driver = {
	.probe = mtk_camsv_probe,
	.remove = mtk_camsv_remove,
	.driver = {
		.name = "mtk-camsv",
		.of_match_table = mtk_camsv_of_ids,
		.pm = &mtk_camsv_pm_ops,
	}
};

module_platform_driver(mtk_camsv_driver);

MODULE_DESCRIPTION("MediaTek CAMSV raw capture driver (MT6771 port of the ISP3.0 CAMSV driver)");
MODULE_AUTHOR("Florian Sylvestre <fsylvestre@baylibre.com>");
MODULE_LICENSE("GPL");
