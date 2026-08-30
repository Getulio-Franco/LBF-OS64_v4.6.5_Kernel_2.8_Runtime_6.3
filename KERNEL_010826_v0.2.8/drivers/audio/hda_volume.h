/*
====================================================================
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: hda_volume.h
Versão: 1.1
Data: 29/08/2026
Autor: LBF-OS Team + AI Assistant

Base:
    - hda_volume original (payload/mute corretos)

Mudanças v1.1:
    - Trocado hda_device_t* por hda_codec_t* (arquitetura atual)
    - Nomes dos flags alinhados ao padrão validado (0xB07F)
    - Adicionado hda_set_master_volume (DAC + PIN de uma vez)
====================================================================
*/
#ifndef HDA_VOLUME_H
#define HDA_VOLUME_H

#include <stdint.h>
#include <stdbool.h>
#include "hda_codec.h"   // hda_codec_t + HDA_VERB_SET_AMP_GAIN (0x3)

// ============================================================================
// FLAGS DO PAYLOAD DE 16 BITS (SET_AMP_GAIN_MUTE)
// Mesmo padrão do 0xB07F que já produz som no STAC9221
// ============================================================================
#define HDA_AMP_SET_RIGHT       (1 << 15)
#define HDA_AMP_SET_LEFT        (1 << 13)
#define HDA_AMP_SET_OUTPUT      (1 << 12)
#define HDA_AMP_MUTE            (1 << 7)

/**
 * Define volume (0-100%) e mute de um nó (DAC ou Pin).
 */
bool hda_set_node_volume(hda_codec_t* codec, uint32_t node,
                         uint8_t volume_percent, bool mute);

/**
 * Muta/desmuta sem perder o volume atual.
 */
bool hda_set_node_mute(hda_codec_t* codec, uint32_t node,
                       bool mute, uint8_t current_volume_percent);

/**
 * Aplica volume/mute no caminho de saída completo (DAC + PIN).
 */
void hda_set_master_volume(hda_codec_t* codec,
                           uint8_t volume_percent, bool mute);

#endif // HDA_VOLUME_H
