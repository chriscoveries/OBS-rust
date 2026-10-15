#include "preflight-loupe-capture.hpp"

#include <obs-encoder.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t RAW_VIDEO_FRAME_RATE_DIVISOR = 4;
constexpr size_t MAX_QUEUED_PACKETS = 8;
constexpr size_t MAX_PACKET_BYTES = 8 * 1024 * 1024;
constexpr uint64_t MAX_RAW_FRAME_BYTES = 64 * 1024 * 1024;
constexpr size_t MAX_SOURCE_HISTORY_FRAMES = 16;
constexpr uint64_t MAX_SOURCE_HISTORY_BYTES = 64 * 1024 * 1024;

} // namespace

PreflightLoupeCapture::~PreflightLoupeCapture()
{
	Stop();
}

bool PreflightLoupeCapture::Start(obs_output_t *output_, obs_encoder_t *encoder)
{
	Stop();
	if (!output_ || !encoder) {
		std::lock_guard lock(mutex);
		status = "Loupe is waiting for a video encoder.";
		return false;
	}
	const char *codec_name = obs_encoder_get_codec(encoder);
	const uint32_t encoded_width = obs_encoder_get_width(encoder);
	const uint32_t encoded_height = obs_encoder_get_height(encoder);
	if (!codec_name || !*codec_name || encoded_width == 0 || encoded_height == 0) {
		std::lock_guard lock(mutex);
		status = "Loupe cannot read this encoder's codec and frame size.";
		return false;
	}
	if (encoded_width > UINT32_MAX / 4 ||
	    static_cast<uint64_t>(encoded_width) * encoded_height * 4 > MAX_RAW_FRAME_BYTES) {
		std::lock_guard lock(mutex);
		status = "Loupe cannot decode this encoded frame size safely.";
		return false;
	}

	obs_video_info video_info = {};
	if (!obs_get_video_info(&video_info) || video_info.output_width == 0 || video_info.output_height == 0 ||
	    video_info.output_width > UINT32_MAX / 4 ||
	    static_cast<uint64_t>(video_info.output_width) * video_info.output_height * 4 > MAX_RAW_FRAME_BYTES) {
		std::lock_guard lock(mutex);
		status = "Loupe cannot capture this source frame size safely.";
		return false;
	}

	decoder_config = {};
	decoder_config.codec_name = codec_name;
	decoder_config.width = encoded_width;
	decoder_config.height = encoded_height;
	uint8_t *extra_data = nullptr;
	size_t extra_data_size = 0;
	if (obs_encoder_get_extra_data(encoder, &extra_data, &extra_data_size) && extra_data && extra_data_size > 0) {
		decoder_config.extra_data.assign(extra_data, extra_data + extra_data_size);
	}

	output = output_;
	source_width = video_info.output_width;
	source_height = video_info.output_height;
	pair_tolerance_ns = std::max<uint64_t>(obs_get_frame_interval_ns() * 2, 1);
	const uint64_t source_frame_bytes = static_cast<uint64_t>(source_width) * source_height * 4;
	const size_t source_history_frames = static_cast<size_t>(std::max<uint64_t>(
		1, std::min<uint64_t>(MAX_SOURCE_HISTORY_FRAMES, MAX_SOURCE_HISTORY_BYTES / source_frame_bytes)));
	pairer.SetSourceCapacity(source_history_frames);
	{
		std::lock_guard lock(mutex);
		packets.clear();
		latest_pair.reset();
		pairer.Reset();
		accepting = false;
		stopping = false;
		decoder_ready = false;
		decoder_opened = false;
		await_keyframe = false;
		stats = {};
		last_packet_capture_timestamp_ns = 0;
		status = "Starting the encoded-frame decoder.";
	}
	worker = std::thread(&PreflightLoupeCapture::DecodeWorker, this);
	{
		std::unique_lock lock(mutex);
		condition.wait(lock, [this] { return decoder_ready; });
		if (!decoder_opened) {
			status = "This encoder's FFmpeg decoder is unavailable; the bench can still run.";
			lock.unlock();
			worker.join();
			output = nullptr;
			return false;
		}
		accepting = true;
		status = "Waiting for matching source and encoded frames.";
	}

	video_scale_info conversion = {};
	conversion.format = VIDEO_FORMAT_RGBA;
	conversion.width = video_info.output_width;
	conversion.height = video_info.output_height;
	conversion.range = VIDEO_RANGE_DEFAULT;
	conversion.colorspace = VIDEO_CS_DEFAULT;
	obs_add_raw_video_callback2(&conversion, RAW_VIDEO_FRAME_RATE_DIVISOR, RawVideoCallback, this);
	raw_callback_registered = true;
	obs_output_add_packet_callback(output, PacketCallback, this);
	packet_callback_registered = true;
	return true;
}

void PreflightLoupeCapture::Stop()
{
	if (packet_callback_registered && output) {
		obs_output_remove_packet_callback(output, PacketCallback, this);
		packet_callback_registered = false;
	}
	if (raw_callback_registered) {
		obs_remove_raw_video_callback(RawVideoCallback, this);
		raw_callback_registered = false;
	}
	if (worker.joinable()) {
		{
			std::lock_guard lock(mutex);
			accepting = false;
			stopping = true;
			packets.clear();
		}
		condition.notify_all();
		worker.join();
	}
	{
		std::lock_guard lock(mutex);
		packets.clear();
		latest_pair.reset();
		pairer.Reset();
		accepting = false;
		stopping = false;
		decoder_ready = false;
		decoder_opened = false;
		await_keyframe = false;
		if (status.find("decoder is unavailable") == std::string::npos) {
			status = "Loupe is waiting for the live bench.";
		}
	}
	output = nullptr;
	source_width = 0;
	source_height = 0;
}

std::shared_ptr<const preflight::loupe::FramePair> PreflightLoupeCapture::LatestPair() const
{
	std::lock_guard lock(mutex);
	return latest_pair;
}

PreflightLoupeCaptureStats PreflightLoupeCapture::Stats() const
{
	std::lock_guard lock(mutex);
	return stats;
}

std::string PreflightLoupeCapture::Status() const
{
	std::lock_guard lock(mutex);
	return status;
}

void PreflightLoupeCapture::RawVideoCallback(void *param, video_data *frame)
{
	if (param && frame) {
		static_cast<PreflightLoupeCapture *>(param)->PushSourceFrame(*frame);
	}
}

void PreflightLoupeCapture::PacketCallback(obs_output_t *, encoder_packet *packet, encoder_packet_time *packet_time,
					   void *param)
{
	if (!param) {
		return;
	}
	auto *capture = static_cast<PreflightLoupeCapture *>(param);
	{
		std::lock_guard lock(capture->mutex);
		++capture->stats.packet_callback_calls;
		if (!packet) {
			++capture->stats.packet_callbacks_null_packet;
			return;
		}
		if (packet->type != OBS_ENCODER_VIDEO) {
			++capture->stats.packet_callbacks_non_video;
			return;
		}
		if (!packet_time) {
			++capture->stats.packet_callbacks_without_timing;
			return;
		}
		if (packet_time->cts == 0) {
			++capture->stats.packet_callbacks_zero_timing;
		}
	}
	if (packet_time) {
		capture->QueueEncodedPacket(*packet, *packet_time);
	}
}

void PreflightLoupeCapture::PushSourceFrame(const video_data &frame)
{
	if (!frame.data[0] || frame.timestamp == 0 || source_width == 0 || source_height == 0 ||
	    source_width > UINT32_MAX / 4 || frame.linesize[0] < source_width * 4) {
		return;
	}
	const uint64_t bytes = static_cast<uint64_t>(source_width) * source_height * 4;
	if (bytes > MAX_RAW_FRAME_BYTES || bytes > std::numeric_limits<size_t>::max()) {
		return;
	}
	auto source = std::make_shared<preflight::loupe::PixelFrame>();
	source->width = source_width;
	source->height = source_height;
	source->stride = source->width * 4;
	source->capture_timestamp_ns = frame.timestamp;
	source->rgba.resize(static_cast<size_t>(bytes));
	for (uint32_t row = 0; row < source->height; ++row) {
		std::memcpy(source->rgba.data() + static_cast<size_t>(row) * source->stride,
			    frame.data[0] + static_cast<size_t>(row) * frame.linesize[0], source->stride);
	}
	std::lock_guard lock(mutex);
	if (accepting) {
		if (stats.source_frames == 0) {
			stats.first_source_timestamp_ns = source->capture_timestamp_ns;
		}
		stats.last_source_timestamp_ns = source->capture_timestamp_ns;
		pairer.PushSource(std::move(source));
		++stats.source_frames;
	}
}

void PreflightLoupeCapture::QueueEncodedPacket(const encoder_packet &packet, const encoder_packet_time &packet_time)
{
	if (!packet.data || packet.size == 0 || packet.size > MAX_PACKET_BYTES || packet_time.cts == 0) {
		return;
	}
	preflight::loupe::EncodedVideoPacket copy;
	copy.bytes.assign(packet.data, packet.data + packet.size);
	copy.pts = packet.pts;
	copy.dts = packet.dts;
	copy.timebase_num = packet.timebase_num;
	copy.timebase_den = packet.timebase_den;
	copy.capture_timestamp_ns = packet_time.cts;
	copy.keyframe = packet.keyframe;
	{
		std::lock_guard lock(mutex);
		if (!accepting) {
			return;
		}
		if (last_packet_capture_timestamp_ns != 0 &&
		    copy.capture_timestamp_ns < last_packet_capture_timestamp_ns) {
			++stats.reordered_packet_timestamps;
		}
		if (stats.encoded_packets == 0) {
			stats.first_packet_timestamp_ns = copy.capture_timestamp_ns;
		}
		stats.last_packet_timestamp_ns = copy.capture_timestamp_ns;
		last_packet_capture_timestamp_ns = copy.capture_timestamp_ns;
		++stats.encoded_packets;
		if (packets.size() >= MAX_QUEUED_PACKETS) {
			packets.clear();
			await_keyframe = true;
		}
		if (await_keyframe && !copy.keyframe) {
			status = "Loupe is waiting for the encoder's next keyframe.";
			return;
		}
		const bool reset_decoder = await_keyframe;
		await_keyframe = false;
		packets.push_back({std::move(copy), reset_decoder});
	}
	condition.notify_one();
}

void PreflightLoupeCapture::DecodeWorker()
{
	{
		std::lock_guard lock(mutex);
		stats.worker_running = true;
	}
	const bool opened = decoder.Open(decoder_config);
	{
		std::lock_guard lock(mutex);
		decoder_ready = true;
		decoder_opened = opened;
		if (!opened) {
			stats.worker_running = false;
		}
	}
	condition.notify_all();
	if (!opened) {
		return;
	}

	while (true) {
		QueuedPacket queued;
		{
			std::unique_lock lock(mutex);
			condition.wait(lock, [this] { return stopping || !packets.empty(); });
			if (stopping) {
				break;
			}
			queued = std::move(packets.front());
			packets.pop_front();
		}
		if (queued.reset_decoder) {
			decoder.Close();
			if (!decoder.Open(decoder_config)) {
				std::lock_guard lock(mutex);
				status = "Loupe decoder could not restart at a keyframe.";
				await_keyframe = true;
				continue;
			}
			std::lock_guard lock(mutex);
			pairer.Reset();
			latest_pair.reset();
		}

		std::vector<preflight::loupe::PixelFrame> decoded_frames;
		if (!decoder.Decode(queued.packet, decoded_frames)) {
			std::lock_guard lock(mutex);
			packets.clear();
			await_keyframe = true;
			status = "Loupe decoder is resynchronizing at the next keyframe.";
			continue;
		}
		for (auto &decoded : decoded_frames) {
			auto encoded = std::make_shared<preflight::loupe::PixelFrame>(std::move(decoded));
			std::lock_guard lock(mutex);
			++stats.decoded_frames;
			if (encoded->width != decoder_config.width || encoded->height != decoder_config.height) {
				++stats.wrong_size_decoded_frames;
			}
			const auto pair = pairer.Match(encoded, pair_tolerance_ns);
			if (pair) {
				++stats.paired_frames;
				const uint64_t source_timestamp = pair->source->capture_timestamp_ns;
				const uint64_t encoded_timestamp = pair->encoded->capture_timestamp_ns;
				const uint64_t delta = source_timestamp > encoded_timestamp
							       ? source_timestamp - encoded_timestamp
							       : encoded_timestamp - source_timestamp;
				stats.max_capture_timestamp_delta_ns =
					std::max(stats.max_capture_timestamp_delta_ns, delta);
				latest_pair = std::make_shared<const preflight::loupe::FramePair>(*pair);
				status = "Showing matching source and encoded frames at 400%.";
			} else {
				++stats.unpaired_decoded_frames;
			}
		}
	}
	decoder.Close();
	{
		std::lock_guard lock(mutex);
		stats.worker_running = false;
	}
}
