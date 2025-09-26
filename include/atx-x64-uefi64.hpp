extern FrameBufferConfig config_graph;

class GloScreenARGB8888 : public VideoControlInterface {
	inline uint32& Locate(const Point& disp) const {
		return *((uint32*)(config_graph.frame_buffer) + disp.x + disp.y * config_graph.pixels_per_scan_line);// without boundary check
	}
public:
	virtual ~GloScreenARGB8888() = default;
	GloScreenARGB8888() {}
	virtual void SetCursor(const Point& disp) const override;
	virtual Point GetCursor() const override;

	// without boundary check
	virtual void DrawPoint(const Point& disp, Color color) const override;
	virtual void DrawRectangle(const Rectangle& rect) const override;

	//
	virtual void DrawFont(const Point& disp, const DisplayFont& font) const override;
	virtual Color GetColor(Point p) const override;
};


class GloScreenABGR8888 : public VideoControlInterface {
	inline uint32& Locate(const Point& disp) const {
		return *((uint32*)(config_graph.frame_buffer) + disp.x + disp.y * config_graph.pixels_per_scan_line);// without boundary check
	}
public:
	virtual ~GloScreenABGR8888() {}
	//GloScreenABGR8888(const FrameBufferConfig& cfg) : config(cfg) {}
	virtual void SetCursor(const Point& disp) const override;
	virtual Point GetCursor() const override;

	// without boundary check
	virtual void DrawPoint(const Point& disp, Color color) const override;
	virtual void DrawRectangle(const Rectangle& rect) const;

	//
	virtual void DrawFont(const Point& disp, const DisplayFont& font) const override;
	virtual Color GetColor(Point p) const override;
};

