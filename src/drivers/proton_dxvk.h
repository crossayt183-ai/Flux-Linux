#ifndef FLUX_PROTON_DXVK_H
#define FLUX_PROTON_DXVK_H

#include "types.h"

#define PE_DOS_SIGNATURE  0x5A4DU     /* "MZ" */
#define PE_NT_SIGNATURE   0x00004550U /* "PE\0\0" */
#define PE_IMAGE_AMD64    0x8664U
#define PE_OPT_MAGIC_64   0x020BU

typedef enum {
    DX_API_D3D9       = 0, /* DXVK D3D9  -> Vulkan 1.4 */
    DX_API_D3D11      = 1, /* DXVK 2.5   -> Vulkan 1.4 */
    DX_API_D3D12_ULT  = 2, /* VKD3D-Proton 2.13 (DX12 Ultimate + RayTracing -> Vulkan 1.4) */
    DX_API_VULKAN_NAT = 3  /* Native Vulkan 1.4 */
} dx_api_kind_t;

typedef struct {
    char          exe_name[64];
    dx_api_kind_t dx_api;
    uint64_t      image_base;
    uint64_t      entry_point;
    uint32_t      spirv_shaders_compiled;
    uint32_t      avg_fps;
    uint32_t      one_pct_low_fps;
    bool          dlss4_fg_active;
    bool          fsr3_active;
    bool          gamemode_active;
    bool          eac_battleye_ok;
    char          translator[48];
    char          proton_version[32];
} proton_game_session_t;

void   proton_dxvk_init(void);
bool   proton_launch_exe(const char *exe_path, dx_api_kind_t api, proton_game_session_t *out);
bool   proton_get_last_session(proton_game_session_t *out);
size_t proton_win32_api_count(void);
size_t proton_shader_cache_count(void);
bool   proton_dxvk_selftest(void);

#endif
