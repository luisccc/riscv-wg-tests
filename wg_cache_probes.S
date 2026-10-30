#include <encoding.h>

    .section .text
    .option push
    .option norvc
    .balign 16
    .globl wg_probe_lines
    .type wg_probe_lines, @function
/* a0 = address, a1 = count, a2 = stride; return {misses, checksum} in a0/a1.
 * No stack/data accesses other than the requested loads in the counter window.
 * CSR reads retire after older loads on CVA6. The checksum makes every load useful.
 */
wg_probe_lines:
    mv t2, a0
    li t3, 0
    csrr t0, CSR_HPMCOUNTER3
    /* Depend on the snapshot so the first load cannot issue ahead of it. */
    andi t5, t0, 0
    add t2, t2, t5
1:
    ld t4, 0(t2)
    add t3, t3, t4
    add t2, t2, a2
    addi a1, a1, -1
    bnez a1, 1b
    csrr t1, CSR_HPMCOUNTER3
    sub a0, t1, t0
    mv a1, t3
    ret
    .size wg_probe_lines, .-wg_probe_lines

    .balign 16
    .globl wg_redirect_probe
    .type wg_redirect_probe, @function
/* Called in M-mode with interrupts disabled and bare addressing.
 * Preserve mtvec/mstatus/mepc. No C code or stack accesses during S-mode/trap.
 * Verify S landing, exact ECALL EPC/cause, and final M landing via a bit mask.
 */
wg_redirect_probe:
    csrr t0, mtvec
    csrr t1, mstatus
    csrr t2, mepc
    la t3, .Lwg_trap
    csrw mtvec, t3
    li t3, (3 << 11)
    csrc mstatus, t3
    li t3, (1 << 11)
    csrs mstatus, t3
    la t3, .Lwg_supervisor
    csrw mepc, t3
    li a0, 0
    mret
    j .Lwg_restore /* Detect fall-through at mret + 4. */
.Lwg_supervisor:
    addi a0, a0, 1
.Lwg_ecall:
    ecall
.Lwg_machine:
    addi a0, a0, 8
.Lwg_restore:
    csrw mtvec, t0
    csrw mepc, t2
    csrw mstatus, t1
    ret
    .balign 4
.Lwg_trap:
    csrr t3, mcause
    li t4, 9
    bne t3, t4, .Lwg_trap_return
    addi a0, a0, 2
    csrr t3, mepc
    la t4, .Lwg_ecall
    bne t3, t4, .Lwg_trap_return
    addi a0, a0, 4
.Lwg_trap_return:
    li t3, (3 << 11)
    csrs mstatus, t3
    la t3, .Lwg_machine
    csrw mepc, t3
    mret
    j .Lwg_restore
    .size wg_redirect_probe, .-wg_redirect_probe
    .balign 16
    .globl wg_csr_redirect_probe
    .type wg_csr_redirect_probe, @function
/* a0 = temporary WID, a1 = original mwid, including its unlocked lock bit. */
wg_csr_redirect_probe:
    li t0, 0
    csrw CSR_MWID, a0
    addi t0, t0, 1
    csrw CSR_MWID, a1
    addi t0, t0, 1
    mv a0, t0
    ret
    .size wg_csr_redirect_probe, .-wg_csr_redirect_probe
    .balign 16
    .globl wg_sharing_probe
    .type wg_sharing_probe, @function
/* a0 = data, a1 = five probe_result records. S-mode never accesses the stack or
 * framework globals: WID-tagged copies of writable shared data need not cohere.
 * All result stores are made in M-mode, outside the measurement windows.
 */
wg_sharing_probe:
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
    mv s0, a1
    mv s1, a0
    csrr s2, mtvec
    csrr s3, mstatus
    csrr s4, mepc
    li s9, 0
    li a1, 8
    li a2, 16
    call wg_probe_lines
    sd a0, 0(s0)
    sd a1, 8(s0)
    mv a0, s1
    li a1, 8
    li a2, 16
    call wg_probe_lines
    sd a0, 16(s0)
    sd a1, 24(s0)
    la t0, .Lsharing_trap
    csrw mtvec, t0
    li t0, (3 << 11)
    csrc mstatus, t0
    li t0, (1 << 11)
    csrs mstatus, t0
    la t0, .Lsharing_s
    csrw mepc, t0
    mret
    j .Lsharing_restore
.Lsharing_s:
    mv a0, s1
    li a1, 8
    li a2, 16
    call wg_probe_lines
    mv s5, a0
    mv s6, a1
    mv a0, s1
    li a1, 8
    li a2, 16
    call wg_probe_lines
    mv s7, a0
    mv s8, a1
.Lsharing_ecall:
    ecall
.Lsharing_m:
    mv a0, s1
    li a1, 8
    li a2, 16
    call wg_probe_lines
    sd s5, 32(s0)
    sd s6, 40(s0)
    sd s7, 48(s0)
    sd s8, 56(s0)
    sd a0, 64(s0)
    sd a1, 72(s0)
.Lsharing_restore:
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
.Lsharing_trap:
    csrr t0, mcause
    li t1, 9
    bne t0, t1, .Lsharing_bad_trap
    csrr t0, mepc
    la t1, .Lsharing_ecall
    bne t0, t1, .Lsharing_bad_trap
    li s9, 1
    la t0, .Lsharing_m
    j .Lsharing_return
.Lsharing_bad_trap:
    li s9, 0
    la t0, .Lsharing_restore
.Lsharing_return:
    csrw mepc, t0
    li t0, (3 << 11)
    csrs mstatus, t0
    mret
    j .Lsharing_restore
    .size wg_sharing_probe, .-wg_sharing_probe
    .option pop
