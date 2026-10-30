# RISCV-WG-TESTS
## About 
This repository hosts unit tests for riscv worldguard extension.

It is based on the [baremetal hypervisor tests framework](https://github.com/josecm/riscv-hyp-tests). 
Additionally, it uses parts of the [fork](https://github.com/ninolomata/riscv-hyp-tests/tree/cva6-dev).

## CVA6 Worlds cache regression

The default registered suite is `dcache_cold_warm`, `dcache_same_wid_ms`,
`dcache_different_wid_ms`, `world_switch_redirect`, `icache_same_wid_ms`, and
`icache_different_wid_ms`, in that order. Each test
uses `TEST_START`, `TEST_ASSERT`, and `TEST_END`. Legacy exploratory tests remain
in the repository but are excluded from the default build; `wg_csrs.c` uses an
obsolete WorldGuard register map and needs porting before it can be re-enabled.

Build for the existing `cv64a6_imafdch_sv39` WT-cache model:

```sh
make PLAT=cva6 LOG_LEVEL=LOG_DETAIL WG_DCACHE_FLUSH_ON_SWITCH=0
```

If the compiler is not on PATH, supply its prefix, for example:

```sh
make PLAT=cva6 LOG_LEVEL=LOG_DETAIL WG_DCACHE_FLUSH_ON_SWITCH=0 \
  CROSS_COMPILE=/home/luis/riscv-fpga-toolchain/bin/riscv-none-elf-
```

Load `build/cva6/rv_wg_test.elf` in the system's Verilator harness. The platform
prints through the UART and reports completion through `tohost` (1 means success;
other odd values encode a nonzero exit status). Set a cycle timeout in the harness:
a broken privilege redirect can hang before software reports a failure. This
repository does not currently include that system harness.

### Model assumptions

- RV64 with H, S and U support (the inherited reset/handler framework requires H).
- Worlds enabled, eight worlds, implemented unlocked `mwid` and `mlwid`.
- Both selected WIDs may access the program, stack and probe data in physical RAM.
- WT D-cache: 32 KiB, eight ways, 16-byte lines. I-cache: 16 KiB, four ways,
  16-byte lines (256 sets). No virtual translation or PTW.
- `DcacheFlushOnFence=0`, `DcacheFlushOnFenceI=0`,
  `IcacheFlushOnWorldSwitch=0`, and `DcacheFlushOnWorldSwitch=0`, matching the
  active configuration. The I-cache test invalidates only once during its
  initial setup; it does not issue an I-cache flush between WID visits.
- `WG_DCACHE_FLUSH_ON_SWITCH` must match the model's compile-time
  `DcacheFlushOnWorldSwitch`. It changes expected results, not the RTL policy.
  Use 0 with the active model to test coexistence of different WIDs in the cache.
  Re-invoking make recompiles the test assertions when this option changes.
- One active hart, no asynchronous cache invalidations or interrupts during probes.

### Measurements and expected results

The eight-line read-only probe uses one word per line and sums values 1 through 8.
The expected checksum is 36 in every world. The cold/warm test also reads the
second word of every line: zero misses and checksum zero are expected.

| Phase | Same M/S WID | Different WIDs, D-flush off | Different WIDs, D-flush on |
|---|---:|---:|---:|
| M first pass | 8 | 8 | 8 |
| M second pass | 0 | 0 | 0 |
| S first pass | 0 | 8 | 8 |
| S second pass | 0 | 0 | 0 |
| M after return | 0 | 0 | 8 |

`LOG_DETAIL` prints measured counts plus individual assertion results. Counter 3
selects CVA6 event 2 (D-cache misses); the probe reads its `hpmcounter3` shadow in
both privilege modes. The initial address depends on the first CSR read, preventing
early issue of the first measured load. The probe has no stack accesses between
counter reads, and the privilege transition assembly accesses no framework globals
or stack in S-mode. Thus distinct WIDs do not introduce stale shared-stack copies.
Counter state, `mcounteren`, cache enable and `mlwid` are restored before test end.

Cold setup disables/re-enables the WT D-cache to invalidate it and completes an
unrelated read before measuring. Assertions and logging run after all measurements:
`TEST_ASSERT` itself issues a fence and must not be placed between warm-up and probe.
The test buffer is linker-allocated and 4 KiB aligned; no fixed scratch address is
assumed. It fits comfortably below associativity even with two WID copies.

### I-cache WID regression

The I-cache tests execute `wg_icache_target` (visible in the ELF symbol table)
five times at exactly one physical address: M/A cold, M/A warm, S/B first,
S/B warm, and M/A return. The control case sets B=A; the distinct-WID case
sets B to the next of eight WIDs. Both keep the line resident across M-to-S
`mret` and S-to-M ECALL transitions with world-switch flushing disabled. The
target contains only a marker increment and a return and fits one 16-byte
instruction line. The assembly verifies the ECALL cause/EPC and never uses the
S-mode stack or framework globals. The I-cache setup saves/restores `mlwid`,
I-cache enable, `mcounteren`, counter inhibit, selector, and counter 3.

Counter 3 selects CVA6 event 1 (`l1_icache_miss_i`). The same assembly reads
the S-accessible `hpmcounter3` CSR at 0xC03 in both modes; setup enables its
S-mode counter permission. The first counter read feeds the indirect target
jump, preventing a speculative target fetch before the snapshot. The second
read follows the target return. Only the target and possibly the measurement
function's second instruction line can miss inside that short window; setup,
handlers and result stores lie outside it. Accordingly the cold checks require
at least one miss, warm checks require zero, and the A-return check requires
zero rather than asserting an exact global count of one. The same-WID control
expects S-first to hit, while the distinct-WID case expects S-first to miss.

Inspect `build/cva6/rv_wg_test.dump` to confirm the target fits one line and
that no active probe or trap line competes for its 256-set I-cache index. If a
counter assertion disagrees with the expected target behavior, corroborate
with accepted `l1_icache_miss_i` and AXI read-address (`ARADDR`, `ARUSER`)
traces filtered to the target's 16-byte-aligned physical line. Expect one
target-line refill tagged A, one tagged B, and none on return to A; setup can
cause additional refills at unrelated addresses.

The redirect test checks exact ECALL cause/EPC and landing markers for same-WID
and different-WID M-to-S `mret` / S-to-M trap sequences. It also changes the active
machine WID and restores it in assembly, verifying both following instructions
execute once. It never sets the `mwid` lock. Authorization faults, PTW, AMOs, and
store-buffer stress are not covered by this suite.

Build/disassembly checks do not replace running the suite on the modified RTL.
For unexpected counts, trace accepted read/refill requests for `probe_data` (visible
in the ELF symbol table) and their WIDs alongside `l1_dcache_miss_i`. The assertions
assume no extraneous data misses inside the assembly window.

