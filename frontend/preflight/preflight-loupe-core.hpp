#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace preflight::loupe {

struct PixelFrame {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t stride = 0;
	uint64_t capture_timestamp_ns = 0;
	std::vector<uint8_t> rgba;
};

struct CropRegion {
	uint32_t source_width = 0;
	uint32_t source_height = 0;
	uint32_t left = 0;
	uint32_t top = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct PixelRect {
	uint32_t left = 0;
	uint32_t top = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

std::optional<CropRegion> make_crop_region(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t source_width,
					   uint32_t source_height);
std::optional<PixelRect> project_crop(const CropRegion &region, uint32_t frame_width, uint32_t frame_height);
std::optional<PixelFrame> crop_at_4x(const PixelFrame &frame, const CropRegion &region);

struct DecoderConfig {
	std::string codec_name;
	uint32_t width = 0;
	uint32_t height = 0;
	std::string pixel_format;
	std::vector<uint8_t> extra_data;
};

struct EncodedVideoPacket {
	std::vector<uint8_t> bytes;
	int64_t pts = 0;
	int64_t dts = 0;
	int32_t timebase_num = 0;
	int32_t timebase_den = 0;
	uint64_t capture_timestamp_ns = 0;
	bool keyframe = false;
};

class EncodedVideoDecoder {
public:
	EncodedVideoDecoder();
	~EncodedVideoDecoder();
	EncodedVideoDecoder(const EncodedVideoDecoder &) = delete;
	EncodedVideoDecoder &operator=(const EncodedVideoDecoder &) = delete;

	bool Open(const DecoderConfig &config);
	void Close();
	bool Decode(const EncodedVideoPacket &packet, std::vector<PixelFrame> &decoded_frames);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

struct FramePair {
	std::shared_ptr<const PixelFrame> source;
	std::shared_ptr<const PixelFrame> encoded;
};

class FramePairer {
public:
	explicit FramePairer(size_t source_capacity = 8);
	void Reset();
	void PushSource(std::shared_ptr<const PixelFrame> source);
	std::optional<FramePair> Match(std::shared_ptr<const PixelFrame> encoded, uint64_t tolerance_ns);

private:
	size_t source_capacity;
	std::vector<std::shared_ptr<const PixelFrame>> source_frames;
};

} // namespace preflight::loupe
