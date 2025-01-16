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


bool dcache_tests() {
    TEST_START();
    int i = 0;
    uint64_t * mem_pos = (uint64_t *)0x900002A0;

    csr_write(CSR_MLWID, 1);

    // Cache the values
    for(; i < 50; i++) {
        mem_pos++;
        
        asm volatile(
            "ld x10, 0(%0)"   // Load the 64-bit value from the address in ptr into register x10
            :                 // No output operand
            : "r"(mem_pos)        // Input operand: 'ptr' is the address of the memory location
            : "x10"           // Clobber list: x10 is modified by the instruction
        );
    }

    i = 0;
    mem_pos = (uint64_t *)0x900002A0;
    csr_write(CSR_MHPMEVENT3, 2); // Register L1 d$ misses
    goto_priv(PRIV_HS);

	for(; i < 50; i++) {
        mem_pos++;
        
        asm volatile(
            "ld x10, 0(%0)"   // Load the 64-bit value from the address in ptr into register x10
            :                 // No output operand
            : "r"(mem_pos)        // Input operand: 'ptr' is the address of the memory location
            : "x10"           // Clobber list: x10 is modified by the instruction
        );
    }
    
    goto_priv(PRIV_M);
    csr_write(CSR_MHPMEVENT3, 0); // Register L1 d$ misses

    uint64_t count = csr_read(CSR_HPMCOUNTER3);
	INFO("count: %ld", count);

    TEST_END(); 
}
