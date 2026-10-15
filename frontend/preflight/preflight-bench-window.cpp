#include "preflight-bench-window.hpp"

#include "obs-preflight-collector.h"
#include "obs-preflight.h"
#include "preflight-test-mode.hpp"

#include <properties-view.hpp>

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QColor>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr uint64_t SAMPLE_WINDOW_MS = 500;
constexpr size_t MAX_ENGINE_WINDOWS = 120;
constexpr size_t CHART_HISTORY_SIZE = 120;

struct ChartSeries {
	QString name;
	QColor color;
	std::vector<double> points;
};

class MetricChart final : public QWidget {
	std::vector<ChartSeries> series;

public:
	explicit MetricChart(QWidget *parent = nullptr) : QWidget(parent)
	{
		setMinimumHeight(114);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

	void SetSeries(std::vector<ChartSeries> series_)
	{
		series = std::move(series_);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.fillRect(rect(), palette().color(QPalette::Base));

		QRectF plot = QRectF(rect()).adjusted(12, 24, -10, -21);
		painter.setPen(palette().color(QPalette::Mid));
		for (int row = 0; row <= 3; ++row) {
			const qreal y = plot.top() + plot.height() * row / 3.0;
			painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
		}

		if (series.empty() || series.front().points.empty()) {
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(plot, Qt::AlignCenter, tr("Waiting for samples"));
			return;
		}

		double minValue = series.front().points.back();
		double maxValue = minValue;
		qreal legend_x = plot.left();
		painter.setFont(QFont(painter.font().family(), painter.font().pointSize() - 1));
		for (const auto &line : series) {
			painter.setPen(QPen(line.color, 2.0));
			painter.drawLine(QPointF(legend_x, 10), QPointF(legend_x + 13, 10));
			painter.setPen(palette().color(QPalette::Text));
			painter.drawText(QPointF(legend_x + 18, 14), line.name);
			legend_x += painter.fontMetrics().horizontalAdvance(line.name) + 34;
		}

		for (const auto &line : series) {
			for (double value : line.points) {
				minValue = std::min(minValue, value);
				maxValue = std::max(maxValue, value);
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

		for (const auto &line : series) {
			if (line.points.size() < 2) {
				continue;
			}
			const size_t first =
				line.points.size() > CHART_HISTORY_SIZE ? line.points.size() - CHART_HISTORY_SIZE : 0;
			const size_t count = line.points.size() - first;
			QPolygonF path;
			path.reserve(static_cast<qsizetype>(count));
			for (size_t i = first; i < line.points.size(); ++i) {
				const qreal x =
					plot.left() + plot.width() * static_cast<qreal>(i - first) /
							      static_cast<qreal>(std::max<size_t>(1, count - 1));
				const double normalized = (line.points[i] - minValue) / (maxValue - minValue);
				const qreal y = plot.bottom() - plot.height() * normalized;
				path.append(QPointF(x, y));
			}
			painter.setPen(QPen(line.color, 2.0));
			painter.drawPolyline(path);
		}
	}
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
	double encoder_skipped_frames = 0.0;
	double lagged_frames = 0.0;
	double dropped_frames = 0.0;
	double bitrate_kbps = 0.0;
	double congestion_percent = 0.0;
	double connect_time_ms = 0.0;
	int32_t verdict = OBS_PREFLIGHT_FAIL;
	QString reason;
};

OBSDataAutoRelease clone_settings(obs_data_t *settings)
{
	OBSDataAutoRelease copy = obs_data_create();
	if (copy && settings) {
		obs_data_apply(copy, settings);
	}
	return copy;
}

QString output_error(obs_output_t *output, const QString &fallback)
{
	const char *error = output ? obs_output_get_last_error(output) : nullptr;
	return error && *error ? QString::fromUtf8(error) : fallback;
}

QString test_mode_label(const char *service_name)
{
	if (!service_name) {
		return {};
	}
	if (strcmp(service_name, "Twitch") == 0) {
		return QObject::tr("Twitch ingest bandwidth test");
	}
	if (strcmp(service_name, "Amazon IVS") == 0) {
		return QObject::tr("Amazon IVS ingest bandwidth test");
	}
	if (strcmp(service_name, "Restream.io") == 0 || strcmp(service_name, "Restream.io - RTMP") == 0) {
		return QObject::tr("Restream ingest test");
	}
	return {};
}

} // namespace

struct PreflightBenchWindow::Impl {
	PreflightBenchWindow *dialog;
	QPushButton *run_button = nullptr;
	QLabel *destination_label = nullptr;
	QLabel *state_label = nullptr;
	QLabel *verdict_label = nullptr;
	QLabel *reason_label = nullptr;
	QLabel *recording_path_label = nullptr;
	QTimer *sample_timer = nullptr;
	QTimer *settings_timer = nullptr;
	OBSPropertiesView *encoder_properties = nullptr;
	OBSServiceAutoRelease test_service;
	OBSOutputAutoRelease stream_output;
	OBSOutputAutoRelease recording_output;
	OBSEncoderAutoRelease stream_video_encoder;
	OBSEncoderAutoRelease stream_audio_encoder;
	OBSEncoderAutoRelease recording_video_encoder;
	OBSEncoderAutoRelease recording_audio_encoder;
	obs_preflight_collector_t *collector = nullptr;
	obs_preflight_engine_t *engine = nullptr;
	std::vector<BenchPoint> points;
	std::array<ChartPane, 8> charts;
	QString supported_test_mode;
	QString recording_path;
	QString setup_error;
	QString stop_completion_status;
	uint64_t last_window_end_ms = 0;
	bool restart_stream_when_stopped = false;
	bool restart_was_active = false;
	bool bench_running = false;
	bool stopping = false;
	bool close_when_stopped = false;

	explicit Impl(PreflightBenchWindow *dialog_) : dialog(dialog_) {}

	~Impl()
	{
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
	}

	void SetStatus(const QString &text) { state_label->setText(text); }

	void StopOutputs(const QString &status, const QString &completion_status = QString())
	{
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

	bool BuildOutputs()
	{
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
		auto safe_key = preflight::make_non_public_test_key(
			service_name, obs_data_get_string(selected_service_settings, "key"));
		supported_test_mode = test_mode_label(service_name);
		if (!safe_key || supported_test_mode.isEmpty() || !service_id ||
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
		obs_data_set_string(test_service_settings, "key", safe_key->c_str());
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
		run_button = new QPushButton(QObject::tr("Start bench"), dialog);
		header->addWidget(title);
		header->addSpacing(12);
		header->addWidget(state_label);
		header->addStretch(1);
		header->addWidget(run_button);
		root->addLayout(header);

		destination_label = new QLabel(supported_test_mode.isEmpty()
						       ? QObject::tr("No safe test service selected")
						       : QObject::tr("Non-public test: %1").arg(supported_test_mode),
					       dialog);
		destination_label->setWordWrap(true);
		root->addWidget(destination_label);

		auto *splitter = new QSplitter(Qt::Horizontal, dialog);
		auto *settings_scroll = new QScrollArea(splitter);
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
		splitter->addWidget(settings_scroll);

		auto *charts_scroll = new QScrollArea(splitter);
		charts_scroll->setWidgetResizable(true);
		auto *chart_container = new QWidget(charts_scroll);
		auto *chart_layout = new QGridLayout(chart_container);
		chart_layout->setSpacing(8);
		AddChart(chart_layout, 0, 0, 0, QObject::tr("CPU"), {QObject::tr("CPU")}, {QColor("#60a5fa")});
		AddChart(chart_layout, 0, 1, 1, QObject::tr("Render time vs frame interval"),
			 {QObject::tr("Render"), QObject::tr("Interval")}, {QColor("#34d399"), QColor("#fbbf24")});
		AddChart(chart_layout, 1, 0, 2, QObject::tr("Encoder skipped frames"), {QObject::tr("Skipped")},
			 {QColor("#f97316")});
		AddChart(chart_layout, 1, 1, 3, QObject::tr("Render-missed frames"), {QObject::tr("Missed")},
			 {QColor("#f87171")});
		AddChart(chart_layout, 2, 0, 4, QObject::tr("Stream dropped frames"), {QObject::tr("Dropped")},
			 {QColor("#fb7185")});
		AddChart(chart_layout, 2, 1, 5, QObject::tr("Stream bitrate"), {QObject::tr("kb/s")},
			 {QColor("#a78bfa")});
		AddChart(chart_layout, 3, 0, 6, QObject::tr("Congestion"), {QObject::tr("%")}, {QColor("#38bdf8")});
		AddChart(chart_layout, 3, 1, 7, QObject::tr("Connect time"), {QObject::tr("ms")}, {QColor("#facc15")});
		chart_layout->setRowStretch(4, 1);
		charts_scroll->setWidget(chart_container);
		splitter->addWidget(charts_scroll);
		splitter->setStretchFactor(0, 0);
		splitter->setStretchFactor(1, 1);
		root->addWidget(splitter, 1);

		auto *summary = new QFrame(dialog);
		auto *summary_layout = new QVBoxLayout(summary);
		verdict_label = new QLabel(QObject::tr("Verdict: waiting for samples"), summary);
		reason_label = new QLabel(
			QObject::tr("Start a non-public test and Preflight will report the current OBS warning bands."),
			summary);
		reason_label->setWordWrap(true);
		summary_layout->addWidget(verdict_label);
		summary_layout->addWidget(reason_label);
		root->addWidget(summary);

		if (!setup_error.isEmpty()) {
			SetStatus(setup_error);
			run_button->setEnabled(false);
		} else if (!supported_test_mode.isEmpty()) {
			SetStatus(QObject::tr("Ready · %1").arg(supported_test_mode));
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
		point.encoder_skipped_frames = static_cast<double>(window.encoder_frames_skipped);
		point.lagged_frames = static_cast<double>(window.lagged_frames);
		point.dropped_frames = static_cast<double>(window.output_frames_dropped);
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
			values([](const BenchPoint &p) { return p.encoder_skipped_frames; }),
			values([](const BenchPoint &p) { return p.lagged_frames; }),
			values([](const BenchPoint &p) { return p.dropped_frames; }),
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
				       : QString::number(points.back().encoder_skipped_frames, 'f', 0),
			points.empty() ? QStringLiteral("—") : QString::number(points.back().lagged_frames, 'f', 0),
			points.empty() ? QStringLiteral("—") : QString::number(points.back().dropped_frames, 'f', 0),
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
		verdict_label->setText(QObject::tr("Verdict: %1").arg(verdict));
		reason_label->setText(point.reason);
	}

	void UpdateEncoderSettings()
	{
		if (!stream_video_encoder || !encoder_properties) {
			return;
		}
		if (stream_output && obs_output_active(stream_output)) {
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
			if (!obs_output_start(stream_output)) {
				SetStatus(output_error(stream_output, QObject::tr("Stream test could not restart.")));
			} else {
				SetStatus(QObject::tr("Stream test restarted; local recording was not interrupted."));
			}
		}
	}
};

PreflightBenchWindow::PreflightBenchWindow(QWidget *parent) : QDialog(parent), impl(std::make_unique<Impl>(this))
{
	setWindowTitle(tr("Preflight live bench"));
	resize(1280, 820);
	impl->sample_timer = new QTimer(this);
	impl->sample_timer->setInterval(static_cast<int>(SAMPLE_WINDOW_MS));
	connect(impl->sample_timer, &QTimer::timeout, this, &PreflightBenchWindow::Sample);
	impl->settings_timer = new QTimer(this);
	impl->settings_timer->setSingleShot(true);
	connect(impl->settings_timer, &QTimer::timeout, this, &PreflightBenchWindow::ApplyEncoderSettings);
	impl->BuildOutputs();
	impl->BuildUi();
	connect(impl->run_button, &QPushButton::clicked, this, &PreflightBenchWindow::ToggleBench);
}

PreflightBenchWindow::~PreflightBenchWindow() = default;

void PreflightBenchWindow::closeEvent(QCloseEvent *event)
{
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
	if (impl->bench_running) {
		impl->StopOutputs(tr("Stopping the stream test and local recording."), tr("Stopped"));
		return;
	}

	if (!impl->setup_error.isEmpty()) {
		impl->SetStatus(impl->setup_error);
		return;
	}
	if (!impl->PrepareRecordingPath()) {
		return;
	}
	if (!obs_output_start(impl->recording_output)) {
		impl->SetStatus(output_error(impl->recording_output, tr("Local recording could not start.")));
		return;
	}
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
