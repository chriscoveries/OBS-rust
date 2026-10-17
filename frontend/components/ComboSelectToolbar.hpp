#pragma once

#include "SourceToolbar.hpp"

#include <string>

class QComboBox;

int FillPropertyCombo(QComboBox *c, obs_property_t *p, const std::string &cur_id, bool is_int = false);

class Ui_DeviceSelectToolbar;

class ComboSelectToolbar : public SourceToolbar {
	Q_OBJECT

protected:
	std::unique_ptr<Ui_DeviceSelectToolbar> ui;
	const char *prop_name;
	bool is_int = false;

public:
	ComboSelectToolbar(QWidget *parent, OBSSource source);
	~ComboSelectToolbar();
	virtual void Init();

public slots:
	void on_device_currentIndexChanged(int idx);
};
