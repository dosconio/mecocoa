#!/usr/bin/env python3

import pathlib
import struct
import sys


def main():
	if len(sys.argv) != 3:
		raise SystemExit("usage: prepare-firmware-dsdt.py INPUT_DSDT OUTPUT_SSDT")
	source = pathlib.Path(sys.argv[1])
	destination = pathlib.Path(sys.argv[2])
	table = bytearray(source.read_bytes())
	if len(table) < 36 or table[:4] != b"DSDT":
		raise SystemExit(f"{source}: expected a complete DSDT table")
	table_length = struct.unpack_from("<I", table, 4)[0]
	if table_length != len(table):
		raise SystemExit(f"{source}: header length {table_length} does not match file length {len(table)}")
	if sum(table) & 0xFF:
		raise SystemExit(f"{source}: invalid ACPI checksum")
	table[:4] = b"SSDT"
	table[9] = 0
	table[9] = (-sum(table)) & 0xFF
	destination.parent.mkdir(parents=True, exist_ok=True)
	destination.write_bytes(table)


if __name__ == "__main__":
	main()
