// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Graphic - Touchpad and Touch Controller
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/console.hpp"
#include <cpp/Device/_Touch.hpp>

#if !_GUI_ENABLE
bool Consman::RegisterTouchDevice(uni::TouchControlBlock*) { return false; }
void Consman::UnregisterTouchDevice(uni::TouchControlBlock*) {}
bool Consman::IsTouchDeviceRegistered(uni::TouchControlBlock*) { return false; }
#endif

#if _GUI_ENABLE

namespace {
	struct TouchGuiBinding {
		uni::TouchControlBlock* control = nullptr;
		TouchGuiBinding* next = nullptr;
		bool pressed = false;
		uint32 reported_overflows = 0;
		stduint last_overflow_log = 0;
	};
	Spinlock touch_handoff_lock;
	TouchGuiBinding* touch_devices = nullptr;
	byte graphic_mouse_buttons = 0;// last mouse-only state consumed by the Graphic thread

	stduint EnterTouchHandoff(void*) { return touch_handoff_lock.Acquire(); }
	void LeaveTouchHandoff(void*, stduint state) { touch_handoff_lock.Release(bool(state)); }

	byte CombinedPointerButtons() {
		byte buttons = graphic_mouse_buttons;
		SpinlockLocal guard(&touch_handoff_lock);
		for (auto* binding = touch_devices; binding; binding = binding->next) if (binding->pressed) buttons |= 1;
		return buttons;
	}

	void ApplyPointerButtons(MouseMessage& message, byte buttons) {
		message.BtnLeft = (buttons & 1) != 0;
		message.BtnRight = (buttons & 2) != 0;
		message.BtnMiddle = (buttons & 4) != 0;
	}

	void MoveTouchPointer(const uni::Point& position) {
		const auto screen = sys_framebuffer.screen_size;
		if (!screen.x || !screen.y) return;
		const auto current = Cursor::global_cursor->GetScreenPosition();
		const stdsint dx = stdsint(minof(position.x, screen.x - 1)) - stdsint(current.x);
		const stdsint dy = stdsint(minof(position.y, screen.y - 1)) - stdsint(current.y);
		if (!dx && !dy) return;
		MouseMessage movement{};
		movement.BtnLeft = Cursor::mouse_btnl_dn;
		movement.BtnRight = Cursor::mouse_btnr_dn;
		movement.BtnMiddle = Cursor::mouse_btnm_dn;
		HandlePointerMessage(movement, dx, dy);
	}

	void HandleTouchPointer(const uni::TouchEvent& event, void* context) {
		auto& binding = *static_cast<TouchGuiBinding*>(context);
		if (!Consman::ento_gui || !Cursor::global_cursor) {
			if (event.phase == uni::TouchPhase::Up || event.phase == uni::TouchPhase::Cancel) binding.pressed = false;
			return;
		}
		if (event.phase != uni::TouchPhase::Cancel) MoveTouchPointer(event.contact.position);
		if (event.phase == uni::TouchPhase::Move) return;
		binding.pressed = event.phase == uni::TouchPhase::Down;
		const byte buttons = CombinedPointerButtons();
		if (event.phase == uni::TouchPhase::Cancel && !(buttons & 1)) {
			Cursor::moving_sheet = nullptr;
			if (Cursor::resizing_sheet) {
				const Rectangle ghost = Cursor::ghost_rect;
				Cursor::resizing_sheet = nullptr;
				Cursor::ghost_rect = Rectangle();
				Cursor::global_cursor->setType(uni::CursorType::Normal);
				global_layman.Lock()->Update(nullptr, ghost);
			}
		}
		MouseMessage message{};
		ApplyPointerButtons(message, buttons);
		hand_mouse(message);
	}

}

void ConsumeMouseForGraphic(MouseMessage message) {
	graphic_mouse_buttons = byte((message.BtnLeft ? 1 : 0) | (message.BtnRight ? 2 : 0) | (message.BtnMiddle ? 4 : 0));
	ApplyPointerButtons(message, CombinedPointerButtons());
	hand_mouse(message);
}

bool PumpTouchDevices() {
	TouchGuiBinding* binding;
	{
		SpinlockLocal guard(&touch_handoff_lock);
		binding = touch_devices;
	}
	bool worked = false;
	while (binding) {
		TouchGuiBinding* next;
		{
			SpinlockLocal guard(&touch_handoff_lock);
			next = binding->next;
		}
		auto* control = binding->control;
		const uint32 overflows = control->getOverflowCount();
		if (overflows != binding->reported_overflows && (!binding->reported_overflows || stduint(tick - binding->last_overflow_log) >= CONFIG_SysTickFreq)) {
			plogwarn("[TOUCH] frame queue overflow; gesture cancelled count=%u", (unsigned)overflows);
			binding->reported_overflows = overflows;
			binding->last_overflow_log = tick;
		}
		uni::TouchCancelReason reason;
		if (control->TakeCancel(reason)) { control->CancelActiveTouches(reason); worked = true; }
		uni::TouchFrameToken token;
		uni::TouchFrameView frame;
		if (control->TakeFrame(token, frame)) {
			control->ProcessFrame(frame);
			control->ReleaseFrame(token);
			worked = true;
		}
		if (control->isDetaching() && control->isDrained()) {
			control->setPointerObserver(nullptr);
			{
				SpinlockLocal guard(&touch_handoff_lock);
				TouchGuiBinding** link = &touch_devices;
				while (*link && *link != binding) link = &(*link)->next;
				if (*link) *link = binding->next;
			}
			delete binding;
			worked = true;
		}
		binding = next;
	}
	return worked;
}

bool Consman::RegisterTouchDevice(uni::TouchControlBlock* control) {
	if (!control || !control->isInitialized() || control->isDetaching()) return false;
	uni::UniquePtr<TouchGuiBinding> binding(new (std::nothrow_t{}) TouchGuiBinding);
	if (!binding) return false;
	SpinlockLocal guard(&touch_handoff_lock);
	for (auto* item = touch_devices; item; item = item->next) if (item->control == control) return false;
	if (!control->setSynchronization(EnterTouchHandoff, LeaveTouchHandoff, nullptr)) return false;
	binding->control = control;
	control->setPointerObserver(HandleTouchPointer, binding.get());
	binding->next = touch_devices;
	touch_devices = binding.release();
	return true;
}

void Consman::UnregisterTouchDevice(uni::TouchControlBlock* control) {
	if (control) control->BeginDetach();
}

bool Consman::IsTouchDeviceRegistered(uni::TouchControlBlock* control) {
	SpinlockLocal guard(&touch_handoff_lock);
	for (auto* item = touch_devices; item; item = item->next) if (item->control == control) return true;
	return false;
}

#endif // _GUI_ENABLE
