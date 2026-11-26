// UTF-8 CPP-ISO11 TAB4 LF
// ModuTitle: STM32H7 SAI audio peripheral
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x) && ((_MCCA & 0xFFFF) == 0x2032)
#include <cpp/Device/Cache>
#include <cpp/Device/DMA>
#include <cpp/Device/SAI>
#include "aucoder.hpp"
#include "unicfg.hpp"

_ESYM_C void R_SAI_INIT();

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_SAI{
	R_SAI_INIT,
	"SAI",
	{}
};

namespace {
	constexpr stduint sai_register_size =
		_IMM(uni::SAIGlobalReg::PDMDLY) + sizeof(uint32);
	constexpr byte sai_block_a = 1;
	constexpr unsigned dma_controller_count = 2;

	bool ResolveDmaStream(unsigned controller, unsigned stream,
		const uni::DMAStream*& result) {
		result = nullptr;
		if (!controller) return stream == 0;
		if (controller > dma_controller_count || stream >= DeviceDmaStreamCount)
			return false;
		result = &uni::DMA[byte(controller)][stream];
		return true;
	}

	bool BindSaiDma(uni::SAI_t& sai, byte block,
		unsigned tx_controller, unsigned tx_stream,
		unsigned rx_controller, unsigned rx_stream) {
		const uni::DMAStream* tx = nullptr;
		const uni::DMAStream* rx = nullptr;
		return ResolveDmaStream(tx_controller, tx_stream, tx) &&
			ResolveDmaStream(rx_controller, rx_stream, rx) &&
			sai[block].setDMA(tx, rx);
	}

#if CONFIG_ENABLE_AUDIOSYS
	static_assert(BoardConfig::Sai::DmaBufferCount == 2);
	static_assert(BoardConfig::Sai::DmaBufferBytes != 0);
	static_assert((BoardConfig::Sai::DmaBufferBase & 31U) == 0);
	static_assert((BoardConfig::Sai::DmaBufferBytes & 31U) == 0);
	static_assert(BoardConfig::Sai::OutputMckDivider > 0);
	static_assert(BoardConfig::Sai::OutputMckDivider < 64);
	static_assert(BoardConfig::AudioCodec::InitialVolumePercent <= 100);

	alignas(32) byte sai_conversion_buffer[BoardConfig::Sai::DmaBufferBytes / 2];

	uni::AudioDeviceInterface* ProbeConfiguredCodec(bool& probe_completed) {
		probe_completed = true;
		if (!BoardConfig::AudioCodec::Enabled ||
			!BoardConfig::AudioCodec::ControlBus ||
			!BoardConfig::AudioCodec::CandidateCount)
			return nullptr;
		probe_completed = false;
		auto* bus_node = Devsman::FindNamedNode(DeviceNodeType::I2cBus,
			BoardConfig::AudioCodec::ControlBus);
		if (!bus_node || DeviceBusType(bus_node->fields.bus_type) != DeviceBusType::I2C ||
			!bus_node->fields.binding.driver_data)
			return nullptr;
		probe_completed = true;
		auto& bus = *static_cast<uni::IIC_t*>(bus_node->fields.binding.driver_data);
		for (stduint index = 0; index < BoardConfig::AudioCodec::CandidateCount; ++index) {
			const auto& candidate = BoardConfig::AudioCodec::Candidates[index];
			if (!candidate.Driver || !candidate.Driver->NodeName ||
				!candidate.Driver->DriverName || !candidate.Driver->ProbeInitialize)
				continue;
			auto* codec = candidate.Driver->ProbeInitialize(bus, candidate.Address);
			if (!codec || !codec->isReady()) continue;
			const auto name = String::newFormat("%s@%02X",
				candidate.Driver->NodeName, unsigned(candidate.Address));
			auto* node = Devsman::RegisterDevice(bus_node, DeviceNodeType::I2cDevice,
				DeviceBusType::I2C, name.reference(), candidate.Driver->DriverName, codec);
			if (!node) return nullptr;
			if (!Devsman::AddAddressResource(node, 0, candidate.Address, 7)) {
				Devsman::UnregisterDevice(node);
				return nullptr;
			}
			return codec;
		}
		return nullptr;
	}

	class Stm32SaiAudioDevice final : public uni::AudioControlInterface {
		uni::SAI_Block* block = nullptr;
		const uni::DMAStream* stream = nullptr;
		uni::AudioDeviceInterface* codec = nullptr;
		byte* buffers[2]{};
		uni::AudioPcmRefillHandler refill = nullptr;
		void* refill_context = nullptr;
		uni::Atomic<uint32> completed[2]{};
		uni::Atomic<uint32> dma_error{ 0 };
		uint32 underruns = 0;
		uint32 volume_percent = BoardConfig::AudioCodec::InitialVolumePercent;
		bool initialized = false;
		bool running = false;
		bool paused = false;
		bool muted = false;
		bool codec_probe_completed = false;

		bool SetCodecVolume(uint32 percent) {
			if (!codec || !codec->isReady()) return false;
			bool result = true;
			for (stduint channel = 0; channel < codec->getChannelCount(); ++channel)
				result = codec->setVolume(channel, percent, percent) && result;
			return result;
		}

		bool SetCodecMute(bool value) {
			if (!codec || !codec->isReady()) return false;
			bool result = true;
			for (stduint channel = 0; channel < codec->getChannelCount(); ++channel)
				result = codec->setMute(channel, value) && result;
			return result;
		}

		AudioHardwareProbeResult ProbeHardware() {
			if (codec && codec->isReady()) return AudioHardwareProbeResult::Ready;
			if (codec_probe_completed) return AudioHardwareProbeResult::Unavailable;
			bool probe_completed = false;
			codec = ProbeConfiguredCodec(probe_completed);
			codec_probe_completed = probe_completed;
			if (!codec) return probe_completed
				? AudioHardwareProbeResult::Unavailable
				: AudioHardwareProbeResult::Deferred;
			if (!SetCodecVolume(volume_percent) || !SetCodecMute(muted))
				return AudioHardwareProbeResult::Unavailable;
			return AudioHardwareProbeResult::Ready;
		}

		bool EnsureCodec() {
			return ProbeHardware() == AudioHardwareProbeResult::Ready;
		}

		static void CompleteMemory0(pureptr_t context) {
			auto* device = (Stm32SaiAudioDevice*)context;
			device->completed[0].fetch_add(1, uni::MemoryOrder_Release);
		}

		static void CompleteMemory1(pureptr_t context) {
			auto* device = (Stm32SaiAudioDevice*)context;
			device->completed[1].fetch_add(1, uni::MemoryOrder_Release);
		}

		static void TransferError(pureptr_t context) {
			auto* device = (Stm32SaiAudioDevice*)context;
			device->dma_error.store(1, uni::MemoryOrder_Release);
		}

		bool IsBufferActive(byte index) const {
			using namespace uni::DMAReg;
			auto& dma = stream->getParent();
			const bool memory1 = dma[CR[stream->getID()]].bitof(_DMA_SxCR_POS_CT);
			return index ? memory1 : !memory1;
		}

		void FillBuffer(byte index) {
			byte* output = buffers[index];
			const uint32 output_bytes = BoardConfig::Sai::DmaBufferBytes;
			const uint32 input_capacity = output_bytes / 2;
			uint32 input_bytes = 0;
			if (!paused && refill)
				input_bytes = refill(refill_context, sai_conversion_buffer, input_capacity);
			if (input_bytes > input_capacity) input_bytes = input_capacity;
			input_bytes &= ~uint32(1);

			auto* destination = (int16*)output;
			const uint32 frames = input_bytes / 2;
			for (uint32 frame = 0; frame < frames; ++frame) {
				int32 left = (int32(sai_conversion_buffer[frame * 2]) - 128) << 8;
				int32 right = (int32(sai_conversion_buffer[frame * 2 + 1]) - 128) << 8;
				destination[frame * 2] = int16(left);
				destination[frame * 2 + 1] = int16(right);
			}
			const uint32 converted_bytes = frames * 4;
			if (converted_bytes < output_bytes)
				MemSet(output + converted_bytes, 0, output_bytes - converted_bytes);
			uni::L1C.CleanDCacheRange(output, output_bytes);
		}

		bool ConfigureSai() {
			block->enAble(false);
			block->setAudioMode(uni::SAIAudioMode_E::MasterTx)
				.setProtocol(uni::SAIProtocol_E::I2S)
				.setDataSize(uni::SAIDataSize_E::Data16)
				.setOutputDrive(uni::SAIOutputDrive_E::Enable)
				.setNoDivider(uni::SAINoDivider_E::Divider)
				.setFifoThreshold(uni::SAIFifoThreshold_E::Quarter)
				.setMonoStereo(uni::SAIMonoStereo_E::Stereo)
				.setSlotNumber(2)
				.setMckOverSampling(uni::SAIMckOverSampling_E::Disable)
				.setAudioFreq(0)
				.setMckDiv(BoardConfig::Sai::OutputMckDivider);
			return block->canMode();
		}

		bool ConfigureDma() {
			auto& dma = stream->getParent();
			const byte state = stream->getState();
			if (state == _DMA_STATE_BUSY || state == _DMA_STATE_ABORT)
				return false;
			if (!dma.enClock()) return false;
			if (!(*stream)[0].setMode(false, true, false, true, true,
				uni::DMADataWidth::HalfWord, uni::DMADataWidth::HalfWord, 2))
				return false;
			stduint request = 0;
			if (!block->getDMARequest(request)) return false;
			stream->setRequest(request);
			uni::DMAStreamCallbacks callbacks{};
			callbacks.Complete = CompleteMemory0;
			callbacks.Memory1Complete = CompleteMemory1;
			callbacks.Error = TransferError;
			callbacks.Context = this;
			stream->setCallbacks(callbacks);
			stream->setInterruptPriority(1, 0);
			return true;
		}

	public:
		bool Initialize(uni::SAI_Block& sai_block, const uni::DMAStream& dma_stream) {
			block = &sai_block;
			stream = &dma_stream;
			buffers[0] = (byte*)BoardConfig::Sai::DmaBufferBase;
			buffers[1] = buffers[0] + BoardConfig::Sai::DmaBufferBytes;
			initialized = true;
			return true;
		}

		AudioHardwareProbeResult ProbeConfiguredHardware() {
			return ProbeHardware();
		}

		bool StartStream(const uni::AudioFormat& format,
			uni::AudioPcmRefillHandler refill_handler, void* context) override {
			if (!initialized || !refill_handler ||
				format.sample_format != uni::AudioSampleFormat::U8 ||
				format.channels != 2 ||
				format.sample_rate != BoardConfig::Sai::OutputSampleRate)
				return false;
			if (!EnsureCodec()) return false;
			if (running && !StopStream()) return false;
			const uni::AudioFormat wire_format{
				.sample_format = uni::AudioSampleFormat::S16LE,
				.channels = 2,
				.sample_rate = BoardConfig::Sai::OutputSampleRate,
			};
			if (!codec->setFormat(wire_format) || !codec->ConfigI2S(0, 16))
				return false;

			refill = refill_handler;
			refill_context = context;
			completed[0].store(0, uni::MemoryOrder_Relaxed);
			completed[1].store(0, uni::MemoryOrder_Relaxed);
			dma_error.store(0, uni::MemoryOrder_Relaxed);
			underruns = 0;
			paused = false;
			if (!ConfigureSai() || !ConfigureDma()) {
				stream->clearCallbacks();
				refill = nullptr;
				refill_context = nullptr;
				return false;
			}

			FillBuffer(0);
			FillBuffer(1);
			const stduint data_register = block->blockBase() + _IMM(uni::SAIReg::DR);
			const stduint transfer_count =
				BoardConfig::Sai::DmaBufferBytes / sizeof(uint16);
			if (!stream->MultiBufferTransfer(
				(pureptr_t)data_register, buffers[0], buffers[1],
				transfer_count, uni::IOMethod::Rupt)) {
				stream->clearCallbacks();
				refill = nullptr;
				refill_context = nullptr;
				return false;
			}
			running = true;
			(*block)[uni::SAIReg::CLRFR] = ~uint32(0);
			block->ResumeDMA();
			block->enAble(true);
			return true;
		}

		bool StopStream() override {
			if (!initialized) return false;
			block->PauseDMA();
			block->enAble(false);
			(*block)[uni::SAIReg::CLRFR] = ~uint32(0);
			bool result = true;
			if (stream->getState() == _DMA_STATE_BUSY)
				result = stream->Abort();
			stream->enInterruptNVIC(false);
			stream->ClearInterruptFlags();
			stream->clearCallbacks();
			completed[0].store(0, uni::MemoryOrder_Relaxed);
			completed[1].store(0, uni::MemoryOrder_Relaxed);
			dma_error.store(0, uni::MemoryOrder_Relaxed);
			refill = nullptr;
			refill_context = nullptr;
			running = false;
			paused = false;
			return result;
		}

		bool PauseStream() override {
			if (!running) return false;
			paused = true;
			return true;
		}

		bool ResumeStream() override {
			if (!running) return false;
			paused = false;
			return true;
		}

		uint8 ServicePlayback() override {
			if (!running) return 0;
			if (dma_error.exchange(0, uni::MemoryOrder_Acq_Rel)) {
				StopStream();
				return 2;
			}

			uint8 serviced = 0;
			for (byte index = 0; index < 2; ++index) {
				if (!completed[index].load(uni::MemoryOrder_Acquire) ||
					IsBufferActive(index))
					continue;
				const uint32 count = completed[index].exchange(
					0, uni::MemoryOrder_Acq_Rel);
				if (count > 1) underruns += count - 1;
				FillBuffer(index);
				serviced = 1;
			}
			return serviced;
		}

		void setVolume(uint32 percent) override {
			volume_percent = percent > 100 ? 100 : percent;
			muted = volume_percent == 0;
			if (EnsureCodec()) {
				SetCodecVolume(volume_percent);
				SetCodecMute(muted);
			}
		}

		uint32 getVolume() const override {
			return volume_percent;
		}

		void setMute(bool value = true) override {
			muted = value;
			if (EnsureCodec()) SetCodecMute(value);
		}
	};

	Stm32SaiAudioDevice sai_audio_device;
#endif

	bool RegisterSaiDevice(const char* name, uni::SAI_t& sai,
		const BoardConfig::Sai::PinSet& pins, uni::QrsDeviceList device,
		bool enabled, unsigned tx_dma_controller, unsigned tx_dma_stream,
		unsigned rx_dma_controller, unsigned rx_dma_stream) {
		bool hardware_ready = false;
		if (enabled) {
			if (!pins.Pins || !pins.Count) {
				plogerro("[SAI] %s enabled without pin configuration", name);
			} else if (!ConfigDeviceGPIN(device) || !ConfigDeviceClock(device) ||
				!sai.enClock()) {
				plogerro("[SAI] %s hardware initialization failed", name);
			} else if (!BindSaiDma(sai, sai_block_a,
				tx_dma_controller, tx_dma_stream,
				rx_dma_controller, rx_dma_stream)) {
				plogerro("[SAI] %s DMA stream binding failed", name);
			} else {
				hardware_ready = true;
			}
		}

		auto* node = Devsman::RegisterPlatformDevice(name, "stm32-sai", &sai);
		if (!node) {
			plogerro("[SAI] cannot register %s", name);
			return false;
		}
		node->fields.rcc_state = sai.isAble()
			? DeviceRccState::Enabled : DeviceRccState::Disabled;
		bool ready = Devsman::AddMemoryResource(node, 0,
			sai.getAddress(), sai_register_size) &&
			Devsman::AddIrqResource(node, sai.getRequest());
		for (stduint i = 0; ready && pins.Pins && i < pins.Count; ++i) {
			const auto& pin = pins.Pins[i];
			ready = Devsman::AddPinResource(node, uint32(i), pin.Name,
				pin.Port, pin.Pin, pin.AlternateFunction);
		}
		stduint request = 0;
		if (ready && (tx_dma_controller || rx_dma_controller)) {
			ready = sai[sai_block_a].getDMARequest(request);
			if (ready && tx_dma_controller) {
				const auto& dma = uni::DMA[byte(tx_dma_controller)];
				ready = Devsman::AddDmaRouteResource(node, 0,
					dma.getAddress(), tx_dma_stream, request, true);
			}
			if (ready && rx_dma_controller) {
				const auto& dma = uni::DMA[byte(rx_dma_controller)];
				ready = Devsman::AddDmaRouteResource(node, 1,
					dma.getAddress(), rx_dma_stream, request, false);
			}
		}
		if (!ready) {
			plogerro("[SAI] cannot register %s resources", name);
			Devsman::UnregisterDevice(node);
			return false;
		}
		return enabled && hardware_ready;
	}
}

void R_SAI_INIT() {
	const bool sai1_ready = RegisterSaiDevice("sai1", uni::SAI1,
		BoardConfig::Sai::SAI1Pins, uni::QrsDeviceList::SAI1,
		BoardConfig::Sai::SAI1Enabled,
		BoardConfig::Sai::SAI1TxDmaController,
		BoardConfig::Sai::SAI1TxDmaStream,
		BoardConfig::Sai::SAI1RxDmaController,
		BoardConfig::Sai::SAI1RxDmaStream);
	RegisterSaiDevice("sai2", uni::SAI2, BoardConfig::Sai::SAI2Pins,
		uni::QrsDeviceList::SAI2, BoardConfig::Sai::SAI2Enabled,
		BoardConfig::Sai::SAI2TxDmaController,
		BoardConfig::Sai::SAI2TxDmaStream,
		BoardConfig::Sai::SAI2RxDmaController,
		BoardConfig::Sai::SAI2RxDmaStream);

#if CONFIG_ENABLE_AUDIOSYS
	const uni::DMAStream* tx_stream = nullptr;
	if (sai1_ready && ResolveDmaStream(
		BoardConfig::Sai::SAI1TxDmaController,
		BoardConfig::Sai::SAI1TxDmaStream, tx_stream) && tx_stream &&
		sai_audio_device.Initialize(
			uni::SAI1[BoardConfig::Sai::OutputBlock], *tx_stream) &&
		!audio_manager.AppendCard("sai1", &sai_audio_device))
		plogerro("[SAI] cannot register sai1 audio backend");
#endif
}

AudioHardwareProbeResult AudioBackendProbeHardware() {
#if CONFIG_ENABLE_AUDIOSYS
	return sai_audio_device.ProbeConfiguredHardware();
#else
	return AudioHardwareProbeResult::Unavailable;
#endif
}
#endif
