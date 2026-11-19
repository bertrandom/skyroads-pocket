#!/usr/bin/env python3
"""Where the CPU spent its time: the "prof <addr> <count>" lines the
simulation firmware prints at its end (pocket.c, sampled by the timer
interrupt), summed per function of the ELF.

    profile.py prof.log [../build/skyroads_fw_sim.elf] [top]"""
import bisect, subprocess, sys

log = sys.argv[1]
elf = sys.argv[2] if len(sys.argv) > 2 else "../build/skyroads_fw_sim.elf"
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
nm = "/opt/homebrew/opt/llvm@22/bin/llvm-nm"

syms = []
for line in subprocess.run([nm, "-n", "--defined-only", elf], capture_output=True, text=True).stdout.splitlines():
    parts = line.split()
    if len(parts) == 3 and parts[1] in "tTwW":
        syms.append((int(parts[0], 16), parts[2]))
syms.sort()
addrs = [a for a, _ in syms]

per, total = {}, 0
for line in open(log, errors="replace"):
    if not line.startswith("prof "):
        continue
    _, a, n = line.split()
    a, n = int(a, 16), int(n)
    i = bisect.bisect_right(addrs, a) - 1
    name = syms[i][1] if i >= 0 else "?"
    per[name] = per.get(name, 0) + n
    total += n
if not total:
    sys.exit("no profile samples in " + log)
print(f"{total} samples ({total / 180:.0f} s at 180 Hz)")
for name, n in sorted(per.items(), key=lambda kv: -kv[1])[:top]:
    print(f"{100 * n / total:6.2f}%  {n:7d}  {name}")
