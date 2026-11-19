#!/bin/sh
# Fetch the third-party hardware this core is built from, at the commits it
# was written against, into vendor/ (not committed):
#   Hazard3        RISC-V CPU, Luke Wren, Apache-2.0
#   jtopl          YM3812 (OPL2), Jose Tejada, GPL-3.0
#   core-template  Analogue's openFPGA core template (APF top level, bridge
#                  command handler, Quartus project)
#   ymfm           Yamaha FM emulation, Aaron Giles, BSD-3-Clause: in the
#                  simulation only, a reference YM3812 for sim/ make ymfm
set -e
cd "$(dirname "$0")"
mkdir -p vendor
fetch() {   # name url commit
    if [ -d "vendor/$1/.git" ]; then
        git -C "vendor/$1" fetch -q --depth 1 origin "$3"
    else
        git clone -q --depth 1 "$2" "vendor/$1"
        git -C "vendor/$1" fetch -q --depth 1 origin "$3"
    fi
    git -C "vendor/$1" -c advice.detachedHead=false checkout -q "$3"
    echo "vendor/$1 at $3"
}
fetch Hazard3       https://github.com/Wren6991/Hazard3.git        8af992930f71a69b0e06c38734c1094f41a05ca0
fetch jtopl         https://github.com/jotego/jtopl.git            ff18c4718487fa35889d870b8e3bbaa7dc2ff46f
fetch core-template https://github.com/open-fpga/core-template.git da3a021b1eaf742604d86d8dc9b33a6666263e6a
fetch ymfm          https://github.com/aaronsgiles/ymfm.git        81aec25ccbb98f4873a255f7551ac4dadac59b4a
