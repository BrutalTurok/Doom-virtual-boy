/*
 * Fast timer interrupt for PCM sound effects (~5.5 kHz).
 *
 * Mixes the two PCM voices (vbpcm[], see vb_sound.c) into a 4-bit stereo
 * level and writes it to VSU channel 5's L/R level register. Only the
 * registers it touches are saved. About 140 times per second (music tick)
 * it restores them and jumps into the generic C interrupt path instead,
 * which calls timer_music_isr() through _timVector.
 */
	.section .text
	.align	1

	.global	_vb_timer_isr

PCM_HZ  = 5556
MUS_HZ  = 140

_vb_timer_isr:
	addi	-40, sp, sp
	st.w	r6, 0[sp]
	st.w	r7, 4[sp]
	st.w	r8, 8[sp]
	st.w	r9, 12[sp]
	st.w	r10, 16[sp]
	st.w	r11, 20[sp]
	st.w	r12, 24[sp]
	st.w	r13, 28[sp]
	st.w	r30, 32[sp]

	/* TCR = enable | 20us  (interrupt off -> zero status dropped) */
	movhi	0x0200, r0, r13
	movea	0x11, r0, r6
	st.b	r6, 0x20[r13]

#ifdef VBPROF
	/* profiling build: record the interrupted PC into cart RAM ring (emulator only) */
	stsr	sr0, r7			/* EIPC */
	movhi	hi(_vbprof_idx), r0, r8
	movea	lo(_vbprof_idx), r8, r8
	ld.w	0[r8], r9
	movhi	0x0600, r0, r10
	mov	r9, r11
	shl	2, r11
	add	r11, r10
	st.w	r7, 0[r10]
	add	1, r9
	andi	0x3FFF, r9, r9
	st.w	r9, 0[r8]
#endif

	mov	r0, r10			/* left accumulator  */
	mov	r0, r11			/* right accumulator */
	movhi	hi(_vbpcm), r0, r6
	movea	lo(_vbpcm), r6, r6

	movea	2, r0, r12		/* voices */
.Lvoice:
	ld.w	0[r6], r7		/* data */
	cmp	r0, r7
	be	.Lnext
	ld.w	4[r6], r8		/* pos */
	mov	r8, r9
	shr	1, r9
	add	r7, r9
	ld.b	0[r9], r9		/* packed byte (sign-extended) */
	mov	r8, r7
	andi	1, r7, r7
	cmp	r0, r7
	bne	.Lhigh
	shl	28, r9			/* low nibble */
	br	.Lnib
.Lhigh:
	shl	24, r9			/* high nibble */
.Lnib:
	sar	28, r9			/* signed -8..7 */
	ld.w	12[r6], r7		/* vl */
	mul	r9, r7
	add	r7, r10
	ld.w	16[r6], r7		/* vr */
	mul	r9, r7
	add	r7, r11
	add	1, r8
	ld.w	8[r6], r7		/* len */
	cmp	r7, r8
	blt	.Lstore
	st.w	r0, 0[r6]		/* finished */
.Lstore:
	st.w	r8, 4[r6]
.Lnext:
	addi	32, r6, r6
	add	-1, r12
	bne	.Lvoice

	/* levels: (acc >> 4) + 8, clamped to 0..15 */
	movea	15, r0, r12
	sar	4, r10
	add	8, r10
	cmp	r0, r10
	bge	1f
	mov	r0, r10
1:	cmp	r12, r10
	ble	2f
	mov	r12, r10
2:	sar	4, r11
	add	8, r11
	cmp	r0, r11
	bge	3f
	mov	r0, r11
3:	cmp	r12, r11
	ble	4f
	mov	r12, r11
4:	shl	4, r10
	or	r11, r10
	movhi	0x0100, r0, r6
	st.b	r10, 0x504[r6]		/* VSU channel 5 SxLRV */

	/* music tick due? */
	movhi	hi(_vbsnd_mus_acc), r0, r6
	movea	lo(_vbsnd_mus_acc), r6, r6
	ld.w	0[r6], r7
	addi	MUS_HZ, r7, r7
	movea	PCM_HZ, r0, r8
	cmp	r8, r7
	blt	.Lnomusic
	sub	r8, r7
	st.w	r7, 0[r6]
	/* restore and take the full C path (it re-arms the timer) */
	ld.w	0[sp], r6
	ld.w	4[sp], r7
	ld.w	8[sp], r8
	ld.w	12[sp], r9
	ld.w	16[sp], r10
	ld.w	20[sp], r11
	ld.w	24[sp], r12
	ld.w	28[sp], r13
	ld.w	32[sp], r30
	addi	40, sp, sp
	jr	__interrupt_handler_prolog

.Lnomusic:
	st.w	r7, 0[r6]
	/* TCR = enable | 20us | interrupt */
	movea	0x19, r0, r6
	st.b	r6, 0x20[r13]
	ld.w	0[sp], r6
	ld.w	4[sp], r7
	ld.w	8[sp], r8
	ld.w	12[sp], r9
	ld.w	16[sp], r10
	ld.w	20[sp], r11
	ld.w	24[sp], r12
	ld.w	28[sp], r13
	ld.w	32[sp], r30
	addi	40, sp, sp
	reti
