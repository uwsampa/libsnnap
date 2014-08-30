SOURCES = inversek2j.c ../snnap.c
OBJECTS = $(SOURCES:%.c=%.o)
EXECUTABLE = inversek2j.elf

LIBDIR := /sampa/share/thierry/benchmarks/BenchNN/lib
TOOLCHAIN := /sampa/share/Xilinx/14.6/14.6/ISE_DS/EDK/gnu/arm/lin
CC := $(TOOLCHAIN)/bin/arm-xilinx-eabi-gcc
CFLAGS += -I.. -I$(LIBDIR)/bsp/include -D_GNU_SOURCE=1 -std=c99
LDFLAGS := -Wl,-T -Wl,$(LIBDIR)/lscript.ld -L$(LIBDIR)/bsp/lib
LIBS := -Wl,--start-group,-lxil,-lgcc,-lc,-lm,--end-group

SETTINGS := /sampa/share/Xilinx/14.6/14.6/ISE_DS/settings64.sh
TCL_DIR := /sampa/share/thierry/benchmarks/boot_files/tcl
PS7_INIT := $(TCL_DIR)/ps7_init_111.tcl
RUN_TCL := $(TCL_DIR)/run_benchmark.tcl

ZYNQ := 2
ZYNQBIT := inversek2j_x8_x64.bit
MINICOM := /sampa/share/thierry/minicom$(ZYNQ).out

# Use the stream interface unless told otherwise.
ifneq ($(STREAM),0)
CFLAGS += -DNPU_STREAM
endif

.PHONY: all clean run

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLE)

run: $(EXECUTABLE)
	cat /dev/null > $(MINICOM)
	wemo switch zynq$(ZYNQ) off
	sleep 3s
	wemo switch zynq$(ZYNQ) on
	sleep 3s
	source $(SETTINGS) ; \
		xmd -tcl $(RUN_TCL) $(ZYNQBIT) $(PS7_INIT) $(EXECUTABLE)
	sleep 3
	cp $(MINICOM) log.txt
	cat log.txt
