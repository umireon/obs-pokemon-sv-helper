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

static void pokemon_sv_plugin_recognize_hotkey(
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

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "plugin loaded successfully (version %s)",
	     PLUGIN_VERSION);

	obs_data_t *context = load_context();

	signal_handler_t *handler = obs_get_signal_handler();
	signal_handler_connect(handler, "hotkey_bindings_changed", hotkey_changed, context);

	recognize_hotkey_id = obs_hotkey_register_frontend(
		"obs-pokemon-sv-plugin.recognize",
		"Recognize Team",
		pokemon_sv_plugin_recognize_hotkey,
		context);
	obs_data_array_t *recognize_hotkey_array = obs_data_get_array(context, CONTEXT_HOTKEY_RECOGNIZE);
	if (obs_data_array_count(recognize_hotkey_array) != 0) {
		obs_hotkey_load(recognize_hotkey_id, recognize_hotkey_array);
	}

	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "plugin unloaded");
}
