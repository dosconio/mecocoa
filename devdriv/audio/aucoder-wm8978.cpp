// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: WM8978 audio codec
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "aucoder.hpp"

#if defined(_MCU_STM32H7x) && CONFIG_ENABLE_AUDIOSYS
#include <cpp/Device/IIC>
#include <cpp/Device/SysTick>

namespace {
	class WM8978Codec final : public uni::AudioDeviceInterface {
		uni::IIC_t* bus = nullptr;
		byte address = 0;
		uint16 registers[58]{};
		bool ready = false;
		bool muted = false;
		uint32 volume[2]{ 100, 100 };
		uni::AudioFormat format{};

		void ResetRegisterMap() {
			static const uint16 defaults[58] = {
				0x0000, 0x0000, 0x0000, 0x0000, 0x0050, 0x0000, 0x0140, 0x0000,
				0x0000, 0x0000, 0x0000, 0x00FF, 0x00FF, 0x0000, 0x0100, 0x00FF,
				0x00FF, 0x0000, 0x012C, 0x002C, 0x002C, 0x002C, 0x002C, 0x0000,
				0x0032, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
				0x0038, 0x000B, 0x0032, 0x0000, 0x0008, 0x000C, 0x0093, 0x00E9,
				0x0000, 0x0000, 0x0000, 0x0000, 0x0003, 0x0010, 0x0010, 0x0100,
				0x0100, 0x0002, 0x0001, 0x0001, 0x0039, 0x0039, 0x0039, 0x0039,
				0x0001, 0x0001,
			};
			for (byte i = 0; i < 58; ++i) registers[i] = defaults[i];
		}

		bool ProbeAddress() {
			bus->SendStart(address);
			const bool result = !bus->getError();
			bus->SendStop();
			return result;
		}

		bool WriteRegister(byte reg, uint16 value) {
			if (reg >= 58) return false;
			bus->SendStart(address);
			if (bus->getError()) { bus->SendStop(); return false; }
			bus->Send(byte((reg << 1) | ((value >> 8) & 1)), true);
			if (bus->getError()) { bus->SendStop(); return false; }
			bus->Send(byte(value), true);
			const bool result = !bus->getError();
			bus->SendStop();
			if (result) registers[reg] = value;
			return result;
		}

		bool SetOutputVolume(stduint channel, uint32 percent) {
			if (channel > 1) return false;
			if (percent > 100) percent = 100;
			byte value = byte(percent * 63 / 100);
			if (!value) value |= 1u << 6;
			const byte left = channel ? 54 : 52;
			if (!WriteRegister(left, value | (1u << 8)) ||
				!WriteRegister(left + 1, value | (1u << 8))) return false;
			volume[channel] = percent;
			return true;
		}

		bool SetDigitalMute(bool value) {
			uint16 control = registers[10];
			if (value) control |= 1u << 6;
			else control &= uint16(~(1u << 6));
			if (!WriteRegister(10, control)) return false;
			muted = value;
			return true;
		}

		bool InitializeRegisters() {
			if (!WriteRegister(0, 0)) return false;
			SysDelay_ms(20);
			ResetRegisterMap();
			if (!WriteRegister(1, 0x001B) || !WriteRegister(2, 0x01B0) ||
				!WriteRegister(3, 0x006F) || !WriteRegister(4, 0x0010) ||
				!WriteRegister(6, 0x0000) || !WriteRegister(10, 0x0008) ||
				!WriteRegister(43, 0x0010) || !WriteRegister(49, 0x0006) ||
				!WriteRegister(50, 0x0001) || !WriteRegister(51, 0x0001)) return false;
			return SetOutputVolume(0, 35) && SetOutputVolume(1, 28) && SetDigitalMute(false);
		}

	public:
		bool ProbeInitialize(uni::IIC_t& value, byte device_address) {
			bus = &value;
			address = device_address;
			ready = false;
			ResetRegisterMap();
			if (!ProbeAddress()) return false;
			ready = InitializeRegisters();
			return ready;
		}

		bool isReady() const override { return ready; }

		bool setFormat(const uni::AudioFormat& value) override {
			if (!ready) return false;
			format = value;
			return true;
		}

		bool ConfigI2S(uint32 protocol, uint32 bits) override {
			if (!ready || protocol > 3) return false;
			byte length = 0;
			switch (bits) {
			case 16: length = 0; break;
			case 20: length = 1; break;
			case 24: length = 2; break;
			case 32: length = 3; break;
			default: return false;
			}
			const byte wm_protocol = protocol ? byte(protocol) : 2;
			return WriteRegister(4, uint16((wm_protocol << 3) | (length << 5)));
		}

		stduint getChannelCount() const override { return 2; }
		const char* getChannelName(stduint channel) const override {
			return channel == 0 ? "Headphone" : channel == 1 ? "Speaker" : nullptr;
		}
		bool getMainChannel(stduint& channel) const override {
			channel = 0;
			return true;
		}
		bool setVolume(stduint channel, uint32 left, uint32 right) override {
			return ready && SetOutputVolume(channel, (left + right) / 2);
		}
		bool getVolume(stduint channel, uint32& left, uint32& right) const override {
			if (!ready || channel > 1) return false;
			left = right = volume[channel];
			return true;
		}
		bool setMute(stduint channel, bool value = true) override {
			return ready && channel < 2 && (muted == value || SetDigitalMute(value));
		}
	};

	WM8978Codec codec;

	uni::AudioDeviceInterface* ProbeInitializeWM8978(uni::IIC_t& bus, byte address) {
		return codec.ProbeInitialize(bus, address) ? &codec : nullptr;
	}
}

const AudioCodecDriver AudioCodecWM8978{
	"wm8978",
	"saiback-wm8978",
	ProbeInitializeWM8978,
};
#else
const AudioCodecDriver AudioCodecWM8978{
	"wm8978",
	"saiback-wm8978",
	nullptr,
};
#endif
