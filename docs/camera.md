# Front camera (GC5035)

Status: working through libcamera and PipeWire (video calls at 720p30,
stills up to 2592x1944). Rear camera (Samsung S5K2X7, 24 MP): not started.

## Hardware path

| Block | Where | Notes |
|---|---|---|
| Sensor | GalaxyCore GC5035, i2c4 `0x37` | stock sensor 1 "sub"; DOVDD = VCAMIO 1.8 V, AVDD = VCAMA1 2.8 V, DVDD = VCAMD 1.2 V; MCLK1 = 24 MHz (CAMTG2); PDN GPIO98, RST GPIO102 |
| CSI-2 | CSI1, 2 lanes | stock `libcameracustom.so` camera table: sub = MCLK1, CSI port 1, front, orientation 0 |
| seninf | `1a040000`, ChromeOS 5.10 `mtk_seninf.c` port | CSI1 -> SENINF_3 -> cam mux 0, one pixel per clock |
| CAMSV | `1a050000` (stock `camsv1`), IRQ SPI 258, `CLK_CAM_CAMSV0` | raw capture to memory, no ISP processing |
| Memory | CMA (below 4 GiB) | no SMI/M4U driver: the CAMSV driver clears larb6's M4U enable bits so the DMA uses physical addresses |

Kernel: `drivers/media/i2c/gc5035.c`, `drivers/media/platform/mediatek/seninf/`,
`drivers/media/platform/mediatek/camsv/` (all in `kernel/delta/`).

### Why CAMSV and not the ISP (P1)

The MT8183 ISP pass 1 (ChromeOS `isp_50/cam`) is programmed by the SCP
co-processor. On the Cosmo the stock ATF configures DEVAPC so that the SCP
(bus domain 3) cannot write the camera block: with violation reporting
enabled (DEVAPC `APC_CON` 0x10207F00 = 0x80000000) every stream start logs a
write violation by domain 3 at cam_b `0x1a00619c`. Kukui's TF-A puts the SCP
in domain 1 and opens CAM_A/B/C for it; the stock MT6771 ATF does not, and
the master-domain registers (DEVAPC AO `0x1000E000`) are secure-world only.
The P1 port was therefore dropped. CAMSV is a simple TG -> PAK -> IMGO DMA
engine the application processor programs itself.

### CAMSV driver

Port of the MT8365 "ISP3.0" CAMSV driver (BayLibre, v8). On MT6771 the
blocks sit at CAMSV `0x510`, TG `0x220`, IMGO `0x020`; the IMGO registers
follow the ISP 5.0 DMA layout (XSIZE `+0x10`, YSIZE `+0x14`, STRIDE `+0x18`,
CON `+0x1C`). Differences from MT8365, all measured with the sensor's
colour bars:

- PAK modes are numbered the other way round: 0 = 8-bit, 1 = 10-bit,
  2 = 12-bit, 3 = 14-bit. Packed output is MediaTek packing (little-endian
  bit stream), not MIPI.
- PAK bypass (`MODULE_EN` PAK_SEL) with 16-bit bus sizes gives 16-bit words
  with the 10-bit data in bits 13:4 (exposed as `Sxxxx14`).
- The ISP 5.0 frame-buffer counter is not used: the PASS1_DON interrupt
  moves `IMGO_BASE_ADDR` to the next buffer, a dummy buffer takes frames
  when none is queued.
- A line-length mismatch makes IMGO raise an error per line; after 8 errors
  in a stream the driver stops the DMA and the interrupts (an error flood
  once hung the board without the watchdog firing).

Formats offered: 8-bit Bayer (default), MediaTek packed 10-bit (`MBRA`
etc.), and with the module option `enable_14bit=1` unpacked 14-bit. libcamera
picks the highest bit depth it can process, whatever the list order, hence
the option: 14-bit gave no visible gain at indoor light and costs ~43% more
CPU in the software ISP.

### Sensor driver changes (ChromeOS `gc5035.c`)

- Digital gain defaults to 1x (`0xb1/0xb2` = `0x01/0x00`, as stock); the
  ChromeOS default was the maximum (~16x).
- `analogue_gain` is the total gain in 1/256 units (256..3994): the largest
  stock analogue step at or below it goes to `0xb6`, the remainder to the
  digital gain, as the stock `set_gain()` does. With the bare 17-step index
  libcamera's AGC stalled at ~2x (its small steps were truncated back).
- Orientation (front) and rotation (0) from the DT; `get_selection` reports
  the 2592x1944 pixel array and the crop of the full and 2x2-binned modes.

## Userspace

`userspace/libcamera/` holds the libcamera patches, a container build
script for Debian packages, the GC5035 tuning file and the global
configuration. Patches (on libcamera 0.7.2, also apply to 0.7.1):

1. simple pipeline: `mtk-camsv` entry with the software ISP;
2. GC5035 sensor helper (black level 64 at 10 bits, linear total gain) and
   sensor properties;
3. soft IPA `Adjust`: contrast/saturation defaults from the tuning file;
4. GPU debayer: pick one input texel per output pixel and derive the Bayer
   phase and all samples from it (upstream mixes `floor()` for the phase
   with continuous sample positions: tinted border at 2:1, single-line
   seams at other scales);
5. unpacked 14-bit Bayer input (CPU and GPU).

Also needed on the device: dma-buf heaps (`CONFIG_DMABUF_HEAPS`, system and
CMA) with a udev rule giving the `video` group access, PipeWire 1.6.9 /
WirePlumber 0.5.12 (trixie-backports; the 1.4.2 libcamera plugin does not
build against libcamera 0.7).

Modes: requests up to 1292x968 (including 1280x720) use the sensor's 2x2
binned 1296x972 mode at 30 fps (noise 2.5x lower than downscaling the full
mode); larger requests use 2592x1944 at 20 fps.

## Tuning

`gc5035.yaml`: black level from the helper, grey-world AWB, identity colour
matrix, contrast 1.3 and saturation 1.3 (chosen from same-scene captures;
1.6 crushed shadows). A colour matrix was fitted from a printed chart
against a reference camera under indoor light; it improves the chart's
chromaticity error from 20.6 to 6.2 but has a blue row of ~2.4, which
amplifies the grey-world AWB's scene-dependent errors (10-20%) into a
visible yellow-green cast. It stays off until the AWB is calibrated
(per-colour-temperature white points).

`tools/cosmo-chart-shot` captures a calibration set (processed frame plus
raw frames at fixed exposure).
