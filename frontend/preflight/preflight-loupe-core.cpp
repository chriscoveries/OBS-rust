#include "preflight-loupe-core.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <unordered_map>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace preflight::loupe {
namespace {

constexpr uint64_t MAX_CAPTURE_TIMESTAMPS = 128;
constexpr uint64_t MAX_ZOOM_IMAGE_BYTES = 64 * 1024 * 1024;
constexpr uint64_t MAX_RGBA_FRAME_BYTES = 64 * 1024 * 1024;

bool rgba_storage_is_valid(const PixelFrame &frame)
{
	if (frame.width == 0 || frame.height == 0 || frame.width > UINT32_MAX / 4 || frame.stride < frame.width * 4) {
		return false;
	}
	const uint64_t row_bytes = static_cast<uint64_t>(frame.width) * 4;
	const uint64_t preceding_rows = frame.height - 1;
	if (row_bytes > MAX_RGBA_FRAME_BYTES || preceding_rows > (MAX_RGBA_FRAME_BYTES - row_bytes) / frame.stride) {
		return false;
	}
	const uint64_t last_row_end = preceding_rows * frame.stride + row_bytes;
	return last_row_end <= frame.rgba.size();
}

uint64_t scale_floor(uint64_t value, uint32_t numerator, uint32_t denominator)
{
	return value * numerator / denominator;
}

uint64_t scale_ceil(uint64_t value, uint32_t numerator, uint32_t denominator)
{
	const uint64_t scaled = value * numerator;
	return scaled / denominator + (scaled % denominator != 0);
}

} // namespace

std::optional<CropRegion> make_crop_region(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t source_width,
					   uint32_t source_height)
{
	if (source_width == 0 || source_height == 0) {
		return std::nullopt;
	}
	const int64_t max_x = source_width;
	const int64_t max_y = source_height;
	const int64_t left = std::clamp<int64_t>(std::min<int64_t>(x0, x1), 0, max_x);
	const int64_t top = std::clamp<int64_t>(std::min<int64_t>(y0, y1), 0, max_y);
	const int64_t right = std::clamp<int64_t>(std::max<int64_t>(x0, x1), 0, max_x);
	const int64_t bottom = std::clamp<int64_t>(std::max<int64_t>(y0, y1), 0, max_y);
	if (right <= left || bottom <= top) {
		return std::nullopt;
	}
	return CropRegion{source_width,
			  source_height,
			  static_cast<uint32_t>(left),
			  static_cast<uint32_t>(top),
			  static_cast<uint32_t>(right - left),
			  static_cast<uint32_t>(bottom - top)};
}

std::optional<PixelRect> project_crop(const CropRegion &region, uint32_t frame_width, uint32_t frame_height)
{
	if (region.source_width == 0 || region.source_height == 0 || region.width == 0 || region.height == 0 ||
	    frame_width == 0 || frame_height == 0 ||
	    static_cast<uint64_t>(region.left) + region.width > region.source_width ||
	    static_cast<uint64_t>(region.top) + region.height > region.source_height) {
		return std::nullopt;
	}

	const uint64_t left = scale_floor(region.left, frame_width, region.source_width);
	const uint64_t top = scale_floor(region.top, frame_height, region.source_height);
	const uint64_t right =
		scale_ceil(static_cast<uint64_t>(region.left) + region.width, frame_width, region.source_width);
	const uint64_t bottom =
		scale_ceil(static_cast<uint64_t>(region.top) + region.height, frame_height, region.source_height);
	const uint64_t bounded_left = std::min<uint64_t>(left, frame_width - 1);
	const uint64_t bounded_top = std::min<uint64_t>(top, frame_height - 1);
	const uint64_t bounded_right = std::clamp<uint64_t>(right, bounded_left + 1, frame_width);
	const uint64_t bounded_bottom = std::clamp<uint64_t>(bottom, bounded_top + 1, frame_height);
	return PixelRect{static_cast<uint32_t>(bounded_left), static_cast<uint32_t>(bounded_top),
			 static_cast<uint32_t>(bounded_right - bounded_left),
			 static_cast<uint32_t>(bounded_bottom - bounded_top)};
}

std::optional<PixelFrame> crop_at_4x(const PixelFrame &frame, const CropRegion &region)
{
	if (!rgba_storage_is_valid(frame)) {
		return std::nullopt;
	}
	std::optional<PixelRect> crop;
	const uint32_t logical_width = frame.source_width ? frame.source_width : frame.width;
	const uint32_t logical_height = frame.source_height ? frame.source_height : frame.height;
	const bool detail_matches_source = frame.source_width != 0 && frame.source_height != 0 &&
					   region.source_width == frame.source_width &&
					   region.source_height == frame.source_height;
	const bool detail_contains_region =
		detail_matches_source && frame.detail_region.width != 0 && frame.detail_region.height != 0 &&
		region.left >= frame.detail_region.left && region.top >= frame.detail_region.top &&
		static_cast<uint64_t>(region.left) + region.width <=
			static_cast<uint64_t>(frame.detail_region.left) + frame.detail_region.width &&
		static_cast<uint64_t>(region.top) + region.height <=
			static_cast<uint64_t>(frame.detail_region.top) + frame.detail_region.height;
	if (detail_contains_region && frame.detail_region.width <= UINT32_MAX / 4 &&
	    frame.detail_stride >= frame.detail_region.width * 4) {
		const uint64_t required =
			static_cast<uint64_t>(frame.detail_stride) * (frame.detail_region.height - 1) +
			static_cast<uint64_t>(frame.detail_region.width) * 4;
		if (required <= frame.detail_rgba.size() && required <= MAX_RGBA_FRAME_BYTES) {
			crop = PixelRect{region.left - frame.detail_region.left, region.top - frame.detail_region.top,
					 region.width, region.height};
		}
	}
	const bool use_detail = crop.has_value();
	if (!crop) {
		crop = project_crop(region, frame.width, frame.height);
	}
	if (!crop || crop->width > UINT32_MAX / 16 || crop->height > UINT32_MAX / 4 || logical_width == 0 ||
	    logical_height == 0) {
		return std::nullopt;
	}

	PixelFrame result;
	result.width = crop->width * 4;
	result.height = crop->height * 4;
	const uint64_t output_stride = static_cast<uint64_t>(result.width) * 4;
	if (output_stride > UINT32_MAX) {
		return std::nullopt;
	}
	result.stride = static_cast<uint32_t>(output_stride);
	result.capture_timestamp_ns = frame.capture_timestamp_ns;
	const uint64_t output_size = output_stride * result.height;
	if (output_size > std::numeric_limits<size_t>::max() || output_size > MAX_ZOOM_IMAGE_BYTES) {
		return std::nullopt;
	}
	result.rgba.resize(static_cast<size_t>(output_size));

	for (uint32_t y = 0; y < crop->height; ++y) {
		for (uint32_t x = 0; x < crop->width; ++x) {
			const uint32_t source_y = crop->top + y;
			const uint32_t source_x = crop->left + x;
			const uint32_t source_stride = use_detail ? frame.detail_stride : frame.stride;
			const auto &source_rgba = use_detail ? frame.detail_rgba : frame.rgba;
			const size_t source_offset = static_cast<size_t>(source_y) * source_stride + source_x * 4;
			const uint8_t *pixel = source_rgba.data() + source_offset;
			for (uint32_t zoom_y = 0; zoom_y < 4; ++zoom_y) {
				for (uint32_t zoom_x = 0; zoom_x < 4; ++zoom_x) {
					const size_t output_offset =
						static_cast<size_t>(y * 4 + zoom_y) * result.stride +
						(x * 4 + zoom_x) * 4;
					std::memcpy(result.rgba.data() + output_offset, pixel, 4);
				}
			}
		}
	}
	return result;
}

struct EncodedVideoDecoder::Impl {
	AVCodecContext *context = nullptr;
	AVPacket *packet = nullptr;
	AVFrame *frame = nullptr;
	SwsContext *scaler = nullptr;
	std::unordered_map<int64_t, uint64_t> capture_timestamps;
	std::deque<int64_t> capture_order;

	void Close()
	{
		sws_freeContext(scaler);
		scaler = nullptr;
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&context);
		capture_timestamps.clear();
		capture_order.clear();
	}

	bool RememberCaptureTimestamp(int64_t pts, uint64_t timestamp_ns)
	{
		if (pts == AV_NOPTS_VALUE || timestamp_ns == 0) {
			return false;
		}
		capture_timestamps[pts] = timestamp_ns;
		capture_order.push_back(pts);
		while (capture_order.size() > MAX_CAPTURE_TIMESTAMPS) {
			const int64_t oldest = capture_order.front();
			capture_order.pop_front();
			if (std::find(capture_order.begin(), capture_order.end(), oldest) == capture_order.end()) {
				capture_timestamps.erase(oldest);
			}
		}
		return true;
	}

	std::optional<uint64_t> TakeCaptureTimestamp(int64_t pts)
	{
		const auto found = capture_timestamps.find(pts);
		if (found == capture_timestamps.end()) {
			return std::nullopt;
		}
		const uint64_t timestamp = found->second;
		capture_timestamps.erase(found);
		capture_order.erase(std::remove(capture_order.begin(), capture_order.end(), pts), capture_order.end());
		return timestamp;
	}

	bool ConvertDecodedFrame(std::vector<PixelFrame> &decoded_frames)
	{
		const int64_t frame_pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
		const auto capture_timestamp = TakeCaptureTimestamp(frame_pts);
		if (!capture_timestamp || frame->width <= 0 || frame->height <= 0 || frame->width > INT_MAX / 4) {
			return true;
		}

		const auto pixel_format = static_cast<AVPixelFormat>(frame->format);
		scaler = sws_getCachedContext(scaler, frame->width, frame->height, pixel_format, frame->width,
					      frame->height, AV_PIX_FMT_RGBA, SWS_POINT, nullptr, nullptr, nullptr);
		if (!scaler) {
			return false;
		}

		PixelFrame decoded;
		decoded.width = static_cast<uint32_t>(frame->width);
		decoded.height = static_cast<uint32_t>(frame->height);
		decoded.stride = decoded.width * 4;
		decoded.capture_timestamp_ns = *capture_timestamp;
		const uint64_t size = static_cast<uint64_t>(decoded.stride) * decoded.height;
		if (size > std::numeric_limits<size_t>::max() || size > MAX_RGBA_FRAME_BYTES) {
			return false;
		}
		decoded.rgba.resize(static_cast<size_t>(size));
		uint8_t *destination[] = {decoded.rgba.data()};
		const int destination_stride[] = {static_cast<int>(decoded.stride)};
		if (sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, destination,
			      destination_stride) != frame->height) {
			return false;
		}
		decoded_frames.push_back(std::move(decoded));
		return true;
	}
};

EncodedVideoDecoder::EncodedVideoDecoder() : impl(std::make_unique<Impl>()) {}
EncodedVideoDecoder::~EncodedVideoDecoder() = default;

bool EncodedVideoDecoder::Open(const DecoderConfig &config)
{
	Close();
	if (config.codec_name.empty() || config.width == 0 || config.height == 0 || config.width > INT_MAX ||
	    config.height > INT_MAX ||
	    config.extra_data.size() > static_cast<size_t>(INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)) {
		return false;
	}
	const AVCodec *codec = avcodec_find_decoder_by_name(config.codec_name.c_str());
	if (!codec) {
		return false;
	}
	impl->context = avcodec_alloc_context3(codec);
	if (!impl->context) {
		return false;
	}
	impl->context->width = static_cast<int>(config.width);
	impl->context->height = static_cast<int>(config.height);
	impl->context->thread_count = 1;
	impl->context->thread_type = 0;
	if (!config.pixel_format.empty()) {
		const AVPixelFormat pixel_format = av_get_pix_fmt(config.pixel_format.c_str());
		if (pixel_format == AV_PIX_FMT_NONE) {
			Close();
			return false;
		}
		impl->context->pix_fmt = pixel_format;
	}
	if (!config.extra_data.empty()) {
		const size_t padded_size = config.extra_data.size() + AV_INPUT_BUFFER_PADDING_SIZE;
		impl->context->extradata = static_cast<uint8_t *>(av_mallocz(padded_size));
		if (!impl->context->extradata) {
			Close();
			return false;
		}
		std::memcpy(impl->context->extradata, config.extra_data.data(), config.extra_data.size());
		impl->context->extradata_size = static_cast<int>(config.extra_data.size());
	}
	if (avcodec_open2(impl->context, codec, nullptr) < 0) {
		Close();
		return false;
	}
	impl->packet = av_packet_alloc();
	impl->frame = av_frame_alloc();
	if (!impl->packet || !impl->frame) {
		Close();
		return false;
	}
	return true;
}

void EncodedVideoDecoder::Close()
{
	impl->Close();
}

bool EncodedVideoDecoder::Decode(const EncodedVideoPacket &packet, std::vector<PixelFrame> &decoded_frames)
{
	if (!impl->context || !impl->packet || !impl->frame || packet.bytes.empty() || packet.bytes.size() > INT_MAX ||
	    packet.timebase_num <= 0 || packet.timebase_den <= 0) {
		return false;
	}
	impl->context->pkt_timebase = AVRational{packet.timebase_num, packet.timebase_den};
	impl->RememberCaptureTimestamp(packet.pts, packet.capture_timestamp_ns);
	av_packet_unref(impl->packet);
	if (av_new_packet(impl->packet, static_cast<int>(packet.bytes.size())) < 0) {
		return false;
	}
	std::memcpy(impl->packet->data, packet.bytes.data(), packet.bytes.size());
	impl->packet->pts = packet.pts;
	impl->packet->dts = packet.dts;
	if (packet.keyframe) {
		impl->packet->flags |= AV_PKT_FLAG_KEY;
	}
	if (avcodec_send_packet(impl->context, impl->packet) < 0) {
		return false;
	}

	while (true) {
		const int result = avcodec_receive_frame(impl->context, impl->frame);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
			return true;
		}
		if (result < 0) {
			return false;
		}
		const bool converted = impl->ConvertDecodedFrame(decoded_frames);
		av_frame_unref(impl->frame);
		if (!converted) {
			return false;
		}
	}
}

FramePairer::FramePairer(size_t source_capacity) : source_capacity(source_capacity) {}

void FramePairer::SetSourceCapacity(size_t source_capacity_)
{
	source_capacity = source_capacity_;
	if (source_frames.size() > source_capacity) {
		source_frames.erase(source_frames.begin(),
				    source_frames.begin() +
					    static_cast<std::ptrdiff_t>(source_frames.size() - source_capacity));
	}
}

void FramePairer::Reset()
{
	source_frames.clear();
}

void FramePairer::PushSource(std::shared_ptr<const PixelFrame> source)
{
	if (!source || source_capacity == 0 || !rgba_storage_is_valid(*source)) {
		return;
	}
	source_frames.push_back(std::move(source));
	if (source_frames.size() > source_capacity) {
		source_frames.erase(source_frames.begin(),
				    source_frames.begin() +
					    static_cast<std::ptrdiff_t>(source_frames.size() - source_capacity));
	}
}

std::optional<FramePair> FramePairer::Match(std::shared_ptr<const PixelFrame> encoded, uint64_t tolerance_ns)
{
	if (!encoded || !rgba_storage_is_valid(*encoded)) {
		return std::nullopt;
	}
	size_t best_index = source_frames.size();
	uint64_t best_delta = std::numeric_limits<uint64_t>::max();
	for (size_t index = 0; index < source_frames.size(); ++index) {
		const uint64_t source_time = source_frames[index]->capture_timestamp_ns;
		const uint64_t encoded_time = encoded->capture_timestamp_ns;
		const uint64_t delta = source_time > encoded_time ? source_time - encoded_time
								  : encoded_time - source_time;
		if (delta < best_delta) {
			best_delta = delta;
			best_index = index;
		}
	}
	if (best_index == source_frames.size() || best_delta > tolerance_ns) {
		return std::nullopt;
	}
	FramePair pair{source_frames[best_index], std::move(encoded)};
	return pair;
}

} // namespace preflight::loupe
