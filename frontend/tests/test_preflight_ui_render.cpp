#include "../preflight/preflight-bench-window.hpp"
#include "../preflight/preflight-apply-plan.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QTableWidget>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace preflight::loupe;

static void require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static obs_preflight_sample parse_sample(const QJsonObject &object)
{
	obs_preflight_sample sample{};
	sample.timestamp_ms = static_cast<uint64_t>(object.value(QStringLiteral("timestamp_ms")).toDouble());
	sample.average_frame_time_ns =
		static_cast<uint64_t>(object.value(QStringLiteral("average_frame_time_ns")).toDouble());
	sample.frame_interval_ns = static_cast<uint64_t>(object.value(QStringLiteral("frame_interval_ns")).toDouble());
	sample.lagged_frames = static_cast<uint32_t>(object.value(QStringLiteral("lagged_frames")).toDouble());
	sample.output_total_bytes =
		static_cast<uint64_t>(object.value(QStringLiteral("output_total_bytes")).toDouble());
	sample.output_total_frames =
		static_cast<uint64_t>(object.value(QStringLiteral("output_total_frames")).toDouble());
	sample.output_frames_dropped =
		static_cast<uint64_t>(object.value(QStringLiteral("output_frames_dropped")).toDouble());
	sample.output_congestion = object.value(QStringLiteral("output_congestion")).toDouble();
	sample.connect_time_ms = static_cast<int32_t>(object.value(QStringLiteral("connect_time_ms")).toInt());
	sample.encoder_total_frames =
		static_cast<uint64_t>(object.value(QStringLiteral("encoder_total_frames")).toDouble());
	sample.encoder_skipped_frames =
		static_cast<uint64_t>(object.value(QStringLiteral("encoder_skipped_frames")).toDouble());
	sample.cpu_percent = object.value(QStringLiteral("cpu_percent")).toDouble();
	return sample;
}

static std::vector<obs_preflight_window> run_golden_trace(const QString &name)
{
	const QString path = QDir(QStringLiteral(PREFLIGHT_GOLDEN_DIR)).filePath(name);
	QFile file(path);
	require(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not open preflight golden trace");

	std::unique_ptr<obs_preflight_engine_t, decltype(&obs_preflight_engine_destroy)> engine(
		obs_preflight_engine_create_with_window(1000), obs_preflight_engine_destroy);
	require(engine != nullptr, "could not create preflight engine");
	while (!file.atEnd()) {
		const QByteArray line = file.readLine().trimmed();
		if (line.isEmpty()) {
			continue;
		}
		QJsonParseError error{};
		const QJsonDocument document = QJsonDocument::fromJson(line, &error);
		require(error.error == QJsonParseError::NoError && document.isObject(), "invalid JSON in golden trace");
		const obs_preflight_sample sample = parse_sample(document.object());
		require(obs_preflight_engine_push_sample(engine.get(), &sample) == OBS_PREFLIGHT_OK,
			"preflight engine rejected a golden sample");
	}

	size_t count = 0;
	require(obs_preflight_engine_get_window_count(engine.get(), &count) == OBS_PREFLIGHT_OK && count > 0,
		"golden trace produced no engine windows");
	std::vector<obs_preflight_window> windows(count);
	for (size_t index = 0; index < count; ++index) {
		require(obs_preflight_engine_get_window(engine.get(), index, &windows[index]) == OBS_PREFLIGHT_OK,
			"could not read preflight engine window");
	}
	return windows;
}

static std::shared_ptr<const FramePair> make_decoded_loupe_pair()
{
	constexpr uint32_t width = 512;
	constexpr uint32_t height = 384;
	constexpr std::array<std::array<uint8_t, 4>, 4> colors = {
		{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}}};
	std::vector<uint8_t> source_pixels(width * height * 4);
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			const auto &color = colors[(y / 96 % 2) * 2 + (x / 128 % 2)];
			const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
			std::copy(color.begin(), color.end(),
				  source_pixels.begin() + static_cast<std::ptrdiff_t>(offset));
		}
	}
	std::vector<uint8_t> encoded_pixels = source_pixels;
	for (uint32_t y = height / 2 - 4; y < height / 2 + 4; ++y) {
		for (uint32_t x = width / 2 - 4; x < width / 2 + 4; ++x) {
			const size_t changed_pixel = (static_cast<size_t>(y) * width + x) * 4;
			encoded_pixels[changed_pixel] = 0;
			encoded_pixels[changed_pixel + 1] = 255;
			encoded_pixels[changed_pixel + 2] = 255;
		}
	}

	EncodedVideoDecoder decoder;
	require(decoder.Open(DecoderConfig{"rawvideo", width, height, "rgba", {}}),
		"FFmpeg rawvideo decoder did not open");
	EncodedVideoPacket packet{encoded_pixels, 0, 0, 1, 30, 900'000'000, true};
	std::vector<PixelFrame> decoded;
	require(decoder.Decode(packet, decoded) && decoded.size() == 1, "rawvideo pattern did not decode");

	auto source = std::make_shared<PixelFrame>(
		PixelFrame{width, height, width * 4, packet.capture_timestamp_ns, std::move(source_pixels)});
	FramePairer pairer;
	pairer.PushSource(source);
	const auto pair = pairer.Match(std::make_shared<PixelFrame>(std::move(decoded.front())), 0);
	require(pair.has_value(), "decoded pattern did not pair with its source frame");
	return std::make_shared<FramePair>(*pair);
}

static void require_nonblank(const QPixmap &pixmap, const char *message)
{
	require(!pixmap.isNull() && pixmap.width() > 1 && pixmap.height() > 1, message);
	const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
	std::set<QRgb> colors;
	for (int y = 0; y < image.height() && colors.size() <= 8; y += 3) {
		const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = 0; x < image.width() && colors.size() <= 8; x += 3) {
			colors.insert(line[x]);
		}
	}
	require(colors.size() > 3, message);
}

static void save_png(QWidget &widget, const QString &path)
{
	const QPixmap pixmap = widget.grab();
	require(qFuzzyCompare(pixmap.devicePixelRatioF(), 2.0), "screenshot device-pixel ratio is not 2x");
	require(pixmap.width() == widget.width() * 2 && pixmap.height() == widget.height() * 2,
		"screenshot pixel dimensions are not twice the widget dimensions");
	require_nonblank(pixmap, "screenshot is blank");
	require(pixmap.save(path, "PNG"), "could not save screenshot PNG");
	std::cout << QFileInfo(path).absoluteFilePath().toStdString() << '\n';
}

static void test_apply_plans()
{
	using preflight::apply::BuildAdvancedPlan;
	using preflight::apply::BuildSimplePlan;
	using preflight::apply::OutputMode;

	const QJsonObject saved_encoder{{QStringLiteral("bitrate"), 6000},
					{QStringLiteral("preset"), QStringLiteral("fast")},
					{QStringLiteral("profile"), QStringLiteral("high")}};
	const QJsonObject initial_encoder{{QStringLiteral("bitrate"), 6000},
					  {QStringLiteral("preset"), QStringLiteral("veryfast")},
					  {QStringLiteral("profile"), QStringLiteral("high")}};
	const QJsonObject edited_encoder{{QStringLiteral("bitrate"), 4500},
					 {QStringLiteral("preset"), QStringLiteral("superfast")},
					 {QStringLiteral("profile"), QStringLiteral("high")}};
	const auto advanced = BuildAdvancedPlan(saved_encoder, initial_encoder, edited_encoder);
	require(advanced.mode == OutputMode::Advanced && advanced.applicable(),
		"advanced settings did not produce an applicable config preview");
	require(advanced.changes.size() == 2 && advanced.changes[0].path == QStringLiteral("streamEncoder.json/bitrate") &&
			advanced.changes[1].path == QStringLiteral("streamEncoder.json/preset"),
		"advanced preview did not list the exact changed encoder keys");
	require(advanced.updated_settings.value(QStringLiteral("profile")) == QJsonValue(QStringLiteral("high")),
		"advanced apply plan discarded an untouched setting");

	const QJsonObject simple_config{{QStringLiteral("VBitrate"), 2500},
					{QStringLiteral("Preset"), QStringLiteral("veryfast")},
					{QStringLiteral("UseAdvanced"), false}};
	const QJsonObject simple_before{{QStringLiteral("bitrate"), 2500},
					{QStringLiteral("preset"), QStringLiteral("veryfast")},
					{QStringLiteral("rate_control"), QStringLiteral("CBR")}};
	const QJsonObject simple_after{{QStringLiteral("bitrate"), 4000},
				       {QStringLiteral("preset"), QStringLiteral("fast")},
				       {QStringLiteral("rate_control"), QStringLiteral("CBR")}};
	const auto simple = BuildSimplePlan(simple_config, simple_before, simple_after, QStringLiteral("x264"), false);
	require(simple.mode == OutputMode::Simple && simple.applicable() && simple.changes.size() == 2,
		"simple settings did not produce a two-key preview in Simple mode");
	require(simple.changes[0].path == QStringLiteral("SimpleOutput.VBitrate") &&
			simple.changes[1].path == QStringLiteral("SimpleOutput.Preset"),
		"simple preview did not map properties to OBS SimpleOutput keys");
	require(simple.updated_settings.value(QStringLiteral("UseAdvanced")) == QJsonValue(false),
		"simple apply plan changed the user's Simple output mode");

	QJsonObject unsupported_after = simple_after;
	unsupported_after.insert(QStringLiteral("lookahead"), true);
	const auto unsupported = BuildSimplePlan(simple_config, simple_before, unsupported_after,
						 QStringLiteral("x264"), false);
	require(!unsupported.applicable() && !unsupported.error.isEmpty(),
		"unpersistable Simple-mode settings were silently offered for Apply");
}

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
	qputenv("QT_SCALE_FACTOR", QByteArrayLiteral("2"));
	QApplication app(argc, argv);
	try {
		require(QGuiApplication::platformName() == QStringLiteral("offscreen"),
			"Qt did not select the offscreen platform");

		const auto clean_windows = run_golden_trace(QStringLiteral("clean.jsonl"));
		const auto congested_windows = run_golden_trace(QStringLiteral("congested_uplink.jsonl"));
		const auto stale_windows = run_golden_trace(QStringLiteral("stale_device_encoder.jsonl"));
		const obs_preflight_window clean = clean_windows.back();
		const obs_preflight_window congested = congested_windows.back();
		const obs_preflight_window stale = stale_windows.back();
		test_apply_plans();
		require(congested_windows.size() > 1, "congested golden trace did not produce chart history");
		require(clean.verdict == OBS_PREFLIGHT_PASS && congested.verdict == OBS_PREFLIGHT_MARGINAL &&
				stale.verdict == OBS_PREFLIGHT_FAIL,
			"golden trace verdicts did not cover pass, marginal and fail");

		PreflightBenchWindowRecordedData recorded;
		recorded.windows = congested_windows;
		recorded.bakeoff_rows = {
			{QStringLiteral("Software encoder"), QStringLiteral("1920 × 1080"), QStringLiteral("60"),
			 clean},
			{QStringLiteral("Hardware encoder"), QStringLiteral("1920 × 1080"), QStringLiteral("60"),
			 congested},
			{QStringLiteral("Fallback encoder"), QStringLiteral("1280 × 720"), QStringLiteral("30"), stale},
		};
		recorded.loupe_pair = make_decoded_loupe_pair();

		PreflightBenchWindow window(recorded);
		window.resize(1440, 1600);
		window.show(); // Qt's offscreen platform keeps this out of the display.
		QApplication::processEvents();

		QLabel *verdict_label = window.findChild<QLabel *>(QStringLiteral("preflight-verdict"));
		QLabel *reason_label = window.findChild<QLabel *>(QStringLiteral("preflight-reason"));
		require(verdict_label && reason_label, "bench verdict widgets are missing");
		require(verdict_label->text() == QStringLiteral("Verdict: Marginal"),
			"bench verdict text does not match the engine result");
		require(reason_label->text() == QString::fromUtf8(congested.reason),
			"bench reason text does not match the engine result");

		auto *table = window.findChild<QTableWidget *>(QStringLiteral("preflight-bakeoff-table"));
		require(table && table->rowCount() == 3, "bake-off table does not show three engine results");
		for (int row = 0; row < table->rowCount(); ++row) {
			require(table->item(row, 5) && table->item(row, 6) && !table->item(row, 6)->text().isEmpty(),
				"bake-off row is missing its verdict or plain-language reason");
		}
		require(table->item(0, 5)->text() == QStringLiteral("Pass") &&
				table->item(1, 5)->text() == QStringLiteral("Marginal") &&
				table->item(2, 5)->text() == QStringLiteral("Fail"),
			"bake-off verdicts do not match the engine results");

		QWidget *loupe_view = window.findChild<QWidget *>(QStringLiteral("preflight-loupe-view"));
		require(loupe_view, "loupe widget is missing");
		QApplication::processEvents();

		const QString output_dir = qEnvironmentVariable("PREFLIGHT_UI_OUTPUT_DIR").isEmpty()
						   ? QCoreApplication::applicationDirPath()
						   : qEnvironmentVariable("PREFLIGHT_UI_OUTPUT_DIR");
		require(QDir().mkpath(output_dir), "could not create PNG output directory");
		save_png(window, QDir(output_dir).filePath(QStringLiteral("preflight-bench.png")));
		table->setParent(nullptr);
		table->setFixedSize(1440, 320);
		table->show(); // Detached only for a complete offscreen table capture.
		QApplication::processEvents();
		save_png(*table, QDir(output_dir).filePath(QStringLiteral("preflight-bakeoff.png")));
		delete table;
		save_png(*loupe_view, QDir(output_dir).filePath(QStringLiteral("preflight-loupe.png")));
	} catch (const std::exception &error) {
		std::cerr << "FAIL test_preflight_ui_render: " << error.what() << '\n';
		return 1;
	}

	std::cout << "PASS test_preflight_ui_render\n";
	return 0;
}
