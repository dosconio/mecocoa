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
	DeviceNode* find_pci_bus_node(uint16 segment, uint8 bus);
	DeviceNode* get_bus_root(DeviceBusType bus_type);
	DeviceNode* find_named_child(DeviceNode* parent, DeviceNodeType node_type, const char* name);
	DeviceNode* append_plain_device(DeviceNode* parent, DeviceNodeType node_type, DeviceBusType bus_type, const char* name);

	namespace {

		struct AmlTableView {
			const uint8* aml;
			stduint length;
			char signature[5];
		};

		constexpr stduint AcpiDeviceCapacity = 512;
		constexpr stduint AcpiSerialResourceCapacity = 512;
		constexpr stduint AcpiResourceCapacity = 1024;
		constexpr stduint AmlTableCapacity = 64;
		constexpr stduint AcpiPathCapacity = 160;
		constexpr stduint AcpiIdCapacity = 48;
		constexpr stduint AcpiUidCapacity = 32;
		constexpr stduint AcpiNameSegmentCapacity = 16;
		constexpr stduint AcpiMalformedRecordCapacity = 16;
		constexpr stduint AmlNamespaceObjectCapacity = 8192;
		constexpr stduint AmlExecutionFrameCapacity = 16;
		constexpr stduint AmlObjectValueCapacity = 512;
		constexpr stduint AmlDynamicObjectCapacity = 128;
		constexpr stduint AmlMutexStateCapacity = 64;
		constexpr stduint AmlBufferCapacity = 65536;
		constexpr stduint AmlEvaluationSummaryCapacity = 32;
		constexpr stduint AmlRegionSummaryCapacity = 16;
		constexpr uint16 InvalidAcpiDeviceIndex = 0xFFFFu;
		constexpr bool LogAllAcpiDevices = false;

		enum class AmlPciReadStatus : uint8 {
			Success,
			OwnerUnresolved,
			InvalidAccess,
			ConfigUnavailable,
			Count,
		};

		enum class AmlPciWriteStatus : uint8 {
			Success,
			OwnerUnresolved,
			InvalidAccess,
			ConfigUnavailable,
			Count,
		};

		enum class AmlMemoryReadStatus : uint8 {
			Success,
			InvalidAccess,
			MappingUnavailable,
			Count,
		};

		enum class AmlMemoryWriteStatus : uint8 {
			Success,
			InvalidAccess,
			MappingUnavailable,
			Count,
		};

		enum class AmlIoReadStatus : uint8 {
			Success,
			InvalidAccess,
			Count,
		};

		enum class AmlIoWriteStatus : uint8 {
			Success,
			InvalidAccess,
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

		enum class AcpiControllerLinkStatus : uint8 {
			Pci,
			AcpiOnly,
			SourceMissing,
			MalformedSource,
			NamespaceMissing,
			PciUnresolved,
			PciNodeMissing,
			Count,
		};

		enum class AcpiResourceType : uint8 {
			Irq,
			GpioInterrupt,
			GpioIo,
			Memory,
			IoPort,
			Count,
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
			stduint namespace_object;
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
			DeviceNode* pci_controller;
			uint32 speed;
			uint16 address;
			uint16 receive_fifo;
			uint16 transmit_fifo;
			uint16 type_flags;
			uint16 device_index;
			uint16 controller_device_index;
			uint8 bus_type;
			uint8 controller_status;
			uint8 general_flags;
			uint8 data_bits;
			uint8 clock_phase;
			uint8 clock_polarity;
			uint8 parity;
			uint8 lines_enabled;
		};

		struct AcpiResourceRecord {
			char source[AcpiPathCapacity];
			uint64 minimum;
			uint64 maximum;
			uint64 length;
			uint64 granularity;
			uint64 translation;
			uint32 value;
			uint32 flags;
			uint16 device_index;
			uint8 width;
			AcpiResourceType type;
		};

		struct AcpiMalformedRecord {
			const uint8* address;
			AcpiMalformedKind kind;
		};

		struct AmlEvaluationSummary {
			char object[5];
			char example[AcpiPathCapacity];
			char scope[AcpiPathCapacity];
			char target[AcpiPathCapacity];
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
		DeviceNode* acpi_controller_nodes[AcpiDeviceCapacity] = {};
		DeviceNode* acpi_endpoint_nodes[AcpiDeviceCapacity] = {};
		AcpiResourceRecord* acpi_resources = nullptr;
		stduint acpi_device_count = 0;
		stduint acpi_serial_resource_count = 0;
		stduint acpi_resource_count = 0;
		stduint acpi_conditional_block_count = 0;
		stduint acpi_parent_absent_count = 0;
		stduint acpi_parent_unknown_count = 0;
		stduint acpi_pci_absent_count = 0;
		stduint acpi_malformed_count = 0;
		stduint acpi_malformed_kind_counts[stduint(AcpiMalformedKind::Count)] = {};
		stduint acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::Count)] = {};
		AcpiMalformedRecord acpi_malformed_records[AcpiMalformedRecordCapacity] = {};
		stduint acpi_malformed_record_count = 0;
		bool acpi_device_truncated = false;
		bool acpi_serial_resource_truncated = false;
		bool acpi_resource_truncated = false;
		AmlTableView aml_tables[AmlTableCapacity] = {};
		stduint aml_table_count = 0;
		uni::AMLNamespace::Object aml_namespace_objects[AmlNamespaceObjectCapacity] = {};
		uni::AMLExecutor::Frame aml_execution_frames[AmlExecutionFrameCapacity] = {};
		uni::AMLExecutor::ObjectValue aml_object_values[AmlObjectValueCapacity] = {};
		uni::AMLNamespace::Object aml_dynamic_objects[AmlDynamicObjectCapacity] = {};
		uni::AMLExecutor::MutexState aml_mutex_states[AmlMutexStateCapacity] = {};
		AmlEvaluationSummary aml_evaluation_summaries[AmlEvaluationSummaryCapacity] = {};
		stduint aml_evaluation_summary_count = 0;
		AmlRegionSummary aml_region_summaries[AmlRegionSummaryCapacity] = {};
		stduint aml_region_summary_count = 0;
		stduint aml_pci_read_counts[stduint(AmlPciReadStatus::Count)] = {};
		char aml_pci_read_examples[stduint(AmlPciReadStatus::Count)][AcpiPathCapacity] = {};
		stduint aml_pci_write_counts[stduint(AmlPciWriteStatus::Count)] = {};
		char aml_pci_write_examples[stduint(AmlPciWriteStatus::Count)][AcpiPathCapacity] = {};
		stduint aml_memory_read_counts[stduint(AmlMemoryReadStatus::Count)] = {};
		char aml_memory_read_examples[stduint(AmlMemoryReadStatus::Count)][AcpiPathCapacity] = {};
		stduint aml_memory_write_counts[stduint(AmlMemoryWriteStatus::Count)] = {};
		char aml_memory_write_examples[stduint(AmlMemoryWriteStatus::Count)][AcpiPathCapacity] = {};
		stduint aml_io_read_counts[stduint(AmlIoReadStatus::Count)] = {};
		char aml_io_read_examples[stduint(AmlIoReadStatus::Count)][AcpiPathCapacity] = {};
		stduint aml_io_write_counts[stduint(AmlIoWriteStatus::Count)] = {};
		char aml_io_write_examples[stduint(AmlIoWriteStatus::Count)][AcpiPathCapacity] = {};

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

		bool parse_acpi_resource_source(const char* source, uni::AMLBytecode::Name& name) {
			name = {};
			if (!source || !source[0] || equal_text(source, "<none>")) return false;
			if (*source == '\\') {
				name.rooted = true;
				++source;
			}
			while (*source == '^') {
				if (name.parent_count == 0xFFu) return false;
				++name.parent_count;
				++source;
			}
			while (*source) {
				if (name.segment_count >= uni::AMLBytecode::NameSegmentCapacity) return false;
				char segment[5] = { '_', '_', '_', '_', 0 };
				stduint length = 0;
				while (*source && *source != '.') {
					if (length >= 4 || !is_aml_name_char(uint8(*source))) return false;
					segment[length++] = *source++;
				}
				if (!length || !is_aml_name_lead(uint8(segment[0]))) return false;
				for0(index, 5) name.segments[name.segment_count][index] = segment[index];
				++name.segment_count;
				if (!*source) break;
				++source;
				if (!*source) return false;
			}
			return name.segment_count != 0;
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

		int find_or_add_acpi_device(const char* path, bool conditional, stduint namespace_object) {
			const int existing = find_acpi_device(path);
			if (existing >= 0) {
				acpi_devices[existing].conditional |= conditional;
				acpi_devices[existing].namespace_object = namespace_object;
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
			device.namespace_object = namespace_object;
			return int(acpi_device_count++);
		}

		uint16 read_le16(const uint8* data) {
			return uint16(data[0]) | uint16(data[1]) << 8;
		}

		uint32 read_le32(const uint8* data) {
			return uint32(data[0]) | uint32(data[1]) << 8 |
				uint32(data[2]) << 16 | uint32(data[3]) << 24;
		}

		uint64 read_le64(const uint8* data) {
			return uint64(read_le32(data)) | uint64(read_le32(data + 4)) << 32;
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

		bool aml_scope_declares_pci_address(const uni::AMLNamespace& aml_namespace, stduint scope) {
			const char adr_name[5] = { '_', 'A', 'D', 'R', 0 };
			for0(depth, uni::AMLNamespace::MaximumDepth) {
				const auto* object = aml_namespace.At(scope);
				if (!object) break;
				if (object->type == uni::AMLNamespace::ObjectType::Device) {
					if (aml_device_is_pci_root(aml_namespace, scope)) return false;
					if (aml_namespace.FindChild(scope, adr_name) != uni::AMLNamespace::InvalidIndex) return true;
				}
				if (object->parent == uni::AMLNamespace::InvalidIndex || object->parent == scope) break;
				scope = object->parent;
			}
			return false;
		}

		DeviceNode* find_pci_device_node(const AmlPciLocation& location) {
			auto* bus_node = find_pci_bus_node(location.segment, location.bus);
			if (!bus_node) return nullptr;
			for (auto* node = reinterpret_cast<DeviceNode*>(bus_node->link.subf); node; node = reinterpret_cast<DeviceNode*>(node->link.next)) {
				if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) continue;
				if (node->fields.pci_segment == location.segment && node->fields.pci_bus == location.bus && node->fields.pci_device == location.device && node->fields.pci_function == location.function) return node;
			}
			return nullptr;
		}

		bool resolve_aml_pci_region_owner(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, AmlPciLocation& location) {
			const auto* region = aml_namespace.At(region_object);
			if (region && region->type == uni::AMLNamespace::ObjectType::OperationRegion) region_scope = region->parent;
			return resolve_aml_pci_scope(aml_namespace, region_scope, location);
		}

		bool aml_pci_device_presence(const uni::AMLNamespace& aml_namespace, stduint device_object, bool& present) {
			AmlPciLocation location = {};
			if (!resolve_aml_pci_scope(aml_namespace, device_object, location) || !aml_pci_config_available(location, 0)) return false;
			present = uint16(uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, 0)) != 0xFFFFu;
			return true;
		}

		void record_aml_pci_read(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, AmlPciReadStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlPciReadStatus::Count)) return;
			if (!aml_pci_read_counts[index] && !aml_namespace.FormatPath(region_object, aml_pci_read_examples[index], sizeof(aml_pci_read_examples[index])) && !aml_namespace.FormatPath(region_scope, aml_pci_read_examples[index], sizeof(aml_pci_read_examples[index]))) copy_text(aml_pci_read_examples[index], sizeof(aml_pci_read_examples[index]), "<unknown>");
			++aml_pci_read_counts[index];
		}

		bool read_aml_pci_region(void* context, const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint8 space, uint64 address, uint8 width, uint64& value) {
			(void)context;
			if (space != 0x02) return false;
			AmlPciLocation location = {};
			if (!resolve_aml_pci_region_owner(aml_namespace, region_object, region_scope, location)) {
				record_aml_pci_read(aml_namespace, region_object, region_scope, AmlPciReadStatus::OwnerUnresolved);
				return false;
			}
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address >= 0x1000u || width / 8 > 0x1000u - address) {
				record_aml_pci_read(aml_namespace, region_object, region_scope, AmlPciReadStatus::InvalidAccess);
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
						record_aml_pci_read(aml_namespace, region_object, region_scope, AmlPciReadStatus::ConfigUnavailable);
						return false;
					}
					register_value = uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, register_address);
					cached_register = register_address;
				}
				value |= uint64(uint8(register_value >> ((byte_address & 3u) * 8))) << (index * 8);
			}
			record_aml_pci_read(aml_namespace, region_object, region_scope, AmlPciReadStatus::Success);
			return true;
		}

		void record_aml_pci_write(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, AmlPciWriteStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlPciWriteStatus::Count)) return;
			if (!aml_pci_write_counts[index] && !aml_namespace.FormatPath(region_object, aml_pci_write_examples[index], sizeof(aml_pci_write_examples[index])) && !aml_namespace.FormatPath(region_scope, aml_pci_write_examples[index], sizeof(aml_pci_write_examples[index]))) copy_text(aml_pci_write_examples[index], sizeof(aml_pci_write_examples[index]), "<unknown>");
			++aml_pci_write_counts[index];
		}

		bool write_aml_pci_region(void* context, const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint8 space, uint64 address, uint8 width, uint64 value) {
			(void)context;
			if (space != 0x02) return false;
			AmlPciLocation location = {};
			if (!resolve_aml_pci_region_owner(aml_namespace, region_object, region_scope, location)) {
				record_aml_pci_write(aml_namespace, region_object, region_scope, AmlPciWriteStatus::OwnerUnresolved);
				return false;
			}
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address >= 0x1000u || width / 8 > 0x1000u - address) {
				record_aml_pci_write(aml_namespace, region_object, region_scope, AmlPciWriteStatus::InvalidAccess);
				return false;
			}
			const uint8 byte_count = width / 8;
			uint16 previous_register = 0xFFFFu;
			for (uint8 index = 0; index < byte_count; ++index) {
				const uint16 register_address = uint16(address + index) & 0xFFFCu;
				if (register_address == previous_register) continue;
				if (!aml_pci_config_available(location, register_address)) {
					record_aml_pci_write(aml_namespace, region_object, region_scope, AmlPciWriteStatus::ConfigUnavailable);
					return false;
				}
				previous_register = register_address;
			}
			uint16 cached_register = 0xFFFFu;
			uint32 register_value = 0;
			bool dirty = false;
			for (uint8 index = 0; index < byte_count; ++index) {
				const uint16 byte_address = uint16(address + index);
				const uint16 register_address = byte_address & 0xFFFCu;
				if (register_address != cached_register) {
					if (dirty) uni::PCI::write_config_register(location.segment, location.bus, location.device, location.function, cached_register, register_value);
					register_value = uni::PCI::read_config_register(location.segment, location.bus, location.device, location.function, register_address);
					cached_register = register_address;
					dirty = false;
				}
				const uint8 shift = uint8((byte_address & 3u) * 8);
				register_value = (register_value & ~(uint32(0xFFu) << shift)) | (uint32(uint8(value >> (index * 8))) << shift);
				dirty = true;
			}
			if (dirty) uni::PCI::write_config_register(location.segment, location.bus, location.device, location.function, cached_register, register_value);
			record_aml_pci_write(aml_namespace, region_object, region_scope, AmlPciWriteStatus::Success);
			return true;
		}

		void record_aml_memory_read(const uni::AMLNamespace& aml_namespace, stduint region_object, AmlMemoryReadStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlMemoryReadStatus::Count)) return;
			if (!aml_memory_read_counts[index] && !aml_namespace.FormatPath(region_object, aml_memory_read_examples[index], sizeof(aml_memory_read_examples[index]))) copy_text(aml_memory_read_examples[index], sizeof(aml_memory_read_examples[index]), "<unknown>");
			++aml_memory_read_counts[index];
		}

		bool ensure_aml_memory_page(stduint page_address) {
			#if CONFIG_ENABLE_MMU
			pageint page = page_address;
			PageEntry* entry = kernel_paging.getEntry(page);
			if (_IMM(entry) == ~_IMM0) {
				if (page_address > ~stduint(0) - PAGE_SIZE || !Memory::MapPages(kernel_paging, page_address, page_address, PAGE_SIZE, PAGESIZE_4KB, PGPROP_present | PGTYPE_device | PGOPTS_omit_warnings)) return false;
			}
			return _IMM(kernel_paging[page_address]) == page_address;
			#else
			(void)page_address;
			return true;
			#endif
		}

		bool read_aml_memory_region(const uni::AMLNamespace& aml_namespace, stduint region_object, uint64 address, uint8 width, uint64& value) {
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address > uint64(~stduint(0)) || width / 8 > uint64(~stduint(0)) - address || (address & (width / 8 - 1))) {
				record_aml_memory_read(aml_namespace, region_object, AmlMemoryReadStatus::InvalidAccess);
				return false;
			}
			const stduint page_address = stduint(address) & ~stduint(PAGE_SIZE - 1);
			if (!ensure_aml_memory_page(page_address)) {
				record_aml_memory_read(aml_namespace, region_object, AmlMemoryReadStatus::MappingUnavailable);
				return false;
			}
			const stduint mapped_address = stduint(address);
			switch (width) {
			case 8: value = *reinterpret_cast<const volatile uint8*>(mapped_address); break;
			case 16: value = *reinterpret_cast<const volatile uint16*>(mapped_address); break;
			case 32: value = *reinterpret_cast<const volatile uint32*>(mapped_address); break;
			case 64:
				#if defined(_ARC_x86)
				value = uint64(*reinterpret_cast<const volatile uint32*>(mapped_address)) | (uint64(*reinterpret_cast<const volatile uint32*>(mapped_address + sizeof(uint32))) << 32);
				#else
				value = *reinterpret_cast<const volatile uint64*>(mapped_address);
				#endif
				break;
			default: return false;
			}
			record_aml_memory_read(aml_namespace, region_object, AmlMemoryReadStatus::Success);
			return true;
		}

		void record_aml_memory_write(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, AmlMemoryWriteStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlMemoryWriteStatus::Count)) return;
			if (!aml_memory_write_counts[index] && !aml_namespace.FormatPath(region_object, aml_memory_write_examples[index], sizeof(aml_memory_write_examples[index])) && !aml_namespace.FormatPath(region_scope, aml_memory_write_examples[index], sizeof(aml_memory_write_examples[index]))) copy_text(aml_memory_write_examples[index], sizeof(aml_memory_write_examples[index]), "<unknown>");
			++aml_memory_write_counts[index];
		}

		bool write_aml_memory_region(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint64 address, uint8 width, uint64 value) {
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address > uint64(~stduint(0)) || width / 8 > uint64(~stduint(0)) - address || (address & (width / 8 - 1))) {
				record_aml_memory_write(aml_namespace, region_object, region_scope, AmlMemoryWriteStatus::InvalidAccess);
				return false;
			}
			const stduint page_address = stduint(address) & ~stduint(PAGE_SIZE - 1);
			if (!ensure_aml_memory_page(page_address)) {
				record_aml_memory_write(aml_namespace, region_object, region_scope, AmlMemoryWriteStatus::MappingUnavailable);
				return false;
			}
			const stduint mapped_address = stduint(address);
			switch (width) {
			case 8: *reinterpret_cast<volatile uint8*>(mapped_address) = uint8(value); break;
			case 16: *reinterpret_cast<volatile uint16*>(mapped_address) = uint16(value); break;
			case 32: *reinterpret_cast<volatile uint32*>(mapped_address) = uint32(value); break;
			case 64:
				#if defined(_ARC_x86)
				*reinterpret_cast<volatile uint32*>(mapped_address) = uint32(value);
				*reinterpret_cast<volatile uint32*>(mapped_address + sizeof(uint32)) = uint32(value >> 32);
				#else
				*reinterpret_cast<volatile uint64*>(mapped_address) = value;
				#endif
				break;
			default: return false;
			}
			record_aml_memory_write(aml_namespace, region_object, region_scope, AmlMemoryWriteStatus::Success);
			return true;
		}

		void record_aml_io_read(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, AmlIoReadStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlIoReadStatus::Count)) return;
			if (!aml_io_read_counts[index] && !aml_namespace.FormatPath(region_object, aml_io_read_examples[index], sizeof(aml_io_read_examples[index])) && !aml_namespace.FormatPath(region_scope, aml_io_read_examples[index], sizeof(aml_io_read_examples[index]))) copy_text(aml_io_read_examples[index], sizeof(aml_io_read_examples[index]), "<unknown>");
			++aml_io_read_counts[index];
		}

		bool read_aml_io_region(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint64 address, uint8 width, uint64& value) {
			const uint8 byte_count = width / 8;
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address > 0xFFFFu || byte_count > 0x10000u - address) {
				record_aml_io_read(aml_namespace, region_object, region_scope, AmlIoReadStatus::InvalidAccess);
				return false;
			}
			const uint16 port = uint16(address);
			switch (width) {
			case 8: value = IN_b(port); break;
			case 16: value = IN_w(port); break;
			case 32: value = IN_d(port); break;
			case 64: value = uint64(IN_d(port)) | (uint64(IN_d(uint16(port + sizeof(uint32)))) << 32); break;
			default: return false;
			}
			record_aml_io_read(aml_namespace, region_object, region_scope, AmlIoReadStatus::Success);
			return true;
		}

		void record_aml_io_write(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, AmlIoWriteStatus status) {
			const stduint index = stduint(status);
			if (index >= stduint(AmlIoWriteStatus::Count)) return;
			if (!aml_io_write_counts[index] && !aml_namespace.FormatPath(region_object, aml_io_write_examples[index], sizeof(aml_io_write_examples[index])) && !aml_namespace.FormatPath(region_scope, aml_io_write_examples[index], sizeof(aml_io_write_examples[index]))) copy_text(aml_io_write_examples[index], sizeof(aml_io_write_examples[index]), "<unknown>");
			++aml_io_write_counts[index];
		}

		bool write_aml_io_region(const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint64 address, uint8 width, uint64 value) {
			const uint8 byte_count = width / 8;
			if ((width != 8 && width != 16 && width != 32 && width != 64) || address > 0xFFFFu || byte_count > 0x10000u - address) {
				record_aml_io_write(aml_namespace, region_object, region_scope, AmlIoWriteStatus::InvalidAccess);
				return false;
			}
			const uint16 port = uint16(address);
			switch (width) {
			case 8: OUT_b(port, uint8(value)); break;
			case 16: OUT_w(port, uint16(value)); break;
			case 32: OUT_d(port, uint32(value)); break;
			case 64:
				OUT_d(port, uint32(value));
				OUT_d(uint16(port + sizeof(uint32)), uint32(value >> 32));
				break;
			default: return false;
			}
			record_aml_io_write(aml_namespace, region_object, region_scope, AmlIoWriteStatus::Success);
			return true;
		}

		bool read_aml_region(void* context, const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint8 space, uint64 address, uint8 width, uint64& value) {
			if (space == 0x00) return read_aml_memory_region(aml_namespace, region_object, address, width, value);
			if (space == 0x01) return read_aml_io_region(aml_namespace, region_object, region_scope, address, width, value);
			if (space == 0x02) return read_aml_pci_region(context, aml_namespace, region_object, region_scope, space, address, width, value);
			return false;
		}

		bool write_aml_region(void* context, const uni::AMLNamespace& aml_namespace, stduint region_object, stduint region_scope, uint8 space, uint64 address, uint8 width, uint64 value) {
			if (space == 0x00) return write_aml_memory_region(aml_namespace, region_object, region_scope, address, width, value);
			if (space == 0x01) return write_aml_io_region(aml_namespace, region_object, region_scope, address, width, value);
			if (space == 0x02) return write_aml_pci_region(context, aml_namespace, region_object, region_scope, space, address, width, value);
			return false;
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

		AcpiResourceRecord* append_acpi_resource(uint16 device_index, AcpiResourceType type) {
			if (!acpi_resources || acpi_resource_count >= AcpiResourceCapacity) {
				acpi_resource_truncated = true;
				return nullptr;
			}
			auto& resource = acpi_resources[acpi_resource_count++];
			resource = {};
			resource.device_index = device_index;
			resource.type = type;
			return &resource;
		}

		void append_irq_resource(uint16 device_index, uint32 interrupt, uint32 flags) {
			auto* resource = append_acpi_resource(device_index, AcpiResourceType::Irq);
			if (!resource) return;
			resource->value = interrupt;
			resource->flags = flags;
		}

		void append_range_resource(uint16 device_index, AcpiResourceType type, uint8 width, uint32 flags, uint64 minimum, uint64 maximum, uint64 length, uint64 granularity = 0, uint64 translation = 0) {
			auto* resource = append_acpi_resource(device_index, type);
			if (!resource) return;
			resource->minimum = minimum;
			resource->maximum = maximum;
			resource->length = length;
			resource->granularity = granularity;
			resource->translation = translation;
			resource->flags = flags;
			resource->width = width;
		}

		void append_address_resource(uint16 device_index, const uint8* descriptor, stduint descriptor_length, uint8 width) {
			stduint value_offset = 0;
			if (width == 16) value_offset = 6;
			else if (width == 32) value_offset = 6;
			else if (width == 64) value_offset = 6;
			else return;
			const stduint value_bytes = width / 8;
			if (descriptor_length < value_offset + value_bytes * 5) {
				report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				return;
			}
			const uint8 resource_type = descriptor[3];
			if (resource_type > 1) return;
			uint64 granularity = 0;
			uint64 minimum = 0;
			uint64 maximum = 0;
			uint64 translation = 0;
			uint64 length = 0;
			if (width == 16) {
				granularity = read_le16(descriptor + 6);
				minimum = read_le16(descriptor + 8);
				maximum = read_le16(descriptor + 10);
				translation = read_le16(descriptor + 12);
				length = read_le16(descriptor + 14);
			}
			else if (width == 32) {
				granularity = read_le32(descriptor + 6);
				minimum = read_le32(descriptor + 10);
				maximum = read_le32(descriptor + 14);
				translation = read_le32(descriptor + 18);
				length = read_le32(descriptor + 22);
			}
			else {
				granularity = read_le64(descriptor + 6);
				minimum = read_le64(descriptor + 14);
				maximum = read_le64(descriptor + 22);
				translation = read_le64(descriptor + 30);
				length = read_le64(descriptor + 38);
			}
			const uint32 flags = uint32(descriptor[4]) | uint32(descriptor[5]) << 8;
			append_range_resource(device_index, resource_type ? AcpiResourceType::IoPort : AcpiResourceType::Memory, width, flags, minimum, maximum, length, granularity, translation);
		}

		void append_extended_address_resource(uint16 device_index, const uint8* descriptor, stduint descriptor_length) {
			if (descriptor_length < 56) {
				report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				return;
			}
			const uint8 resource_type = descriptor[3];
			if (resource_type > 1) return;
			const uint32 flags = uint32(descriptor[4]) | uint32(descriptor[5]) << 8;
			append_range_resource(device_index, resource_type ? AcpiResourceType::IoPort : AcpiResourceType::Memory, 64, flags, read_le64(descriptor + 16), read_le64(descriptor + 24), read_le64(descriptor + 40), read_le64(descriptor + 8), read_le64(descriptor + 32));
		}

		void append_gpio_resources(uint16 device_index, const uint8* descriptor, stduint descriptor_length) {
			if (descriptor_length < 25) {
				report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				return;
			}
			const uint8 connection_type = descriptor[4];
			if (connection_type > 1) return;
			const uint16 pin_offset = read_le16(descriptor + 14);
			const uint16 source_offset = read_le16(descriptor + 17);
			const uint16 vendor_offset = read_le16(descriptor + 19);
			const stduint source_end = vendor_offset ? vendor_offset : descriptor_length;
			if (pin_offset < 23 || source_offset < pin_offset || source_offset > descriptor_length || source_end < source_offset || source_end > descriptor_length || ((source_offset - pin_offset) & 1u)) {
				report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				return;
			}
			const uint32 flags = uint32(read_le16(descriptor + 5)) | uint32(read_le16(descriptor + 7)) << 16;
			for (stduint pin = pin_offset; pin + 2 <= source_offset; pin += 2) {
				auto* resource = append_acpi_resource(device_index, connection_type ? AcpiResourceType::GpioIo : AcpiResourceType::GpioInterrupt);
				if (!resource) return;
				resource->value = read_le16(descriptor + pin);
				resource->flags = flags;
				resource->width = descriptor[9];
				copy_resource_source(resource->source, descriptor + source_offset, descriptor + source_end);
			}
		}

		void append_large_resource(uint16 device_index, const uint8* descriptor, stduint descriptor_length) {
			switch (descriptor[0] & 0x7Fu) {
			case 0x01:
				if (descriptor_length < 12) report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				else {
					const uint64 alignment = read_le16(descriptor + 8) ? uint64(read_le16(descriptor + 8)) << 8 : 0x10000;
					append_range_resource(device_index, AcpiResourceType::Memory, 24, descriptor[3], uint64(read_le16(descriptor + 4)) << 8, uint64(read_le16(descriptor + 6)) << 8, uint64(read_le16(descriptor + 10)) << 8, alignment);
				}
				break;
			case 0x05:
				if (descriptor_length < 20) report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				else append_range_resource(device_index, AcpiResourceType::Memory, 32, descriptor[3], read_le32(descriptor + 4), read_le32(descriptor + 8), read_le32(descriptor + 16), read_le32(descriptor + 12));
				break;
			case 0x06:
				if (descriptor_length < 12) report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				else {
					const uint64 minimum = read_le32(descriptor + 4);
					const uint64 length = read_le32(descriptor + 8);
					const uint64 maximum = length && minimum <= ~uint64(0) - (length - 1) ? minimum + length - 1 : minimum;
					append_range_resource(device_index, AcpiResourceType::Memory, 32, descriptor[3], minimum, maximum, length);
				}
				break;
			case 0x07: append_address_resource(device_index, descriptor, descriptor_length, 32); break;
			case 0x08: append_address_resource(device_index, descriptor, descriptor_length, 16); break;
			case 0x09:
				if (descriptor_length < 5 || descriptor_length < stduint(5u + descriptor[4] * 4u)) report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				else for0(index, descriptor[4]) append_irq_resource(device_index, read_le32(descriptor + 5 + index * 4), descriptor[3]);
				break;
			case 0x0A: append_address_resource(device_index, descriptor, descriptor_length, 64); break;
			case 0x0B: append_extended_address_resource(device_index, descriptor, descriptor_length); break;
			case 0x0C: append_gpio_resources(device_index, descriptor, descriptor_length); break;
			case 0x0E: append_serial_resource(device_index, descriptor, descriptor_length); break;
			default: break;
			}
		}

		void append_small_resource(uint16 device_index, const uint8* descriptor, stduint descriptor_length) {
			const uint8 item_name = (descriptor[0] >> 3) & 0x0Fu;
			if (item_name == 0x04) {
				if (descriptor_length < 3) {
					report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
					return;
				}
				const uint16 mask = read_le16(descriptor + 1);
				const uint8 flags = descriptor_length >= 4 ? descriptor[3] : 1;
				for0(interrupt, 16) if (mask & uint16(1u << interrupt)) append_irq_resource(device_index, uint32(interrupt), flags);
			}
			else if (item_name == 0x08) {
				if (descriptor_length < 8) report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				else append_range_resource(device_index, AcpiResourceType::IoPort, 16, descriptor[1], read_le16(descriptor + 2), read_le16(descriptor + 4), descriptor[7], descriptor[6]);
			}
			else if (item_name == 0x09) {
				if (descriptor_length < 4) report_acpi_malformed(AcpiMalformedKind::ResourceLength, descriptor);
				else {
					const uint64 minimum = read_le16(descriptor + 1);
					const uint64 length = descriptor[3];
					append_range_resource(device_index, AcpiResourceType::IoPort, 16, 0, minimum, length ? minimum + length - 1 : minimum, length);
				}
			}
		}

		void parse_resource_template(uint16 device_index, const uint8* data, stduint length) {
			if (!data) {
				if (length) report_acpi_malformed(AcpiMalformedKind::ResourceHeader, data);
				return;
			}
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
					append_large_resource(device_index, cursor, descriptor_length);
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
				append_small_resource(device_index, cursor, descriptor_length);
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
			if (path_is_child_name(object_path, device_path, "_CRS")) device.crs_dynamic = true;
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
			if (path_is_child_name(method_path, device_path, "_CRS")) device.crs_dynamic = true;
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
			case uni::AMLExecutor::Status::DynamicObjectCapacity: return "dynamic-object-capacity";
			case uni::AMLExecutor::Status::MutexCapacity: return "mutex-capacity";
			case uni::AMLExecutor::Status::BufferCapacity: return "buffer-capacity";
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

		const char* aml_pci_write_status_name(AmlPciWriteStatus status) {
			switch (status) {
			case AmlPciWriteStatus::Success: return "success";
			case AmlPciWriteStatus::OwnerUnresolved: return "owner-unresolved";
			case AmlPciWriteStatus::InvalidAccess: return "invalid-access";
			case AmlPciWriteStatus::ConfigUnavailable: return "config-unavailable";
			default: return "unknown";
			}
		}

		const char* aml_memory_read_status_name(AmlMemoryReadStatus status) {
			switch (status) {
			case AmlMemoryReadStatus::Success: return "success";
			case AmlMemoryReadStatus::InvalidAccess: return "invalid-access";
			case AmlMemoryReadStatus::MappingUnavailable: return "mapping-unavailable";
			default: return "unknown";
			}
		}

		const char* aml_memory_write_status_name(AmlMemoryWriteStatus status) {
			switch (status) {
			case AmlMemoryWriteStatus::Success: return "success";
			case AmlMemoryWriteStatus::InvalidAccess: return "invalid-access";
			case AmlMemoryWriteStatus::MappingUnavailable: return "mapping-unavailable";
			default: return "unknown";
			}
		}

		const char* aml_io_read_status_name(AmlIoReadStatus status) {
			switch (status) {
			case AmlIoReadStatus::Success: return "success";
			case AmlIoReadStatus::InvalidAccess: return "invalid-access";
			default: return "unknown";
			}
		}

		const char* aml_io_write_status_name(AmlIoWriteStatus status) {
			switch (status) {
			case AmlIoWriteStatus::Success: return "success";
			case AmlIoWriteStatus::InvalidAccess: return "invalid-access";
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

		const uint8* aml_table_end(const uint8* address) {
			if (!address) return nullptr;
			const stduint target = reinterpret_cast<stduint>(address);
			for0(index, aml_table_count) {
				if (!aml_tables[index].aml) continue;
				const stduint begin = reinterpret_cast<stduint>(aml_tables[index].aml);
				if (target < begin || target - begin >= aml_tables[index].length) continue;
				return aml_tables[index].aml + aml_tables[index].length;
			}
			return nullptr;
		}

		bool format_aml_name(const uint8* address, char* output, stduint capacity) {
			if (!output || !capacity) return false;
			output[0] = 0;
			const uint8* end = aml_table_end(address);
			if (!end) return false;
			const uint8* cursor = address;
			uni::AMLBytecode::Name name = {};
			if (!uni::AMLBytecode::DecodeName(cursor, end, name)) return false;
			if (name.rooted) append_text(output, capacity, "\\");
			for0(index, name.parent_count) append_text(output, capacity, "^");
			for0(index, name.segment_count) {
				if (index) append_text(output, capacity, ".");
				append_text(output, capacity, name.segments[index]);
			}
			return true;
		}

		void record_aml_evaluation(const uni::AMLNamespace& aml_namespace, const char* object_name, uni::AMLExecutor::Status status, const char* path, const uni::AMLExecutor::Error& error) {
			stduint index = 0;
			while (index < aml_evaluation_summary_count && (!equal_text(aml_evaluation_summaries[index].object, object_name) || aml_evaluation_summaries[index].status != status || aml_evaluation_summaries[index].opcode != error.opcode)) ++index;
			if (index >= aml_evaluation_summary_count) {
				if (aml_evaluation_summary_count >= numsof(aml_evaluation_summaries)) return;
				auto& summary = aml_evaluation_summaries[aml_evaluation_summary_count++];
				copy_text(summary.object, sizeof(summary.object), object_name);
				summary.status = status;
				summary.opcode = error.opcode;
				copy_text(summary.example, sizeof(summary.example), path);
				if (status == uni::AMLExecutor::Status::NotFound && error.address) {
					if (!aml_namespace.FormatPath(error.scope, summary.scope, sizeof(summary.scope))) copy_text(summary.scope, sizeof(summary.scope), "<unknown>");
					if (!format_aml_name(error.address, summary.target, sizeof(summary.target))) copy_text(summary.target, sizeof(summary.target), "<unknown>");
				}
			}
			++aml_evaluation_summaries[index].count;
			if (status == uni::AMLExecutor::Status::RegionUnavailable) record_aml_region(aml_namespace, path, error);
		}

		void log_aml_evaluation_summaries() {
			for0(index, aml_evaluation_summary_count) {
				const auto& summary = aml_evaluation_summaries[index];
				ploginfo("[ACPI-EVAL-SUM] object=%s status=%s count=%u example=%s opcode=%[16H]", summary.object, aml_execution_status_name(summary.status), summary.count, summary.example, summary.opcode);
				if (summary.status == uni::AMLExecutor::Status::NotFound && summary.target[0]) ploginfo("[ACPI-NAME-SUM] object=%s scope=%s target=%s count=%u example=%s", summary.object, summary.scope, summary.target, summary.count, summary.example);
			}
			for0(index, aml_region_summary_count) {
				const auto& summary = aml_region_summaries[index];
				ploginfo("[ACPI-REGION-SUM] space=%s id=%[16H] field=%[16H] flags=%[8H] count=%u example=%s", aml_region_space_name(summary.space), summary.space, summary.field_opcode, summary.field_flags, summary.count, summary.example);
			}
			for0(index, stduint(AmlPciReadStatus::Count)) {
				if (!aml_pci_read_counts[index]) continue;
				ploginfo("[ACPI-PCI-READ-SUM] status=%s count=%u example=%s", aml_pci_read_status_name(AmlPciReadStatus(index)), aml_pci_read_counts[index], aml_pci_read_examples[index]);
			}
			for0(index, stduint(AmlPciWriteStatus::Count)) {
				if (!aml_pci_write_counts[index]) continue;
				ploginfo("[ACPI-PCI-WRITE-SUM] status=%s count=%u example=%s", aml_pci_write_status_name(AmlPciWriteStatus(index)), aml_pci_write_counts[index], aml_pci_write_examples[index]);
			}
			for0(index, stduint(AmlMemoryReadStatus::Count)) {
				if (!aml_memory_read_counts[index]) continue;
				ploginfo("[ACPI-MEMORY-READ-SUM] status=%s count=%u example=%s", aml_memory_read_status_name(AmlMemoryReadStatus(index)), aml_memory_read_counts[index], aml_memory_read_examples[index]);
			}
			for0(index, stduint(AmlMemoryWriteStatus::Count)) {
				if (!aml_memory_write_counts[index]) continue;
				ploginfo("[ACPI-MEMORY-WRITE-SUM] status=%s count=%u example=%s", aml_memory_write_status_name(AmlMemoryWriteStatus(index)), aml_memory_write_counts[index], aml_memory_write_examples[index]);
			}
			for0(index, stduint(AmlIoReadStatus::Count)) {
				if (!aml_io_read_counts[index]) continue;
				ploginfo("[ACPI-IO-READ-SUM] status=%s count=%u example=%s", aml_io_read_status_name(AmlIoReadStatus(index)), aml_io_read_counts[index], aml_io_read_examples[index]);
			}
			for0(index, stduint(AmlIoWriteStatus::Count)) {
				if (!aml_io_write_counts[index]) continue;
				ploginfo("[ACPI-IO-WRITE-SUM] status=%s count=%u example=%s", aml_io_write_status_name(AmlIoWriteStatus(index)), aml_io_write_counts[index], aml_io_write_examples[index]);
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
						record_aml_evaluation(aml_namespace, "_STA", status, device_path, aml_executor.LastError());
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

		void evaluate_x86_acpi_device_resources(const uni::AMLNamespace& aml_namespace, uni::AMLExecutor& aml_executor) {
			const char crs_name[5] = { '_', 'C', 'R', 'S', 0 };
			for (stduint object_index = 1; object_index < aml_namespace.Count(); ++object_index) {
				const auto* object = aml_namespace.At(object_index);
				if (!object || object->type != uni::AMLNamespace::ObjectType::Device) continue;
				char device_path[AcpiPathCapacity] = {};
				if (!aml_namespace.FormatPath(object_index, device_path, sizeof(device_path))) continue;
				const int device_index = find_acpi_device(device_path);
				if (device_index < 0) continue;
				auto& device = acpi_devices[device_index];
				if (!device.crs_dynamic || device.conditional || device.sta_dynamic || (device.has_sta && !(device.sta & 1u))) continue;
				const stduint crs_index = aml_namespace.FindChild(object_index, crs_name);
				if (crs_index == uni::AMLNamespace::InvalidIndex) continue;
				uni::AMLExecutor::Value value = {};
				auto status = aml_executor.Evaluate(crs_index, value);
				uni::AMLExecutor::Error error = aml_executor.LastError();
				if (status == uni::AMLExecutor::Status::Success && (value.type != uni::AMLExecutor::ValueType::Buffer || (value.length && !value.data))) {
					status = uni::AMLExecutor::Status::TypeMismatch;
					error = {};
					error.object = crs_index;
					error.status = status;
				}
				record_aml_evaluation(aml_namespace, "_CRS", status, device_path, error);
				if (status != uni::AMLExecutor::Status::Success) continue;
				parse_resource_template(uint16(device_index), value.data, value.length);
				device.crs_dynamic = false;
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
				find_or_add_acpi_device(path, object->flags & uni::AMLNamespace::ObjectFlagConditional, object_index);
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
					if (path_is_child_name(object_path, device_path, "_STA")) acpi_devices[device_index].sta_dynamic = true;
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
			evaluate_x86_acpi_device_resources(aml_namespace, aml_executor);
		}

		void resolve_x86_acpi_serial_controllers(const uni::AMLNamespace& aml_namespace) {
			for0(index, stduint(AcpiControllerLinkStatus::Count)) acpi_controller_link_counts[index] = 0;
			for0(resource_index, acpi_serial_resource_count) {
				auto& resource = acpi_serial_resources[resource_index];
				resource.controller_device_index = InvalidAcpiDeviceIndex;
				resource.pci_controller = nullptr;
				AcpiControllerLinkStatus status = AcpiControllerLinkStatus::SourceMissing;
				if (resource.controller[0] && !equal_text(resource.controller, "<none>")) {
					uni::AMLBytecode::Name source = {};
					if (!parse_acpi_resource_source(resource.controller, source)) status = AcpiControllerLinkStatus::MalformedSource;
					else if (resource.device_index >= acpi_device_count) status = AcpiControllerLinkStatus::NamespaceMissing;
					else {
						stduint controller_object = aml_namespace.Find(acpi_devices[resource.device_index].namespace_object, source);
						for0(depth, uni::AMLNamespace::MaximumDepth) {
							const auto* object = aml_namespace.At(controller_object);
							if (!object || object->type != uni::AMLNamespace::ObjectType::Alias || object->reference == uni::AMLNamespace::InvalidIndex) break;
							controller_object = object->reference;
						}
						const auto* controller = aml_namespace.At(controller_object);
						if (!controller || controller->type != uni::AMLNamespace::ObjectType::Device) status = AcpiControllerLinkStatus::NamespaceMissing;
						else {
							char controller_path[AcpiPathCapacity] = {};
							const int controller_device_index = aml_namespace.FormatPath(controller_object, controller_path, sizeof(controller_path)) ? find_acpi_device(controller_path) : -1;
							if (controller_device_index < 0) status = AcpiControllerLinkStatus::NamespaceMissing;
							else {
								resource.controller_device_index = uint16(controller_device_index);
								if (!aml_scope_declares_pci_address(aml_namespace, controller_object)) status = AcpiControllerLinkStatus::AcpiOnly;
								else {
									AmlPciLocation location = {};
									if (!resolve_aml_pci_scope(aml_namespace, controller_object, location)) status = AcpiControllerLinkStatus::PciUnresolved;
									else if (!(resource.pci_controller = find_pci_device_node(location))) status = AcpiControllerLinkStatus::PciNodeMissing;
									else status = AcpiControllerLinkStatus::Pci;
								}
							}
						}
					}
				}
				resource.controller_status = uint8(status);
				++acpi_controller_link_counts[stduint(status)];
			}
		}

		void log_acpi_controller_links() {
			if (!acpi_serial_resource_count) return;
			ploginfo("[ACPI-CONTROLLER-SUM] pci=%u acpi=%u source-missing=%u malformed=%u namespace-missing=%u pci-unresolved=%u pci-node-missing=%u",
				acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::Pci)], acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::AcpiOnly)], acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::SourceMissing)], acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::MalformedSource)], acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::NamespaceMissing)], acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::PciUnresolved)], acpi_controller_link_counts[stduint(AcpiControllerLinkStatus::PciNodeMissing)]);
		}

		bool acpi_device_is_present(uint16 device_index) {
			if (device_index >= acpi_device_count) return false;
			const auto& device = acpi_devices[device_index];
			return !device.conditional && !device.sta_dynamic && (!device.has_sta || (device.sta & 1u));
		}

		void release_acpi_info(DeviceAcpiInfo* info) {
			if (!info) return;
			delete[] info->serial_resources;
			delete[] info->gpio_resources;
			delete[] info->irq_resources;
			delete[] info->range_resources;
			delete info;
		}

		DeviceAcpiInfo* create_acpi_info(uint16 device_index) {
			if (device_index >= acpi_device_count) return nullptr;
			const auto& device = acpi_devices[device_index];
			auto* info = new DeviceAcpiInfo{};
			if (!info) return nullptr;
			copy_text(info->path, sizeof(info->path), device.path);
			copy_text(info->hardware_id, sizeof(info->hardware_id), device.hid);
			copy_text(info->compatible_ids, sizeof(info->compatible_ids), device.cid);
			copy_text(info->unique_id, sizeof(info->unique_id), device.uid);
			info->status = device.has_sta ? device.sta : 0x0Fu;
			info->address = device.adr;
			if (device.has_hid) info->flags |= DeviceAcpiInfoFlag_HasHardwareId;
			if (device.has_cid) info->flags |= DeviceAcpiInfoFlag_HasCompatibleId;
			if (device.has_uid) info->flags |= DeviceAcpiInfoFlag_HasUniqueId;
			if (device.has_sta) info->flags |= DeviceAcpiInfoFlag_HasStatus;
			if (device.has_adr) info->flags |= DeviceAcpiInfoFlag_HasAddress;
			if (device.conditional) info->flags |= DeviceAcpiInfoFlag_Conditional;
			for0(index, acpi_serial_resource_count) {
				if (acpi_serial_resources[index].device_index == device_index) ++info->serial_resource_count;
			}
			for0(index, acpi_resource_count) {
				const auto& resource = acpi_resources[index];
				if (resource.device_index != device_index) continue;
				switch (resource.type) {
				case AcpiResourceType::Irq: ++info->irq_resource_count; break;
				case AcpiResourceType::GpioInterrupt:
				case AcpiResourceType::GpioIo: ++info->gpio_resource_count; break;
				case AcpiResourceType::Memory:
				case AcpiResourceType::IoPort: ++info->range_resource_count; break;
				default: break;
				}
			}
			if (info->serial_resource_count) info->serial_resources = new DeviceAcpiSerialResource[info->serial_resource_count]{};
			if (info->gpio_resource_count) info->gpio_resources = new DeviceAcpiGpioResource[info->gpio_resource_count]{};
			if (info->irq_resource_count) info->irq_resources = new DeviceAcpiIrqResource[info->irq_resource_count]{};
			if (info->range_resource_count) info->range_resources = new DeviceAcpiRangeResource[info->range_resource_count]{};
			if ((info->serial_resource_count && !info->serial_resources) || (info->gpio_resource_count && !info->gpio_resources) || (info->irq_resource_count && !info->irq_resources) || (info->range_resource_count && !info->range_resources)) {
				release_acpi_info(info);
				return nullptr;
			}
			uint16 serial_index = 0;
			uint16 gpio_index = 0;
			uint16 irq_index = 0;
			uint16 range_index = 0;
			for0(index, acpi_serial_resource_count) {
				const auto& source = acpi_serial_resources[index];
				if (source.device_index != device_index) continue;
				auto& target = info->serial_resources[serial_index++];
				copy_text(target.controller_path, sizeof(target.controller_path), source.controller);
				target.speed = source.speed;
				target.address = source.address;
				target.receive_fifo = source.receive_fifo;
				target.transmit_fifo = source.transmit_fifo;
				target.type_flags = source.type_flags;
				target.bus_type = DeviceAcpiSerialBusType(source.bus_type);
				target.general_flags = source.general_flags;
				target.data_bits = source.data_bits;
				target.clock_phase = source.clock_phase;
				target.clock_polarity = source.clock_polarity;
				target.parity = source.parity;
				target.lines_enabled = source.lines_enabled;
			}
			for0(index, acpi_resource_count) {
				const auto& source = acpi_resources[index];
				if (source.device_index != device_index) continue;
				switch (source.type) {
				case AcpiResourceType::Irq: {
					auto& target = info->irq_resources[irq_index++];
					target.interrupt = source.value;
					target.flags = source.flags;
					break;
				}
				case AcpiResourceType::GpioInterrupt:
				case AcpiResourceType::GpioIo: {
					auto& target = info->gpio_resources[gpio_index++];
					copy_text(target.controller_path, sizeof(target.controller_path), source.source);
					target.pin = source.value;
					target.flags = source.flags;
					target.type = source.type == AcpiResourceType::GpioInterrupt ? DeviceAcpiGpioType::Interrupt : DeviceAcpiGpioType::Io;
					target.pin_config = source.width;
					break;
				}
				case AcpiResourceType::Memory:
				case AcpiResourceType::IoPort: {
					auto& target = info->range_resources[range_index++];
					target.minimum = source.minimum;
					target.maximum = source.maximum;
					target.length = source.length;
					target.granularity = source.granularity;
					target.translation = source.translation;
					target.flags = source.flags;
					target.type = source.type == AcpiResourceType::Memory ? DeviceAcpiRangeType::Memory : DeviceAcpiRangeType::IoPort;
					target.width = source.width;
					break;
				}
				default: break;
				}
			}
			return info;
		}

		bool attach_acpi_info(DeviceNode* node, uint16 device_index) {
			if (!node) return false;
			if (node->fields.acpi_info) return true;
			node->fields.acpi_info = create_acpi_info(device_index);
			return node->fields.acpi_info != nullptr;
		}

		void format_acpi_node_name(char* destination, stduint capacity, const char* prefix, uint64 value, bool hexadecimal) {
			if (!destination || !capacity) return;
			destination[0] = 0;
			append_text(destination, capacity, prefix);
			if (hexadecimal) append_hex(destination, capacity, value);
			else append_decimal(destination, capacity, value);
		}

		bool serial_node_types(uint8 serial_type, DeviceBusType& bus_type, DeviceNodeType& bus_node_type, DeviceNodeType& endpoint_node_type, const char*& bus_name, const char*& endpoint_name) {
			switch (AcpiSerialBusType(serial_type)) {
			case AcpiSerialBusType::I2C:
				bus_type = DeviceBusType::I2C;
				bus_node_type = DeviceNodeType::I2cBus;
				endpoint_node_type = DeviceNodeType::I2cDevice;
				bus_name = "i2c-bus@";
				endpoint_name = "i2c-dev@";
				return true;
			case AcpiSerialBusType::SPI:
				bus_type = DeviceBusType::SPI;
				bus_node_type = DeviceNodeType::SpiBus;
				endpoint_node_type = DeviceNodeType::SpiDevice;
				bus_name = "spi-bus@";
				endpoint_name = "spi-dev@";
				return true;
			case AcpiSerialBusType::UART:
				bus_type = DeviceBusType::UART;
				bus_node_type = DeviceNodeType::UartBus;
				endpoint_node_type = DeviceNodeType::UartDevice;
				bus_name = "uart-bus@";
				endpoint_name = "uart-dev@";
				return true;
			default:
				return false;
			}
		}

		DeviceNode* ensure_acpi_controller_node(const AcpiSerialResourceRecord& resource, bool& first_link, bool& platform_controller) {
			first_link = false;
			platform_controller = false;
			if (resource.controller_device_index >= acpi_device_count) return nullptr;
			const uint16 device_index = resource.controller_device_index;
			if (acpi_controller_nodes[device_index]) return acpi_controller_nodes[device_index];
			DeviceNode* controller = nullptr;
			const auto status = AcpiControllerLinkStatus(resource.controller_status);
			if (status == AcpiControllerLinkStatus::Pci) controller = resource.pci_controller;
			else if (status == AcpiControllerLinkStatus::AcpiOnly) {
				char name[48] = {};
				format_acpi_node_name(name, sizeof(name), "acpi-controller@", device_index, false);
				auto* root = get_bus_root(DeviceBusType::Platform);
				controller = find_named_child(root, DeviceNodeType::PlatformDevice, name);
				if (!controller) controller = append_plain_device(root, DeviceNodeType::PlatformDevice, DeviceBusType::Platform, StrHeap(name));
				platform_controller = true;
			}
			if (!controller || !attach_acpi_info(controller, device_index)) return nullptr;
			acpi_controller_nodes[device_index] = controller;
			first_link = true;
			return controller;
		}

		DeviceNode* ensure_acpi_serial_bus(const AcpiSerialResourceRecord& resource, DeviceNode* controller, bool& created) {
			created = false;
			DeviceBusType bus_type = DeviceBusType::None;
			DeviceNodeType bus_node_type = DeviceNodeType::BusRoot;
			DeviceNodeType endpoint_node_type = DeviceNodeType::BusRoot;
			const char* bus_prefix = nullptr;
			const char* endpoint_prefix = nullptr;
			if (!serial_node_types(resource.bus_type, bus_type, bus_node_type, endpoint_node_type, bus_prefix, endpoint_prefix)) return nullptr;
			char name[48] = {};
			format_acpi_node_name(name, sizeof(name), bus_prefix, resource.controller_device_index, false);
			auto* root = get_bus_root(bus_type);
			auto* bus = find_named_child(root, bus_node_type, name);
			if (!bus) {
				bus = append_plain_device(root, bus_node_type, bus_type, StrHeap(name));
				created = bus != nullptr;
			}
			if (bus) bus->fields.controller = controller;
			return bus;
		}

		DeviceNode* ensure_acpi_endpoint_node(const AcpiSerialResourceRecord& resource, DeviceNode* bus, bool& created) {
			created = false;
			if (!bus || resource.device_index >= acpi_device_count) return nullptr;
			if (acpi_endpoint_nodes[resource.device_index]) return acpi_endpoint_nodes[resource.device_index];
			DeviceBusType bus_type = DeviceBusType::None;
			DeviceNodeType bus_node_type = DeviceNodeType::BusRoot;
			DeviceNodeType endpoint_node_type = DeviceNodeType::BusRoot;
			const char* bus_prefix = nullptr;
			const char* endpoint_prefix = nullptr;
			if (!serial_node_types(resource.bus_type, bus_type, bus_node_type, endpoint_node_type, bus_prefix, endpoint_prefix)) return nullptr;
			const uint64 unit_address = AcpiSerialBusType(resource.bus_type) == AcpiSerialBusType::UART ? resource.device_index : resource.address;
			char name[48] = {};
			format_acpi_node_name(name, sizeof(name), endpoint_prefix, unit_address, AcpiSerialBusType(resource.bus_type) != AcpiSerialBusType::UART);
			auto* endpoint = find_named_child(bus, endpoint_node_type, name);
			if (!endpoint) {
				endpoint = append_plain_device(bus, endpoint_node_type, bus_type, StrHeap(name));
				created = endpoint != nullptr;
			}
			if (!endpoint || !attach_acpi_info(endpoint, resource.device_index)) return nullptr;
			acpi_endpoint_nodes[resource.device_index] = endpoint;
			return endpoint;
		}

		void materialize_x86_acpi_serial_devices() {
			for0(index, AcpiDeviceCapacity) {
				acpi_controller_nodes[index] = nullptr;
				acpi_endpoint_nodes[index] = nullptr;
			}
			stduint pci_controller_count = 0;
			stduint platform_controller_count = 0;
			stduint bus_count = 0;
			stduint endpoint_count = 0;
			stduint skipped_count = 0;
			for0(index, acpi_serial_resource_count) {
				const auto& resource = acpi_serial_resources[index];
				const auto status = AcpiControllerLinkStatus(resource.controller_status);
				if (!acpi_device_is_present(resource.device_index) || !acpi_device_is_present(resource.controller_device_index) || (status != AcpiControllerLinkStatus::Pci && status != AcpiControllerLinkStatus::AcpiOnly)) {
					++skipped_count;
					continue;
				}
				if (acpi_endpoint_nodes[resource.device_index]) continue;
				bool first_controller_link = false;
				bool platform_controller = false;
				auto* controller = ensure_acpi_controller_node(resource, first_controller_link, platform_controller);
				if (!controller) {
					++skipped_count;
					continue;
				}
				if (first_controller_link) {
					if (platform_controller) ++platform_controller_count;
					else ++pci_controller_count;
				}
				bool bus_created = false;
				auto* bus = ensure_acpi_serial_bus(resource, controller, bus_created);
				if (!bus) {
					++skipped_count;
					continue;
				}
				if (bus_created) ++bus_count;
				bool endpoint_created = false;
				if (!ensure_acpi_endpoint_node(resource, bus, endpoint_created)) {
					++skipped_count;
					continue;
				}
				if (endpoint_created) ++endpoint_count;
			}
			if (acpi_serial_resource_count) ploginfo("[ACPI-NODE-SUM] pci-controller=%u platform-controller=%u bus=%u endpoint=%u skipped=%u", pci_controller_count, platform_controller_count, bus_count, endpoint_count, skipped_count);
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
		if (acpi_resources) delete[] acpi_resources;
		acpi_resources = new AcpiResourceRecord[AcpiResourceCapacity];
		acpi_device_count = 0;
		acpi_serial_resource_count = 0;
		acpi_resource_count = 0;
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
		acpi_resource_truncated = !acpi_resources;
		for0(index, aml_evaluation_summary_count) aml_evaluation_summaries[index] = {};
		aml_evaluation_summary_count = 0;
		for0(index, aml_region_summary_count) aml_region_summaries[index] = {};
		aml_region_summary_count = 0;
		for0(index, stduint(AmlPciReadStatus::Count)) {
			aml_pci_read_counts[index] = 0;
			aml_pci_read_examples[index][0] = 0;
		}
		for0(index, stduint(AmlPciWriteStatus::Count)) {
			aml_pci_write_counts[index] = 0;
			aml_pci_write_examples[index][0] = 0;
		}
		for0(index, stduint(AmlMemoryReadStatus::Count)) {
			aml_memory_read_counts[index] = 0;
			aml_memory_read_examples[index][0] = 0;
		}
		for0(index, stduint(AmlMemoryWriteStatus::Count)) {
			aml_memory_write_counts[index] = 0;
			aml_memory_write_examples[index][0] = 0;
		}
		for0(index, stduint(AmlIoReadStatus::Count)) {
			aml_io_read_counts[index] = 0;
			aml_io_read_examples[index][0] = 0;
		}
		for0(index, stduint(AmlIoWriteStatus::Count)) {
			aml_io_write_counts[index] = 0;
			aml_io_write_examples[index][0] = 0;
		}
		collect_x86_aml_tables();
		uni::AMLNamespace aml_namespace(aml_namespace_objects, numsof(aml_namespace_objects));
		load_x86_aml_namespace(aml_namespace);
		uint8* aml_buffer_storage = new uint8[AmlBufferCapacity];
		const uni::AMLExecutor::Workspace aml_workspace = { aml_execution_frames, numsof(aml_execution_frames), aml_object_values, numsof(aml_object_values), { nullptr, read_aml_region, write_aml_region }, aml_dynamic_objects, numsof(aml_dynamic_objects), aml_mutex_states, numsof(aml_mutex_states), aml_buffer_storage, aml_buffer_storage ? AmlBufferCapacity : 0 };
		const uni::AMLExecutor::Limits aml_limits = { 4096, 256 };
		uni::AMLExecutor aml_executor(aml_namespace, aml_workspace, aml_limits);
		acpi_conditional_block_count = aml_namespace.ConditionalCount();
		collect_x86_acpi_devices(aml_namespace, aml_executor);
		resolve_x86_acpi_serial_controllers(aml_namespace);
		stduint i2c_count = 0;
		stduint spi_count = 0;
		stduint uart_count = 0;
		stduint other_serial_count = 0;
		stduint present_count = 0;
		stduint absent_count = 0;
		stduint unknown_count = 0;
		stduint resource_counts[stduint(AcpiResourceType::Count)] = {};
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
		for0(index, acpi_resource_count) ++resource_counts[stduint(acpi_resources[index].type)];
		ploginfo("[ACPI-ENUM] mode=namespace bits=%u tables=%u objects=%u devices=%u present=%u absent=%u unknown=%u serial=%u i2c=%u spi=%u uart=%u other=%u conditional=%u malformed=%u truncated=%u",
			stduint(sizeof(stduint) * 8), aml_table_count,
			aml_namespace.Count(), acpi_device_count, present_count, absent_count, unknown_count,
			acpi_serial_resource_count,
			i2c_count, spi_count, uart_count, other_serial_count,
			acpi_conditional_block_count, acpi_malformed_count,
			stduint(acpi_device_truncated || acpi_serial_resource_truncated || acpi_resource_truncated || aml_namespace.Truncated()));
		ploginfo("[ACPI-RESOURCE-SUM] irq=%u gpio-int=%u gpio-io=%u memory=%u io=%u total=%u truncated=%u", resource_counts[stduint(AcpiResourceType::Irq)], resource_counts[stduint(AcpiResourceType::GpioInterrupt)], resource_counts[stduint(AcpiResourceType::GpioIo)], resource_counts[stduint(AcpiResourceType::Memory)], resource_counts[stduint(AcpiResourceType::IoPort)], acpi_resource_count, stduint(acpi_resource_truncated));
		log_acpi_controller_links();
		materialize_x86_acpi_serial_devices();
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
		delete[] aml_buffer_storage;
	}

}

#endif
