#include "preflight-apply-plan.hpp"

#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace preflight::apply {
namespace {

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
		       const QJsonObject &edited_settings)
{
	Plan plan;
	plan.mode = OutputMode::Advanced;
	plan.destination = QStringLiteral("streamEncoder.json");
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
		     const QJsonObject &edited_settings, const QString &simple_encoder, bool use_advanced)
{
	Plan plan;
	plan.mode = OutputMode::Simple;
	plan.destination = QStringLiteral("basic.ini · SimpleOutput");
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
