# LV2 Filter Chain Manager (`lv2_manager`)

A lightweight, high-level C library for managing and processing LV2 audio plugin chains. Designed to abstract away the complexity of Lilv, `lv2_manager` provides clean stereo processing, internal ping-pong double-buffering, dynamic block-size expansion, and real-time filter reordering.

---

## Key Features

- **Lilv Abstraction**: Clean C API with zero Lilv dependency leakage in consumer headers.
- **Stereo Processing**: Independent Left and Right audio channel input pointers (`in_l`, `in_r`).
- **Internal Double-Buffering**: Built-in ping-pong buffers (`out1`, `out2`) handle cascading filter processing without requiring external output allocations.
- **Pointer Output**: `lv2_manager_process` returns a direct pointer to internal output buffers (`struct Output *`).
- **Dynamic Reordering**: Move or swap filter positions in the chain dynamically without interrupting audio routing.
- **Dynamic Block Sizing**: Automatically resizes internal buffers at runtime to fit any buffer size (`n_samples`).
- **Parameter Inspection & Control**: Query plugin parameters, read ranges, and update control ports by symbol name.

---

## Directory Structure

```text
test_lv2_chain/
├── README.md
├── src/
│   ├── include/
│   │   └── lv2_manager.h   # Public C header (No Lilv dependencies)
│   ├── lv2_manager.c       # Core implementation & Lilv integration
│   └── main.c              # Test & example usage runner
```

---

## Prerequisites & Dependencies

To build and run `lv2_manager`, you need:

- **GCC** or **Clang** C compiler
- **Lilv** development libraries (`liblilv-dev` on Debian/Ubuntu, `lilv` on Arch Linux)
- **pkg-config**

On Debian/Ubuntu:
```bash
sudo apt update
sudo apt install build-essential liblilv-dev pkg-config
```

On Arch Linux:
```bash
sudo pacman -S base-devel lilv pkgconf
```

---

## Building and Running

Compile the test executable using `pkg-config`:

```bash
gcc -Wall -Wextra $(pkg-config --cflags lilv-0) -Isrc/include src/lv2_manager.c src/main.c $(pkg-config --libs lilv-0) -lm -o lv2_chain_test
```

Run the test binary:

```bash
./lv2_chain_test
```

---

## Quick Start Example

```c
#include <stdio.h>
#include <stdlib.h>
#include "lv2_manager.h"

int main(void) {
    // 1. Create manager instance (initial buffer: 1024 samples, sample rate: 48000 Hz)
    Lv2Manager *manager = lv2_manager_create(1024, 4, 48000);

    // 2. Add LV2 plugins by URI or search query
    int eq_idx   = lv2_manager_add_filter(manager, "biquad");
    int comp_idx = lv2_manager_add_filter(manager, "compressor");

    // 3. Dynamically reorder plugins in chain
    lv2_manager_move_filter(manager, comp_idx, 0); // Move compressor to 1st position

    // 4. Update parameter values
    lv2_manager_set_param(manager, 0, "threshold", -12.0f);

    // 5. Process stereo audio block
    int n_samples = 512;
    float in_left[512] = {0};
    float in_right[512] = {0};

    // Returns pointer to internal Output buffers (out->left, out->right)
    struct Output *out = lv2_manager_process(manager, in_left, in_right, n_samples);

    printf("First processed sample: L=%.4f, R=%.4f\n", out->left[0], out->right[0]);

    // 6. Cleanup
    lv2_manager_destroy(manager);
    return 0;
}
```

---

## API Reference

### Manager Lifecycle
- `Lv2Manager *lv2_manager_create(int n_samples, int min_filter_count, int sample_rate)`: Creates manager instance.
- `void lv2_manager_destroy(Lv2Manager *manager)`: Frees manager and all filter instances.

### Chain Management
- `int lv2_manager_add_filter(Lv2Manager *manager, const char *target_uri)`: Appends plugin matching URI/name query.
- `bool lv2_manager_remove_filter(Lv2Manager *manager, size_t index)`: Removes plugin at given index.
- `bool lv2_manager_move_filter(Lv2Manager *manager, size_t old_index, size_t new_index)`: Moves plugin position.
- `bool lv2_manager_swap_filters(Lv2Manager *manager, size_t index_a, size_t index_b)`: Swaps two plugin positions.
- `bool lv2_manager_set_bypass(Lv2Manager *manager, size_t index, bool enabled)`: Enables or bypasses plugin.

### Parameters & Inspection
- `bool lv2_manager_get_filter_info(Lv2Manager *manager, size_t index, Lv2FilterInfo *out_info)`: Gets metadata & parameters.
- `void lv2_manager_free_filter_info(Lv2FilterInfo *info)`: Frees info struct allocations.
- `bool lv2_manager_set_param(Lv2Manager *manager, size_t filter_index, const char *symbol, float value)`: Sets control port value.
- `float lv2_manager_get_param(Lv2Manager *manager, size_t filter_index, const char *symbol)`: Reads control port value.

### Audio Processing
- `struct Output *lv2_manager_process(Lv2Manager *manager, const float *in_l, const float *in_r, int n_samples)`: Processes stereo audio block and returns output buffer pointer.

---

## License

MIT License. Free for open-source and commercial use.
