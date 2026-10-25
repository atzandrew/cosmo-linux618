/*
 * fbpaint: bring-up probe that stands in for the kernel.
 *
 * Packed exactly like a kernel (arm64 Image header, gzip, DTB appended) so
 * LK loads and jumps to it. Paints LK's two enabled OVL layer buffers in
 * different solid colours, then parks the CPU:
 *   layer 0  0x7df70000, stride 4352: green
 *   layer 3  0x7e878000, stride 4320: blue
 * Green on glass = LK jumps here and layer 0 is visible; blue = layer 3 is
 * on top; unchanged = no jump or a frozen display. Entered with the MMU and
 * D-cache off (arm64 boot protocol), so stores reach DRAM directly.
 */
	.section .text
	.globl	_start
_start:
	b	paint			/* code0 */
	.long	0			/* code1 */
	.quad	0			/* text_offset */
	.quad	0x200000		/* image_size */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0, 0, 0			/* res2-4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 */
paint:
	ldr	x0, =0x7df70000		/* layer 0 buffer */
	ldr	x1, =(4352 * 2160 / 4)
	ldr	w2, =0xff00ff00		/* A B G R bytes, little endian: green either way */
1:	str	w2, [x0], #4
	subs	x1, x1, #1
	b.ne	1b
	ldr	x0, =0x7e878000		/* layer 3 buffer */
	ldr	x1, =(4320 * 2160 / 4)
	ldr	w2, =0xffff0000		/* blue in a8b8g8r8, red in a8r8g8b8 */
2:	str	w2, [x0], #4
	subs	x1, x1, #1
	b.ne	2b
	dsb	sy
3:	wfe
	b	3b
	.ltorg
