# Run tests on spike emulator
# 
# Assumes spike is in PATH

# Build tests
make clean
export PLAT=spike LOG_LEVEL=LOG_WARNING
make

# Run tests
spike -l --log-commits --log=log/spike_trace.log --isa=RV64gch build/spike/rv_wg_test.elf &> log/spike_term.log