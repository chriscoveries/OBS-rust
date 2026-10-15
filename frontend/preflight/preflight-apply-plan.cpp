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
	       left.encoder_id == right.encoder_id && left.encoder_selection == right.encoder_selection;
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

bool SamePlan(const Plan &left, const Plan &right)
{
	if (left.mode != right.mode || !same_target(left.target, right.target) ||
	    left.destination != right.destination || left.error != right.error ||
	    left.saved_settings != right.saved_settings || left.updated_settings != right.updated_settings ||
	    left.changes.size() != right.changes.size()) {
		return false;
	}
	for (size_t index = 0; index < left.changes.size(); ++index) {
		const ConfigChange &a = left.changes[index];
		const ConfigChange &b = right.changes[index];
		if (a.path != b.path || a.key != b.key || a.before != b.before || a.after != b.after) {
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
	obs_data_t *data = obs_data_create_from_json_file_safe(path_utf8.constData(), "bak");
	if (!data) {
		return reject(error, QStringLiteral("OBS could not read the saved stream encoder settings."));
	}
	const char *json = obs_data_get_json(data);
	QJsonParseError parse_error{};
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json ? json : ""), &parse_error);
	obs_data_release(data);
	if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
		return reject(error, QStringLiteral("The saved stream encoder settings are not valid JSON."));
	}
	*settings = document.object();
	return true;
}

bool WriteAdvancedSettings(const Plan &displayed, const Plan &current, QString *error)
{
	if (!SamePlan(displayed, current)) {
		return reject(
			error,
			QStringLiteral("The profile, encoder, or settings changed after this preview was shown."));
	}
	if (current.mode != OutputMode::Advanced || !current.applicable() || current.target.profile_path.isEmpty()) {
		return reject(error, QStringLiteral("There is no applicable Advanced settings preview to write."));
	}
	const QString path = QDir(current.target.profile_path).filePath(QStringLiteral("streamEncoder.json"));
	QJsonObject disk_settings;
	if (!ReadAdvancedSettings(path, &disk_settings, error)) {
		return false;
	}
	if (disk_settings != current.saved_settings) {
		return reject(
			error,
			QStringLiteral("The saved stream encoder settings changed after this preview was shown."));
	}
	const QByteArray path_utf8 = path.toUtf8();
	const QByteArray json = QJsonDocument(current.updated_settings).toJson(QJsonDocument::Indented);
	obs_data_t *updated = obs_data_create_from_json(json.constData());
	if (!updated) {
		return reject(error, QStringLiteral("OBS could not prepare the stream encoder settings for saving."));
	}
	const bool saved = obs_data_save_json_safe(updated, path_utf8.constData(), "tmp", "bak");
	obs_data_release(updated);
	if (!saved) {
		return reject(error,
			      QStringLiteral("OBS could not save streamEncoder.json. No settings were applied."));
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
	for (const ConfigChange &change : current.changes) {
		set_config_value(config, "SimpleOutput", change.key, change.after);
	}
	if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
		for (const ConfigChange &change : current.changes) {
			set_config_value(config, "SimpleOutput", change.key, change.before);
		}
		config_save_safe(config, "tmp", nullptr);
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
