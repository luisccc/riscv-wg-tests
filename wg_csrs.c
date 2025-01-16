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


bool wg_csrs() {
    TEST_START();
    
    csr_write(CSR_MLWID, 1);
    csr_write(CSR_MWIDDELEG, 7);
	
    goto_priv(PRIV_VS);
    TEST_SETUP_EXCEPT();
    csr_write(CSR_SLWID, 2);

    TEST_END(); 
}
