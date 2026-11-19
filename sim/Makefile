# Verilator simulation of the SoC running the firmware.
#
#   make            build obj/Vsky_soc
#   make run        boot to the title screen, frames in frames/
#   make demo       replay the attract demo to its end (long: tens of minutes)
#   make core       the whole core (core_top.v, the APF bridge): the slots
#                   loaded over the bridge, the game run, the save read back
#   make sleep      the whole core: sleep (a save state read out) and wake,
#                   loaded before and after the CPU starts
#   make songs      the game's songs through rtl/audio.v, as WAV files in
#                   songs/ (the writes, from port/tools/render_songs.c, in
#                   songs/writes/)
#   make ymfm       the same songs through ymfm's YM3812 (a reference), as
#                   WAV files in songs/ymfm/
#   make audio      rtl/audio.v alone: an OPL2 tone at 48 kHz, raw against
#                   resampled (spurious energy in dB, spurs.py)
VENDOR := ../vendor
H := $(VENDOR)/Hazard3/hdl
J := $(VENDOR)/jtopl/hdl
HAZARD3 := $(addprefix $(H)/,$(shell sed -n 's/^file //p' $(H)/hazard3.f))
RTL := $(wildcard ../rtl/*.v) $(HAZARD3) $(wildcard $(J)/*.v)

VFLAGS := --cc --exe --build -O3 --x-assign fast --x-initial fast --noassert \
          -Wno-fatal -Wno-lint -Wno-style -Wno-MULTIDRIVEN -Wno-SYNCASYNCNET \
          -I$(H) -I$(J) -CFLAGS -O2 -j 8

OBJ ?= obj

$(OBJ)/Vsky_soc: $(RTL) tb.cpp
	verilator $(VFLAGS) $(VEXTRA) --top-module sky_soc --Mdir $(OBJ) $(RTL) tb.cpp -o Vsky_soc

../build/skyroads_fw_sim.bin:
	$(MAKE) -C ../firmware sim
../build/skyroads_data.bin:
	$(MAKE) -C ../firmware ../build/skyroads_data.bin

run: $(OBJ)/Vsky_soc ../build/skyroads_fw_sim.bin ../build/skyroads_data.bin
	mkdir -p frames
	$(OBJ)/Vsky_soc --frames frames --every 15 --ms 4000

demo: $(OBJ)/Vsky_soc ../build/skyroads_fw_sim.bin ../build/skyroads_data.bin
	mkdir -p frames
	$(OBJ)/Vsky_soc --frames frames --every 70 --wav frames/audio.raw

# the whole core, with stand-ins for the Altera PLL/DDIO/RAM primitives
T := $(VENDOR)/core-template/src/fpga
CORE_RTL := ../fpga/core_top.v $(T)/core/core_bridge_cmd.v $(T)/apf/common.v lint/altera_stubs.v $(RTL)
obj_core/Vcore_top: $(CORE_RTL) tb_core.cpp sdram_model.h
	verilator $(VFLAGS) --top-module core_top --pins-inout-enables --public-flat-rw \
		--Mdir obj_core $(CORE_RTL) tb_core.cpp -o Vcore_top

core: obj_core/Vcore_top ../build/skyroads_fw_sim.bin ../build/skyroads_data.bin
	obj_core/Vcore_top --ms 1500
# sleep and wake: a save state read out, the core powered off and woken
sleep: obj_core/Vcore_top ../build/skyroads_fw_sim.bin ../build/skyroads_data.bin
	obj_core/Vcore_top --sleep
	obj_core/Vcore_top --sleep --wake-late

$(OBJ)/Vsky_soc: sdram_model.h

.PHONY: run demo core

$(OBJ)_audio/Vaudio: ../rtl/audio.v ../rtl/sync_fifo.v $(wildcard $(J)/*.v) tb_audio.cpp
	verilator $(VFLAGS) --top-module audio --public-flat-rw -GSYS_HZ=49152000 --Mdir $(OBJ)_audio \
	    ../rtl/audio.v ../rtl/sync_fifo.v $(wildcard $(J)/*.v) tb_audio.cpp -o Vaudio
audio: $(OBJ)_audio/Vaudio
	$(OBJ)_audio/Vaudio $(OBJ)_audio/raw.s16 $(OBJ)_audio/resampled.s16
	python3 spurs.py $(OBJ)_audio/raw.s16 $(OBJ)_audio/resampled.s16
.PHONY: audio sleep

# the songs through the Pocket's audio hardware (tb_songs.cpp), its system
# clock at the OPL2's rate
$(OBJ)_songs/Vaudio: ../rtl/audio.v ../rtl/sync_fifo.v $(wildcard $(J)/*.v) tb_songs.cpp
	verilator $(VFLAGS) --top-module audio --public-flat-rw -GSYS_HZ=3579545 --Mdir $(OBJ)_songs \
	    ../rtl/audio.v ../rtl/sync_fifo.v $(wildcard $(J)/*.v) tb_songs.cpp -o Vaudio
songs: $(OBJ)_songs/Vaudio
	mkdir -p songs/writes
	$(MAKE) -C ../decomp/port render_songs
	../decomp/port/render_songs ../decomp/resources/Skyroads/MUZAX.LZS songs/writes --writes songs/writes > /dev/null
	for f in songs/writes/*.opl; do n=$$(basename "$$f" .opl); $(OBJ)_songs/Vaudio "$$f" "songs/$$n.wav" & done; wait
.PHONY: songs

# the same songs through ymfm's YM3812, the reference (tb_ymfm.cpp)
Y := $(VENDOR)/ymfm/src
$(OBJ)_ymfm/ymfm_songs: tb_ymfm.cpp $(Y)/ymfm_opl.cpp $(Y)/ymfm_adpcm.cpp $(Y)/ymfm_pcm.cpp
	mkdir -p $(OBJ)_ymfm
	c++ -std=c++14 -O2 -I$(Y) -o $@ $^
ymfm: $(OBJ)_ymfm/ymfm_songs
	test -d songs/writes || $(MAKE) songs
	mkdir -p songs/ymfm
	for f in songs/writes/*.opl; do n=$$(basename "$$f" .opl); $(OBJ)_ymfm/ymfm_songs "$$f" "songs/ymfm/$$n.wav"; done
.PHONY: ymfm
