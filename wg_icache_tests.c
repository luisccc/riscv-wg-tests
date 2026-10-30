#include <rv_wg_test.h>

/* cv64a6_imafdch_sv39: eight worlds, 16-byte I$ lines, four ways. */
#define WG_WID_MASK 7ULL
#define WG_ICACHE_CSR 0x7c0
#define WG_HPM3_BIT (1ULL << 3)

struct icache_result { uint64_t misses, executions; };
struct icache_sharing_result {
    struct icache_result m_first, m_warm, s_first, s_warm, m_return;
};
extern uint64_t wg_icache_sharing_probe(struct icache_sharing_result *);
extern void wg_icache_target(void);

struct icache_saved_state {
    uint64_t mlwid, mcounteren, inhibit, event3, counter3, icache;
};

static struct icache_saved_state icache_setup(uint64_t *machine_wid)
{
    struct icache_saved_state s = {
        .mlwid = CSRR(CSR_MLWID), .mcounteren = CSRR(mcounteren),
        .inhibit = CSRR(mcountinhibit), .event3 = CSRR(CSR_MHPMEVENT3),
        .counter3 = CSRR(CSR_MHPMCOUNTER3), .icache = CSRR(WG_ICACHE_CSR),
    };
    *machine_wid = CSRR(CSR_MWID) & WG_WID_MASK;
    CSRS(mcounteren, WG_HPM3_BIT); /* S can read hpmcounter3 (0xc03). */
    CSRS(mcountinhibit, WG_HPM3_BIT);
    CSRW(CSR_MHPMEVENT3, 1);     /* CVA6 event 1: l1_icache_miss_i. */
    CSRW(CSR_MHPMCOUNTER3, 0);
    CSRC(mcountinhibit, WG_HPM3_BIT);

    /* Only the initial setup invalidates I$. Never flush between WID visits.
     * Re-enabling the CVA6 I$ clears its valid bits before accepting fetches.
     */
    CSRW(WG_ICACHE_CSR, 0);
    CSRW(WG_ICACHE_CSR, 1);
    return s;
}

static void icache_restore(struct icache_saved_state s)
{
    goto_priv(PRIV_M);
    CSRW(CSR_MLWID, s.mlwid);
    CSRS(mcountinhibit, WG_HPM3_BIT);
    CSRW(CSR_MHPMEVENT3, s.event3);
    CSRW(CSR_MHPMCOUNTER3, s.counter3);
    CSRW(mcounteren, s.mcounteren);
    CSRW(WG_ICACHE_CSR, s.icache);
    CSRW(mcountinhibit, s.inhibit);
}

static struct icache_sharing_result icache_sharing(bool different,
                                                   uint64_t *machine_wid,
                                                   uint64_t *supervisor_wid,
                                                   uint64_t *trap_ok)
{
    struct icache_saved_state s = icache_setup(machine_wid);
    *supervisor_wid = different ? ((*machine_wid + 1) & WG_WID_MASK) : *machine_wid;
    CSRW(CSR_MLWID, *supervisor_wid);
    *supervisor_wid = CSRR(CSR_MLWID) & WG_WID_MASK;
    struct icache_sharing_result r = {0};
    *trap_ok = wg_icache_sharing_probe(&r);
    icache_restore(s);
    DETAIL("I$ target %p, WIDs M=%lu S=%lu; misses M=%lu/%lu S=%lu/%lu M-return=%lu",
           wg_icache_target, (unsigned long)*machine_wid,
           (unsigned long)*supervisor_wid, (unsigned long)r.m_first.misses,
           (unsigned long)r.m_warm.misses, (unsigned long)r.s_first.misses,
           (unsigned long)r.s_warm.misses, (unsigned long)r.m_return.misses);
    return r;
}

#define CHECK_VISITS(r, different) \
    TEST_ASSERT("privilege transition and ECALL return", trap_ok == 1, \
                "trap marker %lu", (unsigned long)trap_ok); \
    TEST_ASSERT("five executions of the shared instruction line", \
                (r).m_first.executions == 1 && (r).m_warm.executions == 1 && \
                (r).s_first.executions == 1 && (r).s_warm.executions == 1 && \
                (r).m_return.executions == 1, \
                "markers %lu/%lu/%lu/%lu/%lu", \
                (unsigned long)(r).m_first.executions, \
                (unsigned long)(r).m_warm.executions, \
                (unsigned long)(r).s_first.executions, \
                (unsigned long)(r).s_warm.executions, \
                (unsigned long)(r).m_return.executions); \
    TEST_ASSERT("M cold target misses, M warm target hits", \
                (r).m_first.misses >= 1 && (r).m_warm.misses == 0, \
                "misses %lu/%lu", (unsigned long)(r).m_first.misses, \
                (unsigned long)(r).m_warm.misses); \
    TEST_ASSERT("S first visit follows WID, S warm visit hits", \
                (different) ? ((r).s_first.misses >= 1 && (r).s_warm.misses == 0) \
                            : ((r).s_first.misses == 0 && (r).s_warm.misses == 0), \
                "misses %lu/%lu", (unsigned long)(r).s_first.misses, \
                (unsigned long)(r).s_warm.misses); \
    TEST_ASSERT("return to M WID A retains its line", (r).m_return.misses == 0, \
                "misses %lu", (unsigned long)(r).m_return.misses)

bool icache_same_wid_ms(void)
{
    TEST_START();
    uint64_t machine_wid, supervisor_wid, trap_ok;
    struct icache_sharing_result r = icache_sharing(false, &machine_wid,
                                                    &supervisor_wid, &trap_ok);
    TEST_ASSERT("M and S selected the same WID", machine_wid == supervisor_wid,
                "M=%lu S=%lu", (unsigned long)machine_wid,
                (unsigned long)supervisor_wid);
    CHECK_VISITS(r, false);
    TEST_END();
}

bool icache_different_wid_ms(void)
{
    TEST_START();
    uint64_t machine_wid, supervisor_wid, trap_ok;
    struct icache_sharing_result r = icache_sharing(true, &machine_wid,
                                                    &supervisor_wid, &trap_ok);
    TEST_ASSERT("M and S selected distinct WIDs", machine_wid != supervisor_wid,
                "M=%lu S=%lu", (unsigned long)machine_wid,
                (unsigned long)supervisor_wid);
    CHECK_VISITS(r, true);
    TEST_END();
}
