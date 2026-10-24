// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] ACPI Device Discovery
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600

#include <cpp/Device/ACPI.hpp>
#include <cpp/Device/ACPI/AML.hpp>
#include <cpp/Device/Bus/PCI.hpp>

namespace Devs {

	namespace {

		struct AmlTableView {
			const uint8* aml;
			stduint length;
			char signature[5];
		};

		constexpr stduint AcpiDeviceCapacity = 512;
		constexpr stduint AcpiSerialResourceCapacity = 512;
		constexpr stduint AmlTableCapacity = 64;
		constexpr stduint AcpiPathCapacity = 160;
		constexpr stduint AcpiIdCapacity = 48;
		constexpr stduint AcpiUidCapacity = 32;
		constexpr stduint AcpiNameSegmentCapacity = 16;
		constexpr stduint AcpiMalformedRecordCapacity = 16;
		constexpr stduint AmlNamespaceObjectCapacity = 8192;
		constexpr stduint AmlExecutionFrameCapacity = 16;
		constexpr stduint AmlObjectValueCapacity = 512;
		constexpr stduint AmlEvaluationSummaryCapacity = 32;
		constexpr stduint AmlRegionSummaryCapacity = 16;
		constexpr bool LogAllAcpiDevices = false;

		enum class AmlPciReadStatus : uint8 {
			Success,
			OwnerUnresolved,
			InvalidAccess,
			ConfigUnavailable,
			Count,
		};

		struct AmlPciLocation {
			uint16 segment;
			uint8 bus;
			uint8 device;
			uint8 function;
		};

		enum class AcpiMalformedKind : uint8 {
			SerialHeader,
			SerialLength,
			ResourceHeader,
			ResourceLength,
			Path,
			Namespace,
			Count,
		};

		enum class AcpiSerialBusType : uint8 {
			I2C = 1,
			SPI = 2,
			UART = 3,
			CSI2 = 4,
		};

		struct AcpiNamePath {
			char segments[AcpiNameSegmentCapacity][5];
			uint8 segment_count;
			uint8 parent_count;
			bool rooted;
		};

		struct AcpiDeviceRecord {
			char path[AcpiPathCapacity];
			char hid[AcpiIdCapacity];
			char cid[AcpiIdCapacity];
			char uid[AcpiUidCapacity];
			const uint8* sta_expression;
			stduint sta_expression_length;
			uint64 sta;
			uint64 adr;
			uint8 sta_first_opcode;
			uint8 sta_value_opcode;
			bool has_hid;
			bool has_cid;
			bool has_uid;
			bool has_sta;
			bool has_adr;
			bool sta_dynamic;
			bool sta_method;
			bool has_sta_pattern;
			bool crs_dynamic;
			bool conditional;
		};

		struct AcpiSerialResourceRecord {
			char controller[AcpiPathCapacity];
			uint32 speed;
			uint16 address;
			uint16 receive_fifo;
			uint16 transmit_fifo;
			uint16 type_flags;
			uint16 device_index;
			uint8 bus_type;
			uint8 general_flags;
			uint8 data_bits;
			uint8 clock_phase;
			uint8 clock_polarity;
			uint8 parity;
			uint8 lines_enabled;
		};

		struct AcpiMalformedRecord {
			const uint8* address;
			AcpiMalformedKind kind;
		};

		struct AmlEvaluationSummary {
			char example[AcpiPathCapacity];
			stduint count;
			uint16 opcode;
			uni::AMLExecutor::Status status;
		};

		struct AmlRegionSummary {
			char example[AcpiPathCapacity];
			stduint count;
			uint16 space;
			uint16 field_opcode;
			uint8 field_flags;
		};

		AcpiDeviceRecord acpi_devices[AcpiDeviceCapacity] = {};
		AcpiSerialResourceRecord acpi_serial_resources[AcpiSerialResourceCapacity] = {};
		stduint acpi_device_count = 0;
		stduint acpi_serial_resource_count = 0;
		stduint acpi_conditional_block_count = 0;
		stduint acpi_parent_absent_count = 0;
		stduint acpi_parent_unknown_count = 0;
		stduint acpi_pci_absent_count = 0;
		stduint acpi_malformed_count = 0;
		stduint acpi_malformed_kind_counts[stduint(AcpiMalformedKind::Count)] = {};
		AcpiMalformedRecord acpi_malformed_records[AcpiMalformedRecordCapacity] = {};
		stduint acpi_malformed_record_count = 0;
		bool acpi_device_truncated = false;
		bool acpi_serial_resource_truncated = false;
		AmlTableView aml_tables[AmlTableCapacity] = {};
		stduint aml_table_count = 0;
		uni::AMLNamespace::Object aml_namespace_objects[AmlNamespaceObjectCapacity] = {};
		uni::AMLExecutor::Frame aml_execution_frames[AmlExecutionFrameCapacity] = {};
		uni::AMLExecutor::ObjectValue aml_object_values[AmlObjectValueCapacity] = {};
		AmlEvaluationSummary aml_evaluation_summaries[AmlEvaluationSummaryCapacity] = {};
		stduint aml_evaluation_summary_count = 0;
		AmlRegionSummary aml_region_summaries[AmlRegionSummaryCapacity] = {};
		stduint aml_region_summary_count = 0;
		stduint aml_pci_read_counts[stduint(AmlPciReadStatus::Count)] = {};
		char aml_pci_read_examples[stduint(AmlPciReadStatus::Count)][AcpiPathCapacity] = {};

		bool decode_aml_package_length(const uint8* aml, stduint available, uint32& package_length, uint8& encoded_length) {
			if (!aml || !available) return false;
			const uint8 following = aml[0] >> 6;
			encoded_length = following + 1;
			if (encoded_length > available) return false;
			package_length = following ? aml[0] & 0x0Fu : aml[0] & 0x3Fu;
			for (uint8 index = 0; index < following; ++index) {
				package_length |= uint32(aml[index + 1]) << (4 + index * 8);
			}
			return package_length >= encoded_length;
		}

		bool decode_aml_integer(const uint8*& cursor, const uint8* end, uint64& value) {
			if (!cursor || cursor >= end) return false;
			switch (*cursor++) {
			case 0x00:
				value = 0;
				return true;
			case 0x01:
				value = 1;
				return true;
			case 0xFF:
				value = ~uint64(0);
				return true;
			case 0x0A:
				if (end - cursor < 1) return false;
				value = *cursor++;
				return true;
			case 0x0B:
				if (end - cursor < 2) return false;
				value = uint64(cursor[0]) | uint64(cursor[1]) << 8;
				cursor += 2;
				return true;
			case 0x0C:
				if (end - cursor < 4) return false;
				value = uint64(cursor[0]) | uint64(cursor[1]) << 8 |
					uint64(cursor[2]) << 16 | uint64(cursor[3]) << 24;
				cursor += 4;
				return true;
			case 0x0E:
				if (end - cursor < 8) return false;
				value = 0;
				for0(index, 8) value |= uint64(cursor[index]) << (index * 8);
				cursor += 8;
				return true;
			default:
				return false;
			}
		}

		bool decode_aml_buffer(const uint8* buffer_opcode, const uint8* end, const uint8*& data, stduint& data_length) {
			if (!buffer_opcode || buffer_opcode >= end || *buffer_opcode != 0x11)
				return false;
			uint32 package_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(buffer_opcode + 1,
				end - buffer_opcode - 1, package_length, encoded_length) ||
				package_length > stduint(end - buffer_opcode - 1)) return false;
			const uint8* buffer_end = buffer_opcode + 1 + package_length;
			const uint8* cursor = buffer_opcode + 1 + encoded_length;
			uint64 declared_length = 0;
			if (!decode_aml_integer(cursor, buffer_end, declared_length) ||
				declared_length > stduint(buffer_end - cursor)) return false;
			data = cursor;
			data_length = stduint(declared_length);
			return true;
		}

		const uni::ACPI::DescriptionHeader* acpi_table_at(uint64 address) {
			if (!address || address > uint64(~stduint(0))) return nullptr;
			return reinterpret_cast<const uni::ACPI::DescriptionHeader*>(stduint(address));
		}

		const uni::ACPI::DescriptionHeader* find_x86_acpi_table(const char* signature) {
			if (!acpi_rsdp_addr) return nullptr;
			const auto* rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(acpi_rsdp_addr);
			if (!rsdp->isValid()) return nullptr;
			if (rsdp->revision >= 2 && rsdp->xsdt_address &&
				rsdp->xsdt_address <= uint64(~stduint(0))) {
				const auto* xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(
					stduint(rsdp->xsdt_address));
				if (xsdt->header.length >= sizeof(uni::ACPI::DescriptionHeader) &&
					xsdt->header.isValid("XSDT")) {
					const stduint count = (xsdt->header.length - sizeof(xsdt->header)) /
						sizeof(uint64);
					const uint8* entries = reinterpret_cast<const uint8*>(xsdt) +
						sizeof(xsdt->header);
					for0(index, count) {
						uint64 address = 0;
						MemCopyN(&address, entries + index * sizeof(address), sizeof(address));
						const auto* table = acpi_table_at(address);
						if (table && table->length >= sizeof(*table) &&
							table->isValid(signature)) return table;
					}
				}
			}
			if (!rsdp->rsdt_address) return nullptr;
			const auto* rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(
				stduint(rsdp->rsdt_address));
			if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader) ||
				!rsdt->header.isValid("RSDT")) return nullptr;
			const stduint count = (rsdt->header.length - sizeof(rsdt->header)) /
				sizeof(uint32);
			const uint8* entries = reinterpret_cast<const uint8*>(rsdt) +
				sizeof(rsdt->header);
			for0(index, count) {
				uint32 address = 0;
				MemCopyN(&address, entries + index * sizeof(address), sizeof(address));
				const auto* table = acpi_table_at(address);
				if (table && table->length >= sizeof(*table) &&
					table->isValid(signature)) return table;
			}
			return nullptr;
		}

		bool append_x86_aml_table(const uni::ACPI::DescriptionHeader* table, const char* signature) {
			constexpr uint32 MaximumAmlTableLength = 2u * 1024u * 1024u;
			if (!table || table->length < sizeof(*table) ||
				table->length > MaximumAmlTableLength || !table->isValid(signature)) return false;
			const auto* aml = reinterpret_cast<const uint8*>(table + 1);
			for0(index, aml_table_count) {
				if (aml_tables[index].aml == aml) return true;
			}
			if (aml_table_count >= AmlTableCapacity) return false;
			auto& view = aml_tables[aml_table_count++];
			view = {};
			view.aml = aml;
			view.length = table->length - sizeof(*table);
			for0(index, 4) view.signature[index] = signature[index];
			return true;
		}

		void append_x86_ssdt_tables() {
			if (!acpi_rsdp_addr) return;
			const auto* rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(acpi_rsdp_addr);
			if (!rsdp->isValid()) return;
			if (rsdp->revision >= 2 && rsdp->xsdt_address &&
				rsdp->xsdt_address <= uint64(~stduint(0))) {
				const auto* xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(
					stduint(rsdp->xsdt_address));
				if (xsdt->header.length >= sizeof(uni::ACPI::DescriptionHeader) &&
					xsdt->header.isValid("XSDT")) {
					const stduint count = (xsdt->header.length - sizeof(xsdt->header)) /
						sizeof(uint64);
					const uint8* entries = reinterpret_cast<const uint8*>(xsdt) +
						sizeof(xsdt->header);
					for0(index, count) {
						uint64 address = 0;
						MemCopyN(&address, entries + index * sizeof(address), sizeof(address));
						append_x86_aml_table(acpi_table_at(address), "SSDT");
					}
				}
			}
			if (!rsdp->rsdt_address) return;
			const auto* rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(
				stduint(rsdp->rsdt_address));
			if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader) ||
				!rsdt->header.isValid("RSDT")) return;
			const stduint count = (rsdt->header.length - sizeof(rsdt->header)) /
				sizeof(uint32);
			const uint8* entries = reinterpret_cast<const uint8*>(rsdt) +
				sizeof(rsdt->header);
			for0(index, count) {
				uint32 address = 0;
				MemCopyN(&address, entries + index * sizeof(address), sizeof(address));
				append_x86_aml_table(acpi_table_at(address), "SSDT");
			}
		}

		void collect_x86_aml_tables() {
			aml_table_count = 0;
			const auto* fadt_header = find_x86_acpi_table("FACP");
			if (fadt_header && fadt_header->length >=
				offsetof(uni::ACPI::FADT, dsdt) + sizeof(uint32)) {
				const auto* fadt = reinterpret_cast<const uni::ACPI::FADT*>(fadt_header);
				uint64 dsdt_address = fadt->dsdt;
				if (fadt->header.length >= offsetof(uni::ACPI::FADT, x_dsdt) +
					sizeof(fadt->x_dsdt) && fadt->x_dsdt &&
					fadt->x_dsdt <= uint64(~stduint(0))) {
					dsdt_address = fadt->x_dsdt;
				}
				append_x86_aml_table(acpi_table_at(dsdt_address), "DSDT");
			}
			append_x86_ssdt_tables();
		}

		bool is_aml_name_lead(uint8 value) {
			return value == '_' || (value >= 'A' && value <= 'Z');
		}

		bool is_aml_name_char(uint8 value) {
			return is_aml_name_lead(value) || (value >= '0' && value <= '9');
		}

		bool equal_text(const char* left, const char* right) {
			if (!left || !right) return false;
			while (*left && *right && *left == *right) {
				++left;
				++right;
			}
			return *left == *right;
		}

		stduint text_length(const char* text) {
			stduint length = 0;
			if (!text) return 0;
			while (text[length]) ++length;
			return length;
		}

		void copy_text(char* destination, stduint capacity, const char* source) {
			if (!destination || !capacity) return;
			stduint index = 0;
			if (source) {
				while (source[index] && index + 1 < capacity) {
					destination[index] = source[index];
					++index;
				}
			}
			destination[index] = 0;
		}

		void copy_text_range(char* destination, stduint capacity, const uint8* source, const uint8* end) {
			if (!destination || !capacity) return;
			stduint index = 0;
			while (source && source < end && *source && index + 1 < capacity) {
				destination[index++] = char(*source++);
			}
			destination[index] = 0;
		}

		bool append_text(char* destination, stduint capacity, const char* source) {
			if (!destination || !capacity || !source) return false;
			stduint used = text_length(destination);
			while (*source && used + 1 < capacity) destination[used++] = *source++;
			destination[used] = 0;
			return !*source;
		}

		void append_hex(char* destination, stduint capacity, uint64 value) {
			constexpr char HexDigits[] = "0123456789ABCDEF";
			append_text(destination, capacity, "0x");
			bool started = false;
			for (int shift = 60; shift >= 0; shift -= 4) {
				const uint8 digit = uint8((value >> shift) & 0xFu);
				if (!started && !digit && shift) continue;
				started = true;
				char text[2] = { HexDigits[digit], 0 };
				append_text(destination, capacity, text);
			}
		}

		void append_hex_byte(char* destination, stduint capacity, uint8 value) {
			constexpr char HexDigits[] = "0123456789ABCDEF";
			char text[3] = {
				HexDigits[(value >> 4) & 0x0Fu],
				HexDigits[value & 0x0Fu],
				0,
			};
			append_text(destination, capacity, text);
		}

		void append_decimal(char* destination, stduint capacity, uint64 value) {
			char digits[24] = {};
			stduint count = 0;
			do {
				digits[count++] = char('0' + value % 10u);
				value /= 10u;
			} while (value && count < sizeof(digits));
			while (count) {
				char text[2] = { digits[--count], 0 };
				append_text(destination, capacity, text);
			}
		}

		const char* acpi_malformed_kind_name(AcpiMalformedKind kind) {
			switch (kind) {
			case AcpiMalformedKind::SerialHeader: return "serial-header";
			case AcpiMalformedKind::SerialLength: return "serial-length";
			case AcpiMalformedKind::ResourceHeader: return "resource-header";
			case AcpiMalformedKind::ResourceLength: return "resource-length";
			case AcpiMalformedKind::Path: return "path";
			case AcpiMalformedKind::Namespace: return "namespace";
			default: return "unknown";
			}
		}

		void report_acpi_malformed(AcpiMalformedKind kind, const uint8* address) {
			++acpi_malformed_count;
			const stduint kind_index = stduint(kind);
			if (kind_index < stduint(AcpiMalformedKind::Count)) {
				++acpi_malformed_kind_counts[kind_index];
			}
			if (acpi_malformed_record_count >= AcpiMalformedRecordCapacity) return;
			acpi_malformed_records[acpi_malformed_record_count++] = { address, kind };
		}

		bool parse_aml_name_segment(const uint8*& cursor, const uint8* end, char segment[5]) {
			if (!cursor || end - cursor < 4 || !is_aml_name_lead(cursor[0])) return false;
			for (stduint index = 1; index < 4; ++index) {
				if (!is_aml_name_char(cursor[index])) return false;
			}
			for (stduint index = 0; index < 4; ++index) segment[index] = char(cursor[index]);
			segment[4] = 0;
			cursor += 4;
			return true;
		}

		bool parse_aml_name_path(const uint8*& cursor, const uint8* end, AcpiNamePath& name) {
			name = {};
			if (!cursor || cursor >= end) return false;
			if (*cursor == '\\') {
				name.rooted = true;
				++cursor;
			}
			while (cursor < end && *cursor == '^') {
				if (name.parent_count == 0xFFu) return false;
				++name.parent_count;
				++cursor;
			}
			if (cursor >= end) return false;
			if (*cursor == 0x00) {
				++cursor;
				return true;
			}
			uint8 count = 1;
			if (*cursor == 0x2E) {
				count = 2;
				++cursor;
			}
			else if (*cursor == 0x2F) {
				++cursor;
				if (cursor >= end) return false;
				count = *cursor++;
				if (!count) return false;
			}
			if (count > AcpiNameSegmentCapacity) return false;
			for (uint8 index = 0; index < count; ++index) {
				if (!parse_aml_name_segment(cursor, end, name.segments[index])) return false;
			}
			name.segment_count = count;
			return true;
		}

		bool path_is_child_name(const char* path, const char* parent, const char* name) {
			char expected[AcpiPathCapacity] = {};
			copy_text(expected, sizeof(expected), parent);
			if (text_length(expected) > 1) append_text(expected, sizeof(expected), ".");
			append_text(expected, sizeof(expected), name);
			return equal_text(path, expected);
		}

		int find_acpi_device(const char* path) {
			for0(index, acpi_device_count) {
				if (equal_text(acpi_devices[index].path, path)) return int(index);
			}
			return -1;
		}

		int find_or_add_acpi_device(const char* path, bool conditional) {
			const int existing = find_acpi_device(path);
			if (existing >= 0) {
				acpi_devices[existing].conditional |= conditional;
				return existing;
			}
			if (acpi_device_count >= AcpiDeviceCapacity) {
				acpi_device_truncated = true;
				return -1;
			}
			auto& device = acpi_devices[acpi_device_count];
			device = {};
			copy_text(device.path, sizeof(device.path), path);
			device.conditional = conditional;
			return int(acpi_device_count++);
		}

		uint16 read_le16(const uint8* data) {
			return uint16(data[0]) | uint16(data[1]) << 8;
		}

		uint32 read_le32(const uint8* data) {
			return uint32(data[0]) | uint32(data[1]) << 8 |
				uint32(data[2]) << 16 | uint32(data[3]) << 24;
		}

		void encode_eisa_id(uint32 encoded, char output[AcpiIdCapacity]) {
			const uint32 value = (encoded >> 24) | ((encoded >> 8) & 0x0000FF00u) |
				((encoded << 8) & 0x00FF0000u) | (encoded << 24);
			const uint8 first = uint8((value >> 26) & 0x1Fu);
			const uint8 second = uint8((value >> 21) & 0x1Fu);
			const uint8 third = uint8((value >> 16) & 0x1Fu);
			if (!first || first > 26 || !second || second > 26 || !third || third > 26) {
				output[0] = 0;
				append_hex(output, AcpiIdCapacity, encoded);
				return;
			}
			constexpr char HexDigits[] = "0123456789ABCDEF";
			output[0] = char('@' + first);
			output[1] = char('@' + second);
			output[2] = char('@' + third);
			output[3] = HexDigits[(value >> 12) & 0xFu];
			output[4] = HexDigits[(value >> 8) & 0xFu];
			output[5] = HexDigits[(value >> 4) & 0xFu];
			output[6] = HexDigits[value & 0xFu];
			output[7] = 0;
		}

		bool decode_aml_string(const uint8*& cursor, const uint8* end, char* output, stduint capacity) {
			if (!cursor || cursor >= end || *cursor != 0x0D) return false;
			const uint8* text = ++cursor;
			while (cursor < end && *cursor) ++cursor;
			if (cursor >= end) return false;
			copy_text_range(output, capacity, text, cursor);
			++cursor;
			return true;
		}

		bool decode_aml_id(const uint8*& cursor, const uint8* end, char output[AcpiIdCapacity]) {
			const uint8* original = cursor;
			if (decode_aml_string(cursor, end, output, AcpiIdCapacity)) return true;
			cursor = original;
			uint64 integer = 0;
			if (!decode_aml_integer(cursor, end, integer)) return false;
			encode_eisa_id(uint32(integer), output);
			return true;
		}

		bool aml_named_integer(const uni::AMLNamespace& aml_namespace, stduint parent, const char name[5], uint64& value) {
			const stduint object_index = aml_namespace.FindChild(parent, name);
			const auto* object = aml_namespace.At(object_index);
			if (!object || object->type != uni::AMLNamespace::ObjectType::Name || !object->value || !object->value_length || (object->flags & uni::AMLNamespace::ObjectFlagConditional)) return false;
			const uint8* cursor = object->value;
			return decode_aml_integer(cursor, object->value + object->value_length, value);
		}

		bool aml_device_is_pci_root(const uni::AMLNamespace& aml_namespace, stduint device_index) {
			const char hid_name[5] = { '_', 'H', 'I', 'D', 0 };
			const stduint hid_index = aml_namespace.FindChild(device_index, hid_name);
			const auto* hid = aml_namespace.At(hid_index);
			if (!hid || hid->type != uni::AMLNamespace::ObjectType::Name || !hid->value || !hid->value_length || (hid->flags & uni::AMLNamespace::ObjectFlagConditional)) return false;
			char id[AcpiIdCapacity] = {};
			const uint8* cursor = hid->value;
			if (!decode_aml_id(cursor, hid->value + hid->value_length, id)) return false;
			return StrCompare(id, "PNP0A03") == 0 || StrCompare(id, "PNP0A08") == 0;
		}

		bool aml_pci_config_available(const AmlPciLocation& location, uint16 register_address) {
			return (location.segment == 0 && register_address < 0x100u) || uni::PCI::is_ecam_config_access_available(location.segment, location.bus, location.device, location.function, register_address);
		}

		bool resolve_aml_pci_scope(const uni::AMLNamespace& aml_namespace, stduint scope, AmlPciLocation& location) {
			location = {};
			uint64 addresses[uni::AMLNamespace::MaximumDepth] = {};
			stduint address_count = 0;
			stduint pci_root_object = uni::AMLNamespace::InvalidIndex;
			const char adr_name[5] = { '_', 'A', 'D', 'R', 0 };
			for0(depth, uni::AMLNamespace::MaximumDepth) {
				const auto* object = aml_namespace.At(scope);
				if (!object) break;
				if (object->type == uni::AMLNamespace::ObjectType::Device) {
					if (aml_device_is_pci_root(aml_namespace, scope)) {
						pci_root_object = scope;
						break;
					}
					uint64 address = 0;
					if (aml_named_integer(aml_namespace, scope, adr_name, address)) addresses[address_count++] = address;
				}
				if (object->parent == uni::AMLNamespace::InvalidIndex || object->parent == scope) break;
				scope = object->parent;
			}
			if (pci_root_object == uni::AMLNamespace::InvalidIndex) return false;
			const char seg_name[5] = { '_', 'S', 'E', 'G', 0 };
			const char bbn_name[5] = { '_', 'B', 'B', 'N', 0 };
			uint64 segment = 0;
			uint64 bus_number = 0;
			uint64 named_value = 0;
			if (aml_named_integer(aml_namespace, pci_root_object, seg_name, named_value)) segment = named_value;
			if (aml_named_integer(aml_namespace, pci_root_object, bbn_name, named_value)) bus_number = named_value;
			if (segment > 0xFFFFu || bus_number > 0xFFu) return false;
			if (!address_count) {
				uint64 root_address = 0;
				if (!aml_named_integer(aml_namespace, pci_root_object, adr_name, root_address)) return false;
				addresses[address_count++] = root_address;
			}
			uint8 bus = uint8(bus_number);
			for (stduint index = address_count; index; --index) {
				const uint64 address = addresses[index - 1];
				const uint64 device = address >> 16;
				const uint64 function = address & 0xFFFFu;
				if (device >= 32 || function >= 8) return false;
				location.segment = uint16(segment);
				location.bus = bus;
				location.device = uint8(device);
				location.function = uint8(function);
				if (index == 1) {
					return true;
				}
				if (!aml_pci_config_available(location, 0) || !aml_pci_config_available(location, 0x08) || !aml_pci_config_available(location, 0x18)) return false;
				if (uint16(uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, 0)) == 0xFFFFu) return false;
				const uint32 class_code = uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, 0x08);
				const uint8 bridge_subclass = uint8(class_code >> 16);
				if (uint8(class_code >> 24) != 0x06u || (bridge_subclass != 0x04u && bridge_subclass != 0x09u)) return false;
				const uint32 buses = uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, 0x18);
				bus = uint8(buses >> 8);
				if (!bus) return false;
			}
			return false;
		}

		bool resolve_aml_pci_region_owner(const uni::AMLNamespace& aml_namespace, stduint region_object, AmlPciLocation& location) {
			const auto* region = aml_namespace.At(region_object);
			return region && region->type == uni::AMLNamespace::ObjectType::OperationRegion && resolve_aml_pci_scope(aml_namespace, region->parent, location);
		}

		bool aml_pci_device_presence(const uni::AMLNamespace& aml_namespace, stduint device_object, bool& present) {
			AmlPciLocation location = {};
			if (!resolve_aml_pci_scope(aml_namespace, device_object, location) || !aml_pci_config_available(location, 0)) return false;
			present = uint16(uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, 0)) != 0xFFFFu;
			return true;
		}

		void record_aml_pci_read(const uni::AMLNamespace& aml_namespace, stduint region_object, AmlPciReadStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlPciReadStatus::Count)) return;
			if (!aml_pci_read_counts[index] && !aml_namespace.FormatPath(region_object, aml_pci_read_examples[index], sizeof(aml_pci_read_examples[index]))) copy_text(aml_pci_read_examples[index], sizeof(aml_pci_read_examples[index]), "<unknown>");
			++aml_pci_read_counts[index];
		}

		bool read_aml_pci_region(void* context, const uni::AMLNamespace& aml_namespace, stduint region_object, uint8 space, uint64 address, uint8 width, uint64& value) {
			(void)context;
			if (space != 0x02) return false;
			AmlPciLocation location = {};
			if (!resolve_aml_pci_region_owner(aml_namespace, region_object, location)) {
				record_aml_pci_read(aml_namespace, region_object, AmlPciReadStatus::OwnerUnresolved);
				return false;
			}
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address >= 0x1000u || width / 8 > 0x1000u - address) {
				record_aml_pci_read(aml_namespace, region_object, AmlPciReadStatus::InvalidAccess);
				return false;
			}
			const uint8 byte_count = width / 8;
			uint16 cached_register = 0xFFFFu;
			uint32 register_value = 0;
			value = 0;
			for (uint8 index = 0; index < byte_count; ++index) {
				const uint16 byte_address = uint16(address + index);
				const uint16 register_address = byte_address & 0xFFFCu;
				if (register_address != cached_register) {
					const bool available = aml_pci_config_available(location, register_address);
					if (!available) {
						record_aml_pci_read(aml_namespace, region_object, AmlPciReadStatus::ConfigUnavailable);
						return false;
					}
					register_value = uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, register_address);
					cached_register = register_address;
				}
				value |= uint64(uint8(register_value >> ((byte_address & 3u) * 8))) << (index * 8);
			}
			record_aml_pci_read(aml_namespace, region_object, AmlPciReadStatus::Success);
			return true;
		}

		bool aml_package_end(const uint8* length_at, const uint8* limit, const uint8*& content, const uint8*& package_end) {
			uint32 package_length = 0;
			uint8 encoded_length = 0;
			if (!length_at || length_at >= limit ||
				!decode_aml_package_length(length_at, limit - length_at,
					package_length, encoded_length) ||
				package_length > stduint(limit - length_at)) return false;
			content = length_at + encoded_length;
			package_end = length_at + package_length;
			return content <= package_end;
		}

		const uint8* skip_aml_data_object(const uint8* cursor, const uint8* end) {
			if (!cursor || cursor >= end) return cursor;
			const uint8 opcode = *cursor;
			if (opcode == 0x00 || opcode == 0x01 || opcode == 0xFF) return cursor + 1;
			if (opcode == 0x0A) return end - cursor >= 2 ? cursor + 2 : end;
			if (opcode == 0x0B) return end - cursor >= 3 ? cursor + 3 : end;
			if (opcode == 0x0C) return end - cursor >= 5 ? cursor + 5 : end;
			if (opcode == 0x0E) return end - cursor >= 9 ? cursor + 9 : end;
			if (opcode == 0x0D) {
				++cursor;
				while (cursor < end && *cursor) ++cursor;
				return cursor < end ? cursor + 1 : end;
			}
			if (opcode == 0x11 || opcode == 0x12 || opcode == 0x13) {
				const uint8* content = nullptr;
				const uint8* package_end = nullptr;
				return aml_package_end(cursor + 1, end, content, package_end) ? package_end : end;
			}
			const uint8* name_cursor = cursor;
			AcpiNamePath name = {};
			if (parse_aml_name_path(name_cursor, end, name)) return name_cursor;
			return cursor + 1;
		}

		void decode_aml_cid_package(const uint8* package_opcode, const uint8* end, char output[AcpiIdCapacity]) {
			output[0] = 0;
			const uint8* cursor = nullptr;
			const uint8* package_end = nullptr;
			if (!package_opcode || package_opcode >= end || *package_opcode != 0x12 ||
				!aml_package_end(package_opcode + 1, end, cursor, package_end) ||
				cursor >= package_end) return;
			const uint8 element_count = *cursor++;
			for (uint8 index = 0; index < element_count && cursor < package_end; ++index) {
				char id[AcpiIdCapacity] = {};
				const uint8* value = cursor;
				if (decode_aml_id(value, package_end, id)) {
					if (output[0]) append_text(output, AcpiIdCapacity, ",");
					append_text(output, AcpiIdCapacity, id);
					cursor = value;
				}
				else cursor = skip_aml_data_object(cursor, package_end);
			}
		}

		void copy_resource_source(char destination[AcpiPathCapacity], const uint8* source, const uint8* end) {
			copy_text_range(destination, AcpiPathCapacity, source, end);
			if (!destination[0]) copy_text(destination, AcpiPathCapacity, "<none>");
		}

		void append_serial_resource(uint16 device_index, const uint8* descriptor, stduint descriptor_length) {
			if (descriptor_length < 12 || descriptor[0] != 0x8E) {
				report_acpi_malformed(AcpiMalformedKind::SerialHeader, descriptor);
				return;
			}
			const uint16 type_data_length = read_le16(descriptor + 10);
			if (type_data_length > descriptor_length - 12) {
				report_acpi_malformed(AcpiMalformedKind::SerialLength, descriptor);
				return;
			}
			if (acpi_serial_resource_count >= AcpiSerialResourceCapacity) {
				acpi_serial_resource_truncated = true;
				return;
			}
			auto& resource = acpi_serial_resources[acpi_serial_resource_count];
			resource = {};
			resource.device_index = device_index;
			resource.bus_type = descriptor[5];
			resource.general_flags = descriptor[6];
			resource.type_flags = read_le16(descriptor + 7);
			const uint8* source = descriptor + 12 + type_data_length;
			copy_resource_source(resource.controller, source,
				descriptor + descriptor_length);
			switch (AcpiSerialBusType(resource.bus_type)) {
			case AcpiSerialBusType::I2C:
				if (type_data_length < 6) {
					report_acpi_malformed(AcpiMalformedKind::SerialLength, descriptor);
					return;
				}
				resource.speed = read_le32(descriptor + 12);
				resource.address = read_le16(descriptor + 16);
				break;
			case AcpiSerialBusType::SPI:
				if (type_data_length < 9) {
					report_acpi_malformed(AcpiMalformedKind::SerialLength, descriptor);
					return;
				}
				resource.speed = read_le32(descriptor + 12);
				resource.data_bits = descriptor[16];
				resource.clock_phase = descriptor[17];
				resource.clock_polarity = descriptor[18];
				resource.address = read_le16(descriptor + 19);
				break;
			case AcpiSerialBusType::UART:
				if (type_data_length < 10) {
					report_acpi_malformed(AcpiMalformedKind::SerialLength, descriptor);
					return;
				}
				resource.speed = read_le32(descriptor + 12);
				resource.receive_fifo = read_le16(descriptor + 16);
				resource.transmit_fifo = read_le16(descriptor + 18);
				resource.parity = descriptor[20];
				resource.lines_enabled = descriptor[21];
				break;
			default:
				break;
			}
			++acpi_serial_resource_count;
		}

		void parse_resource_template(uint16 device_index, const uint8* data, stduint length) {
			const uint8* cursor = data;
			const uint8* end = data + length;
			while (cursor < end) {
				if (*cursor & 0x80u) {
					if (end - cursor < 3) {
						report_acpi_malformed(AcpiMalformedKind::ResourceHeader, cursor);
						return;
					}
					const stduint data_length = read_le16(cursor + 1);
					const stduint descriptor_length = data_length + 3;
					if (descriptor_length > stduint(end - cursor)) {
						report_acpi_malformed(AcpiMalformedKind::ResourceLength, cursor);
						return;
					}
					if (*cursor == 0x8E) {
						append_serial_resource(device_index, cursor, descriptor_length);
					}
					cursor += descriptor_length;
					continue;
				}
				const stduint data_length = *cursor & 0x07u;
				const stduint descriptor_length = data_length + 1;
				if (descriptor_length > stduint(end - cursor)) {
					report_acpi_malformed(AcpiMalformedKind::ResourceLength, cursor);
					return;
				}
				const uint8 item_name = (*cursor >> 3) & 0x0Fu;
				cursor += descriptor_length;
				if (item_name == 0x0Fu) return;
			}
		}

		void apply_named_object(int device_index, const char* device_path, const char* object_path, const uint8* value, const uint8* end) {
			if (device_index < 0 || !value || value >= end) return;
			auto& device = acpi_devices[device_index];
			if (path_is_child_name(object_path, device_path, "_HID")) {
				const uint8* cursor = value;
				if (decode_aml_id(cursor, end, device.hid)) device.has_hid = true;
				return;
			}
			if (path_is_child_name(object_path, device_path, "_CID")) {
				const uint8* cursor = value;
				if (*cursor == 0x12) decode_aml_cid_package(cursor, end, device.cid);
				else decode_aml_id(cursor, end, device.cid);
				device.has_cid = device.cid[0] != 0;
				return;
			}
			if (path_is_child_name(object_path, device_path, "_UID")) {
				const uint8* cursor = value;
				if (decode_aml_string(cursor, end, device.uid, sizeof(device.uid))) {
					device.has_uid = true;
					return;
				}
				cursor = value;
				uint64 integer = 0;
				if (decode_aml_integer(cursor, end, integer)) {
					device.uid[0] = 0;
					append_decimal(device.uid, sizeof(device.uid), integer);
					device.has_uid = true;
				}
				return;
			}
			if (path_is_child_name(object_path, device_path, "_STA")) {
				const uint8* cursor = value;
				if (decode_aml_integer(cursor, end, device.sta)) device.has_sta = true;
				else {
					device.sta_dynamic = true;
					device.sta_method = false;
					if (value < end) {
						device.sta_expression = value;
						device.sta_expression_length = stduint(end - value);
						device.sta_first_opcode = *value;
						device.sta_value_opcode = 0;
						device.has_sta_pattern = true;
					}
				}
				return;
			}
			if (path_is_child_name(object_path, device_path, "_ADR")) {
				const uint8* cursor = value;
				if (decode_aml_integer(cursor, end, device.adr)) device.has_adr = true;
				return;
			}
			if (path_is_child_name(object_path, device_path, "_CRS")) {
				const uint8* data = nullptr;
				stduint length = 0;
				if (decode_aml_buffer(value, end, data, length)) {
					parse_resource_template(uint16(device_index), data, length);
				}
				else device.crs_dynamic = true;
			}
		}

		void mark_dynamic_method(int device_index, const char* device_path, const char* method_path) {
			if (device_index < 0) return;
			auto& device = acpi_devices[device_index];
			if (path_is_child_name(method_path, device_path, "_STA")) device.sta_dynamic = true;
			if (path_is_child_name(method_path, device_path, "_CRS")) device.crs_dynamic = true;
		}

		bool decode_static_method_integer(const uint8* body, const uint8* end, uint64& value) {
			if (!body || body >= end || *body != 0xA4) return false;
			const uint8* cursor = body + 1;
			return decode_aml_integer(cursor, end, value);
		}

		bool decode_static_method_buffer(const uint8* body, const uint8* end, const uint8*& data, stduint& data_length) {
			struct NamedBuffer {
				char name[5];
				const uint8* data;
				stduint length;
			};
			NamedBuffer named_buffers[8] = {};
			stduint named_buffer_count = 0;
			const uint8* cursor = body;
			while (cursor < end) {
				if (*cursor == 0xA4) {
					const uint8* value = cursor + 1;
					if (decode_aml_buffer(value, end, data, data_length)) return true;
					AcpiNamePath return_name = {};
					if (parse_aml_name_path(value, end, return_name) &&
						!return_name.rooted && !return_name.parent_count &&
						return_name.segment_count == 1) {
						for0(index, named_buffer_count) {
							if (equal_text(named_buffers[index].name,
								return_name.segments[0])) {
								data = named_buffers[index].data;
								data_length = named_buffers[index].length;
								return true;
							}
						}
					}
					++cursor;
					continue;
				}
				if (*cursor == 0x08) {
					const uint8* value = cursor + 1;
					AcpiNamePath name = {};
					if (!parse_aml_name_path(value, end, name)) {
						++cursor;
						continue;
					}
					const uint8* buffer_data = nullptr;
					stduint buffer_length = 0;
					if (!name.rooted && !name.parent_count &&
						name.segment_count == 1 &&
						named_buffer_count < numsof(named_buffers) &&
						decode_aml_buffer(value, end, buffer_data, buffer_length)) {
						auto& named = named_buffers[named_buffer_count++];
						copy_text(named.name, sizeof(named.name), name.segments[0]);
						named.data = buffer_data;
						named.length = buffer_length;
					}
					cursor = skip_aml_data_object(value, end);
					continue;
				}
				if (*cursor == 0x11 || *cursor == 0x12 || *cursor == 0x13 ||
					*cursor == 0x0D) {
					cursor = skip_aml_data_object(cursor, end);
					continue;
				}
				++cursor;
			}
			return false;
		}

		void apply_method_object(int device_index, const char* device_path, const char* method_path, const uint8* body, const uint8* end) {
			if (device_index < 0) return;
			auto& device = acpi_devices[device_index];
			if (path_is_child_name(method_path, device_path, "_STA")) {
				if (decode_static_method_integer(body, end, device.sta)) {
					device.has_sta = true;
					device.sta_dynamic = false;
				}
				else {
					device.sta_dynamic = true;
					device.sta_method = true;
					if (body < end) {
						device.sta_expression = body;
						device.sta_expression_length = stduint(end - body);
						device.sta_first_opcode = *body;
						device.sta_value_opcode = *body == 0xA4 && body + 1 < end ? body[1] : 0;
						device.has_sta_pattern = true;
					}
				}
				return;
			}
			if (path_is_child_name(method_path, device_path, "_CRS")) {
				const uint8* data = nullptr;
				stduint data_length = 0;
				device.crs_dynamic = true;
				if (decode_static_method_buffer(body, end, data, data_length)) {
					parse_resource_template(uint16(device_index), data, data_length);
				}
			}
		}

		const char* aml_namespace_status_name(uni::AMLNamespace::Status status) {
			switch (status) {
			case uni::AMLNamespace::Status::Success: return "success";
			case uni::AMLNamespace::Status::InvalidArgument: return "invalid-argument";
			case uni::AMLNamespace::Status::MalformedPackage: return "malformed-package";
			case uni::AMLNamespace::Status::MalformedName: return "malformed-name";
			case uni::AMLNamespace::Status::MalformedObject: return "malformed-object";
			case uni::AMLNamespace::Status::Capacity: return "capacity";
			case uni::AMLNamespace::Status::DepthLimit: return "depth-limit";
			default: return "unknown";
			}
		}

		const char* aml_execution_status_name(uni::AMLExecutor::Status status) {
			switch (status) {
			case uni::AMLExecutor::Status::Success: return "success";
			case uni::AMLExecutor::Status::InvalidArgument: return "invalid-argument";
			case uni::AMLExecutor::Status::NotFound: return "not-found";
			case uni::AMLExecutor::Status::Malformed: return "malformed";
			case uni::AMLExecutor::Status::UnsupportedOpcode: return "unsupported-opcode";
			case uni::AMLExecutor::Status::UnsupportedObject: return "unsupported-object";
			case uni::AMLExecutor::Status::ConditionalObject: return "conditional-object";
			case uni::AMLExecutor::Status::TypeMismatch: return "type-mismatch";
			case uni::AMLExecutor::Status::InvalidTarget: return "invalid-target";
			case uni::AMLExecutor::Status::RegionUnavailable: return "region-unavailable";
			case uni::AMLExecutor::Status::FrameLimit: return "frame-limit";
			case uni::AMLExecutor::Status::InstructionLimit: return "instruction-limit";
			case uni::AMLExecutor::Status::LoopLimit: return "loop-limit";
			case uni::AMLExecutor::Status::ObjectValueCapacity: return "object-value-capacity";
			case uni::AMLExecutor::Status::NoReturn: return "no-return";
			default: return "unknown";
			}
		}

		const char* aml_region_space_name(uint16 space) {
			switch (space) {
			case 0x00: return "system-memory";
			case 0x01: return "system-io";
			case 0x02: return "pci-config";
			case 0x03: return "embedded-control";
			case 0x04: return "smbus";
			case 0x05: return "system-cmos";
			case 0x06: return "pci-bar-target";
			case 0x07: return "ipmi";
			case 0x08: return "gpio";
			case 0x09: return "generic-serial-bus";
			case 0x0A: return "platform-communications-channel";
			case 0x7F: return "functional-fixed-hardware";
			default: return "unknown";
			}
		}

		const char* aml_pci_read_status_name(AmlPciReadStatus status) {
			switch (status) {
			case AmlPciReadStatus::Success: return "success";
			case AmlPciReadStatus::OwnerUnresolved: return "owner-unresolved";
			case AmlPciReadStatus::InvalidAccess: return "invalid-access";
			case AmlPciReadStatus::ConfigUnavailable: return "config-unavailable";
			default: return "unknown";
			}
		}

		void record_aml_region(const uni::AMLNamespace& aml_namespace, const char* path, const uni::AMLExecutor::Error& error) {
			const auto* field = aml_namespace.At(error.object);
			if (!field || field->type != uni::AMLNamespace::ObjectType::FieldUnit) return;
			uint16 space = 0x100;
			const auto* region = aml_namespace.At(field->reference);
			if (region && region->type == uni::AMLNamespace::ObjectType::OperationRegion && region->value && region->value_length) space = region->value[0];
			stduint index = 0;
			while (index < aml_region_summary_count && (aml_region_summaries[index].space != space || aml_region_summaries[index].field_opcode != error.opcode || aml_region_summaries[index].field_flags != field->field_flags)) ++index;
			if (index >= aml_region_summary_count) {
				if (aml_region_summary_count >= numsof(aml_region_summaries)) return;
				auto& summary = aml_region_summaries[aml_region_summary_count++];
				summary.space = space;
				summary.field_opcode = error.opcode;
				summary.field_flags = field->field_flags;
				copy_text(summary.example, sizeof(summary.example), path);
			}
			++aml_region_summaries[index].count;
		}

		void record_aml_evaluation(const uni::AMLNamespace& aml_namespace, uni::AMLExecutor::Status status, const char* path, const uni::AMLExecutor::Error& error) {
			stduint index = 0;
			while (index < aml_evaluation_summary_count && (aml_evaluation_summaries[index].status != status || aml_evaluation_summaries[index].opcode != error.opcode)) ++index;
			if (index >= aml_evaluation_summary_count) {
				if (aml_evaluation_summary_count >= numsof(aml_evaluation_summaries)) return;
				auto& summary = aml_evaluation_summaries[aml_evaluation_summary_count++];
				summary.status = status;
				summary.opcode = error.opcode;
				copy_text(summary.example, sizeof(summary.example), path);
			}
			++aml_evaluation_summaries[index].count;
			if (status == uni::AMLExecutor::Status::RegionUnavailable) record_aml_region(aml_namespace, path, error);
		}

		void log_aml_evaluation_summaries() {
			for0(index, aml_evaluation_summary_count) {
				const auto& summary = aml_evaluation_summaries[index];
				ploginfo("[ACPI-EVAL-SUM] object=_STA status=%s count=%u example=%s opcode=%[16H]", aml_execution_status_name(summary.status), summary.count, summary.example, summary.opcode);
			}
			for0(index, aml_region_summary_count) {
				const auto& summary = aml_region_summaries[index];
				ploginfo("[ACPI-REGION-SUM] space=%s id=%[16H] field=%[16H] flags=%[8H] count=%u example=%s", aml_region_space_name(summary.space), summary.space, summary.field_opcode, summary.field_flags, summary.count, summary.example);
			}
			for0(index, stduint(AmlPciReadStatus::Count)) {
				if (!aml_pci_read_counts[index]) continue;
				ploginfo("[ACPI-PCI-READ-SUM] status=%s count=%u example=%s", aml_pci_read_status_name(AmlPciReadStatus(index)), aml_pci_read_counts[index], aml_pci_read_examples[index]);
			}
		}

		void load_x86_aml_namespace(uni::AMLNamespace& aml_namespace) {
			aml_namespace.Reset();
			for0(table_index, aml_table_count) {
				const auto& table = aml_tables[table_index];
				const auto status = aml_namespace.Load(table.aml, table.length, uint16(table_index));
				if (status == uni::AMLNamespace::Status::Success) continue;
				const auto& error = aml_namespace.LastError();
				report_acpi_malformed(AcpiMalformedKind::Namespace, error.address);
				plogwarn("[ACPI-NS-WARN] table=%s index=%u offset=%[x] status=%s",
					table.signature, table_index, error.offset, aml_namespace_status_name(status));
				if (status == uni::AMLNamespace::Status::Capacity) break;
			}
		}

		enum class AcpiParentState : uint8 {
			Present,
			Absent,
			Unknown,
		};

		stduint aml_device_depth(const uni::AMLNamespace& aml_namespace, stduint object_index) {
			stduint depth = 0;
			for0(level, uni::AMLNamespace::MaximumDepth) {
				const auto* object = aml_namespace.At(object_index);
				if (!object) break;
				if (object->type == uni::AMLNamespace::ObjectType::Device) ++depth;
				if (object->parent == uni::AMLNamespace::InvalidIndex || object->parent == object_index) break;
				object_index = object->parent;
			}
			return depth;
		}

		AcpiParentState acpi_parent_state(const uni::AMLNamespace& aml_namespace, stduint object_index) {
			const auto* child = aml_namespace.At(object_index);
			if (!child) return AcpiParentState::Unknown;
			stduint parent_index = child->parent;
			for0(level, uni::AMLNamespace::MaximumDepth) {
				const auto* parent = aml_namespace.At(parent_index);
				if (!parent) break;
				if (parent->type == uni::AMLNamespace::ObjectType::Device) {
					char parent_path[AcpiPathCapacity] = {};
					if (!aml_namespace.FormatPath(parent_index, parent_path, sizeof(parent_path))) return AcpiParentState::Unknown;
					const int device_index = find_acpi_device(parent_path);
					if (device_index < 0) return AcpiParentState::Unknown;
					const auto& device = acpi_devices[device_index];
					if (device.has_sta && !(device.sta & 1u)) return AcpiParentState::Absent;
					if (device.conditional || device.sta_dynamic) return AcpiParentState::Unknown;
				}
				if (parent->parent == uni::AMLNamespace::InvalidIndex || parent->parent == parent_index) break;
				parent_index = parent->parent;
			}
			return AcpiParentState::Present;
		}

		void evaluate_x86_acpi_device_statuses(const uni::AMLNamespace& aml_namespace, uni::AMLExecutor& aml_executor) {
			const char sta_name[5] = { '_', 'S', 'T', 'A', 0 };
			for (stduint depth = 1; depth <= uni::AMLNamespace::MaximumDepth; ++depth) {
				for (stduint object_index = 1; object_index < aml_namespace.Count(); ++object_index) {
					const auto* object = aml_namespace.At(object_index);
					if (!object || object->type != uni::AMLNamespace::ObjectType::Device || aml_device_depth(aml_namespace, object_index) != depth) continue;
					char device_path[AcpiPathCapacity] = {};
					if (!aml_namespace.FormatPath(object_index, device_path, sizeof(device_path))) continue;
					const int device_index = find_acpi_device(device_path);
					if (device_index < 0) continue;
					auto& device = acpi_devices[device_index];
					const AcpiParentState parent_state = acpi_parent_state(aml_namespace, object_index);
					if (parent_state == AcpiParentState::Absent) {
						device.sta = 0;
						device.has_sta = true;
						device.sta_dynamic = false;
						device.conditional = false;
						++acpi_parent_absent_count;
						continue;
					}
					if (parent_state == AcpiParentState::Unknown) {
						device.sta_dynamic = true;
						++acpi_parent_unknown_count;
						continue;
					}
					const stduint sta_index = aml_namespace.FindChild(object_index, sta_name);
					if (device.conditional) continue;
					if (device.sta_dynamic) {
						if (sta_index == uni::AMLNamespace::InvalidIndex) continue;
						uint64 sta = 0;
						const auto status = aml_executor.EvaluateInteger(sta_index, sta);
						record_aml_evaluation(aml_namespace, status, device_path, aml_executor.LastError());
						if (status == uni::AMLExecutor::Status::Success) {
							device.sta = sta;
							device.has_sta = true;
							device.sta_dynamic = false;
						}
						continue;
					}
					if (sta_index == uni::AMLNamespace::InvalidIndex && !device.has_sta) {
						bool present = false;
						if (aml_pci_device_presence(aml_namespace, object_index, present) && !present) {
							device.sta = 0;
							device.has_sta = true;
							++acpi_pci_absent_count;
						}
					}
				}
			}
		}

		void collect_x86_acpi_devices(const uni::AMLNamespace& aml_namespace, uni::AMLExecutor& aml_executor) {
			for (stduint object_index = 1; object_index < aml_namespace.Count(); ++object_index) {
				const auto* object = aml_namespace.At(object_index);
				if (!object || object->type != uni::AMLNamespace::ObjectType::Device) continue;
				char path[AcpiPathCapacity] = {};
				if (!aml_namespace.FormatPath(object_index, path, sizeof(path))) {
					report_acpi_malformed(AcpiMalformedKind::Path, object->value);
					continue;
				}
				find_or_add_acpi_device(path, object->flags & uni::AMLNamespace::ObjectFlagConditional);
			}

			for (stduint object_index = 1; object_index < aml_namespace.Count(); ++object_index) {
				const auto* object = aml_namespace.At(object_index);
				if (!object || object->parent == uni::AMLNamespace::InvalidIndex) continue;
				if (object->type != uni::AMLNamespace::ObjectType::Name && object->type != uni::AMLNamespace::ObjectType::Method) continue;
				const auto* parent = aml_namespace.At(object->parent);
				if (!parent || parent->type != uni::AMLNamespace::ObjectType::Device) continue;
				char device_path[AcpiPathCapacity] = {};
				char object_path[AcpiPathCapacity] = {};
				if (!aml_namespace.FormatPath(object->parent, device_path, sizeof(device_path)) || !aml_namespace.FormatPath(object_index, object_path, sizeof(object_path))) {
					report_acpi_malformed(AcpiMalformedKind::Path, object->value);
					continue;
				}
				const int device_index = find_acpi_device(device_path);
				if (device_index < 0) continue;
				if (object->flags & uni::AMLNamespace::ObjectFlagConditional) {
					acpi_devices[device_index].conditional = true;
					continue;
				}
				const uint8* value_end = object->value + object->value_length;
				if (object->type == uni::AMLNamespace::ObjectType::Name) apply_named_object(device_index, device_path, object_path, object->value, value_end);
				else {
					mark_dynamic_method(device_index, device_path, object_path);
					apply_method_object(device_index, device_path, object_path, object->value, value_end);
				}
			}
			evaluate_x86_acpi_device_statuses(aml_namespace, aml_executor);
		}

		void log_acpi_device(const AcpiDeviceRecord& device) {
			char line[512] = {};
			append_text(line, sizeof(line), "[ACPI-DEV] ");
			append_text(line, sizeof(line), device.path);
			if (device.has_hid) {
				append_text(line, sizeof(line), " HID=");
				append_text(line, sizeof(line), device.hid);
			}
			if (device.has_cid) {
				append_text(line, sizeof(line), " CID=");
				append_text(line, sizeof(line), device.cid);
			}
			if (device.has_uid) {
				append_text(line, sizeof(line), " UID=");
				append_text(line, sizeof(line), device.uid);
			}
			if (device.has_sta) {
				append_text(line, sizeof(line), " STA=");
				append_hex(line, sizeof(line), device.sta);
			}
			else if (device.sta_dynamic) append_text(line, sizeof(line), " STA=dynamic");
			if (device.has_adr) {
				append_text(line, sizeof(line), " ADR=");
				append_hex(line, sizeof(line), device.adr);
			}
			if (device.crs_dynamic) append_text(line, sizeof(line), " CRS=dynamic");
			if (device.conditional) append_text(line, sizeof(line), " conditional");
			ploginfo("%s", line);
		}

		void log_acpi_serial_resource(const AcpiSerialResourceRecord& resource) {
			if (resource.device_index >= acpi_device_count) return;
			const char* device_path = acpi_devices[resource.device_index].path;
			switch (AcpiSerialBusType(resource.bus_type)) {
			case AcpiSerialBusType::I2C:
				ploginfo("[ACPI-I2C] DEV=%s ADR=%[16H] SPEED=%u SOURCE=%s GFLAGS=%[8H] TFLAGS=%[16H]",
					device_path, resource.address, resource.speed,
					resource.controller, resource.general_flags, resource.type_flags);
				break;
			case AcpiSerialBusType::SPI:
				ploginfo("[ACPI-SPI] DEV=%s CS=%u SPEED=%u BITS=%u CPHA=%u CPOL=%u SOURCE=%s GFLAGS=%[8H] TFLAGS=%[16H]",
					device_path, resource.address, resource.speed, resource.data_bits,
					resource.clock_phase, resource.clock_polarity,
					resource.controller, resource.general_flags, resource.type_flags);
				break;
			case AcpiSerialBusType::UART:
				ploginfo("[ACPI-UART] DEV=%s BAUD=%u RX=%u TX=%u PAR=%u LINES=%[8H] SOURCE=%s GFLAGS=%[8H] TFLAGS=%[16H]",
					device_path, resource.speed, resource.receive_fifo,
					resource.transmit_fifo, resource.parity, resource.lines_enabled,
					resource.controller, resource.general_flags, resource.type_flags);
				break;
			default:
				ploginfo("[ACPI-SERIAL] DEV=%s TYPE=%u SOURCE=%s GFLAGS=%[8H] TFLAGS=%[16H]",
					device_path, resource.bus_type, resource.controller,
					resource.general_flags, resource.type_flags);
				break;
			}
		}

		void log_acpi_malformed_records() {
			for (stduint kind_index = 0;
				kind_index < stduint(AcpiMalformedKind::Count); ++kind_index) {
				if (!acpi_malformed_kind_counts[kind_index]) continue;
				plogwarn("[ACPI-AML-SUM] kind=%s count=%u",
					acpi_malformed_kind_name(AcpiMalformedKind(kind_index)),
					acpi_malformed_kind_counts[kind_index]);
			}
			for0(record_index, acpi_malformed_record_count) {
				const auto& record = acpi_malformed_records[record_index];
				const stduint record_address = reinterpret_cast<stduint>(record.address);
				bool located = false;
				for0(table_index, aml_table_count) {
					const auto& table = aml_tables[table_index];
					const stduint table_begin = reinterpret_cast<stduint>(table.aml);
					const stduint table_end = table_begin + table.length;
					if (record_address < table_begin || record_address > table_end) continue;
					char line[256] = {};
					append_text(line, sizeof(line), "[ACPI-AML-WARN] kind=");
					append_text(line, sizeof(line), acpi_malformed_kind_name(record.kind));
					append_text(line, sizeof(line), " table=");
					append_text(line, sizeof(line), table.signature);
					append_text(line, sizeof(line), " index=");
					append_decimal(line, sizeof(line), table_index);
					append_text(line, sizeof(line), " offset=");
					append_hex(line, sizeof(line), record_address - table_begin);
					if (record_address < table_end) {
						append_text(line, sizeof(line), " opcode=");
						append_hex(line, sizeof(line), *record.address);
						append_text(line, sizeof(line), " context=");
						const stduint context_begin = record_address - table_begin > 8u ?
							record_address - 8u : table_begin;
						const stduint context_end = table_end - record_address > 16u ?
							record_address + 16u : table_end;
						for (stduint address = context_begin; address < context_end; ++address) {
							if (address != context_begin) append_text(line, sizeof(line), " ");
							if (address == record_address) append_text(line, sizeof(line), "[");
							append_hex_byte(line, sizeof(line),
								*reinterpret_cast<const uint8*>(address));
							if (address == record_address) append_text(line, sizeof(line), "]");
						}
					}
					plogwarn("%s", line);
					located = true;
					break;
				}
				if (!located) {
					plogwarn("[ACPI-AML-WARN] kind=%s address=%[x] table=unknown",
						acpi_malformed_kind_name(record.kind), record_address);
				}
			}
			if (acpi_malformed_record_count < acpi_malformed_count) {
				plogwarn("[ACPI-AML-WARN] omitted=%u",
					acpi_malformed_count - acpi_malformed_record_count);
			}
		}

		void log_acpi_sta_patterns() {
			struct PatternCount {
				uint8 first_opcode;
				uint8 value_opcode;
				bool method;
				const char* example;
				const uint8* expression;
				stduint expression_length;
				stduint count;
			};
			PatternCount patterns[32] = {};
			stduint pattern_count = 0;
			stduint unclassified_count = 0;
			for0(device_index, acpi_device_count) {
				const auto& device = acpi_devices[device_index];
				if (!device.sta_dynamic) continue;
				if (!device.has_sta_pattern) {
					++unclassified_count;
					continue;
				}
				stduint pattern_index = 0;
				while (pattern_index < pattern_count &&
					(patterns[pattern_index].first_opcode != device.sta_first_opcode ||
						patterns[pattern_index].value_opcode != device.sta_value_opcode ||
						patterns[pattern_index].method != device.sta_method)) {
					++pattern_index;
				}
				if (pattern_index >= pattern_count) {
					if (pattern_count >= numsof(patterns)) {
						++unclassified_count;
						continue;
					}
					auto& pattern = patterns[pattern_count++];
					pattern.first_opcode = device.sta_first_opcode;
					pattern.value_opcode = device.sta_value_opcode;
					pattern.method = device.sta_method;
					pattern.example = device.path;
					pattern.expression = device.sta_expression;
					pattern.expression_length = device.sta_expression_length;
				}
				++patterns[pattern_index].count;
			}
			for0(pattern_index, pattern_count) {
				const auto& pattern = patterns[pattern_index];
				char line[512] = {};
				append_text(line, sizeof(line), "[ACPI-STA-SUM] source=");
				append_text(line, sizeof(line), pattern.method ? "method" : "name");
				append_text(line, sizeof(line), " first=");
				append_hex_byte(line, sizeof(line), pattern.first_opcode);
				append_text(line, sizeof(line), " value=");
				append_hex_byte(line, sizeof(line), pattern.value_opcode);
				append_text(line, sizeof(line), " count=");
				append_decimal(line, sizeof(line), pattern.count);
				append_text(line, sizeof(line), " example=");
				append_text(line, sizeof(line), pattern.example);
				append_text(line, sizeof(line), " bytes=");
				const stduint byte_count = pattern.expression_length < 32u ?
					pattern.expression_length : 32u;
				for0(byte_index, byte_count) {
					if (byte_index) append_text(line, sizeof(line), " ");
					append_hex_byte(line, sizeof(line), pattern.expression[byte_index]);
				}
				if (byte_count < pattern.expression_length) append_text(line, sizeof(line), " ...");
				ploginfo("%s", line);
			}
			if (unclassified_count) {
				ploginfo("[ACPI-STA-SUM] source=unclassified count=%u",
					unclassified_count);
			}
		}

	}

	void discover_x86_acpi_devices() {
		acpi_device_count = 0;
		acpi_serial_resource_count = 0;
		acpi_conditional_block_count = 0;
		acpi_parent_absent_count = 0;
		acpi_parent_unknown_count = 0;
		acpi_pci_absent_count = 0;
		acpi_malformed_count = 0;
		for0(index, stduint(AcpiMalformedKind::Count)) {
			acpi_malformed_kind_counts[index] = 0;
		}
		acpi_malformed_record_count = 0;
		acpi_device_truncated = false;
		acpi_serial_resource_truncated = false;
		for0(index, aml_evaluation_summary_count) aml_evaluation_summaries[index] = {};
		aml_evaluation_summary_count = 0;
		for0(index, aml_region_summary_count) aml_region_summaries[index] = {};
		aml_region_summary_count = 0;
		for0(index, stduint(AmlPciReadStatus::Count)) {
			aml_pci_read_counts[index] = 0;
			aml_pci_read_examples[index][0] = 0;
		}
		collect_x86_aml_tables();
		uni::AMLNamespace aml_namespace(aml_namespace_objects, numsof(aml_namespace_objects));
		load_x86_aml_namespace(aml_namespace);
		const uni::AMLExecutor::Workspace aml_workspace = { aml_execution_frames, numsof(aml_execution_frames), aml_object_values, numsof(aml_object_values), { nullptr, read_aml_pci_region } };
		const uni::AMLExecutor::Limits aml_limits = { 4096, 256 };
		uni::AMLExecutor aml_executor(aml_namespace, aml_workspace, aml_limits);
		acpi_conditional_block_count = aml_namespace.ConditionalCount();
		collect_x86_acpi_devices(aml_namespace, aml_executor);
		stduint i2c_count = 0;
		stduint spi_count = 0;
		stduint uart_count = 0;
		stduint other_serial_count = 0;
		stduint present_count = 0;
		stduint absent_count = 0;
		stduint unknown_count = 0;
		for0(index, acpi_device_count) {
			const auto& device = acpi_devices[index];
			if (device.conditional || device.sta_dynamic) ++unknown_count;
			else if (device.has_sta && !(device.sta & 1u)) ++absent_count;
			else ++present_count;
		}
		for0(index, acpi_serial_resource_count) {
			switch (AcpiSerialBusType(acpi_serial_resources[index].bus_type)) {
			case AcpiSerialBusType::I2C: ++i2c_count; break;
			case AcpiSerialBusType::SPI: ++spi_count; break;
			case AcpiSerialBusType::UART: ++uart_count; break;
			default: ++other_serial_count; break;
			}
		}
		ploginfo("[ACPI-ENUM] mode=namespace bits=%u tables=%u objects=%u devices=%u present=%u absent=%u unknown=%u serial=%u i2c=%u spi=%u uart=%u other=%u conditional=%u malformed=%u truncated=%u",
			stduint(sizeof(stduint) * 8), aml_table_count,
			aml_namespace.Count(), acpi_device_count, present_count, absent_count, unknown_count,
			acpi_serial_resource_count,
			i2c_count, spi_count, uart_count, other_serial_count,
			acpi_conditional_block_count, acpi_malformed_count,
			stduint(acpi_device_truncated || acpi_serial_resource_truncated || aml_namespace.Truncated()));
		if (acpi_parent_absent_count || acpi_parent_unknown_count || acpi_pci_absent_count) ploginfo("[ACPI-PARENT-SUM] absent=%u unknown=%u pci-absent=%u", acpi_parent_absent_count, acpi_parent_unknown_count, acpi_pci_absent_count);
		log_acpi_malformed_records();
		log_aml_evaluation_summaries();
		log_acpi_sta_patterns();
		if (LogAllAcpiDevices) {
			for0(index, acpi_device_count) log_acpi_device(acpi_devices[index]);
		}
		for0(index, acpi_serial_resource_count) {
			const uint16 device_index = acpi_serial_resources[index].device_index;
			if (!LogAllAcpiDevices && device_index < acpi_device_count) {
				log_acpi_device(acpi_devices[device_index]);
			}
			log_acpi_serial_resource(acpi_serial_resources[index]);
		}
	}

}

#endif
