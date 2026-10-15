#include "../preflight/preflight-loupe-core.hpp"

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace preflight::loupe;

static void require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static PixelFrame rgba_frame(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgba, uint64_t timestamp)
{
	return {width, height, width * 4, timestamp, rgba};
}

static void crop_selection_clamps_drag_to_frame_edges()
{
	auto region = make_crop_region(-2, 1, 4, 5, 4, 4);
	require(region.has_value(), "clamped crop should be non-empty");
	require(region->left == 0 && region->top == 1 && region->width == 4 && region->height == 3,
		"crop drag should clamp both corners to the frame");
}

static void crop_projection_preserves_normalized_area()
{
	const auto region = make_crop_region(1, 1, 3, 3, 4, 4);
	require(region.has_value(), "source crop should be valid");
	const auto projected = project_crop(*region, 8, 6);
	require(projected.has_value(), "projected crop should be valid");
	require(projected->left == 2 && projected->top == 1 && projected->width == 4 && projected->height == 4,
		"crop should cover the same normalized area at another resolution");
}

static void four_x_crop_uses_nearest_neighbor_pixels()
{
	const std::vector<uint8_t> pixels = {
		255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
	};
	const PixelFrame frame = rgba_frame(2, 2, pixels, 17);
	const auto region = make_crop_region(0, 0, 2, 2, 2, 2);
	const auto crop = crop_at_4x(frame, *region);
	require(crop.has_value(), "valid frame should crop");
	require(crop->width == 8 && crop->height == 8 && crop->stride == 32,
		"four-times crop should scale dimensions and stride");
	const auto pixel_at = [&crop](uint32_t x, uint32_t y) {
		const size_t offset = static_cast<size_t>(y) * crop->stride + x * 4;
		return std::array<uint8_t, 4>{crop->rgba[offset], crop->rgba[offset + 1], crop->rgba[offset + 2],
					      crop->rgba[offset + 3]};
	};
	require(pixel_at(0, 0) == std::array<uint8_t, 4>{255, 0, 0, 255}, "top-left source pixel should expand");
	require(pixel_at(3, 3) == std::array<uint8_t, 4>{255, 0, 0, 255}, "each source pixel should fill 4 by 4");
	require(pixel_at(4, 0) == std::array<uint8_t, 4>{0, 255, 0, 255}, "adjacent source pixel should stay sharp");
	require(pixel_at(7, 7) == std::array<uint8_t, 4>{255, 255, 255, 255},
		"bottom-right source pixel should expand");
}

static void crop_rejects_incomplete_rgba_storage()
{
	const PixelFrame frame{2, 2, 8, 0, {0, 1, 2, 3}};
	const auto region = make_crop_region(0, 0, 2, 2, 2, 2);
	require(!crop_at_4x(frame, *region), "truncated source pixel storage must be rejected");
}

static void decoder_returns_rgba_from_headless_rawvideo_packet()
{
	EncodedVideoDecoder decoder;
	DecoderConfig config{"rawvideo", 2, 2, "rgba", {}};
	require(decoder.Open(config), "FFmpeg rawvideo decoder should open headlessly");
	const std::vector<uint8_t> expected = {
		10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255,
	};
	EncodedVideoPacket packet{expected, 12, 12, 1, 30, 900'000'000, true};
	std::vector<PixelFrame> decoded;
	require(decoder.Decode(packet, decoded), "decoder should accept a valid rawvideo packet");
	require(decoded.size() == 1, "one rawvideo packet should yield one decoded frame");
	require(decoded.front().width == 2 && decoded.front().height == 2,
		"decoded dimensions should match the packet");
	require(decoded.front().capture_timestamp_ns == packet.capture_timestamp_ns,
		"decoded frame should retain its matching OBS capture timestamp");
	require(decoded.front().rgba == expected, "decoded RGBA pixels should match the packet");
}

static void frame_pairing_follows_capture_time_when_decode_order_is_reordered()
{
	FramePairer pairer(4);
	for (uint64_t timestamp : {100ULL, 200ULL, 300ULL}) {
		pairer.PushSource(std::make_shared<PixelFrame>(rgba_frame(1, 1, {0, 0, 0, 255}, timestamp)));
	}
	for (uint64_t timestamp : {300ULL, 100ULL, 200ULL}) {
		auto decoded = std::make_shared<PixelFrame>(rgba_frame(1, 1, {1, 1, 1, 255}, timestamp));
		const auto pair = pairer.Match(decoded, 0);
		require(pair.has_value(), "reordered decoded output should find the matching source frame");
		require(pair->source->capture_timestamp_ns == timestamp && pair->encoded == decoded,
			"pair should match by capture time instead of packet arrival order");
	}
}

static void frame_pairing_rejects_stale_decoded_frames()
{
	FramePairer pairer;
	pairer.PushSource(std::make_shared<PixelFrame>(rgba_frame(1, 1, {0, 0, 0, 255}, 100)));
	const auto decoded = std::make_shared<PixelFrame>(rgba_frame(1, 1, {1, 1, 1, 255}, 1'000));
	require(!pairer.Match(decoded, 50), "frames outside the pairing tolerance must not be compared");
}

int main()
{
	const std::vector<std::pair<const char *, std::function<void()>>> cases = {
		{"crop_selection_clamps_drag_to_frame_edges", crop_selection_clamps_drag_to_frame_edges},
		{"crop_projection_preserves_normalized_area", crop_projection_preserves_normalized_area},
		{"four_x_crop_uses_nearest_neighbor_pixels", four_x_crop_uses_nearest_neighbor_pixels},
		{"crop_rejects_incomplete_rgba_storage", crop_rejects_incomplete_rgba_storage},
		{"decoder_returns_rgba_from_headless_rawvideo_packet",
		 decoder_returns_rgba_from_headless_rawvideo_packet},
		{"frame_pairing_follows_capture_time_when_decode_order_is_reordered",
		 frame_pairing_follows_capture_time_when_decode_order_is_reordered},
		{"frame_pairing_rejects_stale_decoded_frames", frame_pairing_rejects_stale_decoded_frames},
	};

	int failures = 0;
	for (const auto &[name, test] : cases) {
		try {
			test();
			std::cout << "PASS " << name << '\n';
		} catch (const std::exception &error) {
			std::cerr << "FAIL " << name << ": " << error.what() << '\n';
			++failures;
		}
	}
	return failures == 0 ? 0 : 1;
}
