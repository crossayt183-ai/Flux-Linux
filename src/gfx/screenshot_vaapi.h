#ifndef FLUX_SCREENSHOT_VAAPI_H
#define FLUX_SCREENSHOT_VAAPI_H

#include "types.h"

typedef enum {
    SHOT_MODE_FULLSCREEN = 0, /* PrtSc: Full Desktop (Grim / GNOME Screenshot) */
    SHOT_MODE_WINDOW     = 1, /* Alt+PrtSc: Active Window only */
    SHOT_MODE_REGION     = 2  /* Shift+PrtSc: Slurp / FlameShot Interactive Region */
} screenshot_mode_t;

typedef struct {
    uint32_t          shot_id;
    screenshot_mode_t mode;
    uint32_t          width;
    uint32_t          height;
    uint32_t          file_bytes;
    uint32_t          pixel_crc32;
    bool              copied_to_wayland_clipboard;
    char              saved_path[64];
    char              tool_name[40];
} screenshot_info_t;

typedef struct {
    uint32_t clip_id;
    bool     recording_active;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bitrate_kbps;
    uint32_t file_bytes;
    char     saved_path[64];
    char     encoder_name[48];
} screen_record_info_t;

typedef struct {
    bool     in_use;
    uint32_t size_bytes;
    char     file_name[32];
    char     orig_path[64];
    char     trash_path[64];
} trash_entry_t;

typedef struct {
    bool     vaapi_ready;
    bool     nvdec_nvenc_ready;
    bool     quicksync_2600k_ready;
    bool     zram_active;
    uint32_t zram_ratio_x100; /* e.g. 285 = 2.85x compression ratio */
    uint32_t cpu_load_4k60_pct;
    char     active_codec[64];
    char     driver_backend[64];
    char     usb_hid_status[64];
} vaapi_zram_status_t;

void        screenshot_vaapi_init(void);
bool        screenshot_set_folder(const char *folder_path);
const char *screenshot_get_folder(void);
bool        take_linux_screenshot(screenshot_mode_t mode, screenshot_info_t *out);
bool        get_last_screenshot(screenshot_info_t *out);
uint32_t    screenshot_count(void);

/* 1. Screen Recorder (ShadowPlay / NVENC / QuickSync + Video Folder Picker) */
bool        video_set_folder(const char *folder_path);
const char *video_get_folder(void);
bool        record_screen_clip(uint32_t fps, screen_record_info_t *out);
bool        get_last_recording(screen_record_info_t *out);
bool        is_screen_recording(void);

/* 4. Desktop Trash (/home/.Trash) with 1-Click Restore & Empty Trash */
bool        trash_move_file(const char *path);
bool        trash_restore_last(char *out_restored_path, size_t cap);
uint32_t    trash_empty_all(void);
size_t      trash_item_count(void);
bool        trash_get_item(size_t idx, trash_entry_t *out);

bool        vaapi_get_status(vaapi_zram_status_t *out);
size_t      vaapi_codec_db_size(void);
size_t      usb_nvme_db_size(void);
bool        screenshot_vaapi_selftest(void);

#endif
