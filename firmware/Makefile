# SkyRoads firmware for the Pocket SoC (RV32IMAC + Zba/Zbb/Zbs, bare metal).
#
#   make            ../build/skyroads_fw.bin (data slot 0) and the ELF
#   make host       ../build/fw_host: the same platform layer on this Mac,
#                   the SoC's registers simulated (host_hal.c)
#   make host-test  replay the attract demo with the host build
#   make host-check the same, checking every LZS decode against the shared one
#   make sim        ../build/skyroads_fw_sim.bin: for the Verilator
#                   testbench (SKY_DEMO_TEST on, debug output)
#
# Needs clang with the RISC-V target (Homebrew llvm) and ld.lld (Homebrew lld).
PORT  := ../decomp/port
BUILD := ../build
LLVM  ?= /opt/homebrew/opt/llvm@22/bin
LLD   ?= /opt/homebrew/opt/lld/bin/ld.lld
CC_RV := $(LLVM)/clang
OBJCOPY := $(LLVM)/llvm-objcopy

# the shared game code (see ../decomp/port/README.md)
GAME_SRC := runtime.c video.c manual.c render.c music.c game.c

ARCH   := --target=riscv32-unknown-elf -march=rv32imac_zba_zbb_zbs -mabi=ilp32
WARN   := -w -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-incompatible-function-pointer-types
COMMON := -std=gnu11 -DSKY_NO_GHOSTS -DSKY_FAST_LZS -I. -Ishim -I$(PORT) -fno-strict-aliasing -fwrapv $(WARN)
RV_CFLAGS := $(ARCH) -O2 -ffreestanding -nostdlib -ffunction-sections -fdata-sections \
             -isystem libc/include -mcmodel=medany -fno-pic $(COMMON)

FW_SRC := pocket.c libc/libc.c
RV_OBJS     := $(addprefix $(BUILD)/rv/,$(GAME_SRC:.c=.o) pocket.o lzs_fast.o savestate.o player.o libc.o crt0.o)
RV_SIM_OBJS := $(addprefix $(BUILD)/rv_sim/,$(GAME_SRC:.c=.o) pocket.o lzs_fast.o savestate.o player.o libc.o crt0.o)

all: $(BUILD)/skyroads_fw.bin
sim: $(BUILD)/skyroads_fw_sim.bin

$(PORT)/game.c $(PORT)/protos.h $(PORT)/image_data.h:
	$(MAKE) -C $(PORT) game.c image_data.h

HDRS := $(PORT)/compat.h $(PORT)/protos.h $(PORT)/image_data.h pocket_hw.h

$(BUILD)/rv/%.o: $(PORT)/%.c $(HDRS) | $(BUILD)/rv
	$(CC_RV) $(RV_CFLAGS) -c $< -o $@
$(BUILD)/rv/%.o: %.c $(HDRS) | $(BUILD)/rv
	$(CC_RV) $(RV_CFLAGS) -c $< -o $@
$(BUILD)/rv/libc.o: libc/libc.c | $(BUILD)/rv
	$(CC_RV) $(RV_CFLAGS) -fno-builtin -c $< -o $@
$(BUILD)/rv/crt0.o: crt0.S | $(BUILD)/rv
	$(CC_RV) $(ARCH) -c $< -o $@

# simulation build: getenv("SKY_DEMO_TEST") is set, so render.c ends the run
# when the demo road is finished
$(BUILD)/rv_sim/%.o: $(PORT)/%.c $(HDRS) | $(BUILD)/rv_sim
	$(CC_RV) $(RV_CFLAGS) -DPOCKET_SIM -c $< -o $@
$(BUILD)/rv_sim/%.o: %.c $(HDRS) | $(BUILD)/rv_sim
	$(CC_RV) $(RV_CFLAGS) -DPOCKET_SIM -c $< -o $@
$(BUILD)/rv_sim/libc.o: libc/libc.c | $(BUILD)/rv_sim
	$(CC_RV) $(RV_CFLAGS) -DPOCKET_SIM -fno-builtin -c $< -o $@
$(BUILD)/rv_sim/crt0.o: crt0.S | $(BUILD)/rv_sim
	$(CC_RV) $(ARCH) -c $< -o $@

$(BUILD)/rv $(BUILD)/rv_sim $(BUILD)/host:
	mkdir -p $@

# --wrap: pocket.c filters the main menu's key reads
LDFLAGS := -T link.ld --gc-sections --wrap=FUN_1000_5fad -Map

$(BUILD)/skyroads_fw.elf: $(RV_OBJS) link.ld
	$(LLD) $(LDFLAGS) $(BUILD)/skyroads_fw.map $(RV_OBJS) -o $@
$(BUILD)/skyroads_fw_sim.elf: $(RV_SIM_OBJS) link.ld
	$(LLD) $(LDFLAGS) $(BUILD)/skyroads_fw_sim.map $(RV_SIM_OBJS) -o $@
$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@
	@ls -l $@ | awk '{print $$5, "bytes:", $$9}'

# --- native test build -------------------------------------------------
HOST_OBJS := $(addprefix $(BUILD)/host/,$(GAME_SRC:.c=.o) pocket.o lzs_fast.o host_hal.o)
$(BUILD)/host/%.o: $(PORT)/%.c $(HDRS) | $(BUILD)/host
	cc -O1 -g -DPOCKET_HOST $(COMMON) -c $< -o $@
$(BUILD)/host/%.o: %.c $(HDRS) | $(BUILD)/host
	cc -O1 -g -DPOCKET_HOST $(COMMON) -c $< -o $@
$(BUILD)/fw_host: $(HOST_OBJS)
	cc $(HOST_OBJS) -o $@
host: $(BUILD)/fw_host

$(BUILD)/skyroads_data.bin:
	python3 ../tools/mkpack.py ../decomp/resources/Skyroads $@

host-test: $(BUILD)/fw_host $(BUILD)/skyroads_data.bin
	SKY_DEMO_TEST=1 POCKET_PACK=$(BUILD)/skyroads_data.bin $(BUILD)/fw_host

# every LZS decode run twice, lzs_fast.c against the shared decoder
$(BUILD)/fw_host_check: $(HOST_OBJS) lzs_fast.c
	cc -O1 -g -DPOCKET_HOST -DPOCKET_LZS_CHECK $(COMMON) -c lzs_fast.c -o $(BUILD)/host/lzs_check.o
	cc $(filter-out $(BUILD)/host/lzs_fast.o,$(HOST_OBJS)) $(BUILD)/host/lzs_check.o -o $@
host-check: $(BUILD)/fw_host_check $(BUILD)/skyroads_data.bin
	SKY_DEMO_TEST=1 POCKET_PACK=$(BUILD)/skyroads_data.bin $(BUILD)/fw_host_check

clean:
	rm -rf $(BUILD)/rv $(BUILD)/rv_sim $(BUILD)/host $(BUILD)/skyroads_fw* $(BUILD)/fw_host

.PHONY: all sim host host-test host-check clean
