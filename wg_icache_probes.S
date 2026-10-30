#include <encoding.h>

    .section .text
    .option push
    .option norvc
    .balign 16
    .globl wg_icache_measure
    .type wg_icache_measure, @function
/* a0 = the *same physical* target address in M and S; a1 = execution marker.
 * First line: snapshot then a dependent jump, so the target cannot be fetched
 * before the snapshot. Second line: snapshot immediately after target returns.
 * The second measurement line can add one incidental miss on its first use;
 * callers assert cold>0 and warm==0, not an exact global miss count.
 * No memory/stack accesses or handler instructions occur in this window.
 */
wg_icache_measure:
    csrr t0, CSR_HPMCOUNTER3
    andi t1, t0, 0
    add a0, a0, t1
    jalr t4, 0(a0)
    csrr t1, CSR_HPMCOUNTER3
    sub a0, t1, t0
    ret
    .size wg_icache_measure, .-wg_icache_measure

    .balign 16
    .globl wg_icache_target
    .type wg_icache_target, @function
/* Exactly one 16-byte physical I-cache line; the marker proves execution. */
wg_icache_target:
    addi a1, a1, 1
    jalr x0, 0(t4)
    .size wg_icache_target, .-wg_icache_target

    .balign 16
    .globl wg_icache_sharing_probe
    .type wg_icache_sharing_probe, @function
/* a0 = five {misses, executions} records. All stores and stack accesses occur
 * in M-mode only. Keep target address in s1 through M->S->M transitions.
 * Local trap verifies supervisor ECALL and returns to the M continuation.
 */
wg_icache_sharing_probe:
    addi sp, sp, -96
    sd ra, 0(sp)
    sd s0, 8(sp)
    sd s1, 16(sp)
    sd s2, 24(sp)
    sd s3, 32(sp)
    sd s4, 40(sp)
    sd s5, 48(sp)
    sd s6, 56(sp)
    sd s7, 64(sp)
    sd s8, 72(sp)
    sd s9, 80(sp)
    mv s0, a0
    la s1, wg_icache_target
    csrr s2, mtvec
    csrr s3, mstatus
    csrr s4, mepc
    li s9, 0
    mv a0, s1
    li a1, 0
    call wg_icache_measure
    sd a0, 0(s0)
    sd a1, 8(s0)
    mv a0, s1
    li a1, 0
    call wg_icache_measure
    sd a0, 16(s0)
    sd a1, 24(s0)
    la t0, .Licache_trap
    csrw mtvec, t0
    li t0, (3 << 11)
    csrc mstatus, t0
    li t0, (1 << 11)
    csrs mstatus, t0
    la t0, .Licache_s
    csrw mepc, t0
    mret
    j .Licache_restore
.Licache_s:
    mv a0, s1
    li a1, 0
    call wg_icache_measure
    mv s5, a0
    mv s6, a1
    mv a0, s1
    li a1, 0
    call wg_icache_measure
    mv s7, a0
    mv s8, a1
.Licache_ecall:
    ecall
.Licache_m:
    mv a0, s1
    li a1, 0
    call wg_icache_measure
    sd s5, 32(s0)
    sd s6, 40(s0)
    sd s7, 48(s0)
    sd s8, 56(s0)
    sd a0, 64(s0)
    sd a1, 72(s0)
.Licache_restore:
    csrw mtvec, s2
    csrw mepc, s4
    csrw mstatus, s3
    mv a0, s9
    ld ra, 0(sp)
    ld s0, 8(sp)
    ld s1, 16(sp)
    ld s2, 24(sp)
    ld s3, 32(sp)
    ld s4, 40(sp)
    ld s5, 48(sp)
    ld s6, 56(sp)
    ld s7, 64(sp)
    ld s8, 72(sp)
    ld s9, 80(sp)
    addi sp, sp, 96
    ret
    .balign 4
.Licache_trap:
    csrr t0, mcause
    li t1, 9
    bne t0, t1, .Licache_bad_trap
    csrr t0, mepc
    la t1, .Licache_ecall
    bne t0, t1, .Licache_bad_trap
    li s9, 1
    la t0, .Licache_m
    j .Licache_return
.Licache_bad_trap:
    li s9, 0
    la t0, .Licache_restore
.Licache_return:
    csrw mepc, t0
    li t0, (3 << 11)
    csrs mstatus, t0
    mret
    j .Licache_restore
    .size wg_icache_sharing_probe, .-wg_icache_sharing_probe
    .option pop
