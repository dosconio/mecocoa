// ASCII C++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Desktop Layer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#pragma once

#include "../include/mecocoa.hpp"

namespace uni {

	class Desktop : public SheetTrait {
	public:
		// Windows 2000 Professional default classic desktop background: RGB(58, 110, 165)
		static constexpr uint32 kDefaultBgColor = 0xFF3A6EA5;

	protected:
		Color bg_color = 0xFF3A6EA5;

	public:
		Desktop(Color color = 0xFF3A6EA5);
		virtual ~Desktop();

		void Initialize(LayerManager& layman, const Rectangle& area, Color color = 0xFF3A6EA5);
		void Reconfigure(LayerManager& layman, const Rectangle& area);

		// SheetTrait overrides
		virtual void doshow(void* buf) override;
		virtual void onrupt(SheetEvent event, Point rel_p, ...) override;

		void SetBackgroundColor(Color color);
		Color GetBackgroundColor() const { return bg_color; }

		bool SetWallpaper(const Color* pixels, Size2 size);
	};

	extern Desktop* global_desktop;

} // namespace uni
