#include "preflight-bench-window.hpp"

#include "obs-preflight-collector.h"
#include "obs-preflight.h"
#include "preflight-cloudflare-speed-test.hpp"
#include "preflight-apply-plan.hpp"
#include "preflight-loupe-capture.hpp"
#include "preflight-test-mode.hpp"

#include <properties-view.hpp>

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/config-file.h>
#ifndef PREFLIGHT_UI_RENDER_TEST
#include <widgets/OBSBasic.hpp>
#endif
#if defined(YOUTUBE_ENABLED) && !defined(PREFLIGHT_UI_RENDER_TEST)
#include <utility/YoutubeApiWrappers.hpp>
#endif

#include <QColor>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#ifdef PREFLIGHT_UI_RENDER_TEST
// The recorded-data test never creates live outputs. Keep its link independent
// of OBSBasic's generated Qt UI while compiling the production Apply code.
class OBSBasic {
public:
	struct Profile {
		std::string path;
	};

	static OBSBasic *Get() { return nullptr; }
	config_t *Config() const { return nullptr; }
	Profile GetCurrentProfile() const { return {}; }
	bool Active() const { return false; }
	void ResetOutputs() {}
};
#endif

namespace {

constexpr uint64_t SAMPLE_WINDOW_MS = 500;
// The existing Auto-Configuration wizard measures each encoder step for five seconds.
constexpr uint64_t BAKEOFF_CANDIDATE_DURATION_MS = 5'000;
constexpr size_t MAX_ENGINE_WINDOWS = 120;
constexpr size_t CHART_HISTORY_SIZE = 120;
// OBSBasicStats.cpp:362-369 warns above 75% of frame time and marks danger above 100%.
constexpr double OBS_RENDER_WARNING_FRACTION = 0.75;
constexpr double OBS_RENDER_DANGER_FRACTION = 1.0;
// OBSBasicStats.cpp:393-398 and 419-424 uses >1% for warning and >5% for danger.
constexpr double OBS_STATS_WARNING_PERCENT = 1.0;
constexpr double OBS_STATS_DANGER_PERCENT = 5.0;
// OBSBasicStatusBar.cpp:10-13 labels congestion good through 33.33% and mediocre through 66.67%.
constexpr double OBS_CONGESTION_WARNING_PERCENT = 33.33;
constexpr double OBS_CONGESTION_DANGER_PERCENT = 66.67;

struct ChartSeries {
	QString name;
	QColor color;
	std::vector<double> points;
};

struct ChartWarningLine {
	QString label;
	double value = 0.0;
	QColor color;
};

class MetricChart final : public QWidget {
	std::vector<ChartSeries> series;
	std::vector<ChartWarningLine> warning_lines;

public:
	explicit MetricChart(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumHeight(132);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void SetSeries(std::vector<ChartSeries> series_)
	{
		series = std::move(series_);
		update();
	}

	void SetWarningLines(std::vector<ChartWarningLine> lines)
	{
		QStringList labels;
		for (const auto &line : lines) {
			labels.push_back(line.label);
		}
		setProperty("preflightWarningLines", labels);
		warning_lines = std::move(lines);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.fillRect(rect(), palette().color(QPalette::Base));
		QRectF plot = QRectF(rect()).adjusted(48, 24, -10, -22);
		bool has_points = false;
		double minValue = 0.0;
		double maxValue = 100.0;
		qreal legend_x = plot.left();
		painter.setFont(QFont(painter.font().family(), painter.font().pointSize() - 1));
		for (const auto &line : series) {
			painter.setPen(QPen(line.color, 2.0));
			painter.drawLine(QPointF(legend_x, 10), QPointF(legend_x + 13, 10));
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(QPointF(legend_x + 18, 14), line.name);
			legend_x += painter.fontMetrics().horizontalAdvance(line.name) + 34;
			for (double value : line.points) {
				if (!has_points) {
					minValue = maxValue = value;
					has_points = true;
				} else {
					minValue = std::min(minValue, value);
					maxValue = std::max(maxValue, value);
				}
			}
		}
		for (const auto &line : warning_lines) {
			if (!has_points) {
				minValue = maxValue = line.value;
				has_points = true;
			} else {
				minValue = std::min(minValue, line.value);
				maxValue = std::max(maxValue, line.value);
			}
		}
		if (maxValue - minValue < 0.001) {
			const double padding = std::max(1.0, std::abs(maxValue) * 0.05);
			minValue -= padding;
			maxValue += padding;
		} else {
			const double padding = (maxValue - minValue) * 0.08;
			minValue -= padding;
			maxValue += padding;
		}
		QStringList y_axis_labels;
		painter.setPen(palette().color(QPalette::Mid));
		for (int row = 0; row <= 3; ++row) {
			const qreal y = plot.top() + plot.height() * row / 3.0;
			const double value = maxValue - (maxValue - minValue) * row / 3.0;
			const int precision = std::abs(value) >= 100.0 ? 0 : std::abs(value) >= 10.0 ? 1 : 2;
			QString label = QString::number(value, 'f', precision);
			while (label.contains('.') && label.endsWith('0')) {
				label.chop(1);
			}
			if (label.endsWith('.')) {
				label.chop(1);
			}
			y_axis_labels.push_back(label);
			painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(QRectF(0, y - 8, plot.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
					 label);
			painter.setPen(palette().color(QPalette::Mid));
		}
		setProperty("preflightYAxisLabels", y_axis_labels);

		if (!has_points || series.empty() || series.front().points.empty()) {
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(plot, Qt::AlignCenter, tr("Waiting for samples"));
		} else {
			for (const auto &line : warning_lines) {
				const qreal normalized = (line.value - minValue) / (maxValue - minValue);
				const qreal y = plot.bottom() - plot.height() * normalized;
				painter.setPen(QPen(line.color, 1.0, Qt::DashLine));
				painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
				const int label_width = painter.fontMetrics().horizontalAdvance(line.label) + 16;
				const qreal label_y = std::clamp(y - 9, plot.top(), plot.bottom() - 17);
				QRectF label_rect(plot.right() - label_width - 8, label_y, label_width, 17);
				painter.fillRect(label_rect, palette().color(QPalette::Base));
				painter.setPen(line.color);
				painter.drawText(label_rect, Qt::AlignRight | Qt::AlignVCenter, line.label);
			}

			for (const auto &line : series) {
				if (line.points.size() < 2) {
					continue;
				}
				const size_t first = line.points.size() > CHART_HISTORY_SIZE
							     ? line.points.size() - CHART_HISTORY_SIZE
							     : 0;
				const size_t count = line.points.size() - first;
				QPolygonF path;
				path.reserve(static_cast<qsizetype>(count));
				for (size_t i = first; i < line.points.size(); ++i) {
					const qreal x = plot.left() +
							plot.width() * static_cast<qreal>(i - first) /
								static_cast<qreal>(std::max<size_t>(1, count - 1));
					const double normalized = (line.points[i] - minValue) / (maxValue - minValue);
					const qreal y = plot.bottom() - plot.height() * normalized;
					path.append(QPointF(x, y));
				}
				painter.setPen(QPen(line.color, 2.0));
				painter.drawPolyline(path);
			}
		}
	}
};

class LoupeView final : public QWidget {
	std::shared_ptr<const preflight::loupe::FramePair> pair;
	std::optional<preflight::loupe::CropRegion> selection;
	std::optional<preflight::loupe::CropRegion> dragging_region;
	QRectF source_image_rect;
	QRectF source_panel;
	QRectF source_zoom_panel;
	QRectF encoded_zoom_panel;
	QPoint drag_start;
	QPoint moving_offset;
	std::optional<preflight::loupe::PixelRect> moving_crop;
	bool dragging = false;
	bool moving_selection = false;

	static uint32_t source_width(const preflight::loupe::PixelFrame &frame)
	{
		return frame.source_width ? frame.source_width : frame.width;
	}

	static uint32_t source_height(const preflight::loupe::PixelFrame &frame)
	{
		return frame.source_height ? frame.source_height : frame.height;
	}

	static QImage image_for(const preflight::loupe::PixelFrame &frame)
	{
		return QImage(frame.rgba.data(), static_cast<int>(frame.width), static_cast<int>(frame.height),
			      static_cast<int>(frame.stride), QImage::Format_RGBA8888);
	}

	static QRectF fitted_image_rect(const QRectF &bounds, const preflight::loupe::PixelFrame &frame)
	{
		if (frame.width == 0 || frame.height == 0 || bounds.width() <= 0 || bounds.height() <= 0) {
			return {};
		}
		const qreal scale = std::min(bounds.width() / frame.width, bounds.height() / frame.height);
		const QSizeF size(frame.width * scale, frame.height * scale);
		return QRectF(QPointF(bounds.center().x() - size.width() / 2.0,
				      bounds.center().y() - size.height() / 2.0),
			      size);
	}

	std::optional<QPoint> source_point(const QPointF &point) const
	{
		if (!pair || !pair->source || !source_image_rect.contains(point) || source_image_rect.width() <= 0 ||
		    source_image_rect.height() <= 0) {
			return std::nullopt;
		}
		const auto &frame = *pair->source;
		const uint32_t logical_width = source_width(frame);
		const uint32_t logical_height = source_height(frame);
		const qreal x = (point.x() - source_image_rect.left()) / source_image_rect.width();
		const qreal y = (point.y() - source_image_rect.top()) / source_image_rect.height();
		return QPoint(std::clamp(static_cast<int>(x * logical_width), 0, static_cast<int>(logical_width) - 1),
			      std::clamp(static_cast<int>(y * logical_height), 0,
					 static_cast<int>(logical_height) - 1));
	}

	std::optional<preflight::loupe::CropRegion> bounded_drag_region(const QPoint &end) const
	{
		if (!pair || !pair->source) {
			return std::nullopt;
		}
		const auto start = source_point(drag_start);
		const auto finish = source_point(end);
		if (!start || !finish) {
			return std::nullopt;
		}
		const auto &frame = *pair->source;
		const uint32_t logical_width = source_width(frame);
		const uint32_t logical_height = source_height(frame);
		const int max_width = std::max(
			1,
			static_cast<int>((std::min(source_zoom_panel.width(), encoded_zoom_panel.width()) - 8) / 4.0));
		const int max_height = std::max(
			1, static_cast<int>((std::min(source_zoom_panel.height(), encoded_zoom_panel.height()) - 28) /
					    4.0));
		int left = std::min(start->x(), finish->x());
		int top = std::min(start->y(), finish->y());
		int right = std::max(start->x(), finish->x());
		int bottom = std::max(start->y(), finish->y());
		if (right - left > max_width) {
			if (finish->x() >= start->x()) {
				right = start->x() + max_width;
			} else {
				left = start->x() - max_width;
			}
		}
		if (bottom - top > max_height) {
			if (finish->y() >= start->y()) {
				bottom = start->y() + max_height;
			} else {
				top = start->y() - max_height;
			}
		}
		return preflight::loupe::make_crop_region(left, top, right, bottom, logical_width, logical_height);
	}

	void draw_zoom(QPainter &painter, const QRectF &panel, const QString &label,
		       const preflight::loupe::PixelFrame &frame, const preflight::loupe::CropRegion &region) const
	{
		painter.setPen(palette().color(QPalette::Text));
		painter.drawText(panel.adjusted(4, 2, -4, -4), Qt::AlignTop | Qt::AlignHCenter, label);
		QRectF viewport = panel.adjusted(4, 24, -4, -4);
		painter.setPen(palette().color(QPalette::Mid));
		painter.drawRect(viewport);
		const auto zoomed = preflight::loupe::crop_at_4x(frame, region);
		if (!zoomed) {
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(viewport, Qt::AlignCenter, tr("Select a smaller area"));
			return;
		}
		const QRectF source_rect(0, 0, zoomed->width, zoomed->height);
		const QSizeF zoom_size(zoomed->width, zoomed->height);
		const QRectF target(QPointF(viewport.center().x() - zoom_size.width() / 2.0,
					    viewport.center().y() - zoom_size.height() / 2.0),
				    zoom_size);
		painter.save();
		painter.setClipRect(viewport);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
		painter.drawImage(target, image_for(*zoomed), source_rect);
		painter.restore();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), palette().color(QPalette::Base));
		const QRectF bounds = QRectF(rect()).adjusted(6, 5, -6, -6);
		const qreal gap = 8;
		const qreal preview_width = std::min<qreal>(190, std::max<qreal>(140, bounds.width() * 0.24));
		const qreal zoom_width = (bounds.width() - preview_width - gap * 2) / 2;
		source_panel = QRectF(bounds.left(), bounds.top(), preview_width, bounds.height());
		source_zoom_panel = QRectF(source_panel.right() + gap, bounds.top(), zoom_width, bounds.height());
		encoded_zoom_panel = QRectF(source_zoom_panel.right() + gap, bounds.top(), zoom_width, bounds.height());
		for (const QRectF &panel : {source_panel, source_zoom_panel, encoded_zoom_panel}) {
			painter.setPen(palette().color(QPalette::Mid));
			painter.drawRect(panel);
		}
		painter.setPen(palette().color(QPalette::Text));
		painter.drawText(source_panel.adjusted(4, 2, -4, -4), Qt::AlignTop | Qt::AlignHCenter,
				 tr("Source · drag to move"));
		if (!pair || !pair->source || !pair->encoded) {
			painter.drawText(bounds, Qt::AlignCenter, tr("Waiting for matching source and encoded frames"));
			return;
		}

		const auto &source = *pair->source;
		const auto &encoded = *pair->encoded;
		const QRectF preview_bounds = source_panel.adjusted(5, 24, -5, -5);
		source_image_rect = fitted_image_rect(preview_bounds, source);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
		painter.drawImage(source_image_rect, image_for(source));

		if (!selection) {
			const uint32_t logical_width = source_width(source);
			const uint32_t logical_height = source_height(source);
			const uint32_t visible_crop_width = static_cast<uint32_t>(std::max<qreal>(
				1, (std::min(source_zoom_panel.width(), encoded_zoom_panel.width()) - 8) / 4.0));
			const uint32_t visible_crop_height = static_cast<uint32_t>(std::max<qreal>(
				1, (std::min(source_zoom_panel.height(), encoded_zoom_panel.height()) - 28) / 4.0));
			const uint32_t crop_width =
				std::min({64U, visible_crop_width, std::max<uint32_t>(1, logical_width / 8)});
			const uint32_t crop_height =
				std::min({36U, visible_crop_height, std::max<uint32_t>(1, logical_height / 8)});
			selection = preflight::loupe::make_crop_region(
				static_cast<int32_t>((logical_width - crop_width) / 2),
				static_cast<int32_t>((logical_height - crop_height) / 2),
				static_cast<int32_t>((logical_width + crop_width) / 2),
				static_cast<int32_t>((logical_height + crop_height) / 2), logical_width,
				logical_height);
		}
		const auto active_region = dragging_region ? dragging_region : selection;
		if (!active_region) {
			return;
		}
		const auto source_crop = preflight::loupe::project_crop(*active_region, source.width, source.height);
		if (source_crop) {
			const QRectF selection_rect(
				source_image_rect.left() + source_image_rect.width() * source_crop->left / source.width,
				source_image_rect.top() + source_image_rect.height() * source_crop->top / source.height,
				source_image_rect.width() * source_crop->width / source.width,
				source_image_rect.height() * source_crop->height / source.height);
			painter.setPen(QPen(QColor("#facc15"), 2));
			painter.drawRect(selection_rect);
		}
		painter.drawText(source_panel.adjusted(4, source_panel.height() - 22, -4, -2), Qt::AlignCenter,
				 tr("Drag a small area"));
		draw_zoom(painter, source_zoom_panel, tr("Source · 400%"), source, *active_region);
		draw_zoom(painter, encoded_zoom_panel, tr("Encoded · 400%"), encoded, *active_region);
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		const auto point = source_point(event->position());
		if (event->button() == Qt::LeftButton && point) {
			dragging = true;
			drag_start = event->position().toPoint();
			moving_selection = false;
			moving_crop.reset();
			if (selection && pair && pair->source) {
				const auto projected = preflight::loupe::project_crop(
					*selection, source_width(*pair->source), source_height(*pair->source));
				if (projected && point->x() >= static_cast<int>(projected->left) &&
				    point->x() < static_cast<int>(projected->left + projected->width) &&
				    point->y() >= static_cast<int>(projected->top) &&
				    point->y() < static_cast<int>(projected->top + projected->height)) {
					moving_selection = true;
					moving_crop = projected;
					moving_offset = QPoint(point->x() - static_cast<int>(projected->left),
							       point->y() - static_cast<int>(projected->top));
					dragging_region = preflight::loupe::CropRegion{source_width(*pair->source),
										       source_height(*pair->source),
										       projected->left,
										       projected->top,
										       projected->width,
										       projected->height};
				}
			}
			if (!moving_selection) {
				dragging_region = bounded_drag_region(drag_start);
			}
			update();
			event->accept();
			return;
		}
		QWidget::mousePressEvent(event);
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (dragging) {
			if (moving_selection && moving_crop && pair && pair->source) {
				const auto point = source_point(event->position());
				if (point) {
					const int max_left =
						static_cast<int>(source_width(*pair->source) - moving_crop->width);
					const int max_top =
						static_cast<int>(source_height(*pair->source) - moving_crop->height);
					const uint32_t left = static_cast<uint32_t>(
						std::clamp(point->x() - moving_offset.x(), 0, max_left));
					const uint32_t top = static_cast<uint32_t>(
						std::clamp(point->y() - moving_offset.y(), 0, max_top));
					dragging_region = preflight::loupe::CropRegion{
						source_width(*pair->source), source_height(*pair->source), left, top,
						moving_crop->width,          moving_crop->height};
				}
			} else {
				dragging_region = bounded_drag_region(event->position().toPoint());
			}
			update();
			event->accept();
			return;
		}
		QWidget::mouseMoveEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (dragging && event->button() == Qt::LeftButton) {
			if (dragging_region) {
				selection = dragging_region;
			}
			dragging_region.reset();
			moving_crop.reset();
			dragging = false;
			moving_selection = false;
			update();
			event->accept();
			return;
		}
		QWidget::mouseReleaseEvent(event);
	}

public:
	explicit LoupeView(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumHeight(205);
		setMouseTracking(true);
		setToolTip(tr("Drag on the source image to compare the same region at four-times magnification."));
	}

	void SetPair(std::shared_ptr<const preflight::loupe::FramePair> new_pair)
	{
		if (pair == new_pair) {
			return;
		}
		pair = std::move(new_pair);
		if (!pair) {
			selection.reset();
		} else if (selection && pair->source &&
			   (selection->source_width != source_width(*pair->source) ||
			    selection->source_height != source_height(*pair->source))) {
			const auto remapped = preflight::loupe::project_crop(*selection, source_width(*pair->source),
									     source_height(*pair->source));
			if (remapped) {
				selection = preflight::loupe::CropRegion{source_width(*pair->source),
									 source_height(*pair->source),
									 remapped->left,
									 remapped->top,
									 remapped->width,
									 remapped->height};
			} else {
				selection.reset();
			}
		}
		update();
	}

	std::optional<preflight::loupe::CropRegion> SelectedCrop() const { return selection; }
};

struct ChartPane {
	MetricChart *chart = nullptr;
	QLabel *latest = nullptr;
	std::vector<QString> names;
	std::vector<QColor> colors;
};

struct BenchPoint {
	uint64_t end_ms = 0;
	double cpu_percent = 0.0;
	double average_frame_time_ms = 0.0;
	double frame_interval_ms = 0.0;
	double encoder_skipped_percent = 0.0;
	double render_lag_percent = 0.0;
	double dropped_percent = 0.0;
	double bitrate_kbps = 0.0;
	double congestion_percent = 0.0;
	double connect_time_ms = 0.0;
	int32_t verdict = OBS_PREFLIGHT_FAIL;
	QString reason;
};

struct BakeoffCandidate {
	std::string encoder_id;
	obs_preflight_mode mode{};
};

OBSDataAutoRelease clone_settings(obs_data_t *settings)
{
	OBSDataAutoRelease copy = obs_data_create();
	if (copy && settings) {
		obs_data_apply(copy, settings);
	}
	return copy;
}

QJsonObject json_object_from_settings(obs_data_t *settings)
{
	if (!settings) {
		return {};
	}
	const char *json = obs_data_get_json(settings);
	if (!json) {
		return {};
	}
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json));
	return document.isObject() ? document.object() : QJsonObject{};
}

QString output_error(obs_output_t *output, const QString &fallback)
{
	const char *error = output ? obs_output_get_last_error(output) : nullptr;
	return error && *error ? QString::fromUtf8(error) : fallback;
}

QString test_mode_label(preflight::UplinkTestMode mode)
{
	switch (mode) {
	case preflight::UplinkTestMode::TwitchBandwidth:
		return QObject::tr("Twitch ingest bandwidth test (bandwidthtest)");
	case preflight::UplinkTestMode::AmazonIvsBandwidth:
		return QObject::tr("Amazon IVS ingest bandwidth test (bandwidthtest)");
	case preflight::UplinkTestMode::RestreamBandwidth:
		return QObject::tr("Restream ingest test (test=true)");
	case preflight::UplinkTestMode::YouTubeThrowawayStream:
		return QObject::tr("YouTube one-use test stream key (no public broadcast)");
	case preflight::UplinkTestMode::CloudflareSpeedOnly:
		return QObject::tr("Cloudflare speed test only");
	}
	return {};
}

} // namespace

struct PreflightBenchWindow::Impl {
	PreflightBenchWindow *dialog;
	QPushButton *run_button = nullptr;
	QPushButton *bakeoff_button = nullptr;
	QLabel *destination_label = nullptr;
	QLabel *uplink_result_label = nullptr;
	QLabel *loupe_status_label = nullptr;
	QLabel *state_label = nullptr;
	QLabel *verdict_label = nullptr;
	QLabel *reason_label = nullptr;
	QLabel *recording_path_label = nullptr;
	QLabel *bakeoff_status_label = nullptr;
	QTableWidget *bakeoff_table = nullptr;
	QGroupBox *apply_group = nullptr;
	QLabel *apply_mode_label = nullptr;
	QLabel *apply_note = nullptr;
	QTableWidget *apply_table = nullptr;
	QPushButton *apply_button = nullptr;
	QTimer *sample_timer = nullptr;
	QTimer *loupe_timer = nullptr;
	QTimer *settings_timer = nullptr;
	QTimer *uplink_timer = nullptr;
	OBSPropertiesView *encoder_properties = nullptr;
	obs_data_t *initial_encoder_settings = nullptr;
	OBSServiceAutoRelease test_service;
	OBSOutputAutoRelease stream_output;
	OBSOutputAutoRelease recording_output;
	OBSEncoderAutoRelease stream_video_encoder;
	OBSEncoderAutoRelease stream_audio_encoder;
	OBSEncoderAutoRelease recording_video_encoder;
	OBSEncoderAutoRelease recording_audio_encoder;
	OBSEncoderAutoRelease bakeoff_encoder;
	obs_data_t *bakeoff_audio_settings = nullptr;
	obs_preflight_collector_t *collector = nullptr;
	LoupeView *loupe_view = nullptr;
	PreflightLoupeCapture loupe_capture;
	preflight::CloudflareSpeedTest cloudflare_speed_test;
	obs_preflight_engine_t *engine = nullptr;
	obs_preflight_engine_t *bakeoff_engine = nullptr;
	obs_preflight_bakeoff_t *bakeoff_planner = nullptr;
	std::vector<BenchPoint> points;
	std::vector<BakeoffCandidate> bakeoff_candidates;
	obs_preflight_candidate_plan active_candidate{};
	std::array<ChartPane, 8> charts;
	QElapsedTimer bakeoff_elapsed;
	QString supported_test_mode;
	QString recording_path;
	QString setup_error;
	QString youtube_test_error;
	QString stop_completion_status;
	QString bakeoff_candidate_reason;
	QString initial_output_mode;
	QString simple_output_encoder;
	QString apply_feedback;
	std::optional<preflight::apply::Plan> apply_plan;
	std::string configured_service_name;
	uint32_t current_output_width = 0;
	uint32_t current_output_height = 0;
	uint32_t current_fps_num = 0;
	uint32_t current_fps_den = 0;
	int64_t candidate_render_headroom_ns = 0;
	int64_t stream_bitrate_kbps = 0;
	double candidate_encoder_skip_percent = 0.0;
	int32_t candidate_verdict = OBS_PREFLIGHT_PASS;
	size_t active_bakeoff_row = 0;
	uint64_t last_window_end_ms = 0;
	uint64_t candidate_last_window_end_ms = 0;
	bool restart_stream_when_stopped = false;
	bool restart_was_active = false;
	bool bench_running = false;
	bool stopping = false;
	bool bakeoff_running = false;
	bool bakeoff_waiting_for_stop = false;
	bool bakeoff_cancelled = false;
	bool bakeoff_candidate_active = false;
	bool bakeoff_candidate_has_samples = false;
	bool cloudflare_speed_only = false;
	bool requires_youtube_throwaway_stream = false;
	bool close_when_stopped = false;
	bool simple_output_advanced = false;

	explicit Impl(PreflightBenchWindow *dialog_) : dialog(dialog_) {}

	~Impl()
	{
		StopLoupe();
		if (sample_timer) {
			sample_timer->stop();
		}
		if (settings_timer) {
			settings_timer->stop();
		}
		if (stream_output && obs_output_active(stream_output)) {
			obs_output_stop(stream_output);
		}
		if (recording_output && obs_output_active(recording_output)) {
			obs_output_stop(recording_output);
		}
		obs_preflight_collector_destroy(collector);
		obs_preflight_engine_destroy(engine);
		obs_preflight_engine_destroy(bakeoff_engine);
		obs_preflight_bakeoff_destroy(bakeoff_planner);
		if (initial_encoder_settings) {
			obs_data_release(initial_encoder_settings);
		}
		if (bakeoff_audio_settings) {
			obs_data_release(bakeoff_audio_settings);
		}
	}

	void SetStatus(const QString &text) { state_label->setText(text); }

	void StopOutputs(const QString &status, const QString &completion_status = QString())
	{
		StopLoupe();
		bench_running = false;
		restart_stream_when_stopped = false;
		restart_was_active = false;
		stopping = true;
		if (stream_output && obs_output_active(stream_output)) {
			obs_output_stop(stream_output);
		}
		if (recording_output && obs_output_active(recording_output)) {
			obs_output_stop(recording_output);
		}
		stop_completion_status = completion_status.isEmpty() ? QObject::tr("Stopped") : completion_status;
		run_button->setEnabled(false);
		run_button->setText(QObject::tr("Stopping…"));
		SetStatus(status);
		sample_timer->start(100);
	}

	void UpdateLoupe()
	{
		if (loupe_view) {
			loupe_view->SetPair(loupe_capture.LatestPair());
			if (const auto crop = loupe_view->SelectedCrop()) {
				loupe_capture.SetSourceCrop(*crop);
			}
		}
		if (loupe_status_label) {
			loupe_status_label->setText(QString::fromStdString(loupe_capture.Status()));
		}
	}

	void StartLoupe(obs_encoder_t *encoder)
	{
		if (!stream_output || !encoder) {
			return;
		}
		if (loupe_capture.Start(stream_output, encoder)) {
			loupe_timer->start(100);
		} else if (loupe_timer) {
			loupe_timer->stop();
		}
		UpdateLoupe();
	}

	void StopLoupe()
	{
		if (loupe_timer) {
			loupe_timer->stop();
		}
		loupe_capture.Stop();
		UpdateLoupe();
	}

	void FinishStopping()
	{
		stopping = false;
		sample_timer->stop();
		run_button->setEnabled(true);
		run_button->setText(QObject::tr("Start bench"));
		SetStatus(stop_completion_status);
		stop_completion_status.clear();
		if (close_when_stopped) {
			close_when_stopped = false;
			dialog->close();
		}
	}

	QJsonObject SimpleConfigSnapshot(OBSBasic *main) const
	{
		QJsonObject settings;
		config_t *config = main->Config();
		settings.insert(QStringLiteral("VBitrate"),
				static_cast<double>(config_get_uint(config, "SimpleOutput", "VBitrate")));
		settings.insert(QStringLiteral("UseAdvanced"), config_get_bool(config, "SimpleOutput", "UseAdvanced"));
		const QString preset_key = preflight::apply::SimplePresetConfigKey(simple_output_encoder);
		if (!preset_key.isEmpty()) {
			const QByteArray key = preset_key.toUtf8();
			const char *preset = config_get_string(config, "SimpleOutput", key.constData());
			settings.insert(preset_key, QString::fromUtf8(preset ? preset : ""));
		}
		if (simple_output_advanced && (simple_output_encoder == QStringLiteral("x264") ||
					       simple_output_encoder == QStringLiteral("x264_lowcpu"))) {
			const char *custom = config_get_string(config, "SimpleOutput", "x264Settings");
			settings.insert(QStringLiteral("x264Settings"), QString::fromUtf8(custom ? custom : ""));
		}
		return settings;
	}

	void RefreshApplyPreview()
	{
		if (!apply_table || !apply_button || !encoder_properties || !initial_encoder_settings) {
			return;
		}
		OBSBasic *main = OBSBasic::Get();
		if (!main) {
			apply_plan = preflight::apply::Plan{};
			apply_plan->error = QObject::tr("OBS settings are not available in this window.");
		} else {
			const char *mode_value = config_get_string(main->Config(), "Output", "Mode");
			const QString mode = QString::fromUtf8(mode_value ? mode_value : "");
			apply_mode_label->setText(QObject::tr("OBS output mode: %1 · mode stays unchanged")
							  .arg(mode.isEmpty() ? QObject::tr("unknown") : mode));
			const QJsonObject initial = json_object_from_settings(initial_encoder_settings);
			const QJsonObject edited = json_object_from_settings(encoder_properties->GetSettings());
			if (mode != initial_output_mode) {
				apply_plan = preflight::apply::Plan{};
				apply_plan->error = QObject::tr(
					"The OBS output mode changed while Preflight was open. Close and reopen Preflight to review the new mode.");
			} else if (mode == QStringLiteral("Advanced")) {
				const std::filesystem::path profile_dir = main->GetCurrentProfile().path;
				const QString saved_path =
					QString::fromStdString((profile_dir / "streamEncoder.json").string());
				const QByteArray saved_path_bytes = saved_path.toUtf8();
				OBSDataAutoRelease saved;
				if (QFileInfo::exists(saved_path)) {
					saved = obs_data_create_from_json_file_safe(saved_path_bytes.constData(),
										    "bak");
					if (!saved) {
						apply_plan = preflight::apply::Plan{};
						apply_plan->error = QObject::tr(
							"OBS could not read the saved stream encoder settings. No changes can be applied safely.");
					} else {
						apply_plan = preflight::apply::BuildAdvancedPlan(
							json_object_from_settings(saved), initial, edited);
					}
				} else {
					saved = obs_data_create();
					apply_plan = preflight::apply::BuildAdvancedPlan(
						json_object_from_settings(saved), initial, edited);
				}
			} else if (mode == QStringLiteral("Simple")) {
				apply_plan = preflight::apply::BuildSimplePlan(SimpleConfigSnapshot(main), initial,
									       edited, simple_output_encoder,
									       simple_output_advanced);
			} else {
				apply_plan = preflight::apply::Plan{};
				apply_plan->error =
					QObject::tr("This OBS output mode does not have a supported Apply mapping.");
			}
		}

		const auto &plan = *apply_plan;
		apply_table->setRowCount(static_cast<int>(plan.changes.size()));
		for (size_t row = 0; row < plan.changes.size(); ++row) {
			const auto &change = plan.changes[row];
			const int table_row = static_cast<int>(row);
			auto *key = new QTableWidgetItem(change.path);
			auto *before = new QTableWidgetItem(preflight::apply::ValueForDisplay(change.before));
			auto *after = new QTableWidgetItem(preflight::apply::ValueForDisplay(change.after));
			for (QTableWidgetItem *item : {key, before, after}) {
				item->setFlags(item->flags() & ~Qt::ItemIsEditable);
			}
			apply_table->setItem(table_row, 0, key);
			apply_table->setItem(table_row, 1, before);
			apply_table->setItem(table_row, 2, after);
		}
		apply_table->resizeRowsToContents();
		apply_button->setEnabled(plan.applicable());
		if (!plan.error.isEmpty()) {
			apply_note->setText(plan.error);
		} else if (plan.changes.empty()) {
			apply_note->setText(
				apply_feedback.isEmpty()
					? QObject::tr(
						  "Edit a setting to preview exact config keys. Nothing is written until Apply.")
					: apply_feedback);
		} else {
			apply_note->setText(QObject::tr(
				"Review the exact key changes above. Nothing is written until Apply is pressed."));
		}
	}

	void ApplyConfigChanges()
	{
		if (!apply_table || !apply_button) {
			return;
		}
		RefreshApplyPreview();
		if (!apply_plan || !apply_plan->applicable()) {
			return;
		}
		OBSBasic *main = OBSBasic::Get();
		if (!main) {
			return;
		}
		const preflight::apply::Plan plan = *apply_plan;
		if (plan.mode == preflight::apply::OutputMode::Advanced) {
			const std::filesystem::path profile_dir = main->GetCurrentProfile().path;
			const QString saved_path =
				QString::fromStdString((profile_dir / "streamEncoder.json").string());
			const QByteArray path_bytes = saved_path.toUtf8();
			const QByteArray json = QJsonDocument(plan.updated_settings).toJson(QJsonDocument::Indented);
			OBSDataAutoRelease updated = obs_data_create_from_json(json.constData());
			if (!updated || !obs_data_save_json_safe(updated, path_bytes.constData(), "tmp", "bak")) {
				apply_note->setText(QObject::tr(
					"OBS could not save streamEncoder.json. No settings were applied."));
				return;
			}
		} else if (plan.mode == preflight::apply::OutputMode::Simple) {
			config_t *config = main->Config();
			for (const auto &change : plan.changes) {
				if (change.key == QStringLiteral("VBitrate")) {
					config_set_uint(config, "SimpleOutput", "VBitrate",
							static_cast<uint64_t>(change.after.toDouble()));
				} else {
					const QByteArray key = change.key.toUtf8();
					const QByteArray value = change.after.toString().toUtf8();
					config_set_string(config, "SimpleOutput", key.constData(), value.constData());
				}
			}
			if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
				for (const auto &change : plan.changes) {
					const QByteArray key = change.key.toUtf8();
					if (change.before.isUndefined()) {
						config_remove_value(config, "SimpleOutput", key.constData());
					} else if (change.key == QStringLiteral("VBitrate")) {
						config_set_uint(config, "SimpleOutput", "VBitrate",
								static_cast<uint64_t>(change.before.toDouble()));
					} else {
						const QByteArray value = change.before.toString().toUtf8();
						config_set_string(config, "SimpleOutput", key.constData(),
								  value.constData());
					}
				}
				apply_note->setText(QObject::tr(
					"OBS could not save the Simple output settings. No settings were applied."));
				return;
			}
		} else {
			return;
		}

		OBSDataAutoRelease new_baseline = clone_settings(encoder_properties->GetSettings());
		if (new_baseline) {
			obs_data_addref(new_baseline);
			if (initial_encoder_settings) {
				obs_data_release(initial_encoder_settings);
			}
			initial_encoder_settings = new_baseline.Get();
		}
		if (!main->Active()) {
			main->ResetOutputs();
		}
		apply_feedback = QObject::tr("Applied to OBS %1 settings. The output mode was not changed.")
					 .arg(plan.mode == preflight::apply::OutputMode::Simple
						      ? QObject::tr("Simple")
						      : QObject::tr("Advanced"));
		RefreshApplyPreview();
	}

	bool BuildOutputs()
	{
		OBSBasic *main = OBSBasic::Get();
		if (!main) {
			setup_error = QObject::tr("OBS is not ready to read output settings.");
			return false;
		}
		const char *mode = config_get_string(main->Config(), "Output", "Mode");
		initial_output_mode = QString::fromUtf8(mode ? mode : "");
		if (initial_output_mode == QStringLiteral("Simple")) {
			const char *encoder = config_get_string(main->Config(), "SimpleOutput", "StreamEncoder");
			simple_output_encoder = QString::fromUtf8(encoder ? encoder : "");
			simple_output_advanced = config_get_bool(main->Config(), "SimpleOutput", "UseAdvanced");
		}
		OBSServiceAutoRelease selected_service = obs_frontend_get_streaming_service();
		if (!selected_service) {
			setup_error = QObject::tr("Choose a stream service in OBS before starting the bench.");
			return false;
		}

		OBSDataAutoRelease selected_service_settings = obs_service_get_settings(selected_service);
		if (!selected_service_settings) {
			setup_error = QObject::tr("Could not read the selected OBS stream service settings.");
			return false;
		}
		const char *service_name = obs_data_get_string(selected_service_settings, "service");
		const char *service_id = obs_service_get_id(selected_service);
		configured_service_name = service_name ? service_name : "";
		const auto uplink_mode = preflight::uplink_test_mode_for_service(configured_service_name);
		supported_test_mode = test_mode_label(uplink_mode);
		if (uplink_mode == preflight::UplinkTestMode::CloudflareSpeedOnly) {
			cloudflare_speed_only = true;
			setup_error = QObject::tr(
				"This service has no non-public OBS test mode. Cloudflare will measure network speed only; no OBS stream is sent.");
			return false;
		}

		auto safe_key = preflight::make_non_public_test_key(
			service_name, obs_data_get_string(selected_service_settings, "key"));
		requires_youtube_throwaway_stream = uplink_mode == preflight::UplinkTestMode::YouTubeThrowawayStream;
#ifndef YOUTUBE_ENABLED
		if (requires_youtube_throwaway_stream) {
			setup_error = QObject::tr("YouTube's private test-key flow is unavailable in this OBS build.");
			return false;
		}
#endif
		if ((!safe_key && !requires_youtube_throwaway_stream) || supported_test_mode.isEmpty() || !service_id ||
		    strcmp(service_id, "rtmp_common") != 0) {
			setup_error = QObject::tr(
				"This service has no supported non-public test mode yet. Preflight will not send a public stream.");
			return false;
		}

		OBSDataAutoRelease test_service_settings = clone_settings(selected_service_settings);
		if (!test_service_settings) {
			setup_error = QObject::tr("Could not copy the OBS stream service settings.");
			return false;
		}
		obs_data_set_string(test_service_settings, "key", safe_key ? safe_key->c_str() : "");
		test_service = obs_service_create(obs_service_get_id(selected_service), "preflight_non_public_test",
						  test_service_settings, nullptr);
		if (!test_service) {
			setup_error = QObject::tr("Could not create the non-public test service.");
			return false;
		}

		OBSOutputAutoRelease configured_stream = obs_frontend_get_streaming_output();
		OBSOutputAutoRelease configured_recording = obs_frontend_get_recording_output();
		OBSEncoderAutoRelease configured_stream_video =
			configured_stream ? obs_output_get_video_encoder(configured_stream) : nullptr;
		OBSEncoderAutoRelease configured_stream_audio =
			configured_stream ? obs_output_get_audio_encoder(configured_stream, 0) : nullptr;
		OBSEncoderAutoRelease configured_recording_video =
			configured_recording ? obs_output_get_video_encoder(configured_recording) : nullptr;
		OBSEncoderAutoRelease configured_recording_audio =
			configured_recording ? obs_output_get_audio_encoder(configured_recording, 0) : nullptr;

		if (!configured_stream_video) {
			setup_error = QObject::tr("OBS has no configured streaming video encoder.");
			return false;
		}

		OBSDataAutoRelease stream_video_settings = obs_encoder_get_settings(configured_stream_video);
		OBSDataAutoRelease stream_audio_settings =
			configured_stream_audio ? OBSDataAutoRelease(obs_encoder_get_settings(configured_stream_audio))
						: OBSDataAutoRelease(obs_data_create());
		OBSDataAutoRelease recording_video_settings =
			configured_recording_video
				? OBSDataAutoRelease(obs_encoder_get_settings(configured_recording_video))
				: clone_settings(stream_video_settings);
		OBSDataAutoRelease recording_audio_settings =
			configured_recording_audio
				? OBSDataAutoRelease(obs_encoder_get_settings(configured_recording_audio))
				: clone_settings(stream_audio_settings);
		if (!stream_video_settings || !stream_audio_settings || !recording_video_settings ||
		    !recording_audio_settings) {
			setup_error = QObject::tr("Could not read the configured encoder settings.");
			return false;
		}
		bakeoff_audio_settings = stream_audio_settings.Get();
		obs_data_addref(bakeoff_audio_settings);
		stream_bitrate_kbps = obs_data_get_int(stream_video_settings, "bitrate");
		const char *stream_video_id = obs_encoder_get_id(configured_stream_video);
		const char *stream_audio_id = configured_stream_audio ? obs_encoder_get_id(configured_stream_audio)
								      : "ffmpeg_aac";
		const char *recording_video_id =
			configured_recording_video ? obs_encoder_get_id(configured_recording_video) : stream_video_id;
		const char *recording_audio_id =
			configured_recording_audio ? obs_encoder_get_id(configured_recording_audio) : stream_audio_id;
		const size_t recording_audio_mixer =
			configured_recording_audio ? obs_encoder_get_mixer_index(configured_recording_audio)
			: configured_stream_audio  ? obs_encoder_get_mixer_index(configured_stream_audio)
						   : 0;
		if (recording_audio_mixer >= std::numeric_limits<size_t>::digits) {
			setup_error = QObject::tr("The configured recording audio mixer is not supported.");
			return false;
		}

		if (!stream_audio_settings) {
			setup_error = QObject::tr("Could not read the configured stream audio encoder settings.");
			return false;
		}
		obs_service_apply_encoder_settings(test_service, stream_video_settings, stream_audio_settings);
		OBSDataAutoRelease property_settings = clone_settings(stream_video_settings);
		if (!property_settings) {
			setup_error = QObject::tr("Could not create the stream encoder settings view.");
			return false;
		}
		OBSDataAutoRelease initial_settings = clone_settings(property_settings);
		if (!initial_settings) {
			setup_error = QObject::tr("Could not snapshot the current stream encoder settings.");
			return false;
		}
		obs_data_addref(initial_settings);
		initial_encoder_settings = initial_settings.Get();
		encoder_properties = new OBSPropertiesView(property_settings.Get(), stream_video_id,
							   (PropertiesReloadCallback)obs_get_encoder_properties, 190);
		encoder_properties->setFrameShape(QFrame::NoFrame);
		encoder_properties->setScrolling(true);
		QObject::connect(encoder_properties, &OBSPropertiesView::Changed, dialog,
				 [this]() { settings_timer->start(600); });

		stream_video_encoder = obs_video_encoder_create(stream_video_id, "preflight_stream_video",
								stream_video_settings, nullptr);
		stream_audio_encoder = obs_audio_encoder_create(
			stream_audio_id, "preflight_stream_audio", stream_audio_settings,
			configured_stream_audio ? obs_encoder_get_mixer_index(configured_stream_audio) : 0, nullptr);
		recording_video_encoder = obs_video_encoder_create(recording_video_id, "preflight_recording_video",
								   recording_video_settings, nullptr);
		recording_audio_encoder = obs_audio_encoder_create(recording_audio_id, "preflight_recording_audio",
								   recording_audio_settings, recording_audio_mixer,
								   nullptr);
		if (!stream_video_encoder || !stream_audio_encoder || !recording_video_encoder ||
		    !recording_audio_encoder) {
			setup_error = QObject::tr("Could not create a separate stream and recording encoder pair.");
			return false;
		}

		video_t *video = obs_get_video();
		audio_t *audio = obs_get_audio();
		if (!video || !audio) {
			setup_error = QObject::tr("OBS audio and video must be initialized before starting Preflight.");
			return false;
		}
		obs_encoder_set_video(stream_video_encoder, video);
		obs_encoder_set_audio(stream_audio_encoder, audio);
		obs_encoder_set_video(recording_video_encoder, video);
		obs_encoder_set_audio(recording_audio_encoder, audio);

		const char *service_output_type = obs_service_get_preferred_output_type(test_service);
		const char *stream_output_type =
			service_output_type && (obs_get_output_flags(service_output_type) & OBS_OUTPUT_SERVICE)
				? service_output_type
				: "rtmp_output";
		if ((obs_get_output_flags(stream_output_type) & OBS_OUTPUT_SERVICE) == 0) {
			setup_error = QObject::tr("OBS has no service output available for this test.");
			return false;
		}

		stream_output = obs_output_create(stream_output_type, "preflight_test_stream", nullptr, nullptr);
		recording_output = obs_output_create("ffmpeg_muxer", "preflight_local_recording", nullptr, nullptr);
		if (!stream_output || !recording_output) {
			setup_error = QObject::tr("Could not create both Preflight outputs.");
			return false;
		}

		OBSDataAutoRelease stream_output_settings = obs_output_get_settings(configured_stream);
		if (!stream_output_settings) {
			setup_error = QObject::tr("Could not read the OBS streaming output settings.");
			return false;
		}
		obs_output_update(stream_output, stream_output_settings);

		obs_output_set_video_encoder(stream_output, stream_video_encoder);
		obs_output_set_audio_encoder(stream_output, stream_audio_encoder, 0);
		obs_output_set_service(stream_output, test_service);
		obs_output_set_reconnect_settings(stream_output, 0, 0);
		obs_output_set_video_encoder(recording_output, recording_video_encoder);
		obs_output_set_audio_encoder(recording_output, recording_audio_encoder, 0);
		obs_output_set_mixers(recording_output, size_t{1} << recording_audio_mixer);

		collector = obs_preflight_collector_create();
		engine = obs_preflight_engine_create_with_window(SAMPLE_WINDOW_MS);
		if (!collector || !engine) {
			setup_error = QObject::tr("Could not initialize the Preflight stats collector.");
			return false;
		}

		return true;
	}

	bool PrepareYouTubeTestKey()
	{
		youtube_test_error.clear();
		if (!requires_youtube_throwaway_stream) {
			return true;
		}
#if defined(YOUTUBE_ENABLED) && !defined(PREFLIGHT_UI_RENDER_TEST)
		OBSBasic *main = OBSBasic::Get();
		auto *youtube = main ? dynamic_cast<YoutubeApiWrappers *>(main->GetAuth()) : nullptr;
		if (!youtube || configured_service_name != youtube->service()) {
			youtube_test_error = QObject::tr(
				"Connect the YouTube account that matches the selected OBS stream service before testing.");
			return false;
		}

		// Match the wizard: a new non-reusable ingest stream key is created for each output start.
		StreamDescription stream = {"", "", "OBS Studio Test Stream"};
		if (!youtube->InsertStream(stream) || stream.name.isEmpty()) {
			QString detail = youtube->GetLastError();
			youtube_test_error =
				detail.isEmpty() ? QObject::tr("YouTube could not create a one-use test stream key.")
						 : QObject::tr("YouTube could not create a one-use test stream key: %1")
							   .arg(detail);
			return false;
		}

		OBSDataAutoRelease settings = obs_service_get_settings(test_service);
		if (!settings) {
			youtube_test_error = QObject::tr("Could not update the one-use YouTube test stream key.");
			return false;
		}
		const QByteArray key = stream.name.toUtf8();
		obs_data_set_string(settings, "key", key.constData());
		obs_service_update(test_service, settings);
		return true;
#else
		youtube_test_error = QObject::tr("YouTube's private test-key flow is unavailable in this OBS build.");
		return false;
#endif
	}

	void ToggleCloudflareSpeedTest()
	{
		if (cloudflare_speed_test.IsRunning()) {
			cloudflare_speed_test.Cancel();
			run_button->setEnabled(false);
			run_button->setText(QObject::tr("Stopping…"));
			SetStatus(QObject::tr("Stopping the Cloudflare speed test."));
			return;
		}
		if (!cloudflare_speed_test.Start()) {
			SetStatus(QObject::tr("Could not start the Cloudflare speed test."));
			return;
		}
		run_button->setText(QObject::tr("Cancel speed test"));
		SetStatus(QObject::tr("Cloudflare speed test only · no OBS stream is sent."));
		if (uplink_result_label) {
			uplink_result_label->setText(QObject::tr("Measuring download and upload · up to 4 MiB each."));
		}
		uplink_timer->start(100);
	}

	void PollCloudflareSpeedTest()
	{
		auto result = cloudflare_speed_test.TakeResult();
		if (!result) {
			return;
		}
		uplink_timer->stop();
		run_button->setEnabled(true);
		run_button->setText(QObject::tr("Run speed test only"));
		if (result->cancelled) {
			SetStatus(QObject::tr("Cloudflare speed test canceled."));
			uplink_result_label->setText(QObject::tr("Canceled before both directions finished."));
		} else if (!result->success) {
			SetStatus(QObject::tr("Cloudflare speed test failed."));
			uplink_result_label->setText(
				result->error.empty() ? QObject::tr("Cloudflare returned no usable measurements.")
						      : QString::fromStdString(result->error));
		} else {
			SetStatus(QObject::tr("Cloudflare speed test complete."));
			uplink_result_label->setText(QObject::tr("Download %1 Mb/s · Upload %2 Mb/s")
							     .arg(result->download_mbps, 0, 'f', 1)
							     .arg(result->upload_mbps, 0, 'f', 1));
		}
		if (close_when_stopped) {
			close_when_stopped = false;
			dialog->close();
		}
	}

	bool BuildBakeoffPlan()
	{
		std::vector<std::string> encoders;
		for (size_t index = 0;; ++index) {
			const char *encoder_id = nullptr;
			if (!obs_enum_encoder_types(index, &encoder_id)) {
				break;
			}
			if (encoder_id && obs_get_encoder_type(encoder_id) == OBS_ENCODER_VIDEO) {
				encoders.emplace_back(encoder_id);
			}
		}
		std::sort(encoders.begin(), encoders.end());
		encoders.erase(std::unique(encoders.begin(), encoders.end()), encoders.end());
		if (encoders.empty()) {
			SetBakeoffStatus(QObject::tr("No video encoders are registered in OBS."));
			return false;
		}

		struct obs_video_info video_info = {};
		if (!obs_get_video_info(&video_info) || video_info.output_width == 0 || video_info.output_height == 0 ||
		    video_info.fps_num == 0 || video_info.fps_den == 0) {
			SetBakeoffStatus(QObject::tr("OBS video settings are not ready for an encoder bake-off."));
			return false;
		}
		current_output_width = video_info.output_width;
		current_output_height = video_info.output_height;
		current_fps_num = video_info.fps_num;
		current_fps_den = video_info.fps_den;

		struct Scale {
			uint32_t numerator;
			uint32_t denominator;
		};
		constexpr std::array<Scale, 3> scales = {{{1, 1}, {2, 3}, {1, 2}}};
		constexpr std::array<uint32_t, 2> frame_rate_divisors = {1, 2};
		std::vector<obs_preflight_mode> modes;
		for (const Scale &scale : scales) {
			const uint64_t width =
				(static_cast<uint64_t>(current_output_width) * scale.numerator / scale.denominator) &
				~UINT64_C(1);
			const uint64_t height =
				(static_cast<uint64_t>(current_output_height) * scale.numerator / scale.denominator) &
				~UINT64_C(1);
			if (width == 0 || height == 0 || width > UINT32_MAX || height > UINT32_MAX) {
				continue;
			}
			for (uint32_t divisor : frame_rate_divisors) {
				const uint64_t fps_den = static_cast<uint64_t>(current_fps_den) * divisor;
				if (fps_den > UINT32_MAX) {
					continue;
				}
				const obs_preflight_mode mode = {
					.width = static_cast<uint32_t>(width),
					.height = static_cast<uint32_t>(height),
					.fps_num = current_fps_num,
					.fps_den = static_cast<uint32_t>(fps_den),
				};
				const auto duplicate =
					std::find_if(modes.begin(), modes.end(), [&mode](const auto &existing) {
						return existing.width == mode.width && existing.height == mode.height &&
						       existing.fps_num == mode.fps_num &&
						       existing.fps_den == mode.fps_den;
					});
				if (duplicate == modes.end()) {
					modes.push_back(mode);
				}
			}
		}
		if (modes.empty()) {
			SetBakeoffStatus(QObject::tr("OBS has no usable resolution and frame-rate candidates."));
			return false;
		}

		bakeoff_candidates.clear();
		bakeoff_candidates.reserve(encoders.size() * modes.size());
		for (const std::string &encoder_id : encoders) {
			for (const obs_preflight_mode &mode : modes) {
				bakeoff_candidates.push_back({encoder_id, mode});
			}
		}

		std::vector<const char *> encoder_ids;
		encoder_ids.reserve(encoders.size());
		for (const std::string &encoder_id : encoders) {
			encoder_ids.push_back(encoder_id.c_str());
		}
		bakeoff_planner = obs_preflight_bakeoff_create(encoder_ids.data(), encoder_ids.size(), modes.data(),
							       modes.size(), BAKEOFF_CANDIDATE_DURATION_MS);
		if (!bakeoff_planner) {
			SetBakeoffStatus(QObject::tr("The preflight engine could not build the bake-off plan."));
			return false;
		}

		bakeoff_table->setRowCount(static_cast<int>(bakeoff_candidates.size()));
		for (size_t row = 0; row < bakeoff_candidates.size(); ++row) {
			const BakeoffCandidate &candidate = bakeoff_candidates[row];
			const QString fps =
				QString::number(static_cast<double>(candidate.mode.fps_num) / candidate.mode.fps_den,
						'f', candidate.mode.fps_den == 1 ? 0 : 2);
			bakeoff_table->setItem(static_cast<int>(row), 0,
					       new QTableWidgetItem(QString::fromStdString(candidate.encoder_id)));
			bakeoff_table->setItem(
				static_cast<int>(row), 1,
				new QTableWidgetItem(
					QStringLiteral("%1 × %2").arg(candidate.mode.width).arg(candidate.mode.height)));
			bakeoff_table->setItem(static_cast<int>(row), 2, new QTableWidgetItem(fps));
			bakeoff_table->setItem(static_cast<int>(row), 3, new QTableWidgetItem(QStringLiteral("—")));
			bakeoff_table->setItem(static_cast<int>(row), 4, new QTableWidgetItem(QStringLiteral("—")));
			bakeoff_table->setItem(static_cast<int>(row), 5, new QTableWidgetItem(QObject::tr("Pending")));
			bakeoff_table->setItem(static_cast<int>(row), 6, new QTableWidgetItem());
		}
		bakeoff_table->resizeRowsToContents();
		SetBakeoffStatus(QObject::tr("Ready to test %1 candidates (%2 seconds each).")
					 .arg(bakeoff_candidates.size())
					 .arg(BAKEOFF_CANDIDATE_DURATION_MS / 1000));
		return true;
	}

	void SetBakeoffStatus(const QString &text)
	{
		if (bakeoff_status_label) {
			bakeoff_status_label->setText(text);
		}
	}

	bool PrepareRecordingPath()
	{
		const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
		QDir directory(movies.isEmpty() ? QDir::tempPath() : movies);
		const QString output_directory = directory.filePath(QStringLiteral("OBS Preflight"));
		if (!QDir().mkpath(output_directory)) {
			SetStatus(QObject::tr("Could not create the local Preflight recording folder."));
			return false;
		}

		recording_path = QDir(output_directory)
					 .filePath(QStringLiteral("Preflight-%1.mkv")
							   .arg(QDateTime::currentDateTime().toString(
								   QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
		OBSDataAutoRelease recording_settings = obs_output_get_settings(recording_output);
		if (!recording_settings) {
			SetStatus(QObject::tr("Could not prepare the local recording settings."));
			return false;
		}
		obs_data_set_string(recording_settings, "path", recording_path.toUtf8().constData());
		obs_data_set_string(recording_settings, "muxer_settings", "");
		obs_data_set_bool(recording_settings, "allow_overwrite", false);
		obs_output_update(recording_output, recording_settings);
		if (recording_path_label) {
			recording_path_label->setText(QObject::tr("Saved to %1").arg(recording_path));
		}
		return true;
	}

	void AddChart(QGridLayout *layout, int row, int column, size_t index, const QString &title,
		      const std::vector<QString> &names, const std::vector<QColor> &colors)
	{
		auto *frame = new QFrame(dialog);
		frame->setFrameShape(QFrame::StyledPanel);
		auto *pane_layout = new QVBoxLayout(frame);
		pane_layout->setContentsMargins(8, 5, 8, 8);
		auto *header = new QHBoxLayout;
		auto *title_label = new QLabel(title, frame);
		QFont title_font = title_label->font();
		title_font.setBold(true);
		title_label->setFont(title_font);
		auto *latest = new QLabel(QStringLiteral("—"), frame);
		latest->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		header->addWidget(title_label);
		header->addStretch(1);
		header->addWidget(latest);
		pane_layout->addLayout(header);
		auto *chart = new MetricChart(frame);
		chart->setObjectName(QStringLiteral("preflight-chart-%1").arg(index));
		pane_layout->addWidget(chart, 1);
		layout->addWidget(frame, row, column);
		charts[index] = {chart, latest, names, colors};
	}

	void BuildUi()
	{
		auto *root = new QVBoxLayout(dialog);
		auto *header = new QHBoxLayout;
		auto *title = new QLabel(QObject::tr("Preflight live bench"), dialog);
		QFont title_font = title->font();
		title_font.setPointSize(title_font.pointSize() + 3);
		title_font.setBold(true);
		title->setFont(title_font);
		state_label = new QLabel(QObject::tr("Ready"), dialog);
		run_button = new QPushButton(cloudflare_speed_only ? QObject::tr("Run speed test only")
								   : QObject::tr("Start bench"),
					     dialog);
		header->addWidget(title);
		header->addSpacing(12);
		header->addWidget(state_label);
		header->addStretch(1);
		header->addWidget(run_button);
		root->addLayout(header);

		destination_label = new QLabel(
			supported_test_mode.isEmpty() ? QObject::tr("No safe test service selected")
			: cloudflare_speed_only       ? supported_test_mode
						      : QObject::tr("Non-public test: %1").arg(supported_test_mode),
			dialog);
		destination_label->setWordWrap(true);
		root->addWidget(destination_label);
		if (cloudflare_speed_only) {
			auto *privacy_note = new QLabel(
				QObject::tr(
					"Speed test only: Cloudflare receives your IP and up to 4 MiB in each direction; "
					"it collects measurements for aggregated connection-quality insights. No OBS stream is sent."),
				dialog);
			privacy_note->setWordWrap(true);
			root->addWidget(privacy_note);
			uplink_result_label = new QLabel(QObject::tr("No speed test has run yet."), dialog);
			uplink_result_label->setWordWrap(true);
			root->addWidget(uplink_result_label);
		}

		auto *splitter = new QSplitter(Qt::Horizontal, dialog);
		splitter->setObjectName(QStringLiteral("preflight-main-splitter"));
		auto *settings_scroll = new QScrollArea(splitter);
		settings_scroll->setObjectName(QStringLiteral("preflight-settings-scroll"));
		settings_scroll->setWidgetResizable(true);
		auto *settings_panel = new QWidget(settings_scroll);
		auto *settings_layout = new QVBoxLayout(settings_panel);
		auto *encoder_group = new QGroupBox(QObject::tr("Stream encoder settings"), settings_panel);
		auto *encoder_layout = new QVBoxLayout(encoder_group);
		if (encoder_properties) {
			encoder_layout->addWidget(encoder_properties);
		} else {
			encoder_layout->addWidget(new QLabel(
				setup_error.isEmpty() ? QObject::tr("Encoder settings unavailable") : setup_error,
				encoder_group));
		}
		settings_layout->addWidget(encoder_group);
		if (encoder_properties) {
			apply_group = new QGroupBox(QObject::tr("Apply to OBS"), settings_panel);
			apply_group->setObjectName(QStringLiteral("preflight-apply-preview"));
			auto *apply_layout = new QVBoxLayout(apply_group);
			apply_mode_label = new QLabel(apply_group);
			apply_mode_label->setWordWrap(true);
			apply_table = new QTableWidget(0, 3, apply_group);
			apply_table->setObjectName(QStringLiteral("preflight-apply-diff"));
			apply_table->setHorizontalHeaderLabels(
				{QObject::tr("Config key"), QObject::tr("Current"), QObject::tr("Proposed")});
			apply_table->verticalHeader()->hide();
			apply_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
			apply_table->setWordWrap(true);
			apply_table->setTextElideMode(Qt::ElideNone);
			apply_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
			apply_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
			apply_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
			apply_table->setMaximumHeight(190);
			apply_note = new QLabel(apply_group);
			apply_note->setObjectName(QStringLiteral("preflight-apply-note"));
			apply_note->setWordWrap(true);
			apply_button = new QPushButton(QObject::tr("Apply to OBS"), apply_group);
			apply_button->setObjectName(QStringLiteral("preflight-apply-button"));
			apply_button->setEnabled(false);
			apply_layout->addWidget(apply_mode_label);
			apply_layout->addWidget(apply_table);
			apply_layout->addWidget(apply_note);
			apply_layout->addWidget(apply_button);
			settings_layout->addWidget(apply_group);
		}
		auto *recording_group = new QGroupBox(QObject::tr("Local recording"), settings_panel);
		auto *recording_layout = new QVBoxLayout(recording_group);
		recording_path_label = new QLabel(
			QObject::tr("A new local file path appears here when the bench starts."), recording_group);
		recording_path_label->setWordWrap(true);
		recording_layout->addWidget(recording_path_label);
		recording_layout->addWidget(new QLabel(
			QObject::tr("The recording uses its own encoder, so stream changes leave it running."),
			recording_group));
		settings_layout->addWidget(recording_group);
		settings_layout->addStretch(1);
		settings_scroll->setWidget(settings_panel);
		settings_scroll->setMinimumWidth(320);
		settings_scroll->setVisible(encoder_properties != nullptr);
		splitter->addWidget(settings_scroll);

		auto *charts_scroll = new QScrollArea(splitter);
		charts_scroll->setObjectName(QStringLiteral("preflight-charts-scroll"));
		charts_scroll->setWidgetResizable(true);
		auto *chart_container = new QWidget(charts_scroll);
		auto *chart_column = new QVBoxLayout(chart_container);
		auto *chart_layout = new QGridLayout;
		chart_layout->setSpacing(8);
		AddChart(chart_layout, 0, 0, 0, QObject::tr("CPU"), {QObject::tr("CPU")}, {QColor("#60a5fa")});
		AddChart(chart_layout, 0, 1, 1, QObject::tr("Render time vs frame interval"),
			 {QObject::tr("Render"), QObject::tr("Interval")}, {QColor("#34d399"), QColor("#fbbf24")});
		AddChart(chart_layout, 1, 0, 2, QObject::tr("Encoder skipped frames (%)"), {QObject::tr("Skipped")},
			 {QColor("#f97316")});
		AddChart(chart_layout, 1, 1, 3, QObject::tr("Render-missed frames (%)"), {QObject::tr("Missed")},
			 {QColor("#f87171")});
		AddChart(chart_layout, 2, 0, 4, QObject::tr("Stream dropped frames (%)"), {QObject::tr("Dropped")},
			 {QColor("#fb7185")});
		AddChart(chart_layout, 2, 1, 5, QObject::tr("Stream bitrate"), {QObject::tr("kb/s")},
			 {QColor("#a78bfa")});
		AddChart(chart_layout, 3, 0, 6, QObject::tr("Congestion"), {QObject::tr("%")}, {QColor("#38bdf8")});
		AddChart(chart_layout, 3, 1, 7, QObject::tr("Connect time"), {QObject::tr("ms")}, {QColor("#facc15")});
		for (int row = 0; row < 4; ++row) {
			chart_layout->setRowStretch(row, 1);
		}
		chart_layout->setColumnStretch(0, 1);
		chart_layout->setColumnStretch(1, 1);
		charts[1].chart->SetWarningLines({
			{QObject::tr("75% interval"), 0.0, QColor("#fbbf24")},
			{QObject::tr("100% interval"), 0.0, QColor("#f87171")},
		});
		const std::vector<ChartWarningLine> frame_loss_warnings = {
			{QObject::tr("1% OBS warning"), OBS_STATS_WARNING_PERCENT, QColor("#fbbf24")},
			{QObject::tr("5% OBS danger"), OBS_STATS_DANGER_PERCENT, QColor("#f87171")},
		};
		charts[2].chart->SetWarningLines(frame_loss_warnings);
		charts[3].chart->SetWarningLines(frame_loss_warnings);
		charts[4].chart->SetWarningLines(frame_loss_warnings);
		charts[6].chart->SetWarningLines({
			{QObject::tr("33.33%"), OBS_CONGESTION_WARNING_PERCENT, QColor("#fbbf24")},
			{QObject::tr("66.67%"), OBS_CONGESTION_DANGER_PERCENT, QColor("#f87171")},
		});
		chart_column->addLayout(chart_layout, 1);

		auto *loupe_group = new QGroupBox(QObject::tr("400% source vs encoded-frame loupe"), chart_container);
		loupe_group->setObjectName(QStringLiteral("preflight-loupe-group"));
		auto *loupe_layout = new QVBoxLayout(loupe_group);
		loupe_status_label = new QLabel(QObject::tr("Loupe is waiting for the live bench."), loupe_group);
		loupe_status_label->setWordWrap(true);
		loupe_view = new LoupeView(loupe_group);
		loupe_view->setObjectName(QStringLiteral("preflight-loupe-view"));
		loupe_layout->addWidget(loupe_status_label);
		loupe_layout->addWidget(loupe_view, 1);
		chart_column->addWidget(loupe_group, 0);
		charts_scroll->setWidget(chart_container);
		splitter->addWidget(charts_scroll);
		splitter->setStretchFactor(0, 0);
		splitter->setStretchFactor(1, 1);
		root->addWidget(splitter, 1);

		auto *bakeoff_group = new QGroupBox(QObject::tr("Encoder bake-off"), dialog);
		bakeoff_group->setObjectName(QStringLiteral("preflight-bakeoff-group"));
		auto *bakeoff_layout = new QVBoxLayout(bakeoff_group);
		auto *bakeoff_controls = new QHBoxLayout;
		auto *bakeoff_description = new QLabel(
			QObject::tr(
				"Tests every registered video encoder at the current, two-thirds and half resolution, "
				"at the current and half frame rate. Each candidate uses the non-public test output."),
			bakeoff_group);
		bakeoff_description->setWordWrap(true);
		bakeoff_status_label = new QLabel(QObject::tr("No bake-off has run yet."), bakeoff_group);
		bakeoff_button = new QPushButton(QObject::tr("Run bake-off"), bakeoff_group);
		bakeoff_controls->addWidget(bakeoff_status_label, 1);
		bakeoff_controls->addWidget(bakeoff_button);
		bakeoff_table = new QTableWidget(0, 7, bakeoff_group);
		bakeoff_table->setObjectName(QStringLiteral("preflight-bakeoff-table"));
		bakeoff_table->setHorizontalHeaderLabels(
			{QObject::tr("Encoder"), QObject::tr("Resolution"), QObject::tr("FPS"), QObject::tr("Headroom"),
			 QObject::tr("Skipped"), QObject::tr("Verdict"), QObject::tr("Reason")});
		bakeoff_table->verticalHeader()->hide();
		bakeoff_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		bakeoff_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		bakeoff_table->setSortingEnabled(false);
		bakeoff_table->setWordWrap(true);
		bakeoff_table->setTextElideMode(Qt::ElideNone);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
		bakeoff_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		bakeoff_table->setMinimumHeight(145);
		bakeoff_layout->addWidget(bakeoff_description);
		bakeoff_layout->addLayout(bakeoff_controls);
		bakeoff_layout->addWidget(bakeoff_table);
		root->addWidget(bakeoff_group);

		auto *summary = new QFrame(dialog);
		summary->setObjectName(QStringLiteral("preflight-verdict-summary"));
		auto *summary_layout = new QHBoxLayout(summary);
		verdict_label = new QLabel(QObject::tr("Waiting"), summary);
		verdict_label->setObjectName(QStringLiteral("preflight-verdict"));
		QFont verdict_font = verdict_label->font();
		verdict_font.setPointSize(verdict_font.pointSize() + 2);
		verdict_font.setBold(true);
		verdict_label->setFont(verdict_font);
		reason_label = new QLabel(
			QObject::tr("Start a non-public test and Preflight will report the current OBS warning bands."),
			summary);
		reason_label->setObjectName(QStringLiteral("preflight-reason"));
		reason_label->setWordWrap(true);
		summary_layout->addWidget(verdict_label, 0, Qt::AlignTop);
		summary_layout->addWidget(reason_label, 1);
		root->insertWidget(2, summary);

		if (cloudflare_speed_only) {
			bakeoff_button->setEnabled(false);
			SetStatus(QObject::tr("Ready · Cloudflare speed test only"));
		} else if (!setup_error.isEmpty()) {
			SetStatus(setup_error);
			run_button->setEnabled(false);
			bakeoff_button->setEnabled(false);
		} else if (!supported_test_mode.isEmpty()) {
			SetStatus(QObject::tr("Ready · %1").arg(supported_test_mode));
		}
	}

	void LoadRecordedData(const PreflightBenchWindowRecordedData &recorded_data)
	{
		for (const obs_preflight_window &window : recorded_data.windows) {
			PushPoint(window);
		}

		bakeoff_table->setRowCount(static_cast<int>(recorded_data.bakeoff_rows.size()));
		for (size_t row = 0; row < recorded_data.bakeoff_rows.size(); ++row) {
			const auto &recorded = recorded_data.bakeoff_rows[row];
			const auto &result = recorded.result;
			const int table_row = static_cast<int>(row);
			bakeoff_table->setItem(table_row, 0, new QTableWidgetItem(recorded.encoder));
			bakeoff_table->setItem(table_row, 1, new QTableWidgetItem(recorded.resolution));
			bakeoff_table->setItem(table_row, 2, new QTableWidgetItem(recorded.fps));
			bakeoff_table->setItem(table_row, 3,
					       new QTableWidgetItem(QStringLiteral("%1 ms").arg(
						       result.render_headroom_ns / 1'000'000.0, 0, 'f', 2)));
			bakeoff_table->setItem(table_row, 4,
					       new QTableWidgetItem(QStringLiteral("%1 %").arg(
						       result.encoder_skip_percent, 0, 'f', 2)));
			bakeoff_table->setItem(table_row, 5, new QTableWidgetItem(BakeoffVerdictText(result.verdict)));
			bakeoff_table->setItem(table_row, 6, new QTableWidgetItem(QString::fromUtf8(result.reason)));
		}
		bakeoff_table->resizeRowsToContents();
		bakeoff_status_label->setText(QObject::tr("Recorded results from the preflight engine."));
		if (loupe_view) {
			loupe_view->SetPair(recorded_data.loupe_pair);
		}
		if (loupe_status_label) {
			loupe_status_label->setText(
				recorded_data.loupe_pair ? QObject::tr("Comparing recorded source and decoded frames.")
							 : QObject::tr("No recorded frame pair is available."));
		}
	}

	void PushPoint(const obs_preflight_window &window)
	{
		if (window.end_ms <= last_window_end_ms) {
			return;
		}
		last_window_end_ms = window.end_ms;
		BenchPoint point;
		point.end_ms = window.end_ms;
		point.cpu_percent = window.cpu_percent;
		point.average_frame_time_ms = window.average_frame_time_ns / 1'000'000.0;
		point.frame_interval_ms = window.frame_interval_ns / 1'000'000.0;
		point.encoder_skipped_percent = window.encoder_skip_percent;
		point.render_lag_percent = window.render_lag_percent;
		point.dropped_percent = window.output_drop_percent;
		point.bitrate_kbps = window.output_bitrate_kbps;
		point.congestion_percent = window.congestion_percent;
		point.connect_time_ms = static_cast<double>(window.connect_time_ms);
		point.verdict = window.verdict;
		point.reason = QString::fromUtf8(window.reason);
		points.push_back(std::move(point));
		if (points.size() > CHART_HISTORY_SIZE) {
			points.erase(points.begin(),
				     points.begin() + static_cast<std::ptrdiff_t>(points.size() - CHART_HISTORY_SIZE));
		}
		UpdateCharts();
		UpdateVerdict();
	}

	void UpdateCharts()
	{
		auto values = [this](auto get) {
			std::vector<double> result;
			result.reserve(points.size());
			for (const auto &point : points) {
				result.push_back(get(point));
			}
			return result;
		};
		const std::array<std::vector<double>, 9> data = {
			values([](const BenchPoint &p) { return p.cpu_percent; }),
			values([](const BenchPoint &p) { return p.average_frame_time_ms; }),
			values([](const BenchPoint &p) { return p.frame_interval_ms; }),
			values([](const BenchPoint &p) { return p.encoder_skipped_percent; }),
			values([](const BenchPoint &p) { return p.render_lag_percent; }),
			values([](const BenchPoint &p) { return p.dropped_percent; }),
			values([](const BenchPoint &p) { return p.bitrate_kbps; }),
			values([](const BenchPoint &p) { return p.congestion_percent; }),
			values([](const BenchPoint &p) { return p.connect_time_ms; }),
		};
		const std::array<QString, 8> latest = {
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 %").arg(points.back().cpu_percent, 0, 'f', 1),
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 / %2 ms")
						 .arg(points.back().average_frame_time_ms, 0, 'f', 2)
						 .arg(points.back().frame_interval_ms, 0, 'f', 2),
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 %").arg(points.back().encoder_skipped_percent, 0, 'f', 2),
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 %").arg(points.back().render_lag_percent, 0, 'f', 2),
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 %").arg(points.back().dropped_percent, 0, 'f', 2),
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 kb/s").arg(points.back().bitrate_kbps, 0, 'f', 0),
			points.empty() ? QStringLiteral("—")
				       : QStringLiteral("%1 %").arg(points.back().congestion_percent, 0, 'f', 1),
			points.empty() || points.back().connect_time_ms <= 0
				? QStringLiteral("Waiting")
				: QStringLiteral("%1 ms").arg(points.back().connect_time_ms, 0, 'f', 0),
		};
		for (size_t i = 0; i < charts.size(); ++i) {
			std::vector<ChartSeries> lines;
			static constexpr std::array<size_t, 8> data_indices = {0, 1, 3, 4, 5, 6, 7, 8};
			if (i == 1) {
				lines.push_back({charts[i].names[0], charts[i].colors[0], data[1]});
				lines.push_back({charts[i].names[1], charts[i].colors[1], data[2]});
			} else {
				lines.push_back({charts[i].names[0], charts[i].colors[0], data[data_indices[i]]});
			}
			charts[i].chart->SetSeries(std::move(lines));
			charts[i].latest->setText(latest[i]);
		}
		const double frame_interval = points.empty() ? 0.0 : points.back().frame_interval_ms;
		charts[1].chart->SetWarningLines({
			{QObject::tr("75% interval"), frame_interval * OBS_RENDER_WARNING_FRACTION, QColor("#fbbf24")},
			{QObject::tr("100% interval"), frame_interval * OBS_RENDER_DANGER_FRACTION, QColor("#f87171")},
		});
	}

	void UpdateVerdict()
	{
		if (points.empty()) {
			return;
		}
		const auto &point = points.back();
		const QString verdict = point.verdict == OBS_PREFLIGHT_PASS       ? QObject::tr("Pass")
					: point.verdict == OBS_PREFLIGHT_MARGINAL ? QObject::tr("Marginal")
										  : QObject::tr("Fail");
		verdict_label->setText(verdict);
		reason_label->setText(point.reason);
		const QString badge_color = point.verdict == OBS_PREFLIGHT_PASS       ? QStringLiteral("#166534")
					    : point.verdict == OBS_PREFLIGHT_MARGINAL ? QStringLiteral("#854d0e")
										      : QStringLiteral("#991b1b");
		verdict_label->setStyleSheet(
			QStringLiteral(
				"QLabel { background: %1; color: white; border-radius: 6px; padding: 7px 14px; }")
				.arg(badge_color));
	}

	QString BakeoffVerdictText(int32_t verdict) const
	{
		if (verdict == OBS_PREFLIGHT_PASS) {
			return QObject::tr("Pass");
		}
		if (verdict == OBS_PREFLIGHT_MARGINAL) {
			return QObject::tr("Marginal");
		}
		return QObject::tr("Fail");
	}

	void SetBakeoffCell(int column, const QString &text)
	{
		if (bakeoff_table && active_bakeoff_row < static_cast<size_t>(bakeoff_table->rowCount())) {
			bakeoff_table->item(static_cast<int>(active_bakeoff_row), column)->setText(text);
		}
	}

	void SetCandidateMetrics(const obs_preflight_window &window)
	{
		candidate_render_headroom_ns = window.render_headroom_ns;
		candidate_encoder_skip_percent = window.encoder_skip_percent;
		SetBakeoffCell(3, QStringLiteral("%1 ms").arg(candidate_render_headroom_ns / 1'000'000.0, 0, 'f', 2));
		SetBakeoffCell(4, QStringLiteral("%1 %").arg(candidate_encoder_skip_percent, 0, 'f', 2));
	}

	void CompleteBakeoffCandidate(int32_t verdict, const QString &reason)
	{
		if (!bakeoff_candidate_active || !bakeoff_planner) {
			return;
		}
		candidate_verdict = std::max(candidate_verdict, verdict);
		const QString final_reason = reason.isEmpty() ? bakeoff_candidate_reason : reason;
		const int32_t status = obs_preflight_bakeoff_complete(bakeoff_planner, candidate_verdict);
		if (status != OBS_PREFLIGHT_OK) {
			SetBakeoffStatus(QObject::tr("The preflight engine could not record a candidate result."));
		}
		bakeoff_candidate_active = false;
		SetBakeoffCell(5, BakeoffVerdictText(candidate_verdict));
		SetBakeoffCell(6, final_reason.isEmpty() ? QObject::tr("No plain-language reason was reported.")
							 : final_reason);
		bakeoff_table->scrollToItem(bakeoff_table->item(static_cast<int>(active_bakeoff_row), 0));
		bakeoff_waiting_for_stop = true;
		StopLoupe();
		if (stream_output && obs_output_active(stream_output)) {
			obs_output_stop(stream_output);
			sample_timer->start(100);
			SetBakeoffStatus(QObject::tr("Candidate %1 of %2 finished; stopping its output.")
						 .arg(active_candidate.order)
						 .arg(bakeoff_candidates.size()));
		} else {
			QTimer::singleShot(0, dialog, [this]() { ContinueBakeoffAfterStop(); });
		}
	}

	void FinishBakeoff(bool cancelled)
	{
		StopLoupe();
		if (stream_output && !obs_output_active(stream_output)) {
			obs_output_set_video_encoder(stream_output, stream_video_encoder);
		}
		bakeoff_encoder = nullptr;
		bakeoff_running = false;
		bakeoff_waiting_for_stop = false;
		bakeoff_candidate_active = false;
		bakeoff_candidate_has_samples = false;
		bakeoff_cancelled = false;
		sample_timer->stop();
		obs_preflight_engine_destroy(bakeoff_engine);
		bakeoff_engine = nullptr;
		obs_preflight_bakeoff_destroy(bakeoff_planner);
		bakeoff_planner = nullptr;
		bakeoff_button->setEnabled(setup_error.isEmpty());
		bakeoff_button->setText(QObject::tr("Run bake-off"));
		run_button->setEnabled(setup_error.isEmpty());
		if (encoder_properties) {
			encoder_properties->setEnabled(true);
		}
		SetBakeoffStatus(cancelled ? QObject::tr("Bake-off stopped. Completed results remain in the table.")
					   : QObject::tr("Bake-off complete. Results for %1 candidates are shown.")
						     .arg(bakeoff_candidates.size()));
		SetStatus(cancelled ? QObject::tr("Bake-off stopped.") : QObject::tr("Bake-off complete."));
		if (close_when_stopped) {
			close_when_stopped = false;
			dialog->close();
		}
	}

	void ContinueBakeoffAfterStop()
	{
		if (!bakeoff_waiting_for_stop) {
			return;
		}
		bakeoff_waiting_for_stop = false;
		if (stream_output && !obs_output_active(stream_output)) {
			obs_output_set_video_encoder(stream_output, stream_video_encoder);
		}
		bakeoff_encoder = nullptr;
		if (bakeoff_engine) {
			obs_preflight_engine_destroy(bakeoff_engine);
		}
		bakeoff_engine = obs_preflight_engine_create_with_window(SAMPLE_WINDOW_MS);
		if (bakeoff_cancelled) {
			FinishBakeoff(true);
			return;
		}
		if (!bakeoff_engine) {
			SetBakeoffStatus(QObject::tr("Could not prepare the next candidate's stats window."));
			FinishBakeoff(true);
			return;
		}
		StartNextBakeoffCandidate();
	}

	void StartNextBakeoffCandidate()
	{
		if (!bakeoff_running || !bakeoff_planner) {
			return;
		}
		active_candidate = {};
		const int32_t next_status = obs_preflight_bakeoff_next(bakeoff_planner, &active_candidate);
		if (next_status == OBS_PREFLIGHT_COMPLETE) {
			FinishBakeoff(false);
			return;
		}
		if (next_status != OBS_PREFLIGHT_OK || active_candidate.order == 0 ||
		    active_candidate.order > bakeoff_candidates.size()) {
			SetBakeoffStatus(QObject::tr("The preflight engine could not read the next candidate."));
			FinishBakeoff(true);
			return;
		}

		active_bakeoff_row = active_candidate.order - 1;
		bakeoff_candidate_active = true;
		bakeoff_candidate_has_samples = false;
		candidate_verdict = OBS_PREFLIGHT_PASS;
		candidate_render_headroom_ns = 0;
		candidate_encoder_skip_percent = 0.0;
		candidate_last_window_end_ms = 0;
		bakeoff_candidate_reason.clear();
		SetBakeoffCell(3, QStringLiteral("—"));
		SetBakeoffCell(4, QStringLiteral("—"));
		SetBakeoffCell(5, QObject::tr("Running"));
		SetBakeoffCell(6, QString());
		if (!PrepareYouTubeTestKey()) {
			bakeoff_cancelled = true;
			CompleteBakeoffCandidate(OBS_PREFLIGHT_FAIL, youtube_test_error);
			return;
		}

		OBSDataAutoRelease settings = obs_encoder_defaults(active_candidate.encoder);
		if (!settings) {
			CompleteBakeoffCandidate(OBS_PREFLIGHT_FAIL,
						 QObject::tr("OBS could not create settings for this encoder."));
			return;
		}
		if (stream_bitrate_kbps > 0) {
			obs_data_set_int(settings, "bitrate", stream_bitrate_kbps);
		}
		obs_service_apply_encoder_settings(test_service, settings, bakeoff_audio_settings);
		bakeoff_encoder = obs_video_encoder_create(active_candidate.encoder, "preflight_bakeoff_video",
							   settings, nullptr);
		if (!bakeoff_encoder) {
			CompleteBakeoffCandidate(
				OBS_PREFLIGHT_FAIL,
				QObject::tr("OBS could not create this encoder on the current device."));
			return;
		}

		video_t *video = obs_get_video();
		if (!video || active_candidate.fps_num != current_fps_num || current_fps_den == 0 ||
		    active_candidate.fps_den % current_fps_den != 0) {
			CompleteBakeoffCandidate(
				OBS_PREFLIGHT_FAIL,
				QObject::tr(
					"OBS could not attach this frame-rate candidate to the current video mix."));
			return;
		}
		obs_encoder_set_video(bakeoff_encoder, video);
		if (active_candidate.width != current_output_width ||
		    active_candidate.height != current_output_height) {
			obs_encoder_set_scaled_size(bakeoff_encoder, active_candidate.width, active_candidate.height);
		}
		const uint32_t divisor = active_candidate.fps_den / current_fps_den;
		if (divisor == 0 || !obs_encoder_set_frame_rate_divisor(bakeoff_encoder, divisor)) {
			CompleteBakeoffCandidate(
				OBS_PREFLIGHT_FAIL,
				QObject::tr("This encoder could not apply the requested frame-rate divisor."));
			return;
		}

		obs_output_set_video_encoder(stream_output, bakeoff_encoder);
		StartLoupe(bakeoff_encoder);
		if (!obs_output_start(stream_output)) {
			CompleteBakeoffCandidate(
				OBS_PREFLIGHT_FAIL,
				output_error(stream_output,
					     QObject::tr("This encoder could not start the private test output.")));
			return;
		}
		bakeoff_elapsed.start();
		sample_timer->start(static_cast<int>(SAMPLE_WINDOW_MS));
		SetBakeoffStatus(QObject::tr("Testing candidate %1 of %2: %3.")
					 .arg(active_candidate.order)
					 .arg(bakeoff_candidates.size())
					 .arg(QString::fromUtf8(active_candidate.encoder)));
	}

	void StopBakeoff()
	{
		if (!bakeoff_running) {
			return;
		}
		bakeoff_cancelled = true;
		bakeoff_button->setEnabled(false);
		bakeoff_button->setText(QObject::tr("Stopping…"));
		if (bakeoff_candidate_active) {
			SetBakeoffCell(5, QObject::tr("Stopped"));
			SetBakeoffCell(6, QObject::tr("Stopped before this candidate finished."));
			bakeoff_candidate_active = false;
		}
		bakeoff_waiting_for_stop = true;
		StopLoupe();
		if (stream_output && obs_output_active(stream_output)) {
			obs_output_stop(stream_output);
			sample_timer->start(100);
		} else {
			QTimer::singleShot(0, dialog, [this]() { ContinueBakeoffAfterStop(); });
		}
		SetBakeoffStatus(QObject::tr("Stopping the current private test candidate."));
	}

	void SampleBakeoff()
	{
		if (!bakeoff_running || !bakeoff_candidate_active) {
			return;
		}
		if (!stream_output || !obs_output_active(stream_output)) {
			CompleteBakeoffCandidate(
				OBS_PREFLIGHT_FAIL,
				output_error(stream_output,
					     QObject::tr("The private test output stopped unexpectedly.")));
			return;
		}
		video_t *video = obs_encoder_video(bakeoff_encoder);
		if (obs_preflight_collect_sample(collector, bakeoff_engine, stream_output, video) == OBS_PREFLIGHT_OK) {
			size_t count = 0;
			if (obs_preflight_engine_get_window_count(bakeoff_engine, &count) == OBS_PREFLIGHT_OK &&
			    count > 0) {
				obs_preflight_window window = {};
				if (obs_preflight_engine_get_window(bakeoff_engine, count - 1, &window) ==
					    OBS_PREFLIGHT_OK &&
				    window.end_ms > candidate_last_window_end_ms) {
					candidate_last_window_end_ms = window.end_ms;
					bakeoff_candidate_has_samples = true;
					SetCandidateMetrics(window);
					PushPoint(window);
					if (window.verdict >= candidate_verdict) {
						candidate_verdict = window.verdict;
						bakeoff_candidate_reason = QString::fromUtf8(window.reason);
					}
					SetBakeoffCell(5, BakeoffVerdictText(candidate_verdict));
					if (candidate_verdict == OBS_PREFLIGHT_FAIL) {
						CompleteBakeoffCandidate(candidate_verdict, bakeoff_candidate_reason);
						return;
					}
				}
			}
		}

		if (bakeoff_elapsed.elapsed() >= static_cast<qint64>(active_candidate.duration_ms)) {
			if (!bakeoff_candidate_has_samples) {
				CompleteBakeoffCandidate(
					OBS_PREFLIGHT_FAIL,
					QObject::tr("The encoder produced no usable samples during this test."));
			} else {
				CompleteBakeoffCandidate(candidate_verdict, bakeoff_candidate_reason);
			}
		}
	}

	void UpdateEncoderSettings()
	{
		RefreshApplyPreview();
		if (!stream_video_encoder || !encoder_properties) {
			return;
		}
		if (stream_output && obs_output_active(stream_output)) {
			StopLoupe();
			restart_stream_when_stopped = true;
			restart_was_active = true;
			obs_output_stop(stream_output);
			SetStatus(QObject::tr("Restarting the stream test output; local recording continues."));
			return;
		}
		obs_encoder_update(stream_video_encoder, encoder_properties->GetSettings());
		if (restart_stream_when_stopped && restart_was_active && stream_output) {
			restart_stream_when_stopped = false;
			restart_was_active = false;
			if (!PrepareYouTubeTestKey()) {
				StopOutputs(QObject::tr("Stopping after the YouTube test key could not be refreshed."),
					    youtube_test_error);
				return;
			}
			StartLoupe(stream_video_encoder);
			if (!obs_output_start(stream_output)) {
				StopLoupe();
				SetStatus(output_error(stream_output, QObject::tr("Stream test could not restart.")));
			} else {
				SetStatus(QObject::tr("Stream test restarted; local recording was not interrupted."));
			}
		}
	}
};

PreflightBenchWindow::PreflightBenchWindow(QWidget *parent) : PreflightBenchWindow(parent, nullptr) {}

PreflightBenchWindow::PreflightBenchWindow(const PreflightBenchWindowRecordedData &recorded_data, QWidget *parent)
	: PreflightBenchWindow(parent, &recorded_data)
{
}

PreflightBenchWindow::PreflightBenchWindow(QWidget *parent, const PreflightBenchWindowRecordedData *recorded_data)
	: QDialog(parent),
	  impl(std::make_unique<Impl>(this))
{
	setObjectName(QStringLiteral("preflight-bench-window"));
	setWindowTitle(tr("Preflight live bench"));
	resize(1280, 1020);
	impl->sample_timer = new QTimer(this);
	impl->sample_timer->setInterval(static_cast<int>(SAMPLE_WINDOW_MS));
	connect(impl->sample_timer, &QTimer::timeout, this, &PreflightBenchWindow::Sample);
	impl->loupe_timer = new QTimer(this);
	impl->loupe_timer->setInterval(100);
	connect(impl->loupe_timer, &QTimer::timeout, this, [this]() { impl->UpdateLoupe(); });
	impl->settings_timer = new QTimer(this);
	impl->settings_timer->setSingleShot(true);
	connect(impl->settings_timer, &QTimer::timeout, this, &PreflightBenchWindow::ApplyEncoderSettings);
	impl->uplink_timer = new QTimer(this);
	impl->uplink_timer->setInterval(100);
	connect(impl->uplink_timer, &QTimer::timeout, this, [this]() { impl->PollCloudflareSpeedTest(); });
	if (recorded_data) {
		impl->setup_error = tr("This preview uses recorded samples; live OBS outputs are unavailable.");
	} else {
		impl->BuildOutputs();
	}
	impl->BuildUi();
	impl->RefreshApplyPreview();
	if (recorded_data) {
		impl->LoadRecordedData(*recorded_data);
	}
	connect(impl->run_button, &QPushButton::clicked, this, &PreflightBenchWindow::ToggleBench);
	connect(impl->bakeoff_button, &QPushButton::clicked, this, &PreflightBenchWindow::ToggleBakeoff);
	if (impl->apply_button) {
		connect(impl->apply_button, &QPushButton::clicked, this, [this]() { impl->ApplyConfigChanges(); });
	}
}

PreflightBenchWindow::~PreflightBenchWindow() = default;

void PreflightBenchWindow::closeEvent(QCloseEvent *event)
{
	if (impl->cloudflare_speed_test.IsRunning()) {
		impl->close_when_stopped = true;
		impl->cloudflare_speed_test.Cancel();
		impl->run_button->setEnabled(false);
		impl->run_button->setText(tr("Stopping…"));
		impl->SetStatus(tr("Stopping the Cloudflare speed test before closing."));
		impl->uplink_timer->start(100);
		event->ignore();
		return;
	}
	if (impl->bakeoff_running || impl->bakeoff_waiting_for_stop) {
		impl->close_when_stopped = true;
		if (impl->bakeoff_running && !impl->bakeoff_cancelled) {
			impl->StopBakeoff();
		}
		event->ignore();
		return;
	}
	if (impl->bench_running || impl->stopping) {
		impl->close_when_stopped = true;
		if (!impl->stopping) {
			impl->StopOutputs(tr("Stopping the stream test and local recording."), tr("Stopped"));
		}
		event->ignore();
		return;
	}
	event->accept();
}

void PreflightBenchWindow::ToggleBench()
{
	if (impl->cloudflare_speed_only) {
		impl->ToggleCloudflareSpeedTest();
		return;
	}
	if (impl->bench_running) {
		impl->StopOutputs(tr("Stopping the stream test and local recording."), tr("Stopped"));
		return;
	}
	if (impl->bakeoff_running || impl->stopping) {
		impl->SetStatus(tr("Stop the current preflight operation before starting the live bench."));
		return;
	}

	if (!impl->setup_error.isEmpty()) {
		impl->SetStatus(impl->setup_error);
		return;
	}
	if (!impl->PrepareYouTubeTestKey()) {
		impl->SetStatus(impl->youtube_test_error);
		return;
	}
	if (!impl->PrepareRecordingPath()) {
		return;
	}
	if (!obs_output_start(impl->recording_output)) {
		impl->SetStatus(output_error(impl->recording_output, tr("Local recording could not start.")));
		return;
	}
	impl->StartLoupe(impl->stream_video_encoder);
	if (!obs_output_start(impl->stream_output)) {
		const QString error = output_error(impl->stream_output, tr("Non-public stream test could not start."));
		impl->StopOutputs(tr("Stopping the local recording after the stream test failed."), error);
		return;
	}
	impl->bench_running = true;
	impl->run_button->setText(tr("Stop bench"));
	impl->SetStatus(tr("Recording locally while the non-public stream test runs."));
	impl->sample_timer->start();
}

void PreflightBenchWindow::ToggleBakeoff()
{
	if (impl->cloudflare_speed_only) {
		impl->SetBakeoffStatus(tr("Bake-off needs a service with a non-public OBS test mode."));
		return;
	}
	if (impl->bakeoff_running) {
		impl->StopBakeoff();
		return;
	}
	if (!impl->setup_error.isEmpty()) {
		impl->SetBakeoffStatus(impl->setup_error);
		return;
	}
	if (impl->bench_running || impl->stopping || (impl->stream_output && obs_output_active(impl->stream_output))) {
		impl->SetBakeoffStatus(tr("Stop the live bench before starting the bake-off."));
		return;
	}
	if (!impl->BuildBakeoffPlan()) {
		return;
	}
	impl->bakeoff_engine = obs_preflight_engine_create_with_window(SAMPLE_WINDOW_MS);
	if (!impl->bakeoff_engine) {
		impl->SetBakeoffStatus(tr("Could not create the bake-off stats engine."));
		obs_preflight_bakeoff_destroy(impl->bakeoff_planner);
		impl->bakeoff_planner = nullptr;
		return;
	}
	impl->bakeoff_running = true;
	impl->bakeoff_cancelled = false;
	impl->bakeoff_button->setText(tr("Stop bake-off"));
	impl->run_button->setEnabled(false);
	if (impl->encoder_properties) {
		impl->encoder_properties->setEnabled(false);
	}
	impl->SetStatus(tr("Running each candidate through the non-public test output."));
	impl->StartNextBakeoffCandidate();
}

void PreflightBenchWindow::Sample()
{
	if (impl->stopping) {
		const bool stream_stopped = !impl->stream_output || !obs_output_active(impl->stream_output);
		const bool recording_stopped = !impl->recording_output || !obs_output_active(impl->recording_output);
		if (stream_stopped && recording_stopped) {
			impl->FinishStopping();
		}
		return;
	}
	if (impl->bakeoff_waiting_for_stop) {
		if (!impl->stream_output || !obs_output_active(impl->stream_output)) {
			impl->ContinueBakeoffAfterStop();
		}
		return;
	}
	if (impl->bakeoff_running) {
		impl->SampleBakeoff();
		return;
	}

	if (!impl->stream_output || !obs_output_active(impl->stream_output)) {
		if (impl->restart_stream_when_stopped) {
			impl->UpdateEncoderSettings();
			return;
		}
		if (impl->bench_running) {
			impl->StopOutputs(tr("Stopping the local recording after the stream test ended."),
					  output_error(impl->stream_output, tr("The stream test stopped.")));
		}
		return;
	}

	if (impl->recording_output && !obs_output_active(impl->recording_output)) {
		impl->StopOutputs(tr("Stopping the stream test after the local recording stopped."),
				  output_error(impl->recording_output, tr("The local recording stopped.")));
		return;
	}

	video_t *video = obs_encoder_video(impl->stream_video_encoder);
	const int32_t status = obs_preflight_collect_sample(impl->collector, impl->engine, impl->stream_output, video);
	if (status != OBS_PREFLIGHT_OK) {
		return;
	}

	size_t count = 0;
	if (obs_preflight_engine_get_window_count(impl->engine, &count) != OBS_PREFLIGHT_OK) {
		return;
	}
	if (count >= MAX_ENGINE_WINDOWS) {
		obs_preflight_engine_destroy(impl->engine);
		impl->engine = obs_preflight_engine_create_with_window(SAMPLE_WINDOW_MS);
		impl->last_window_end_ms = 0;
		return;
	}
	if (count == 0) {
		return;
	}
	obs_preflight_window window = {};
	if (obs_preflight_engine_get_window(impl->engine, count - 1, &window) == OBS_PREFLIGHT_OK) {
		impl->PushPoint(window);
	}
}

void PreflightBenchWindow::ApplyEncoderSettings()
{
	impl->UpdateEncoderSettings();
}
