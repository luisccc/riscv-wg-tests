#include <rv_wg_test.h>
#include <page_tables.h> 

#define __ASM_STR(x)	#x

#define csr_read(csr)                                   \
	({                                                  \
		register unsigned long __v;                     \
		__asm__ __volatile__("csrr %0, " __ASM_STR(csr) \
				     : "=r"(__v)                \
				     :                          \
				     : "memory");               \
		__v;                                            \
	})

#define csr_write(csr, val)                                        \
	({                                                         \
		unsigned long __v = (unsigned long)(val);          \
		__asm__ __volatile__("csrw " __ASM_STR(csr) ", %0" \
				     :                             \
				     : "rK"(__v)                   \
				     : "memory");                  \
	})


bool icache_tests() {
    TEST_START();

	csr_write(CSR_MLWID, 1);
    csr_write(CSR_MHPMEVENT3, 1); // Register L1 i$ misses
	goto_priv(PRIV_HS);

    // Inline assembly for a loop
    asm volatile (
        "addi a0, zero, 0\n"
        "addi t0, zero, 0\n"
        "addi t1, zero, 1000\n"
		"addi t2, zero, 500\n"
        "block_start:\n"
        "bge t0, t1, block_end\n"
        "add a0, a0, 1\n"
        "addi t0, t0, 1\n"
		"beq t0, t2, perm_change\n"
		"j block_start\n"
    );

	asm volatile (
		"perm_change:\n"
	);
	goto_priv(PRIV_M);
	asm volatile (
		"j block_start\n"
		"block_end:\n"
	);

	csr_write(CSR_MHPMEVENT3, 0);
	uint64_t count = csr_read(CSR_HPMCOUNTER3);
	INFO("count: %ld", count);

    TEST_END(); 
}
