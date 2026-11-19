#!/bin/sh
# Assemble the Quartus project in build/quartus from Analogue's core template
# (vendor/core-template/src/fpga) and this core's sources, then:
#   cd build/quartus && quartus_sh --flow compile ap_core
# (Quartus Prime Lite, Cyclone V; Linux or Windows).
set -e
cd "$(dirname "$0")/.."
T=vendor/core-template/src/fpga
OUT=build/quartus
[ -d "$T" ] || { echo "run ./fetch-deps.sh first"; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$T"/. "$OUT"/
rm -rf "$OUT/output_files" "$OUT/db" "$OUT/incremental_db"
cp fpga/core_top.v "$OUT/core/core_top.v"
cp fpga/mf_pllbase_0002.v "$OUT/core/mf_pllbase/mf_pllbase_0002.v"

QSF="$OUT/ap_core.qsf"
# the template's SignalTap setup refers to its own example signals
grep -v -e SIGNALTAP_FILE -e USE_SIGNALTAP_FILE -e SLD_FILE "$QSF" > "$QSF.new"
mv "$QSF.new" "$QSF"
{
    echo ""
    echo "# SkyRoads (added by fpga/mkproject.sh)"
    # one process: Quartus's parallel helpers crash under x86 emulation
    # (Docker on Apple silicon) and leave the synthesis waiting for them
    echo "set_global_assignment -name NUM_PARALLEL_PROCESSORS 1"
    # the system clock closes with little margin (the CPU's decode); the
    # default seed missed by 0.4 ns after an unrelated change, this one meets it
    echo "set_global_assignment -name SEED 2"
    # the template ships the DDIO clock pin's IP but does not use it
    echo "set_global_assignment -name QIP_FILE core/pin_ddio_clk.qip"
    echo "set_global_assignment -name SEARCH_PATH ../../vendor/Hazard3/hdl"
    echo "set_global_assignment -name SEARCH_PATH ../../vendor/jtopl/hdl"
    for f in rtl/*.v; do echo "set_global_assignment -name VERILOG_FILE ../../$f"; done
    sed -n 's/^file //p' vendor/Hazard3/hdl/hazard3.f | while read -r f; do
        echo "set_global_assignment -name VERILOG_FILE ../../vendor/Hazard3/hdl/$f"
    done
    for f in vendor/jtopl/hdl/*.v; do echo "set_global_assignment -name VERILOG_FILE ../../$f"; done
    # the SDRAM's registers in the I/O cells: at 49 MHz with the inverted
    # clock (core_top.v) the margins are about half a cycle
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_a[*]"
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_ba[*]"
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_dqm[*]"
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_ras_n"
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_cas_n"
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_we_n"
    echo "set_instance_assignment -name FAST_OUTPUT_REGISTER ON -to dram_dq[*]"
    echo "set_instance_assignment -name FAST_OUTPUT_ENABLE_REGISTER ON -to dram_dq[*]"
    echo "set_instance_assignment -name FAST_INPUT_REGISTER ON -to dram_dq[*]"
} >> "$QSF"
echo "Quartus project in $OUT: cd $OUT && quartus_sh --flow compile ap_core"
