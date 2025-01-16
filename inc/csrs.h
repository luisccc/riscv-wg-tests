
#ifndef CSRS_H
#define CSRS_H

#include <util.h>

#define STVEC_MODE_OFF (0)
#define STVEC_MODE_LEN (2)
#define STVEC_MODE_MSK BIT_MASK(STVEC_MODE_OFF, STVEC_MODE_LEN)
#define STVEC_MODE_DIRECT (0)
#define STVEC_MODE_VECTRD (1)

#if (RV64)
#define SATP_MODE_OFF (60)
#define SATP_MODE_DFLT SATP_MODE_39
#define SATP_ASID_OFF (44)
#define SATP_ASID_LEN (16)
#define HGATP_VMID_OFF (44)
#define HGATP_VMID_LEN (14)
#elif (RV32)
#define SATP_MODE_OFF (31)
#define SATP_MODE_DFLT SATP_MODE_32
#define SATP_ASID_OFF (22)
#define SATP_ASID_LEN (9)
#define HGATP_VMID_OFF (22)
#define HGATP_VMID_LEN (7)
#endif


#define MSTATUS_MPRV_OFF    (17)
#define MSTATUS_MPRV    (1ULL << MSTATUS_MPRV_OFF)
#define MSTATUS_TW_OFF  (21)
#define MSTATUS_TW   (1ULL << MSTATUS_TW_OFF)
#define MSTATUS_GVA_OFF    (38)
#define MSTATUS_GVA    (1ULL << MSTATUS_GVA_OFF)

#define SATP_MODE_BARE (0ULL << SATP_MODE_OFF)
#define SATP_MODE_32 (1ULL << SATP_MODE_OFF)
#define SATP_MODE_39 (8ULL << SATP_MODE_OFF)
#define SATP_MODE_48 (9ULL << SATP_MODE_OFF)
#define SATP_ASID_MSK BIT_MASK(SATP_ASID_OFF, SATP_ASID_LEN)

#define HGATP_MODE_OFF SATP_MODE_OFF
#define HGATP_MODE_DFLT SATP_MODE_DFLT
#define HGATP_VMID_MSK BIT_MASK(HGATP_VMID_OFF, HGATP_VMID_LEN)

#define SSTATUS_UIE_BIT (1ULL << 0)
#define SSTATUS_SIE_BIT (1ULL << 1)
#define SSTATUS_UPIE_BIT (1ULL << 4)
#define SSTATUS_SPIE_BIT (1ULL << 5)
#define SSTATUS_SPP_BIT (1ULL << 8)
#define SSTATUS_FS_OFF (13)
#define SSTATUS_FS_LEN (2)
#define SSTATUS_FS_MSK BIT_MASK(SSTATUS_FS_OFF, SSTATUS_FS_LEN)
#define SSTATUS_FS_AOFF (0)
#define SSTATUS_FS_INITIAL (1ULL << SSTATUS_FS_OFF)
#define SSTATUS_FS_CLEAN (2ULL << SSTATUS_FS_OFF)
#define SSTATUS_FS_DIRTY (3ULL << SSTATUS_FS_OFF)
#define SSTATUS_XS_OFF (15)
#define SSTATUS_XS_LEN (2)
#define SSTATUS_XS_MSK BIT_MASK(SSTATUS_XS_OFF, SSTATUS_XS_LEN)
#define SSTATUS_XS_AOFF (0)
#define SSTATUS_XS_INITIAL (1ULL << SSTATUS_XS_OFF)
#define SSTATUS_XS_CLEAN (2ULL << SSTATUS_XS_OFF)
#define SSTATUS_XS_DIRTY (3ULL << SSTATUS_XS_OFF)
#define SSTATUS_SUM (1ULL << 18)
#define SSTATUS_MXR (1ULL << 19)
#define SSTATUS_SD (1ULL << 63)

#define SIE_USIE (1ULL << 0)
#define SIE_SSIE (1ULL << 1)
#define SIE_UTIE (1ULL << 4)
#define SIE_STIE (1ULL << 5)
#define SIE_UEIE (1ULL << 8)
#define SIE_SEIE (1ULL << 9)

#define SIP_USIP SIE_USIE
#define SIP_SSIP SIE_SSIE
#define SIP_UTIP SIE_UTIE
#define SIP_STIP SIE_STIE
#define SIP_UEIP SIE_UEIE
#define SIP_SEIP SIE_SEIE

#define HIE_VSSIE (1ULL << 2)
#define HIE_VSTIE (1ULL << 6)
#define HIE_VSEIE (1ULL << 10)
#define HIE_SGEIE (1ULL << 12)

#define HIP_VSSIP HIE_VSSIE
#define HIP_VSTIP HIE_VSTIE
#define HIP_VSEIP HIE_VSEIE
#define HIP_SGEIP HIE_SGEIE

#define CAUSE_INT_BIT (1ULL << 63)
#define CAUSE_MSK (CAUSE_INT_BIT - 1)
#define CAUSE_USI (0 | CAUSE_INT_BIT)
#define CAUSE_SSI (1 | CAUSE_INT_BIT)
#define CAUSE_VSSI (2 | CAUSE_INT_BIT)
#define CAUSE_UTI (4 | CAUSE_INT_BIT)
#define CAUSE_STI (5 | CAUSE_INT_BIT)
#define CAUSE_VSTI (6 | CAUSE_INT_BIT)
#define CAUSE_UEI (8 | CAUSE_INT_BIT)
#define CAUSE_SEI (9 | CAUSE_INT_BIT)
#define CAUSE_VSEI (10 | CAUSE_INT_BIT)
#define CAUSE_IAM (0)
#define CAUSE_IAF (1)
#define CAUSE_ILI (2)
#define CAUSE_BKP (3)
#define CAUSE_LAM (4)
#define CAUSE_LAF (5)
#define CAUSE_SAM (6)
#define CAUSE_SAF (7)
#define CAUSE_ECU (8)
#define CAUSE_ECS (9)
#define CAUSE_ECV (10)
#define CAUSE_ECM (11)
#define CAUSE_IPF (12)
#define CAUSE_LPF (13)
#define CAUSE_SPF (15)
#define CAUSE_IGPF (20)
#define CAUSE_LGPF (21)
#define CAUSE_VRTI (22)
#define CAUSE_SGPF (23)

#define HIDELEG_USI SIP_USIP
#define HIDELEG_SSI SIP_SSIP
#define HIDELEG_UTI SIP_UTIP
#define HIDELEG_STI SIP_STIP
#define HIDELEG_UEI SIP_UEIP
#define HIDELEG_SEI SIP_SEIP
#define HIDELEG_VSSI HIP_VSSIP
#define HIDELEG_VSTI HIP_VSTIP
#define HIDELEG_VSEI HIP_VSEIP
#define HIDELEG_SGEI HIP_SGEIP

#define HEDELEG_IAM (1ULL << 0)
#define HEDELEG_IAF (1ULL << 1)
#define HEDELEG_ILI (1ULL << 2)
#define HEDELEG_BKP (1ULL << 3)
#define HEDELEG_LAM (1ULL << 4)
#define HEDELEG_LAF (1ULL << 5)
#define HEDELEG_SAM (1ULL << 6)
#define HEDELEG_SAF (1ULL << 7)
#define HEDELEG_ECU (1ULL << 8)
#define HEDELEG_ECS (1ULL << 9)
#define HEDELEG_ECV (1ULL << 10)
#define HEDELEG_IPF (1ULL << 12)
#define HEDELEG_LPF (1ULL << 13)
#define HEDELEG_SPF (1ULL << 15)

#define MISA_H (1ULL << 7)

#define HSTATUS_VSBE (1ULL << 5)
#define HSTATUS_GVA_OFF (6)
#define HSTATUS_GVA (1ULL << HSTATUS_GVA_OFF)
#define HSTATUS_SPV (1ULL << 7)
#define HSTATUS_SPVP (1ULL << 8)
#define HSTATUS_HU (1ULL << 9)
#define HSTATUS_VTVM (1ULL << 20)
#define HSTATUS_VTW (1ULL << 21)
#define HSTATUS_VTSR (1ULL << 22)
#define HSTATUS_VSXL_OFF (32)
#define HSTATUS_VSXL_LEN (2)
#define HSTATUS_VSXL_MSK (BIT_MASK(HSTATUS_VSXL_OFF, HSTATUS_VSXL_LEN))
#define HSTATUS_VSXL_32 (1ULL << HSTATUS_VSXL_OFF)
#define HSTATUS_VSXL_64 (2ULL << HSTATUS_VSXL_OFF)

#define HCOUNTEREN_CY   (1ULL << 0)
#define HCOUNTEREN_TM   (1ULL << 1)   
#define HCOUNTEREN_IR   (1ULL << 2)
#define HCOUNTEREN(N)   (1ULL << (N))

#define TINST_ADDROFF_OFF (15)
#define TINST_ADDROFF_LEN (5)
#define TINST_ADDROFF_MASK BIT_MASK(TINST_ADDROFF_OFF, TINST_ADDROFF_LEN)
#define TINST_ADDROFF TINST_ADDROFF_MASK

#endif /* __ARCH_CSRS_H__ */
