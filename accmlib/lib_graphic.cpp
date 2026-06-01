#include "aaaaa.h"

GraphicForm::GraphicForm(const uni::Rectangle& rect, const char* title) {
	rect_ = rect;
	focus_sheet_ = nullptr;

	// Calculate client area size (subtract borders and title bar)
	stduint client_w = rect.width - 2;
	stduint client_h = rect.height - 19;

	// Allocate framebuffer in user space
	fb_buffer_ = (uni::Color*)malloc(client_w * client_h * sizeof(uni::Color));
	if (!fb_buffer_) {
		form_id_ = -1;
		pvci_ = nullptr;
		playman_ = nullptr;
		return;
	}

	// Create form in kernel space
	form_id_ = sys_create_form(-_IMM0, &rect_);
	if (form_id_ >= 0) {
		sys_set_form_buffer(form_id_, fb_buffer_);

		// Set default background (Solarized Light warm background: 0xFFFDF6E3)
		uni::Color bg_color = uni::Color::FromRGB888(0xFFFDF6E3);
		for (stduint i = 0; i < client_w * client_h; i++) {
			fb_buffer_[i] = bg_color;
		}
		sys_update_form(form_id_, nullptr);
	} else {
		free(fb_buffer_);
		fb_buffer_ = nullptr;
		pvci_ = nullptr;
		playman_ = nullptr;
		return;
	}

	// Initialize user-space MARGB8888 rendering interface and LayerManager as client area container
	pvci_ = new uni::VideoControlInterfaceMARGB8888(fb_buffer_, uni::Size2(client_w, client_h));
	playman_ = new uni::LayerManager(pvci_, uni::Rectangle(uni::Point(0, 0), uni::Size2(client_w, client_h)));
}

GraphicForm::~GraphicForm() {
	// Clean up user-space LayerManager and VCI
	if (playman_) {
		delete playman_;
	}
	if (pvci_) {
		delete pvci_;
	}

	// Close kernel-side Form and free user-space framebuffer
	if (form_id_ >= 0) {
		sys_close_form(form_id_);
	}
	if (fb_buffer_) {
		free(fb_buffer_);
	}
}

stdsint GraphicForm::getFormId() const {
	return form_id_;
}

uni::Color* GraphicForm::getBuffer() const {
	return fb_buffer_;
}

uni::LayerManager& GraphicForm::getLayerManager() {
	return *playman_;
}

stduint GraphicForm::getClientWidth() const {
	return rect_.width - 2;
}

stduint GraphicForm::getClientHeight() const {
	return rect_.height - 19;
}

void GraphicForm::HandleEvent(const uni::SheetMessage& smsg) {
	if (!playman_) return;

	if (smsg.event == uni::SheetEvent::onTimer) {
		playman_->CheckTimers(smsg.args[3]);
	}
	else if (smsg.event == uni::SheetEvent::onKeybd) {
		if (focus_sheet_) {
			keyboard_event_t kbd_event = *(keyboard_event_t*)smsg.args;
			focus_sheet_->onrupt(uni::SheetEvent::onKeybd, uni::Point(0, 0), &kbd_event);
		}
	}
	else {
		// Coordinate translation: Convert window-relative coordinates to client-relative coordinates
		// Top border is 18 pixels (17 title bar + 1 border), left border is 1 pixel
		uni::Point translated_p(smsg.args[0] - 1, smsg.args[1] - 18);

		if (smsg.event == uni::SheetEvent::onClick) {
			uni::SheetTrait* clicked = playman_->getTop(translated_p);
			if (clicked) {
				focus_sheet_ = clicked;
			}
		}

		// Dispatch to user-space LayerManager
		playman_->onrupt(smsg.event, translated_p, smsg.args[2], smsg.args[3]);
	}

	// If user-space controls produced graphical changes, trigger kernel compositor update
	if (playman_->is_dirty) {
		sys_update_form(form_id_, &playman_->dirty_area);
		playman_->is_dirty = false;
		playman_->dirty_area = {};
	}
}

void GraphicForm::DrawString(const uni::Point& vertex, const char* str, uni::Color col) {
	sys_draw_default_string(form_id_, vertex, str, col);
}
