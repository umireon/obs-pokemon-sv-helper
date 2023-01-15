/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <util/config-file.h>
#include <obs-frontend-api.h>

#include "plugin-macros.generated.h"

struct filter_context {
	obs_source_t *source;
	obs_hotkey_id recognize_hotkey_id;
	gs_texrender_t *texrender;
};

static void recognize_hotkey(
	void *data,
	obs_hotkey_id id,
	obs_hotkey_t *hotkey,
	bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed) return;

	blog(LOG_INFO, "hotkey");
	return;
}

static const char *CONFIG_SECTION_NAME = "obs-pokemon-sv-plugin";
static const char *CONFIG_CONTEXT = "context";
static const char *CONTEXT_HOTKEY_RECOGNIZE = "hotkey_recognize";
static obs_hotkey_id recognize_hotkey_id;

static obs_data_t *load_context() {
	config_t *config = obs_frontend_get_global_config();
	config_set_default_string(config, CONFIG_SECTION_NAME, CONFIG_CONTEXT, "{}");
	const char *context_json = config_get_string(config, CONFIG_SECTION_NAME, CONFIG_CONTEXT);

	obs_data_t *context = obs_data_create_from_json(context_json);
	obs_data_array_t *empty_array = obs_data_array_create();
	obs_data_set_default_array(context, CONTEXT_HOTKEY_RECOGNIZE, empty_array);
	return context;
}

static void save_context(obs_data_t *context) {
	config_t *config = obs_frontend_get_global_config();
	const char *context_json = obs_data_get_json(context);
	config_set_string(config, CONFIG_SECTION_NAME, CONFIG_CONTEXT, context_json);
}

static void hotkey_changed(void *data, calldata_t *cd) {
	obs_data_t *context = (obs_data_t*)data;
	obs_hotkey_id *hotkey_id = calldata_ptr(cd, "key");
	if (*hotkey_id == recognize_hotkey_id) {
		obs_data_array_t *array = obs_hotkey_save(*hotkey_id);
		obs_data_set_array(context, "hotkey_recognize", array);
		save_context(context);
	}
}


static const char *filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Pokemon SV Plugin";
}

static void filter_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct filter_context *context = data;
	obs_source_skip_video_filter(context->source);
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct filter_context *context = bzalloc(sizeof(struct filter_context));
	context->source = source;

    context->recognize_hotkey_id = OBS_INVALID_HOTKEY_PAIR_ID;

	context->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	// context->enableHotkey = OBS_INVALID_HOTKEY_PAIR_ID;
	// source_record_filter_update(context, settings);
	// obs_add_main_render_callback(source_record_filter_offscreen_render,
	// 			     context);
	// obs_frontend_add_event_callback(frontend_event, context);
	return context;
}

static void filter_video_tick(void *data, float seconds) {
	UNUSED_PARAMETER(seconds);
	struct filter_context *context = data;

	obs_source_t *parent = obs_filter_get_parent(context->source);
	if (!parent) {
		return;
	}

	if (context->recognize_hotkey_id == OBS_INVALID_HOTKEY_PAIR_ID) {
		context->recognize_hotkey_id = obs_hotkey_register_source(
			parent,
			"obs-pokemon-sv-plugin.recognize",
			"Recognize Team",
			recognize_hotkey,
			context);
	}

	gs_texrender_reset(filter->texrender);

	if (!gs_texrender_begin(filter->texrender, 1920, 1080)) {
		return;
	}
	obs_source_video_render(parent);
	gs_texrender_end(filter->texrender);

}

struct obs_source_info filter_info = {
	.id = "obs-pokemon-sv-plugin",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_get_name,
	.create = filter_create,
	.video_render = filter_video_render,
	.video_tick = filter_video_tick,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "plugin loaded successfully (version %s)",
	     PLUGIN_VERSION);

	obs_register_source(&filter_info);

	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "plugin unloaded");
}
