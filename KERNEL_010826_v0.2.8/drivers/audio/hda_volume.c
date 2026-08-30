/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: hda_volume.c
Versão: 1.1
Data: 29/08/2026
Autor: LBF-OS Team + AI Assistant

Mudanças v1.1:
    - Usa hda_send_verb + hda_make_verb da arquitetura atual
    - Mantida a matemática de ganho/mute do arquivo original
====================================================================
*/

#include "hda_volume.h"
#include "hda_corb_rirb.h"

// ============================================================================
// VOLUME / MUTE POR NÓ
// ============================================================================
bool hda_set_node_volume(hda_codec_t* codec, uint32_t node,
                         uint8_t volume_percent, bool mute) {
    if (!codec || !codec->ring || !codec->is_initialized) return false;
    if (volume_percent > 100) volume_percent = 100;

    // 0-100% -> escala de 7 bits do hardware (0..127)
    uint8_t gain = (uint8_t)((uint32_t)volume_percent * 127 / 100);

    // Payload: Output Amp + Canal Esquerdo + Direito
    uint16_t payload = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT;
    if (mute) {
        payload |= HDA_AMP_MUTE;          // bit 7 = mute
    } else {
        payload |= (gain & 0x7F);         // bits 6:0 = ganho
    }

    uint32_t response = 0;
    if (!hda_send_verb(codec->ring, codec->codec_addr,
                       hda_make_verb(node, HDA_VERB_SET_AMP_GAIN, payload),
                       &response)) {
        return false;
    }
    return (response != 0xFFFFFFFF);
}

// ============================================================================
// MUTE SEM PERDER O VOLUME
// ============================================================================
bool hda_set_node_mute(hda_codec_t* codec, uint32_t node,
                       bool mute, uint8_t current_volume_percent) {
    return hda_set_node_volume(codec, node, current_volume_percent, mute);
}

// ============================================================================
// MASTER VOLUME (DAC + PIN)
// ============================================================================
void hda_set_master_volume(hda_codec_t* codec,
                           uint8_t volume_percent, bool mute) {
    if (!codec || !codec->is_initialized) return;
    hda_set_node_volume(codec, codec->dac_node, volume_percent, mute);
    hda_set_node_volume(codec, codec->pin_node, volume_percent, mute);
}
