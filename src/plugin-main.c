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

struct filter_context {
	obs_source_t *source;
	obs_hotkey_id recognize_hotkey_id;
	gs_texrender_t *texrender;
	struct pokemon_detector_sv_context *detector_context;
	obs_data_t *settings;
};

static void recognize_hotkey(
	void *data,
	obs_hotkey_id id,
	obs_hotkey_t *hotkey,
	bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed) return;

	blog(LOG_INFO, "hotkey");

	struct filter_context *context = (struct filter_context*)data;
	obs_source_t *parent = obs_filter_get_parent(context->source);
	const uint32_t width = obs_source_get_width(parent);
	const uint32_t height = obs_source_get_height(parent);

	obs_enter_graphics();
	context->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	gs_texrender_reset(context->texrender);
	blog(LOG_INFO, "hotkey2");
	if (!gs_texrender_begin(context->texrender, width, height)) {
		obs_leave_graphics();
		return;
	}

	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	blog(LOG_INFO, "hotkey3");
	obs_source_video_render(parent);
	gs_texrender_end(context->texrender);

	gs_stagesurf_t *stagesurface = gs_stagesurface_create(
			width, height, GS_BGRA);;
	gs_stage_texture(stagesurface, gs_texrender_get_texture(context->texrender));
	uint8_t *video_data;
	uint32_t linesize;

	blog(LOG_INFO, "hotkey4");
	if (!gs_stagesurface_map(stagesurface, &video_data, &linesize)) {
		return;
	}
	blog(LOG_INFO, "hotkey5");

	uint8_t *raw_image = bzalloc(width * height * 4);

	if (video_data && linesize) {
		memcpy(raw_image, video_data, width * height * 4);
        // for (size_t i = 0; i < height; ++i) {
        //     const size_t dst_offset = height * i * 4;
        //     const size_t src_offset = linesize * i;
		// 	printf("%zu\n", dst_offset);
		// 	printf("%zu\n", src_offset);
		// 	printf("%u\n", linesize);
        //     for (size_t j = 0; j < width; ++j) {
		// 		for (size_t k = 0; k < 4; k++) {
		// 			raw_image[width * j + i * 4 + k] = video_data[src_offset + j * 4 + k];
		// 		}
        //     }
        // }
	}
	obs_leave_graphics();

	pokemon_detector_sv_load_screen(context->detector_context, raw_image);
	pokemon_detector_sv_crop_opponent_pokemons(context->detector_context);

	const char *stream_path = obs_data_get_string(context->settings, "stream_path");
	const char *stream_prefix = obs_data_get_string(context->settings, "stream_prefix");

	for (int i = 0; i < 6; i++) {
		char stream_format[512];
		snprintf(stream_format, sizeof(stream_format), "%s-%d", stream_prefix, i + 1);
		char *stream_filename = os_generate_formatted_filename("png", true, stream_format);
		pokemon_detector_sv_export_opponent_pokemon_image(context->detector_context, i, stream_path, stream_filename);
		bfree(stream_filename);
	}

	const char *log_path = obs_data_get_string(context->settings, "log_path");
	const char *log_prefix = obs_data_get_string(context->settings, "log_prefix");
	for (int i = 0; i < 6; i++) {
		char log_format[512];
		snprintf(log_format, sizeof(log_format), "%s-%d", log_prefix, i + 1);
		char *log_filename = os_generate_formatted_filename("png", true, log_format);
		pokemon_detector_sv_export_opponent_pokemon_image(context->detector_context, i, log_path, log_filename);
		bfree(log_filename);
	}
	blog(LOG_INFO, "hotkey9");
	return;
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
	
	context->detector_context = pokemon_detector_sv_create(pokemon_detector_sv_default_config);

	context->settings = settings;
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
