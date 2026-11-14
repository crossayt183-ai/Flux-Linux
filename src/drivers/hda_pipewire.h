#ifndef FLUX_HDA_PIPEWIRE_H
#define FLUX_HDA_PIPEWIRE_H

#include "types.h"

typedef enum {
    PW_NODE_HDA_ANALOG   = 0, /* Built-in HDA Intel + Realtek ALC1220/ALC256 */
    PW_NODE_HDMI_DP      = 1, /* GPU DisplayPort / HDMI Digital Audio */
    PW_NODE_USB_PRO_DAC  = 2, /* External USB Audio Class 2.0 Interface (Focusrite/Fiio) */
    PW_NODE_BT_A2DP_SINK = 3, /* Bluetooth LDAC / AptX HD Headphone Sink */
    PW_NODE_APP_FIREFOX  = 4  /* Mozilla Firefox WebAudio / Media Stream */
} pw_node_kind_t;

typedef struct {
    uint32_t       node_id;
    pw_node_kind_t kind;
    char           node_name[48];
    char           driver_backend[32];
    uint32_t       sample_rate_hz;
    uint16_t       quantum_frames;
    uint8_t        channels;
    uint8_t        volume_percent;
    bool           muted;
    bool           active;
} pw_audio_node_t;

typedef struct {
    uint16_t subvendor_id;
    uint16_t subdevice_id;
    uint32_t codec_id;
    uint8_t  nid_pin_spk;
    uint8_t  nid_pin_hp;
    uint8_t  nid_pin_mic;
    const char *laptop_model;
    const char *quirk_fixup;
} hda_codec_quirk_t;

void   hda_pipewire_init(void);
size_t pipewire_node_count(void);
bool   pipewire_get_node(size_t idx, pw_audio_node_t *out);
bool   pipewire_set_quantum(uint16_t quantum_frames, uint32_t rate_hz);
bool   pipewire_set_default_sink(uint32_t node_id);
uint32_t pipewire_active_sink_id(void);
size_t hda_codec_quirk_table_size(void);
bool   hda_pipewire_selftest(void);

#endif
