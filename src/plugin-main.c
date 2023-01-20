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

#include <inttypes.h>

#include <obs-module.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <obs-frontend-api.h>
#include <util/dstr.h>

#include "plugin-macros.generated.h"

#include "pokemon-detector-sv.h"

#define N_POKEMONS 6

enum filter_state {
	STATE_UNKNOWN,
	STATE_ENTERING_SELECT_POKEMON,
	STATE_SELECT_POKEMON,
	STATE_ENTERING_MATCH,
	STATE_MATCH,
	STATE_ENTERING_RESULT,
	STATE_RESULT,
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
	enum pokemon_detector_sv_scene prev_scene;
	uint64_t last_state_change_ns;
	uint64_t match_start_ns;
	uint64_t last_elapsed_seconds;
	int selection_order_indexes[N_POKEMONS];
	uint64_t match_end_ns;
	int my_selection_order_map[N_POKEMONS];
	struct pokemon_detector_sv_matchstate matchstate;
};

static const char PATH_SEPARATOR = '/';

static void filter_main_render_callback(void *data, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(cx);
	UNUSED_PARAMETER(cy);
	UNUSED_PARAMETER(data);

	struct filter_context *context = (struct filter_context *)data;
	if (!obs_source_enabled(context->source))
		return;
	obs_source_t *parent = obs_filter_get_parent(context->source);
	if (!parent || obs_source_removed(parent))
		return;
	const uint32_t width = obs_source_get_width(parent);
	const uint32_t height = obs_source_get_height(parent);

	if (width == 0 || height == 0)
		return;

	gs_texrender_reset(context->texrender);
	if (!gs_texrender_begin(context->texrender, width, height))
		return;
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	obs_source_video_render(parent);
	gs_texrender_end(context->texrender);

	uint32_t stagesurface_width =
		gs_stagesurface_get_width(context->stagesurface);
	uint32_t stagesurface_height =
		gs_stagesurface_get_height(context->stagesurface);
	if (context->stagesurface &&
	    (stagesurface_width != width || stagesurface_height != height)) {
		gs_stagesurface_destroy(context->stagesurface);
		context->stagesurface = NULL;
	}
	if (context->stagesurface == NULL) {
		context->stagesurface =
			gs_stagesurface_create(width, height, GS_BGRA);
	}
	if (context->video_data &&
	    (context->width != width || context->height != height)) {
		bfree(context->video_data);
		context->video_data = NULL;
	}
	if (context->video_data == NULL) {
		context->video_data = bzalloc(width * height * 4);
		context->width = width;
		context->height = height;
	}

	gs_stage_texture(context->stagesurface,
			 gs_texrender_get_texture(context->texrender));
	uint8_t *stagesurface_data;
	uint32_t linesize;
	if (!gs_stagesurface_map(context->stagesurface, &stagesurface_data,
				 &linesize))
		return;
	if (stagesurface_data && linesize) {
		if (width * 4 == linesize) {
			memcpy(context->video_data, stagesurface_data,
			       width * height * 4);
		}
	}
	gs_stagesurface_unmap(context->stagesurface);

	pokemon_detector_sv_load_screen(context->detector_context,
					context->video_data, width, height);
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

	context->detector_context =
		pokemon_detector_sv_create(pokemon_detector_sv_default_config);

	context->settings = settings;

	obs_add_main_render_callback(filter_main_render_callback, context);

	return context;
}

static void filter_destroy(void *data)
{
	struct filter_context *context = data;
	obs_remove_main_render_callback(filter_main_render_callback, context);
	bfree(context);
}

static void write_stream_files(struct filter_context *context)
{
	const char *stream_path =
		obs_data_get_string(context->settings, "stream_path");
	const char *stream_prefix =
		obs_data_get_string(context->settings, "stream_prefix");
	for (int i = 0; i < 6; i++) {
		char stream_format[512];
		snprintf(stream_format, sizeof(stream_format), "%s-%d",
			 stream_prefix, i + 1);
		char *stream_filename = os_generate_formatted_filename(
			"png", true, stream_format);
		pokemon_detector_sv_opponent_pokemon_export_image(
			context->detector_context, i, stream_path,
			stream_filename);
		bfree(stream_filename);
	}
}

static void write_log_files(struct filter_context *context)
{
	const char *log_path =
		obs_data_get_string(context->settings, "log_path");
	const char *log_prefix =
		obs_data_get_string(context->settings, "log_prefix");
	for (int i = 0; i < 6; i++) {
		char log_format[512];
		snprintf(log_format, sizeof(log_format), "%s-%d", log_prefix,
			 i + 1);
		char *log_filename =
			os_generate_formatted_filename("png", true, log_format);
		pokemon_detector_sv_opponent_pokemon_export_image(
			context->detector_context, i, log_path, log_filename);
		bfree(log_filename);

		const char *pokemon_id =
			pokemon_detector_sv_opponent_pokemon_recognize(
				context->detector_context, i);
		context->matchstate.opponent_pokemon_ids[i] = pokemon_id;
		blog(LOG_INFO, "%s\n", pokemon_id);
	}
}

static void flush_match_log(struct filter_context *context)
{
	const char ps = PATH_SEPARATOR;

	const char *log_path =
		obs_data_get_string(context->settings, "log_path");

	char path[512];
	snprintf(path, sizeof(path), "%s%cmatch_log.txt", log_path, ps);
	pokemon_detector_sv_matchstate_append(&context->matchstate, path);
	struct pokemon_detector_sv_matchstate empty_matchstate;
	memset(&empty_matchstate, 0, sizeof(empty_matchstate));
	context->matchstate = empty_matchstate;
}

static bool selection_order_detect_change(struct filter_context *context)
{
	pokemon_detector_sv_my_selection_order_crop(context->detector_context);

	int orders[N_POKEMONS];
	bool change_detected = false;
	for (int i = 0; i < N_POKEMONS; i++) {
		orders[i] = pokemon_detector_sv_my_selection_order_recognize(
			context->detector_context, i);
		if (orders[i] > 0 &&
		    context->my_selection_order_map[orders[i] - 1] != i) {
			context->my_selection_order_map[orders[i] - 1] = i;
			change_detected = true;
		}
	}
	if (change_detected) {
		blog(LOG_INFO, "My order: %d %d %d %d %d %d\n", orders[0],
		     orders[1], orders[2], orders[3], orders[4], orders[5]);
		for (int i = 0; i < N_POKEMONS; i++) {
			context->matchstate.my_selection_order[i] = orders[i];
		}
	}
	return change_detected;
}

static void export_selection_order_image(struct filter_context *context)
{
	const char *stream_path =
		obs_data_get_string(context->settings, "stream_path");
	const char *selection_order_image_prefix = "SelectionOrder";
	for (int i = 0; i < N_POKEMONS; i++) {
		char filename_format[512];
		snprintf(filename_format, sizeof(filename_format), "%s-%d",
			 selection_order_image_prefix, i + 1);

		char *filename = os_generate_formatted_filename(
			"png", true, filename_format);

		char filepath[512];
		snprintf(filepath, sizeof(filepath), "%s/%s", stream_path,
			 filename);
		bfree(filename);

		pokemon_detector_sv_my_selection_order_export_image(
			context->detector_context, i, filepath,
			context->matchstate.my_selection_order[i] == -1);
	}
}

static uint64_t update_timer_text(obs_source_t *timer_source,
				  uint64_t time_start, uint64_t time_now,
				  uint64_t last_elapsed_seconds)
{
	uint64_t elapsed_seconds = (time_now - time_start) / 1000000000;
	if (elapsed_seconds == last_elapsed_seconds)
		return elapsed_seconds;
	uint64_t remaining_seconds = 20 * 60 - elapsed_seconds;
	uint64_t minutes = remaining_seconds / 60;
	uint64_t seconds = remaining_seconds % 60;

	char time_str[512];
	snprintf(time_str, sizeof(time_str), "%02" PRIu64 ":%02" PRIu64,
		 minutes, seconds);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", time_str);
	obs_source_update(timer_source, settings);
	obs_data_release(settings);

	return elapsed_seconds;
}

static void filter_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct filter_context *context = data;

	obs_source_t *parent = obs_filter_get_parent(context->source);
	if (!parent) {
		return;
	}

	if (!context->started)
		return;

	const enum pokemon_detector_sv_scene scene =
		pokemon_detector_sv_detect_scene(context->detector_context);
	if (context->state == STATE_UNKNOWN) {
		if (scene == POKEMON_DETECTOR_SV_SCENE_SELECT_POKEMON) {
			context->state = STATE_ENTERING_SELECT_POKEMON;
			context->last_state_change_ns =
				obs_get_video_frame_time();
			blog(LOG_INFO, "State: UNKNOWN to ENTERING_SELECT");
		}
	} else if (context->state == STATE_ENTERING_SELECT_POKEMON) {
		const uint64_t frame_time_ns = obs_get_video_frame_time();
		if (frame_time_ns - context->last_state_change_ns >
		    1000000000) {
			pokemon_detector_sv_opponent_pokemon_crop(
				context->detector_context);
			write_stream_files(context);
			write_log_files(context);
			context->state = STATE_SELECT_POKEMON;
			blog(LOG_INFO,
			     "State: ENTERING_SELECT to SELECT_POKEMON");
		}
	} else if (context->state == STATE_SELECT_POKEMON) {
		if (selection_order_detect_change(context)) {
			export_selection_order_image(context);
		}

		if (scene == POKEMON_DETECTOR_SV_SCENE_BLACK_TRANSITION) {
			context->state = STATE_ENTERING_MATCH;
			blog(LOG_INFO,
			     "State: SELECT_POKEMON to ENTERING_MATCH");
		}
	} else if (context->state == STATE_ENTERING_MATCH) {
		if (context->prev_scene !=
			    POKEMON_DETECTOR_SV_SCENE_BLACK_TRANSITION &&
		    scene == POKEMON_DETECTOR_SV_SCENE_BLACK_TRANSITION) {
			context->state = STATE_MATCH;
			context->match_start_ns = os_gettime_ns();
			blog(LOG_INFO, "State: ENTERING_MATCH to MATCH");
		} else if (scene == POKEMON_DETECTOR_SV_SCENE_SELECT_POKEMON) {
			context->state = STATE_SELECT_POKEMON;
			blog(LOG_INFO,
			     "State: ENTERING_MATCH to SELECT_POKEMON");
		}
	} else if (context->state == STATE_MATCH) {
		const char *timer_name =
			obs_data_get_string(context->settings, "timer_source");
		obs_source_t *timer_source = obs_get_source_by_name(timer_name);
		if (timer_source != NULL) {
			context->last_elapsed_seconds = update_timer_text(
				timer_source, context->match_start_ns,
				os_gettime_ns(), context->last_elapsed_seconds);
			obs_source_release(timer_source);
		}

		if (scene == POKEMON_DETECTOR_SV_SCENE_SELECT_POKEMON) {
			context->state = STATE_ENTERING_SELECT_POKEMON;
			blog(LOG_INFO,
			     "State: MATCH to ENTERING_SELECT_POKEMON");
		} else if (context->prev_scene !=
				   POKEMON_DETECTOR_SV_SCENE_BLACK_TRANSITION &&
			   scene ==
				   POKEMON_DETECTOR_SV_SCENE_BLACK_TRANSITION) {
			context->match_end_ns = os_gettime_ns();
			context->state = STATE_RESULT;
			blog(LOG_INFO, "MATCH to RESULT");
		}
	} else if (context->state == STATE_RESULT) {
		uint64_t now = os_gettime_ns();
		if (now - context->match_end_ns > 2000000000) {
			obs_frontend_take_source_screenshot(parent);
			pokemon_detector_sv_result_crop(
				context->detector_context);
			context->matchstate.result =
				pokemon_detector_sv_result_recognize(
					context->detector_context);
			flush_match_log(context);
			context->state = STATE_UNKNOWN;
			blog(LOG_INFO, "RESULT to UNKNOWN");
		} else if (scene == POKEMON_DETECTOR_SV_SCENE_SELECT_POKEMON) {
			flush_match_log(context);
			context->state = STATE_ENTERING_SELECT_POKEMON;
			blog(LOG_INFO, "MATCH to ENTERING_SELECT_POKEMON");
		}
	}
	context->prev_scene = scene;
}

static bool add_text_sources_to_list(void *param, obs_source_t *source)
{
	obs_property_t *prop = param;
	const char *id = obs_source_get_id(source);
	if (strcmp(id, "text_gdiplus_v2") == 0 ||
	    strcmp(id, "text_gdiplus") == 0 ||
	    strcmp(id, "text_ft2_source") == 0 ||
	    strcmp(id, "text_ft2_source_v2") == 0) {
		const char *name = obs_source_get_name(source);
		obs_property_list_add_string(prop, name, name);
	}
	return true;
}

static obs_properties_t *filter_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *props_stream = obs_properties_create();
	obs_properties_add_path(props_stream, "stream_path", "Output directory",
				OBS_PATH_DIRECTORY, NULL, NULL);
	obs_properties_add_text(props_stream, "stream_prefix",
				"Output file name prefix", OBS_TEXT_DEFAULT);
	obs_properties_add_group(props, "stream", "Stream", OBS_GROUP_NORMAL,
				 props_stream);

	obs_properties_t *props_log = obs_properties_create();
	obs_properties_add_path(props_log, "log_path", "Output path for log",
				OBS_PATH_DIRECTORY, NULL, NULL);
	obs_properties_add_text(props_log, "log_prefix",
				"Output file name prefix", OBS_TEXT_DEFAULT);
	obs_properties_add_group(props, "log", "Log", OBS_GROUP_NORMAL,
				 props_log);

	obs_properties_t *props_timer = obs_properties_create();
	obs_property_t *prop_timer = obs_properties_add_list(
		props_timer, "timer_source", "Timer Source",
		OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(add_text_sources_to_list, prop_timer);
	obs_properties_add_group(props, "timer", "Timer", OBS_GROUP_NORMAL,
				 props_timer);

	return props;
}

static const char *get_frontend_record_path(config_t *config)
{
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
	const char ps = PATH_SEPARATOR;

	config_t *config = obs_frontend_get_profile_config();
	const char *record_path = get_frontend_record_path(config);
	char stream_path[512];
	snprintf(stream_path, sizeof(stream_path), "%s%cstream", record_path,
		 ps);
	obs_data_set_default_string(settings, "stream_path", stream_path);
	obs_data_set_default_string(settings, "stream_prefix",
				    "OpponentPokemon");

	char log_path[512];
	snprintf(log_path, sizeof(log_path), "%s%clog", record_path, ps);
	obs_data_set_default_string(settings, "log_path", record_path);
	obs_data_set_default_string(settings, "log_prefix",
				    config_get_string(config, "Output",
						      "FilenameFormatting"));
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
