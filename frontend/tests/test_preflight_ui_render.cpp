#include "../preflight/preflight-bench-window.hpp"
#include "../preflight/preflight-apply-plan.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QTemporaryDir>
#include <QSplitter>
#include <QTabWidget>
#include <utility/OBSProxyStyle.hpp>

#include <util/config-file.h>

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

bool ApplyOBSThemeForRenderTest(QApplication &application, const QStringList &theme_files,
				const QPalette &default_palette, QString *error);

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
	const QString path = QDir(QString::fromUtf8(PREFLIGHT_GOLDEN_DIR)).filePath(name);
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
	using preflight::apply::AppendAdvancedEncoderChanges;
	using preflight::apply::AppendConfigSectionChanges;
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
	require(advanced.changes.size() == 2 &&
			advanced.changes[0].path == QStringLiteral("streamEncoder.json/bitrate") &&
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
	const auto unsupported =
		BuildSimplePlan(simple_config, simple_before, unsupported_after, QStringLiteral("x264"), false);
	require(!unsupported.applicable() && !unsupported.error.isEmpty(),
		"unpersistable Simple-mode settings were silently offered for Apply");
	QJsonObject fractional_bitrate = simple_after;
	fractional_bitrate.insert(QStringLiteral("bitrate"), 4000.5);
	const auto invalid_bitrate =
		BuildSimplePlan(simple_config, simple_before, fractional_bitrate, QStringLiteral("x264"), false);
	require(!invalid_bitrate.applicable() && !invalid_bitrate.error.isEmpty(),
		"a non-integer Simple output bitrate was offered for Apply");
}

static void test_apply_contract_regressions()
{
	using preflight::apply::BuildAdvancedPlan;
	using preflight::apply::BuildSimplePlan;
	using preflight::apply::ReadAdvancedSettings;
	using preflight::apply::TargetIdentity;
	using preflight::apply::WriteAdvancedSettings;
	using preflight::apply::WriteSimpleConfig;

	QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/preflight-apply-Ω-XXXXXX"));
	require(temporary.isValid(), "could not create a Unicode temporary profile path");
	const QString profile_path = temporary.path();
	const TargetIdentity target{QStringLiteral("Préflight profile"), profile_path,
				    QStringLiteral("obs_x264"),          QStringLiteral("x264"),
				    QStringLiteral("obs_x264"),          QStringLiteral("x264")};

	const QString basic_ini = QDir(profile_path).filePath(QStringLiteral("basic.ini"));
	const QByteArray basic_path_utf8 = basic_ini.toUtf8();
	config_t *raw_config = nullptr;
	require(config_open(&raw_config, basic_path_utf8.constData(), CONFIG_OPEN_ALWAYS) == CONFIG_SUCCESS &&
			raw_config,
		"could not open temporary basic.ini");
	std::unique_ptr<config_t, decltype(&config_close)> config(raw_config, config_close);
	config_set_string(config.get(), "Output", "Mode", "Simple");
	config_set_uint(config.get(), "SimpleOutput", "VBitrate", 2500);
	config_set_string(config.get(), "SimpleOutput", "Preset", "veryfast");
	config_set_bool(config.get(), "SimpleOutput", "UseAdvanced", false);
	config_set_string(config.get(), "SimpleOutput", "RecQuality", "High");
	config_set_string(config.get(), "SimpleOutput", "RecEncoder", "x264");
	config_set_string(config.get(), "SimpleOutput", "RecFormat2", "hybrid_mp4");
	require(config_save(config.get()) == CONFIG_SUCCESS, "could not seed temporary basic.ini");

	const QJsonObject simple_saved{{QStringLiteral("VBitrate"), 2500},
				       {QStringLiteral("Preset"), QStringLiteral("veryfast")},
				       {QStringLiteral("UseAdvanced"), false}};
	const QJsonObject simple_initial{{QStringLiteral("bitrate"), 2500},
					 {QStringLiteral("preset"), QStringLiteral("veryfast")}};
	const QJsonObject simple_shown{{QStringLiteral("bitrate"), 4000},
				       {QStringLiteral("preset"), QStringLiteral("fast")}};
	const QJsonObject simple_newer{{QStringLiteral("bitrate"), 5000},
				       {QStringLiteral("preset"), QStringLiteral("fast")}};
	const auto displayed_simple =
		BuildSimplePlan(simple_saved, simple_initial, simple_shown, QStringLiteral("x264"), false, target);
	const auto stale_simple =
		BuildSimplePlan(simple_saved, simple_initial, simple_newer, QStringLiteral("x264"), false, target);
	QString error;
	require(!WriteSimpleConfig(config.get(), displayed_simple, stale_simple, &error),
		"a stale Simple Apply plan was written before its updated diff was reviewed");
	require(config_get_uint(config.get(), "SimpleOutput", "VBitrate") == 2500,
		"refusing a stale Simple plan still changed basic.ini");

	const auto current_simple =
		BuildSimplePlan(simple_saved, simple_initial, simple_shown, QStringLiteral("x264"), false, target);
	require(WriteSimpleConfig(config.get(), displayed_simple, current_simple, &error),
		"the displayed Simple Apply plan did not write to temporary basic.ini");
	config_close(config.release());
	config = nullptr;
	config_t *reopened_basic = nullptr;
	require(config_open(&reopened_basic, basic_path_utf8.constData(), CONFIG_OPEN_EXISTING) == CONFIG_SUCCESS &&
			reopened_basic,
		"could not reopen temporary basic.ini");
	std::unique_ptr<config_t, decltype(&config_close)> applied_config(reopened_basic, config_close);
	require(config_get_uint(applied_config.get(), "SimpleOutput", "VBitrate") == 4000 &&
			std::string(config_get_string(applied_config.get(), "SimpleOutput", "Preset")) == "fast" &&
			!config_get_bool(applied_config.get(), "SimpleOutput", "UseAdvanced") &&
			std::string(config_get_string(applied_config.get(), "SimpleOutput", "RecQuality")) == "High" &&
			std::string(config_get_string(applied_config.get(), "Output", "Mode")) == "Simple",
		"Simple Apply changed an unplanned basic.ini key or changed Output.Mode");

	config_set_uint(applied_config.get(), "Video", "OutputCX", 1920);
	config_set_uint(applied_config.get(), "Video", "OutputCY", 1080);
	config_set_uint(applied_config.get(), "Video", "FPSType", 1);
	config_set_uint(applied_config.get(), "Video", "FPSInt", 60);
	config_set_string(applied_config.get(), "Video", "FPSCommon", "60");
	config_set_uint(applied_config.get(), "Video", "FPSNum", 60);
	config_set_uint(applied_config.get(), "Video", "FPSDen", 1);
	require(config_save(applied_config.get()) == CONFIG_SUCCESS, "could not seed temporary simple Video settings");
	const QJsonObject simple_composite_stream_saved{{QStringLiteral("VBitrate"), 4000},
							{QStringLiteral("Preset"), QStringLiteral("fast")},
							{QStringLiteral("UseAdvanced"), false}};
	const QJsonObject simple_composite_stream_initial{{QStringLiteral("bitrate"), 4000},
							  {QStringLiteral("preset"), QStringLiteral("fast")}};
	const QJsonObject simple_composite_stream_edited{{QStringLiteral("bitrate"), 4500},
							 {QStringLiteral("preset"), QStringLiteral("fast")}};
	auto simple_composite = BuildSimplePlan(simple_composite_stream_saved, simple_composite_stream_initial,
						simple_composite_stream_edited, QStringLiteral("x264"), false, target);
	const QJsonObject simple_recording_saved{{QStringLiteral("RecQuality"), QStringLiteral("High")},
						 {QStringLiteral("RecEncoder"), QStringLiteral("x264")},
						 {QStringLiteral("RecFormat2"), QStringLiteral("hybrid_mp4")}};
	const QJsonObject simple_recording_edited{{QStringLiteral("RecQuality"), QStringLiteral("HQ")},
						  {QStringLiteral("RecEncoder"), QStringLiteral("x264")},
						  {QStringLiteral("RecFormat2"), QStringLiteral("hybrid_mp4")}};
	AppendConfigSectionChanges(simple_composite, QStringLiteral("SimpleOutput"), simple_recording_saved,
				   simple_recording_edited);
	const QJsonObject simple_video_saved{{QStringLiteral("OutputCX"), 1920},
					     {QStringLiteral("OutputCY"), 1080},
					     {QStringLiteral("FPSType"), 1},
					     {QStringLiteral("FPSInt"), 60},
					     {QStringLiteral("FPSCommon"), QStringLiteral("60")},
					     {QStringLiteral("FPSNum"), 60},
					     {QStringLiteral("FPSDen"), 1}};
	const QJsonObject simple_video_edited{{QStringLiteral("OutputCX"), 1280},
					      {QStringLiteral("OutputCY"), 720},
					      {QStringLiteral("FPSType"), 1},
					      {QStringLiteral("FPSInt"), 30},
					      {QStringLiteral("FPSCommon"), QStringLiteral("60")},
					      {QStringLiteral("FPSNum"), 60},
					      {QStringLiteral("FPSDen"), 1}};
	AppendConfigSectionChanges(simple_composite, QStringLiteral("Video"), simple_video_saved, simple_video_edited);
	require(WriteSimpleConfig(applied_config.get(), simple_composite, simple_composite, &error),
		"Simple Apply did not write the recording quality and Video plan to basic.ini");
	config_close(applied_config.release());
	reopened_basic = nullptr;
	require(config_open(&reopened_basic, basic_path_utf8.constData(), CONFIG_OPEN_EXISTING) == CONFIG_SUCCESS &&
			reopened_basic,
		"could not reopen basic.ini after the composite Simple Apply");
	applied_config.reset(reopened_basic);
	require(std::string(config_get_string(applied_config.get(), "SimpleOutput", "RecQuality")) == "HQ" &&
			std::string(config_get_string(applied_config.get(), "SimpleOutput", "RecEncoder")) == "x264" &&
			std::string(config_get_string(applied_config.get(), "SimpleOutput", "RecFormat2")) ==
				"hybrid_mp4" &&
			config_get_uint(applied_config.get(), "Video", "OutputCX") == 1280 &&
			config_get_uint(applied_config.get(), "Video", "OutputCY") == 720 &&
			config_get_uint(applied_config.get(), "Video", "FPSInt") == 30 &&
			std::string(config_get_string(applied_config.get(), "Output", "Mode")) == "Simple",
		"Simple composite Apply changed unplanned keys or lost the output mode");

	const QString stream_path = QDir(profile_path).filePath(QStringLiteral("streamEncoder.json"));
	const QString record_path = QDir(profile_path).filePath(QStringLiteral("recordEncoder.json"));
	const QJsonObject advanced_saved{{QStringLiteral("bitrate"), 6000},
					 {QStringLiteral("preset"), QStringLiteral("veryfast")},
					 {QStringLiteral("profile"), QStringLiteral("high")},
					 {QStringLiteral("lookahead"), false}};
	const QJsonObject advanced_initial{{QStringLiteral("bitrate"), 6000},
					   {QStringLiteral("preset"), QStringLiteral("veryfast")},
					   {QStringLiteral("profile"), QStringLiteral("high")},
					   {QStringLiteral("lookahead"), false}};
	const QJsonObject advanced_edited{{QStringLiteral("bitrate"), 4500},
					  {QStringLiteral("preset"), QStringLiteral("fast")},
					  {QStringLiteral("profile"), QStringLiteral("high")},
					  {QStringLiteral("lookahead"), false}};
	auto write_json = [](const QString &path, const QJsonObject &object) {
		QFile file(path);
		require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
			"could not create temporary encoder JSON");
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.close();
	};
	auto read_json = [](const QString &path) {
		QFile file(path);
		require(file.open(QIODevice::ReadOnly), "could not read temporary encoder JSON");
		return QJsonDocument::fromJson(file.readAll()).object();
	};
	auto write_bytes = [](const QString &path, const QByteArray &bytes) {
		QFile file(path);
		require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not create temporary file bytes");
		require(file.write(bytes) == bytes.size(), "could not write temporary file bytes");
	};
	auto read_bytes = [](const QString &path) {
		QFile file(path);
		require(file.open(QIODevice::ReadOnly), "could not read temporary file bytes");
		return file.readAll();
	};
	const QJsonObject record_saved{{QStringLiteral("recording"), QStringLiteral("untouched")}};
	write_json(stream_path, advanced_saved);
	write_json(record_path, record_saved);
	config_set_string(applied_config.get(), "Output", "Mode", "Advanced");
	require(config_save(applied_config.get()) == CONFIG_SUCCESS,
		"could not prepare the Advanced output-mode persistence check");
	const auto displayed_advanced = BuildAdvancedPlan(advanced_saved, advanced_initial, advanced_edited, target);
	require(WriteAdvancedSettings(displayed_advanced, displayed_advanced, &error),
		"the displayed Advanced Apply plan did not write to temporary streamEncoder.json");
	const QJsonObject expected_advanced{{QStringLiteral("bitrate"), 4500},
					    {QStringLiteral("preset"), QStringLiteral("fast")},
					    {QStringLiteral("profile"), QStringLiteral("high")},
					    {QStringLiteral("lookahead"), false}};
	require(read_json(stream_path) == expected_advanced && read_json(record_path) == record_saved,
		"Advanced Apply lost untouched settings or modified recordEncoder.json");
	applied_config.reset();
	reopened_basic = nullptr;
	require(config_open(&reopened_basic, basic_path_utf8.constData(), CONFIG_OPEN_EXISTING) == CONFIG_SUCCESS &&
			reopened_basic,
		"could not reopen basic.ini for the Advanced output-mode check");
	applied_config.reset(reopened_basic);
	require(std::string(config_get_string(applied_config.get(), "Output", "Mode")) == "Advanced",
		"Advanced mode did not persist in basic.ini");

	const QJsonObject record_composite_saved{{QStringLiteral("crf"), 20},
						 {QStringLiteral("profile"), QStringLiteral("high")},
						 {QStringLiteral("untouched"), QStringLiteral("keep")}};
	const QJsonObject record_composite_edited{{QStringLiteral("crf"), 18},
						  {QStringLiteral("profile"), QStringLiteral("high")},
						  {QStringLiteral("untouched"), QStringLiteral("keep")}};
	const QJsonObject advanced_video_saved{{QStringLiteral("OutputCX"), 1920}, {QStringLiteral("OutputCY"), 1080},
					       {QStringLiteral("FPSType"), 1},     {QStringLiteral("FPSInt"), 60},
					       {QStringLiteral("FPSNum"), 60},     {QStringLiteral("FPSDen"), 1}};
	const QJsonObject advanced_video_edited{{QStringLiteral("OutputCX"), 1280}, {QStringLiteral("OutputCY"), 720},
						{QStringLiteral("FPSType"), 2},     {QStringLiteral("FPSInt"), 60},
						{QStringLiteral("FPSNum"), 30000},  {QStringLiteral("FPSDen"), 1001}};
	write_json(stream_path, advanced_saved);
	write_json(record_path, record_composite_saved);
	config_set_uint(applied_config.get(), "Video", "OutputCX", 1920);
	config_set_uint(applied_config.get(), "Video", "OutputCY", 1080);
	config_set_uint(applied_config.get(), "Video", "FPSType", 1);
	config_set_uint(applied_config.get(), "Video", "FPSInt", 60);
	config_set_uint(applied_config.get(), "Video", "FPSNum", 60);
	config_set_uint(applied_config.get(), "Video", "FPSDen", 1);
	require(config_save(applied_config.get()) == CONFIG_SUCCESS,
		"could not seed temporary Advanced Video settings");
	auto composite_advanced = BuildAdvancedPlan(advanced_saved, advanced_initial, advanced_edited, target);
	AppendAdvancedEncoderChanges(composite_advanced, QStringLiteral("recordEncoder.json"), record_composite_saved,
				     record_composite_saved, record_composite_edited);
	AppendConfigSectionChanges(composite_advanced, QStringLiteral("Video"), advanced_video_saved,
				   advanced_video_edited);
	require(WriteAdvancedSettings(applied_config.get(), composite_advanced, composite_advanced, &error),
		"Advanced Apply did not write stream, recording and Video settings together");
	const QJsonObject expected_record_composite{{QStringLiteral("crf"), 18},
						    {QStringLiteral("profile"), QStringLiteral("high")},
						    {QStringLiteral("untouched"), QStringLiteral("keep")}};
	require(read_json(stream_path) == expected_advanced && read_json(record_path) == expected_record_composite &&
			config_get_uint(applied_config.get(), "Video", "OutputCX") == 1280 &&
			config_get_uint(applied_config.get(), "Video", "FPSNum") == 30000 &&
			config_get_uint(applied_config.get(), "Video", "FPSDen") == 1001 &&
			std::string(config_get_string(applied_config.get(), "Output", "Mode")) == "Advanced",
		"Advanced composite Apply lost encoder keys, Video values or output mode");

	TargetIdentity changed_profile = target;
	changed_profile.profile_name = QStringLiteral("Other profile");
	changed_profile.profile_path = QDir(profile_path).filePath(QStringLiteral("autre-配置"));
	QDir().mkpath(changed_profile.profile_path);
	const QString other_stream_path =
		QDir(changed_profile.profile_path).filePath(QStringLiteral("streamEncoder.json"));
	write_json(other_stream_path, advanced_saved);
	const auto profile_changed_plan =
		BuildAdvancedPlan(advanced_saved, advanced_initial, advanced_edited, changed_profile);
	require(!WriteAdvancedSettings(displayed_advanced, profile_changed_plan, &error) &&
			read_json(other_stream_path) == advanced_saved,
		"Apply wrote settings after the active profile changed");

	TargetIdentity changed_encoder = target;
	changed_encoder.encoder_id = QStringLiteral("obs_nvenc_h264_tex");
	write_json(stream_path, advanced_saved);
	const auto encoder_changed_plan =
		BuildAdvancedPlan(advanced_saved, advanced_initial, advanced_edited, changed_encoder);
	require(!WriteAdvancedSettings(displayed_advanced, encoder_changed_plan, &error) &&
			read_json(stream_path) == advanced_saved,
		"Apply wrote settings after the configured encoder changed");
	TargetIdentity changed_recording_encoder = target;
	changed_recording_encoder.recording_encoder_id = QStringLiteral("obs_nvenc_h264_tex");
	write_json(record_path, record_composite_saved);
	const auto recording_encoder_changed_plan =
		BuildAdvancedPlan(advanced_saved, advanced_initial, advanced_edited, changed_recording_encoder);
	require(!WriteAdvancedSettings(displayed_advanced, recording_encoder_changed_plan, &error) &&
			read_json(stream_path) == advanced_saved && read_json(record_path) == record_composite_saved,
		"Apply wrote settings after the configured recording encoder changed");

	const QString corrupt_stream_path = QDir(profile_path).filePath(QStringLiteral("streamEncoder.json"));
	const QString corrupt_backup_path = corrupt_stream_path + QStringLiteral(".bak");
	const QByteArray corrupt_stream_bytes = QByteArrayLiteral("{corrupt stream settings\n");
	const QByteArray backup_stream_bytes = QByteArrayLiteral("{\"bitrate\":1234}\n");
	write_bytes(corrupt_stream_path, corrupt_stream_bytes);
	write_bytes(corrupt_backup_path, backup_stream_bytes);
	QJsonObject corrupt_preview;
	const bool corrupt_preview_ok = ReadAdvancedSettings(corrupt_stream_path, &corrupt_preview, &error);
	require(!corrupt_preview_ok && read_bytes(corrupt_stream_path) == corrupt_stream_bytes &&
			read_bytes(corrupt_backup_path) == backup_stream_bytes,
		"preview repaired a corrupt streamEncoder.json from its backup or changed either file");

	QTemporaryDir rollback_root;
	require(rollback_root.isValid(), "could not create temporary rollback profile root");
	const QString rollback_profile = QDir(rollback_root.path()).filePath(QStringLiteral("profile"));
	require(QDir().mkpath(rollback_profile), "could not create temporary rollback profile");
	const QString rollback_ini = QDir(rollback_profile).filePath(QStringLiteral("basic.ini"));
	const QByteArray rollback_ini_utf8 = rollback_ini.toUtf8();
	config_t *rollback_raw = nullptr;
	require(config_open(&rollback_raw, rollback_ini_utf8.constData(), CONFIG_OPEN_ALWAYS) == CONFIG_SUCCESS &&
			rollback_raw,
		"could not open temporary rollback basic.ini");
	std::unique_ptr<config_t, decltype(&config_close)> rollback_config(rollback_raw, config_close);
	config_set_string(rollback_config.get(), "Output", "Mode", "Simple");
	config_set_uint(rollback_config.get(), "SimpleOutput", "VBitrate", 2500);
	config_set_bool(rollback_config.get(), "SimpleOutput", "UseAdvanced", false);
	require(config_save(rollback_config.get()) == CONFIG_SUCCESS, "could not seed temporary rollback basic.ini");
	const QJsonObject rollback_saved{{QStringLiteral("VBitrate"), 2500}, {QStringLiteral("UseAdvanced"), false}};
	const QJsonObject rollback_initial{{QStringLiteral("bitrate"), 2500}};
	const QJsonObject rollback_edited{{QStringLiteral("bitrate"), 2500},
					  {QStringLiteral("preset"), QStringLiteral("fast")}};
	const auto rollback_plan = BuildSimplePlan(rollback_saved, rollback_initial, rollback_edited,
						   QStringLiteral("x264"), false, target);
	require(rollback_plan.applicable() && rollback_plan.changes.size() == 1 &&
			rollback_plan.changes.front().before.isUndefined(),
		"rollback fixture did not plan a previously unset Simple key");
	require(QDir(rollback_profile).removeRecursively(), "could not remove profile to force save failure");
	error.clear();
	require(!WriteSimpleConfig(rollback_config.get(), rollback_plan, rollback_plan, &error) &&
			!config_has_user_value(rollback_config.get(), "SimpleOutput", "Preset") &&
			error.contains(QStringLiteral("rollback"), Qt::CaseInsensitive),
		"failed Simple save did not remove an originally unset key and report the failed rollback save");
}

static void test_apply_ui_contracts()
{
	using preflight::apply::BuildAdvancedPlan;
	using preflight::apply::BuildSimplePlan;

	QTemporaryDir temporary;
	require(temporary.isValid(), "could not create temporary no-Apply profile");
	const QString basic_path = QDir(temporary.path()).filePath(QStringLiteral("basic.ini"));
	const QString stream_path = QDir(temporary.path()).filePath(QStringLiteral("streamEncoder.json"));
	auto write_bytes = [](const QString &path, const QByteArray &bytes) {
		QFile file(path);
		require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed no-Apply config file");
		require(file.write(bytes) == bytes.size(), "could not seed complete no-Apply config bytes");
	};
	auto read_bytes = [](const QString &path) {
		QFile file(path);
		require(file.open(QIODevice::ReadOnly), "could not read no-Apply config file");
		return file.readAll();
	};
	const QByteArray basic_bytes = QByteArrayLiteral("[Output]\nMode=Advanced\n");
	const QByteArray stream_bytes = QByteArrayLiteral("{\"bitrate\":6000,\"preset\":\"veryfast\"}\n");
	write_bytes(basic_path, basic_bytes);
	write_bytes(stream_path, stream_bytes);

	const QJsonObject saved{{QStringLiteral("bitrate"), 6000},
				{QStringLiteral("preset"), QStringLiteral("veryfast")}};
	const QJsonObject initial = saved;
	const QJsonObject edited{{QStringLiteral("bitrate"), 4500}, {QStringLiteral("preset"), QStringLiteral("fast")}};
	const auto advanced = BuildAdvancedPlan(saved, initial, edited,
						{QStringLiteral("Profile"), temporary.path(),
						 QStringLiteral("obs_x264"), QStringLiteral("obs_x264")});
	require(advanced.applicable(), "close-without-Apply fixture has no applicable Advanced diff");
	PreflightBenchWindowRecordedData advanced_recorded;
	advanced_recorded.apply_preview = advanced;
	advanced_recorded.enable_recorded_apply_controls_for_test = true;
	{
		PreflightBenchWindow window(advanced_recorded);
		auto *apply_button = window.findChild<QPushButton *>(QStringLiteral("preflight-apply-button"));
		require(apply_button && apply_button->isEnabled(),
			"recorded positive-control Apply button is not enabled for an applicable plan");
		window.show();
		QApplication::processEvents();
		window.close();
		QApplication::processEvents();
	}
	require(read_bytes(basic_path) == basic_bytes && read_bytes(stream_path) == stream_bytes,
		"closing Preflight without Apply changed basic.ini or streamEncoder.json");

	const QJsonObject simple_saved{{QStringLiteral("VBitrate"), 2500}, {QStringLiteral("UseAdvanced"), false}};
	const QJsonObject simple_initial{{QStringLiteral("bitrate"), 2500}, {QStringLiteral("lookahead"), false}};
	const QJsonObject unsupported_edit{{QStringLiteral("bitrate"), 2500}, {QStringLiteral("lookahead"), true}};
	const auto unsupported_simple = BuildSimplePlan(
		simple_saved, simple_initial, unsupported_edit, QStringLiteral("x264"), false,
		{QStringLiteral("Profile"), temporary.path(), QStringLiteral("obs_x264"), QStringLiteral("x264")});
	require(!unsupported_simple.applicable(), "unsupported Simple edit fixture unexpectedly became applicable");
	PreflightBenchWindowRecordedData unsupported_recorded;
	unsupported_recorded.apply_preview = unsupported_simple;
	unsupported_recorded.enable_recorded_apply_controls_for_test = true;
	PreflightBenchWindow unsupported_window(unsupported_recorded);
	auto *apply_button = unsupported_window.findChild<QPushButton *>(QStringLiteral("preflight-apply-button"));
	require(apply_button && !apply_button->isEnabled(),
		"real Apply button is enabled for an unsupported Simple-mode edit");
}

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
	qputenv("QT_SCALE_FACTOR", QByteArrayLiteral("2"));
	QApplication app(argc, argv);
	app.setStyle(new OBSProxyStyle());
	const QPalette default_palette = app.palette();
	try {
		require(QGuiApplication::platformName() == QStringLiteral("offscreen"),
			"Qt did not select the offscreen platform");
		const QString theme_dir = QString::fromUtf8(PREFLIGHT_THEME_DIR);
		QString theme_error;
		require(ApplyOBSThemeForRenderTest(app,
						   {QDir(theme_dir).filePath(QStringLiteral("Yami.obt")),
						    QDir(theme_dir).filePath(QStringLiteral("Yami_Default.ovt"))},
						   default_palette, &theme_error),
			QStringLiteral("could not apply OBS Yami theme: %1").arg(theme_error).toUtf8().constData());

		const auto clean_windows = run_golden_trace(QStringLiteral("clean.jsonl"));
		const auto congested_windows = run_golden_trace(QStringLiteral("congested_uplink.jsonl"));
		const auto stale_windows = run_golden_trace(QStringLiteral("stale_device_encoder.jsonl"));
		const obs_preflight_window clean = clean_windows.back();
		const obs_preflight_window congested = congested_windows.back();
		const obs_preflight_window stale = stale_windows.back();
		test_apply_plans();
		test_apply_contract_regressions();
		test_apply_ui_contracts();
		require(congested_windows.size() > 1, "congested golden trace did not produce chart history");
		require(clean.verdict == OBS_PREFLIGHT_PASS && congested.verdict == OBS_PREFLIGHT_MARGINAL &&
				stale.verdict == OBS_PREFLIGHT_FAIL,
			"golden trace verdicts did not cover pass, marginal and fail");

		PreflightBenchWindowRecordedData recorded;
		recorded.windows = congested_windows;
		recorded.gpu_samples.resize(congested_windows.size(), preflight::gpu::Sample{54.0, 41.0, 28.0, 16.0});
		recorded.bakeoff_rows = {
			{QStringLiteral("Software"), QStringLiteral("1080p"), QStringLiteral("60"), clean},
			{QStringLiteral("Hardware"), QStringLiteral("1080p"), QStringLiteral("60"), congested},
			{QStringLiteral("Fallback"), QStringLiteral("720p"), QStringLiteral("30"), stale},
		};
		recorded.loupe_pair = make_decoded_loupe_pair();
		recorded.recording_loupe_pair = recorded.loupe_pair;
		preflight::bench::ImpactEntry impact;
		impact.id = 1;
		impact.description = "Stream bitrate";
		impact.key = "bitrate";
		impact.old_value = "6000";
		impact.new_value = "4500";
		impact.state = preflight::bench::ImpactState::Complete;
		impact.baseline = preflight::bench::MetricWindow{1, clean, 35.0};
		impact.after = preflight::bench::MetricWindow{2, congested, 54.0};
		impact.deltas = preflight::bench::CalculateImpactDelta(*impact.baseline, *impact.after);
		recorded.impacts.push_back(impact);
		preflight::bench::ImpactEntry settling;
		settling.id = 2;
		settling.description = "Live captions";
		settling.key = "enabled";
		settling.old_value = "off";
		settling.new_value = "on";
		settling.state = preflight::bench::ImpactState::Settling;
		settling.baseline = preflight::bench::MetricWindow{1, clean};
		recorded.impacts.push_back(settling);
		recorded.toggles.push_back(
			{"Live captions (on-device)", "mac_live_captions_filter", true, true, "Applies immediately"});
		recorded.apply_preview = preflight::apply::BuildAdvancedPlan(
			{{QStringLiteral("bitrate"), 6000},
			 {QStringLiteral("preset"), QStringLiteral("veryfast")},
			 {QStringLiteral("profile"), QStringLiteral("high")}},
			{{QStringLiteral("bitrate"), 6000},
			 {QStringLiteral("preset"), QStringLiteral("veryfast")},
			 {QStringLiteral("profile"), QStringLiteral("high")}},
			{{QStringLiteral("bitrate"), 4500},
			 {QStringLiteral("preset"), QStringLiteral("fast")},
			 {QStringLiteral("profile"), QStringLiteral("high")}},
			{QStringLiteral("Demo profile"), QStringLiteral("recorded-profile"), QStringLiteral("obs_x264"),
			 QStringLiteral("obs_x264")});
		preflight::apply::AppendAdvancedEncoderChanges(
			*recorded.apply_preview, QStringLiteral("recordEncoder.json"),
			{{QStringLiteral("crf"), 20}, {QStringLiteral("profile"), QStringLiteral("high")}},
			{{QStringLiteral("crf"), 20}, {QStringLiteral("profile"), QStringLiteral("high")}},
			{{QStringLiteral("crf"), 18}, {QStringLiteral("profile"), QStringLiteral("high")}});
		preflight::apply::AppendConfigSectionChanges(*recorded.apply_preview, QStringLiteral("Video"),
							     {{QStringLiteral("OutputCX"), 1920}},
							     {{QStringLiteral("OutputCX"), 1280}});

		PreflightBenchWindow window(recorded);
		window.resize(1500, 1280);
		window.show(); // Qt's offscreen platform keeps this out of the display.
		QApplication::processEvents();

		QLabel *verdict_label = window.findChild<QLabel *>(QStringLiteral("preflight-verdict"));
		QLabel *reason_label = window.findChild<QLabel *>(QStringLiteral("preflight-reason"));
		require(verdict_label && reason_label, "bench verdict widgets are missing");
		require(verdict_label->text() == QStringLiteral("Marginal"),
			"bench verdict text does not match the engine result");
		require(reason_label->text() == QString::fromStdString(preflight::bench::BuildVerdictReason(congested)),
			"bench reason text is not derived from the measured OBS limits");
		auto *summary = window.findChild<QWidget *>(QStringLiteral("preflight-verdict-summary"));
		auto *one_screen = window.findChild<QSplitter *>(QStringLiteral("preflight-one-screen"));
		auto *settings = window.findChild<QWidget *>(QStringLiteral("preflight-settings"));
		auto *right_side = window.findChild<QWidget *>(QStringLiteral("preflight-right-side"));
		require(summary && one_screen && one_screen->count() == 2 && settings && right_side &&
				window.findChildren<QTabWidget *>().empty(),
			"Preflight must use the single-screen settings and preview layout without tabs");
		auto *captions_toggle = window.findChild<QCheckBox *>(QStringLiteral("preflight-toggle-live-captions"));
		auto *impact_list = window.findChild<QTableWidget *>(QStringLiteral("preflight-impact-list"));
		require(captions_toggle && captions_toggle->isEnabled() && captions_toggle->isChecked(),
			"recorded live captions toggle is not enabled and on");
		require(impact_list && impact_list->columnCount() == 5 && impact_list->rowCount() == 2 &&
				impact_list->horizontalHeaderItem(0)->text() == QStringLiteral("Change") &&
				impact_list->horizontalHeaderItem(1)->text() == QStringLiteral("Before") &&
				impact_list->horizontalHeaderItem(2)->text() == QStringLiteral("After") &&
				impact_list->horizontalHeaderItem(3)->text() == QStringLiteral("Δ") &&
				impact_list->horizontalHeaderItem(4)->text() == QStringLiteral("State"),
			"impact list is missing its Change/Before/After/Delta/State columns");
		require(impact_list->item(0, 0)->text() == QStringLiteral("Stream bitrate 6000 → 4500") &&
				impact_list->item(0, 1)->text() == QStringLiteral("Pass · CPU 55.0% · GPU 35.0%") &&
				impact_list->item(0, 2)->text() == QStringLiteral("Marginal · CPU 70.0% · GPU 54.0%") &&
				impact_list->item(0, 3)->text().contains(QStringLiteral("CPU +15.0 pp")) &&
				impact_list->item(0, 3)->text().contains(QStringLiteral("GPU +19.0 pp")) &&
				impact_list->item(0, 3)->text().contains(QStringLiteral("Drop +3.33 pp")) &&
				impact_list->item(0, 4)->text() == QStringLiteral("Complete") &&
				impact_list->item(1, 2)->text() == QStringLiteral("Settling") &&
				impact_list->item(1, 4)->text() == QStringLiteral("Settling") &&
				impact_list->rowHeight(0) >= 90 && impact_list->rowHeight(1) >= 90,
			"impact rows are missing their before/after values, deltas or settling state");

		auto *metric_table = window.findChild<QTableWidget *>(QStringLiteral("preflight-metric-table"));
		require(metric_table && metric_table->rowCount() == 9 && metric_table->columnCount() == 4 &&
				metric_table->horizontalHeaderItem(0)->text() == QStringLiteral("Metric") &&
				metric_table->horizontalHeaderItem(1)->text() == QStringLiteral("Value") &&
				metric_table->horizontalHeaderItem(2)->text() == QStringLiteral("Limit") &&
				metric_table->horizontalHeaderItem(3)->text() == QStringLiteral("Status"),
			"Live bench is missing the compact Stats-style metric table");
		for (int row = 0; row < metric_table->rowCount(); ++row) {
			require(metric_table->item(row, 0) && metric_table->cellWidget(row, 1) &&
					metric_table->cellWidget(row, 2) && metric_table->cellWidget(row, 3),
				"metric row is missing its value, limit or status");
		}
		auto *combined_chart = window.findChild<QWidget *>(QStringLiteral("preflight-combined-chart"));
		require(combined_chart && combined_chart->isVisible() && combined_chart->minimumHeight() >= 220 &&
				combined_chart->property("preflightStartsAtZero").toBool(),
			"combined normalized chart is missing or too small");
		combined_chart->repaint();
		QStringList axis = combined_chart->property("preflightYAxisLabels").toStringList();
		require(axis.size() == 5 && axis.front() == QStringLiteral("120") &&
				axis.back() == QStringLiteral("0") &&
				std::all_of(axis.begin(), axis.end(),
					    [](const QString &label) { return !label.contains('.'); }) &&
				combined_chart->property("preflightYAxisMaximum").toDouble() == 120.0 &&
				combined_chart->property("preflightYAxisTitle").toString() ==
					QStringLiteral("% of limit"),
			"combined chart axis must use a 120% top for data below that limit");
		require(combined_chart->property("preflightSeriesNames").toStringList() ==
					QStringList({QStringLiteral("CPU"), QStringLiteral("GPU"),
						     QStringLiteral("Render"), QStringLiteral("Missed"),
						     QStringLiteral("Skipped"), QStringLiteral("Dropped"),
						     QStringLiteral("Congestion")}) &&
				combined_chart->property("preflightSeriesColors").toStringList() ==
					QStringList({QStringLiteral("#56B4E9"), QStringLiteral("#0072B2"),
						     QStringLiteral("#E69F00"), QStringLiteral("#CC79A7"),
						     QStringLiteral("#F0E442"), QStringLiteral("#D55E00"),
						     QStringLiteral("#009E73")}) &&
				combined_chart->property("preflightLatestValues").toStringList() ==
					QStringList({QStringLiteral("70.00"), QStringLiteral("54.00"),
						     QStringLiteral("42.00"), QStringLiteral("0.00"),
						     QStringLiteral("0.00"), QStringLiteral("66.67"),
						     QStringLiteral("52.50")}),
			"combined chart series do not match their OBS limits and Okabe-Ito colors");
		require(combined_chart->property("preflightLimitLine").toBool() &&
				combined_chart->property("preflightLimitLabel").toString() == QStringLiteral("Limit"),
			"combined chart is missing its OBS limit line");
		auto *gpu_value = window.findChild<QLabel *>(QStringLiteral("preflight-metric-value-1"));
		require(gpu_value && gpu_value->text() == QStringLiteral("54.0 %"),
			"GPU row did not show the recorded utilization sample");
		require(metric_table->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Stretch &&
				metric_table->horizontalHeader()->sectionResizeMode(3) ==
					QHeaderView::ResizeToContents &&
				metric_table->columnWidth(3) < metric_table->width() / 2,
			"Live stats Status column consumed the spare table width");
		QMetaObject::invokeMethod(metric_table, "cellClicked", Qt::DirectConnection, Q_ARG(int, 1),
					  Q_ARG(int, 0));
		QApplication::processEvents();
		require(combined_chart->property("preflightEmphasizedRow").toInt() == 1,
			"clicking the GPU legend row did not emphasize its series");
		QMetaObject::invokeMethod(metric_table, "cellClicked", Qt::DirectConnection, Q_ARG(int, 1),
					  Q_ARG(int, 0));
		QApplication::processEvents();
		require(combined_chart->property("preflightEmphasizedRow").toInt() == -1,
			"clicking the emphasized row again did not restore all series");

		obs_preflight_window over_limit{};
		over_limit.end_ms = 1000;
		over_limit.frame_interval_ns = 16'666'667;
		over_limit.average_frame_time_ns = 8'000'000;
		over_limit.encoder_frames = 100;
		over_limit.output_drop_percent = 6.1;
		over_limit.verdict = OBS_PREFLIGHT_FAIL;
		PreflightBenchWindowRecordedData high_data;
		high_data.apply_preview = recorded.apply_preview;
		high_data.windows.push_back(over_limit);
		PreflightBenchWindow high_window(high_data);
		high_window.resize(1500, 1280);
		high_window.show();
		QApplication::processEvents();
		auto *high_chart = high_window.findChild<QWidget *>(QStringLiteral("preflight-combined-chart"));
		require(high_chart, "over-limit fixture did not build its combined chart");
		high_chart->repaint();
		require(high_chart->property("preflightYAxisMaximum").toDouble() == 150.0,
			"combined chart did not round a 122% data maximum to the next clean 150% tick");
		high_window.close();

		auto *table = window.findChild<QTableWidget *>(QStringLiteral("preflight-bakeoff-table"));
		require(table && table->rowCount() == 3 && table->columnCount() == 3,
			"bake-off table does not show compact candidate results");
		require(table->columnWidth(2) >= table->cellWidget(0, 2)->sizeHint().width(),
			"bake-off action column clips the Use these button");
		require(table->wordWrap() &&
				table->verticalHeader()->sectionResizeMode(0) == QHeaderView::ResizeToContents,
			"bake-off reasons do not wrap into compact content-sized rows");
		require(table->rowHeight(0) >= 48,
			"bake-off rows do not reserve enough height for candidate details and the result reason");
		const int bakeoff_content_height = table->horizontalHeader()->sizeHint().height() +
						   table->verticalHeader()->length() + table->frameWidth() * 2;
		require(table->height() <= bakeoff_content_height + 4,
			"bake-off table has empty space below its visible rows");
		auto *bakeoff_group = window.findChild<QWidget *>(QStringLiteral("preflight-bakeoff-group"));
		require(bakeoff_group && bakeoff_group->height() - table->geometry().bottom() <= 24,
			"inline bake-off leaves empty space below its table");
		for (int row = 0; row < table->rowCount(); ++row) {
			const QString expected_reason =
				row == 0 ? QStringLiteral("All OBS limits met")
					 : QString::fromStdString(
						   preflight::bench::BuildVerdictReason(row == 1 ? congested : stale));
			require(table->item(row, 1) &&
					table->item(row, 1)->text().contains(row == 0   ? QStringLiteral("Pass")
									     : row == 1 ? QStringLiteral("Marginal")
											: QStringLiteral("Fail")) &&
					table->item(row, 1)->text().contains(expected_reason) &&
					qobject_cast<QPushButton *>(table->cellWidget(row, 2)) &&
					qobject_cast<QPushButton *>(table->cellWidget(row, 2))->text() ==
						QStringLiteral("Use these"),
				"bake-off row is missing its metrics, reason or Use these action");
		}
		QWidget *loupe_view = window.findChild<QWidget *>(QStringLiteral("preflight-loupe-view"));
		require(loupe_view && window.findChild<QWidget *>(QStringLiteral("preflight-live-preview")),
			"encoded preview or loupe widget is missing");
		auto *apply_group = window.findChild<QWidget *>(QStringLiteral("preflight-apply-preview"));
		auto *apply_table = window.findChild<QTableWidget *>(QStringLiteral("preflight-apply-diff"));
		require(apply_group && apply_table && apply_table->rowCount() == 4 &&
				apply_table->item(0, 0)->text() == QStringLiteral("streamEncoder.json/bitrate") &&
				apply_table->item(1, 2)->text() == QStringLiteral("fast") &&
				apply_table->item(2, 0)->text() == QStringLiteral("recordEncoder.json/crf") &&
				apply_table->item(3, 0)->text() == QStringLiteral("Video.OutputCX"),
			"recorded Apply preview does not show the exact stream, recording and Video diff");
		const int apply_content_height = apply_table->horizontalHeader()->sizeHint().height() +
						 apply_table->verticalHeader()->length() +
						 apply_table->frameWidth() * 2;
		require(apply_table->height() <= apply_content_height + 4,
			"Apply preview has empty space below its visible diff rows");

		const QString output_dir = qEnvironmentVariable("PREFLIGHT_UI_OUTPUT_DIR").isEmpty()
						   ? QCoreApplication::applicationDirPath()
						   : qEnvironmentVariable("PREFLIGHT_UI_OUTPUT_DIR");
		require(QDir().mkpath(output_dir), "could not create PNG output directory");
		const QString yami_screenshot = QStringLiteral("preflight-yami-emphasized-2x.png");
		const QString yami_apply_screenshot = QStringLiteral("preflight-yami-apply-state-2x.png");
		const QString system_screenshot = QStringLiteral("preflight-system-one-screen-2x.png");
		QMetaObject::invokeMethod(metric_table, "cellClicked", Qt::DirectConnection, Q_ARG(int, 0),
					  Q_ARG(int, 0));
		QApplication::processEvents();
		save_png(window, QDir(output_dir).filePath(yami_screenshot));
		auto *settings_scroll = window.findChild<QScrollArea *>(QStringLiteral("preflight-settings-scroll"));
		require(settings_scroll && apply_group && apply_group->isVisible(),
			"settings scroll area or Apply preview is missing");
		QApplication::processEvents();
		save_png(window, QDir(output_dir).filePath(yami_apply_screenshot));

		require(ApplyOBSThemeForRenderTest(app, {QDir(theme_dir).filePath(QStringLiteral("System.obt"))},
						   default_palette, &theme_error),
			QStringLiteral("could not apply OBS System theme: %1").arg(theme_error).toUtf8().constData());
		settings_scroll->verticalScrollBar()->setValue(0);
		QApplication::processEvents();
		save_png(window, QDir(output_dir).filePath(system_screenshot));

		require(ApplyOBSThemeForRenderTest(app,
						   {QDir(theme_dir).filePath(QStringLiteral("Yami.obt")),
						    QDir(theme_dir).filePath(QStringLiteral("Yami_Default.ovt"))},
						   default_palette, &theme_error),
			QStringLiteral("could not restore OBS Yami theme: %1").arg(theme_error).toUtf8().constData());
		PreflightBenchWindowRecordedData failed = recorded;
		failed.windows = stale_windows;
		failed.gpu_samples.resize(stale_windows.size(), preflight::gpu::Sample{54.0, 41.0, 28.0, 16.0});
		PreflightBenchWindow fail_window(failed);
		fail_window.resize(1500, 1280);
		fail_window.show();
		QApplication::processEvents();
		auto *fail_verdict = fail_window.findChild<QLabel *>(QStringLiteral("preflight-verdict"));
		auto *fail_reason = fail_window.findChild<QLabel *>(QStringLiteral("preflight-reason"));
		require(fail_verdict && fail_verdict->text() == QStringLiteral("Fail") && fail_reason &&
				fail_reason->text() == QStringLiteral("Encoder: 0 frames in window"),
			"stale-encoder golden trace did not render its deterministic Fail reason");
		save_png(fail_window, QDir(output_dir).filePath(QStringLiteral("preflight-yami-fail-state-2x.png")));

	} catch (const std::exception &error) {
		std::cerr << "FAIL test_preflight_ui_render: " << error.what() << '\n';
		return 1;
	}

	std::cout << "PASS test_preflight_ui_render\n";
	return 0;
}
