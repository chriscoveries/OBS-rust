#pragma once

#include "obs-preflight.h"
#include "preflight-loupe-core.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct obs_data;
typedef struct obs_data obs_data_t;
struct obs_source;
typedef struct obs_source obs_source_t;

/* Widget-free data and control surface for the Preflight bench. */
namespace preflight::bench {

/* A retained OBS settings object. get() is borrowed until this wrapper is
 * destroyed; callers that keep the pointer must call obs_data_addref(). */
class SettingsObject {
	obs_data_t *value = nullptr;

	explicit SettingsObject(obs_data_t *value, bool take_ownership);

public:
	SettingsObject() = default;
	static SettingsObject Retain(obs_data_t *value);
	static SettingsObject Take(obs_data_t *value);

	SettingsObject(const SettingsObject &other);
	SettingsObject(SettingsObject &&other) noexcept;
	SettingsObject &operator=(SettingsObject other) noexcept;
	~SettingsObject();

	obs_data_t *get() const;
	explicit operator bool() const;
};

enum class EncoderRole { Stream, Recording };

struct EncoderSettings {
	std::string encoder_id;
	SettingsObject values;
};

struct VideoSettings {
	/* The base canvas is informational: it cannot change while OBS is running. */
	uint32_t base_width = 0;
	uint32_t base_height = 0;
	uint32_t output_width = 0;
	uint32_t output_height = 0;
	uint32_t fps_num = 0;
	uint32_t fps_den = 1;
	uint32_t fps_divisor = 1;
	bool fps_divisor_supported = false;
};

struct MetricWindow {
	uint64_t sequence = 0;
	obs_preflight_window value{};
};

struct MetricDelta {
	double cpu_percentage_points = 0.0;
	double render_time_ms = 0.0;
	double encoder_skipped_percentage_points = 0.0;
	double missed_percentage_points = 0.0;
	double output_dropped_percentage_points = 0.0;
	double bitrate_kbps = 0.0;
	int32_t verdict_before = OBS_PREFLIGHT_PASS;
	int32_t verdict_after = OBS_PREFLIGHT_PASS;
};

/* Deltas are after minus baseline; percentage deltas are percentage points. */
MetricDelta CalculateImpactDelta(const obs_preflight_window &baseline, const obs_preflight_window &after);

enum class ImpactState { WaitingForBaseline, Settling, Complete, NoBaseline };

struct ImpactEntry {
	uint64_t id = 0;
	std::string description;
	std::string key;
	std::string old_value;
	std::string new_value;
	ImpactState state = ImpactState::WaitingForBaseline;
	std::optional<MetricWindow> baseline;
	std::optional<MetricWindow> after;
	std::optional<MetricDelta> deltas;
};

struct SettingChange {
	std::string description;
	std::string key;
	std::string old_value;
	std::string new_value;
};

class ImpactTracker {
	std::optional<MetricWindow> latest_complete_window;
	std::vector<ImpactEntry> entries;
	uint64_t next_id = 1;

public:
	void RecordChanges(const std::vector<SettingChange> &changes);
	void ObserveCompleteWindow(const MetricWindow &window);
	const std::optional<MetricWindow> &LatestCompleteWindow() const { return latest_complete_window; }
	const std::vector<ImpactEntry> &Entries() const { return entries; }
};

struct FeatureToggle {
	std::string name;
	std::string source_filter_id;
	bool enabled = false;
	bool available = false;
	std::string status;
};

struct CandidateSettings {
	EncoderSettings stream_encoder;
	VideoSettings video;
};

struct Snapshot {
	/* Each pair contains both the source frame and its decoded encoder output. */
	std::shared_ptr<const loupe::FramePair> stream_frame;
	std::shared_ptr<const loupe::FramePair> recording_frame;
	std::optional<MetricWindow> latest_metrics;
	std::vector<MetricWindow> metric_history;
	EncoderSettings stream_encoder;
	EncoderSettings recording_encoder;
	VideoSettings video;
	std::vector<ImpactEntry> impacts;
	std::vector<FeatureToggle> toggles;
};

/* Changes made through this API affect only the private bench encoders. They
 * never update the user's OBS profile; persistence remains a separate Apply
 * action. Feature toggles instead change their OBS filter immediately, because
 * enabled state is already live OBS state and is never part of Apply. */
class DataApi {
public:
	virtual ~DataApi() = default;
	virtual Snapshot GetSnapshot() const = 0;
	/* Baseline is the latest complete engine window before applying the edit.
	 * The first complete window after the encoder restart closes the impact. */
	virtual bool SetEncoderSettings(EncoderRole role, const SettingsObject &settings,
					const std::vector<SettingChange> &changes) = 0;
	virtual bool SetVideoSettings(const VideoSettings &settings, const std::vector<SettingChange> &changes) = 0;
	/* Retains one live OBS filter as the binding for a named immediate toggle. */
	virtual bool RegisterFeatureToggle(obs_source_t *filter, const std::string &name,
					   const std::string &source_filter_id) = 0;
	/* Applies obs_source_set_enabled() immediately to filter after validating its id. */
	virtual bool SetFeatureToggle(obs_source_t *filter, const std::string &name,
				      const std::string &source_filter_id, bool enabled) = 0;
	/* Updates private bench editor state only; no profile file is written. */
	virtual bool LoadCandidate(const CandidateSettings &candidate) = 0;
};

} // namespace preflight::bench
