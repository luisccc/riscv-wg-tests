#include <rv_wg_test.h>
#include <page_tables.h>

/* World checker validation for the cv64a6_imafdch_sv39 model (8 worlds, Smlwidlist,
 * Smwiddeleg and Sswid enabled, PMWIDLIST/PMLWIDLIST all ones). Every access runs through
 * wg_world_probe, which does one access in the selected world and always returns to M.
 *
 * Worlds used: mlwidlist = {1,2}, mwiddeleg = {2}. S/HS world (mlwid): 1 is allowed,
 * 3 is not. U/VS/VU world with delegation (slwid): 2 is allowed, 1 is not.
 */
#define WG_DCACHE_CSR 0x7c1

#define WID_LIST   0x6ULL  /* worlds 1 and 2 */
#define WID_DELEG  0x4ULL  /* world 2 */
#define WID_S_GOOD 1ULL
#define WID_S_BAD  3ULL
#define WID_U_GOOD 2ULL
#define WID_U_BAD  1ULL

#define ST_MPP_S (1ULL << 11)
#define ST_MPP_U (0ULL)
#define ST_MPRV  MSTATUS_MPRV
#define ST_MPV   MSTATUS_MPV

#define WID_FAULT_TVAL 4

enum wg_kind { WG_LOAD, WG_STORE, WG_AMOADD, WG_AMOSWAP, WG_LRSC, WG_FETCH };

/* Layout must match wg_world_probes.S */
struct wg_trap {
    uint64_t triggered, cause, epc, tval, tval2, tinst, gva, mpv, rdata, rdata2;
};

extern void wg_world_probe(uint64_t kind, uintptr_t addr, uint64_t wdata,
                           uint64_t mstatus_bits, struct wg_trap *out);
extern char wg_world_ld[], wg_world_sd[], wg_world_amoadd[], wg_world_amoswap[];
extern char wg_world_lr[], wg_world_stub[], wg_world_stub_end[];

static volatile uint64_t wid_buf[512] __attribute__((aligned(PAGE_SIZE)));
static volatile uint64_t wid_page[512] __attribute__((aligned(PAGE_SIZE)));
static uint32_t wid_code_page[PAGE_SIZE / sizeof(uint32_t)] __attribute__((aligned(PAGE_SIZE)));
static volatile uint64_t wid_flush_completion __attribute__((aligned(PAGE_SIZE)));

struct wid_state { uint64_t mlwid, mlwidlist, mwiddeleg, slwid, dcache; };

static struct wg_trap probe(enum wg_kind kind, uintptr_t addr, uint64_t wdata, uint64_t bits)
{
    struct wg_trap r;
    wg_world_probe(kind, addr, wdata, bits, &r);
    return r;
}

/* Lines are tagged per WID, so another world's copy can be stale. Invalidate the WT D$
 * around every cross-world handover.
 */
static void dcache_invalidate(void)
{
    asm volatile("fence rw, rw" ::: "memory");
    CSRW(WG_DCACHE_CSR, 0);
    CSRW(WG_DCACHE_CSR, 1);
    (void)wid_flush_completion;
}

static void mem_wr(volatile uint64_t *p, uint64_t v)
{
    *p = v;
    dcache_invalidate();
}

static uint64_t mem_rd(volatile uint64_t *p)
{
    dcache_invalidate();
    return *p;
}

static struct wid_state wid_save(void)
{
    struct wid_state s = {
        .mlwid = CSRR(CSR_MLWID), .mlwidlist = CSRR(CSR_MLWIDLIST),
        .mwiddeleg = CSRR(CSR_MWIDDELEG), .dcache = CSRR(WG_DCACHE_CSR),
    };
    /* slwid is only accessible while mwiddeleg != 0 */
    s.slwid = s.mwiddeleg ? CSRR(CSR_SLWID) : 0;
    return s;
}

/* mwiddeleg is masked by the current mlwidlist and slwid is only writable while
 * mwiddeleg != 0, so the write order matters. Returns whether every value read back.
 */
static bool wid_set(uint64_t list, uint64_t deleg, uint64_t slwid, uint64_t mlwid)
{
    CSRW(CSR_MLWIDLIST, list);
    CSRW(CSR_MWIDDELEG, deleg);
    if (deleg) CSRW(CSR_SLWID, slwid);
    CSRW(CSR_MLWID, mlwid);
    return CSRR(CSR_MLWIDLIST) == list && CSRR(CSR_MWIDDELEG) == deleg &&
           (!deleg || CSRR(CSR_SLWID) == slwid) && CSRR(CSR_MLWID) == mlwid;
}

static void wid_restore(struct wid_state s)
{
    /* slwid is unobservable while mwiddeleg == 0, so it only needs restoring with it */
    CSRW(CSR_MLWIDLIST, s.mlwidlist);
    CSRW(CSR_MWIDDELEG, s.mwiddeleg);
    if (s.mwiddeleg) CSRW(CSR_SLWID, s.slwid);
    CSRW(CSR_MLWID, s.mlwid);
    CSRW(satp, 0);
    CSRW(CSR_VSATP, 0);
    CSRW(CSR_HGATP, 0);
    sfence();
    hfence();
    CSRW(WG_DCACHE_CSR, s.dcache);
}

static inline void fence_i(void)
{
    asm volatile(".word 0x0000100f" ::: "memory"); /* fence.i */
}

/* TEST_ASSERT evaluates its condition more than once, so set the CSRs first */
#define WID_SET(label, list, deleg, slwid, mlwid) { \
    bool _wid_ok = wid_set(list, deleg, slwid, mlwid); \
    TEST_ASSERT("setup: " label, _wid_ok, "world CSRs did not read back as written"); \
}

#define TRAP_DETAIL(label, r) \
    DETAIL("\t" label ": triggered=%lu cause=%lu epc=0x%lx tval=0x%lx tval2=0x%lx " \
           "tinst=0x%lx gva=%lu mpv=%lu rdata=0x%lx", \
           (unsigned long)(r).triggered, (unsigned long)(r).cause, (unsigned long)(r).epc, \
           (unsigned long)(r).tval, (unsigned long)(r).tval2, (unsigned long)(r).tinst, \
           (unsigned long)(r).gva, (unsigned long)(r).mpv, (unsigned long)(r).rdata)

#define CHECK_NO_TRAP(label, r) \
    TEST_ASSERT(label " completes without trap", !(r).triggered, \
                "cause %lu epc 0x%lx tval 0x%lx", (unsigned long)(r).cause, \
                (unsigned long)(r).epc, (unsigned long)(r).tval)

#define CHECK_TRAP(label, r, exp_cause, exp_epc) \
    TEST_ASSERT(label " traps with expected cause and epc", \
                (r).triggered && (r).cause == (exp_cause) && (r).epc == (uint64_t)(exp_epc), \
                "triggered %lu cause %lu (exp %lu) epc 0x%lx (exp 0x%lx)", \
                (unsigned long)(r).triggered, (unsigned long)(r).cause, \
                (unsigned long)(exp_cause), (unsigned long)(r).epc, \
                (unsigned long)(uint64_t)(exp_epc))

#define CHECK_WID_FAULT(label, r, exp_epc) \
    CHECK_TRAP(label, r, CAUSE_SWC, exp_epc); \
    TEST_ASSERT(label " reports world fault tval/tval2/tinst", \
                (r).tval == WID_FAULT_TVAL && (r).tval2 == 0 && (r).tinst == 0, \
                "tval 0x%lx tval2 0x%lx tinst 0x%lx", (unsigned long)(r).tval, \
                (unsigned long)(r).tval2, (unsigned long)(r).tinst)

/* ------------------------------------------------------------------------------------ */

bool wid_data_s(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t a = (uintptr_t)&wid_buf[0];
    uint64_t bits = ST_MPRV | ST_MPP_S;

    WID_SET("S world allowed", WID_LIST, 0, 0, WID_S_GOOD);
    mem_wr(&wid_buf[0], 0x1111);
    struct wg_trap ld_ok = probe(WG_LOAD, a, 0, bits);
    struct wg_trap sd_ok = probe(WG_STORE, a, 0x2222, bits);
    uint64_t after_sd = mem_rd(&wid_buf[0]);
    struct wg_trap amo_ok = probe(WG_AMOADD, a, 1, bits);
    uint64_t after_amo = mem_rd(&wid_buf[0]);

    WID_SET("S world not allowed", WID_LIST, 0, 0, WID_S_BAD);
    dcache_invalidate();
    struct wg_trap ld_bad = probe(WG_LOAD, a, 0, bits);
    struct wg_trap sd_bad = probe(WG_STORE, a, 0xdead, bits);
    struct wg_trap amo_bad = probe(WG_AMOADD, a, 0x100, bits);
    uint64_t after_bad = mem_rd(&wid_buf[0]);
    wid_restore(s);

    TRAP_DETAIL("S load (allowed)", ld_ok);
    TRAP_DETAIL("S load (not allowed)", ld_bad);
    TRAP_DETAIL("S store (not allowed)", sd_bad);
    TRAP_DETAIL("S amoadd (not allowed)", amo_bad);
    CHECK_NO_TRAP("allowed S load", ld_ok);
    TEST_ASSERT("allowed S load returns memory", ld_ok.rdata == 0x1111, "got 0x%lx",
                (unsigned long)ld_ok.rdata);
    CHECK_NO_TRAP("allowed S store", sd_ok);
    TEST_ASSERT("allowed S store reaches memory", after_sd == 0x2222, "got 0x%lx",
                (unsigned long)after_sd);
    CHECK_NO_TRAP("allowed S amoadd", amo_ok);
    TEST_ASSERT("allowed S amoadd returns old value and updates memory",
                amo_ok.rdata == 0x2222 && after_amo == 0x2223, "old 0x%lx mem 0x%lx",
                (unsigned long)amo_ok.rdata, (unsigned long)after_amo);
    CHECK_WID_FAULT("disallowed S load", ld_bad, wg_world_ld);
    CHECK_WID_FAULT("disallowed S store", sd_bad, wg_world_sd);
    CHECK_WID_FAULT("disallowed S amoadd", amo_bad, wg_world_amoadd);
    TEST_ASSERT("disallowed S store/amoadd leave memory unchanged", after_bad == 0x2223,
                "got 0x%lx", (unsigned long)after_bad);
    TEST_END();
}

bool wid_data_u(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t a = (uintptr_t)&wid_buf[0];
    uint64_t bits = ST_MPRV | ST_MPP_U;
    mem_wr(&wid_buf[0], 0x3333);

    /* (a) no delegation: U uses the S world and is checked against mlwidlist */
    WID_SET("no delegation, S world allowed", WID_LIST, 0, 0, WID_S_GOOD);
    struct wg_trap nodeleg_ok = probe(WG_LOAD, a, 0, bits);
    WID_SET("no delegation, S world not allowed", WID_LIST, 0, 0, WID_S_BAD);
    dcache_invalidate();
    struct wg_trap nodeleg_bad = probe(WG_LOAD, a, 0, bits);

    /* (b) delegation with an allowed slwid: mlwid no longer matters */
    WID_SET("delegated, U world allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_BAD);
    dcache_invalidate();
    struct wg_trap deleg_ok = probe(WG_LOAD, a, 0, bits);

    /* (c) delegation with an slwid outside mwiddeleg */
    WID_SET("delegated, U world not allowed", WID_LIST, WID_DELEG, WID_U_BAD, WID_S_GOOD);
    dcache_invalidate();
    struct wg_trap deleg_bad = probe(WG_LOAD, a, 0, bits);
    wid_restore(s);

    TRAP_DETAIL("U load, no delegation, mlwid not allowed", nodeleg_bad);
    TRAP_DETAIL("U load, delegated, slwid not allowed", deleg_bad);
    CHECK_NO_TRAP("U load without delegation, allowed mlwid", nodeleg_ok);
    TEST_ASSERT("U load without delegation returns memory", nodeleg_ok.rdata == 0x3333,
                "got 0x%lx", (unsigned long)nodeleg_ok.rdata);
    CHECK_WID_FAULT("U load without delegation, disallowed mlwid", nodeleg_bad, wg_world_ld);
    CHECK_NO_TRAP("U load with delegation, allowed slwid", deleg_ok);
    TEST_ASSERT("U load with delegation returns memory", deleg_ok.rdata == 0x3333,
                "got 0x%lx", (unsigned long)deleg_ok.rdata);
    CHECK_WID_FAULT("U load with delegation, disallowed slwid", deleg_bad, wg_world_ld);
    TEST_END();
}

bool wid_fetch_su(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t stub = (uintptr_t)wg_world_stub;

    WID_SET("S world allowed", WID_LIST, 0, 0, WID_S_GOOD);
    struct wg_trap s_ok = probe(WG_FETCH, stub, 0, ST_MPP_S);
    WID_SET("S world not allowed", WID_LIST, 0, 0, WID_S_BAD);
    struct wg_trap s_bad = probe(WG_FETCH, stub, 0, ST_MPP_S);
    WID_SET("U world allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_GOOD);
    struct wg_trap u_ok = probe(WG_FETCH, stub, 0, ST_MPP_U);
    WID_SET("U world not allowed", WID_LIST, WID_DELEG, WID_U_BAD, WID_S_GOOD);
    struct wg_trap u_bad = probe(WG_FETCH, stub, 0, ST_MPP_U);
    wid_restore(s);

    TRAP_DETAIL("S fetch (not allowed)", s_bad);
    TRAP_DETAIL("U fetch (not allowed)", u_bad);
    CHECK_TRAP("allowed S fetch reaches the ecall", s_ok, CAUSE_ECS, stub);
    CHECK_WID_FAULT("disallowed S fetch", s_bad, stub);
    CHECK_TRAP("allowed U fetch reaches the ecall", u_ok, CAUSE_ECU, stub);
    CHECK_WID_FAULT("disallowed U fetch", u_bad, stub);
    TEST_END();
}

bool wid_virtual(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t a = (uintptr_t)&wid_buf[0];
    uintptr_t stub = (uintptr_t)wg_world_stub;
    mem_wr(&wid_buf[0], 0x4444);

    /* bare vsatp/hgatp: VS and VU are both checked against mwiddeleg with slwid */
    WID_SET("virtual world allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_GOOD);
    struct wg_trap vs_ld_ok = probe(WG_LOAD, a, 0, ST_MPRV | ST_MPV | ST_MPP_S);
    struct wg_trap vu_ld_ok = probe(WG_LOAD, a, 0, ST_MPRV | ST_MPV | ST_MPP_U);
    struct wg_trap vs_if_ok = probe(WG_FETCH, stub, 0, ST_MPV | ST_MPP_S);
    struct wg_trap vu_if_ok = probe(WG_FETCH, stub, 0, ST_MPV | ST_MPP_U);

    WID_SET("virtual world not allowed", WID_LIST, WID_DELEG, WID_U_BAD, WID_S_GOOD);
    dcache_invalidate();
    struct wg_trap vs_ld_bad = probe(WG_LOAD, a, 0, ST_MPRV | ST_MPV | ST_MPP_S);
    struct wg_trap vu_ld_bad = probe(WG_LOAD, a, 0, ST_MPRV | ST_MPV | ST_MPP_U);
    struct wg_trap vs_if_bad = probe(WG_FETCH, stub, 0, ST_MPV | ST_MPP_S);
    struct wg_trap vu_if_bad = probe(WG_FETCH, stub, 0, ST_MPV | ST_MPP_U);
    wid_restore(s);

    TRAP_DETAIL("VS load (not allowed)", vs_ld_bad);
    TRAP_DETAIL("VU load (not allowed)", vu_ld_bad);
    TRAP_DETAIL("VS fetch (not allowed)", vs_if_bad);
    TRAP_DETAIL("VU fetch (not allowed)", vu_if_bad);
    CHECK_NO_TRAP("allowed VS load", vs_ld_ok);
    CHECK_NO_TRAP("allowed VU load", vu_ld_ok);
    TEST_ASSERT("allowed VS/VU loads return memory",
                vs_ld_ok.rdata == 0x4444 && vu_ld_ok.rdata == 0x4444, "VS 0x%lx VU 0x%lx",
                (unsigned long)vs_ld_ok.rdata, (unsigned long)vu_ld_ok.rdata);
    CHECK_TRAP("allowed VS fetch reaches the ecall", vs_if_ok, CAUSE_ECV, stub);
    CHECK_TRAP("allowed VU fetch reaches the ecall", vu_if_ok, CAUSE_ECU, stub);
    CHECK_WID_FAULT("disallowed VS load", vs_ld_bad, wg_world_ld);
    CHECK_WID_FAULT("disallowed VU load", vu_ld_bad, wg_world_ld);
    CHECK_WID_FAULT("disallowed VS fetch", vs_if_bad, stub);
    CHECK_WID_FAULT("disallowed VU fetch", vu_if_bad, stub);
    /* mstatus.MPV saves the hart's V. The fetches really run in VS/VU, but the loads are
     * MPRV+MPV probes issued from M (V=0): only ld_st_v selects the guest world.
     */
    TEST_ASSERT("virtual fetch faults are taken from V=1", vs_if_bad.mpv && vu_if_bad.mpv,
                "mpv VS %lu VU %lu", (unsigned long)vs_if_bad.mpv,
                (unsigned long)vu_if_bad.mpv);
    TEST_ASSERT("MPRV virtual load faults are taken from V=0", !vs_ld_bad.mpv && !vu_ld_bad.mpv,
                "mpv VS %lu VU %lu", (unsigned long)vs_ld_bad.mpv,
                (unsigned long)vu_ld_bad.mpv);
    TEST_END();
}

bool wid_ptw_data(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t va = vs_page_base(VSURWX_GRWX);
    uint64_t bits = ST_MPRV | ST_MPP_U;

    /* U world allowed through delegation, so only the walker (S world) can fault */
    WID_SET("U world allowed, S world allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_GOOD);
    hspt_init();
    hspt[2][VSURWX_GRWX] = ((uintptr_t)wid_page >> 2) | PTE_AD | PTE_V | PTE_U | PTE_RWX;
    mem_wr(&wid_page[0], 0x5555);
    sfence();
    struct wg_trap ok = probe(WG_LOAD, va, 0, bits);

    WID_SET("U world allowed, S world not allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_BAD);
    sfence();
    dcache_invalidate();
    struct wg_trap bad = probe(WG_LOAD, va, 0, bits);
    wid_restore(s);

    DETAIL("\twaveform: PTE reads hspt[0][%lu]@0x%lx hspt[1][%lu]@0x%lx hspt[2][%lu]@0x%lx "
           "tagged with mlwid (allowed run %lu, faulting run must issue none); "
           "data read 0x%lx tagged with slwid %lu",
           (unsigned long)PTE_INDEX(0, va), (unsigned long)&hspt[0][PTE_INDEX(0, va)],
           (unsigned long)PTE_INDEX(1, va), (unsigned long)&hspt[1][PTE_INDEX(1, va)],
           (unsigned long)PTE_INDEX(2, va), (unsigned long)&hspt[2][PTE_INDEX(2, va)],
           (unsigned long)WID_S_GOOD, (unsigned long)wid_page, (unsigned long)WID_U_GOOD);
    TRAP_DETAIL("U load, walker world not allowed", bad);
    CHECK_NO_TRAP("translated U load, allowed walker world", ok);
    TEST_ASSERT("translated U load returns the mapped page", ok.rdata == 0x5555, "got 0x%lx",
                (unsigned long)ok.rdata);
    CHECK_WID_FAULT("translated U load, disallowed walker world", bad, wg_world_ld);
    TEST_END();
}

bool wid_ptw_fetch(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t va = vs_page_base(VSURWX_GRWX);
    size_t stub_words = (wg_world_stub_end - wg_world_stub) / sizeof(uint32_t);

    for (size_t i = 0; i < stub_words; ++i)
        wid_code_page[i] = ((uint32_t *)wg_world_stub)[i];
    dcache_invalidate();
    fence_i();

    WID_SET("U world allowed, S world allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_GOOD);
    hspt_init();
    hspt[2][VSURWX_GRWX] = ((uintptr_t)wid_code_page >> 2) | PTE_AD | PTE_V | PTE_U | PTE_RWX;
    dcache_invalidate();
    sfence();
    struct wg_trap ok = probe(WG_FETCH, va, 0, ST_MPP_U);

    WID_SET("U world allowed, S world not allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_BAD);
    sfence();
    dcache_invalidate();
    struct wg_trap bad = probe(WG_FETCH, va, 0, ST_MPP_U);
    wid_restore(s);

    DETAIL("\twaveform: code page 0x%lx mapped at 0x%lx, PTE hspt[2][%lu]@0x%lx",
           (unsigned long)wid_code_page, (unsigned long)va, (unsigned long)VSURWX_GRWX,
           (unsigned long)&hspt[2][VSURWX_GRWX]);
    TRAP_DETAIL("U fetch, walker world not allowed", bad);
    CHECK_TRAP("translated U fetch reaches the ecall", ok, CAUSE_ECU, va);
    CHECK_WID_FAULT("translated U fetch, disallowed walker world", bad, va);
    TEST_END();
}

bool wid_ptw_gstage(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t va = vs_page_base(VSRWX_GURWX);
    uint64_t bits = ST_MPRV | ST_MPV | ST_MPP_S;

    /* VS world (slwid) allowed, so only the G-stage reads (HS world) can fault */
    WID_SET("VS world allowed, HS world allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_GOOD);
    vspt_init();
    hpt_init();
    hpt[2][VSRWX_GURWX] = ((uintptr_t)wid_page >> 2) | PTE_AD | PTE_V | PTE_U | PTE_RWX;
    mem_wr(&wid_page[0], 0x6666);
    hfence();
    struct wg_trap ok = probe(WG_LOAD, va, 0, bits);

    WID_SET("VS world allowed, HS world not allowed", WID_LIST, WID_DELEG, WID_U_GOOD, WID_S_BAD);
    hfence();
    dcache_invalidate();
    struct wg_trap bad = probe(WG_LOAD, va, 0, bits);
    wid_restore(s);

    DETAIL("\twaveform: guest tables vspt@0x%lx read with slwid %lu, G-stage tables "
           "hpt_root@0x%lx hpt@0x%lx read with mlwid %lu, data 0x%lx read with slwid %lu",
           (unsigned long)vspt, (unsigned long)WID_U_GOOD, (unsigned long)hpt_root,
           (unsigned long)hpt, (unsigned long)WID_S_GOOD, (unsigned long)wid_page,
           (unsigned long)WID_U_GOOD);
    TRAP_DETAIL("VS load, G-stage world not allowed", bad);
    CHECK_NO_TRAP("two-stage VS load, allowed worlds", ok);
    TEST_ASSERT("two-stage VS load returns the mapped page", ok.rdata == 0x6666, "got 0x%lx",
                (unsigned long)ok.rdata);
    CHECK_WID_FAULT("two-stage VS load, disallowed G-stage world", bad, wg_world_ld);
    TEST_END();
}

bool wid_amo(void)
{
    TEST_START();
    struct wid_state s = wid_save();
    uintptr_t a = (uintptr_t)&wid_buf[1];
    uint64_t sbits = ST_MPRV | ST_MPP_S;

    WID_SET("S world allowed", WID_LIST, 0, 0, WID_S_GOOD);

    /* (a) M world */
    mem_wr(&wid_buf[1], 10);
    struct wg_trap m_add = probe(WG_AMOADD, a, 5, 0);
    uint64_t m_add_mem = mem_rd(&wid_buf[1]);
    struct wg_trap m_swap = probe(WG_AMOSWAP, a, 7, 0);
    uint64_t m_swap_mem = mem_rd(&wid_buf[1]);
    struct wg_trap m_lrsc = probe(WG_LRSC, a, 9, 0);
    uint64_t m_lrsc_mem = mem_rd(&wid_buf[1]);

    /* (b) S world via MPRV */
    mem_wr(&wid_buf[1], 20);
    struct wg_trap s_add = probe(WG_AMOADD, a, 5, sbits);
    uint64_t s_add_mem = mem_rd(&wid_buf[1]);
    struct wg_trap s_swap = probe(WG_AMOSWAP, a, 7, sbits);
    uint64_t s_swap_mem = mem_rd(&wid_buf[1]);
    struct wg_trap s_lrsc = probe(WG_LRSC, a, 9, sbits);
    uint64_t s_lrsc_mem = mem_rd(&wid_buf[1]);

    /* (c) S world not allowed */
    WID_SET("S world not allowed", WID_LIST, 0, 0, WID_S_BAD);
    dcache_invalidate();
    struct wg_trap bad_add = probe(WG_AMOADD, a, 0x100, sbits);
    struct wg_trap bad_swap = probe(WG_AMOSWAP, a, 0x200, sbits);
    struct wg_trap bad_lrsc = probe(WG_LRSC, a, 0x300, sbits);
    uint64_t bad_mem = mem_rd(&wid_buf[1]);
    uint64_t mwid = CSRR(CSR_MWID) & 0x7;
    wid_restore(s);

    DETAIL("\twaveform: AMOs to 0x%lx tagged with mwid %lu (M), then mlwid %lu (S)",
           (unsigned long)a, (unsigned long)mwid, (unsigned long)WID_S_GOOD);
    TRAP_DETAIL("S amoadd (not allowed)", bad_add);
    TRAP_DETAIL("S lr.d (not allowed)", bad_lrsc);
    CHECK_NO_TRAP("M amoadd", m_add);
    CHECK_NO_TRAP("M amoswap", m_swap);
    CHECK_NO_TRAP("M lr/sc", m_lrsc);
    TEST_ASSERT("M AMOs return old values and update memory",
                m_add.rdata == 10 && m_add_mem == 15 && m_swap.rdata == 15 &&
                m_swap_mem == 7 && m_lrsc.rdata == 7 && m_lrsc.rdata2 == 0 && m_lrsc_mem == 9,
                "add %lu/%lu swap %lu/%lu lr %lu sc %lu mem %lu",
                (unsigned long)m_add.rdata, (unsigned long)m_add_mem,
                (unsigned long)m_swap.rdata, (unsigned long)m_swap_mem,
                (unsigned long)m_lrsc.rdata, (unsigned long)m_lrsc.rdata2,
                (unsigned long)m_lrsc_mem);
    CHECK_NO_TRAP("allowed S amoadd", s_add);
    CHECK_NO_TRAP("allowed S amoswap", s_swap);
    CHECK_NO_TRAP("allowed S lr/sc", s_lrsc);
    TEST_ASSERT("allowed S AMOs return old values and update memory",
                s_add.rdata == 20 && s_add_mem == 25 && s_swap.rdata == 25 &&
                s_swap_mem == 7 && s_lrsc.rdata == 7 && s_lrsc.rdata2 == 0 && s_lrsc_mem == 9,
                "add %lu/%lu swap %lu/%lu lr %lu sc %lu mem %lu",
                (unsigned long)s_add.rdata, (unsigned long)s_add_mem,
                (unsigned long)s_swap.rdata, (unsigned long)s_swap_mem,
                (unsigned long)s_lrsc.rdata, (unsigned long)s_lrsc.rdata2,
                (unsigned long)s_lrsc_mem);
    CHECK_WID_FAULT("disallowed S amoadd", bad_add, wg_world_amoadd);
    CHECK_WID_FAULT("disallowed S amoswap", bad_swap, wg_world_amoswap);
    CHECK_WID_FAULT("disallowed S lr.d", bad_lrsc, wg_world_lr);
    TEST_ASSERT("disallowed S AMOs leave memory unchanged", bad_mem == 9, "got %lu",
                (unsigned long)bad_mem);
    TEST_END();
}
