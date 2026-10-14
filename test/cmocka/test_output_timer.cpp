/* Tools -> Output Timer (plugins/frontend-tools/output-timer.cpp) after its
 * timer stops a recording or a stream. The real dialog is built on Qt's
 * offscreen platform (no display); the frontend API is stubbed here and
 * behaves like the frontend: starting an output sends the STARTED event and
 * stopping it sends STOPPING. The timer is set to 1 s with "Enable ... timer
 * every time" checked, so it runs whether the dialog is open or not. */

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
extern "C" {
#include <cmocka.h>
}

#include <obs-frontend-api.h>
#include <obs-module.h>

#include "output-timer.hpp"

extern OutputTimer *ot;
extern "C" void InitOutputTimer();

/* ---- frontend stubs ---------------------------------------------------- */

static QMainWindow *main_window;
static obs_frontend_event_cb event_cb;
static void *event_data;
static bool recording;
static bool streaming;
static int recording_stops;
static int streaming_stops;

static void send(enum obs_frontend_event event)
{
	if (event_cb) {
		event_cb(event, event_data);
	}
}

extern "C" {
const char *obs_module_text(const char *val)
{
	return val;
}

bool obs_module_get_string(const char *, const char **)
{
	return false;
}

void *obs_frontend_get_main_window(void)
{
	return main_window;
}

void *obs_frontend_add_tools_menu_qaction(const char *name)
{
	return new QAction(name, main_window);
}

void obs_frontend_add_event_callback(obs_frontend_event_cb callback, void *private_data)
{
	event_cb = callback;
	event_data = private_data;
}

void obs_frontend_add_save_callback(obs_frontend_save_cb, void *) {}
void obs_frontend_push_ui_translation(obs_frontend_translate_ui_cb) {}
void obs_frontend_pop_ui_translation(void) {}
void obs_frontend_save(void) {}

bool obs_frontend_recording_active(void)
{
	return recording;
}

bool obs_frontend_recording_paused(void)
{
	return false;
}

void obs_frontend_recording_start(void)
{
	recording = true;
	send(OBS_FRONTEND_EVENT_RECORDING_STARTING);
	send(OBS_FRONTEND_EVENT_RECORDING_STARTED);
}

void obs_frontend_recording_stop(void)
{
	recording_stops++;
	send(OBS_FRONTEND_EVENT_RECORDING_STOPPING);
	recording = false;
	send(OBS_FRONTEND_EVENT_RECORDING_STOPPED);
}

bool obs_frontend_streaming_active(void)
{
	return streaming;
}

void obs_frontend_streaming_start(void)
{
	streaming = true;
	send(OBS_FRONTEND_EVENT_STREAMING_STARTING);
	send(OBS_FRONTEND_EVENT_STREAMING_STARTED);
}

void obs_frontend_streaming_stop(void)
{
	streaming_stops++;
	send(OBS_FRONTEND_EVENT_STREAMING_STOPPING);
	streaming = false;
	send(OBS_FRONTEND_EVENT_STREAMING_STOPPED);
}
}

/* ---- helpers ----------------------------------------------------------- */

static void run_events_for(int ms)
{
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
}

/* Runs the event loop until `active` turns false (the timer stopped the
 * output) or 5 s pass. */
static bool wait_until_stopped(const bool &active)
{
	QElapsedTimer t;
	t.start();
	while (active && t.elapsed() < 5000) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
	return !active;
}

static int setup(void **)
{
	recording = streaming = false;
	recording_stops = streaming_stops = 0;

	Ui_OutputTimer *ui = ot->ui.get();
	ui->recordingTimerHours->setValue(0);
	ui->recordingTimerMinutes->setValue(0);
	ui->recordingTimerSeconds->setValue(1);
	ui->streamingTimerHours->setValue(0);
	ui->streamingTimerMinutes->setValue(0);
	ui->streamingTimerSeconds->setValue(1);
	ui->autoStartRecordTimer->setChecked(true);
	ui->autoStartStreamTimer->setChecked(true);
	ot->hide();
	return 0;
}

static int teardown(void **)
{
	if (recording) {
		obs_frontend_recording_stop();
	}
	if (streaming) {
		obs_frontend_streaming_stop();
	}
	ot->hide();
	run_events_for(50);
	return 0;
}

static void assert_button(QPushButton *button, QLabel *time, const char *text, bool checked)
{
	assert_string_equal(button->text().toUtf8().constData(), text);
	assert_int_equal(button->isChecked(), checked);
	assert_string_equal(time->text().toUtf8().constData(), "00:00:00");
}

/* ---- cases ------------------------------------------------------------- */

/* The dialog is closed while the timer runs out. When it is opened again it
 * must offer Start: before the fix it still showed Stop (checked), and
 * clicking that "Stop" started a new recording. */
static void recording_timer_ends_with_dialog_closed(void **)
{
	obs_frontend_recording_start();
	assert_true(wait_until_stopped(recording));
	assert_int_equal(recording_stops, 1);

	ot->show();
	run_events_for(50);
	assert_button(ot->ui->outputTimerRecord, ot->ui->recordTime, "Start", false);
}

static void streaming_timer_ends_with_dialog_closed(void **)
{
	obs_frontend_streaming_start();
	assert_true(wait_until_stopped(streaming));
	assert_int_equal(streaming_stops, 1);

	ot->show();
	run_events_for(50);
	assert_button(ot->ui->outputTimerStream, ot->ui->streamTime, "Start", false);
}

/* Guard: with the dialog open the button already went back to Start. */
static void recording_timer_ends_with_dialog_open(void **)
{
	ot->show();
	obs_frontend_recording_start();
	assert_true(wait_until_stopped(recording));
	run_events_for(50);
	assert_button(ot->ui->outputTimerRecord, ot->ui->recordTime, "Start", false);
}

/* Guard: a recording stopped by hand while the dialog is closed cancels the
 * timer, so it does not stop anything later on. */
static void manual_stop_with_dialog_closed_cancels_timer(void **)
{
	obs_frontend_recording_start();
	run_events_for(200);
	obs_frontend_recording_stop();
	run_events_for(1500);
	assert_int_equal(recording_stops, 1);

	ot->show();
	run_events_for(50);
	assert_button(ot->ui->outputTimerRecord, ot->ui->recordTime, "Start", false);
}

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);

	main_window = new QMainWindow();
	InitOutputTimer();
	if (!ot || !event_cb) {
		fprintf(stderr, "Output Timer did not initialize\n");
		return 1;
	}

	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(recording_timer_ends_with_dialog_closed, setup, teardown),
		cmocka_unit_test_setup_teardown(streaming_timer_ends_with_dialog_closed, setup, teardown),
		cmocka_unit_test_setup_teardown(recording_timer_ends_with_dialog_open, setup, teardown),
		cmocka_unit_test_setup_teardown(manual_stop_with_dialog_closed_cancels_timer, setup, teardown),
	};
	int failed = cmocka_run_group_tests(tests, NULL, NULL);

	delete main_window;
	return failed;
}
