#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <util/config-file.h>

#include <vector>

namespace preflight::apply {

enum class OutputMode { Simple, Advanced, Unsupported };

struct TargetIdentity {
	QString profile_name;
	QString profile_path;
	QString encoder_id;
	QString encoder_selection;
	QString recording_encoder_id;
	QString recording_encoder_selection;
};

struct ConfigChange {
	QString path;
	QString key;
	QJsonValue before;
	QJsonValue after;
	QString section;
};

struct Plan {
	OutputMode mode = OutputMode::Unsupported;
	TargetIdentity target;
	QString destination;
	QString error;
	QJsonObject saved_settings;
	std::vector<ConfigChange> changes;
	QJsonObject updated_settings;
	bool has_recording_settings = false;
	QJsonObject saved_recording_settings;
	QJsonObject updated_recording_settings;
	QJsonObject saved_config_values;
	QJsonObject updated_config_values;

	bool applicable() const { return error.isEmpty() && !changes.empty(); }
};

Plan BuildAdvancedPlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		       const QJsonObject &edited_settings, const TargetIdentity &target = {});
Plan BuildSimplePlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		     const QJsonObject &edited_settings, const QString &simple_encoder, bool use_advanced,
		     const TargetIdentity &target = {});
void AppendAdvancedEncoderChanges(Plan &plan, const QString &file_name, const QJsonObject &saved_settings,
				  const QJsonObject &initial_editor_settings, const QJsonObject &edited_settings);
void AppendConfigSectionChanges(Plan &plan, const QString &section, const QJsonObject &saved_values,
				const QJsonObject &edited_values);
bool SamePlan(const Plan &left, const Plan &right);
bool ReadAdvancedSettings(const QString &path, QJsonObject *settings, QString *error = nullptr);
bool WriteAdvancedSettings(const Plan &displayed, const Plan &current, QString *error = nullptr);
bool WriteAdvancedSettings(config_t *config, const Plan &displayed, const Plan &current, QString *error = nullptr);
bool WriteSimpleConfig(config_t *config, const Plan &displayed, const Plan &current, QString *error = nullptr);
QString SimplePresetConfigKey(const QString &encoder);
QString ValueForDisplay(const QJsonValue &value);

} // namespace preflight::apply
