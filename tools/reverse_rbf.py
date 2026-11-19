#!/usr/bin/env python3
"""The Pocket loads bitstreams as .rbf_r: Quartus's .rbf with the bits of
every byte reversed.

    reverse_rbf.py ap_core.rbf skyroads.rbf_r"""
import sys

table = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))
data = open(sys.argv[1], "rb").read()
open(sys.argv[2], "wb").write(data.translate(table))
print(f"{sys.argv[2]}: {len(data)} bytes")
