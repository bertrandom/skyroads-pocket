# SkyRoads for the Analogue Pocket (openFPGA).  See README.md.
#
#   ./fetch-deps.sh     the CPU, the OPL2 and Analogue's template, in vendor/
#   make                firmware and data pack (build/)
#   make host-test      the firmware's platform layer on this Mac: demo replay
#   make sim            Verilator: the SoC boots the game (frames in sim/frames)
#   make sim-demo       Verilator: replay the attract demo to its end (~30 min)
#   make sim-core       Verilator: the whole core over the APF bridge (~5 min)
#   make project        the Quartus project in build/quartus
#   make package        the SD card tree in build/package (with the bitstream
#                       once build/quartus has been compiled)
PKG  := build/package
CORE := $(PKG)/Cores/skyroads.SkyRoads
RBF  := build/quartus/output_files/ap_core.rbf

all: build/skyroads_fw.bin build/skyroads_data.bin

build/skyroads_fw.bin: FORCE
	$(MAKE) -C firmware
build/skyroads_data.bin: tools/mkpack.py
	python3 tools/mkpack.py decomp/resources/Skyroads $@

host-test:
	$(MAKE) -C firmware host-test

sim: build/skyroads_data.bin
	$(MAKE) -C firmware sim
	$(MAKE) -C sim run
sim-demo: build/skyroads_data.bin
	$(MAKE) -C firmware sim
	$(MAKE) -C sim demo

sim-core: build/skyroads_data.bin
	$(MAKE) -C firmware sim
	$(MAKE) -C sim core

project:
	sh fpga/mkproject.sh

package: all
	rm -rf $(PKG)
	mkdir -p $(CORE) $(PKG)/Assets/skyroads/common $(PKG)/Platforms/_images
	cp dist/Cores/skyroads.SkyRoads/* $(CORE)/
	cp dist/Platforms/skyroads.json $(PKG)/Platforms/
	python3 tools/mkplatimage.py decomp/menu-backgrounds/4x3/mainmenu.png $(PKG)/Platforms/_images/skyroads.bin
	cp build/skyroads_fw.bin build/skyroads_data.bin $(PKG)/Assets/skyroads/common/
	@if [ -f $(RBF) ]; then python3 tools/reverse_rbf.py $(RBF) $(CORE)/skyroads.rbf_r; \
	 else echo "no bitstream yet ($(RBF)): compile build/quartus first"; fi
	@echo "copy the contents of $(PKG) to the root of the Pocket's SD card"

.PHONY: all host-test sim sim-demo sim-core project package FORCE
