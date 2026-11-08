// ASCII CPP-ISO11 TAB4 LF
#ifndef TOUCH_GT1XX_HPP_
#define TOUCH_GT1XX_HPP_

#include <cpp/Device/_Touch.hpp>

namespace TouchGt1xx {
	// Call from the owning device thread. The driver owns the returned touch device.
	uni::TouchTrait* Initialize(const uni::Size2& screen);
	// Process acquisition/diagnostics and yield the owner; false means acquisition stopped.
	bool Process();
	uni::TouchControlBlock* GetControlBlock();
	// Call only after the Graphic thread has acknowledged unregistering the block.
	void Shutdown();
}

#endif
