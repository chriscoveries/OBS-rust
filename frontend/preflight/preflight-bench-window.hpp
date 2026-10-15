#pragma once

#include <QDialog>

#include <memory>

class QCloseEvent;

class PreflightBenchWindow : public QDialog {
	Q_OBJECT

	struct Impl;
	std::unique_ptr<Impl> impl;

public:
	explicit PreflightBenchWindow(QWidget *parent = nullptr);
	~PreflightBenchWindow() override;

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void ToggleBench();
	void Sample();
	void ApplyEncoderSettings();
};
