#define _POSIX_C_SOURCE 200809L

#include "lv2_manager.h"

#include <lilv/lilv.h>
#include <lv2/urid/urid.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char **uris;
  size_t count;
} HostURIDMap;

struct Filter {
  bool enabled;
  char *name;
  char *uri;
  LilvInstance *instance;
  const LilvPlugin *plugin;

  // Audio port indices ((uint32_t)-1 if not present)
  uint32_t audio_in_l;
  uint32_t audio_in_r;
  uint32_t audio_out_l;
  uint32_t audio_out_r;

  // Control port values
  uint32_t num_ports;
  float *control_values;
  bool *is_control_in;

  // Parameters metadata
  Lv2ParamInfo *params;
  size_t param_count;
};

struct OutputInternal {
  struct Output pub; // pub.left and pub.right
  int capacity;
};

struct Lv2Manager {
  struct OutputInternal out1, out2;

  struct Filter *filters;
  size_t filter_count;
  size_t filter_capacity;

  int sample_rate;
  LilvWorld *world;

  // Cached Lilv URIs for port classification
  LilvNode *lv2_InputPort;
  LilvNode *lv2_OutputPort;
  LilvNode *lv2_AudioPort;
  LilvNode *lv2_ControlPort;
  LilvNode *lv2_toggled;

  // URID Map feature
  HostURIDMap urid_map;
  LV2_URID_Map map_feature;
  LV2_URID_Unmap unmap_feature;
  LV2_Feature uri_map_feature;
  LV2_Feature uri_unmap_feature;
  const LV2_Feature *features[3];
};

static LV2_URID host_urid_map(LV2_URID_Map_Handle handle, const char *uri) {
  HostURIDMap *map = (HostURIDMap *)handle;
  for (size_t i = 0; i < map->count; ++i) {
    if (strcmp(map->uris[i], uri) == 0) {
      return (LV2_URID)(i + 1);
    }
  }
  char **new_uris = realloc(map->uris, sizeof(char *) * (map->count + 1));
  if (!new_uris) return 0;
  map->uris = new_uris;

  size_t len = strlen(uri) + 1;
  map->uris[map->count] = malloc(len);
  if (map->uris[map->count]) {
    memcpy(map->uris[map->count], uri, len);
  }
  map->count++;
  return (LV2_URID)map->count;
}

static const char *host_urid_unmap(LV2_URID_Unmap_Handle handle, LV2_URID urid) {
  HostURIDMap *map = (HostURIDMap *)handle;
  if (urid > 0 && urid <= map->count) {
    return map->uris[urid - 1];
  }
  return NULL;
}

static bool ensure_output_capacity(struct OutputInternal *out, int n_samples) {
  if (out->capacity >= n_samples) return true;

  float *new_l = realloc(out->pub.left, sizeof(float) * n_samples);
  float *new_r = realloc(out->pub.right, sizeof(float) * n_samples);
  if (!new_l || !new_r) return false;

  out->pub.left = new_l;
  out->pub.right = new_r;
  out->capacity = n_samples;
  return true;
}

Lv2Manager *lv2_manager_create(int n_samples, int min_filter_count, int sample_rate) {
  Lv2Manager *manager = calloc(1, sizeof(*manager));
  if (manager == NULL) return NULL;

  int initial_size = n_samples > 0 ? n_samples : 512;

  manager->out1.pub.left = calloc(initial_size, sizeof(float));
  manager->out1.pub.right = calloc(initial_size, sizeof(float));
  manager->out1.capacity = initial_size;

  manager->out2.pub.left = calloc(initial_size, sizeof(float));
  manager->out2.pub.right = calloc(initial_size, sizeof(float));
  manager->out2.capacity = initial_size;

  size_t capacity = min_filter_count > 0 ? min_filter_count : 4;
  manager->filters = calloc(capacity, sizeof(struct Filter));
  if (manager->filters == NULL || !manager->out1.pub.left || !manager->out1.pub.right ||
      !manager->out2.pub.left || !manager->out2.pub.right) {
    free(manager->out1.pub.left);
    free(manager->out1.pub.right);
    free(manager->out2.pub.left);
    free(manager->out2.pub.right);
    free(manager->filters);
    free(manager);
    return NULL;
  }
  manager->filter_capacity = capacity;

  manager->world = lilv_world_new();
  if (manager->world) {
    lilv_world_load_all(manager->world);
    manager->lv2_InputPort   = lilv_new_uri(manager->world, LV2_CORE__InputPort);
    manager->lv2_OutputPort  = lilv_new_uri(manager->world, LV2_CORE__OutputPort);
    manager->lv2_AudioPort   = lilv_new_uri(manager->world, LV2_CORE__AudioPort);
    manager->lv2_ControlPort = lilv_new_uri(manager->world, LV2_CORE__ControlPort);
    manager->lv2_toggled     = lilv_new_uri(manager->world, LV2_CORE__toggled);
  }

  manager->sample_rate = sample_rate;

  manager->urid_map.uris = NULL;
  manager->urid_map.count = 0;

  manager->map_feature.handle = &manager->urid_map;
  manager->map_feature.map = host_urid_map;
  manager->uri_map_feature.URI = LV2_URID__map;
  manager->uri_map_feature.data = &manager->map_feature;

  manager->unmap_feature.handle = &manager->urid_map;
  manager->unmap_feature.unmap = host_urid_unmap;
  manager->uri_unmap_feature.URI = LV2_URID__unmap;
  manager->uri_unmap_feature.data = &manager->unmap_feature;

  manager->features[0] = &manager->uri_map_feature;
  manager->features[1] = &manager->uri_unmap_feature;
  manager->features[2] = NULL;

  return manager;
}

static void free_filter_struct(struct Filter *f) {
  if (!f) return;
  if (f->instance) {
    lilv_instance_deactivate(f->instance);
    lilv_instance_free(f->instance);
  }
  free(f->name);
  free(f->uri);
  free(f->control_values);
  free(f->is_control_in);

  if (f->params) {
    for (size_t i = 0; i < f->param_count; i++) {
      free(f->params[i].symbol);
      free(f->params[i].name);
    }
    free(f->params);
  }
  memset(f, 0, sizeof(*f));
}

void lv2_manager_destroy(Lv2Manager *manager) {
  if (manager == NULL) return;

  for (size_t i = 0; i < manager->filter_count; i++) {
    free_filter_struct(&manager->filters[i]);
  }

  for (size_t i = 0; i < manager->urid_map.count; i++) {
    free(manager->urid_map.uris[i]);
  }
  free(manager->urid_map.uris);

  free(manager->filters);
  free(manager->out1.pub.left);
  free(manager->out1.pub.right);
  free(manager->out2.pub.left);
  free(manager->out2.pub.right);

  if (manager->world) {
    lilv_node_free(manager->lv2_InputPort);
    lilv_node_free(manager->lv2_OutputPort);
    lilv_node_free(manager->lv2_AudioPort);
    lilv_node_free(manager->lv2_ControlPort);
    lilv_node_free(manager->lv2_toggled);
    lilv_world_free(manager->world);
  }
  free(manager);
}

Lv2PluginAvailableList lv2_manager_get_available_plugins(Lv2Manager *manager) {
  Lv2PluginAvailableList list = {NULL, 0};
  if (!manager || !manager->world) return list;

  const LilvPlugins *plugins = lilv_world_get_all_plugins(manager->world);
  uint32_t total = lilv_plugins_size(plugins);
  if (total == 0) return list;

  list.plugins = calloc(total, sizeof(Lv2PluginAvailableInfo));
  size_t idx = 0;

  LILV_FOREACH(plugins, i, plugins) {
    const LilvPlugin *p = lilv_plugins_get(plugins, i);
    const LilvNode *uri_node = lilv_plugin_get_uri(p);
    LilvNode *name_node = lilv_plugin_get_name(p);
    const LilvPluginClass *pclass = lilv_plugin_get_class(p);
    const LilvNode *class_node = pclass ? lilv_plugin_class_get_label(pclass) : NULL;

    const char *uri_str = uri_node ? lilv_node_as_uri(uri_node) : NULL;
    const char *name_str = name_node ? lilv_node_as_string(name_node) : NULL;
    const char *cat_str = class_node ? lilv_node_as_string(class_node) : "Plugin";

    if (name_str && uri_str) {
      list.plugins[idx].name = strdup(name_str);
      list.plugins[idx].uri = strdup(uri_str);
      list.plugins[idx].category = strdup(cat_str);
      idx++;
    }
    lilv_node_free(name_node);
  }
  list.count = idx;
  return list;
}

void lv2_manager_free_available_plugins(Lv2PluginAvailableList *list) {
  if (!list || !list->plugins) return;
  for (size_t i = 0; i < list->count; i++) {
    free(list->plugins[i].name);
    free(list->plugins[i].uri);
    free(list->plugins[i].category);
  }
  free(list->plugins);
  list->plugins = NULL;
  list->count = 0;
}

int lv2_manager_add_filter(Lv2Manager *manager, const char *target_uri) {
  if (manager == NULL || target_uri == NULL || manager->world == NULL) return -1;

  if (manager->filter_count >= manager->filter_capacity) {
    size_t new_cap = manager->filter_capacity * 2;
    struct Filter *new_filters = realloc(manager->filters, sizeof(struct Filter) * new_cap);
    if (!new_filters) return -1;
    memset(new_filters + manager->filter_capacity, 0, sizeof(struct Filter) * (new_cap - manager->filter_capacity));
    manager->filters = new_filters;
    manager->filter_capacity = new_cap;
  }

  const LilvPlugins *plugins = lilv_world_get_all_plugins(manager->world);
  const LilvPlugin *target_plugin = NULL;

  LILV_FOREACH(plugins, i, plugins) {
    const LilvPlugin *p = lilv_plugins_get(plugins, i);
    const LilvNode *uri_node = lilv_plugin_get_uri(p);
    const char *uri = lilv_node_as_uri(uri_node);

    LilvNode *name_node = lilv_plugin_get_name(p);
    const char *name = lilv_node_as_string(name_node);

    if ((uri && strstr(uri, target_uri)) || (name && strstr(name, target_uri))) {
      target_plugin = p;
      lilv_node_free(name_node);
      break;
    }
    lilv_node_free(name_node);
  }

  if (!target_plugin) {
    fprintf(stderr, "Error: Plugin not found for URI pattern: %s\n", target_uri);
    return -1;
  }

  LilvInstance *instance = lilv_plugin_instantiate(target_plugin, manager->sample_rate, manager->features);
  if (!instance) {
    fprintf(stderr, "Error: Plugin instantiate failed\n");
    return -1;
  }

  struct Filter *filter = &manager->filters[manager->filter_count];
  memset(filter, 0, sizeof(*filter));

  filter->instance = instance;
  filter->plugin = target_plugin;
  filter->enabled = true;

  LilvNode *name_node = lilv_plugin_get_name(target_plugin);
  filter->name = strdup(lilv_node_as_string(name_node));
  lilv_node_free(name_node);

  filter->uri = strdup(lilv_node_as_uri(lilv_plugin_get_uri(target_plugin)));

  filter->audio_in_l  = (uint32_t)-1;
  filter->audio_in_r  = (uint32_t)-1;
  filter->audio_out_l = (uint32_t)-1;
  filter->audio_out_r = (uint32_t)-1;

  uint32_t num_ports = lilv_plugin_get_num_ports(target_plugin);
  filter->num_ports = num_ports;
  filter->control_values = calloc(num_ports, sizeof(float));
  filter->is_control_in = calloc(num_ports, sizeof(bool));

  uint32_t audio_in_count = 0;
  uint32_t audio_out_count = 0;
  size_t control_in_count = 0;

  for (uint32_t p = 0; p < num_ports; p++) {
    const LilvPort *port = lilv_plugin_get_port_by_index(target_plugin, p);
    bool is_input  = lilv_port_is_a(target_plugin, port, manager->lv2_InputPort);
    bool is_output = lilv_port_is_a(target_plugin, port, manager->lv2_OutputPort);
    bool is_audio  = lilv_port_is_a(target_plugin, port, manager->lv2_AudioPort);
    bool is_ctrl   = lilv_port_is_a(target_plugin, port, manager->lv2_ControlPort);

    if (is_audio) {
      if (is_input) {
        if (audio_in_count == 0) filter->audio_in_l = p;
        else if (audio_in_count == 1) filter->audio_in_r = p;
        audio_in_count++;
      } else if (is_output) {
        if (audio_out_count == 0) filter->audio_out_l = p;
        else if (audio_out_count == 1) filter->audio_out_r = p;
        audio_out_count++;
      }
    } else if (is_ctrl && is_input) {
      filter->is_control_in[p] = true;
      control_in_count++;

      LilvNode *def_node = NULL, *min_node = NULL, *max_node = NULL;
      lilv_port_get_range(target_plugin, port, &def_node, &min_node, &max_node);

      float def_val = def_node ? lilv_node_as_float(def_node) : 0.0f;
      filter->control_values[p] = def_val;

      lilv_node_free(def_node);
      lilv_node_free(min_node);
      lilv_node_free(max_node);

      lilv_instance_connect_port(instance, p, &filter->control_values[p]);
    }
  }

  filter->param_count = control_in_count;
  if (control_in_count > 0) {
    filter->params = calloc(control_in_count, sizeof(Lv2ParamInfo));
    size_t idx = 0;
    for (uint32_t p = 0; p < num_ports; p++) {
      if (!filter->is_control_in[p]) continue;

      const LilvPort *port = lilv_plugin_get_port_by_index(target_plugin, p);
      const LilvNode *sym_node = lilv_port_get_symbol(target_plugin, port);
      LilvNode *pname_node = lilv_port_get_name(target_plugin, port);

      LilvNode *def_node = NULL, *min_node = NULL, *max_node = NULL;
      lilv_port_get_range(target_plugin, port, &def_node, &min_node, &max_node);

      filter->params[idx].port_index  = p;
      filter->params[idx].symbol      = strdup(lilv_node_as_string(sym_node));
      filter->params[idx].name        = strdup(lilv_node_as_string(pname_node));
      filter->params[idx].default_val = def_node ? lilv_node_as_float(def_node) : 0.0f;
      filter->params[idx].min_val     = min_node ? lilv_node_as_float(min_node) : 0.0f;
      filter->params[idx].max_val     = max_node ? lilv_node_as_float(max_node) : 1.0f;
      filter->params[idx].current_val = filter->control_values[p];
      filter->params[idx].is_toggle   = lilv_port_has_property(target_plugin, port, manager->lv2_toggled);

      lilv_node_free(pname_node);
      lilv_node_free(def_node);
      lilv_node_free(min_node);
      lilv_node_free(max_node);
      idx++;
    }
  }

  lilv_instance_activate(instance);

  int added_idx = (int)manager->filter_count;
  manager->filter_count++;
  return added_idx;
}

bool lv2_manager_remove_filter(Lv2Manager *manager, size_t index) {
  if (!manager || index >= manager->filter_count) return false;

  free_filter_struct(&manager->filters[index]);

  for (size_t i = index; i < manager->filter_count - 1; i++) {
    manager->filters[i] = manager->filters[i + 1];
  }
  memset(&manager->filters[manager->filter_count - 1], 0, sizeof(struct Filter));
  manager->filter_count--;
  return true;
}

bool lv2_manager_move_filter(Lv2Manager *manager, size_t old_index, size_t new_index) {
  if (!manager || old_index >= manager->filter_count || new_index >= manager->filter_count) {
    return false;
  }
  if (old_index == new_index) return true;

  struct Filter target = manager->filters[old_index];

  if (old_index < new_index) {
    memmove(&manager->filters[old_index],
            &manager->filters[old_index + 1],
            sizeof(struct Filter) * (new_index - old_index));
  } else {
    memmove(&manager->filters[new_index + 1],
            &manager->filters[new_index],
            sizeof(struct Filter) * (old_index - new_index));
  }

  manager->filters[new_index] = target;
  return true;
}

bool lv2_manager_swap_filters(Lv2Manager *manager, size_t index_a, size_t index_b) {
  if (!manager || index_a >= manager->filter_count || index_b >= manager->filter_count) {
    return false;
  }
  if (index_a == index_b) return true;

  struct Filter temp = manager->filters[index_a];
  manager->filters[index_a] = manager->filters[index_b];
  manager->filters[index_b] = temp;
  return true;
}

bool lv2_manager_set_bypass(Lv2Manager *manager, size_t index, bool enabled) {
  if (!manager || index >= manager->filter_count) return false;
  manager->filters[index].enabled = enabled;
  return true;
}

bool lv2_manager_get_filter_info(Lv2Manager *manager, size_t index, Lv2FilterInfo *out_info) {
  if (!manager || index >= manager->filter_count || !out_info) return false;

  struct Filter *f = &manager->filters[index];
  out_info->index = index;
  out_info->name = strdup(f->name);
  out_info->uri = strdup(f->uri);
  out_info->enabled = f->enabled;
  out_info->param_count = f->param_count;

  if (f->param_count > 0) {
    out_info->params = calloc(f->param_count, sizeof(Lv2ParamInfo));
    for (size_t i = 0; i < f->param_count; i++) {
      out_info->params[i].port_index  = f->params[i].port_index;
      out_info->params[i].symbol      = strdup(f->params[i].symbol);
      out_info->params[i].name        = strdup(f->params[i].name);
      out_info->params[i].min_val     = f->params[i].min_val;
      out_info->params[i].max_val     = f->params[i].max_val;
      out_info->params[i].default_val = f->params[i].default_val;
      out_info->params[i].current_val = f->control_values[f->params[i].port_index];
      out_info->params[i].is_toggle   = f->params[i].is_toggle;
    }
  } else {
    out_info->params = NULL;
  }
  return true;
}

void lv2_manager_free_filter_info(Lv2FilterInfo *info) {
  if (!info) return;
  free(info->name);
  free(info->uri);
  if (info->params) {
    for (size_t i = 0; i < info->param_count; i++) {
      free(info->params[i].symbol);
      free(info->params[i].name);
    }
    free(info->params);
  }
  memset(info, 0, sizeof(*info));
}

bool lv2_manager_set_param(Lv2Manager *manager, size_t filter_index, const char *symbol, float value) {
  if (!manager || filter_index >= manager->filter_count || !symbol) return false;

  struct Filter *f = &manager->filters[filter_index];
  for (size_t i = 0; i < f->param_count; i++) {
    if (strcmp(f->params[i].symbol, symbol) == 0) {
      uint32_t p = f->params[i].port_index;
      f->control_values[p] = value;
      f->params[i].current_val = value;
      return true;
    }
  }
  return false;
}

float lv2_manager_get_param(Lv2Manager *manager, size_t filter_index, const char *symbol) {
  if (!manager || filter_index >= manager->filter_count || !symbol) return 0.0f;

  struct Filter *f = &manager->filters[filter_index];
  for (size_t i = 0; i < f->param_count; i++) {
    if (strcmp(f->params[i].symbol, symbol) == 0) {
      uint32_t p = f->params[i].port_index;
      return f->control_values[p];
    }
  }
  return 0.0f;
}

struct Output *lv2_manager_process(Lv2Manager *manager,
                                   const float *in_l, const float *in_r,
                                   int n_samples) {
  if (!manager || n_samples <= 0) return NULL;

  ensure_output_capacity(&manager->out1, n_samples);
  ensure_output_capacity(&manager->out2, n_samples);

  size_t enabled_count = 0;
  for (size_t i = 0; i < manager->filter_count; i++) {
    if (manager->filters[i].enabled) enabled_count++;
  }

  if (enabled_count == 0) {
    if (in_l) memcpy(manager->out1.pub.left, in_l, sizeof(float) * n_samples);
    else memset(manager->out1.pub.left, 0, sizeof(float) * n_samples);

    if (in_r) memcpy(manager->out1.pub.right, in_r, sizeof(float) * n_samples);
    else memset(manager->out1.pub.right, 0, sizeof(float) * n_samples);

    return &manager->out1.pub;
  }

  struct OutputInternal *src_buf = &manager->out1;
  struct OutputInternal *dst_buf = &manager->out2;

  if (in_l) memcpy(src_buf->pub.left, in_l, sizeof(float) * n_samples);
  else memset(src_buf->pub.left, 0, sizeof(float) * n_samples);

  if (in_r) memcpy(src_buf->pub.right, in_r, sizeof(float) * n_samples);
  else memset(src_buf->pub.right, 0, sizeof(float) * n_samples);

  for (size_t i = 0; i < manager->filter_count; i++) {
    struct Filter *f = &manager->filters[i];
    if (!f->enabled) continue;

    if (f->audio_in_l != (uint32_t)-1) {
      lilv_instance_connect_port(f->instance, f->audio_in_l, src_buf->pub.left);
    }
    if (f->audio_in_r != (uint32_t)-1) {
      lilv_instance_connect_port(f->instance, f->audio_in_r, src_buf->pub.right);
    }

    if (f->audio_out_l != (uint32_t)-1) {
      lilv_instance_connect_port(f->instance, f->audio_out_l, dst_buf->pub.left);
    }
    if (f->audio_out_r != (uint32_t)-1) {
      lilv_instance_connect_port(f->instance, f->audio_out_r, dst_buf->pub.right);
    }

    lilv_instance_run(f->instance, (uint32_t)n_samples);

    if (f->audio_out_l != (uint32_t)-1 && f->audio_out_r == (uint32_t)-1) {
      memcpy(dst_buf->pub.right, dst_buf->pub.left, sizeof(float) * n_samples);
    }

    struct OutputInternal *tmp = src_buf;
    src_buf = dst_buf;
    dst_buf = tmp;
  }

  return &src_buf->pub;
}
