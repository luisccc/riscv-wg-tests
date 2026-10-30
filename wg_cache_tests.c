#include <rv_wg_test.h>

/* Geometry of cv64a6_imafdch_sv39. Keep this in sync with the model. */
#define WG_LINE_BYTES 16
#define WG_LINES 8
#define WG_WID_MASK 7ULL
#define WG_DCACHE_CSR 0x7c1
#define WG_HPM3_BIT (1ULL << 3)

#ifndef WG_DCACHE_FLUSH_ON_SWITCH
#define WG_DCACHE_FLUSH_ON_SWITCH 1
#endif
#if WG_DCACHE_FLUSH_ON_SWITCH != 0 && WG_DCACHE_FLUSH_ON_SWITCH != 1
#error WG_DCACHE_FLUSH_ON_SWITCH must be 0 or 1
#endif

/* Reserved by the linker through .bss; never overlaps hard-coded test addresses.
 * Only one word per line is measured. All worlds read the same physical memory.
 */
static volatile uint64_t probe_data[WG_LINES][WG_LINE_BYTES / sizeof(uint64_t)]
    __attribute__((aligned(4096)));
static volatile uint64_t flush_completion __attribute__((aligned(4096)));

struct probe_result { uint64_t misses, checksum; };
extern struct probe_result wg_probe_lines(const volatile void *, uint64_t, uint64_t);
extern uint64_t wg_redirect_probe(void);
extern uint64_t wg_csr_redirect_probe(uint64_t, uint64_t);

struct saved_state {
    uint64_t mwid, mlwid, mcounteren, inhibit, event3, counter3, dcache;
};

static struct saved_state setup(uint64_t *machine_wid)
{
    struct saved_state s = {
        .mwid = CSRR(CSR_MWID), .mlwid = CSRR(CSR_MLWID),
        .mcounteren = CSRR(mcounteren), .inhibit = CSRR(mcountinhibit),
        .event3 = CSRR(CSR_MHPMEVENT3), .counter3 = CSRR(CSR_MHPMCOUNTER3),
        .dcache = CSRR(WG_DCACHE_CSR),
    };
    *machine_wid = s.mwid & WG_WID_MASK;
    CSRW(CSR_MLWID, *machine_wid);
    CSRS(mcounteren, WG_HPM3_BIT);
    CSRS(mcountinhibit, WG_HPM3_BIT);
    CSRW(CSR_MHPMEVENT3, 2);
    CSRW(CSR_MHPMCOUNTER3, 0);
    CSRC(mcountinhibit, WG_HPM3_BIT);

    for (unsigned i = 0; i < WG_LINES; ++i) {
        probe_data[i][0] = i + 1;
        probe_data[i][1] = 0;
    }
    asm volatile("fence iorw, iorw" ::: "memory");
    /* WT re-enable walks/invalidates the cache before enabling allocations.
     * An unrelated read waits for that operation to finish before measurement.
     */
    CSRW(WG_DCACHE_CSR, 0);
    CSRW(WG_DCACHE_CSR, 1);
    (void)flush_completion;
    return s;
}

static void restore(struct saved_state s)
{
    goto_priv(PRIV_M);
    CSRW(CSR_MLWID, s.mlwid);
    CSRS(mcountinhibit, WG_HPM3_BIT);
    CSRW(CSR_MHPMEVENT3, s.event3);
    CSRW(CSR_MHPMCOUNTER3, s.counter3);
    CSRW(mcounteren, s.mcounteren);
    CSRW(WG_DCACHE_CSR, s.dcache);
    CSRW(mcountinhibit, s.inhibit);
}

#define CHECK_PROBE(label, result, expected_misses) \
    TEST_ASSERT(label " misses", (result).misses == (expected_misses), \
                "got %lu, expected %lu", (unsigned long)(result).misses, \
                (unsigned long)(expected_misses)); \
    TEST_ASSERT(label " contents", (result).checksum == 36, \
                "checksum %lu, expected 36", (unsigned long)(result).checksum)

bool dcache_cold_warm(void)
{
    TEST_START();
    uint64_t wid;
    struct saved_state s = setup(&wid);
    struct probe_result cold = wg_probe_lines(probe_data, WG_LINES, WG_LINE_BYTES);
    struct probe_result warm = wg_probe_lines(probe_data, WG_LINES, WG_LINE_BYTES);
    struct probe_result spatial = wg_probe_lines(&probe_data[0][1], WG_LINES, WG_LINE_BYTES);
    restore(s);
    DETAIL("D$ misses: cold=%lu warm=%lu spatial=%lu", cold.misses,
           warm.misses, spatial.misses);
    CHECK_PROBE("cold", cold, WG_LINES);
    CHECK_PROBE("warm", warm, 0);
    TEST_ASSERT("same-line second words hit", spatial.misses == 0 && spatial.checksum == 0,
                "misses %lu, sum %lu", (unsigned long)spatial.misses,
                (unsigned long)spatial.checksum);
    TEST_END();
}

struct sharing_result {
    struct probe_result m_first, m_warm, s_first, s_warm, m_return;
};
extern uint64_t wg_sharing_probe(const volatile void *, struct sharing_result *);

static struct sharing_result sharing(bool different)
{
    uint64_t wid;
    struct saved_state s = setup(&wid);
    CSRW(CSR_MLWID, different ? ((wid + 1) & WG_WID_MASK) : wid);
    struct sharing_result r;

    if (!wg_sharing_probe(probe_data, &r))
        ERROR("unexpected trap in cache sharing probe");
    restore(s);
    
    DETAIL("D$ misses: M cold=%lu M warm=%lu S first=%lu S warm=%lu M return=%lu",
           r.m_first.misses, r.m_warm.misses, r.s_first.misses,
           r.s_warm.misses, r.m_return.misses);
    return r;
}

#define CHECK_SHARING(r, different) \
    CHECK_PROBE("M cold", (r).m_first, WG_LINES); \
    CHECK_PROBE("M warm", (r).m_warm, 0); \
    CHECK_PROBE("S first", (r).s_first, (different) ? WG_LINES : 0); \
    CHECK_PROBE("S warm", (r).s_warm, 0); \
    CHECK_PROBE("M return", (r).m_return, \
                (different) && WG_DCACHE_FLUSH_ON_SWITCH ? WG_LINES : 0)

bool dcache_same_wid_ms(void)
{
    TEST_START();
    struct sharing_result r = sharing(false);
    CHECK_SHARING(r, false);
    TEST_END();
}

bool dcache_different_wid_ms(void)
{
    TEST_START();
    struct sharing_result r = sharing(true);
    CHECK_SHARING(r, true);
    TEST_END();
}

/*
    Verifies that the microarchitectural handling of World changes does not 
    alter the architectural execution path. mret, traps/ecall, repeated World 
    switches, and direct writes to the active WID must all resume at the correct next PC.
*/
bool world_switch_redirect(void)
{
    TEST_START();
    uint64_t wid;
    struct saved_state s = setup(&wid);
    // Test that the different transitions do nt corrupt something
    // Earlier versions had a bug where during exceptions, mepc could be corrupted
    uint64_t same = wg_redirect_probe();

    // Test the same, but with a different WID
    CSRW(CSR_MLWID, (wid + 1) & WG_WID_MASK);
    uint64_t different = wg_redirect_probe();
    uint64_t repeated = wg_redirect_probe();

    bool unlocked = !(s.mwid >> 63);
    uint64_t csr_markers = 0;
    if (unlocked)
        csr_markers = wg_csr_redirect_probe((wid + 1) & WG_WID_MASK, s.mwid);

    uint64_t restored_mwid = CSRR(CSR_MWID);
    restore(s);

    TEST_ASSERT("same-WID mret/ecall targets", same == 15, "markers 0x%lx", same);
    TEST_ASSERT("different-WID mret/ecall targets", different == 15,
                "markers 0x%lx", different);
    TEST_ASSERT("repeated transition targets", repeated == 15, "markers 0x%lx", repeated);
    TEST_ASSERT("mwid is unlocked for CSR redirect test", unlocked, "mwid 0x%lx", s.mwid);
    TEST_ASSERT("active CSR writes resume at next instruction",
                csr_markers == 2 && restored_mwid == s.mwid,
                "markers %lu, mwid 0x%lx", csr_markers, restored_mwid);
    TEST_END();
}
