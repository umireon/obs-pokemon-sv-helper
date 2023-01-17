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
#include <util/platform.h>
#include <obs-frontend-api.h>

#include "plugin-macros.generated.h"

#include <pokemon-detector-sv.h>

enum filter_state {
	STATE_UNKNOWN,
	STATE_ENTERING_SELECT,
	STATE_SELECT,
	STATE_MATCH,
};

struct filter_context {
	obs_source_t *source;
	obs_hotkey_id recognize_hotkey_id;
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurface;
	uint8_t *video_data;
	struct pokemon_detector_sv_context *detector_context;
	obs_data_t *settings;
	bool started;
	uint32_t width, height;
	enum filter_state state;
	uint64_t last_state_change_ns;
};

static void filter_main_render_callback(void *data, uint32_t cx, uint32_t cy) {
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);
	UNUSED_PARAMETER(data);

	struct filter_context *context = (struct filter_context*)data;
	if (!obs_source_enabled(context->source))
		return;
	obs_source_t *parent = obs_filter_get_parent(context->source);
	if (!parent || obs_source_removed(parent))
		return;
	const uint32_t width = obs_source_get_width(parent);
	const uint32_t height = obs_source_get_height(parent);

	if (width == 0 || height == 0) return;

	gs_texrender_reset(context->texrender);
	if (!gs_texrender_begin(context->texrender, width, height)) return;
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	obs_source_video_render(parent);
	gs_texrender_end(context->texrender);

	uint32_t stagesurface_width = gs_stagesurface_get_width(context->stagesurface);
	uint32_t stagesurface_height = gs_stagesurface_get_height(context->stagesurface);
	if (context->stagesurface && (stagesurface_width != width || stagesurface_height != height)) {
		gs_stagesurface_destroy(context->stagesurface);
		context->stagesurface = NULL;
	}
	if (context->stagesurface == NULL) {
		context->stagesurface = gs_stagesurface_create(width, height, GS_BGRA);
	}
	if (context->video_data && (context->width != width || context->height != height)) {
		bfree(context->video_data);
		context->video_data = NULL;
	}
	if (context->video_data == NULL) {
		context->video_data = bzalloc(width * height * 4);
		context->width = width;
		context->height = height;
	}

	gs_stage_texture(context->stagesurface, gs_texrender_get_texture(context->texrender));
	uint8_t *stagesurface_data;
	uint32_t linesize;
	if (!gs_stagesurface_map(context->stagesurface, &stagesurface_data, &linesize)) return;
	if (stagesurface_data && linesize) {
		if (width * 4 == linesize) {
			memcpy(context->video_data, stagesurface_data, width * height * 4);
		}
	}
	gs_stagesurface_unmap(context->stagesurface);

	pokemon_detector_sv_load_screen(context->detector_context, context->video_data);
	context->started = true;
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
	struct filter_context *context = bzalloc(sizeof(struct filter_context));
	context->source = source;

    context->recognize_hotkey_id = OBS_INVALID_HOTKEY_PAIR_ID;

	context->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	context->stagesurface = NULL;
	context->video_data = NULL;

	context->detector_context = pokemon_detector_sv_create(pokemon_detector_sv_default_config);

	context->settings = settings;

	obs_add_main_render_callback(filter_main_render_callback, context);

	return context;
}

static void filter_destroy(void *data) {
	struct filter_context *context = data;
	obs_remove_main_render_callback(filter_main_render_callback, context);
	bfree(context);
}

static void write_stream_files(struct filter_context *context) {
	const char *stream_path = obs_data_get_string(context->settings, "stream_path");
	const char *stream_prefix = obs_data_get_string(context->settings, "stream_prefix");
	for (int i = 0; i < 6; i++) {
		char stream_format[512];
		snprintf(stream_format, sizeof(stream_format), "%s-%d", stream_prefix, i + 1);
		char *stream_filename = os_generate_formatted_filename("png", true, stream_format);
		pokemon_detector_sv_export_opponent_pokemon_image(context->detector_context, i, stream_path, stream_filename);
		bfree(stream_filename);
	}
}

static void write_log_files(struct filter_context *context) {
	const char *log_path = obs_data_get_string(context->settings, "log_path");
	const char *log_prefix = obs_data_get_string(context->settings, "log_prefix");
	for (int i = 0; i < 6; i++) {
		char log_format[512];
		snprintf(log_format, sizeof(log_format), "%s-%d", log_prefix, i + 1);
		char *log_filename = os_generate_formatted_filename("png", true, log_format);
		pokemon_detector_sv_export_opponent_pokemon_image(context->detector_context, i, log_path, log_filename);
		bfree(log_filename);
	}
}

static void filter_video_tick(void *data, float seconds) {
	UNUSED_PARAMETER(seconds);
	struct filter_context *context = data;

	obs_source_t *parent = obs_filter_get_parent(context->source);
	if (!parent) {
		return;
	}

	if (!context->started) return;

	const enum pokemon_detector_sv_scene scene = pokemon_detector_sv_detect_scene(context->detector_context);
	blog(LOG_INFO, "%d %d\n", context->state, scene);
	if (context->state == STATE_UNKNOWN) {
		if (scene == POKEMON_DETECTOR_SV_SCENE_SELECT) {
			context->state = STATE_ENTERING_SELECT;
			context->last_state_change_ns = obs_get_video_frame_time();
		}
	} else if (context->state == STATE_ENTERING_SELECT) {
		const uint64_t frame_time_ns = obs_get_video_frame_time();
		if (frame_time_ns - context->last_state_change_ns > 1000000000) {
			pokemon_detector_sv_crop_opponent_pokemons(context->detector_context);
			write_stream_files(context);
			write_log_files(context);
			context->state = STATE_SELECT;
		}
	} else if (context->state == STATE_SELECT) {
		if (scene == POKEMON_DETECTOR_SV_SCENE_BLACK_TRANSITION) {
			context->state = STATE_UNKNOWN;
		}
	}
}

static obs_properties_t *filter_properties(void *data) {
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *props_stream = obs_properties_create();
	obs_properties_add_path(props_stream, "stream_path", "Output directory", OBS_PATH_DIRECTORY, NULL, NULL);
	obs_properties_add_text(props_stream, "stream_prefix", "Output file name prefix", OBS_TEXT_DEFAULT);
	obs_properties_add_group(props, "stream", "Stream", OBS_GROUP_NORMAL, props_stream);

	obs_properties_t *props_log = obs_properties_create();
	obs_properties_add_path(props_log, "log_path", "Output path for log", OBS_PATH_DIRECTORY, NULL, NULL);
	obs_properties_add_text(props_log, "log_prefix", "Output file name prefix", OBS_TEXT_DEFAULT);
	obs_properties_add_group(props, "log", "Log", OBS_GROUP_NORMAL, props_log);

	return props;
}

static const char *get_frontend_record_path(config_t *config) {
	const char *mode = config_get_string(config, "Output", "Mode");
	const char *type = config_get_string(config, "AdvOut", "RecType");
	const char *adv_path =
		strcmp(type, "Standard") != 0 || strcmp(type, "standard") != 0
			? config_get_string(config, "AdvOut", "FFFilePath")
			: config_get_string(config, "AdvOut", "RecFilePath");
	bool adv_out = strcmp(mode, "Advanced") == 0 ||
		       strcmp(mode, "advanced") == 0;
	return adv_out ? adv_path
			: config_get_string(config, "SimpleOutput", "FilePath");
}

static void filter_defaults(obs_data_t *settings)
{
	config_t *config = obs_frontend_get_profile_config();
	const char *record_path = get_frontend_record_path(config);
	obs_data_set_default_string(settings, "stream_path", record_path);
	obs_data_set_default_string(settings, "stream_prefix", "OpponentPokemon");

	obs_data_set_default_string(settings, "log_path", record_path);
	obs_data_set_default_string(settings, "log_prefix", config_get_string(config, "Output", "FilenameFormatting"));
}

struct obs_source_info filter_info = {
	.id = "obs-pokemon-sv-plugin",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_get_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.video_render = filter_video_render,
	.video_tick = filter_video_tick,
	.get_properties = filter_properties,
	.get_defaults = filter_defaults,
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
