#pragma once

#include <QString>
#include <QStringList>

#include <string>

/* GPU acceleration status (obs-rust/obs-studio#846): is OBS rendering on a
 * hardware GPU? The classification lives in rust/obs-gpu-status. */
namespace GpuStatus {

enum class State { Green, Yellow, Red };

struct Info {
	State state = State::Green;
	QString adapter;
	QString summary;
	QStringList causes;
	QStringList gpus;
	/* The gpuAcceleration object obs-websocket returns in GetStats. */
	std::string json;
};

/* False in builds without rust/obs-gpu-status; nothing is shown then. */
bool Available();

/* Record libobs' one-second average frame time; call once a second. */
void Sample();

/* Classify the current adapter, detected GPUs and recent frame times. */
Info Query();

/* Call once after video is reset: logs a warning when the state is yellow
 * or red, and registers the "get_gpu_status" proc on the global proc
 * handler (out: string json) for obs-websocket. */
void Init();

} // namespace GpuStatus
