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


bool tlb_tests() {
    TEST_START();
    int i = 0;
    uint64_t * mem_pos = (uint64_t *)0x900002A0;

    csr_write(CSR_MLWID, 2);
    csr_write(CSR_MWIDDELEG, 0xF);

    goto_priv(PRIV_HS);
    TEST_SETUP_EXCEPT();
    csr_write(CSR_SLWID, 0);

    goto_priv(PRIV_M);

    // Test TLB flushs
    csr_write(CSR_MHPMEVENT3, 19); // Register tlb flushs
    csr_write(CSR_MLWID, 1); // Change mlwid

    goto_priv(PRIV_HS);
    TEST_SETUP_EXCEPT();
    csr_write(CSR_SLWID, 3); // Change slwid

    goto_priv(PRIV_M);
    csr_write(CSR_MHPMEVENT3, 0); // Stop counting

    uint64_t count = csr_read(CSR_HPMCOUNTER3);
	TEST_ASSERT("TLB flushs the correct ammount of times", count == 2);

    TEST_END(); 
}
