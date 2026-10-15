#include "preflight-bench-window.hpp"

#include "obs-preflight-collector.h"
#include "obs-preflight.h"
#include "preflight-cloudflare-speed-test.hpp"
#include "preflight-apply-plan.hpp"
#include "preflight-loupe-capture.hpp"
#include "preflight-test-mode.hpp"

#include <properties-view.hpp>
#include <qt-wrappers.hpp>

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
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
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
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
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
#include <map>
#include <numeric>
#include <string>
#include <vector>

#ifdef PREFLIGHT_UI_RENDER_TEST
// The recorded-data test never creates live outputs. Keep its link independent
// of OBSBasic's generated Qt UI while compiling the production Apply code.
class OBSBasic {
public:
	struct Profile {
		std::string name;
		std::filesystem::path path;
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
constexpr int MAX_BAKEOFF_TABLE_VIEW_HEIGHT = 280;
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

// Reads the text colour the active OBS theme gives a class such as "text-warning", so painted
// charts use the same warning and danger colours as the Stats window.
QColor theme_class_color(QWidget *context, const char *classes)
{
	QLabel probe(context);
	probe.hide();
	setClasses(&probe, classes);
	probe.ensurePolished();
	return probe.palette().color(QPalette::WindowText);
}

class MetricChart final : public QWidget {
	std::vector<ChartSeries> series;
	std::vector<ChartWarningLine> warning_lines;
	bool starts_at_zero = false;

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

	void SetStartsAtZero(bool value)
	{
		starts_at_zero = value;
		setProperty("preflightStartsAtZero", value);
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
		QRectF plot = QRectF(rect()).adjusted(70, 24, -10, -22);
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
		auto nice_step = [](double raw_step) {
			if (raw_step <= 0.0 || !std::isfinite(raw_step)) {
				return 1.0;
			}
			const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
			const double fraction = raw_step / magnitude;
			const double rounded = fraction <= 1.0   ? 1.0
					       : fraction <= 2.0 ? 2.0
					       : fraction <= 2.5 ? 2.5
					       : fraction <= 5.0 ? 5.0
								 : 10.0;
			return rounded * magnitude;
		};
		if (starts_at_zero) {
			minValue = 0.0;
			const double step = nice_step(std::max(0.0, maxValue) / 3.0);
			maxValue = std::max(step * 3.0, step);
		} else {
			if (maxValue - minValue < 0.001) {
				const double padding = std::max(1.0, std::abs(maxValue) * 0.05);
				minValue -= padding;
				maxValue += padding;
			}
			const double step = nice_step((maxValue - minValue) / 3.0);
			const double original_min = minValue;
			const double original_max = maxValue;
			minValue = std::floor(original_min / step) * step;
			maxValue = minValue + step * 3.0;
			if (maxValue < original_max) {
				maxValue = std::ceil(original_max / step) * step;
				minValue = maxValue - step * 3.0;
			}
		}
		const double axis_step = (maxValue - minValue) / 3.0;
		QStringList y_axis_labels;
		painter.setPen(palette().color(QPalette::Mid));
		for (int row = 0; row <= 3; ++row) {
			const qreal y = plot.top() + plot.height() * row / 3.0;
			const double value = maxValue - (maxValue - minValue) * row / 3.0;
			int precision = 0;
			double scaled_step = axis_step;
			while (precision < 3 && std::abs(scaled_step - std::round(scaled_step)) > 0.000001) {
				scaled_step *= 10.0;
				++precision;
			}
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
			QStringList warning_labels_off_line;
			for (const auto &line : warning_lines) {
				const qreal normalized = (line.value - minValue) / (maxValue - minValue);
				const qreal y = plot.bottom() - plot.height() * normalized;
				painter.setPen(QPen(line.color, 1.0, Qt::DashLine));
				painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
				const int label_height = 17;
				const int label_width = painter.fontMetrics().horizontalAdvance(line.label) + 16;
				const qreal above = y - label_height - 4;
				const qreal below = y + 4;
				const qreal label_y =
					above >= plot.top() ? above : std::min(below, plot.bottom() - label_height);
				warning_labels_off_line.push_back(label_y + label_height < y || label_y > y
									  ? QStringLiteral("true")
									  : QStringLiteral("false"));
				QRectF label_rect(plot.right() - label_width - 8, label_y, label_width, 17);
				painter.fillRect(label_rect, palette().color(QPalette::Base));
				painter.setPen(line.color);
				painter.drawText(label_rect, Qt::AlignRight | Qt::AlignVCenter, line.label);
			}
			setProperty("preflightWarningLabelsOffLine", warning_labels_off_line);

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

class MetricSparkline final : public QWidget {
	std::vector<double> points;

public:
	explicit MetricSparkline(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumSize(96, 22);
		setMaximumHeight(26);
		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	}

	void SetPoints(std::vector<double> values)
	{
		points = std::move(values);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		if (points.size() < 2) {
			return;
		}
		const auto [minimum, maximum] = std::minmax_element(points.begin(), points.end());
		double low = *minimum;
		double high = *maximum;
		if (high - low < 0.000001) {
			const double padding = std::max(1.0, std::abs(high) * 0.05);
			low -= padding;
			high += padding;
		}
		const QRectF plot = QRectF(rect()).adjusted(4, 5, -6, -5);
		QPolygonF line;
		line.reserve(static_cast<qsizetype>(points.size()));
		for (size_t index = 0; index < points.size(); ++index) {
			const qreal x = plot.left() + plot.width() * static_cast<qreal>(index) /
							      static_cast<qreal>(points.size() - 1);
			const qreal y = plot.bottom() - plot.height() * (points[index] - low) / (high - low);
			line.push_back(QPointF(x, y));
		}
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		/* Text colour stays readable on both the row and its selection highlight. */
		QColor stroke = palette().color(QPalette::Text);
		stroke.setAlphaF(0.65);
		painter.setPen(QPen(stroke, 1.25));
		painter.drawPolyline(line);
		painter.setPen(Qt::NoPen);
		painter.setBrush(palette().color(QPalette::Text));
		painter.drawEllipse(line.back(), 2.5, 2.5);
	}
};

class LoupeView final : public QWidget {
	std::shared_ptr<const preflight::loupe::FramePair> stream_pair;
	std::shared_ptr<const preflight::loupe::FramePair> recording_pair;
	std::optional<preflight::loupe::CropRegion> selection;
	std::optional<preflight::loupe::CropRegion> dragging_region;
	QRectF preview_image_rect;
	QRectF preview_panel;
	QRectF source_zoom_panel;
	QRectF encoded_zoom_panel;
	QRectF recording_zoom_panel;
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
		if (!stream_pair || !stream_pair->source || !preview_image_rect.contains(point) ||
		    preview_image_rect.width() <= 0 || preview_image_rect.height() <= 0) {
			return std::nullopt;
		}
		const auto &frame = *stream_pair->source;
		const uint32_t logical_width = source_width(frame);
		const uint32_t logical_height = source_height(frame);
		const qreal x = (point.x() - preview_image_rect.left()) / preview_image_rect.width();
		const qreal y = (point.y() - preview_image_rect.top()) / preview_image_rect.height();
		return QPoint(std::clamp(static_cast<int>(x * logical_width), 0, static_cast<int>(logical_width) - 1),
			      std::clamp(static_cast<int>(y * logical_height), 0,
					 static_cast<int>(logical_height) - 1));
	}

	std::optional<preflight::loupe::CropRegion> bounded_drag_region(const QPoint &end) const
	{
		if (!stream_pair || !stream_pair->source) {
			return std::nullopt;
		}
		const auto start = source_point(drag_start);
		const auto finish = source_point(end);
		if (!start || !finish) {
			return std::nullopt;
		}
		const auto &frame = *stream_pair->source;
		const uint32_t logical_width = source_width(frame);
		const uint32_t logical_height = source_height(frame);
		const qreal zoom_width =
			std::min({source_zoom_panel.width(), encoded_zoom_panel.width(), recording_zoom_panel.width()});
		const qreal zoom_height = std::min(
			{source_zoom_panel.height(), encoded_zoom_panel.height(), recording_zoom_panel.height()});
		const int max_width = std::max(1, static_cast<int>((zoom_width - 8) / 4.0));
		const int max_height = std::max(1, static_cast<int>((zoom_height - 28) / 4.0));
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
		const qreal preview_width = std::max<qreal>(260, bounds.width() * 0.46);
		const qreal zoom_width = (bounds.width() - preview_width - gap * 3) / 3;
		preview_panel = QRectF(bounds.left(), bounds.top(), preview_width, bounds.height());
		source_zoom_panel = QRectF(preview_panel.right() + gap, bounds.top(), zoom_width, bounds.height());
		encoded_zoom_panel = QRectF(source_zoom_panel.right() + gap, bounds.top(), zoom_width, bounds.height());
		recording_zoom_panel =
			QRectF(encoded_zoom_panel.right() + gap, bounds.top(), zoom_width, bounds.height());
		for (const QRectF &panel :
		     {preview_panel, source_zoom_panel, encoded_zoom_panel, recording_zoom_panel}) {
			painter.setPen(palette().color(QPalette::Mid));
			painter.drawRect(panel);
		}
		painter.setPen(palette().color(QPalette::Text));
		painter.drawText(preview_panel.adjusted(4, 2, -4, -4), Qt::AlignTop | Qt::AlignHCenter,
				 tr("Stream · drag to select"));
		if (!stream_pair || !stream_pair->source || !stream_pair->encoded) {
			painter.drawText(bounds, Qt::AlignCenter, tr("Waiting for matching source and encoded frames"));
			return;
		}

		const auto &source = *stream_pair->source;
		const auto &encoded = *stream_pair->encoded;
		const QRectF preview_bounds = preview_panel.adjusted(5, 24, -5, -5);
		preview_image_rect = fitted_image_rect(preview_bounds, encoded);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
		painter.drawImage(preview_image_rect, image_for(encoded));

		if (!selection) {
			const uint32_t logical_width = source_width(source);
			const uint32_t logical_height = source_height(source);
			const uint32_t visible_crop_width = static_cast<uint32_t>(
				std::max<qreal>(1, (std::min({source_zoom_panel.width(), encoded_zoom_panel.width(),
							      recording_zoom_panel.width()}) -
						    8) / 4.0));
			const uint32_t visible_crop_height = static_cast<uint32_t>(
				std::max<qreal>(1, (std::min({source_zoom_panel.height(), encoded_zoom_panel.height(),
							      recording_zoom_panel.height()}) -
						    28) / 4.0));
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
		const auto stream_crop = preflight::loupe::project_crop(*active_region, encoded.width, encoded.height);
		if (stream_crop) {
			const QRectF selection_rect(
				preview_image_rect.left() +
					preview_image_rect.width() * stream_crop->left / encoded.width,
				preview_image_rect.top() +
					preview_image_rect.height() * stream_crop->top / encoded.height,
				preview_image_rect.width() * stream_crop->width / encoded.width,
				preview_image_rect.height() * stream_crop->height / encoded.height);
			painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
			painter.drawRect(selection_rect);
		}
		draw_zoom(painter, source_zoom_panel, tr("Source · 400%"), source, *active_region);
		draw_zoom(painter, encoded_zoom_panel, tr("Stream · 400%"), encoded, *active_region);
		if (recording_pair && recording_pair->encoded) {
			draw_zoom(painter, recording_zoom_panel, tr("Recording · 400%"), *recording_pair->encoded,
				  *active_region);
		} else {
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(recording_zoom_panel, Qt::AlignCenter,
					 tr("Recording frame not available yet"));
		}
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		const auto point = source_point(event->position());
		if (event->button() == Qt::LeftButton && point) {
			dragging = true;
			drag_start = event->position().toPoint();
			moving_selection = false;
			moving_crop.reset();
			if (selection && stream_pair && stream_pair->source) {
				const auto projected =
					preflight::loupe::project_crop(*selection, source_width(*stream_pair->source),
								       source_height(*stream_pair->source));
				if (projected && point->x() >= static_cast<int>(projected->left) &&
				    point->x() < static_cast<int>(projected->left + projected->width) &&
				    point->y() >= static_cast<int>(projected->top) &&
				    point->y() < static_cast<int>(projected->top + projected->height)) {
					moving_selection = true;
					moving_crop = projected;
					moving_offset = QPoint(point->x() - static_cast<int>(projected->left),
							       point->y() - static_cast<int>(projected->top));
					dragging_region =
						preflight::loupe::CropRegion{source_width(*stream_pair->source),
									     source_height(*stream_pair->source),
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
			if (moving_selection && moving_crop && stream_pair && stream_pair->source) {
				const auto point = source_point(event->position());
				if (point) {
					const int max_left = static_cast<int>(source_width(*stream_pair->source) -
									      moving_crop->width);
					const int max_top = static_cast<int>(source_height(*stream_pair->source) -
									     moving_crop->height);
					const uint32_t left = static_cast<uint32_t>(
						std::clamp(point->x() - moving_offset.x(), 0, max_left));
					const uint32_t top = static_cast<uint32_t>(
						std::clamp(point->y() - moving_offset.y(), 0, max_top));
					dragging_region =
						preflight::loupe::CropRegion{source_width(*stream_pair->source),
									     source_height(*stream_pair->source),
									     left,
									     top,
									     moving_crop->width,
									     moving_crop->height};
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

	void SetPairs(std::shared_ptr<const preflight::loupe::FramePair> new_stream_pair,
		      std::shared_ptr<const preflight::loupe::FramePair> new_recording_pair)
	{
		if (stream_pair == new_stream_pair && recording_pair == new_recording_pair) {
			return;
		}
		stream_pair = std::move(new_stream_pair);
		recording_pair = std::move(new_recording_pair);
		if (!stream_pair) {
			selection.reset();
		} else if (selection && stream_pair->source &&
			   (selection->source_width != source_width(*stream_pair->source) ||
			    selection->source_height != source_height(*stream_pair->source))) {
			const auto remapped = preflight::loupe::project_crop(
				*selection, source_width(*stream_pair->source), source_height(*stream_pair->source));
			if (remapped) {
				selection = preflight::loupe::CropRegion{source_width(*stream_pair->source),
									 source_height(*stream_pair->source),
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
	QString title;
	std::vector<QString> names;
	std::vector<ChartWarningLine> warning_lines;
};

struct MetricRowWidgets {
	QLabel *value = nullptr;
	MetricSparkline *sparkline = nullptr;
	QLabel *status = nullptr;
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

QJsonObject video_config_snapshot(config_t *config)
{
	QJsonObject values;
	values.insert(QStringLiteral("OutputCX"), static_cast<double>(config_get_uint(config, "Video", "OutputCX")));
	values.insert(QStringLiteral("OutputCY"), static_cast<double>(config_get_uint(config, "Video", "OutputCY")));
	values.insert(QStringLiteral("FPSType"), static_cast<double>(config_get_uint(config, "Video", "FPSType")));
	const char *fps_common = config_get_string(config, "Video", "FPSCommon");
	values.insert(QStringLiteral("FPSCommon"), QString::fromUtf8(fps_common ? fps_common : ""));
	values.insert(QStringLiteral("FPSInt"), static_cast<double>(config_get_uint(config, "Video", "FPSInt")));
	values.insert(QStringLiteral("FPSNum"), static_cast<double>(config_get_uint(config, "Video", "FPSNum")));
	values.insert(QStringLiteral("FPSDen"), static_cast<double>(config_get_uint(config, "Video", "FPSDen")));
	return values;
}

QJsonObject simple_recording_config_snapshot(config_t *config)
{
	QJsonObject values;
	for (const char *key : {"RecQuality", "RecEncoder", "RecFormat2"}) {
		const char *value = config_get_string(config, "SimpleOutput", key);
		values.insert(QString::fromUtf8(key), QString::fromUtf8(value ? value : ""));
	}
	return values;
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

struct PreflightBenchWindow::Impl : preflight::bench::DataApi {
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
	QTableWidget *metric_table = nullptr;
	QTableWidget *impact_table = nullptr;
	QGroupBox *chart_detail_group = nullptr;
	MetricChart *chart_detail = nullptr;
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
	OBSPropertiesView *recording_encoder_properties = nullptr;
	QComboBox *resolution_combo = nullptr;
	QComboBox *fps_combo = nullptr;
	QComboBox *simple_recording_quality_combo = nullptr;
	QVBoxLayout *stream_settings_layout = nullptr;
	QCheckBox *captions_toggle = nullptr;
	QVBoxLayout *toggle_layout = nullptr;
	std::map<std::string, obs_source_t *> toggle_filters;
	std::map<std::string, QCheckBox *> toggle_widgets;
	obs_data_t *initial_encoder_settings = nullptr;
	obs_data_t *initial_recording_encoder_settings = nullptr;
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
	PreflightLoupeCapture recording_loupe_capture;
	preflight::CloudflareSpeedTest cloudflare_speed_test;
	obs_preflight_engine_t *engine = nullptr;
	obs_preflight_engine_t *bakeoff_engine = nullptr;
	obs_preflight_bakeoff_t *bakeoff_planner = nullptr;
	std::vector<BenchPoint> points;
	std::vector<preflight::bench::MetricWindow> metric_history;
	preflight::bench::ImpactTracker impact_tracker;
	uint64_t impact_window_sequence = 0;
	QJsonObject last_stream_editor_settings;
	QJsonObject last_recording_editor_settings;
	QJsonObject simple_quality_expected_settings;
	std::vector<BakeoffCandidate> bakeoff_candidates;
	obs_preflight_candidate_plan active_candidate{};
	std::array<ChartPane, 8> charts;
	std::array<MetricRowWidgets, 8> metric_rows;
	int selected_metric = -1;
	QElapsedTimer bakeoff_elapsed;
	QString supported_test_mode;
	QString recording_path;
	QString setup_error;
	QString youtube_test_error;
	QString stop_completion_status;
	QString bakeoff_candidate_reason;
	QString initial_output_mode;
	QString simple_output_encoder;
	QString simple_recording_encoder;
	QString initial_simple_recording_quality;
	QString current_simple_recording_quality;
	QString stream_video_encoder_id;
	QString recording_video_encoder_id;
	QString apply_feedback;
	std::optional<preflight::apply::Plan> apply_plan;
	std::optional<preflight::apply::Plan> recorded_apply_preview;
	preflight::bench::VideoSettings video_settings;
	preflight::bench::VideoSettings initial_video_settings;
	std::vector<preflight::bench::FeatureToggle> feature_toggles;
	bool enable_recorded_apply_controls_for_test = false;
	preflight::apply::TargetIdentity initial_apply_target;
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
	bool restart_recording_when_stopped = false;
	bool stream_settings_pending = false;
	bool recording_settings_pending = false;
	bool video_settings_pending = false;
	bool bench_running = false;
	bool stopping = false;
	bool bakeoff_running = false;
	bool bakeoff_waiting_for_stop = false;
	bool bakeoff_cancelled = false;
	bool bakeoff_candidate_active = false;
	bool bakeoff_candidate_has_samples = false;
	bool cloudflare_speed_only = false;
	bool requires_youtube_throwaway_stream = false;
	bool apply_preview_ready = false;
	bool close_when_stopped = false;
	bool simple_output_advanced = false;

	explicit Impl(PreflightBenchWindow *dialog_) : dialog(dialog_) {}

	preflight::bench::Snapshot GetSnapshot() const override
	{
		preflight::bench::Snapshot snapshot;
		snapshot.stream_frame = loupe_capture.LatestPair();
		snapshot.recording_frame = recording_loupe_capture.LatestPair();
		if (!metric_history.empty()) {
			snapshot.latest_metrics = metric_history.back();
			snapshot.metric_history = metric_history;
		}
		snapshot.stream_encoder.encoder_id = stream_video_encoder_id.toStdString();
		snapshot.stream_encoder.values = preflight::bench::SettingsObject::Retain(
			encoder_properties ? encoder_properties->GetSettings() : nullptr);
		snapshot.recording_encoder.encoder_id = recording_video_encoder_id.toStdString();
		snapshot.recording_encoder.values = preflight::bench::SettingsObject::Retain(
			recording_encoder_properties ? recording_encoder_properties->GetSettings() : nullptr);
		snapshot.video = video_settings;
		snapshot.impacts = impact_tracker.Entries();
		snapshot.toggles = feature_toggles;
		return snapshot;
	}

	bool SetEncoderSettings(preflight::bench::EncoderRole role, const preflight::bench::SettingsObject &settings,
				const std::vector<preflight::bench::SettingChange> &changes) override
	{
		OBSPropertiesView *view = role == preflight::bench::EncoderRole::Stream ? encoder_properties
											: recording_encoder_properties;
		if (!view || !settings) {
			return false;
		}
		impact_tracker.RecordChanges(changes);
		obs_data_clear(view->GetSettings());
		obs_data_apply(view->GetSettings(), settings.get());
		view->ReloadProperties();
		if (role == preflight::bench::EncoderRole::Stream) {
			stream_settings_pending = true;
			last_stream_editor_settings = json_object_from_settings(view->GetSettings());
		} else {
			recording_settings_pending = true;
			last_recording_editor_settings = json_object_from_settings(view->GetSettings());
		}
		RenderImpactEntries(impact_tracker.Entries());
		InvalidateApplyPreview();
		if (settings_timer) {
			settings_timer->start(0);
		}
		return true;
	}

	bool SetVideoSettings(const preflight::bench::VideoSettings &settings,
			      const std::vector<preflight::bench::SettingChange> &changes) override
	{
		if (settings.base_width != video_settings.base_width ||
		    settings.base_height != video_settings.base_height || settings.output_width == 0 ||
		    settings.output_height == 0 || settings.fps_num == 0 || settings.fps_den == 0 ||
		    settings.fps_divisor == 0) {
			return false;
		}
		const int fps_index = fps_combo ? fps_combo->findData(static_cast<uint>(settings.fps_divisor)) : 0;
		if (fps_index < 0) {
			return false;
		}
		impact_tracker.RecordChanges(changes);
		video_settings = settings;
		video_settings_pending = true;
		if (resolution_combo) {
			const QSignalBlocker blocker(resolution_combo);
			resolution_combo->setCurrentText(
				QStringLiteral("%1 × %2").arg(settings.output_width).arg(settings.output_height));
		}
		if (fps_combo) {
			const QSignalBlocker blocker(fps_combo);
			fps_combo->setCurrentIndex(fps_index);
		}
		RenderImpactEntries(impact_tracker.Entries());
		InvalidateApplyPreview();
		if (settings_timer) {
			settings_timer->start(0);
		}
		return true;
	}

	bool SetFeatureToggle(obs_source_t *filter, const std::string &name, const std::string &source_filter_id,
			      bool enabled) override
	{
		if (!filter || obs_source_get_type(filter) != OBS_SOURCE_TYPE_FILTER) {
			return false;
		}
		const char *actual_id = obs_source_get_id(filter);
		if (!actual_id || source_filter_id != actual_id) {
			return false;
		}
		const bool old_enabled = obs_source_enabled(filter);
		if (old_enabled != enabled) {
			impact_tracker.RecordChanges(
				{{name, source_filter_id, old_enabled ? "on" : "off", enabled ? "on" : "off"}});
		}
		obs_source_set_enabled(filter, enabled);
		const auto existing = std::find_if(feature_toggles.begin(), feature_toggles.end(),
						   [&source_filter_id](const auto &item) {
							   return item.source_filter_id == source_filter_id;
						   });
		preflight::bench::FeatureToggle toggle{name, source_filter_id, enabled, true,
						       "Takes effect immediately; excluded from Apply."};
		if (existing == feature_toggles.end()) {
			feature_toggles.push_back(std::move(toggle));
		} else {
			*existing = std::move(toggle);
		}
		const auto widget = toggle_widgets.find(source_filter_id);
		if (widget != toggle_widgets.end()) {
			const QSignalBlocker blocker(widget->second);
			widget->second->setChecked(enabled);
		}
		RenderImpactEntries(impact_tracker.Entries());
		return true;
	}

	bool RegisterFeatureToggle(obs_source_t *filter, const std::string &name,
				   const std::string &source_filter_id) override
	{
		if (!filter || obs_source_get_type(filter) != OBS_SOURCE_TYPE_FILTER) {
			return false;
		}
		const char *actual_id = obs_source_get_id(filter);
		if (!actual_id || source_filter_id != actual_id) {
			return false;
		}
		obs_source_t *retained = obs_source_get_ref(filter);
		if (!retained) {
			return false;
		}
		auto bound = toggle_filters.find(source_filter_id);
		if (bound != toggle_filters.end()) {
			obs_source_release(bound->second);
		}
		toggle_filters[source_filter_id] = retained;
		const bool enabled = obs_source_enabled(filter);
		const auto existing = std::find_if(feature_toggles.begin(), feature_toggles.end(),
						   [&source_filter_id](const auto &item) {
							   return item.source_filter_id == source_filter_id;
						   });
		preflight::bench::FeatureToggle toggle{name, source_filter_id, enabled, true,
						       "Takes effect immediately; excluded from Apply."};
		if (existing == feature_toggles.end()) {
			feature_toggles.push_back(std::move(toggle));
		} else {
			*existing = std::move(toggle);
		}
		QCheckBox *toggle_widget = nullptr;
		auto widget = toggle_widgets.find(source_filter_id);
		if (widget != toggle_widgets.end()) {
			toggle_widget = widget->second;
		} else if (name == "Live captions (on-device)" && captions_toggle) {
			toggle_widget = captions_toggle;
			toggle_widget->setText(QString::fromStdString(name));
			toggle_widgets[source_filter_id] = toggle_widget;
			QObject::connect(
				toggle_widget, &QCheckBox::toggled, dialog, [this, source_filter_id, name](bool on) {
					auto live_filter = toggle_filters.find(source_filter_id);
					if (live_filter == toggle_filters.end() ||
					    !SetFeatureToggle(live_filter->second, name, source_filter_id, on)) {
						SetStatus(QObject::tr("This live filter is no longer available."));
					}
				});
		} else if (toggle_layout) {
			toggle_widget = new QCheckBox(QString::fromStdString(name), toggle_layout->parentWidget());
			toggle_widget->setObjectName(QStringLiteral("preflight-toggle-%1").arg(toggle_widgets.size()));
			toggle_layout->insertWidget(std::max(0, toggle_layout->count() - 1), toggle_widget);
			toggle_widgets[source_filter_id] = toggle_widget;
			QObject::connect(
				toggle_widget, &QCheckBox::toggled, dialog, [this, source_filter_id, name](bool on) {
					auto live_filter = toggle_filters.find(source_filter_id);
					if (live_filter == toggle_filters.end() ||
					    !SetFeatureToggle(live_filter->second, name, source_filter_id, on)) {
						SetStatus(QObject::tr("This live filter is no longer available."));
					}
				});
		}
		if (toggle_widget) {
			toggle_widgets[source_filter_id] = toggle_widget;
			const QSignalBlocker blocker(toggle_widget);
			toggle_widget->setChecked(enabled);
			toggle_widget->setEnabled(true);
			toggle_widget->setToolTip(QObject::tr("This filter toggle takes effect immediately."));
		}
		return true;
	}

	bool ReplaceStreamEncoderEditor(const std::string &encoder_id, const preflight::bench::SettingsObject &settings)
	{
		if (!stream_settings_layout || !settings || encoder_id.empty()) {
			return false;
		}
		const QByteArray id = QByteArray::fromStdString(encoder_id);
		OBSDataAutoRelease editor_settings = clone_settings(settings.get());
		if (!editor_settings) {
			return false;
		}
		auto *replacement = new OBSPropertiesView(editor_settings.Get(), id.constData(),
							  (PropertiesReloadCallback)obs_get_encoder_properties, 190);
		replacement->setFrameShape(QFrame::NoFrame);
		replacement->setScrolling(true);
		QObject::connect(replacement, &OBSPropertiesView::Changed, dialog, [this]() {
			RecordEditorChanges(preflight::bench::EncoderRole::Stream);
			stream_settings_pending = true;
			InvalidateApplyPreview();
			settings_timer->start(600);
		});
		if (encoder_properties) {
			stream_settings_layout->removeWidget(encoder_properties);
			delete encoder_properties;
		}
		encoder_properties = replacement;
		stream_settings_layout->insertWidget(0, encoder_properties);
		stream_video_encoder_id = QString::fromUtf8(id);
		last_stream_editor_settings = json_object_from_settings(encoder_properties->GetSettings());
		return true;
	}

	bool LoadCandidate(const preflight::bench::CandidateSettings &candidate) override
	{
		if (candidate.video.base_width != video_settings.base_width ||
		    candidate.video.base_height != video_settings.base_height || bench_running || bakeoff_running ||
		    stopping || !candidate.stream_encoder.values) {
			return false;
		}
		const std::string previous_encoder = stream_video_encoder_id.toStdString();
		const QJsonObject previous_settings =
			encoder_properties ? json_object_from_settings(encoder_properties->GetSettings())
					   : QJsonObject{};
		if (candidate.stream_encoder.encoder_id != previous_encoder &&
		    !ReplaceStreamEncoderEditor(candidate.stream_encoder.encoder_id, candidate.stream_encoder.values)) {
			return false;
		}
		const QJsonObject next_settings = json_object_from_settings(candidate.stream_encoder.values.get());
		std::vector<preflight::bench::SettingChange> encoder_changes;
		if (previous_encoder != candidate.stream_encoder.encoder_id) {
			encoder_changes.push_back({"Stream encoder", "encoder_id", previous_encoder,
						   candidate.stream_encoder.encoder_id});
		}
		for (auto it = next_settings.constBegin(); it != next_settings.constEnd(); ++it) {
			const QJsonValue previous = previous_settings.value(it.key());
			if (previous != it.value()) {
				encoder_changes.push_back(
					{"Stream encoder", it.key().toStdString(),
					 preflight::apply::ValueForDisplay(previous).toStdString(),
					 preflight::apply::ValueForDisplay(it.value()).toStdString()});
			}
		}
		if (!SetEncoderSettings(preflight::bench::EncoderRole::Stream, candidate.stream_encoder.values,
					encoder_changes)) {
			return false;
		}
		std::vector<preflight::bench::SettingChange> video_changes;
		if (candidate.video.output_width != video_settings.output_width ||
		    candidate.video.output_height != video_settings.output_height) {
			video_changes.push_back({"Video", "output resolution",
						 std::to_string(video_settings.output_width) + "x" +
							 std::to_string(video_settings.output_height),
						 std::to_string(candidate.video.output_width) + "x" +
							 std::to_string(candidate.video.output_height)});
		}
		if (static_cast<uint64_t>(candidate.video.fps_num) * video_settings.fps_den !=
		    static_cast<uint64_t>(video_settings.fps_num) * candidate.video.fps_den) {
			video_changes.push_back(
				{"Video", "FPS",
				 std::to_string(video_settings.fps_num) + "/" + std::to_string(video_settings.fps_den),
				 std::to_string(candidate.video.fps_num) + "/" +
					 std::to_string(candidate.video.fps_den)});
		}
		return SetVideoSettings(candidate.video, video_changes);
	}

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
		for (const auto &[id, filter] : toggle_filters) {
			(void)id;
			obs_source_release(filter);
		}
		if (initial_encoder_settings) {
			obs_data_release(initial_encoder_settings);
		}
		if (initial_recording_encoder_settings) {
			obs_data_release(initial_recording_encoder_settings);
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
			loupe_view->SetPairs(loupe_capture.LatestPair(), recording_loupe_capture.LatestPair());
			if (const auto crop = loupe_view->SelectedCrop()) {
				loupe_capture.SetSourceCrop(*crop);
				recording_loupe_capture.SetSourceCrop(*crop);
			}
		}
		if (loupe_status_label) {
			const QString stream_status = QString::fromStdString(loupe_capture.Status());
			const QString recording_status = QString::fromStdString(recording_loupe_capture.Status());
			loupe_status_label->setText(recording_status.isEmpty()
							    ? stream_status
							    : stream_status + QStringLiteral(" · ") + recording_status);
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

	void StartRecordingLoupe()
	{
		if (!recording_output || !recording_video_encoder) {
			return;
		}
		if (recording_loupe_capture.Start(recording_output, recording_video_encoder)) {
			loupe_timer->start(100);
		}
		UpdateLoupe();
	}

	void StopLoupe()
	{
		if (loupe_timer) {
			loupe_timer->stop();
		}
		loupe_capture.Stop();
		recording_loupe_capture.Stop();
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

	bool ApplySimpleRecordingQuality(const QString &quality)
	{
		if (!recording_encoder_properties || quality == QStringLiteral("Lossless")) {
			return false;
		}
		OBSDataAutoRelease settings(obs_data_create());
		if (!settings) {
			return false;
		}
		if (quality == QStringLiteral("Stream")) {
			if (stream_video_encoder_id != recording_video_encoder_id || !encoder_properties) {
				return false;
			}
			obs_data_apply(settings, encoder_properties->GetSettings());
		} else {
			const QString encoder_id = recording_video_encoder_id.toLower();
			const bool low_cpu_x264 =
				simple_recording_encoder.contains(QStringLiteral("lowcpu"), Qt::CaseInsensitive);
			const bool x264 = encoder_id.contains(QStringLiteral("x264"));
			const bool apple = encoder_id.contains(QStringLiteral("videotoolbox")) ||
					   encoder_id.contains(QStringLiteral("apple"));
			const bool qsv = encoder_id.contains(QStringLiteral("qsv"));
			const bool hardware_cqp = qsv || encoder_id.contains(QStringLiteral("nvenc")) ||
						  encoder_id.contains(QStringLiteral("amf")) ||
						  encoder_id.contains(QStringLiteral("amd"));
			if (!x264 && !apple && !hardware_cqp) {
				return false;
			}
			const double diagonal = std::hypot(static_cast<double>(video_settings.output_width),
							   static_cast<double>(video_settings.output_height));
			const int crf_reduction = static_cast<int>((1.0 - std::min(2000.0, diagonal) / 2000.0) * 10.0);
			const int quality_value =
				(quality == QStringLiteral("HQ") ? 16 : 23) - crf_reduction - (low_cpu_x264 ? 2 : 0);
			if (x264) {
				obs_data_set_string(settings, "rate_control", "CRF");
				obs_data_set_int(settings, "crf", quality_value);
				obs_data_set_bool(settings, "use_bufsize", true);
				obs_data_set_string(settings, "profile", "high");
				obs_data_set_string(settings, "preset", low_cpu_x264 ? "ultrafast" : "veryfast");
			} else if (apple) {
				obs_data_set_string(settings, "rate_control", "CRF");
				obs_data_set_string(settings, "profile",
						    encoder_id.contains(QStringLiteral("hevc")) ? "main" : "high");
				obs_data_set_int(settings, "quality", quality == QStringLiteral("HQ") ? 70 : 50);
			} else {
				bool supports_icq = false;
				if (qsv && !encoder_id.contains(QStringLiteral("av1"))) {
					OBSProperties properties = obs_encoder_properties(recording_video_encoder);
					obs_property_t *rate_control = obs_properties_get(properties, "rate_control");
					for (size_t i = 0;
					     rate_control && i < obs_property_list_item_count(rate_control); ++i) {
						const char *value = obs_property_list_item_string(rate_control, i);
						if (value && strcmp(value, "ICQ") == 0) {
							supports_icq = true;
							break;
						}
					}
				}
				const bool main_profile = encoder_id.contains(QStringLiteral("nvenc")) &&
							  (encoder_id.contains(QStringLiteral("hevc")) ||
							   encoder_id.contains(QStringLiteral("av1")));
				obs_data_set_string(settings, "rate_control", supports_icq ? "ICQ" : "CQP");
				obs_data_set_string(settings, "profile", main_profile ? "main" : "high");
				obs_data_set_int(settings, supports_icq ? "icq_quality" : "cqp", quality_value);
				if (encoder_id.contains(QStringLiteral("amf")) ||
				    encoder_id.contains(QStringLiteral("amd"))) {
					obs_data_set_string(settings, "preset", "quality");
				}
			}
		}
		obs_data_clear(recording_encoder_properties->GetSettings());
		obs_data_apply(recording_encoder_properties->GetSettings(), settings);
		recording_encoder_properties->ReloadProperties();
		last_recording_editor_settings = json_object_from_settings(recording_encoder_properties->GetSettings());
		simple_quality_expected_settings = last_recording_editor_settings;
		recording_settings_pending = true;
		if (settings_timer) {
			settings_timer->start(0);
		}
		return true;
	}

	preflight::apply::TargetIdentity MakeApplyTarget(OBSBasic *main, const QString &mode,
							 const char *actual_encoder_id) const
	{
		preflight::apply::TargetIdentity target;
		if (!main) {
			return target;
		}
		try {
			const auto &profile = main->GetCurrentProfile();
			target.profile_name =
				QString::fromUtf8(profile.name.data(), static_cast<qsizetype>(profile.name.size()));
			const std::string path_utf8 = profile.path.u8string();
			target.profile_path =
				QString::fromUtf8(path_utf8.data(), static_cast<qsizetype>(path_utf8.size()));
			target.encoder_id = QString::fromUtf8(actual_encoder_id ? actual_encoder_id : "");
			const char *encoder_selection = nullptr;
			if (mode == QStringLiteral("Simple")) {
				encoder_selection = config_get_string(main->Config(), "SimpleOutput", "StreamEncoder");
			} else if (mode == QStringLiteral("Advanced")) {
				encoder_selection = config_get_string(main->Config(), "AdvOut", "Encoder");
			}
			target.encoder_selection = QString::fromUtf8(encoder_selection ? encoder_selection : "");
			OBSOutputAutoRelease configured_recording = obs_frontend_get_recording_output();
			OBSEncoderAutoRelease configured_recording_video =
				configured_recording ? obs_output_get_video_encoder(configured_recording) : nullptr;
			const char *recording_id =
				configured_recording_video ? obs_encoder_get_id(configured_recording_video) : nullptr;
			target.recording_encoder_id = QString::fromUtf8(recording_id ? recording_id : "");
			const char *recording_selection = nullptr;
			if (mode == QStringLiteral("Simple")) {
				recording_selection = config_get_string(main->Config(), "SimpleOutput", "RecEncoder");
			} else if (mode == QStringLiteral("Advanced")) {
				recording_selection = config_get_string(main->Config(), "AdvOut", "RecEncoder");
			}
			target.recording_encoder_selection =
				QString::fromUtf8(recording_selection ? recording_selection : "");
		} catch (const std::exception &) {
			return {};
		}
		return target;
	}

	preflight::apply::TargetIdentity CurrentApplyTarget(OBSBasic *main, const QString &mode) const
	{
		OBSOutputAutoRelease configured_stream = obs_frontend_get_streaming_output();
		OBSEncoderAutoRelease configured_video =
			configured_stream ? obs_output_get_video_encoder(configured_stream) : nullptr;
		const char *encoder_id = configured_video ? obs_encoder_get_id(configured_video) : nullptr;
		return MakeApplyTarget(main, mode, encoder_id);
	}

	preflight::apply::Plan BuildCurrentApplyPlan(OBSBasic *main) const
	{
		preflight::apply::Plan plan;
		if (!main || !encoder_properties || !initial_encoder_settings) {
			plan.error = QObject::tr("OBS settings are not available in this window.");
			return plan;
		}
		const char *mode_value = config_get_string(main->Config(), "Output", "Mode");
		const QString mode = QString::fromUtf8(mode_value ? mode_value : "");
		plan.mode = mode == QStringLiteral("Simple")     ? preflight::apply::OutputMode::Simple
			    : mode == QStringLiteral("Advanced") ? preflight::apply::OutputMode::Advanced
								 : preflight::apply::OutputMode::Unsupported;
		plan.target = CurrentApplyTarget(main, mode);
		if (mode != initial_output_mode) {
			plan.error = QObject::tr(
				"The OBS output mode changed while Preflight was open. Close and reopen Preflight to review the new mode.");
			return plan;
		}
		if (plan.target.profile_name != initial_apply_target.profile_name ||
		    plan.target.profile_path != initial_apply_target.profile_path ||
		    plan.target.encoder_id != initial_apply_target.encoder_id ||
		    plan.target.encoder_selection != initial_apply_target.encoder_selection ||
		    plan.target.recording_encoder_id != initial_apply_target.recording_encoder_id ||
		    plan.target.recording_encoder_selection != initial_apply_target.recording_encoder_selection) {
			plan.error = QObject::tr(
				"The active profile or stream/recording encoder changed while Preflight was open. Close and reopen Preflight to review it.");
			return plan;
		}

		const QJsonObject initial = json_object_from_settings(initial_encoder_settings);
		const QJsonObject edited = json_object_from_settings(encoder_properties->GetSettings());
		if (stream_video_encoder_id != initial_apply_target.encoder_id) {
			plan.error = QObject::tr(
				"Apply is available only when the selected bake-off encoder matches the OBS stream encoder. The current preview remains private to Preflight.");
			return plan;
		}
		QJsonObject saved_video = video_config_snapshot(main->Config());
		QJsonObject edited_video = saved_video;
		if (video_settings.output_width != initial_video_settings.output_width ||
		    video_settings.output_height != initial_video_settings.output_height) {
			edited_video.insert(QStringLiteral("OutputCX"),
					    static_cast<double>(video_settings.output_width));
			edited_video.insert(QStringLiteral("OutputCY"),
					    static_cast<double>(video_settings.output_height));
		}
		const bool fps_changed =
			static_cast<uint64_t>(video_settings.fps_num) * initial_video_settings.fps_den !=
			static_cast<uint64_t>(initial_video_settings.fps_num) * video_settings.fps_den;
		if (fps_changed) {
			if (video_settings.fps_den == 1) {
				edited_video.insert(QStringLiteral("FPSType"), 1);
				edited_video.insert(QStringLiteral("FPSInt"),
						    static_cast<double>(video_settings.fps_num));
			} else {
				edited_video.insert(QStringLiteral("FPSType"), 2);
				edited_video.insert(QStringLiteral("FPSNum"),
						    static_cast<double>(video_settings.fps_num));
				edited_video.insert(QStringLiteral("FPSDen"),
						    static_cast<double>(video_settings.fps_den));
			}
		}
		if (mode == QStringLiteral("Advanced")) {
			QJsonObject saved_stream;
			const QString saved_stream_path =
				QDir(plan.target.profile_path).filePath(QStringLiteral("streamEncoder.json"));
			if (!preflight::apply::ReadAdvancedSettings(saved_stream_path, &saved_stream, &plan.error)) {
				return plan;
			}
			plan = preflight::apply::BuildAdvancedPlan(saved_stream, initial, edited, plan.target);
			QJsonObject saved_recording;
			const QString saved_recording_path =
				QDir(plan.target.profile_path).filePath(QStringLiteral("recordEncoder.json"));
			if (!preflight::apply::ReadAdvancedSettings(saved_recording_path, &saved_recording,
								    &plan.error)) {
				return plan;
			}
			preflight::apply::AppendAdvancedEncoderChanges(
				plan, QStringLiteral("recordEncoder.json"), saved_recording,
				json_object_from_settings(initial_recording_encoder_settings),
				json_object_from_settings(recording_encoder_properties->GetSettings()));
			preflight::apply::AppendConfigSectionChanges(plan, QStringLiteral("Video"), saved_video,
								     edited_video);
			return plan;
		}
		if (mode == QStringLiteral("Simple")) {
			plan = preflight::apply::BuildSimplePlan(SimpleConfigSnapshot(main), initial, edited,
								 simple_output_encoder, simple_output_advanced,
								 plan.target);
			const QJsonObject initial_recording =
				json_object_from_settings(initial_recording_encoder_settings);
			const QJsonObject edited_recording =
				json_object_from_settings(recording_encoder_properties->GetSettings());
			if (initial_recording != edited_recording &&
			    edited_recording != simple_quality_expected_settings) {
				plan.error = QObject::tr(
					"This recording encoder edit has no matching SimpleOutput key. Change the Simple recording quality instead.");
				return plan;
			}
			QJsonObject saved_recording = simple_recording_config_snapshot(main->Config());
			QJsonObject edited_recording_config = saved_recording;
			if (simple_recording_quality_combo && simple_recording_quality_combo->currentIndex() >= 0) {
				edited_recording_config.insert(
					QStringLiteral("RecQuality"),
					simple_recording_quality_combo->currentData().toString());
			}
			preflight::apply::AppendConfigSectionChanges(plan, QStringLiteral("SimpleOutput"),
								     saved_recording, edited_recording_config);
			preflight::apply::AppendConfigSectionChanges(plan, QStringLiteral("Video"), saved_video,
								     edited_video);
			return plan;
		}
		plan.error = QObject::tr("This OBS output mode does not have a supported Apply mapping.");
		return plan;
	}

	void InvalidateApplyPreview()
	{
		apply_preview_ready = false;
		apply_feedback.clear();
		if (apply_button) {
			apply_button->setEnabled(false);
		}
		if (apply_note) {
			apply_note->setText(
				QObject::tr("Setting changed; waiting to refresh the visible Apply preview."));
		}
		if (settings_timer) {
			settings_timer->start(600);
		}
	}

	void RefreshApplyPreview()
	{
		if (!apply_table || !apply_button) {
			return;
		}
		apply_preview_ready = false;
		apply_button->setEnabled(false);
		if (recorded_apply_preview) {
			apply_plan = *recorded_apply_preview;
			apply_mode_label->setText(QObject::tr("%1 (recorded preview)")
							  .arg(apply_plan->mode == preflight::apply::OutputMode::Simple
								       ? QObject::tr("Simple")
								       : QObject::tr("Advanced")));
		} else {
			OBSBasic *main = OBSBasic::Get();
			const char *mode_value = main ? config_get_string(main->Config(), "Output", "Mode") : nullptr;
			const QString mode = QString::fromUtf8(mode_value ? mode_value : "");
			apply_mode_label->setText(
				QObject::tr("%1 (stays unchanged)").arg(mode.isEmpty() ? QObject::tr("unknown") : mode));
			apply_plan = BuildCurrentApplyPlan(main);
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
		ResizeApplyTableToContents();
		apply_preview_ready = plan.applicable() &&
				      (!recorded_apply_preview || enable_recorded_apply_controls_for_test);
		apply_button->setEnabled(apply_preview_ready);
		if (!apply_feedback.isEmpty()) {
			apply_note->setText(apply_feedback);
		} else if (!plan.error.isEmpty()) {
			apply_note->setText(plan.error);
		} else if (plan.changes.empty()) {
			apply_note->setText(QObject::tr(
				"Edit a setting to preview exact config keys. Nothing is written until Apply."));
		} else {
			apply_note->setText(QObject::tr(
				"Review the exact key changes above. Nothing is written until Apply is pressed."));
		}
	}

	void ApplyConfigChanges()
	{
		if (!apply_table || !apply_button || !apply_preview_ready || !apply_plan || !apply_plan->applicable()) {
			return;
		}
		OBSBasic *main = OBSBasic::Get();
		if (!main) {
			return;
		}
		const preflight::apply::Plan displayed = *apply_plan;
		const preflight::apply::Plan current = BuildCurrentApplyPlan(main);
		if (!preflight::apply::SamePlan(displayed, current)) {
			apply_feedback = QObject::tr(
				"The editor or OBS target changed after this preview was shown. Review the updated diff before applying.");
			apply_preview_ready = false;
			apply_button->setEnabled(false);
			RefreshApplyPreview();
			return;
		}
		QString write_error;
		const bool wrote =
			displayed.mode == preflight::apply::OutputMode::Advanced
				? preflight::apply::WriteAdvancedSettings(main->Config(), displayed, current,
									  &write_error)
			: displayed.mode == preflight::apply::OutputMode::Simple
				? preflight::apply::WriteSimpleConfig(main->Config(), displayed, current, &write_error)
				: false;
		if (!wrote) {
			apply_feedback =
				write_error.isEmpty()
					? QObject::tr(
						  "OBS refused to write settings that no longer match the visible preview.")
					: write_error;
			RefreshApplyPreview();
			return;
		}

		if (displayed.mode != preflight::apply::OutputMode::Simple &&
		    displayed.mode != preflight::apply::OutputMode::Advanced) {
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
					 .arg(displayed.mode == preflight::apply::OutputMode::Simple
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
			const char *recording_encoder = config_get_string(main->Config(), "SimpleOutput", "RecEncoder");
			simple_recording_encoder = QString::fromUtf8(recording_encoder ? recording_encoder : "");
			simple_output_advanced = config_get_bool(main->Config(), "SimpleOutput", "UseAdvanced");
			const char *recording_quality = config_get_string(main->Config(), "SimpleOutput", "RecQuality");
			initial_simple_recording_quality =
				QString::fromUtf8(recording_quality ? recording_quality : "Stream");
			current_simple_recording_quality = initial_simple_recording_quality;
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
		stream_video_encoder_id = QString::fromUtf8(stream_video_id ? stream_video_id : "");
		initial_apply_target = MakeApplyTarget(main, initial_output_mode, stream_video_id);
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
		QObject::connect(encoder_properties, &OBSPropertiesView::Changed, dialog, [this]() {
			RecordEditorChanges(preflight::bench::EncoderRole::Stream);
			stream_settings_pending = true;
			if (initial_output_mode == QStringLiteral("Simple") &&
			    current_simple_recording_quality == QStringLiteral("Stream")) {
				ApplySimpleRecordingQuality(current_simple_recording_quality);
			}
			InvalidateApplyPreview();
			settings_timer->start(600);
		});
		last_stream_editor_settings = json_object_from_settings(encoder_properties->GetSettings());

		recording_video_encoder_id = QString::fromUtf8(recording_video_id ? recording_video_id : "");
		OBSDataAutoRelease recording_property_settings = clone_settings(recording_video_settings);
		OBSDataAutoRelease initial_recording = clone_settings(recording_property_settings);
		if (!recording_property_settings || !initial_recording) {
			setup_error = QObject::tr("Could not create the recording encoder settings view.");
			return false;
		}
		obs_data_addref(initial_recording);
		initial_recording_encoder_settings = initial_recording.Get();
		recording_encoder_properties =
			new OBSPropertiesView(recording_property_settings.Get(), recording_video_id,
					      (PropertiesReloadCallback)obs_get_encoder_properties, 190);
		recording_encoder_properties->setFrameShape(QFrame::NoFrame);
		recording_encoder_properties->setScrolling(true);
		QObject::connect(recording_encoder_properties, &OBSPropertiesView::Changed, dialog, [this]() {
			RecordEditorChanges(preflight::bench::EncoderRole::Recording);
			recording_settings_pending = true;
			InvalidateApplyPreview();
			settings_timer->start(600);
		});
		last_recording_editor_settings = json_object_from_settings(recording_encoder_properties->GetSettings());

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
		struct obs_video_info active_video_info = {};
		if (!obs_get_video_info(&active_video_info) || active_video_info.fps_num == 0 ||
		    active_video_info.fps_den == 0) {
			setup_error = QObject::tr("OBS video settings are not ready for Preflight.");
			return false;
		}
		video_settings.base_width = active_video_info.base_width;
		video_settings.base_height = active_video_info.base_height;
		video_settings.output_width = active_video_info.output_width;
		video_settings.output_height = active_video_info.output_height;
		video_settings.fps_num = active_video_info.fps_num;
		video_settings.fps_den = active_video_info.fps_den;
		video_settings.fps_divisor = 1;
		video_settings.fps_divisor_supported = true;
		initial_video_settings = video_settings;
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
			QString encoder_label = QString::fromStdString(candidate.encoder_id);
			if (encoder_label.startsWith(QStringLiteral("obs_"))) {
				encoder_label.remove(0, 4);
			}
			if (encoder_label.endsWith(QStringLiteral("_tex"))) {
				encoder_label.chop(4);
			}
			encoder_label.replace(QLatin1Char('_'), QLatin1Char(' '));
			const QString candidate_label = QStringLiteral("%1\n%2×%3 · %4fps")
								.arg(encoder_label)
								.arg(candidate.mode.width)
								.arg(candidate.mode.height)
								.arg(fps);
			bakeoff_table->setItem(static_cast<int>(row), 0, new QTableWidgetItem(candidate_label));
			bakeoff_table->setItem(static_cast<int>(row), 1, new QTableWidgetItem(QObject::tr("Pending")));
			auto *use = new QPushButton(QObject::tr("Use these"), bakeoff_table);
			use->setObjectName(QStringLiteral("preflight-bakeoff-use-%1").arg(row));
			QObject::connect(use, &QPushButton::clicked, dialog,
					 [this, row]() { UseBakeoffCandidate(row); });
			bakeoff_table->setCellWidget(static_cast<int>(row), 2, use);
		}
		ResizeBakeoffTableToContents();
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

	void SetBakeoffUseActionsEnabled(bool enabled)
	{
		if (!bakeoff_table) {
			return;
		}
		for (int row = 0; row < bakeoff_table->rowCount(); ++row) {
			if (auto *use = qobject_cast<QPushButton *>(bakeoff_table->cellWidget(row, 2))) {
				use->setEnabled(enabled);
			}
		}
	}

	void UseBakeoffCandidate(size_t row)
	{
		if (row >= bakeoff_candidates.size() || bench_running || bakeoff_running) {
			SetStatus(QObject::tr("Stop the bench before loading bake-off settings."));
			return;
		}
		const BakeoffCandidate &candidate = bakeoff_candidates[row];
		const QByteArray encoder_id = QByteArray::fromStdString(candidate.encoder_id);
		OBSDataAutoRelease settings = obs_encoder_defaults(encoder_id.constData());
		if (!settings) {
			SetBakeoffStatus(QObject::tr("OBS could not load the selected encoder settings."));
			return;
		}
		if (stream_bitrate_kbps > 0) {
			obs_data_set_int(settings, "bitrate", stream_bitrate_kbps);
		}
		if (test_service) {
			obs_service_apply_encoder_settings(test_service, settings, bakeoff_audio_settings);
		}
		preflight::bench::CandidateSettings selected;
		selected.stream_encoder.encoder_id = candidate.encoder_id;
		selected.stream_encoder.values = preflight::bench::SettingsObject::Retain(settings);
		selected.video.base_width = video_settings.base_width;
		selected.video.base_height = video_settings.base_height;
		selected.video.output_width = candidate.mode.width;
		selected.video.output_height = candidate.mode.height;
		selected.video.fps_num = candidate.mode.fps_num;
		selected.video.fps_den = candidate.mode.fps_den;
		selected.video.fps_divisor = current_fps_den && candidate.mode.fps_den % current_fps_den == 0
						     ? candidate.mode.fps_den / current_fps_den
						     : 0;
		selected.video.fps_divisor_supported = selected.video.fps_divisor > 0;
		if (!selected.video.fps_divisor_supported || !LoadCandidate(selected)) {
			SetBakeoffStatus(QObject::tr("These settings cannot be loaded while the bench is active."));
			return;
		}
		SetBakeoffStatus(QObject::tr("Loaded into the Preflight editors. Nothing was written to OBS."));
	}

	void RenderImpactEntries(const std::vector<preflight::bench::ImpactEntry> &entries)
	{
		if (!impact_table) {
			return;
		}
		impact_table->setRowCount(static_cast<int>(entries.size()));
		auto window_text = [this](const std::optional<preflight::bench::MetricWindow> &window) {
			return window ? QStringLiteral("%1 · %2% CPU")
						.arg(BakeoffVerdictText(window->value.verdict))
						.arg(window->value.cpu_percent, 0, 'f', 1)
				      : QStringLiteral("—");
		};
		auto signed_value = [](double value, int precision) {
			const QString magnitude = QString::number(std::abs(value), 'f', precision);
			return (value < 0.0 ? QStringLiteral("−") : QStringLiteral("+")) + magnitude;
		};
		for (size_t row = 0; row < entries.size(); ++row) {
			const auto &entry = entries[row];
			const QString description = QString::fromStdString(entry.description);
			const QString change =
				description + (entry.key.empty()
						       ? QString()
						       : QStringLiteral("\n%1: %2 → %3")
								 .arg(QString::fromStdString(entry.key),
								      QString::fromStdString(entry.old_value),
								      QString::fromStdString(entry.new_value)));
			QString deltas = QStringLiteral("—");
			if (entry.deltas) {
				const auto &delta = *entry.deltas;
				deltas =
					QStringLiteral(
						"CPU %1 pp · render %2 ms\nskip %3 · missed %4 · drop %5 pp\nbitrate %6 kb/s · %7 → %8")
						.arg(signed_value(delta.cpu_percentage_points, 1),
						     signed_value(delta.render_time_ms, 2),
						     signed_value(delta.encoder_skipped_percentage_points, 2),
						     signed_value(delta.missed_percentage_points, 2),
						     signed_value(delta.output_dropped_percentage_points, 2),
						     signed_value(delta.bitrate_kbps, 0),
						     BakeoffVerdictText(delta.verdict_before),
						     BakeoffVerdictText(delta.verdict_after));
			}
			const QString state =
				entry.state == preflight::bench::ImpactState::Settling     ? QObject::tr("Settling")
				: entry.state == preflight::bench::ImpactState::Complete   ? QObject::tr("Complete")
				: entry.state == preflight::bench::ImpactState::NoBaseline ? QObject::tr("No baseline")
											   : QObject::tr("Waiting");
			const int index = static_cast<int>(row);
			impact_table->setItem(index, 0, new QTableWidgetItem(change));
			impact_table->setItem(index, 1, new QTableWidgetItem(window_text(entry.baseline)));
			impact_table->setItem(index, 2, new QTableWidgetItem(window_text(entry.after)));
			impact_table->setItem(index, 3, new QTableWidgetItem(deltas));
			impact_table->setItem(index, 4, new QTableWidgetItem(state));
		}
		impact_table->resizeRowsToContents();
		impact_table->setMaximumHeight(std::min(230, impact_table->horizontalHeader()->sizeHint().height() +
								     impact_table->verticalHeader()->length() +
								     impact_table->frameWidth() * 2 + 4));
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

	std::vector<double> MetricValues(size_t index) const
	{
		std::vector<double> values;
		values.reserve(points.size());
		for (const BenchPoint &point : points) {
			switch (index) {
			case 0:
				values.push_back(point.cpu_percent);
				break;
			case 1:
				values.push_back(point.average_frame_time_ms);
				break;
			case 2:
				values.push_back(point.render_lag_percent);
				break;
			case 3:
				values.push_back(point.encoder_skipped_percent);
				break;
			case 4:
				values.push_back(point.dropped_percent);
				break;
			case 5:
				values.push_back(point.bitrate_kbps);
				break;
			case 6:
				values.push_back(point.congestion_percent);
				break;
			case 7:
				values.push_back(point.connect_time_ms);
				break;
			default:
				break;
			}
		}
		return values;
	}

	void UpdateChartDetail()
	{
		if (!chart_detail || selected_metric < 0 || selected_metric >= static_cast<int>(charts.size())) {
			return;
		}
		const size_t index = static_cast<size_t>(selected_metric);
		chart_detail_group->setTitle(charts[index].title);
		const QColor primary = chart_detail->palette().color(QPalette::Highlight);
		const QColor warning_color = theme_class_color(chart_detail, "text-warning");
		const QColor danger_color = theme_class_color(chart_detail, "text-danger");
		std::vector<ChartSeries> series;
		if (index == 1) {
			// The frame interval is drawn as the 100% line, so only render time needs a series.
			std::vector<double> render;
			render.reserve(points.size());
			for (const BenchPoint &point : points) {
				render.push_back(point.average_frame_time_ms);
			}
			series.push_back({charts[index].names[0], primary, std::move(render)});
		} else {
			series.push_back({charts[index].names.front(), primary, MetricValues(index)});
		}
		chart_detail->SetStartsAtZero(index == 0 || index == 2 || index == 3 || index == 4 || index == 6);
		std::vector<ChartWarningLine> warnings = charts[index].warning_lines;
		for (size_t line = 0; line < warnings.size(); ++line) {
			// Each chart lists its OBS warning band first and its danger band second.
			warnings[line].color = line == 0 ? warning_color : danger_color;
		}
		if (index == 1 && !points.empty() && warnings.size() == 2) {
			warnings[0].value = points.back().frame_interval_ms * OBS_RENDER_WARNING_FRACTION;
			warnings[1].value = points.back().frame_interval_ms * OBS_RENDER_DANGER_FRACTION;
		}
		chart_detail->SetWarningLines(std::move(warnings));
		chart_detail->SetSeries(std::move(series));
	}

	void ShowMetricDetail(int index)
	{
		if (index < 0) {
			selected_metric = -1;
			chart_detail_group->setVisible(false);
			return;
		}
		if (index >= static_cast<int>(charts.size())) {
			return;
		}
		selected_metric = index;
		chart_detail_group->setVisible(true);
		UpdateChartDetail();
	}

	void ResizeBakeoffTableToContents()
	{
		if (!bakeoff_table) {
			return;
		}
		bakeoff_table->resizeRowsToContents();
		int content_height = bakeoff_table->horizontalHeader()->sizeHint().height() +
				     bakeoff_table->verticalHeader()->length() + bakeoff_table->frameWidth() * 2 + 2;
		content_height = std::max(content_height, bakeoff_table->horizontalHeader()->sizeHint().height() + 8);
		bakeoff_table->setFixedHeight(std::min(content_height, MAX_BAKEOFF_TABLE_VIEW_HEIGHT));
	}

	void ResizeMetricTableToContents()
	{
		if (!metric_table) {
			return;
		}
		// Show all eight rows without a scroll bar; the detail chart takes the remaining height.
		metric_table->resizeRowsToContents();
		metric_table->setFixedHeight(metric_table->horizontalHeader()->sizeHint().height() +
					     metric_table->verticalHeader()->length() + metric_table->frameWidth() * 2);
	}

	void ResizeApplyTableToContents()
	{
		if (!apply_table) {
			return;
		}
		apply_table->resizeRowsToContents();
		int content_height = apply_table->horizontalHeader()->sizeHint().height() +
				     apply_table->verticalHeader()->length() + apply_table->frameWidth() * 2 + 2;
		content_height = std::max(content_height, apply_table->horizontalHeader()->sizeHint().height() + 8);
		apply_table->setFixedHeight(std::min(content_height, 190));
	}

	void BuildUi()
	{
		auto *root = new QVBoxLayout(dialog);
		auto *header = new QHBoxLayout;
		auto *title = new QLabel(QObject::tr("Preflight"), dialog);
		setClasses(title, "text-title");
		state_label = new QLabel(QObject::tr("Ready"), dialog);
		setClasses(state_label, "text-muted");
		run_button = new QPushButton(
			cloudflare_speed_only ? QObject::tr("Run speed test") : QObject::tr("Start bench"), dialog);
		run_button->setObjectName(QStringLiteral("preflight-start-button"));
		header->addWidget(title);
		auto *summary = new QFrame(dialog);
		summary->setObjectName(QStringLiteral("preflight-verdict-summary"));
		auto *summary_layout = new QHBoxLayout(summary);
		verdict_label = new QLabel(QObject::tr("Waiting"), summary);
		verdict_label->setObjectName(QStringLiteral("preflight-verdict"));
		setClasses(verdict_label, "bg-info text-info");
		reason_label =
			new QLabel(QObject::tr("Start a private test to see the current OBS warning bands."), summary);
		reason_label->setObjectName(QStringLiteral("preflight-reason"));
		reason_label->setWordWrap(true);
		summary_layout->addWidget(verdict_label, 0, Qt::AlignVCenter);
		summary_layout->addWidget(reason_label, 1);
		header->addWidget(summary, 1);
		header->addWidget(run_button);
		root->addLayout(header);
		auto *status_row = new QHBoxLayout;
		state_label->setObjectName(QStringLiteral("preflight-operation-status"));
		status_row->addWidget(state_label, 1);

		QString destination;
		if (supported_test_mode.isEmpty()) {
			destination = QObject::tr("No safe test service selected");
		} else if (cloudflare_speed_only) {
			destination = supported_test_mode;
		} else {
			destination = QObject::tr("Private test: %1").arg(supported_test_mode);
		}
		destination_label = new QLabel(destination, dialog);
		destination_label->setObjectName(QStringLiteral("preflight-destination"));
		destination_label->setWordWrap(true);
		setClasses(destination_label, "text-muted");
		destination_label->setVisible(!recorded_apply_preview);
		status_row->addWidget(destination_label, 2);
		if (cloudflare_speed_only) {
			auto *privacy_note = new QLabel(
				QObject::tr(
					"Speed test only. Cloudflare receives your IP and up to 4 MiB in each direction; no OBS stream is sent."),
				dialog);
			privacy_note->setWordWrap(true);
			status_row->addWidget(privacy_note, 2);
			uplink_result_label = new QLabel(QObject::tr("No speed test has run yet."), dialog);
			uplink_result_label->setObjectName(QStringLiteral("preflight-uplink-result"));
			uplink_result_label->setWordWrap(true);
			status_row->addWidget(uplink_result_label, 2);
		}
		root->addLayout(status_row);

		auto *one_screen = new QSplitter(Qt::Horizontal, dialog);
		one_screen->setObjectName(QStringLiteral("preflight-one-screen"));
		root->addWidget(one_screen, 1);
		auto *left_scroll = new QScrollArea(one_screen);
		left_scroll->setObjectName(QStringLiteral("preflight-settings-scroll"));
		left_scroll->setWidgetResizable(true);
		left_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		left_scroll->setMinimumWidth(350);
		auto *settings = new QWidget(left_scroll);
		settings->setObjectName(QStringLiteral("preflight-settings"));
		auto *settings_layout = new QVBoxLayout(settings);
		settings_layout->setContentsMargins(4, 4, 8, 4);
		settings_layout->setSpacing(8);
		left_scroll->setWidget(settings);
		one_screen->addWidget(left_scroll);

		auto *right_side = new QWidget(one_screen);
		right_side->setObjectName(QStringLiteral("preflight-right-side"));
		auto *right_layout = new QVBoxLayout(right_side);
		right_layout->setContentsMargins(8, 4, 4, 4);
		right_layout->setSpacing(8);
		one_screen->addWidget(right_side);
		one_screen->setStretchFactor(0, 4);
		one_screen->setStretchFactor(1, 6);
		one_screen->setSizes({430, 700});

		auto *video_group = new QGroupBox(QObject::tr("Video"), settings);
		video_group->setObjectName(QStringLiteral("preflight-video-settings"));
		auto *video_form = new QFormLayout(video_group);
		resolution_combo = new QComboBox(video_group);
		resolution_combo->setObjectName(QStringLiteral("preflight-output-resolution"));
		fps_combo = new QComboBox(video_group);
		fps_combo->setObjectName(QStringLiteral("preflight-output-fps"));
		struct obs_video_info initial_video = {};
		const bool have_video = !recorded_apply_preview && obs_get_video_info(&initial_video);
		if (have_video && video_settings.base_width == 0) {
			video_settings = {initial_video.base_width,
					  initial_video.base_height,
					  initial_video.output_width,
					  initial_video.output_height,
					  initial_video.fps_num,
					  initial_video.fps_den,
					  1,
					  true};
			initial_video_settings = video_settings;
		}
		const uint32_t output_width = video_settings.output_width ? video_settings.output_width : 1920;
		const uint32_t output_height = video_settings.output_height ? video_settings.output_height : 1080;
		const std::array<std::pair<uint32_t, uint32_t>, 4> output_sizes = {
			std::pair{output_width, output_height}, std::pair{1920U, 1080U}, std::pair{1280U, 720U},
			std::pair{960U, 540U}};
		for (const auto &[width, height] : output_sizes) {
			const QString label = QStringLiteral("%1 × %2").arg(width).arg(height);
			if (resolution_combo->findText(label) < 0) {
				resolution_combo->addItem(label, QVariant::fromValue(QSize(static_cast<int>(width),
											   static_cast<int>(height))));
			}
		}
		const uint32_t base_fps_num = video_settings.fps_num ? video_settings.fps_num : 60;
		const uint32_t base_fps_den = video_settings.fps_den && video_settings.fps_divisor
						      ? video_settings.fps_den / video_settings.fps_divisor
						      : 1;
		for (uint32_t divisor = 1; divisor <= 4; ++divisor) {
			const double fps = static_cast<double>(base_fps_num) / (base_fps_den * divisor);
			fps_combo->addItem(QString::number(fps, 'f', std::abs(fps - std::round(fps)) < 0.001 ? 0 : 2),
					   QVariant::fromValue(divisor));
		}
		if (have_video) {
			const QString current_resolution =
				QStringLiteral("%1 × %2").arg(output_width).arg(output_height);
			resolution_combo->setCurrentText(current_resolution);
			fps_combo->setCurrentIndex(fps_combo->findData(
				static_cast<uint>(video_settings.fps_divisor ? video_settings.fps_divisor : 1)));
		}
		video_form->addRow(QObject::tr("Output resolution"), resolution_combo);
		video_form->addRow(QObject::tr("Frame rate"), fps_combo);
		auto *canvas_note =
			new QLabel(QObject::tr("Base canvas stays %1 × %2 while OBS is running.")
					   .arg(video_settings.base_width ? video_settings.base_width : 1920)
					   .arg(video_settings.base_height ? video_settings.base_height : 1080),
				   video_group);
		canvas_note->setObjectName(QStringLiteral("preflight-base-canvas-note"));
		setClasses(canvas_note, "text-muted");
		video_form->addRow(QString(), canvas_note);
		settings_layout->addWidget(video_group);

		charts[0] = {QObject::tr("CPU"), {QObject::tr("CPU")}, {}};
		charts[1] = {QObject::tr("Render time vs interval"),
			     {QObject::tr("Render"), QObject::tr("Interval")},
			     {{QObject::tr("75% interval"), 0.0, {}}, {QObject::tr("100% interval"), 0.0, {}}}};
		const std::vector<ChartWarningLine> frame_loss_warnings = {
			{QObject::tr("1% OBS warning"), OBS_STATS_WARNING_PERCENT, {}},
			{QObject::tr("5% OBS danger"), OBS_STATS_DANGER_PERCENT, {}},
		};
		charts[2] = {QObject::tr("Missed frames"), {QObject::tr("Missed")}, frame_loss_warnings};
		charts[3] = {QObject::tr("Encoder skipped frames"), {QObject::tr("Skipped")}, frame_loss_warnings};
		charts[4] = {QObject::tr("Stream dropped frames"), {QObject::tr("Dropped")}, frame_loss_warnings};
		charts[5] = {QObject::tr("Stream bitrate"), {QObject::tr("kb/s")}, {}};
		charts[6] = {QObject::tr("Congestion"),
			     {QObject::tr("%")},
			     {{QObject::tr("33.33%"), OBS_CONGESTION_WARNING_PERCENT, {}},
			      {QObject::tr("66.67%"), OBS_CONGESTION_DANGER_PERCENT, {}}}};
		charts[7] = {QObject::tr("Connect time"), {QObject::tr("ms")}, {}};

		metric_table = new QTableWidget(static_cast<int>(metric_rows.size()), 4, right_side);
		metric_table->setObjectName(QStringLiteral("preflight-metric-table"));
		metric_table->setHorizontalHeaderLabels(
			{QObject::tr("Metric"), QObject::tr("Value"), QObject::tr("Trend"), QObject::tr("Status")});
		metric_table->verticalHeader()->hide();
		metric_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		metric_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		metric_table->setSelectionMode(QAbstractItemView::SingleSelection);
		metric_table->setShowGrid(false);
		metric_table->setWordWrap(false);
		metric_table->setFocusPolicy(Qt::NoFocus);
		// Names, values and trends stay together on the left, as in OBS's Stats window; the
		// status column takes the remaining width.
		metric_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		metric_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		metric_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
		metric_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
		metric_table->horizontalHeader()->setMinimumSectionSize(72);
		metric_table->horizontalHeader()->resizeSection(2, 160);
		metric_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		metric_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		metric_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		const std::array<QString, 8> metric_names = {
			QObject::tr("CPU"),        QObject::tr("Render time vs interval"),
			QObject::tr("Missed"),     QObject::tr("Skipped"),
			QObject::tr("Dropped"),    QObject::tr("Bitrate"),
			QObject::tr("Congestion"), QObject::tr("Connect time")};
		for (size_t row = 0; row < metric_rows.size(); ++row) {
			auto *metric = new QTableWidgetItem(metric_names[row]);
			metric->setFlags(metric->flags() & ~Qt::ItemIsEditable);
			metric_table->setItem(static_cast<int>(row), 0, metric);
			auto *value = new QLabel(QStringLiteral("—"), metric_table);
			value->setObjectName(QStringLiteral("preflight-metric-value-%1").arg(row));
			setClasses(value, "text-bold");
			value->setContentsMargins(12, 0, 16, 0);
			metric_table->setCellWidget(static_cast<int>(row), 1, value);
			auto *sparkline = new MetricSparkline(metric_table);
			sparkline->setObjectName(QStringLiteral("preflight-metric-trend-%1").arg(row));
			metric_table->setCellWidget(static_cast<int>(row), 2, sparkline);
			auto *status = new QLabel(QStringLiteral("—"), metric_table);
			status->setObjectName(QStringLiteral("preflight-metric-status-%1").arg(row));
			metric_table->setCellWidget(static_cast<int>(row), 3, status);
			metric_rows[row] = {value, sparkline, status};
		}
		connect(metric_table, &QTableWidget::currentCellChanged, dialog,
			[this](int row, int, int, int) { ShowMetricDetail(row); });
		ResizeMetricTableToContents();
		auto *metrics_group = new QGroupBox(QObject::tr("Live stats"), right_side);
		metrics_group->setObjectName(QStringLiteral("preflight-metrics"));
		auto *metrics_layout = new QVBoxLayout(metrics_group);
		metrics_layout->setContentsMargins(6, 8, 6, 6);
		metrics_layout->addWidget(metric_table);
		right_layout->addWidget(metrics_group);

		chart_detail_group = new QGroupBox(QObject::tr("Metric detail"), right_side);
		chart_detail_group->setObjectName(QStringLiteral("preflight-chart-detail-group"));
		auto *detail_layout = new QVBoxLayout(chart_detail_group);
		chart_detail = new MetricChart(chart_detail_group);
		chart_detail->setObjectName(QStringLiteral("preflight-chart-detail"));
		chart_detail->setMinimumHeight(148);
		detail_layout->addWidget(chart_detail);
		chart_detail_group->setVisible(false);
		right_layout->addWidget(chart_detail_group);

		auto *preview_group = new QGroupBox(QObject::tr("Live encoded preview"), right_side);
		preview_group->setObjectName(QStringLiteral("preflight-live-preview"));
		auto *preview_layout = new QVBoxLayout(preview_group);
		loupe_status_label = new QLabel(QObject::tr("Waiting for the live bench."), preview_group);
		loupe_status_label->setWordWrap(true);
		loupe_view = new LoupeView(preview_group);
		loupe_view->setObjectName(QStringLiteral("preflight-loupe-view"));
		loupe_view->setMinimumHeight(220);
		loupe_view->setMaximumHeight(350);
		preview_layout->addWidget(loupe_view, 1);
		preview_layout->addWidget(loupe_status_label);
		right_layout->insertWidget(0, preview_group, 2);
		auto *impact_group = new QGroupBox(QObject::tr("Impact of recent changes"), right_side);
		impact_group->setObjectName(QStringLiteral("preflight-impact-group"));
		auto *impact_layout = new QVBoxLayout(impact_group);
		impact_table = new QTableWidget(0, 5, impact_group);
		impact_table->setObjectName(QStringLiteral("preflight-impact-list"));
		impact_table->setHorizontalHeaderLabels(
			{QObject::tr("Change"), QObject::tr("Baseline"), QObject::tr("After"),
			 QObject::tr("Δ · percent points unless shown"), QObject::tr("State")});
		impact_table->verticalHeader()->hide();
		impact_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		impact_table->setWordWrap(true);
		impact_table->setTextElideMode(Qt::ElideNone);
		impact_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		impact_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		impact_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		impact_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
		impact_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
		impact_table->setMaximumHeight(150);
		impact_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
		impact_layout->addWidget(impact_table);
		right_layout->addWidget(impact_group);
		right_layout->setStretch(0, 3);
		right_layout->setStretch(1, 2);
		right_layout->setStretch(2, 0);

		auto *stream_group = new QGroupBox(QObject::tr("Stream encoder"), settings);
		stream_group->setObjectName(QStringLiteral("preflight-stream-settings"));
		stream_settings_layout = new QVBoxLayout(stream_group);
		if (encoder_properties) {
			stream_settings_layout->addWidget(encoder_properties);
		} else {
			stream_settings_layout->addWidget(new QLabel(
				setup_error.isEmpty() ? QObject::tr("Encoder settings unavailable") : setup_error,
				stream_group));
		}
		settings_layout->addWidget(stream_group);
		auto *recording_group = new QGroupBox(QObject::tr("Recording encoder"), settings);
		recording_group->setObjectName(QStringLiteral("preflight-recording-settings"));
		auto *recording_layout = new QVBoxLayout(recording_group);
		if (initial_output_mode == QStringLiteral("Simple") && !recorded_apply_preview) {
			auto *quality_form = new QFormLayout;
			simple_recording_quality_combo = new QComboBox(recording_group);
			simple_recording_quality_combo->setObjectName(
				QStringLiteral("preflight-simple-recording-quality"));
			simple_recording_quality_combo->addItem(QObject::tr("Same as stream"),
								QStringLiteral("Stream"));
			simple_recording_quality_combo->addItem(QObject::tr("High quality, medium file"),
								QStringLiteral("Small"));
			simple_recording_quality_combo->addItem(QObject::tr("Indistinguishable, large file"),
								QStringLiteral("HQ"));
			simple_recording_quality_combo->addItem(QObject::tr("Lossless"), QStringLiteral("Lossless"));
			const int quality_index =
				simple_recording_quality_combo->findData(initial_simple_recording_quality);
			if (quality_index >= 0) {
				simple_recording_quality_combo->setCurrentIndex(quality_index);
			}
			simple_recording_quality_combo->setEnabled(recording_encoder_properties != nullptr);
			quality_form->addRow(QObject::tr("Simple recording quality"), simple_recording_quality_combo);
			recording_layout->addLayout(quality_form);
			QObject::connect(
				simple_recording_quality_combo, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
				[this](int index) {
					if (index < 0 || recorded_apply_preview) {
						return;
					}
					const QString next = simple_recording_quality_combo->currentData().toString();
					if (next == current_simple_recording_quality) {
						return;
					}
					const QString previous = current_simple_recording_quality;
					if (!ApplySimpleRecordingQuality(next)) {
						const QSignalBlocker blocker(simple_recording_quality_combo);
						simple_recording_quality_combo->setCurrentIndex(
							simple_recording_quality_combo->findData(previous));
						SetStatus(QObject::tr(
							"This Simple recording quality cannot be previewed by the active encoder."));
						return;
					}
					impact_tracker.RecordChanges({{"Recording quality", "RecQuality",
								       previous.toStdString(), next.toStdString()}});
					current_simple_recording_quality = next;
					RenderImpactEntries(impact_tracker.Entries());
					InvalidateApplyPreview();
				});
		}
		if (recording_encoder_properties) {
			recording_layout->addWidget(recording_encoder_properties);
		} else {
			recording_layout->addWidget(
				new QLabel(QObject::tr("Recording encoder settings unavailable"), recording_group));
		}
		settings_layout->addWidget(recording_group);
		auto *toggle_group = new QGroupBox(QObject::tr("Live filters"), settings);
		toggle_group->setObjectName(QStringLiteral("preflight-feature-toggles"));
		toggle_layout = new QVBoxLayout(toggle_group);
		captions_toggle = new QCheckBox(QObject::tr("Live captions (on-device)"), toggle_group);
		captions_toggle->setObjectName(QStringLiteral("preflight-toggle-live-captions"));
		captions_toggle->setEnabled(false);
		captions_toggle->setToolTip(QObject::tr("This filter is not available in the current OBS scene."));
		toggle_layout->addWidget(captions_toggle);
		auto *toggle_note = new QLabel(
			QObject::tr("Filter toggles take effect immediately and are not part of Apply."), toggle_group);
		toggle_note->setObjectName(QStringLiteral("preflight-toggle-note"));
		toggle_note->setWordWrap(true);
		setClasses(toggle_note, "text-muted");
		toggle_layout->addWidget(toggle_note);
		settings_layout->addWidget(toggle_group);

		auto *bakeoff_group = new QGroupBox(QObject::tr("Encoder bake-off"), settings);
		bakeoff_group->setObjectName(QStringLiteral("preflight-bakeoff-group"));
		bakeoff_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
		auto *bakeoff_layout = new QVBoxLayout(bakeoff_group);
		auto *bakeoff_controls = new QHBoxLayout;
		bakeoff_status_label = new QLabel(QObject::tr("No bake-off has run yet."), bakeoff_group);
		bakeoff_status_label->setWordWrap(true);
		bakeoff_button = new QPushButton(QObject::tr("Try every encoder…"), bakeoff_group);
		bakeoff_button->setObjectName(QStringLiteral("preflight-bakeoff-button"));
		bakeoff_controls->addWidget(bakeoff_status_label, 1);
		bakeoff_controls->addWidget(bakeoff_button);
		bakeoff_table = new QTableWidget(0, 3, bakeoff_group);
		bakeoff_table->setObjectName(QStringLiteral("preflight-bakeoff-table"));
		bakeoff_table->setHorizontalHeaderLabels(
			{QObject::tr("Candidate"), QObject::tr("Result"), QObject::tr("Action")});
		bakeoff_table->verticalHeader()->hide();
		bakeoff_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		bakeoff_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		bakeoff_table->setSortingEnabled(false);
		bakeoff_table->setWordWrap(true);
		bakeoff_table->setTextElideMode(Qt::ElideNone);
		// Keep the action label intact in the narrower settings column; let the
		// candidate and result descriptions wrap into the remaining space.
		bakeoff_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		bakeoff_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
		bakeoff_table->horizontalHeader()->resizeSection(0, 108);
		bakeoff_table->horizontalHeader()->resizeSection(2, 104);
		bakeoff_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		bakeoff_table->verticalHeader()->setMinimumSectionSize(48);
		bakeoff_table->setMinimumHeight(0);
		bakeoff_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		bakeoff_layout->addLayout(bakeoff_controls);
		bakeoff_layout->addWidget(bakeoff_table);
		settings_layout->addWidget(bakeoff_group);

		recording_path_label = new QLabel(QObject::tr("Created when the bench starts."), settings);
		recording_path_label->setObjectName(QStringLiteral("preflight-recording-path"));
		recording_path_label->setWordWrap(true);
		setClasses(recording_path_label, "text-muted");
		settings_layout->addWidget(recording_path_label);

		if (encoder_properties || recorded_apply_preview) {
			apply_group = new QGroupBox(QObject::tr("Apply to OBS"), settings);
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
			apply_table->setMinimumHeight(0);
			apply_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			apply_note = new QLabel(apply_group);
			apply_note->setObjectName(QStringLiteral("preflight-apply-note"));
			apply_note->setWordWrap(true);
			apply_button = new QPushButton(QObject::tr("Apply"), apply_group);
			apply_button->setObjectName(QStringLiteral("preflight-apply-button"));
			apply_button->setEnabled(false);
			apply_layout->addWidget(apply_mode_label);
			apply_layout->addWidget(apply_table);
			apply_layout->addWidget(apply_note);
			auto *apply_buttons = new QHBoxLayout;
			apply_buttons->addStretch(1);
			apply_buttons->addWidget(apply_button);
			apply_layout->addLayout(apply_buttons);
			settings_layout->addWidget(apply_group);
		}
		settings_layout->addStretch(1);
		QObject::connect(resolution_combo, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
				 [this](int) { ApplyVideoControls(); });
		QObject::connect(fps_combo, qOverload<int>(&QComboBox::currentIndexChanged), dialog,
				 [this](int) { ApplyVideoControls(); });
		if (cloudflare_speed_only) {
			bakeoff_button->setEnabled(false);
			SetStatus(QObject::tr("Ready · Cloudflare speed test only"));
		} else if (!setup_error.isEmpty()) {
			SetStatus(recorded_apply_preview ? QObject::tr("Recorded preview") : setup_error);
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
			const QString candidate_label = recorded.encoder + QStringLiteral("\n") + recorded.resolution +
							QStringLiteral(" · ") + recorded.fps + QStringLiteral(" fps");
			auto *candidate_item = new QTableWidgetItem(candidate_label);
			candidate_item->setToolTip(candidate_label);
			bakeoff_table->setItem(table_row, 0, candidate_item);
			const QString result_summary = QStringLiteral("%1 ms · %2% skipped · %3")
							       .arg(result.render_headroom_ns / 1'000'000.0, 0, 'f', 2)
							       .arg(result.encoder_skip_percent, 0, 'f', 2)
							       .arg(BakeoffVerdictText(result.verdict)) +
						       QStringLiteral("\n") + QString::fromUtf8(result.reason);
			auto *result_item = new QTableWidgetItem(result_summary);
			result_item->setToolTip(result_summary);
			bakeoff_table->setItem(table_row, 1, result_item);
			auto *use = new QPushButton(QObject::tr("Use these"), bakeoff_table);
			use->setObjectName(QStringLiteral("preflight-bakeoff-use-%1").arg(row));
			use->setEnabled(false); // Recorded render fixtures have no live encoder to load.
			bakeoff_table->setCellWidget(table_row, 2, use);
		}
		ResizeBakeoffTableToContents();
		bakeoff_status_label->setText(QObject::tr("Recorded results from the preflight engine."));
		if (loupe_view) {
			loupe_view->SetPairs(recorded_data.loupe_pair, recorded_data.recording_loupe_pair
									       ? recorded_data.recording_loupe_pair
									       : recorded_data.loupe_pair);
		}
		if (loupe_status_label) {
			loupe_status_label->setText(
				recorded_data.loupe_pair ? QObject::tr("Comparing recorded source and decoded frames.")
							 : QObject::tr("No recorded frame pair is available."));
		}
		RenderImpactEntries(recorded_data.impacts);
		for (const auto &toggle : recorded_data.toggles) {
			if (toggle.name == "Live captions (on-device)" && captions_toggle) {
				captions_toggle->setChecked(toggle.enabled);
				captions_toggle->setEnabled(toggle.available);
				if (!toggle.status.empty()) {
					captions_toggle->setToolTip(QString::fromStdString(toggle.status));
				}
			}
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
		const preflight::bench::MetricWindow completed{++impact_window_sequence, window};
		metric_history.push_back(completed);
		if (metric_history.size() > MAX_ENGINE_WINDOWS) {
			metric_history.erase(
				metric_history.begin(),
				metric_history.begin() +
					static_cast<std::ptrdiff_t>(metric_history.size() - MAX_ENGINE_WINDOWS));
		}
		impact_tracker.ObserveCompleteWindow(completed);
		RenderImpactEntries(impact_tracker.Entries());
		if (points.size() > CHART_HISTORY_SIZE) {
			points.erase(points.begin(),
				     points.begin() + static_cast<std::ptrdiff_t>(points.size() - CHART_HISTORY_SIZE));
		}
		UpdateCharts();
		UpdateVerdict();
	}

	void UpdateCharts()
	{
		if (points.empty()) {
			return;
		}
		const BenchPoint &point = points.back();
		auto percentage_status = [](double value) {
			if (value > OBS_STATS_DANGER_PERCENT) {
				return std::pair{QObject::tr("Danger"), "text-danger"};
			}
			if (value > OBS_STATS_WARNING_PERCENT) {
				return std::pair{QObject::tr("Warning"), "text-warning"};
			}
			// Like OBS's Stats window, a value inside its limits gets no status label.
			return std::pair{QString(), ""};
		};
		const double render_percent = point.frame_interval_ms > 0.0
						      ? point.average_frame_time_ms / point.frame_interval_ms * 100.0
						      : 0.0;
		const auto render_status = render_percent > OBS_RENDER_DANGER_FRACTION * 100.0
						   ? std::pair{QObject::tr("Danger"), "text-danger"}
					   : render_percent > OBS_RENDER_WARNING_FRACTION * 100.0
						   ? std::pair{QObject::tr("Warning"), "text-warning"}
						   : std::pair{QString(), ""};
		const auto missed_status = percentage_status(point.render_lag_percent);
		const auto skipped_status = percentage_status(point.encoder_skipped_percent);
		const auto dropped_status = percentage_status(point.dropped_percent);
		const auto congestion_status = point.congestion_percent > OBS_CONGESTION_DANGER_PERCENT
						       ? std::pair{QObject::tr("Danger"), "text-danger"}
					       : point.congestion_percent > OBS_CONGESTION_WARNING_PERCENT
						       ? std::pair{QObject::tr("Warning"), "text-warning"}
						       : std::pair{QString(), ""};
		const std::array<QString, 8> values = {
			QStringLiteral("%1 %").arg(point.cpu_percent, 0, 'f', 1),
			QStringLiteral("%1 / %2 ms")
				.arg(point.average_frame_time_ms, 0, 'f', 2)
				.arg(point.frame_interval_ms, 0, 'f', 2),
			QStringLiteral("%1 %").arg(point.render_lag_percent, 0, 'f', 2),
			QStringLiteral("%1 %").arg(point.encoder_skipped_percent, 0, 'f', 2),
			QStringLiteral("%1 %").arg(point.dropped_percent, 0, 'f', 2),
			QStringLiteral("%1 kb/s").arg(point.bitrate_kbps, 0, 'f', 0),
			QStringLiteral("%1 %").arg(point.congestion_percent, 0, 'f', 1),
			point.connect_time_ms <= 0 ? QObject::tr("Waiting")
						   : QStringLiteral("%1 ms").arg(point.connect_time_ms, 0, 'f', 0),
		};
		const std::array<QString, 8> statuses = {QString(),
							 render_status.first,
							 missed_status.first,
							 skipped_status.first,
							 dropped_status.first,
							 QString(),
							 congestion_status.first,
							 point.connect_time_ms <= 0 ? QObject::tr("Waiting")
										    : QString()};
		const std::array<const char *, 8> classes = {"",
							     render_status.second,
							     missed_status.second,
							     skipped_status.second,
							     dropped_status.second,
							     "",
							     congestion_status.second,
							     ""};
		for (size_t index = 0; index < metric_rows.size(); ++index) {
			metric_rows[index].value->setText(values[index]);
			metric_rows[index].status->setText(statuses[index]);
			setClasses(metric_rows[index].value, classes[index]);
			setClasses(metric_rows[index].status, classes[index]);
			metric_rows[index].sparkline->SetPoints(MetricValues(index));
		}
		ResizeMetricTableToContents();
		UpdateChartDetail();
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
		const char *verdict_class = point.verdict == OBS_PREFLIGHT_PASS ? "bg-success text-success text-bold"
					    : point.verdict == OBS_PREFLIGHT_MARGINAL
						    ? "bg-warning text-warning text-bold"
						    : "bg-danger text-danger text-bold";
		setClasses(verdict_label, verdict_class);
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
			ResizeBakeoffTableToContents();
		}
	}

	void SetCandidateMetrics(const obs_preflight_window &window)
	{
		candidate_render_headroom_ns = window.render_headroom_ns;
		candidate_encoder_skip_percent = window.encoder_skip_percent;
		SetBakeoffCell(1, QStringLiteral("%1 ms · %2% skipped · %3")
					  .arg(candidate_render_headroom_ns / 1'000'000.0, 0, 'f', 2)
					  .arg(candidate_encoder_skip_percent, 0, 'f', 2)
					  .arg(BakeoffVerdictText(candidate_verdict)));
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
		SetBakeoffCell(1, QStringLiteral("%1 ms · %2% skipped · %3\n%4")
					  .arg(candidate_render_headroom_ns / 1'000'000.0, 0, 'f', 2)
					  .arg(candidate_encoder_skip_percent, 0, 'f', 2)
					  .arg(BakeoffVerdictText(candidate_verdict))
					  .arg(final_reason.isEmpty()
						       ? QObject::tr("No plain-language reason was reported.")
						       : final_reason));
		bakeoff_table->item(static_cast<int>(active_bakeoff_row), 1)->setToolTip(final_reason);
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
		SetBakeoffUseActionsEnabled(true);
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
		SetBakeoffCell(1, QObject::tr("Running"));
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
			SetBakeoffCell(1, QObject::tr("Stopped\nStopped before this candidate finished."));
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

	void RecordEditorChanges(preflight::bench::EncoderRole role)
	{
		OBSPropertiesView *view = role == preflight::bench::EncoderRole::Stream ? encoder_properties
											: recording_encoder_properties;
		QJsonObject &previous = role == preflight::bench::EncoderRole::Stream ? last_stream_editor_settings
										      : last_recording_editor_settings;
		if (!view) {
			return;
		}
		const QJsonObject current = json_object_from_settings(view->GetSettings());
		QStringList keys;
		for (auto it = previous.constBegin(); it != previous.constEnd(); ++it) {
			if (current.value(it.key()) != it.value()) {
				keys.push_back(it.key());
			}
		}
		for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
			if (!previous.contains(it.key())) {
				keys.push_back(it.key());
			}
		}
		std::sort(keys.begin(), keys.end());
		std::vector<preflight::bench::SettingChange> changes;
		const std::string description = role == preflight::bench::EncoderRole::Stream ? "Stream encoder"
											      : "Recording encoder";
		for (const QString &key : keys) {
			const QString old_value = preflight::apply::ValueForDisplay(previous.value(key));
			const QString new_value = preflight::apply::ValueForDisplay(current.value(key));
			if (old_value != new_value) {
				changes.push_back({description, key.toStdString(), old_value.toStdString(),
						   new_value.toStdString()});
			}
		}
		previous = current;
		if (!changes.empty()) {
			impact_tracker.RecordChanges(changes);
			RenderImpactEntries(impact_tracker.Entries());
		}
	}

	void ApplyVideoControls()
	{
		if (!resolution_combo || !fps_combo || recorded_apply_preview || resolution_combo->currentIndex() < 0 ||
		    fps_combo->currentIndex() < 0) {
			return;
		}
		const QSize output_size = resolution_combo->currentData().toSize();
		const uint32_t requested_divisor = fps_combo->currentData().toUInt();
		if (output_size.width() <= 0 || output_size.height() <= 0 || requested_divisor == 0 ||
		    video_settings.fps_divisor == 0) {
			return;
		}
		const uint32_t base_den = video_settings.fps_den / video_settings.fps_divisor;
		preflight::bench::VideoSettings requested = video_settings;
		requested.output_width = static_cast<uint32_t>(output_size.width());
		requested.output_height = static_cast<uint32_t>(output_size.height());
		requested.fps_divisor = requested_divisor;
		requested.fps_den = base_den * requested_divisor;
		requested.fps_divisor_supported = true;
		std::vector<preflight::bench::SettingChange> changes;
		const QString old_resolution =
			QStringLiteral("%1 × %2").arg(video_settings.output_width).arg(video_settings.output_height);
		const QString new_resolution =
			QStringLiteral("%1 × %2").arg(requested.output_width).arg(requested.output_height);
		if (old_resolution != new_resolution) {
			changes.push_back({"Video", "Output resolution", old_resolution.toStdString(),
					   new_resolution.toStdString()});
		}
		const QString old_fps =
			QString::number(static_cast<double>(video_settings.fps_num) / video_settings.fps_den, 'f', 2);
		const QString new_fps =
			QString::number(static_cast<double>(requested.fps_num) / requested.fps_den, 'f', 2);
		if (requested_divisor != video_settings.fps_divisor) {
			changes.push_back({"Video", "FPS", old_fps.toStdString(), new_fps.toStdString()});
		}
		if (requested.output_width != video_settings.output_width ||
		    requested.output_height != video_settings.output_height ||
		    requested.fps_den != video_settings.fps_den) {
			SetVideoSettings(requested, changes);
			settings_timer->start(600);
		}
	}

	bool RecreateVideoEncoders()
	{
		if (!video_settings_pending || !encoder_properties || !recording_encoder_properties ||
		    !video_settings.fps_divisor_supported) {
			return !video_settings_pending;
		}
		video_t *video = obs_get_video();
		if (!video) {
			SetStatus(QObject::tr("OBS's video mix is unavailable; output mode was not applied."));
			return false;
		}
		OBSDataAutoRelease stream_settings = clone_settings(encoder_properties->GetSettings());
		OBSDataAutoRelease recording_settings = clone_settings(recording_encoder_properties->GetSettings());
		if (!stream_settings || !recording_settings) {
			SetStatus(
				QObject::tr("Could not read the bench encoder settings; output mode was not applied."));
			return false;
		}
		if (test_service) {
			obs_service_apply_encoder_settings(test_service, stream_settings, bakeoff_audio_settings);
		}
		const QByteArray stream_id = stream_video_encoder_id.toUtf8();
		const QByteArray recording_id = recording_video_encoder_id.toUtf8();
		OBSEncoderAutoRelease new_stream(obs_video_encoder_create(
			stream_id.constData(), "preflight_stream_video", stream_settings, nullptr));
		OBSEncoderAutoRelease new_recording(obs_video_encoder_create(
			recording_id.constData(), "preflight_recording_video", recording_settings, nullptr));
		if (!new_stream || !new_recording) {
			SetStatus(QObject::tr("Could not recreate both video encoders; output mode was not applied."));
			return false;
		}
		obs_encoder_set_video(new_stream, video);
		obs_encoder_set_video(new_recording, video);
		obs_encoder_set_scaled_size(new_stream, video_settings.output_width, video_settings.output_height);
		obs_encoder_set_scaled_size(new_recording, video_settings.output_width, video_settings.output_height);
		if (!obs_encoder_set_frame_rate_divisor(new_stream, video_settings.fps_divisor) ||
		    !obs_encoder_set_frame_rate_divisor(new_recording, video_settings.fps_divisor)) {
			SetStatus(QObject::tr(
				"An encoder rejected OBS's live frame-rate divisor; output mode was not applied."));
			return false;
		}
		if (stream_output) {
			obs_output_set_video_encoder(stream_output, nullptr);
		}
		if (recording_output) {
			obs_output_set_video_encoder(recording_output, nullptr);
		}
		stream_video_encoder = obs_encoder_get_ref(new_stream);
		recording_video_encoder = obs_encoder_get_ref(new_recording);
		if (stream_output) {
			obs_output_set_video_encoder(stream_output, stream_video_encoder);
		}
		if (recording_output) {
			obs_output_set_video_encoder(recording_output, recording_video_encoder);
		}
		current_output_width = video_settings.output_width;
		current_output_height = video_settings.output_height;
		current_fps_num = video_settings.fps_num;
		current_fps_den = video_settings.fps_den;
		video_settings_pending = false;
		if (engine) {
			obs_preflight_engine_destroy(engine);
			engine = obs_preflight_engine_create_with_window(SAMPLE_WINDOW_MS);
			last_window_end_ms = 0;
		}
		return engine != nullptr;
	}

	void RestartRequestedOutputs()
	{
		if (!restart_stream_when_stopped && !restart_recording_when_stopped) {
			return;
		}
		if (restart_stream_when_stopped && !PrepareYouTubeTestKey()) {
			StopOutputs(QObject::tr("Stopping after the YouTube test key could not be refreshed."),
				    youtube_test_error);
			return;
		}
		if (restart_recording_when_stopped) {
			StartRecordingLoupe();
			if (!obs_output_start(recording_output)) {
				restart_recording_when_stopped = false;
				StopOutputs(output_error(recording_output,
							 QObject::tr("Local recording could not restart.")));
				return;
			}
			restart_recording_when_stopped = false;
		}
		if (restart_stream_when_stopped) {
			StartLoupe(stream_video_encoder);
			if (!obs_output_start(stream_output)) {
				restart_stream_when_stopped = false;
				StopOutputs(output_error(stream_output, QObject::tr("Stream test could not restart.")));
				return;
			}
			restart_stream_when_stopped = false;
		}
		bench_running = true;
		sample_timer->start(static_cast<int>(SAMPLE_WINDOW_MS));
		SetStatus(QObject::tr("Bench restarted with the reviewed settings."));
	}

	void UpdateEncoderSettings()
	{
		RefreshApplyPreview();
		if (video_settings_pending) {
			const bool stream_active = stream_output && obs_output_active(stream_output);
			const bool recording_active = recording_output && obs_output_active(recording_output);
			if (stream_active || recording_active) {
				restart_stream_when_stopped = stream_active;
				restart_recording_when_stopped = recording_active;
				StopLoupe();
				if (stream_active) {
					obs_output_stop(stream_output);
				}
				if (recording_active) {
					obs_output_stop(recording_output);
				}
				sample_timer->start(100);
				SetStatus(
					QObject::tr("Restarting both bench encoders to apply the video output mode."));
				return;
			}
			if (!RecreateVideoEncoders()) {
				return;
			}
			RestartRequestedOutputs();
		}

		if (stream_settings_pending && stream_video_encoder && encoder_properties) {
			if (stream_output && obs_output_active(stream_output)) {
				if (!restart_stream_when_stopped) {
					restart_stream_when_stopped = true;
					loupe_capture.Stop();
					obs_output_stop(stream_output);
				}
				SetStatus(QObject::tr("Restarting the stream test output; local recording continues."));
				sample_timer->start(100);
				return;
			}
			obs_encoder_update(stream_video_encoder, encoder_properties->GetSettings());
			stream_settings_pending = false;
			if (restart_stream_when_stopped) {
				RestartRequestedOutputs();
			}
		}

		if (recording_settings_pending && recording_video_encoder && recording_encoder_properties) {
			if (recording_output && obs_output_active(recording_output)) {
				if (!restart_recording_when_stopped) {
					restart_recording_when_stopped = true;
					recording_loupe_capture.Stop();
					obs_output_stop(recording_output);
				}
				SetStatus(QObject::tr("Restarting the recording encoder; the stream test continues."));
				sample_timer->start(100);
				return;
			}
			obs_encoder_update(recording_video_encoder, recording_encoder_properties->GetSettings());
			recording_settings_pending = false;
			if (restart_recording_when_stopped) {
				RestartRequestedOutputs();
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
	resize(1040, 720);
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
		impl->recorded_apply_preview = recorded_data->apply_preview;
		impl->enable_recorded_apply_controls_for_test = recorded_data->enable_recorded_apply_controls_for_test;
	} else {
		impl->BuildOutputs();
	}
	impl->BuildUi();
	impl->RefreshApplyPreview();
	if (recorded_data) {
		impl->LoadRecordedData(*recorded_data);
	}
	QTimer::singleShot(0, this, [this]() {
		impl->ResizeBakeoffTableToContents();
		impl->ResizeApplyTableToContents();
	});
	connect(impl->run_button, &QPushButton::clicked, this, &PreflightBenchWindow::ToggleBench);
	connect(impl->bakeoff_button, &QPushButton::clicked, this, &PreflightBenchWindow::ToggleBakeoff);
	if (impl->apply_button) {
		connect(impl->apply_button, &QPushButton::clicked, this, [this]() { impl->ApplyConfigChanges(); });
	}
}

PreflightBenchWindow::~PreflightBenchWindow() = default;

preflight::bench::DataApi *PreflightBenchWindow::GetDataApi()
{
	return impl.get();
}

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
	impl->StartRecordingLoupe();
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
	impl->SetBakeoffUseActionsEnabled(false);
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
	const bool stream_active = impl->stream_output && obs_output_active(impl->stream_output);
	const bool recording_active = impl->recording_output && obs_output_active(impl->recording_output);
	if ((impl->restart_stream_when_stopped && !stream_active) ||
	    (impl->restart_recording_when_stopped && !recording_active) ||
	    (impl->video_settings_pending && !stream_active && !recording_active)) {
		impl->UpdateEncoderSettings();
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
