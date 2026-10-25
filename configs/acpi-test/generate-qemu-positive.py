#!/usr/bin/env python3

import pathlib
import struct
import sys


def encode_package_length(value):
	for encoded_length, maximum in ((1, 0x3F), (2, 0xFFF), (3, 0xFFFFF), (4, 0xFFFFFFF)):
		if value > maximum:
			continue
		if encoded_length == 1:
			return bytes((value,))
		encoded = bytearray((((encoded_length - 1) << 6) | (value & 0x0F),))
		remaining = value >> 4
		for _ in range(encoded_length - 1):
			encoded.append(remaining & 0xFF)
			remaining >>= 8
		return bytes(encoded)
	raise ValueError("AML package is too large")


def package(payload):
	for encoded_length in range(1, 5):
		encoded = encode_package_length(len(payload) + encoded_length)
		if len(encoded) == encoded_length:
			return encoded + payload
	raise ValueError("AML package is too large")


def name_segment(segment):
	if not segment or len(segment) > 4:
		raise ValueError("invalid AML name segment")
	return segment.ljust(4, "_").encode("ascii")


def name_string(path):
	rooted = path.startswith("\\")
	if rooted:
		path = path[1:]
	segments = path.split(".") if path else []
	encoded = bytearray((0x5C,)) if rooted else bytearray()
	if len(segments) == 2:
		encoded.append(0x2E)
	elif len(segments) > 2:
		encoded.extend((0x2F, len(segments)))
	for segment in segments:
		encoded.extend(name_segment(segment))
	return bytes(encoded)


def integer(value):
	if value == 0:
		return b"\x00"
	if value == 1:
		return b"\x01"
	if value <= 0xFF:
		return b"\x0A" + struct.pack("<B", value)
	if value <= 0xFFFF:
		return b"\x0B" + struct.pack("<H", value)
	if value <= 0xFFFFFFFF:
		return b"\x0C" + struct.pack("<I", value)
	return b"\x0E" + struct.pack("<Q", value)


def string(value):
	return b"\x0D" + value.encode("ascii") + b"\x00"


def name(name, value):
	return b"\x08" + name_string(name) + value


def scope(path, *terms):
	payload = name_string(path) + b"".join(terms)
	return b"\x10" + package(payload)


def device(name, *terms):
	payload = name_string(name) + b"".join(terms)
	return b"\x5B\x82" + package(payload)


def method(name, flags, *terms):
	payload = name_string(name) + bytes((flags,)) + b"".join(terms)
	return b"\x14" + package(payload)


def data_package(*elements):
	payload = bytes((len(elements),)) + b"".join(elements)
	return b"\x12" + package(payload)


def if_block(predicate, *terms):
	return b"\xA0" + package(predicate + b"".join(terms))


def operation_region(name, space, offset, length):
	return b"\x5B\x80" + name_string(name) + bytes((space,)) + integer(offset) + integer(length)


def field(region, flags, field_name, bit_length):
	payload = name_string(region) + bytes((flags,)) + name_segment(field_name) + encode_package_length(bit_length)
	return b"\x5B\x81" + package(payload)


def buffer(data):
	return b"\x11" + package(integer(len(data)) + data)


def store(source, target_opcode):
	return b"\x70" + name_string(source) + bytes((target_opcode,))


def return_value(value):
	return b"\xA4" + value


def concatenate_resource_templates(left, right):
	return b"\x84" + name_string(left) + name_string(right) + b"\x00"


def large_resource(tag, body):
	return bytes((tag,)) + struct.pack("<H", len(body)) + body


def serial_resource(bus_type, type_flags, type_data, source):
	body = bytes((2, 0, bus_type, 2))
	body += struct.pack("<H", type_flags)
	body += bytes((1,)) + struct.pack("<H", len(type_data))
	body += type_data + source.encode("ascii") + b"\x00"
	return large_resource(0x8E, body)


def i2c_resource(address, speed, source):
	return serial_resource(1, 0, struct.pack("<IH", speed, address), source)


def spi_resource(chip_select, speed, source):
	return serial_resource(2, 0, struct.pack("<IBBBH", speed, 8, 0, 0, chip_select), source)


def uart_resource(baud, source):
	return serial_resource(3, 0x13, struct.pack("<IHHBB", baud, 0x40, 0x40, 0, 0xC0), source)


def gpio_interrupt(pin, source):
	pin_offset = 23
	source_offset = pin_offset + 2
	body = bytes((1, 0))
	body += struct.pack("<HHBHH", 2, 2, 0, 0, 0)
	body += struct.pack("<H", pin_offset) + bytes((0,))
	body += struct.pack("<HHH", source_offset, 0, 0)
	body += struct.pack("<H", pin) + source.encode("ascii") + b"\x00"
	return large_resource(0x8C, body)


def resource_template(*resources):
	return b"".join(resources) + b"\x79\x00"


def build_aml():
	gpio = device("MGPO", name("_HID", string("MCOA0005")), name("_STA", integer(0x0F)))

	touchpad_bus_resources = resource_template(i2c_resource(0x2C, 400000, "\\_SB.PCI0.MI2C"))
	touchpad_gpio_resources = resource_template(gpio_interrupt(0x14, "\\_SB.MGPO"))
	touchpad = device(
		"MTPD",
		name("_HID", string("ALPS0001")),
		name("_CID", string("PNP0C50")),
		name("_UID", integer(1)),
		name("SBFB", buffer(touchpad_bus_resources)),
		name("SBFG", buffer(touchpad_gpio_resources)),
		method("_STA", 0, return_value(integer(0x0F))),
		method("_CRS", 0, return_value(concatenate_resource_templates("SBFB", "SBFG"))),
	)
	absent = device(
		"MOFF",
		name("_HID", string("MCOA00FF")),
		name("_STA", integer(0)),
		name("_CRS", buffer(resource_template(i2c_resource(0x3E, 100000, "\\_SB.PCI0.MI2C")))),
	)
	i2c = device(
		"MI2C",
		name("_HID", string("MCOA0001")),
		name("_ADR", integer(0)),
		operation_region("PCFG", 2, 0, 4),
		field("PCFG", 3, "PCID", 32),
		method("_STA", 0x08, store("PCID", 0x60), return_value(integer(0x0F))),
	)

	spi_endpoint = device(
		"MSPD",
		name("_HID", string("MCOA0006")),
		name("_STA", integer(0x0F)),
		name("_CRS", buffer(resource_template(spi_resource(1, 10000000, "\\_SB.PCI0.MSPI")))),
	)
	spi = device(
		"MSPI",
		name("_HID", string("MCOA0003")),
		operation_region("SMEM", 0, 0x400, 1),
		field("SMEM", 1, "MDBY", 8),
		method("_STA", 0, store("MDBY", 0x60), return_value(integer(0x0F))),
		spi_endpoint,
	)

	uart_endpoint = device(
		"MURT",
		name("_HID", string("MCOA0007")),
		name("_STA", integer(0x0F)),
		method(
			"_CRS",
			0,
			name("RBUF", buffer(resource_template(uart_resource(115200, "\\_SB.PCI0.MUAR")))),
			return_value(name_string("RBUF")),
		),
	)
	uart = device(
		"MUAR",
		name("_HID", string("MCOA0004")),
		name("_STA", integer(0x0F)),
		uart_endpoint,
	)
	conditional_dependency = if_block(
		integer(1),
		scope("\\_SB_.PCI0.MI2C", name("_DEP", data_package(name_string("\\_SB_.MGPO")))),
	)

	return scope("\\_SB_", gpio) + scope("\\_SB_.PCI0", i2c, spi, uart) + scope("\\_SB_.PCI0.MI2C", touchpad, absent) + conditional_dependency


def build_table():
	aml = build_aml()
	header = struct.pack(
		"<4sIBB6s8sI4sI",
		b"SSDT",
		36 + len(aml),
		2,
		0,
		b"MECOCO",
		b"ACPITEST",
		1,
		b"MCCA",
		1,
	)
	table = bytearray(header + aml)
	table[9] = (-sum(table)) & 0xFF
	return bytes(table)


def main():
	if len(sys.argv) != 2:
		raise SystemExit("usage: generate-qemu-positive.py OUTPUT.aml")
	output = pathlib.Path(sys.argv[1])
	output.parent.mkdir(parents=True, exist_ok=True)
	table = build_table()
	if struct.unpack_from("<I", table, 4)[0] != len(table) or sum(table) & 0xFF:
		raise RuntimeError("generated ACPI table failed validation")
	output.write_bytes(table)


if __name__ == "__main__":
	main()
