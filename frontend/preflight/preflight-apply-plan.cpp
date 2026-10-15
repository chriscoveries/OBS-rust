#include "preflight-apply-plan.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QFileInfo>
#include <QDir>
#include <QStringList>

#include <obs-data.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace preflight::apply {
namespace {

bool same_target(const TargetIdentity &left, const TargetIdentity &right)
{
	return left.profile_name == right.profile_name && left.profile_path == right.profile_path &&
	       left.encoder_id == right.encoder_id && left.encoder_selection == right.encoder_selection &&
	       left.recording_encoder_id == right.recording_encoder_id &&
	       left.recording_encoder_selection == right.recording_encoder_selection;
}

bool config_value_matches(config_t *config, const char *section, const QString &key, const QJsonValue &value)
{
	const QByteArray key_utf8 = key.toUtf8();
	if (value.isUndefined()) {
		return !config_has_user_value(config, section, key_utf8.constData());
	}
	if (value.isString()) {
		const char *current = config_get_string(config, section, key_utf8.constData());
		return QString::fromUtf8(current ? current : "") == value.toString();
	}
	if (value.isBool()) {
		return config_get_bool(config, section, key_utf8.constData()) == value.toBool();
	}
	if (value.isDouble()) {
		return static_cast<double>(config_get_int(config, section, key_utf8.constData())) == value.toDouble();
	}
	return false;
}

bool config_snapshot_matches(config_t *config, const QJsonObject &values)
{
	for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
		const qsizetype separator = it.key().indexOf('/');
		if (separator <= 0 || separator == it.key().size() - 1 ||
		    !config_value_matches(config, it.key().left(separator).toUtf8().constData(),
					  it.key().mid(separator + 1), it.value())) {
			return false;
		}
	}
	return true;
}

void set_config_value(config_t *config, const char *section, const QString &key, const QJsonValue &value)
{
	const QByteArray key_utf8 = key.toUtf8();
	if (value.isUndefined()) {
		config_remove_value(config, section, key_utf8.constData());
	} else if (value.isString()) {
		const QByteArray text = value.toString().toUtf8();
		config_set_string(config, section, key_utf8.constData(), text.constData());
	} else if (value.isBool()) {
		config_set_bool(config, section, key_utf8.constData(), value.toBool());
	} else if (value.isDouble()) {
		config_set_int(config, section, key_utf8.constData(), static_cast<int64_t>(value.toDouble()));
	}
}

bool reject(QString *error, const QString &message)
{
	if (error) {
		*error = message;
	}
	return false;
}

QStringList changed_properties(const QJsonObject &initial, const QJsonObject &edited)
{
	QStringList keys;
	for (auto it = initial.constBegin(); it != initial.constEnd(); ++it) {
		if (edited.value(it.key()) != it.value()) {
			keys.push_back(it.key());
		}
	}
	for (auto it = edited.constBegin(); it != edited.constEnd(); ++it) {
		if (!initial.contains(it.key())) {
			keys.push_back(it.key());
		}
	}
	keys.removeDuplicates();
	std::sort(keys.begin(), keys.end());
	return keys;
}

QString simple_preset_key(const QString &encoder)
{
	if (encoder == QStringLiteral("x264") || encoder == QStringLiteral("x264_lowcpu")) {
		return QStringLiteral("Preset");
	}
	if (encoder == QStringLiteral("qsv") || encoder == QStringLiteral("qsv_av1")) {
		return QStringLiteral("QSVPreset");
	}
	if (encoder == QStringLiteral("nvenc") || encoder == QStringLiteral("nvenc_av1") ||
	    encoder == QStringLiteral("nvenc_hevc")) {
		return QStringLiteral("NVENCPreset2");
	}
	if (encoder == QStringLiteral("amd") || encoder == QStringLiteral("amd_hevc")) {
		return QStringLiteral("AMDPreset");
	}
	if (encoder == QStringLiteral("amd_av1")) {
		return QStringLiteral("AMDAV1Preset");
	}
	if (encoder == QStringLiteral("apple_h264") || encoder == QStringLiteral("apple_hevc")) {
		return QStringLiteral("ApplePreset");
	}
	return {};
}

void add_change(Plan &plan, const QString &path, const QString &key, const QJsonObject &saved, const QJsonValue &value)
{
	const QJsonValue before = saved.value(key);
	if (before == value) {
		return;
	}
	plan.changes.push_back({path, key, before, value});
	plan.updated_settings.insert(key, value);
}

} // namespace

QString SimplePresetConfigKey(const QString &encoder)
{
	return simple_preset_key(encoder);
}

Plan BuildAdvancedPlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		       const QJsonObject &edited_settings, const TargetIdentity &target)
{
	Plan plan;
	plan.mode = OutputMode::Advanced;
	plan.target = target;
	plan.destination = QStringLiteral("streamEncoder.json");
	plan.saved_settings = saved_settings;
	plan.updated_settings = saved_settings;
	for (const QString &property : changed_properties(initial_editor_settings, edited_settings)) {
		const QJsonValue edited = edited_settings.value(property);
		if (saved_settings.value(property) == edited) {
			continue;
		}
		if (edited.isUndefined()) {
			plan.updated_settings.remove(property);
		} else {
			plan.updated_settings.insert(property, edited);
		}
		plan.changes.push_back({QStringLiteral("streamEncoder.json/%1").arg(property), property,
					saved_settings.value(property), edited});
	}
	return plan;
}

Plan BuildSimplePlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		     const QJsonObject &edited_settings, const QString &simple_encoder, bool use_advanced,
		     const TargetIdentity &target)
{
	Plan plan;
	plan.mode = OutputMode::Simple;
	plan.target = target;
	plan.destination = QStringLiteral("basic.ini · SimpleOutput");
	plan.saved_settings = saved_settings;
	plan.updated_settings = saved_settings;
	QStringList unsupported;
	QStringList written_keys;
	const QString preset_key = simple_preset_key(simple_encoder);

	for (const QString &property : changed_properties(initial_editor_settings, edited_settings)) {
		QString config_key;
		if (property == QStringLiteral("bitrate")) {
			const QJsonValue bitrate = edited_settings.value(property);
			const double value = bitrate.toDouble(-1.0);
			if (!bitrate.isDouble() || !std::isfinite(value) || value < 0.0 || std::floor(value) != value ||
			    value > std::numeric_limits<int32_t>::max()) {
				unsupported.push_back(property);
				continue;
			}
			config_key = QStringLiteral("VBitrate");
		} else if (property == QStringLiteral("preset") || property == QStringLiteral("preset2")) {
			config_key = preset_key;
		} else if (property == QStringLiteral("x264opts") && use_advanced &&
			   (simple_encoder == QStringLiteral("x264") ||
			    simple_encoder == QStringLiteral("x264_lowcpu"))) {
			config_key = QStringLiteral("x264Settings");
		}

		if (config_key.isEmpty() || written_keys.contains(config_key)) {
			unsupported.push_back(property);
			continue;
		}
		written_keys.push_back(config_key);
		const QJsonValue edited = edited_settings.value(property);
		add_change(plan, QStringLiteral("SimpleOutput.%1").arg(config_key), config_key, saved_settings, edited);
	}

	if (!unsupported.isEmpty()) {
		unsupported.removeDuplicates();
		plan.error = QStringLiteral("Simple output does not store these changed encoder properties: %1")
				     .arg(unsupported.join(QStringLiteral(", ")));
	}
	return plan;
}

void AppendAdvancedEncoderChanges(Plan &plan, const QString &file_name, const QJsonObject &saved_settings,
				  const QJsonObject &initial_editor_settings, const QJsonObject &edited_settings)
{
	if (plan.mode != OutputMode::Advanced || file_name.isEmpty()) {
		plan.error = QStringLiteral("Encoder file changes require an Advanced output plan.");
		return;
	}
	QJsonObject updated = saved_settings;
	for (const QString &property : changed_properties(initial_editor_settings, edited_settings)) {
		const QJsonValue edited = edited_settings.value(property);
		if (saved_settings.value(property) == edited) {
			continue;
		}
		if (edited.isUndefined()) {
			updated.remove(property);
		} else {
			updated.insert(property, edited);
		}
		plan.changes.push_back({QStringLiteral("%1/%2").arg(file_name, property), property,
					saved_settings.value(property), edited});
	}
	plan.has_recording_settings = true;
	plan.saved_recording_settings = saved_settings;
	plan.updated_recording_settings = updated;
}

void AppendConfigSectionChanges(Plan &plan, const QString &section, const QJsonObject &saved_values,
				const QJsonObject &edited_values)
{
	if (section.isEmpty()) {
		plan.error = QStringLiteral("A config section is required to preview saved OBS settings.");
		return;
	}
	const QString prefix = section + QStringLiteral("/");
	for (auto it = saved_values.constBegin(); it != saved_values.constEnd(); ++it) {
		plan.saved_config_values.insert(prefix + it.key(), it.value());
		plan.updated_config_values.insert(prefix + it.key(), it.value());
	}
	for (auto it = edited_values.constBegin(); it != edited_values.constEnd(); ++it) {
		if (!plan.saved_config_values.contains(prefix + it.key())) {
			plan.saved_config_values.insert(prefix + it.key(), QJsonValue(QJsonValue::Undefined));
			plan.updated_config_values.insert(prefix + it.key(), QJsonValue(QJsonValue::Undefined));
		}
	}
	for (auto it = plan.saved_config_values.constBegin(); it != plan.saved_config_values.constEnd(); ++it) {
		if (!it.key().startsWith(prefix)) {
			continue;
		}
		const QString key = it.key().mid(prefix.size());
		const QJsonValue after = edited_values.value(key);
		if (it.value() == after) {
			continue;
		}
		plan.updated_config_values.insert(it.key(), after);
		plan.changes.push_back({QStringLiteral("%1.%2").arg(section, key), key, it.value(), after, section});
	}
}

bool SamePlan(const Plan &left, const Plan &right)
{
	if (left.mode != right.mode || !same_target(left.target, right.target) ||
	    left.destination != right.destination || left.error != right.error ||
	    left.saved_settings != right.saved_settings || left.updated_settings != right.updated_settings ||
	    left.has_recording_settings != right.has_recording_settings ||
	    left.saved_recording_settings != right.saved_recording_settings ||
	    left.updated_recording_settings != right.updated_recording_settings ||
	    left.saved_config_values != right.saved_config_values ||
	    left.updated_config_values != right.updated_config_values || left.changes.size() != right.changes.size()) {
		return false;
	}
	for (size_t index = 0; index < left.changes.size(); ++index) {
		const ConfigChange &a = left.changes[index];
		const ConfigChange &b = right.changes[index];
		if (a.path != b.path || a.key != b.key || a.before != b.before || a.after != b.after ||
		    a.section != b.section) {
			return false;
		}
	}
	return true;
}

bool ReadAdvancedSettings(const QString &path, QJsonObject *settings, QString *error)
{
	if (!settings) {
		return reject(error, QStringLiteral("No destination was provided for the saved encoder settings."));
	}
	if (!QFileInfo::exists(path)) {
		*settings = {};
		return true;
	}
	const QByteArray path_utf8 = path.toUtf8();
	// Preview must never let libobs repair the user's file by restoring its backup.
	obs_data_t *data = obs_data_create_from_json_file(path_utf8.constData());
	if (!data) {
		return reject(error, QStringLiteral("OBS could not read the saved encoder settings."));
	}
	const char *json = obs_data_get_json(data);
	QJsonParseError parse_error{};
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json ? json : ""), &parse_error);
	obs_data_release(data);
	if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
		return reject(error, QStringLiteral("The saved encoder settings are not valid JSON."));
	}
	*settings = document.object();
	return true;
}

bool WriteAdvancedSettings(const Plan &displayed, const Plan &current, QString *error)
{
	return WriteAdvancedSettings(nullptr, displayed, current, error);
}

bool WriteAdvancedSettings(config_t *config, const Plan &displayed, const Plan &current, QString *error)
{
	if (!SamePlan(displayed, current)) {
		return reject(
			error,
			QStringLiteral("The profile, encoder, or settings changed after this preview was shown."));
	}
	if (current.mode != OutputMode::Advanced || !current.applicable() || current.target.profile_path.isEmpty()) {
		return reject(error, QStringLiteral("There is no applicable Advanced settings preview to write."));
	}
	if (!current.saved_config_values.isEmpty() && !config) {
		return reject(error,
			      QStringLiteral("The preview includes Video settings but no OBS config is available."));
	}
	if (config) {
		const char *mode = config_get_string(config, "Output", "Mode");
		if (!mode || QString::fromUtf8(mode) != QStringLiteral("Advanced") ||
		    !config_snapshot_matches(config, current.saved_config_values)) {
			return reject(
				error,
				QStringLiteral(
					"Advanced output or Video settings changed after this preview was shown."));
		}
	}

	const QString profile_path = current.target.profile_path;
	const QString stream_path = QDir(profile_path).filePath(QStringLiteral("streamEncoder.json"));
	const QString recording_path = QDir(profile_path).filePath(QStringLiteral("recordEncoder.json"));
	QJsonObject disk_stream;
	if (!ReadAdvancedSettings(stream_path, &disk_stream, error)) {
		return false;
	}
	if (disk_stream != current.saved_settings) {
		return reject(
			error,
			QStringLiteral("The saved stream encoder settings changed after this preview was shown."));
	}
	const bool write_stream = current.updated_settings != current.saved_settings;
	const bool write_recording = current.has_recording_settings &&
				     current.updated_recording_settings != current.saved_recording_settings;
	QJsonObject disk_recording;
	if (write_recording) {
		if (!ReadAdvancedSettings(recording_path, &disk_recording, error)) {
			return false;
		}
		if (disk_recording != current.saved_recording_settings) {
			return reject(
				error,
				QStringLiteral(
					"The saved recording encoder settings changed after this preview was shown."));
		}
	}

	auto save_json = [](const QString &path, const QJsonObject &object) {
		const QByteArray path_utf8 = path.toUtf8();
		const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Indented);
		obs_data_t *updated = obs_data_create_from_json(json.constData());
		if (!updated) {
			return false;
		}
		const bool saved = obs_data_save_json_safe(updated, path_utf8.constData(), "tmp", "bak");
		obs_data_release(updated);
		return saved;
	};
	auto save_video_config = [&]() {
		for (const ConfigChange &change : current.changes) {
			if (!change.section.isEmpty()) {
				set_config_value(config, change.section.toUtf8().constData(), change.key, change.after);
			}
		}
		return config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS;
	};
	auto restore_video_config = [&]() {
		for (const ConfigChange &change : current.changes) {
			if (!change.section.isEmpty()) {
				set_config_value(config, change.section.toUtf8().constData(), change.key,
						 change.before);
			}
		}
		return config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS;
	};
	const bool has_video_changes =
		std::any_of(current.changes.begin(), current.changes.end(),
			    [](const ConfigChange &change) { return !change.section.isEmpty(); });
	if (has_video_changes && !save_video_config()) {
		(void)restore_video_config();
		return reject(error, QStringLiteral("OBS could not save the Video settings."));
	}
	bool wrote_stream = false;
	if (write_stream) {
		wrote_stream = save_json(stream_path, current.updated_settings);
		if (!wrote_stream) {
			if (has_video_changes) {
				(void)restore_video_config();
			}
			return reject(
				error,
				QStringLiteral("OBS could not save streamEncoder.json. No settings were applied."));
		}
	}
	if (write_recording && !save_json(recording_path, current.updated_recording_settings)) {
		if (wrote_stream) {
			(void)save_json(stream_path, current.saved_settings);
		}
		if (has_video_changes) {
			(void)restore_video_config();
		}
		return reject(error,
			      QStringLiteral("OBS could not save recordEncoder.json. No settings were applied."));
	}
	return true;
}

bool WriteSimpleConfig(config_t *config, const Plan &displayed, const Plan &current, QString *error)
{
	if (!config || !SamePlan(displayed, current)) {
		return reject(
			error,
			QStringLiteral("The profile, encoder, or settings changed after this preview was shown."));
	}
	if (current.mode != OutputMode::Simple || !current.applicable()) {
		return reject(error, QStringLiteral("There is no applicable Simple settings preview to write."));
	}
	const char *mode = config_get_string(config, "Output", "Mode");
	if (!mode || QString::fromUtf8(mode) != QStringLiteral("Simple")) {
		return reject(error, QStringLiteral("OBS output mode changed after this preview was shown."));
	}
	for (auto it = current.saved_settings.constBegin(); it != current.saved_settings.constEnd(); ++it) {
		if (!config_value_matches(config, "SimpleOutput", it.key(), it.value())) {
			return reject(error,
				      QStringLiteral("Simple output settings changed after this preview was shown."));
		}
	}
	if (!config_snapshot_matches(config, current.saved_config_values)) {
		return reject(error,
			      QStringLiteral("OBS recording or Video settings changed after this preview was shown."));
	}
	for (const ConfigChange &change : current.changes) {
		const QByteArray section =
			(change.section.isEmpty() ? QStringLiteral("SimpleOutput") : change.section).toUtf8();
		set_config_value(config, section.constData(), change.key, change.after);
	}
	if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
		for (const ConfigChange &change : current.changes) {
			const QByteArray section =
				(change.section.isEmpty() ? QStringLiteral("SimpleOutput") : change.section).toUtf8();
			set_config_value(config, section.constData(), change.key, change.before);
		}
		if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
			return reject(
				error,
				QStringLiteral(
					"OBS could not save the Simple output settings or persist their rollback. Check basic.ini before applying again."));
		}
		return reject(
			error,
			QStringLiteral("OBS could not save the Simple output settings. No settings were applied."));
	}
	return true;
}

QString ValueForDisplay(const QJsonValue &value)
{
	if (value.isUndefined()) {
		return QStringLiteral("(unset)");
	}
	if (value.isString()) {
		return value.toString();
	}
	QJsonObject wrapper;
	wrapper.insert(QStringLiteral("value"), value);
	const QByteArray json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
	const qsizetype separator = json.indexOf(':');
	return QString::fromUtf8(json.mid(separator + 1, json.size() - separator - 2));
}

} // namespace preflight::apply
