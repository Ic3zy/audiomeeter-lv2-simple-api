#ifndef LV2_MANAGER_H
#define LV2_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lv2Manager Lv2Manager;

/**
 * @brief Container structure for stereo output audio buffers (left and right channels).
 */
struct Output {
  float *left;
  float *right;
};

/**
 * @brief Metadata for a configurable filter parameter (LV2 Control Port).
 */
typedef struct {
  uint32_t port_index;
  char *symbol;        // Parameter identifier symbol (e.g., "gain", "threshold")
  char *name;          // Human-readable parameter name (e.g., "Input Gain")
  float min_val;       // Minimum value
  float max_val;       // Maximum value
  float default_val;   // Default value
  float current_val;   // Active value
  bool is_toggle;      // True if parameter is boolean/toggle
} Lv2ParamInfo;

/**
 * @brief General metadata for an active plugin/filter in the chain.
 */
typedef struct {
  size_t index;
  char *name;
  char *uri;
  bool enabled;
  Lv2ParamInfo *params;
  size_t param_count;
} Lv2FilterInfo;

/**
 * @brief Metadata for an available installed LV2 plugin on the system.
 */
typedef struct {
  char *name;
  char *uri;
  char *category;
} Lv2PluginAvailableInfo;

/**
 * @brief List container for available installed LV2 plugins.
 */
typedef struct {
  Lv2PluginAvailableInfo *plugins;
  size_t count;
} Lv2PluginAvailableList;

/**
 * @brief Creates a new Lv2Manager instance.
 * @param n_samples Initial internal double-buffer sample capacity (expands dynamically)
 * @param min_filter_count Initial filter array capacity
 * @param sample_rate Audio sample rate (e.g., 44100 or 48000)
 * @return Lv2Manager* Pointer to created manager instance, or NULL on error
 */
Lv2Manager *lv2_manager_create(int n_samples, int min_filter_count, int sample_rate);

/**
 * @brief Destroys an Lv2Manager instance and frees all associated resources.
 * @param manager Pointer to Lv2Manager instance
 */
void lv2_manager_destroy(Lv2Manager *manager);

/**
 * @brief Queries all installed LV2 plugins on the system for plugin discovery / selection menus.
 * @param manager Pointer to Lv2Manager instance
 * @return Lv2PluginAvailableList Container struct of available plugins (must free with lv2_manager_free_available_plugins)
 */
Lv2PluginAvailableList lv2_manager_get_available_plugins(Lv2Manager *manager);

/**
 * @brief Frees memory allocated by lv2_manager_get_available_plugins.
 * @param list Pointer to Lv2PluginAvailableList container
 */
void lv2_manager_free_available_plugins(Lv2PluginAvailableList *list);

/**
 * @brief Adds a new filter to the end of the processing chain.
 * @param manager Pointer to Lv2Manager instance
 * @param target_uri Exact plugin URI or search query substring (e.g., "biquad", "amp")
 * @return Index of the added filter in the chain, or -1 on error
 */
int lv2_manager_add_filter(Lv2Manager *manager, const char *target_uri);

/**
 * @brief Removes a filter from the processing chain by index.
 * @param manager Pointer to Lv2Manager instance
 * @param index Index of the filter to remove
 * @return true on success, false if index is out of bounds
 */
bool lv2_manager_remove_filter(Lv2Manager *manager, size_t index);

/**
 * @brief Moves a filter from its current index to a target index in the chain.
 * @param manager Pointer to Lv2Manager instance
 * @param old_index Current index of the filter
 * @param new_index Target index position
 * @return true on success, false if indices are out of bounds
 */
bool lv2_manager_move_filter(Lv2Manager *manager, size_t old_index, size_t new_index);

/**
 * @brief Swaps the positions of two filters in the processing chain.
 * @param manager Pointer to Lv2Manager instance
 * @param index_a Index of first filter
 * @param index_b Index of second filter
 * @return true on success, false if indices are out of bounds
 */
bool lv2_manager_swap_filters(Lv2Manager *manager, size_t index_a, size_t index_b);

/**
 * @brief Sets the bypass state of a filter in the chain.
 * @param manager Pointer to Lv2Manager instance
 * @param index Index of the filter
 * @param enabled true to process audio, false to bypass
 * @return true on success, false if index is out of bounds
 */
bool lv2_manager_set_bypass(Lv2Manager *manager, size_t index, bool enabled);

/**
 * @brief Queries metadata and configurable parameter list for a filter.
 * @param manager Pointer to Lv2Manager instance
 * @param index Index of the filter
 * @param out_info Pointer to Lv2FilterInfo struct to populate
 * @return true on success, false on failure (must free with lv2_manager_free_filter_info)
 */
bool lv2_manager_get_filter_info(Lv2Manager *manager, size_t index, Lv2FilterInfo *out_info);

/**
 * @brief Frees internal allocations inside a Lv2FilterInfo populated by lv2_manager_get_filter_info.
 * @param info Pointer to Lv2FilterInfo struct
 */
void lv2_manager_free_filter_info(Lv2FilterInfo *info);

/**
 * @brief Sets a filter parameter value by its symbol identifier.
 * @param manager Pointer to Lv2Manager instance
 * @param filter_index Index of the filter in the chain
 * @param symbol Parameter symbol identifier (e.g., "gain")
 * @param value New value to set
 * @return true on success, false if filter or symbol was not found
 */
bool lv2_manager_set_param(Lv2Manager *manager, size_t filter_index, const char *symbol, float value);

/**
 * @brief Reads the current value of a filter parameter by symbol identifier.
 * @param manager Pointer to Lv2Manager instance
 * @param filter_index Index of the filter in the chain
 * @param symbol Parameter symbol identifier
 * @return Current float value, or 0.0f on error
 */
float lv2_manager_get_param(Lv2Manager *manager, size_t filter_index, const char *symbol);

/**
 * @brief Processes input audio through the filter chain using internal ping-pong double buffering.
 * 
 * No external output buffer allocation is required by the caller.
 * 
 * @param manager Pointer to Lv2Manager instance
 * @param in_l Left channel input audio buffer (NULL treats channel as silence)
 * @param in_r Right channel input audio buffer (NULL treats channel as silence)
 * @param n_samples Number of audio samples to process
 * @return struct Output* Pointer to internal output struct containing left and right processed buffers
 */
struct Output *lv2_manager_process(Lv2Manager *manager,
                                   const float *in_l, const float *in_r,
                                   int n_samples);

#ifdef __cplusplus
}
#endif

#endif // LV2_MANAGER_H
