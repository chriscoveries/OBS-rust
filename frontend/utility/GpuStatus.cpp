#include "GpuStatus.hpp"
#include "platform.hpp"

#ifdef OBS_GPU_STATUS
#include "obs-gpu-status.h"
#endif

#include <obs.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <deque>
#include <mutex>
#include <vector>

namespace GpuStatus {

bool Available()
{
#ifdef OBS_GPU_STATUS
	return true;
#else
	return false;
#endif
}

#ifdef OBS_GPU_STATUS
static Info Parse(int state, const char *json)
{
	Info info;
	info.state = state == 0 ? State::Green : state == 1 ? State::Yellow : State::Red;
	info.json = json ? json : "null";

	QJsonObject obj = QJsonDocument::fromJson(QByteArray(info.json.c_str())).object();
	info.adapter = obj["adapter"].toString();
	info.summary = obj["summary"].toString();
	for (const QJsonValue &cause : obj["causes"].toArray()) {
		info.causes << cause.toString();
	}
	for (const QJsonValue &value : obj["detectedGpus"].toArray()) {
		QJsonObject gpu = value.toObject();
		QString name = gpu["name"].toString();
		if (name.isEmpty()) {
			name = gpu["vendor"].toString().toUpper() + " " + gpu["deviceId"].toString();
		}
		if (!gpu["renderNode"].toBool()) {
			name += " (no render node)";
		}
		info.gpus << name;
	}
	return info;
}
#endif

#ifdef OBS_GPU_STATUS
/* One-second frame-time averages; the Rust side judges their median. */
static constexpr size_t maxFrameSamples = 15;
static std::mutex frameSamplesMutex;
static std::deque<uint64_t> frameSamples;

typedef const unsigned char *(*GlGetStringFn)(unsigned int);
static constexpr unsigned int GL_VENDOR_ENUM = 0x1F00;
static constexpr unsigned int GL_RENDERER_ENUM = 0x1F01;

struct AdapterSearch {
	uint32_t id;
	std::string name;
};

static bool FindAdapter(void *param, const char *name, uint32_t id)
{
	auto *search = static_cast<AdapterSearch *>(param);
	if (id != search->id) {
		return true;
	}
	search->name = name ? name : "";
	return false;
}

/* The active adapter as libobs logs it ("Loading up OpenGL on adapter ..."),
 * not gs_get_device_name(), which only names the API ("OpenGL"). Call with
 * the graphics context entered. */
static std::string ActiveAdapterName()
{
	if (gs_get_device_type() == GS_DEVICE_OPENGL) {
		static auto glGetStringFn = reinterpret_cast<GlGetStringFn>(GetGlGetStringProc());
		if (glGetStringFn) {
			const char *vendor = reinterpret_cast<const char *>(glGetStringFn(GL_VENDOR_ENUM));
			const char *renderer = reinterpret_cast<const char *>(glGetStringFn(GL_RENDERER_ENUM));
			if (renderer && *renderer) {
				return vendor && *vendor ? std::string(vendor) + " " + renderer : std::string(renderer);
			}
		}
	} else {
		obs_video_info ovi = {};
		if (obs_get_video_info(&ovi)) {
			AdapterSearch search = {ovi.adapter, ""};
			gs_enum_adapters(FindAdapter, &search);
			if (!search.name.empty()) {
				return search.name;
			}
		}
	}
	const char *api = gs_get_device_name();
	return api ? api : "";
}
#endif

void Sample()
{
#ifdef OBS_GPU_STATUS
	uint64_t frame = obs_get_average_frame_time_ns();
	if (frame == 0) {
		return; /* no frames rendered yet */
	}
	std::lock_guard<std::mutex> lock(frameSamplesMutex);
	frameSamples.push_back(frame);
	while (frameSamples.size() > maxFrameSamples) {
		frameSamples.pop_front();
	}
#endif
}

Info Query()
{
#ifdef OBS_GPU_STATUS
	std::string adapter;
	bool haveAdapter = false;
	obs_enter_graphics();
	if (gs_get_device_name()) {
		adapter = ActiveAdapterName();
		haveAdapter = true;
	}
	obs_leave_graphics();

	std::vector<uint64_t> samples;
	{
		std::lock_guard<std::mutex> lock(frameSamplesMutex);
		samples.assign(frameSamples.begin(), frameSamples.end());
	}

	int state = 2;
	char *json = obs_gpu_status_report(haveAdapter ? adapter.c_str() : nullptr, samples.data(), samples.size(),
					   obs_get_frame_interval_ns(), &state);
	Info info = Parse(state, json);
	obs_gpu_status_free(json);
	return info;
#else
	return Info();
#endif
}

#ifdef OBS_GPU_STATUS
static void GetGpuStatusProc(void *, calldata_t *cd)
{
	Info info = Query();
	calldata_set_string(cd, "json", info.json.c_str());
}
#endif

void Init()
{
#ifdef OBS_GPU_STATUS
	static bool registered = false;
	if (!registered) {
		proc_handler_add(obs_get_proc_handler(), "void get_gpu_status(out string json)", GetGpuStatusProc,
				 nullptr);
		registered = true;
	}

	Info info = Query();
	if (info.state == State::Green) {
		blog(LOG_INFO, "GPU acceleration: green: %s", info.summary.toUtf8().constData());
		return;
	}

	const char *color = info.state == State::Yellow ? "YELLOW" : "RED";
	blog(LOG_WARNING, "==== GPU acceleration: %s ====", color);
	blog(LOG_WARNING, "%s", info.summary.toUtf8().constData());
	for (const QString &gpu : info.gpus) {
		blog(LOG_WARNING, "  detected GPU: %s", gpu.toUtf8().constData());
	}
	for (const QString &cause : info.causes) {
		blog(LOG_WARNING, "  possible cause: %s", cause.toUtf8().constData());
	}
#endif
}

} // namespace GpuStatus
