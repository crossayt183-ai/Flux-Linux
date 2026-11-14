#ifndef FLUX_GPU_DRM_H
#define FLUX_GPU_DRM_H

#include "types.h"

#define GPU_VENDOR_AMD     0x1002U
#define GPU_VENDOR_INTEL   0x8086U
#define GPU_VENDOR_NVIDIA  0x10DEU
#define GPU_VENDOR_QEMU    0x1234U

typedef enum {
    GPU_DRV_NONE       = 0,
    GPU_DRV_AMDGPU     = 1, /* Built-in kernel amdgpu + Mesa RADV/RadeonSI */
    GPU_DRV_I915_XE    = 2, /* Built-in kernel i915/xe + Mesa ANV/Iris */
    GPU_DRV_NOUVEAU    = 3, /* Built-in fallback open driver (basic 2D/KMS only) */
    GPU_DRV_NVIDIA_PRO = 4, /* Proprietary nvidia.ko + CUDA + Full 3D Boost */
    GPU_DRV_BOCHS_DRM  = 5  /* QEMU/StdVGA DRM KMS bridge */
} gpu_driver_kind_t;

typedef enum {
    GPU_PERF_BASIC_2D  = 0, /* No 3D acceleration, base clocks only (nouveau) */
    GPU_PERF_FULL_MESA = 1, /* Full out-of-the-box Mesa OpenGL 4.6 + Vulkan 1.3 */
    GPU_PERF_FULL_PROP = 2  /* Full proprietary NVIDIA 3D + Vulkan + CUDA + NVENC */
} gpu_perf_tier_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  arch_gen;
    uint8_t  compute_units;
    uint16_t base_clock_mhz;
    uint16_t boost_clock_mhz;
    uint32_t vram_mib;
    const char *chip_name;
    const char *marketing_name;
    const char *default_kmod;
    const char *mesa_driver;
} gpu_pci_entry_t;

typedef struct {
    uint32_t reg_offset;
    uint32_t reset_val;
    uint32_t mask_rw;
    const char *reg_name;
    const char *subsystem;
} gpu_mmio_reg_t;

typedef struct {
    bool              present;
    uint16_t          vendor_id;
    uint16_t          device_id;
    gpu_driver_kind_t active_driver;
    gpu_perf_tier_t   perf_tier;
    uint32_t          vram_mib;
    uint32_t          current_clock_mhz;
    uint32_t          max_boost_mhz;
    uint32_t          temp_celsius;
    uint32_t          fan_percent;
    bool              vulkan_ready;
    bool              opengl_ready;
    bool              cuda_ready;
    bool              raytracing_ready;
    char              card_name[64];
    char              kmod_name[32];
    char              mesa_version[48];
    char              status_note[96];
} gpu_card_state_t;

typedef struct {
    char     pnp_id[4];
    uint16_t product_code;
    uint16_t native_w;
    uint16_t native_h;
    uint16_t min_vrr_hz;
    uint16_t max_refresh_hz;
    uint8_t  color_bpc;
    bool     freesync_gsync;
    bool     hdr10_ready;
    bool     dsc_12a;
    const char *model_name;
    const char *panel_tech;
    const char *connector_spec;
} drm_monitor_edid_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    uint32_t min_vrr_hz;
    uint32_t pixel_clock_khz;
    uint32_t scaling_pct;
    uint8_t  color_bpc;
    bool     vrr_enabled;
    bool     hdr10_enabled;
    bool     dsc_enabled;
    char     connector_name[32];
    char     monitor_name[64];
    char     sync_tech[40];
} drm_display_mode_t;

void     gpu_drm_init(void);
size_t   gpu_drm_card_count(void);
bool     gpu_drm_get_card(size_t idx, gpu_card_state_t *out);
bool     gpu_drm_switch_nvidia_driver(bool enable_proprietary);
bool     gpu_drm_is_nvidia_proprietary(void);
uint32_t gpu_drm_lookup_mmio_default(uint16_t vendor, uint32_t reg_offset);
size_t   gpu_drm_pci_db_size(void);
size_t   gpu_drm_monitor_db_size(void);
bool     gpu_drm_get_active_display(drm_display_mode_t *out);
bool     gpu_drm_set_display_mode(uint32_t width, uint32_t height, uint32_t refresh_hz, bool vrr, bool hdr10);
bool     gpu_drm_selftest(void);

#endif
