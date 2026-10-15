#include "preflight-bench-data.hpp"

#include <obs-data.h>

#include <utility>

namespace preflight::bench {

SettingsObject::SettingsObject(obs_data_t *value, bool take_ownership) : value(value)
{
	if (this->value && !take_ownership) {
		obs_data_addref(this->value);
	}
}

SettingsObject SettingsObject::Retain(obs_data_t *value)
{
	return SettingsObject(value, false);
}

SettingsObject SettingsObject::Take(obs_data_t *value)
{
	return SettingsObject(value, true);
}

SettingsObject::SettingsObject(const SettingsObject &other) : SettingsObject(other.value, false) {}

SettingsObject::SettingsObject(SettingsObject &&other) noexcept : value(std::exchange(other.value, nullptr)) {}

SettingsObject &SettingsObject::operator=(SettingsObject other) noexcept
{
	std::swap(value, other.value);
	return *this;
}

SettingsObject::~SettingsObject()
{
	if (value) {
		obs_data_release(value);
	}
}

obs_data_t *SettingsObject::get() const
{
	return value;
}

SettingsObject::operator bool() const
{
	return value != nullptr;
}

} // namespace preflight::bench
