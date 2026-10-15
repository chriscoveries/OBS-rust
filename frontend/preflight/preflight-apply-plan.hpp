#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <vector>

namespace preflight::apply {

enum class OutputMode { Simple, Advanced, Unsupported };

struct ConfigChange {
	QString path;
	QString key;
	QJsonValue before;
	QJsonValue after;
};

struct Plan {
	OutputMode mode = OutputMode::Unsupported;
	QString destination;
	QString error;
	std::vector<ConfigChange> changes;
	QJsonObject updated_settings;

	bool applicable() const { return error.isEmpty() && !changes.empty(); }
};

Plan BuildAdvancedPlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		       const QJsonObject &edited_settings);
Plan BuildSimplePlan(const QJsonObject &saved_settings, const QJsonObject &initial_editor_settings,
		     const QJsonObject &edited_settings, const QString &simple_encoder, bool use_advanced);
QString SimplePresetConfigKey(const QString &encoder);
QString ValueForDisplay(const QJsonValue &value);

} // namespace preflight::apply
