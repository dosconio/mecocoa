// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: ES8388 audio codec
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "aucoder.hpp"

#if defined(_MCU_STM32H7x) && CONFIG_ENABLE_AUDIOSYS
#include <cpp/Device/IIC>
#include <cpp/Device/SysTick>

namespace {
	class ES8388Codec final : public uni::AudioDeviceInterface {
		uni::IIC_t* bus = nullptr;
		byte address = 0;
		bool ready = false;
		bool muted = false;
		uint32 volume[2]{ 100, 100 };
		uni::AudioFormat format{};

		bool WriteRegister(byte reg, byte value) {
			bus->SendStart(address);
			if (bus->getError()) { bus->SendStop(); return false; }
			bus->Send(reg, true);
			if (bus->getError()) { bus->SendStop(); return false; }
			bus->Send(value, true);
			const bool result = !bus->getError();
			bus->SendStop();
			return result;
		}

		bool ReadRegister(byte reg, byte& value) {
			bus->SendStart(address);
			if (bus->getError()) { bus->SendStop(); return false; }
			bus->Send(reg, true);
			if (bus->getError()) { bus->SendStop(); return false; }
			bus->SendStart();
			bus->Send(byte((address << 1) | 1), true);
			if (bus->getError()) { bus->SendStop(); return false; }
			value = bus->ReadByte(true, false);
			const bool result = !bus->getError();
			bus->SendStop();
			return result;
		}

		bool SetOutputVolume(stduint channel, uint32 percent) {
			if (channel > 1) return false;
			if (percent > 100) percent = 100;
			const byte value = byte(percent * 33 / 100);
			const byte left = channel ? 0x30 : 0x2E;
			if (!WriteRegister(left, value) || !WriteRegister(left + 1, value))
				return false;
			volume[channel] = percent;
			return true;
		}

		bool SetDigitalMute(bool value) {
			byte control = 0;
			if (!ReadRegister(0x19, control)) return false;
			if (value) control |= 1u << 2;
			else control &= byte(~(1u << 2));
			if (!WriteRegister(0x19, control)) return false;
			muted = value;
			return true;
		}

		bool InitializeRegisters() {
			if (!WriteRegister(0x00, 0x80) || !WriteRegister(0x00, 0x00)) return false;
			SysDelay_ms(100);
			if (!WriteRegister(0x01, 0x58) || !WriteRegister(0x01, 0x50) ||
				!WriteRegister(0x02, 0xF3) || !WriteRegister(0x02, 0xF0) ||
				!WriteRegister(0x03, 0x09) || !WriteRegister(0x00, 0x06) ||
				!WriteRegister(0x04, 0x00) || !WriteRegister(0x08, 0x00) ||
				!WriteRegister(0x2B, 0x80) || !WriteRegister(0x09, 0x88) ||
				!WriteRegister(0x0C, 0x4C) || !WriteRegister(0x0D, 0x02) ||
				!WriteRegister(0x10, 0x00) || !WriteRegister(0x11, 0x00) ||
				!WriteRegister(0x17, 0x18) || !WriteRegister(0x18, 0x02) ||
				!WriteRegister(0x1A, 0x00) || !WriteRegister(0x1B, 0x00) ||
				!WriteRegister(0x27, 0xB8) || !WriteRegister(0x2A, 0xB8))
				return false;
			SysDelay_ms(100);
			if (!WriteRegister(0x02, 0x0A) || !WriteRegister(0x04, 0x3C)) return false;
			return SetOutputVolume(0, 10) && SetOutputVolume(1, 10) && SetDigitalMute(false);
		}

	public:
		bool ProbeInitialize(uni::IIC_t& value, byte device_address) {
			bus = &value;
			address = device_address;
			ready = false;
			byte control0 = 0, control1 = 0;
			if (!ReadRegister(0x00, control0) || !ReadRegister(0x01, control1)) return false;
			(void)control0;
			(void)control1;
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
			case 16: length = 3; break;
			case 18: length = 2; break;
			case 20: length = 1; break;
			case 24: length = 0; break;
			case 32: length = 4; break;
			default: return false;
			}
			return WriteRegister(0x17, byte((protocol << 1) | (length << 3)));
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

	ES8388Codec codec;

	uni::AudioDeviceInterface* ProbeInitializeES8388(uni::IIC_t& bus, byte address) {
		return codec.ProbeInitialize(bus, address) ? &codec : nullptr;
	}
}

const AudioCodecDriver AudioCodecES8388{
	"es8388",
	"saiback-es8388",
	ProbeInitializeES8388,
};
#else
const AudioCodecDriver AudioCodecES8388{
	"es8388",
	"saiback-es8388",
	nullptr,
};
#endif
