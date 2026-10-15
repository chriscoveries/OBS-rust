#include "../preflight/preflight-loupe-core.hpp"

namespace preflight::loupe {

std::optional<CropRegion> make_crop_region(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t)
{
	return std::nullopt;
}

std::optional<PixelRect> project_crop(const CropRegion &, uint32_t, uint32_t)
{
	return std::nullopt;
}

std::optional<PixelFrame> crop_at_4x(const PixelFrame &, const CropRegion &)
{
	return std::nullopt;
}

struct EncodedVideoDecoder::Impl {};

EncodedVideoDecoder::EncodedVideoDecoder() : impl(std::make_unique<Impl>()) {}
EncodedVideoDecoder::~EncodedVideoDecoder() = default;
bool EncodedVideoDecoder::Open(const DecoderConfig &) { return false; }
void EncodedVideoDecoder::Close() {}
bool EncodedVideoDecoder::Decode(const EncodedVideoPacket &, std::vector<PixelFrame> &) { return false; }

FramePairer::FramePairer(size_t capacity) : source_capacity(capacity) {}
void FramePairer::Reset() { source_frames.clear(); }
void FramePairer::PushSource(std::shared_ptr<const PixelFrame>)
{
	if (source_capacity == 0) {
		source_frames.clear();
	}
}
std::optional<FramePair> FramePairer::Match(std::shared_ptr<const PixelFrame>, uint64_t)
{
	return std::nullopt;
}

} // namespace preflight::loupe
