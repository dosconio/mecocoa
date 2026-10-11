// ASCII ASM TAB4 LF
// ModuTitle: User library - ARM EABI compiler runtime (64-bit divide with remainder)

// The compiler emits calls to these helpers when the target has no single
// instruction for the operation, so no libgcc or compiler-rt is needed.
// AAPCS: numerator in r0:r1, denominator in r2:r3,
// results as quotient in r0:r1 and remainder in r2:r3.

	.syntax unified
	.thumb
	.text

	.global __aeabi_uldivmod
	.thumb_func
__aeabi_uldivmod:
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r4, #0	// quotient low
	movs	r5, #0	// quotient high
	movs	r6, #0	// remainder low
	movs	r7, #0	// remainder high
	movs	r10, #64	// one bit per iteration, most significant first
udiv_loop:
	adds	r0, r0, r0	// numerator <<= 1, carry holds its old bit 63
	adcs	r1, r1, r1
	adcs	r6, r6, r6	// remainder = (remainder << 1) | that bit
	adcs	r7, r7, r7
	adds	r4, r4, r4	// quotient <<= 1, the new bit is set below
	adcs	r5, r5, r5
	subs	r8, r6, r2	// remainder - denominator
	sbcs	r9, r7, r3
	bcc	udiv_next	// borrow: remainder is smaller, keep the quotient bit 0
	movs	r6, r8
	movs	r7, r9
	orrs	r4, r4, #1
udiv_next:
	sub	r10, r10, #1
	cmp	r10, #0
	bne	udiv_loop
	movs	r0, r4
	movs	r1, r5
	movs	r2, r6
	movs	r3, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}

	.global __aeabi_ldivmod
	.thumb_func
__aeabi_ldivmod:
	push	{r4, r5, lr}
	movs	r4, #0	// denominator was negative
	movs	r5, #0	// numerator was negative
	cmp	r1, #0
	bpl	ldiv_den	// numerator is non-negative
	movs	r5, #1
	mvns	r0, r0	// negate the numerator
	mvns	r1, r1
	adds	r0, r0, #1
	adcs	r1, r1, #0
ldiv_den:
	cmp	r3, #0
	bpl	ldiv_go	// denominator is non-negative
	movs	r4, #1
	mvns	r2, r2	// negate the denominator
	mvns	r3, r3
	adds	r2, r2, #1
	adcs	r3, r3, #0
ldiv_go:
	eors	r4, r4, r5	// the quotient is negative only if the signs differ
	bl	__aeabi_uldivmod
	cmp	r5, #0
	beq	ldiv_quot	// the remainder takes the numerator's sign
	mvns	r2, r2
	mvns	r3, r3
	adds	r2, r2, #1
	adcs	r3, r3, #0
ldiv_quot:
	cmp	r4, #0
	beq	ldiv_done
	mvns	r0, r0
	mvns	r1, r1
	adds	r0, r0, #1
	adcs	r1, r1, #0
ldiv_done:
	pop	{r4, r5, pc}
