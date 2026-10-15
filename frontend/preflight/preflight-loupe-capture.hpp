#pragma once

#include "preflight-loupe-core.hpp"

#include <obs.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class PreflightLoupeCapture {
	struct QueuedPacket {
		preflight::loupe::EncodedVideoPacket packet;
		bool reset_decoder = false;
	};

	preflight::loupe::FramePairer pairer{4};
	preflight::loupe::EncodedVideoDecoder decoder;
	preflight::loupe::DecoderConfig decoder_config;
	std::condition_variable condition;
	mutable std::mutex mutex;
	std::deque<QueuedPacket> packets;
	std::shared_ptr<const preflight::loupe::FramePair> latest_pair;
	std::thread worker;
	obs_output_t *output = nullptr;
	uint32_t source_width = 0;
	uint32_t source_height = 0;
	uint64_t pair_tolerance_ns = 0;
	bool accepting = false;
	bool stopping = false;
	bool decoder_ready = false;
	bool decoder_opened = false;
	bool await_keyframe = false;
	bool raw_callback_registered = false;
	bool packet_callback_registered = false;
	std::string status;

	static void RawVideoCallback(void *param, video_data *frame);
	static void PacketCallback(obs_output_t *output, encoder_packet *packet, encoder_packet_time *packet_time,
				   void *param);
	void PushSourceFrame(const video_data &frame);
	void QueueEncodedPacket(const encoder_packet &packet, const encoder_packet_time &packet_time);
	void DecodeWorker();

public:
	PreflightLoupeCapture() = default;
	~PreflightLoupeCapture();
	PreflightLoupeCapture(const PreflightLoupeCapture &) = delete;
	PreflightLoupeCapture &operator=(const PreflightLoupeCapture &) = delete;

	bool Start(obs_output_t *output, obs_encoder_t *encoder);
	void Stop();
	std::shared_ptr<const preflight::loupe::FramePair> LatestPair() const;
	std::string Status() const;
};
