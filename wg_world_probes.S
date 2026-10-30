#include <encoding.h>

/* struct wg_trap layout (must match wg_world_tests.c) */
#define TRAP_TRIGGERED 0
#define TRAP_CAUSE     8
#define TRAP_EPC       16
#define TRAP_TVAL      24
#define TRAP_TVAL2     32
#define TRAP_TINST     40
#define TRAP_GVA       48
#define TRAP_MPV       56
#define TRAP_RDATA     64
#define TRAP_RDATA2    72

/* probe kinds (must match enum wg_kind in wg_world_tests.c) */
#define KIND_LOAD    0
#define KIND_STORE   1
#define KIND_AMOADD  2
#define KIND_AMOSWAP 3
#define KIND_LRSC    4
#define KIND_FETCH   5

#define MSTATUS_MPP_M   (3 << 11)
#define MSTATUS_MPRV_B  (1 << 17)
#define MSTATUS_MPV_B   0x8000000000
#define MSTATUS_GVA_SH  38
#define MSTATUS_MPV_SH  39

    .section .text
    .option push
    .option norvc
    .balign 16
    .globl wg_world_probe
    .type wg_world_probe, @function
/* a0 = kind, a1 = address, a2 = write data, a3 = mstatus bits (MPP/MPV, plus MPRV for
 * data kinds), a4 = struct wg_trap *.
 * Called in M-mode. Performs exactly one access in the world selected by a3 and always
 * returns in M-mode. A local trap vector records the trap; medeleg is cleared so every
 * trap lands here. No C code or stack accesses while MPRV is set or below M-mode.
 */
wg_world_probe:
    csrr t0, mtvec
    csrr t1, mstatus
    csrr t2, mepc
    csrr t3, medeleg
    sd zero, TRAP_TRIGGERED(a4)
    li t4, -1
    sd t4, TRAP_CAUSE(a4)
    sd zero, TRAP_EPC(a4)
    sd zero, TRAP_TVAL(a4)
    sd zero, TRAP_TVAL2(a4)
    sd zero, TRAP_TINST(a4)
    sd zero, TRAP_GVA(a4)
    sd zero, TRAP_MPV(a4)
    sd zero, TRAP_RDATA(a4)
    sd t4, TRAP_RDATA2(a4)
    csrw medeleg, zero
    la t4, .Lworld_trap
    csrw mtvec, t4
    li t4, MSTATUS_MPP_M | MSTATUS_MPRV_B
    csrc mstatus, t4
    li t4, MSTATUS_MPV_B
    csrc mstatus, t4
    li t4, KIND_FETCH
    beq a0, t4, .Lworld_fetch
    li t5, 0
    li t6, -1
    csrs mstatus, a3
    li t4, KIND_LOAD
    beq a0, t4, .Lworld_load
    li t4, KIND_STORE
    beq a0, t4, .Lworld_store
    li t4, KIND_AMOADD
    beq a0, t4, .Lworld_amoadd
    li t4, KIND_AMOSWAP
    beq a0, t4, .Lworld_amoswap
    j .Lworld_lrsc
.Lworld_load:
    .globl wg_world_ld
wg_world_ld:
    ld t5, 0(a1)
    j .Lworld_data_done
.Lworld_store:
    .globl wg_world_sd
wg_world_sd:
    sd a2, 0(a1)
    j .Lworld_data_done
.Lworld_amoadd:
    .globl wg_world_amoadd
wg_world_amoadd:
    amoadd.d t5, a2, (a1)
    j .Lworld_data_done
.Lworld_amoswap:
    .globl wg_world_amoswap
wg_world_amoswap:
    amoswap.d t5, a2, (a1)
    j .Lworld_data_done
.Lworld_lrsc:
    .globl wg_world_lr
wg_world_lr:
    lr.d t5, (a1)
    .globl wg_world_sc
wg_world_sc:
    sc.d t6, a2, (a1)
.Lworld_data_done:
    li t4, MSTATUS_MPRV_B
    csrc mstatus, t4
    sd t5, TRAP_RDATA(a4)
    sd t6, TRAP_RDATA2(a4)
.Lworld_restore:
    csrw mtvec, t0
    csrw mepc, t2
    csrw medeleg, t3
    csrw mstatus, t1
    ret
.Lworld_fetch:
    csrs mstatus, a3
    csrw mepc, a1
    mret
    j .Lworld_restore /* Detect fall-through at mret + 4. */

    .balign 4
.Lworld_trap:
    li t4, 1
    sd t4, TRAP_TRIGGERED(a4)
    csrr t4, mcause
    sd t4, TRAP_CAUSE(a4)
    csrr t4, mepc
    sd t4, TRAP_EPC(a4)
    csrr t4, mtval
    sd t4, TRAP_TVAL(a4)
    csrr t4, CSR_MTVAL2
    sd t4, TRAP_TVAL2(a4)
    csrr t4, CSR_MTINST
    sd t4, TRAP_TINST(a4)
    csrr t4, mstatus
    srli t5, t4, MSTATUS_GVA_SH
    andi t5, t5, 1
    sd t5, TRAP_GVA(a4)
    srli t5, t4, MSTATUS_MPV_SH
    andi t5, t5, 1
    sd t5, TRAP_MPV(a4)
    /* Return to M-mode at the restore path, with MPRV and MPV cleared. */
    li t4, MSTATUS_MPRV_B
    csrc mstatus, t4
    li t4, MSTATUS_MPV_B
    csrc mstatus, t4
    li t4, MSTATUS_MPP_M
    csrs mstatus, t4
    la t4, .Lworld_restore
    csrw mepc, t4
    mret
    .size wg_world_probe, .-wg_world_probe

/* Position independent ecall stub executed in the lower world. It is either run in place
 * (bare fetch tests) or copied to a mapped page (PTW fetch tests).
 */
    .balign 16
    .globl wg_world_stub
    .type wg_world_stub, @function
wg_world_stub:
    ecall
1:
    j 1b
    .globl wg_world_stub_end
wg_world_stub_end:
    .size wg_world_stub, .-wg_world_stub
    .option pop
