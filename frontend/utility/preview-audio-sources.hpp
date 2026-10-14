#pragma once

#include <obs.h>

#include <QHashFunctions>
#include <QString>

#include <unordered_set>

/* The audio sources the Audio Mixer marks as Preview in Studio Mode: the
 * visible items of `scene` that have audio and whose audio is active,
 * including those inside visible groups and nested scenes. Their UUIDs are
 * added to `uuids`. */
void CollectPreviewAudioSources(obs_scene_t *scene, std::unordered_set<QString> &uuids);
