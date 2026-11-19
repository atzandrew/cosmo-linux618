#!/usr/bin/env python3
# Cosmo Communicator libcamera patches (run in the libcamera source root).
# 1 simple pipeline: mtk-camsv entry with software ISP
# 2 GC5035 sensor helper + sensor properties
# 3 soft IPA Adjust: contrast/saturation defaults from the tuning file
# 4 GPU debayer: whole-texel sampling (consistent Bayer phase, border wrap)
# 5 unpacked 14-bit Bayer input (CPU + GPU); stats via 0.7.2 sumShift_ = 6
#   (skipped with --no-14bit)
import sys
p='src/libcamera/pipeline/simple/simple.cpp'; s=open(p).read()
o='\t{ "mtk-seninf", { { "mtk-mdp", 3 } }, false },\n'
assert o in s; s=s.replace(o,'\t{ "mtk-camsv", {}, true },\n'+o); open(p,'w').write(s)
p='src/ipa/libipa/camera_sensor_helper.cpp'; s=open(p).read()
o='class CameraSensorHelperGc08a3 : public CameraSensorHelper\n'
assert o in s
s=s.replace(o,'''class CameraSensorHelperGc5035 : public CameraSensorHelper
{
public:
	CameraSensorHelperGc5035()
	{
		/* 64 at 10 bits (P1:0x60 = 0x40 in the init table, as GC05A2/GC08A3). */
		blackLevel_ = 4096;
		/*
		 * The Cosmo driver's analogue gain control is the total sensor
		 * gain in 1/256 units (stock analogue step + digital remainder).
		 */
		gain_ = AnalogueGainLinear{ 1, 0, 0, 256 };
	}
};
REGISTER_CAMERA_SENSOR_HELPER("gc5035", CameraSensorHelperGc5035)

'''+o); open(p,'w').write(s)
p='src/libcamera/sensor/camera_sensor_properties.cpp'; s=open(p).read()
o='\t\t{ "gc08a3", {\n'
assert o in s
s=s.replace(o,'''\t\t{ "gc5035", {
\t\t\t.unitCellSize = { 1120, 1120 },
\t\t\t.testPatternModes = {
\t\t\t\t{ controls::draft::TestPatternModeOff, 0 },
\t\t\t\t{ controls::draft::TestPatternModeColorBars, 1 },
\t\t\t},
\t\t\t.sensorDelays = {
\t\t\t\t.exposureDelay = 2,
\t\t\t\t.gainDelay = 2,
\t\t\t\t.vblankDelay = 2,
\t\t\t\t.hblankDelay = 2
\t\t\t},
\t\t} },
'''+o); open(p,'w').write(s)

p='src/ipa/simple/algorithms/adjust.h'; s=open(p).read()
o='private:\n\tvoid applySaturation(Matrix<float, 3, 3> &ccm, float saturation);\n'
assert o in s
s=s.replace(o,'private:\n\tvoid applySaturation(Matrix<float, 3, 3> &ccm, float saturation);\n\n\t/* Optional defaults from the tuning file ("contrast", "saturation") */\n\tstd::optional<float> defaultContrast_;\n\tstd::optional<float> defaultSaturation_;\n')
if '#include <optional>' not in s:
    s=s.replace('#pragma once\n','#pragma once\n\n#include <optional>\n',1)
open(p,'w').write(s)
p='src/ipa/simple/algorithms/adjust.cpp'; s=open(p).read()
o='''int Adjust::init(IPAContext &context, [[maybe_unused]] const ValueNode &tuningData)
{
	context.ctrlMap[&controls::Gamma] =
		ControlInfo(0.1f, 10.0f, kDefaultGamma);
	context.ctrlMap[&controls::Contrast] =
		ControlInfo(0.0f, 2.0f, kDefaultContrast);
	if (context.ccmEnabled)
		context.ctrlMap[&controls::Saturation] =
			ControlInfo(0.0f, 2.0f, kDefaultSaturation);
	return 0;
}'''
assert o in s
s=s.replace(o,'''int Adjust::init(IPAContext &context, const ValueNode &tuningData)
{
	/*
	 * Optional per-sensor defaults, e.g. in <sensor>.yaml:
	 *   - Adjust:
	 *       contrast: 1.3
	 *       saturation: 1.3
	 * Applications can still override them with the controls.
	 */
	defaultContrast_ = tuningData["contrast"].get<float>();
	if (defaultContrast_)
		defaultContrast_ = std::clamp(*defaultContrast_, 0.0f, 2.0f);
	defaultSaturation_ = tuningData["saturation"].get<float>();
	if (defaultSaturation_)
		defaultSaturation_ = std::clamp(*defaultSaturation_, 0.0f, 2.0f);

	context.ctrlMap[&controls::Gamma] =
		ControlInfo(0.1f, 10.0f, kDefaultGamma);
	context.ctrlMap[&controls::Contrast] =
		ControlInfo(0.0f, 2.0f, defaultContrast_.value_or(kDefaultContrast));
	if (context.ccmEnabled)
		context.ctrlMap[&controls::Saturation] =
			ControlInfo(0.0f, 2.0f, defaultSaturation_.value_or(kDefaultSaturation));
	return 0;
}''')
o='''	context.activeState.knobs.contrast = std::optional<float>();
	context.activeState.knobs.saturation = std::optional<float>();'''
assert o in s
s=s.replace(o,'''	context.activeState.knobs.contrast = defaultContrast_;
	context.activeState.knobs.saturation = defaultSaturation_;''')
if '#include <algorithm>' not in s:
    s=s.replace('#include "adjust.h"\n','#include "adjust.h"\n\n#include <algorithm>\n',1)
open(p,'w').write(s)

# patch 4 v3: GPU debayer samples whole texels (consistent Bayer phase).
p='src/libcamera/shaders/bayer_unpacked.frag'; s=open(p).read()
o='void main(void) {\n'
assert s.count(o)==1
s=s.replace(o, """uniform vec2            tex_size;
uniform vec2            tex_bayer_first_red;
uniform float           stride_factor;

""" + o)
o='    float C = fetch(center.x, center.y); // ( 0, 0)\n'
assert s.count(o)==1
s=s.replace(o, """    /*
     * Cosmo: pick one input texel per output pixel and derive both the Bayer
     * phase and every sample position from it. The interpolated position
     * lands exactly on texel boundaries at 2:1 scaling and crosses them at
     * other scales; deriving the phase (floor) and the samples (continuous)
     * separately then disagree, which tints the border and leaves single
     * wrong-colour rows/columns. Neighbours outside the image wrap by two
     * texels (same Bayer colour).
     */
    vec2 cosmo_p = floor(center.zw - tex_bayer_first_red + 0.25);
    vec2 cosmo_alt = mod(cosmo_p + tex_bayer_first_red, 2.0);
    vec4 cosmo_xi = cosmo_p.x + vec4(-2.0, -1.0, 1.0, 2.0);
    vec4 cosmo_yi = cosmo_p.y + vec4(-2.0, -1.0, 1.0, 2.0);
    cosmo_xi += 2.0 * step(cosmo_xi, vec4(-0.5)) -
                2.0 * step(vec4(tex_size.x - 0.5), cosmo_xi);
    cosmo_yi += 2.0 * step(cosmo_yi, vec4(-0.5)) -
                2.0 * step(vec4(tex_size.y - 0.5), cosmo_yi);
    vec4 cosmo_center = vec4((cosmo_p.x + 0.5) / tex_size.x * stride_factor,
                             (cosmo_p.y + 0.5) / tex_size.y, 0.0, 0.0);
    vec4 cosmo_x = (cosmo_xi + 0.5) / tex_size.x * stride_factor;
    vec4 cosmo_y = (cosmo_yi + 0.5) / tex_size.y;
    #define center cosmo_center
    #define xCoord cosmo_x
    #define yCoord cosmo_y

""" + o)
o='    vec2 alternate = mod(floor(center.zw), 2.0);\n'
assert s.count(o)==1
s=s.replace(o,'    vec2 alternate = cosmo_alt;\n')
open(p,'w').write(s)

print('patches 1-4 applied')
if '--no-14bit' in sys.argv:
    sys.exit(0)
# libcamera 0.7.1 patch 5: unpacked 14-bit Bayer input (16-bit words, 14 significant
# bits; MediaTek CAMSV PAK bypass delivers 10-bit data MSB-aligned to 14 bits).
def rep(path, old, new, count=1):
    s = open(path).read()
    assert s.count(old) >= 1, (path, old[:60])
    s = s.replace(old, new, count)
    open(path, 'w').write(s)

# --- statistics (CPU, used by both debayer paths)
rep('include/libcamera/internal/software_isp/swstats_cpu.h',
    '\tvoid statsBGGR12Line0(const uint8_t *src[], SwIspStats &stats);\n',
    '\tvoid statsBGGR12Line0(const uint8_t *src[], SwIspStats &stats);\n'
    '\t/* Bayer 14 bpp (16-bit words) */\n'
    '\tvoid statsBGGR14Line0(const uint8_t *src[], SwIspStats &stats);\n')
p = 'src/libcamera/software_isp/swstats_cpu.cpp'
s = open(p).read()
a = s.index('void SwStatsCpu::statsBGGR12Line0(')
b = s.index('\n}\n', a) + 3
f12 = s[a:b]
# 0.7.2: stats sums are scaled to 8 bpp units by sumShift_ (upstream fix for
# the AWB black level), so the 14-bit line function is the 12-bit one with a
# divide by 64 for the Y histogram, and sumShift_ = 6.
f14 = (f12.replace('statsBGGR12Line0', 'statsBGGR14Line0')
          .replace('divide Y by 16 for 12 -> 8 bpp value', 'divide Y by 64 for 14 -> 8 bpp value')
          .replace('SWSTATS_ACCUMULATE_LINE_STATS(16)', 'SWSTATS_ACCUMULATE_LINE_STATS(64)'))
assert f14 != f12 and 'ACCUMULATE_LINE_STATS(64)' in f14
s = s[:b] + '\n' + f14 + s[b:]
old = '\t\tcase 12:\n\t\t\tstats0_ = &SwStatsCpu::statsBGGR12Line0;\n\t\t\tsumShift_ = 4;\n\t\t\treturn 0;\n'
assert old in s
s = s.replace(old, old + '\t\tcase 14:\n\t\t\tstats0_ = &SwStatsCpu::statsBGGR14Line0;\n\t\t\tsumShift_ = 6;\n\t\t\treturn 0;\n', 1)
open(p, 'w').write(s)

# --- CPU debayer
rep('src/libcamera/software_isp/debayer_cpu.h',
    '\tvoid debayer12_GRGR_BGR888(uint8_t *dst, const uint8_t *src[]);\n',
    '\tvoid debayer12_GRGR_BGR888(uint8_t *dst, const uint8_t *src[]);\n'
    '\t/* unpacked 14-bit raw bayer format */\n'
    '\ttemplate<bool addAlphaByte, bool ccmEnabled>\n'
    '\tvoid debayer14_BGBG_BGR888(uint8_t *dst, const uint8_t *src[]);\n'
    '\ttemplate<bool addAlphaByte, bool ccmEnabled>\n'
    '\tvoid debayer14_GRGR_BGR888(uint8_t *dst, const uint8_t *src[]);\n')
p = 'src/libcamera/software_isp/debayer_cpu.cpp'
s = open(p).read()
a = s.index('template<bool addAlphaByte, bool ccmEnabled>\nvoid DebayerCpu::debayer12_BGBG_BGR888(')
b = s.index('template<bool addAlphaByte, bool ccmEnabled>\nvoid DebayerCpu::debayer10P_BGBG_BGR888(')
f12 = s[a:b]
f14 = (f12.replace('debayer12_', 'debayer14_')
          .replace('divide values by 16 for 12 -> 8 bpp value', 'divide values by 64 for 14 -> 8 bpp value')
          .replace('(1, 1, 16)', '(1, 1, 64)'))
assert f14.count('(1, 1, 64)') == 4
s = s[:b] + f14 + s[b:]
old = '(bayerFormat.bitDepth == 8 || bayerFormat.bitDepth == 10 || bayerFormat.bitDepth == 12) &&'
assert s.count(old) == 2
s = s.replace(old, '(bayerFormat.bitDepth == 8 || bayerFormat.bitDepth == 10 ||\n\t     bayerFormat.bitDepth == 12 || bayerFormat.bitDepth == 14) &&')
old = '\t\tcase 12:\n\t\t\tSET_DEBAYER_METHODS(debayer12_BGBG_BGR888, debayer12_GRGR_BGR888)\n\t\t\tbreak;\n'
assert old in s
s = s.replace(old, old + '\t\tcase 14:\n\t\t\tSET_DEBAYER_METHODS(debayer14_BGBG_BGR888, debayer14_GRGR_BGR888)\n\t\t\tbreak;\n', 1)
open(p, 'w').write(s)

# --- GPU debayer
p = 'src/libcamera/software_isp/debayer_egl.cpp'
s = open(p).read()
old = '(bayerFormat.bitDepth == 8 || bayerFormat.bitDepth == 10) &&'
assert s.count(old) == 1
s = s.replace(old, '(bayerFormat.bitDepth == 8 || bayerFormat.bitDepth == 10 ||\n\t     bayerFormat.bitDepth == 14) &&')
# first-red table (first switch): add the 14-bit formats next to the 8-bit ones
for o in ('SBGGR', 'SGBRG', 'SGRBG', 'SRGGB'):
    old = '\tcase libcamera::formats::%s8:\n\tcase libcamera::formats::%s10_CSI2P:\n' % (o, o)
    assert old in s, o
    s = s.replace(old, '\tcase libcamera::formats::%s8:\n\tcase libcamera::formats::%s14:\n\tcase libcamera::formats::%s10_CSI2P:\n' % (o, o, o), 1)
# shader selection (second switch)
old = ('\tcase libcamera::formats::SBGGR10_CSI2P:\n\tcase libcamera::formats::SGBRG10_CSI2P:\n'
       '\tcase libcamera::formats::SGRBG10_CSI2P:\n\tcase libcamera::formats::SRGGB10_CSI2P:\n'
       '\t\tegl_.pushEnv(shaderEnv, "#define RAW10P");\n')
assert s.count(old) == 1
s = s.replace(old, '''	case libcamera::formats::SBGGR14:
	case libcamera::formats::SGBRG14:
	case libcamera::formats::SGRBG14:
	case libcamera::formats::SRGGB14:
		/* 16-bit words with 14 significant bits, read as two bytes */
		egl_.pushEnv(shaderEnv, "#define RAW14");
		fragmentShaderData = bayer_unpacked_frag;
		vertexShaderData = bayer_unpacked_vert;
		glFormat_ = GL_RG;
		bytesPerPixel_ = 2;
		break;
''' + old)
inc = '#include "debayer_egl.h"\n'
assert inc in s
s = s.replace(inc, inc + '\n#ifndef GL_RG\n#define GL_RG 0x8227\n#endif\n', 1)
open(p, 'w').write(s)

# --- shader
rep('src/libcamera/shaders/bayer_unpacked.frag',
    '    #else\n    #define fetch(x, y) texture2D(tex_y, vec2(x, y)).r\n',
    '    #elif defined(RAW14)\n'
    '    #define pixel(p) ((p.r * 255.0 + p.g * 65280.0) / 16383.0)\n'
    '    #define fetch(x, y) pixel(texture2D(tex_y, vec2(x, y)))\n'
    '    #else\n    #define fetch(x, y) texture2D(tex_y, vec2(x, y)).r\n')


print('patch 5 applied')
