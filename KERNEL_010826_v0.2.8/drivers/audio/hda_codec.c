#include "hda_codec.h"
#include "util/sysutils.h"
#include "drivers/proc.h"

static uint32_t hda_get_param(hda_corb_rirb_t* ring, uint8_t codec_addr, uint8_t node, uint8_t param) {
    uint32_t response = 0;
    if (!hda_send_verb(ring, codec_addr, hda_make_verb(node, 0xF00, param), &response)) {
        return 0xFFFFFFFF;
    }
    return response;
}

uint32_t hda_make_verb(uint32_t node, uint32_t verb, uint32_t payload) {
    uint32_t formatted_node = (node & 0x7F) << 20;

    if (verb <= 0xF) {
        return formatted_node | ((verb & 0x0F) << 16) | (payload & 0xFFFF);
    }
    return formatted_node | ((verb & 0xFFF) << 8) | (payload & 0xFF);
}

static int hda_find_connection_index(hda_codec_t* ctx, uint8_t pin_node, uint8_t dac_node) {
    uint32_t conn_len_resp = hda_get_param(ctx->ring, ctx->codec_addr, pin_node, HDA_PAR_CONN_LIST);
    if (conn_len_resp == 0xFFFFFFFF) return -1;

    uint8_t conn_count = conn_len_resp & 0xFF;
    if (conn_count == 0) return -1;

    uint32_t response = 0;
    if (!hda_send_verb(ctx->ring, ctx->codec_addr, hda_make_verb(pin_node, HDA_VERB_GET_CONNECT, 0x00), &response)) {
        return -1;
    }

    for (uint8_t i = 0; i < conn_count && i < 4; i++) {
        uint8_t target_node = (response >> (i * 8)) & 0xFF;
        if (target_node == dac_node) return i;
    }
    return -1;
}

bool hda_codec_init(hda_codec_t* codec_ctx, hda_corb_rirb_t* ring_ctx, uint8_t codec_addr) {
    if (!codec_ctx || !ring_ctx || !ring_ctx->is_initialized) return false;

    codec_ctx->ring = ring_ctx;
    codec_ctx->codec_addr = codec_addr;
    codec_ctx->is_initialized = false;

    // 1. Handshake do Vendor ID
    uint32_t vendor = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        vendor = hda_get_param(ring_ctx, codec_addr, 0x00, HDA_PAR_VENDOR_ID);
        if (vendor != 0 && vendor != 0xFFFFFFFF) break;
        sys_sleep(10);
    }
    if (vendor == 0xFFFFFFFF || vendor == 0) return false;
    codec_ctx->vendor_id = vendor;

    // 2. Descoberta do AFG (Audio Function Group)
    uint32_t node_count = hda_get_param(ring_ctx, codec_addr, 0x00, HDA_PAR_NODE_COUNT);
    if (node_count == 0xFFFFFFFF) return false;

    uint8_t start_node = (node_count >> 16) & 0xFF;
    uint8_t total_nodes = node_count & 0xFF;

    codec_ctx->afg_node = 0;
    for (uint8_t i = 0; i < total_nodes; i++) {
        uint8_t node_id = start_node + i;
        uint32_t fg_resp = hda_get_param(ring_ctx, codec_addr, node_id, HDA_PAR_FG_TYPE);
        if (fg_resp != 0xFFFFFFFF && (fg_resp & 0x7F) == 0x01) {
            codec_ctx->afg_node = node_id;
            break;
        }
    }
    if (codec_ctx->afg_node == 0) return false;

    // 3. Varredura dos Widgets (Encontrar DAC e PIN Complex)
    uint32_t afg_resp = hda_get_param(ring_ctx, codec_addr, codec_ctx->afg_node, HDA_PAR_NODE_COUNT);
    if (afg_resp == 0xFFFFFFFF) return false;

    uint8_t widget_start = (afg_resp >> 16) & 0xFF;
    uint8_t widget_count = afg_resp & 0xFF;

    codec_ctx->dac_node = 0;
    codec_ctx->pin_node = 0;

    for (uint8_t i = 0; i < widget_count; i++) {
        uint8_t widget_id = widget_start + i;
        uint32_t wcaps = hda_get_param(ring_ctx, codec_addr, widget_id, HDA_PAR_WIDGET_CAP);
        if (wcaps == 0xFFFFFFFF) continue;

        uint8_t wtype = (wcaps >> 20) & 0xF;

        if (wtype == WIDGET_TYPE_AUDIO_OUTPUT && codec_ctx->dac_node == 0) {
            codec_ctx->dac_node = widget_id;
        } else if (wtype == WIDGET_TYPE_PIN_COMPLEX && codec_ctx->pin_node == 0) {
            uint32_t pin_caps = hda_get_param(ring_ctx, codec_addr, widget_id, HDA_PAR_PIN_CAP);
            if (pin_caps != 0xFFFFFFFF && (pin_caps & (1 << 4))) { // Output capable
                codec_ctx->pin_node = widget_id;
            }
        }
    }

    if (codec_ctx->dac_node != 0 && codec_ctx->pin_node == 0) {
        codec_ctx->pin_node = codec_ctx->dac_node + 1;
    }
    if (codec_ctx->dac_node == 0 || codec_ctx->pin_node == 0) return false;

    // 4. Power State D0 (Ligar tudo)
    uint32_t dummy = 0;
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->afg_node, HDA_VERB_SET_POWER_STATE, 0x00), &dummy);
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->dac_node, HDA_VERB_SET_POWER_STATE, 0x00), &dummy);
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_POWER_STATE, 0x00), &dummy);
    sys_sleep(10);

    // 5. Configuração do Pin Control (Headphone / Line-out Out Enable)
    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_PIN_CTL, 0x40), &dummy);

    uint32_t pin_caps = hda_get_param(ring_ctx, codec_addr, codec_ctx->pin_node, HDA_PAR_PIN_CAP);
    if (pin_caps != 0xFFFFFFFF && (pin_caps & (1 << 16))) { // Suporta EAPD
        hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_EAPD, 0x02), &dummy);
    }

    // 6. Roteamento PIN -> DAC
    int conn_index = hda_find_connection_index(codec_ctx, codec_ctx->pin_node, codec_ctx->dac_node);
    if (conn_index < 0) conn_index = 0;

    hda_send_verb(ring_ctx, codec_addr, hda_make_verb(codec_ctx->pin_node, HDA_VERB_SET_CONNECT, conn_index), &dummy);

    // 7. Configuração Inicial de Ganho/Unmute (Volume em 100%)
    hda_codec_set_volume(codec_ctx, 100);

    codec_ctx->is_initialized = true;
    return true;
}

bool hda_setup_dac_stream(hda_codec_t* codec, uint8_t stream_id, uint16_t format) {
    if (!codec || !codec->ring || !codec->is_initialized || stream_id == 0 || stream_id > 0x0F) {
        return false;
    }

    uint32_t dummy = 0;
    if (!hda_send_verb(codec->ring, codec->codec_addr, hda_make_verb(codec->dac_node, HDA_VERB_SET_FORMAT, format), &dummy)) {
        return false;
    }

    uint8_t stream_payload = (uint8_t)((stream_id & 0x0F) << 4);
    if (!hda_send_verb(codec->ring, codec->codec_addr, hda_make_verb(codec->dac_node, HDA_VERB_SET_STREAM, stream_payload), &dummy)) {
        return false;
    }

    return true;
}

// ============================================================================
// CONTROLE DE VOLUME (DAC + PIN)
// ============================================================================
void hda_codec_set_volume(hda_codec_t* codec, uint8_t volume_percent) {
    if (!codec || !codec->ring) return;
    if (volume_percent > 100) volume_percent = 100;

    uint8_t gain = (volume_percent * 127) / 100;
    uint16_t amp_payload = 0xB000 | (gain & 0x7F);
    uint32_t dummy = 0;

    vga_print_string("[HDA_CODEC] set_volume: 1o send_verb (DAC)...\n", 0, 38);
    hda_send_verb(codec->ring, codec->codec_addr,
                  hda_make_verb(codec->dac_node, HDA_VERB_SET_AMP_GAIN, amp_payload), &dummy);

    vga_print_string("[HDA_CODEC] set_volume: 2o send_verb (PIN)...\n", 0, 38);
    hda_send_verb(codec->ring, codec->codec_addr,
                  hda_make_verb(codec->pin_node, HDA_VERB_SET_AMP_GAIN, amp_payload), &dummy);

    vga_print_string("[HDA_CODEC] set_volume: saiu OK\n", 0, 38);
}
