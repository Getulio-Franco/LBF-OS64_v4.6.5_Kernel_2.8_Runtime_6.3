/*
====================================================================
              LBF-OS — TEMPLATE OFICIAL INTEL HDA
                    HISTÓRICO DE COLABORAÇÃO
====================================================================
Arquivo: audiohda.c
Versão: 3.0 (TEMPLATE OFICIAL DO S.O.)
Data: 30/08/2026
Autor: LBF-OS Team + AI Assistant
Compatibilidade: Kernel v2.8 | Runtime v6.3 | LBF-OS Base_v4.6.5

--------------------------------------------------------------------
1. PROPÓSITO DESTE ARQUIVO
--------------------------------------------------------------------
Este é o TEMPLATE OFICIAL do LBF-OS para desenvolvimento de
software.elf com áudio via Intel HDA no Ring 3. Todo software
futuro que usar áudio deve nascer a partir deste modelo, até
que uma nova versão o substitua.

--------------------------------------------------------------------
2. RECURSOS CONTIDOS (tudo abaixo JÁ ESTÁ funcionando)
--------------------------------------------------------------------
[A] STREAMING CONTÍNUO (syscalls oficiais do Kernel v2.8):
    - sys_audio_open()               → abre stream, retorna id >= 0
    - sys_audio_write(id, buf, size) → envia chunks PCM (máx 4096B)
    - sys_audio_close(id)            → fecha stream
    - sys_audio_set_volume(pct, mut) → aplica volume no mixer
    - Formato PCM: 48kHz, 16-bit, Stereo (interleaved L/R)

[B] GERADORES DE ÁUDIO PCM (exemplos de produção):
    - gerar_pcm_chime / gerar_pcm_siren / gerar_pcm_bass /
      gerar_pcm_effect / mix_all_sounds

[C] CONTROLE DE VOLUME (botões VOL -, VOL + e MUTE):
    - Faixa segura 1..100 (NUNCA 0, NUNCA negativo)
    - Degraus de 10 em 10 (padrão 10,20,...,100)
    - MUTE = volume 1 (silêncio prático, sem crash)

[D] GUI VCL/IPC completa: janela, botões, memo de console,
    foco de janela, máquina de estados de mouse.

--------------------------------------------------------------------
3. GUIA / OBSERVAÇÕES PARA PROGRAMADORES (CONTROLE DE VOLUME)
--------------------------------------------------------------------
REGRA DE OURO #1 — O volume NUNCA pode ser 0 nem negativo.
    Volume 0 trava/mata o processo de Ring 3 (bug histórico do
    caminho de mute + cast/IntToStr). Use SEMPRE clamp_volume()
    após qualquer cálculo. Piso = 1, teto = 100.

REGRA #2 — MUTE = volume 1. Não existe "mute real" de hardware
    neste template: o mixer do kernel multiplica as amostras por
    volume/100; com 1% o som é inaudível e o sistema fica estável.

REGRA #3 — Após mudar g_volume, chame
    sys_audio_set_volume((uint8_t)g_volume, false) para o kernel
    aplicar no mixer de software.

REGRA #4 — Degraus de 10: subir = (g_volume/10 + 1)*10;
    descer = ((g_volume+9)/10 - 1)*10 com piso 1. Isso mantém o
    padrão limpo 10,20,...,100 mesmo vindo do estado 1 (MUTED).

REGRA #5 — Não execute syscalls pesadas dentro do despacho de
    click do WM quando puder evitar. O padrão "pending_volume"
    (ação deferida no loop principal) está disponível e é o mais
    seguro para não dessincronizar foco/mouse da janela.

REGRA #6 — Abra o stream (sys_audio_open) ANTES de tocar. Envie
    chunks de no máximo 4096 bytes; trate written == 0 (ring
    cheio) com sys_sleep(5) e escrita parcial com sys_sleep(1).

REGRA #7 — Ao fechar o software, feche o stream.
    (Tratar_Fechamento_Software já faz isso automaticamente.)

--------------------------------------------------------------------
4. HISTÓRICO RESUMIDO (v2.1 → v2.9)
--------------------------------------------------------------------
v2.1: streaming estático, CHUNK 4096, buffers alinhados 4096
v2.2: sintaxe __attribute__ corrigida, backpressure no write
v2.3: botões VOL-/VOL+/MUTE + estado de volume local
v2.4/2.5: MUTE simplificado; callbacks puros; MUTE afastado da
          borda direita do WM (zona de arraste)
v2.6/2.7: clamp 1..100 (nunca 0/negativo); MUTE = 1
v2.8: clamp_volume() + ação deferida pending_volume
v2.9: degraus de 10 (arredondamento para múltiplos de 10)
v3.0: TEMPLATE OFICIAL — especificação + guia p/ programadores

Testado em: LBF-OS Base_v4.6.5 / Kernel v2.8 / Runtime v6.3
====================================================================
*/

#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/sysutils.h"
#include "../system/liblib.h"
#include "components/TOS_IPC.h"
#include "components/TOSSerial.h"

#define M_PI 3.14159265358979323846f

// ============================================================================
// CONFIGURAÇÕES DE ÁUDIO - INTEL HDA (STREAMING ESTÁTICO)
// ============================================================================
#define AUDIO_SAMPLES_COUNT 32000    // ~0.66 segundos a 48kHz (Estéreo)
#define CHUNK_SIZE 4096              // 4096 bytes exatos por chamada (Max k_write_temp)

// ============================================================================
// BUFFERS DE ÁUDIO ALINHADOS
// ============================================================================
static int16_t g_pcm_chime[AUDIO_SAMPLES_COUNT * 2]  __attribute__((aligned(4096)));
static int16_t g_pcm_siren[AUDIO_SAMPLES_COUNT * 2]  __attribute__((aligned(4096)));
static int16_t g_pcm_bass[AUDIO_SAMPLES_COUNT * 2]   __attribute__((aligned(4096)));
static int16_t g_pcm_effect[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));
static int16_t g_mix_buffer[AUDIO_SAMPLES_COUNT * 2] __attribute__((aligned(4096)));

static int g_audio_stream_id = -1;

// ============================================================================
// ESTADO LOCAL DE VOLUME (blindado: 1..100, nunca 0, nunca negativo)
// ============================================================================
static int g_volume = 100;

// Ação de volume DEFERIDA (processada no loop principal, fora do
// despacho de click do WM) — padrão mais seguro p/ foco/mouse
static volatile int pending_volume = -1;   // -1 = nada pendente

// ============================================================================
// PROTEÇÃO DE VOLUME — garante faixa 1..100 SEMPRE (REGRA #1)
// ============================================================================
static void clamp_volume(void) {
    if (g_volume > 100) g_volume = 100;
    if (g_volume < 1)   g_volume = 1;   // piso seguro: nunca zero/negativo
}

// ============================================================================
// FUNÇÃO SENO E GERADORES DE ÁUDIO
// ============================================================================
static float custom_sinf(float x) {
    while (x > M_PI)  x -= 2.0f * M_PI;
    while (x < -M_PI) x += 2.0f * M_PI;
    float x2 = x * x;
    float x3 = x * x2;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    return x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f);
}

static uint32_t gerar_pcm_chime(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float freqs[3] = {523.25f, 659.25f, 783.99f};
    float rad_steps[3];
    float angles[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) rad_steps[i] = (2.0f * M_PI * freqs[i]) / (float)sample_rate;
    for (uint32_t i = 0; i < samples; i++) {
        float mix = 0.0f;
        for (int j = 0; j < 3; j++) {
            mix += custom_sinf(angles[j]);
            angles[j] += rad_steps[j];
            if (angles[j] > 2.0f * M_PI) angles[j] -= 2.0f * M_PI;
        }
        int16_t val = (int16_t)((mix / 3.0f) * (float)amplitude);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
    }
    return samples * 2;
}

static uint32_t gerar_pcm_siren(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float base_freq = 600.0f;
    float angle = 0.0f, mod_angle = 0.0f;
    float mod_step = (2.0f * M_PI * 5.0f) / (float)sample_rate;
    for (uint32_t i = 0; i < samples; i++) {
        float current_freq = base_freq + (200.0f * custom_sinf(mod_angle));
        float rad_step = (2.0f * M_PI * current_freq) / (float)sample_rate;
        int16_t val = (int16_t)(custom_sinf(angle) * (float)amplitude);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
        angle += rad_step;
        if (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
        mod_angle += mod_step;
        if (mod_angle > 2.0f * M_PI) mod_angle -= 2.0f * M_PI;
    }
    return samples * 2;
}

static uint32_t gerar_pcm_bass(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float rad_step = (2.0f * M_PI * 110.0f) / (float)sample_rate; // 110Hz (A2)
    float angle = 0.0f;
    for (uint32_t i = 0; i < samples; i++) {
        int16_t val = (int16_t)(custom_sinf(angle) * (float)amplitude);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
        angle += rad_step;
        if (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
    }
    return samples * 2;
}

static uint32_t gerar_pcm_effect(int16_t* buffer, uint32_t samples, uint16_t amplitude) {
    uint32_t sample_rate = 48000;
    float rad_step = (2.0f * M_PI * 1200.0f) / (float)sample_rate;
    float angle = 0.0f;
    for (uint32_t i = 0; i < samples; i++) {
        int16_t val = (int16_t)(custom_sinf(angle) * (float)amplitude);
        buffer[i*2] = val;
        buffer[i*2+1] = val;
        angle += rad_step;
        if (angle > 2.0f * M_PI) angle -= 2.0f * M_PI;
    }
    return samples * 2;
}

void mix_all_sounds(void) {
    memset(g_mix_buffer, 0, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
    int16_t* sounds[] = {g_pcm_chime, g_pcm_siren, g_pcm_bass, g_pcm_effect};
    float volumes[] = {0.8f, 0.6f, 1.0f, 0.7f};
    for (int s = 0; s < 4; s++) {
        int16_t* src = sounds[s];
        float vol = volumes[s];
        for (uint32_t i = 0; i < AUDIO_SAMPLES_COUNT * 2; i++) {
            int32_t mixed = g_mix_buffer[i] + (int32_t)(src[i] * vol);
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            g_mix_buffer[i] = (int16_t)mixed;
        }
    }
}

// ============================================================================
// STREAMING VIA RING BUFFER ESTÁTICO (RING 3 -> KERNEL) — REGRA #6
// ============================================================================
static bool stream_audio_buffer(int16_t* buffer, uint32_t total_bytes) {
    if (g_audio_stream_id < 0) return false;

    uint32_t offset = 0;

    while (offset < total_bytes) {
        uint32_t remaining = total_bytes - offset;
        uint32_t chunk_to_send = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

        int written = sys_audio_write(g_audio_stream_id,
                                      (uint8_t*)buffer + offset,
                                      chunk_to_send);

        if (written < 0) return false;

        if (written == 0) {
            // Ring do kernel cheio: cede CPU para o kernel drenar via poll/IRQ
            sys_sleep(5);
            continue;
        }

        offset += (uint32_t)written;

        if (written < (int)chunk_to_send) {
            // Escrita parcial: ring quase cheio, respira 1ms
            sys_sleep(1);
        }
    }

    return true;
}

// ============================================================================
// CONTROLES DE TOCAR
// ============================================================================
void play_chime(void) {
    if (g_audio_stream_id < 0) return;
    gerar_pcm_chime(g_pcm_chime, AUDIO_SAMPLES_COUNT, 16000);
    stream_audio_buffer(g_pcm_chime, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
}

void play_siren(void) {
    if (g_audio_stream_id < 0) return;
    gerar_pcm_siren(g_pcm_siren, AUDIO_SAMPLES_COUNT, 12000);
    stream_audio_buffer(g_pcm_siren, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
}

void play_bass(void) {
    if (g_audio_stream_id < 0) return;
    gerar_pcm_bass(g_pcm_bass, AUDIO_SAMPLES_COUNT, 18000);
    stream_audio_buffer(g_pcm_bass, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
}

void play_effect(void) {
    if (g_audio_stream_id < 0) return;
    gerar_pcm_effect(g_pcm_effect, AUDIO_SAMPLES_COUNT, 14000);
    stream_audio_buffer(g_pcm_effect, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
}

void play_mix_all(void) {
    if (g_audio_stream_id < 0) return;
    gerar_pcm_chime(g_pcm_chime, AUDIO_SAMPLES_COUNT, 16000);
    gerar_pcm_siren(g_pcm_siren, AUDIO_SAMPLES_COUNT, 12000);
    gerar_pcm_bass(g_pcm_bass, AUDIO_SAMPLES_COUNT, 18000);
    gerar_pcm_effect(g_pcm_effect, AUDIO_SAMPLES_COUNT, 14000);
    mix_all_sounds();
    stream_audio_buffer(g_mix_buffer, AUDIO_SAMPLES_COUNT * 2 * sizeof(int16_t));
}

// ============================================================================
// GUI & WINDOW CALLBACKS
// ============================================================================
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);

int my_app_slot = -1;
TGUIEnvironment MyApp;
const int winWidth = 620;
const int winHeight = 440;

TGUIControl* BtnChime = NULL;
TGUIControl* BtnSiren = NULL;
TGUIControl* BtnBass = NULL;
TGUIControl* BtnEffect = NULL;
TGUIControl* BtnStopAll = NULL;
TGUIControl* BtnMixTest = NULL;
TGUIControl* BtnOpenStream = NULL;
TGUIControl* BtnCloseStream = NULL;
TGUIControl* BtnVolUp = NULL;
TGUIControl* BtnVolDown = NULL;
TGUIControl* BtnMute = NULL;
TGUIControl* ExeMemo = NULL;

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

char Obter_Tecla_Entrada(void) {
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return 0;
}

void Flush_Grafico_Janela(void) {
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (g_audio_stream_id >= 0) {
        sys_audio_close(g_audio_stream_id);
        g_audio_stream_id = -1;
    }
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

/* ============================================================================
 * CALLBACKS DE VOLUME (REGRA #1..#5 — clamp 1..100, degraus de 10)
 * ============================================================================ */
static void print_volume_status(void) {
    char msg[64];
    char v[16];
    strcpy(msg, "[Volume] ");
    IntToStr(g_volume, v); strcat(msg, v); strcat(msg, "%");
    if (g_volume <= 1) strcat(msg, " (MUTED)");  // 1% = praticamente silêncio
    strcat(msg, "\n");
    GUI_Memo_AddStr(ExeMemo, msg);
}

void OnBtnVolUpClick(void* sender) {
    // Sobe para o PRÓXIMO múltiplo de 10 (1 -> 10, 10 -> 20, ... 90 -> 100)
    int next = (g_volume / 10 + 1) * 10;
    g_volume = (next > 100) ? 100 : next;
    sys_audio_set_volume((uint8_t)g_volume, false);
    print_volume_status();
}

void OnBtnVolDownClick(void* sender) {
    // Desce para o múltiplo de 10 ANTERIOR, piso seguro = 1 (10 -> 1, 20 -> 10)
    int prev = ((g_volume + 9) / 10 - 1) * 10;
    g_volume = (prev < 1) ? 1 : prev;
    sys_audio_set_volume((uint8_t)g_volume, false);
    print_volume_status();
}

void OnBtnMuteClick(void* sender) {
    // MUTE = volume 1 (mínimo seguro, nunca 0) — ação deferida (REGRA #5)
    pending_volume = 1;
}

/* ============================================================================
 * CALLBACKS DOS BOTÕES DE ÁUDIO
 * ============================================================================ */
void OnBtnOpenStreamClick(void* sender) {
    if (g_audio_stream_id >= 0) {
        GUI_Memo_AddStr(ExeMemo, "[HDA] Stream ja aberto.\n");
        return;
    }
    g_audio_stream_id = sys_audio_open();
    if (g_audio_stream_id >= 0) {
        GUI_Memo_AddStr(ExeMemo, "[HDA] Stream aberto com sucesso!\n");
    } else {
        GUI_Memo_AddStr(ExeMemo, "[HDA] ERRO ao abrir stream.\n");
    }
}

void OnBtnCloseStreamClick(void* sender) {
    if (g_audio_stream_id < 0) return;
    sys_audio_close(g_audio_stream_id);
    g_audio_stream_id = -1;
    GUI_Memo_AddStr(ExeMemo, "[HDA] Stream fechado.\n");
}

void OnBtnChimeClick(void* sender) {
    if (g_audio_stream_id < 0) return;
    GUI_Memo_AddStr(ExeMemo, "[Audio] Tocando Sino...\n");
    play_chime();
}

void OnBtnSirenClick(void* sender) {
    if (g_audio_stream_id < 0) return;
    GUI_Memo_AddStr(ExeMemo, "[Audio] Tocando Sirene...\n");
    play_siren();
}

void OnBtnBassClick(void* sender) {
    if (g_audio_stream_id < 0) return;
    GUI_Memo_AddStr(ExeMemo, "[Audio] Tocando Baixo...\n");
    play_bass();
}

void OnBtnEffectClick(void* sender) {
    if (g_audio_stream_id < 0) return;
    GUI_Memo_AddStr(ExeMemo, "[Audio] Tocando Efeito...\n");
    play_effect();
}

void OnBtnMixTestClick(void* sender) {
    if (g_audio_stream_id < 0) return;
    GUI_Memo_AddStr(ExeMemo, "[Audio] Tocando Mix de sons...\n");
    play_mix_all();
}

void OnBtnStopAllClick(void* sender) {
    if (g_audio_stream_id >= 0) {
        sys_audio_close(g_audio_stream_id);
        g_audio_stream_id = -1;
        GUI_Memo_AddStr(ExeMemo, "[HDA] Stream encerrado.\n");
    }
}

/* ============================================================================
 * FUNÇÃO PRINCIPAL (MAIN)
 * ============================================================================ */
int main(int argc, char* argv[]) {
    static int ultimo_x = 0, ultimo_y = 0, mouse_hold_timer = 0;
    static bool primeiro_desenho = true, ultimo_estado_foco = false;

    graphics_init_app(winWidth, winHeight);
    wm_init();

    my_app_slot = OS_IPC_RegisterApp("Teste Audio Intel HDA", winWidth, winHeight);
    if (my_app_slot == -1) return -1;

    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "LBF-OS Audio Intel HDA - Ring 3 v3.0", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);

    int btnW = 140, btnH = 30, startY = 35, spacing = 10;
    BtnOpenStream  = GUI_CreateButton(&MyApp, 10, startY, btnW, btnH, "ABRIR STREAM", OnBtnOpenStreamClick);
    BtnCloseStream = GUI_CreateButton(&MyApp, 10 + btnW + spacing, startY, btnW, btnH, "FECHAR STREAM", OnBtnCloseStreamClick);

    int startY2 = startY + btnH + 10;
    BtnChime  = GUI_CreateButton(&MyApp, 10, startY2, btnW, btnH, "1. Sino", OnBtnChimeClick);
    BtnSiren  = GUI_CreateButton(&MyApp, 10 + btnW + spacing, startY2, btnW, btnH, "2. Sirene", OnBtnSirenClick);
    BtnBass   = GUI_CreateButton(&MyApp, 10 + (btnW + spacing) * 2, startY2, btnW, btnH, "3. Baixo", OnBtnBassClick);
    BtnEffect = GUI_CreateButton(&MyApp, 10 + (btnW + spacing) * 3, startY2, btnW, btnH, "4. Efeito", OnBtnEffectClick);

    int startY3 = startY2 + btnH + 10;
    BtnMixTest = GUI_CreateButton(&MyApp, 10, startY3, 200, btnH, "TESTE MIXAGEM", OnBtnMixTestClick);
    BtnStopAll = GUI_CreateButton(&MyApp, 220, startY3, 150, btnH, "PARAR TUDO", OnBtnStopAllClick);

    // Botões de volume (MUTE afastado da borda direita — 520..590)
    BtnVolDown = GUI_CreateButton(&MyApp, 380, startY3, 70, btnH, "VOL -", OnBtnVolDownClick);
    BtnVolUp   = GUI_CreateButton(&MyApp, 455, startY3, 70, btnH, "VOL +", OnBtnVolUpClick);
    BtnMute    = GUI_CreateButton(&MyApp, 520, startY3, 70, btnH, "MUTE", OnBtnMuteClick);

    GUI_CreateLabel(&MyApp, 10, startY3 + btnH + 15, "Console:");
    ExeMemo = GUI_CreateMemo(&MyApp, 10, startY3 + btnH + 35, 600, 250);
    gui_set_prop(ExeMemo, PROP_COLOR, 0x000000);

    GUI_Memo_AddStr(ExeMemo, "[Sistema] Driver Intel HDA carregado.\n");
    GUI_Memo_AddStr(ExeMemo, "[Sistema] Template oficial v3.0 (Kernel v2.8 / Runtime v6.3).\n");
    GUI_Memo_AddStr(ExeMemo, "[Volume] 100%\n");
    Flush_Grafico_Janela();

    while (1) {
        if (IPC_WINDOW_LIST[my_app_slot].is_active == 0) {
            Tratar_Fechamento_Software();
            break;
        }

        bool precisa_redesenhar = false;

        if (primeiro_desenho) {
            primeiro_desenho = false;
            precisa_redesenhar = true;
        }

        bool euTenhoFocoJanelaReal = (IPC_CONTROL->active_focus_slot == my_app_slot);
        if (euTenhoFocoJanelaReal != ultimo_estado_foco) {
            ultimo_estado_foco = euTenhoFocoJanelaReal;
            if (MyApp.MainWindow) ((TForm*)MyApp.MainWindow)->ActiveFocus = euTenhoFocoJanelaReal;
            precisa_redesenhar = true;
        }

        char key = Obter_Tecla_Entrada();
        if (key != 0) {
            GUI_ProcessKeyboard(&MyApp, key);
            precisa_redesenhar = true;
        }

        // Processamento do Mouse
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2;

                if (BtnOpenStream && rel_x >= BtnOpenStream->Left && rel_x < (BtnOpenStream->Left + BtnOpenStream->Width) &&
                    rel_y >= BtnOpenStream->Top && rel_y < (BtnOpenStream->Top + BtnOpenStream->Height)) {
                    gui_set_prop(BtnOpenStream, PROP_STATE, 2);
                }
                else if (BtnCloseStream && rel_x >= BtnCloseStream->Left && rel_x < (BtnCloseStream->Left + BtnCloseStream->Width) &&
                         rel_y >= BtnCloseStream->Top && rel_y < (BtnCloseStream->Top + BtnCloseStream->Height)) {
                    gui_set_prop(BtnCloseStream, PROP_STATE, 2);
                }
                else if (BtnChime && rel_x >= BtnChime->Left && rel_x < (BtnChime->Left + BtnChime->Width) &&
                    rel_y >= BtnChime->Top && rel_y < (BtnChime->Top + BtnChime->Height)) {
                    gui_set_prop(BtnChime, PROP_STATE, 2);
                }
                else if (BtnSiren && rel_x >= BtnSiren->Left && rel_x < (BtnSiren->Left + BtnSiren->Width) &&
                         rel_y >= BtnSiren->Top && rel_y < (BtnSiren->Top + BtnSiren->Height)) {
                    gui_set_prop(BtnSiren, PROP_STATE, 2);
                }
                else if (BtnBass && rel_x >= BtnBass->Left && rel_x < (BtnBass->Left + BtnBass->Width) &&
                    rel_y >= BtnBass->Top && rel_y < (BtnBass->Top + BtnBass->Height)) {
                    gui_set_prop(BtnBass, PROP_STATE, 2);
                }
                else if (BtnEffect && rel_x >= BtnEffect->Left && rel_x < (BtnEffect->Left + BtnEffect->Width) &&
                         rel_y >= BtnEffect->Top && rel_y < (BtnEffect->Top + BtnEffect->Height)) {
                    gui_set_prop(BtnEffect, PROP_STATE, 2);
                }
                else if (BtnMixTest && rel_x >= BtnMixTest->Left && rel_x < (BtnMixTest->Left + BtnMixTest->Width) &&
                         rel_y >= BtnMixTest->Top && rel_y < (BtnMixTest->Top + BtnMixTest->Height)) {
                    gui_set_prop(BtnMixTest, PROP_STATE, 2);
                }
                else if (BtnStopAll && rel_x >= BtnStopAll->Left && rel_x < (BtnStopAll->Left + BtnStopAll->Width) &&
                         rel_y >= BtnStopAll->Top && rel_y < (BtnStopAll->Top + BtnStopAll->Height)) {
                    gui_set_prop(BtnStopAll, PROP_STATE, 2);
                }
                else if (BtnVolDown && rel_x >= BtnVolDown->Left && rel_x < (BtnVolDown->Left + BtnVolDown->Width) &&
                         rel_y >= BtnVolDown->Top && rel_y < (BtnVolDown->Top + BtnVolDown->Height)) {
                    gui_set_prop(BtnVolDown, PROP_STATE, 2);
                }
                else if (BtnVolUp && rel_x >= BtnVolUp->Left && rel_x < (BtnVolUp->Left + BtnVolUp->Width) &&
                         rel_y >= BtnVolUp->Top && rel_y < (BtnVolUp->Top + BtnVolUp->Height)) {
                    gui_set_prop(BtnVolUp, PROP_STATE, 2);
                }
                else if (BtnMute && rel_x >= BtnMute->Left && rel_x < (BtnMute->Left + BtnMute->Width) &&
                         rel_y >= BtnMute->Top && rel_y < (BtnMute->Top + BtnMute->Height)) {
                    gui_set_prop(BtnMute, PROP_STATE, 2);
                }

                events_process_mouse(rel_x, rel_y, 1, 0);

                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) precisa_redesenhar = true;
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }

        if (mouse_hold_timer > 0) {
            mouse_hold_timer--;
            if (mouse_hold_timer == 0) {
                if (BtnOpenStream) gui_set_prop(BtnOpenStream, PROP_STATE, 0);
                if (BtnCloseStream) gui_set_prop(BtnCloseStream, PROP_STATE, 0);
                if (BtnChime)  gui_set_prop(BtnChime, PROP_STATE, 0);
                if (BtnSiren)  gui_set_prop(BtnSiren, PROP_STATE, 0);
                if (BtnBass)   gui_set_prop(BtnBass, PROP_STATE, 0);
                if (BtnEffect) gui_set_prop(BtnEffect, PROP_STATE, 0);
                if (BtnMixTest) gui_set_prop(BtnMixTest, PROP_STATE, 0);
                if (BtnStopAll) gui_set_prop(BtnStopAll, PROP_STATE, 0);
                if (BtnVolDown) gui_set_prop(BtnVolDown, PROP_STATE, 0);
                if (BtnVolUp)   gui_set_prop(BtnVolUp, PROP_STATE, 0);
                if (BtnMute)    gui_set_prop(BtnMute, PROP_STATE, 0);
                events_process_mouse(ultimo_x, ultimo_y, 0, 0);
                precisa_redesenhar = true;
            }
        }

        // Aplica volume pendente FORA do despacho de click (REGRA #5)
        if (pending_volume >= 0) {
            g_volume = pending_volume;
            pending_volume = -1;
            clamp_volume();                                  // 1..100 sempre
            sys_audio_set_volume((uint8_t)g_volume, false);
            print_volume_status();
            precisa_redesenhar = true;
        }

        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32);
    }

    sys_exit();
    return 0;
}
