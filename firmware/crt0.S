/* Start-up and trap entry for the Pocket firmware.
 *
 * The APF loads the whole image (code, data) into SDRAM at 0x10000000
 * before the CPU leaves reset, so there is nothing to copy: clear .bss,
 * set the stack, install the trap vector, enable the timer interrupt
 * (mstatus.MIE is set by main once the game state is ready) and run. */
    .section .text.start, "ax"
    .globl _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, _stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:  la      t0, trap_entry
    csrw    mtvec, t0
    li      t0, 0x80                /* mie.MTIE */
    csrw    mie, t0
    call    main
    li      a0, 0
    call    plat_halt

/* The only interrupt is the SoC timer (mip.MTIP).  Save what the C
 * calling convention lets the handler clobber. */
    .section .text.trap, "ax"
    .balign 4
trap_entry:
    addi    sp, sp, -64
    sw      ra, 0(sp)
    sw      t0, 4(sp)
    sw      t1, 8(sp)
    sw      t2, 12(sp)
    sw      a0, 16(sp)
    sw      a1, 20(sp)
    sw      a2, 24(sp)
    sw      a3, 28(sp)
    sw      a4, 32(sp)
    sw      a5, 36(sp)
    sw      a6, 40(sp)
    sw      a7, 44(sp)
    sw      t3, 48(sp)
    sw      t4, 52(sp)
    sw      t5, 56(sp)
    sw      t6, 60(sp)
    csrr    t0, mcause
    bltz    t0, 1f                  /* interrupt */
    /* an exception: report it (simulation) and stop */
    mv      a0, t0
    csrr    a1, mepc
    csrr    a2, mtval
    call    pocket_exception
1:  call    pocket_timer_irq
    lw      ra, 0(sp)
    lw      t0, 4(sp)
    lw      t1, 8(sp)
    lw      t2, 12(sp)
    lw      a0, 16(sp)
    lw      a1, 20(sp)
    lw      a2, 24(sp)
    lw      a3, 28(sp)
    lw      a4, 32(sp)
    lw      a5, 36(sp)
    lw      a6, 40(sp)
    lw      a7, 44(sp)
    lw      t3, 48(sp)
    lw      t4, 52(sp)
    lw      t5, 56(sp)
    lw      t6, 60(sp)
    addi    sp, sp, 64
    mret

/* end of the game, or an exception: tell the simulation, then wait */
    .section .text.halt, "ax"
    .globl plat_halt
plat_halt:
    li      t0, 0x40000028          /* R_SIM_EXIT */
    sw      a0, 0(t0)
1:  wfi
    j       1b

/* save states (savestate.c).
 * ss_setjmp(ctx): the callee-saved registers, sp, ra, gp, tp into ctx; 0.
 * ss_resume(regions, n, ctx): with interrupts off, copy n regions
 * { dst, len, src } (word sized, len a multiple of 4) using registers only,
 * since the stack being run on is one of them, then return 1 from the
 * ss_setjmp that filled ctx (which the copy has just put back). */
    .section .text.ss, "ax"
    .globl ss_setjmp
ss_setjmp:
    sw      ra, 0(a0)
    sw      sp, 4(a0)
    sw      gp, 8(a0)
    sw      tp, 12(a0)
    sw      s0, 16(a0)
    sw      s1, 20(a0)
    sw      s2, 24(a0)
    sw      s3, 28(a0)
    sw      s4, 32(a0)
    sw      s5, 36(a0)
    sw      s6, 40(a0)
    sw      s7, 44(a0)
    sw      s8, 48(a0)
    sw      s9, 52(a0)
    sw      s10, 56(a0)
    sw      s11, 60(a0)
    li      a0, 0
    ret

    .globl ss_resume
ss_resume:
    csrci   mstatus, 8
1:  beqz    a1, 3f
    lw      t0, 0(a0)
    lw      t1, 4(a0)
    lw      t2, 8(a0)
2:  beqz    t1, 4f
    lw      t3, 0(t2)
    sw      t3, 0(t0)
    addi    t0, t0, 4
    addi    t2, t2, 4
    addi    t1, t1, -4
    j       2b
4:  addi    a0, a0, 12
    addi    a1, a1, -1
    j       1b
3:  lw      ra, 0(a2)
    lw      sp, 4(a2)
    lw      gp, 8(a2)
    lw      tp, 12(a2)
    lw      s0, 16(a2)
    lw      s1, 20(a2)
    lw      s2, 24(a2)
    lw      s3, 28(a2)
    lw      s4, 32(a2)
    lw      s5, 36(a2)
    lw      s6, 40(a2)
    lw      s7, 44(a2)
    lw      s8, 48(a2)
    lw      s9, 52(a2)
    lw      s10, 56(a2)
    lw      s11, 60(a2)
    li      a0, 1
    ret
