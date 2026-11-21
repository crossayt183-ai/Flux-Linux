#ifndef FLUX_VULKAN_RADV_NVK_H
#define FLUX_VULKAN_RADV_NVK_H

#include "types.h"

typedef struct {
    uint32_t pipeline_id;
    uint8_t  vk_driver;    /* 1=NVIDIA-NVK-570, 2=Mesa-RADV-ACO, 3=Mesa-ANV-Xe, 4=Lavapipe-LLVM-SW */
    uint8_t  shader_stage; /* 1=Vertex/Mesh, 2=Fragment, 3=Compute-Async, 4=RayGen-BVH-RT */
    uint16_t push_const_bytes;
    const char *vk_extension_name;
    const char *spirv_optimization;
    const char *descriptor_binding_model;
} vulkan_pipeline_rule_t;

typedef struct {
    bool     vulkan_1_4_core_online;
    bool     ray_tracing_bvh_ready;
    bool     mesh_shader_pipeline;
    bool     descriptor_buffer_bindless;
    uint32_t compiled_pso_count;
} vulkan_radv_nvk_status_t;

void   vulkan_radv_nvk_init(void);
bool   vulkan_radv_nvk_get_status(vulkan_radv_nvk_status_t *out);
size_t vulkan_radv_nvk_db_size(void);
bool   vulkan_radv_nvk_selftest(void);

#endif
