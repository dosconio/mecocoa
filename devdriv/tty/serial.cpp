// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Serial / UART
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

/*
* Intel 8250
* NS16450, NS16550(A)

Send 0x3FA (FCR) 0xC7（Enable and Clear FIFO）,  Read 0x3FA MS2B (IIR)。
- 00  8250
- 10  16550
- 11  16550A

*/

#include "../../include/mecocoa.hpp"
#include "c/driver/UART.h"

_ESYM_C void R_COM1_INIT();
bool SerialCom1Available();

void sysinfo_classic(OstreamTrait& com1, byte func);

#if (_MCCA & 0xFF00) == 0x8600
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_COM1{
	.init = R_COM1_INIT,
	.name = "COM1",
};
#endif

UART_t com1(PORT_COM1_DATA);
UART_t com2(0x02F8);
extern OstreamTrait* con0_out;
namespace {
	bool g_com1_available = false;
	bool g_com2_vtty_initialized = false;
	Dnode* g_com2_vtty = nullptr;
	ProcessBlock* g_com2_cot = nullptr;

	enum class SerialInputPolicy : uint8 {
		Com1Console,
		LazyCotConsole,
		EchoOnly,
	};

	struct LegacyComPort {
		const char* name;
		stduint base;
		stduint irq;
		bool available;
		SerialInputPolicy input_policy;
	};

	LegacyComPort legacy_com_ports[] = {
		{"uart@com1", 0x03F8, IRQ_COM13_RS232_P1, false, SerialInputPolicy::Com1Console},
		{"uart@com2", 0x02F8, IRQ_COM24_Serial, false, SerialInputPolicy::LazyCotConsole},
		{"uart@com3", 0x03E8, IRQ_COM13_RS232_P1, false, SerialInputPolicy::EchoOnly},
		{"uart@com4", 0x02E8, IRQ_COM24_Serial, false, SerialInputPolicy::EchoOnly},
	};

	Console_t* serial_console_for_port(const LegacyComPort& port) {
		if (port.base == 0x03F8) return &com1;
		if (port.base == 0x02F8) return &com2;
		return nullptr;
	}

	void ensure_serial_lazy_vtty(LegacyComPort& port) {
		if (port.input_policy != SerialInputPolicy::LazyCotConsole || !port.available) return;
		if (port.base != 0x02F8 || g_com2_vtty_initialized) return;
		auto* con = serial_console_for_port(port);
		if (!con) return;
		g_com2_vtty = VTTY_Append(con);
		g_com2_vtty_initialized = g_com2_vtty != nullptr;
	}

	bool probe_legacy_uart_scratch(stduint base) {
		// Legacy UART SCR is a software scratch register, so a read/write
		// round trip here checks whether the port range is decoded before
		// touching baud, FIFO, modem, or interrupt-control registers.
		const auto scratch = base + 7;
		const uint8 saved = innpb(scratch);
		outpb(scratch, 0x5A);
		const uint8 first = innpb(scratch);
		outpb(scratch, 0xA5);
		const uint8 second = innpb(scratch);
		outpb(scratch, saved);
		return first == 0x5A && second == 0xA5;
	}

	void register_available_legacy_com_ports() {
		if (!Devsman::Root()) return;
		for0a(i, legacy_com_ports) {
			auto& port = legacy_com_ports[i];
			if (!port.available) continue;
			ploginfo("[UART] %s detected at %[16H]", port.name, port.base);
			if (auto* node = Devsman::RegisterPlatformDevice(port.name)) {
				Devsman::AddIoPortResource(node, 0, port.base, 8);
				Devsman::AddIrqResource(node, port.irq);
			}
		}
	}

	void initialize_legacy_uart_port(const LegacyComPort& port) {
		outpb(port.base + 1, 0x00);	// Disable all interrupts
		outpb(port.base + 3, 0x80);	// Enable DLAB (set baud rate divisor)
		outpb(port.base + 0, 0x03);	// Set divisor to 3 (lo byte) 38400 baud
		outpb(port.base + 1, 0x00);	// Set divisor to 3 (hi byte)
		outpb(port.base + 3, 0x03);	// 8 bits, no parity, one stop bit (DLAB = 0)
		outpb(port.base + 2, 0xC7);	// Enable FIFO, clear them, with 14-byte threshold
		outpb(port.base + 4, 0x0B);	// IRQs enabled, RTS/DSR set (OUT2 bit must be 1)
		// Enable "Received Data Available" interrupt explicitly after DLAB is cleared.
		outpb(port.base + 1, 0x01);
	}

	bool serial_irq_has_available_port(stduint irq) {
		for0a(i, legacy_com_ports) {
			const auto& port = legacy_com_ports[i];
			if (port.available && port.irq == irq) return true;
		}
		return false;
	}

	bool serial_tx_ready(const LegacyComPort& port) {
		return innpb(port.base + 5) & 0x20;
	}

	void serial_out_byte(const LegacyComPort& port, uint8 data) {
		while (!serial_tx_ready(port));
		outpb(port.base, data);
	}

	uint8 serial_in_byte(const LegacyComPort& port) {
		return innpb(port.base);
	}

	void handle_serial_input(LegacyComPort& port) {
		const uint8 data = serial_in_byte(port);
		if (port.input_policy == SerialInputPolicy::Com1Console) {
			if (data == '\r') {
				com1.OutFormat("\n");
			}
			com1.OutFormat("%c", data);// com1.OutFormat("Receive %c\n\r", data);
			sysinfo_classic(com1, data);
			return;
		}
		if (port.input_policy == SerialInputPolicy::LazyCotConsole) {
			ensure_serial_lazy_vtty(port);
			if (!g_com2_vtty) return;

			byte queued = data == '\r' ? '\n' : data;
			if (!g_com2_cot) {
				// Provide immediate visible feedback for the first trigger before cot
				// has a chance to drain and echo the queued character itself.
				if (data == '\r') {
					serial_out_byte(port, '\n');
				}
				serial_out_byte(port, data);
			}
			if (auto* q = VTTY_INNQ(g_com2_vtty)) {
				q->OutChar(queued);
			}
			EnsureCotForVtty(g_com2_vtty, &g_com2_cot);
			Consman::WakeBlockedWaitersDeferred();
			return;
		}
		if (data == '\r') {
			serial_out_byte(port, '\n');
		}
		serial_out_byte(port, data);
	}

	void drain_serial_interrupts(stduint irq) {
		for0a(i, legacy_com_ports) {
			auto& port = legacy_com_ports[i];
			if (!port.available || port.irq != irq) continue;
			uint8 iir = innpb(port.base + 2);
			while ((iir & 0x01) == 0) {// Pending
				const uint8 int_id = (iir >> 1) & 0x07;// reason
				switch (int_id) {
				case 0x02: // Received Data Available
				case 0x06: // Character Timeout Indication (16550+ FIFO timeout)
					handle_serial_input(port);
					break;
				case 0x01: // Transmitter Holding Register Empty
					break;
				case 0x03: // Receiver Line Status
					innpb(port.base + 5); // Clear Pending
					break;
				case 0x00: // Modem Status
					innpb(port.base + 6); // Clear Pending
					break;
				}
				iir = innpb(port.base + 2);
			}
		}
	}
}

bool SerialCom1Available() {
	return g_com1_available;
}

void SerialInitializeLazyCotVttys() {
	for0a(i, legacy_com_ports) {
		ensure_serial_lazy_vtty(legacy_com_ports[i]);
	}
}
//

#ifdef _UEFI
extern UefiData uefi_data;

#include <cpp/Witch/TextChrome.hpp>
#include "../../depends/desktop.hpp"

// ScreenLog: console fallback shown when no COM1 answers, driven by the kernel's own VideoConsole2
#ifdef _UEFI_AUFDBG
enum { ScreenLogRows = 40 };
static FramebufferInfo screen_log_fb;
static uni::ScreenBridge screen_log_bridge(screen_log_fb);
static uni::BitmapFontEngine screen_log_font(1);
static Rectangle screen_log_win{ Point(0, 0), Size2(0, 0), Color::Black };
static uni::VideoConsole2 screen_log_console(&screen_log_bridge, screen_log_win, Color::Green, Color::Black);
static uni::BufferChar* screen_log_text = nullptr;
static Color* screen_log_line = nullptr;
static Color* screen_log_pixels = nullptr;
static bool screen_log_on_layer = false;

// the console rasterizes into RAM; push the used rows to the framebuffer in one sequential pass
static void ScreenLogPush() {
	if (screen_log_on_layer || !screen_log_pixels) return;
	stduint used = (stduint)screen_log_console.getCursor().y + 1;
	if (used > screen_log_console.getRows()) used = screen_log_console.getRows();
	if (!used) return;
	Rectangle rect{ Point(0, 0), Size2(screen_log_win.width, used * 16), Color::Black };
	screen_log_bridge.DrawPoints(rect, screen_log_pixels);
}

class ScreenLogSink : public OstreamTrait {
public:
	virtual int out(const char* str, stduint len) override {
		const int res = screen_log_console.out(str, len);
		ScreenLogPush();
		return res;
	}
};
static ScreenLogSink screen_log_sink;

_ESYM_C void ScreenLogAttach() {
	if (probe_legacy_uart_scratch(PORT_COM1_DATA)) return;	// a real COM1 keeps the UART console
	if (con0_out == &screen_log_sink) return;
	auto& cfg = uefi_data.frame_buffer_config;
	screen_log_fb.physical_range = Slice{ (stduint)cfg.frame_buffer, cfg.vertical_resolution * cfg.pixels_per_scan_line * 4 };
	screen_log_fb.screen_size = Size2(cfg.horizontal_resolution, cfg.vertical_resolution);
	screen_log_fb.pitch = cfg.pixels_per_scan_line * 4;
	screen_log_fb.bpp = 32;
	screen_log_fb.format = cfg.pixel_format;
	stduint rows = screen_log_fb.screen_size.y / 16;
	if (rows > ScreenLogRows) rows = ScreenLogRows;
	screen_log_win.width = screen_log_fb.screen_size.x;	// full width: DrawPoints steps the source by the screen width
	screen_log_win.height = rows * 16;
	screen_log_console.window = screen_log_win;
	screen_log_console.setFontEngine(&screen_log_font);
	if (!screen_log_text) screen_log_text = new uni::BufferChar[screen_log_console.getCols() * screen_log_console.getRows()];
	if (!screen_log_line) screen_log_line = new Color[screen_log_console.getLineBufferSize()];
	if (!screen_log_pixels) screen_log_pixels = new Color[screen_log_win.getArea()];
	if (!screen_log_text || !screen_log_line || !screen_log_pixels) return;
	screen_log_console.setBuffers(screen_log_pixels, screen_log_text, screen_log_line);
	screen_log_console.Clear();
	screen_log_bridge.DrawRectangle(Rectangle{ Point(0, 0), screen_log_win.getSize(), Color::Black });
	con0_out = &screen_log_sink;
}

_ESYM_C void ScreenLogToLayer() {
	if (con0_out != &screen_log_sink) return;
	auto* desk = uni::global_desktop;
	if (!desk || !desk->sheet_buffer) return;
	if (desk->sheet_area.width != screen_log_win.width) return;	// the desktop canvas must share the log row stride
	screen_log_console.setModeBuffer(desk->sheet_buffer);
	screen_log_console.window = screen_log_win;
	screen_log_console.doshow(nullptr);
	screen_log_on_layer = true;
	auto layman = global_layman.Lock();
	layman->AddDirty(screen_log_win);
	layman->is_dirty = true;
}
#endif
#endif

//

void R_COM1_INIT() {
	for0a(i, legacy_com_ports) {
		auto& port = legacy_com_ports[i];
		port.available = probe_legacy_uart_scratch(port.base);
		if (port.available) {
			initialize_legacy_uart_port(port);
		}
	}
	g_com1_available = legacy_com_ports[0].available;
	if (g_com1_available) {
		con0_out = &com1;
	}
	else {
		// Do not register or enable IRQ for a COM port that did not answer the
		// SCR probe; later console code also checks SerialCom1Available().
		if (Devsman::Root()) plogwarn("[UART] COM1 probe failed at %[16H], skip init", PORT_COM1_DATA);
		ScreenLogAttach();
	}
	// Ensure IRQ_COM13_RS232_P1 maps to IRQ 4 (IDT index 0x24 if base is 0x20) [cite: 282]
	#if _MCCA == 0x8664
	if (serial_irq_has_available_port(IRQ_COM13_RS232_P1)) {
		IC[IRQ_COM13_RS232_P1].setModeRupt(mglb(Handint_COM1_Entry), SegCo64);
		register_interrupt_handler(IRQ_COM13_RS232_P1, Handint_COM1);
	}
	if (serial_irq_has_available_port(IRQ_COM24_Serial)) {
		IC[IRQ_COM24_Serial].setModeRupt(mglb(Handint_COM2_Entry), SegCo64);
		register_interrupt_handler(IRQ_COM24_Serial, Handint_COM2);
	}
	#else
	if (serial_irq_has_available_port(IRQ_COM13_RS232_P1)) {
		IC[IRQ_COM13_RS232_P1].setRange(mglb(Handint_COM1_Entry), SegCo32);
		register_interrupt_handler(IRQ_COM13_RS232_P1, Handint_COM1);
	}
	if (serial_irq_has_available_port(IRQ_COM24_Serial)) {
		IC[IRQ_COM24_Serial].setRange(mglb(Handint_COM2_Entry), SegCo32);
		register_interrupt_handler(IRQ_COM24_Serial, Handint_COM2);
	}
	#endif
	// flap32 prehost calls R_COM1_INIT() before Memory::initialize().
	// Register into device tree only after Devsman has created the tree.
	register_available_legacy_com_ports();
}

void Handint_COM1()
{
	IC.SendEOI(IRQ_COM13_RS232_P1); // Acknowledge interrupt
	drain_serial_interrupts(IRQ_COM13_RS232_P1);
}

void Handint_COM2()
{
	IC.SendEOI(IRQ_COM24_Serial); // Acknowledge interrupt
	drain_serial_interrupts(IRQ_COM24_Serial);
}

#endif
