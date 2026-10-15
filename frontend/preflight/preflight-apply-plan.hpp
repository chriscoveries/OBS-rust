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
};

struct ConfigChange {
	QString path;
	QString key;
	QJsonValue before;
	QJsonValue after;
};

struct Plan {
	OutputMode mode = OutputMode::Unsupported;
	TargetIdentity target;
	QString destination;
	QString error;
	QJsonObject saved_settings;
	std::vector<ConfigChange> changes;
	QJsonObject updated_settings;

	bool applicable() const { return error.isEmpty() && !changes.empty(); }
};

Plan BuildAdvancedPlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		       const QJsonObject &edited_settings, const TargetIdentity &target = {});
Plan BuildSimplePlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		     const QJsonObject &edited_settings, const QString &simple_encoder, bool use_advanced,
		     const TargetIdentity &target = {});
bool SamePlan(const Plan &left, const Plan &right);
bool ReadAdvancedSettings(const QString &path, QJsonObject *settings, QString *error = nullptr);
bool WriteAdvancedSettings(const Plan &displayed, const Plan &current, QString *error = nullptr);
bool WriteSimpleConfig(config_t *config, const Plan &displayed, const Plan &current, QString *error = nullptr);
QString SimplePresetConfigKey(const QString &encoder);
QString ValueForDisplay(const QJsonValue &value);

} // namespace preflight::apply
