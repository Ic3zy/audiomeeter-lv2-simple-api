#include "lv2_manager.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  printf("=== Testing Lv2Manager High-Level API ===\n\n");

  Lv2Manager *manager = lv2_manager_create(1024, 4, 48000);
  if (!manager) {
    fprintf(stderr, "Error: Failed to create Lv2Manager\n");
    return 1;
  }
  printf("[OK] Lv2Manager created successfully.\n");

  // 1. Add Filters
  printf("\nAdding filters to chain...\n");
  int idx0 = lv2_manager_add_filter(manager, "multisampler");
  int idx1 = lv2_manager_add_filter(manager, "mono");
  int idx2 = lv2_manager_add_filter(manager, "stereo");

  printf("Added filter indices: [%d], [%d], [%d]\n", idx0, idx1, idx2);

  // Print initial sequence
  printf("\n--- Initial Filter Sequence ---\n");
  for (size_t i = 0; i < 3; i++) {
    Lv2FilterInfo info;
    if (lv2_manager_get_filter_info(manager, i, &info)) {
      printf("Index [%zu]: %s (%s)\n", i, info.name,
             info.enabled ? "Active" : "Bypassed");
      lv2_manager_free_filter_info(&info);
    }
  }

  // 2. Move Filter: Move filter at index 2 to index 0
  printf("\n---> Move: Moving filter at index [2] to index [0]...\n");
  lv2_manager_move_filter(manager, 2, 0);

  printf("\n--- Sequence After Move ---\n");
  for (size_t i = 0; i < 3; i++) {
    Lv2FilterInfo info;
    if (lv2_manager_get_filter_info(manager, i, &info)) {
      printf("Index [%zu]: %s\n", i, info.name);
      lv2_manager_free_filter_info(&info);
    }
  }

  // 3. Swap Filters: Swap index 1 and index 2
  printf("\n---> Swap: Swapping filter at index [1] with index [2]...\n");
  lv2_manager_swap_filters(manager, 1, 2);

  printf("\n--- Sequence After Swap ---\n");
  for (size_t i = 0; i < 3; i++) {
    Lv2FilterInfo info;
    if (lv2_manager_get_filter_info(manager, i, &info)) {
      printf("Index [%zu]: %s\n", i, info.name);
      lv2_manager_free_filter_info(&info);
    }
  }

  // 4. Test Audio Processing
  int n_samples = 256;
  float *in_l = calloc(n_samples, sizeof(float));
  float *in_r = calloc(n_samples, sizeof(float));

  for (int i = 0; i < n_samples; i++) {
    in_l[i] = 0.5f;
    in_r[i] = 0.5f;
  }

  printf("\nProcessing %d audio samples...\n", n_samples);
  struct Output *out = lv2_manager_process(manager, in_l, in_r, n_samples);

  if (out && out->left && out->right) {
    printf("[OK] Audio successfully processed with updated sequence.\n");
    printf("Sample [0] Output: Left = %.4f, Right = %.4f\n", out->left[0],
           out->right[0]);
  } else {
    fprintf(stderr, "Error: Failed to process audio\n");
  }

  free(in_l);
  free(in_r);
  lv2_manager_destroy(manager);

  printf("\n=== Test Completed Successfully ===\n");
  return 0;
}