#include "preview-audio-sources.hpp"

static bool collect_item(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto &uuids = *static_cast<std::unordered_set<QString> *>(data);

	if (!obs_sceneitem_visible(item)) {
		return true;
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) {
		return true;
	}

	/* Groups and nested scenes play their sources too. */
	obs_scene_t *inner = obs_group_or_scene_from_source(source);
	if (inner) {
		obs_scene_enum_items(inner, collect_item, data);
		return true;
	}

	uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_AUDIO) == 0) {
		return true;
	}

	if (!obs_source_audio_active(source)) {
		return true;
	}

	auto uuidPointer = obs_source_get_uuid(source);
	if (uuidPointer && *uuidPointer) {
		uuids.insert(QString::fromUtf8(uuidPointer));
	}

	return true;
}

void CollectPreviewAudioSources(obs_scene_t *scene, std::unordered_set<QString> &uuids)
{
	obs_scene_enum_items(scene, collect_item, &uuids);
}
