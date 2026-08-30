/* ============================================================================
ARCHITECTURE: Ring 3 (User Space Network SDK Application)
FILE: http_test.c
DESCRIPTION: Suite de Diagnóstico e Testes Modulares da Pilha de Rede (Ring 3)
CORREÇÃO v2.9: parse_ip e ip_to_str ajustados para Network Byte Order (Big Endian)
               para casar com o MAKE_IP e DHCP/DNS corrigidos.
============================================================================ */
#include "sdk/libgui.h"
#include "../system/graphics.h"
#include "../gui/wm.h"
#include "../system/string.h"
#include "../system/liblib.h"
#include "net_user/net_utils.h"

// Novas Bibliotecas do Sistema (Mecanismo Try/Except, Assert e Conversões)
#include "../system/sysutils.h"
#include "../system/assert.h"
#include "../system/setjmp.h"

// Módulos da Pilha de Rede Ring 3
#include "components/TOS_IPC.h"
#include "net_user/net_interface.h"
#include "net_user/net_poll.h"
#include "net_user/arp.h"
#include "net_user/ip.h"
#include "net_user/icmp.h"
#include "net_user/udp.h"
#include "net_user/tcp.h"
#include "net_user/dhcp.h"
#include "net_user/dns.h"
#include "net_user/socket.h"

// Protótipos de renderização gráfica RAD
void gui_draw_form(TForm* form);
void gui_render_form(TForm* form);
extern void events_process_mouse(int x, int y, int pressed, int button);
extern void* g_focused_control;
extern void GUI_Memo_AddStr(TGUIControl* memo, const char* str);
extern void GUI_Memo_Clear(TGUIControl* memo);

// Variáveis Globais de Janela e Estado
int my_app_slot = -1;
TGUIEnvironment MyApp;
uint32_t g_dns_server = 0x0302000A; // Default DNS (10.0.2.3 em Network Byte Order)
const int winWidth = 620;
const int winHeight = 520;

// Ponteiros de Controle RAD
TGUIControl* EditTarget   = NULL;
TGUIControl* MemoResponse = NULL;
TGUIControl* BtnTest1     = NULL; // Net Interface & MAC
TGUIControl* BtnTest2     = NULL; // ARP Lookup
TGUIControl* BtnTest3     = NULL; // DHCP DORA
TGUIControl* BtnTest4     = NULL; // ICMP Ping
TGUIControl* BtnTest5     = NULL; // DNS Resolve
TGUIControl* BtnTest6     = NULL; // HTTP GET
TGUIControl* BtnClear     = NULL; // Limpar logs

typedef struct {
    uint8_t dummy[sizeof(IPC_WINDOW_LIST[0])];
    volatile uint8_t fila_teclado_virtual;
    volatile uint8_t tem_evento_teclado;
} __attribute__((packed)) AppWindowInfoExtended;

// ============================================================================
// HELPER FUNCTIONS & EXCEPTION LOGGING
// ============================================================================
static void Exibir_Falha_Assert(const char* contexto) {
    char linha_str[16];
    IntToStr(g_last_assert_info.line, linha_str);
    GUI_Memo_AddStr(MemoResponse, "\n[EXCECAO / ASSERT FALHOU] Contexto: ");
    GUI_Memo_AddStr(MemoResponse, contexto);
    GUI_Memo_AddStr(MemoResponse, "\n Expressao: ");
    GUI_Memo_AddStr(MemoResponse, g_last_assert_info.expression);
    GUI_Memo_AddStr(MemoResponse, "\n Arquivo   : ");
    GUI_Memo_AddStr(MemoResponse, g_last_assert_info.file);
    GUI_Memo_AddStr(MemoResponse, " | Linha: ");
    GUI_Memo_AddStr(MemoResponse, linha_str);
    GUI_Memo_AddStr(MemoResponse, "\n Funcao    : ");
    GUI_Memo_AddStr(MemoResponse, g_last_assert_info.function);
    GUI_Memo_AddStr(MemoResponse, "\n[SISTEMA] Execucao capturada com seguranca.\n\n");
}

static size_t safe_strcpy(char* dest, const char* src, size_t max_size) {
    if (!dest || max_size == 0) return 0;
    size_t i = 0;
    if (src) {
        while (src[i] != '\0' && i < (max_size - 1)) {
            dest[i] = src[i];
            i++;
        }
    }
    dest[i] = '\0';
    return i;
}

static size_t safe_strcat(char* dest, const char* src, size_t max_size, size_t current_len) {
    if (!dest || !src || current_len >= max_size) return current_len;
    while (*src && current_len < (max_size - 1)) {
        dest[current_len++] = *src++;
    }
    dest[current_len] = '\0';
    return current_len;
}

// ============================================================================
// CORREÇÃO CRÍTICA v2.9: Network Byte Order (Big Endian)
// ============================================================================

// Converte string "10.0.2.15" para uint32_t em Network Byte Order (0x0A00020F)
static uint32_t parse_ip(const char* ip_str) {
    if (!ip_str) return 0;
    uint32_t bytes[4] = {0};
    int byte_idx = 0;
    int current_val = 0;
    bool has_digit = false;
    
    while (*ip_str) {
        if (*ip_str >= '0' && *ip_str <= '9') {
            current_val = (current_val * 10) + (*ip_str - '0');
            if (current_val > 255) return 0;
            has_digit = true;
        } else if (*ip_str == '.') {
            if (!has_digit || byte_idx >= 3) return 0;
            bytes[byte_idx++] = current_val;
            current_val = 0;
            has_digit = false;
        } else {
            return 0;
        }
        ip_str++;
    }
    if (!has_digit || byte_idx != 3) return 0;
    bytes[3] = current_val;
    
    // ESTILO ANTIGO (wire-correct no x86):
    return (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
}

// Converte uint32_t em Network Byte Order para string "10.0.2.15"
static void ip_to_str(uint32_t ip, char* out_buf) {
    if (!out_buf) return;

    uint8_t b1 = ip & 0xFF;
    uint8_t b2 = (ip >> 8) & 0xFF;
    uint8_t b3 = (ip >> 16) & 0xFF;
    uint8_t b4 = (ip >> 24) & 0xFF;
    
    char temp[16];
    IntToStr(b1, temp);   strcpy(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b2, temp);   strcat(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b3, temp);   strcat(out_buf, temp); strcat(out_buf, ".");
    IntToStr(b4, temp);   strcat(out_buf, temp);
}

void Flush_Grafico_Janela(void) {
    if (my_app_slot < 0 || !MyApp.MainWindow) return;
    gui_draw_form((TForm*)MyApp.MainWindow);
    gui_render_form((TForm*)MyApp.MainWindow);
    OS_IPC_FlipBuffers(my_app_slot, winWidth, winHeight);
}

void Tratar_Fechamento_Software(void) {
    if (my_app_slot < 0) return;
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_VISIBLE, 0);
    uint32_t* b0 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_0;
    uint32_t* b1 = (uint32_t*)(uintptr_t)IPC_WINDOW_LIST[my_app_slot].buffer_ptr_1;
    if (b0) memset(b0, 0, winWidth * winHeight * 4);
    if (b1) memset(b1, 0, winWidth * winHeight * 4);
    IPC_WINDOW_LIST[my_app_slot].is_active = 0;
    sys_sleep(50);
}

char Obter_Tecla_Entrada(void) {
    if (my_app_slot < 0) return 0;
    AppWindowInfoExtended* ext_slot = (AppWindowInfoExtended*)&IPC_WINDOW_LIST[my_app_slot];
    if (ext_slot->tem_evento_teclado == 1) {
        char key = (char)ext_slot->fila_teclado_virtual;
        ext_slot->tem_evento_teclado = 0;
        return key;
    }
    return 0;
}

// ============================================================================
// CALLBACKS DOS BOTÕES DE TESTE (PROTEGIDOS VIA TRY/EXCEPT)
// ============================================================================

// TESTE 1: Interface de Rede e Polling (net_interface / net_poll)
void OnBtnTest1Click(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(MemoResponse, "[TESTE 1] Interface de Rede & Polling Hardware...\n");
    Flush_Grafico_Janela();
    TRY {
        uint8_t mac[6] = {0};
        int status = sys_net_get_mac(mac);
        assert(status == 0);
        char msg[128];
        char hex_byte[8];
        strcpy(msg, "[OK] MAC: ");
        for (int i = 0; i < 6; i++) {
            IntToHex(mac[i], hex_byte, 2);
            strcat(msg, hex_byte + 2);
            if (i < 5) strcat(msg, ":");
        }
        strcat(msg, "\n");
        GUI_Memo_AddStr(MemoResponse, msg);
        for (int i = 0; i < 5; i++) {
            net_poll();
            sys_sleep(10);
        }
        GUI_Memo_AddStr(MemoResponse, "[OK] Loop net_poll() executado com sucesso.\n\n");
    } EXCEPT {
        Exibir_Falha_Assert("Teste 1 (Interface / MAC)");
    }
    Flush_Grafico_Janela();
}

// TESTE 2: Resolução de Endereço ARP (arp.o)
void OnBtnTest2Click(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(MemoResponse, "[TESTE 2] Disparando ARP Request...\n");
    Flush_Grafico_Janela();
    TRY {
        // CONFIGURAÇÃO PARA NAT (10.0.2.x) - IP CORRETO (Big Endian via MAKE_IP)
        uint32_t ip_local = MAKE_IP(10, 0, 2, 15);    // 10.0.2.15
        uint32_t gateway = MAKE_IP(10, 0, 2, 2);      // 10.0.2.2 (Gateway NAT)
        uint32_t netmask = MAKE_IP(255, 255, 255, 0); // 255.255.255.0
        
        ip_set_config(ip_local, netmask, gateway);
        GUI_Memo_AddStr(MemoResponse, "[INFO] Modo NAT (10.0.2.x)\n");
        
        char msg[64];
        strcpy(msg, "[INFO] IP Local configurado para 10.0.2.15\n");
        GUI_Memo_AddStr(MemoResponse, msg);
        
        char gw_msg[64];
        strcpy(gw_msg, "[INFO] Gateway configurado para 10.0.2.2\n");
        GUI_Memo_AddStr(MemoResponse, gw_msg);
        
        // Mostra o IP em formato legível
        char ip_str[32];
        ip_to_str(ip_get_my_ip(), ip_str);
        strcpy(msg, "[DEBUG] ip_get_my_ip() retorna: ");
        strcat(msg, ip_str);
        strcat(msg, "\n");
        GUI_Memo_AddStr(MemoResponse, msg);
        
        // Tenta converter o valor do campo de texto
        char* input = GUI_Edit_GetText(EditTarget);
        uint32_t target_ip = parse_ip(input);
        
        // Se falhou o parse de IP, tenta resolver via DNS
        if (target_ip == 0 && input && input[0] != '\0') {
            target_ip = dns_resolve(input, g_dns_server);
        }
        
        // Caso não haja IP válido, usa o Gateway (10.0.2.2)
        if (target_ip == 0) {
            target_ip = ip_get_gateway();
        }
        if (target_ip == 0) {
            target_ip = MAKE_IP(10, 0, 2, 2);  // Gateway NAT
        }
        
        if (target_ip == 0) {
            GUI_Memo_AddStr(MemoResponse, "[ERRO] IP Alvo inválido!\n\n");
            Flush_Grafico_Janela();
            return;
        }
        
        // Mostra o IP alvo
        char str_ip[32];
        ip_to_str(target_ip, str_ip);
        char msg2[128];
        strcpy(msg2, "     IP Alvo: ");
        strcat(msg2, str_ip);
        strcat(msg2, " (Gateway NAT)\n");
        GUI_Memo_AddStr(MemoResponse, msg2);
        Flush_Grafico_Janela();
        
        // ENVIA ARP REQUEST PARA O GATEWAY
        GUI_Memo_AddStr(MemoResponse, "[ARP] Enviando Request para 10.0.2.2...\n");
        Flush_Grafico_Janela();
        arp_send_request(target_ip);
        GUI_Memo_AddStr(MemoResponse, "[ARP] Request enviado. Aguardando Reply...\n");
        Flush_Grafico_Janela();
        
        // AGUARDA A RESPOSTA ARP (50 tentativas * 20ms = 1 segundo)
        uint8_t dest_mac[6] = {0};
        bool sucesso = false;
        for (int i = 0; i < 50; i++) {
            net_poll();
            if (arp_lookup(target_ip, dest_mac)) {
                sucesso = true;
                break;
            }
            if (i % 10 == 0 && i > 0) {
                char progress[32];
                strcpy(progress, "[ARP] Aguardando... ");
                IntToStr(i * 20, progress + strlen(progress));
                strcat(progress, "ms\n");
                GUI_Memo_AddStr(MemoResponse, progress);
                Flush_Grafico_Janela();
            }
            sys_sleep(20);
        }
        
        // MOSTRA O RESULTADO
        if (sucesso) {
            char mac_msg[128];
            char hex_byte[8];
            strcpy(mac_msg, "[OK] ARP Reply recebido! MAC: ");
            for (int i = 0; i < 6; i++) {
                IntToHex(dest_mac[i], hex_byte, 2);
                strcat(mac_msg, hex_byte + 2);
                if (i < 5) strcat(mac_msg, ":");
            }
            strcat(mac_msg, "\n\n");
            GUI_Memo_AddStr(MemoResponse, mac_msg);
        } else {
            GUI_Memo_AddStr(MemoResponse, "\n[AVISO] Timeout: Nenhuma resposta ARP do gateway (10.0.2.2).\n\n");
            GUI_Memo_AddStr(MemoResponse, "[DICA] Verifique:\n");
            GUI_Memo_AddStr(MemoResponse, "  1. VirtualBox está configurado para NAT\n");
            GUI_Memo_AddStr(MemoResponse, "  2. Driver E1000 inicializou corretamente\n");
            GUI_Memo_AddStr(MemoResponse, "  3. O RX está funcionando (veja os logs do kernel)\n");
            GUI_Memo_AddStr(MemoResponse, "  4. O IP está configurado como 10.0.2.15\n\n");
            
            char ip_atual[32];
            ip_to_str(ip_get_my_ip(), ip_atual);
            strcpy(msg, "[DEBUG] IP atual: ");
            strcat(msg, ip_atual);
            strcat(msg, "\n");
            GUI_Memo_AddStr(MemoResponse, msg);
        }
    } EXCEPT {
        Exibir_Falha_Assert("Teste 2 (ARP Lookup)");
    }
    Flush_Grafico_Janela();
}

// TESTE 3: Configuração Dinâmica DHCP (dhcp.o)
void OnBtnTest3Click(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(MemoResponse, "[TESTE 3] Solicitando configuracao via DHCP (DORA)...\n");
    Flush_Grafico_Janela();
    TRY {
        uint8_t mac[6] = {0};
        int mac_res = sys_net_get_mac(mac);
        assert(mac_res == 0);
        
        dhcp_config_t dhcp_cfg;
        if (dhcp_request_ip(mac, &dhcp_cfg) == 0) {
            ip_set_config(dhcp_cfg.ip, dhcp_cfg.netmask, dhcp_cfg.gateway);
            if (dhcp_cfg.dns_server != 0) g_dns_server = dhcp_cfg.dns_server;
            
            char str_ip[32], str_gw[32];
            ip_to_str(dhcp_cfg.ip, str_ip);
            ip_to_str(dhcp_cfg.gateway, str_gw);
            
            GUI_Memo_AddStr(MemoResponse, "[OK] DHCP Sucesso!\n");
            GUI_Memo_AddStr(MemoResponse, "     IP Atribuido: ");
            GUI_Memo_AddStr(MemoResponse, str_ip);
            GUI_Memo_AddStr(MemoResponse, "\n     Gateway IP : ");
            GUI_Memo_AddStr(MemoResponse, str_gw);
            GUI_Memo_AddStr(MemoResponse, "\n\n");
        } else {
            GUI_Memo_AddStr(MemoResponse, "[ERRO] DHCP DORA falhou. Mantendo IP static fallback.\n\n");
        }
    } EXCEPT {
        Exibir_Falha_Assert("Teste 3 (DHCP DORA)");
    }
    Flush_Grafico_Janela();
}

// TESTE 4: Diagnóstico ICMP Echo / Ping (icmp.o)
void OnBtnTest4Click(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(MemoResponse, "[TESTE 4] Enviando ICMP Echo Request (Ping)...\n");
    Flush_Grafico_Janela();
    TRY {
        char* input = GUI_Edit_GetText(EditTarget);
        uint32_t target_ip = parse_ip(input);
        if (target_ip == 0) target_ip = ip_get_gateway();
        if (target_ip == 0) target_ip = MAKE_IP(10, 0, 2, 2);  // Usa MAKE_IP em vez de 0x0202000A
        
        assert(target_ip != 0);
        
        const char ping_data[] = "PING_TEST";
        uint16_t ping_len = sizeof(ping_data) - 1;
        icmp_send_echo_request(target_ip, 1, 101, ping_data, ping_len);
        
        GUI_Memo_AddStr(MemoResponse, "[ICMP] Pacote Echo disparado via IP. Processando RX...\n");
        Flush_Grafico_Janela();
        
        for (int i = 0; i < 15; i++) {
            net_poll();
            sys_sleep(20);
        }
        GUI_Memo_AddStr(MemoResponse, "[OK] Ciclo de recepcao ICMP finalizado.\n\n");
    } EXCEPT {
        Exibir_Falha_Assert("Teste 4 (ICMP Ping)");
    }
    Flush_Grafico_Janela();
}

// TESTE 5: Resolução de Nomes DNS (dns.o)
void OnBtnTest5Click(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(MemoResponse, "[TESTE 5] Resolvendo Nome de Dominio via UDP/DNS...\n");
    Flush_Grafico_Janela();
    TRY {
        char* domain = GUI_Edit_GetText(EditTarget);
        if (!domain || domain[0] == '\0') domain = "google.com";
        assert(g_dns_server != 0);
        
        uint32_t resolved_ip = dns_resolve(domain, g_dns_server);
        if (resolved_ip != 0) {
            char ip_buf[32];
            ip_to_str(resolved_ip, ip_buf);
            GUI_Memo_AddStr(MemoResponse, "[OK] DNS Resolvido com sucesso -> IP: ");
            GUI_Memo_AddStr(MemoResponse, ip_buf);
            GUI_Memo_AddStr(MemoResponse, "\n\n");
        } else {
            GUI_Memo_AddStr(MemoResponse, "[ERRO] Falha ao resolver dominio informado.\n\n");
        }
    } EXCEPT {
        Exibir_Falha_Assert("Teste 5 (DNS Resolve)");
    }
    Flush_Grafico_Janela();
}

// TESTE 6: Socket TCP e Requisição HTTP Completa
void OnBtnTest6Click(void* sender) {
    (void)sender;
    GUI_Memo_AddStr(MemoResponse, "[TESTE 6] Iniciando Requisicao HTTP GET...\n");
    Flush_Grafico_Janela();
    TRY {
        char* target_str = GUI_Edit_GetText(EditTarget);
        if (!target_str || target_str[0] == '\0') target_str = "google.com";
        
        uint32_t target_ip = parse_ip(target_str);
        if (target_ip == 0) {
            target_ip = dns_resolve(target_str, g_dns_server);
            if (target_ip == 0) {
                GUI_Memo_AddStr(MemoResponse, "[ERRO] DNS Falhou para HTTP GET.\n\n");
                Flush_Grafico_Janela();
                return;
            }
        }
        
        int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(sockfd >= 0);
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(80);
        server_addr.sin_addr.s_addr = target_ip;
        
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            GUI_Memo_AddStr(MemoResponse, "[ERRO] Falha ao conectar Socket TCP.\n\n");
            close(sockfd);
            Flush_Grafico_Janela();
            return;
        }
        
        char request[256];
        size_t len = safe_strcpy(request, "GET / HTTP/1.1\r\nHost: ", sizeof(request));
        len = safe_strcat(request, target_str, sizeof(request), len);
        len = safe_strcat(request, "\r\nUser-Agent: TOS-Test/1.0\r\nConnection: close\r\n\r\n", sizeof(request), len);
        
        send(sockfd, request, len, 0);
        
        char rx_buf[256];
        int bytes = 0;
        int timeout = 200;
        while (timeout > 0) {
            bytes = recv(sockfd, rx_buf, sizeof(rx_buf) - 1, 0);
            if (bytes > 0) {
                rx_buf[bytes] = '\0';
                GUI_Memo_AddStr(MemoResponse, rx_buf);
                Flush_Grafico_Janela();
                timeout = 100;
            } else {
                sys_sleep(10);
                timeout--;
            }
        }
        close(sockfd);
        GUI_Memo_AddStr(MemoResponse, "\n[OK] Conexao TCP encerrada.\n\n");
    } EXCEPT {
        Exibir_Falha_Assert("Teste 6 (Socket TCP / HTTP GET)");
    }
    Flush_Grafico_Janela();
}

void OnBtnClearClick(void* sender) {
    (void)sender;
    GUI_Memo_Clear(MemoResponse);
    Flush_Grafico_Janela();
}

// ============================================================================
// INICIALIZAÇÃO DA INTERFACE RAD E LOOP PRINCIPAL
// ============================================================================
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    static int ultimo_x = 0, ultimo_y = 0;
    static int mouse_hold_timer = 0;
    static bool primeiro_desenho = true;
    static bool ultimo_estado_foco = false;
    void* ultimo_controle_focado = NULL;
    
    graphics_init_app(winWidth, winHeight);
    wm_init();
    
    my_app_slot = OS_IPC_RegisterApp("Suite Testes Redes Ring 3", winWidth, winHeight);
    if (my_app_slot == -1) return -1; 
    
    graphics_set_slot(my_app_slot);
    GUI_InitApplication(&MyApp, my_app_slot, "Suite de Diagnostico de Rede - Ring 3", winWidth, winHeight);
    if (MyApp.MainWindow) gui_set_prop(MyApp.MainWindow, PROP_COLOR, 0x000000);
    
    // Linha superior: Input Alvo e Botão Limpar
    GUI_CreateLabel(&MyApp, 10, 32, "Host / IP Alvo:");
    EditTarget = GUI_CreateEdit(&MyApp, 10, 50, 440, 28, "google.com", NULL);
    BtnClear   = GUI_CreateButton(&MyApp, 460, 50, 150, 28, "LIMPAR LOGS", OnBtnClearClick);
    
    // Linha 1 de Testes
    BtnTest1   = GUI_CreateButton(&MyApp, 10,  88, 190, 30, "1. MAC / Polling", OnBtnTest1Click);
    BtnTest2   = GUI_CreateButton(&MyApp, 210, 88, 190, 30, "2. ARP Request",  OnBtnTest2Click);
    BtnTest3   = GUI_CreateButton(&MyApp, 410, 88, 200, 30, "3. DHCP (DORA)",   OnBtnTest3Click);
    
    // Linha 2 de Testes
    BtnTest4   = GUI_CreateButton(&MyApp, 10,  124, 190, 30, "4. ICMP Ping",    OnBtnTest4Click);
    BtnTest5   = GUI_CreateButton(&MyApp, 210, 124, 190, 30, "5. Resolver DNS", OnBtnTest5Click);
    BtnTest6   = GUI_CreateButton(&MyApp, 410, 124, 200, 30, "6. Requisicao HTTP", OnBtnTest6Click);
    
    // Área de Output (Memo)
    GUI_CreateLabel(&MyApp, 10, 162, "Log de Execucao em Tempo Real:");
    MemoResponse = GUI_CreateMemo(&MyApp, 10, 180, 600, 330);
    gui_set_prop(MemoResponse, PROP_COLOR, 0x000000);
    
    g_focused_control = (void*)EditTarget;
    ultimo_controle_focado = (void*)EditTarget;
    gui_set_prop(EditTarget, PROP_SET_FOCUS, 1);
    
    Flush_Grafico_Janela();
    
    while(1) {
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
        
        if (g_focused_control != NULL) {
            ultimo_controle_focado = g_focused_control;
        } else if (ultimo_controle_focado != NULL) {
            g_focused_control = ultimo_controle_focado;
            gui_set_prop((TGUIControl*)ultimo_controle_focado, PROP_SET_FOCUS, 1);
        }
        
        char key = Obter_Tecla_Entrada();
        if (key != 0) {
            GUI_ProcessKeyboard(&MyApp, key); 
            precisa_redesenhar = true; 
        }
        
        if (IPC_WINDOW_LIST[my_app_slot].has_click_event == 1) {
            if (mouse_hold_timer == 0) {
                int rel_x = IPC_WINDOW_LIST[my_app_slot].local_click_x;
                int rel_y = IPC_WINDOW_LIST[my_app_slot].local_click_y;
                ultimo_x = rel_x;
                ultimo_y = rel_y;
                mouse_hold_timer = 2; 
                
                events_process_mouse(rel_x, rel_y, 1, 0);
                if (GUI_ProcessMouseClick(&MyApp, rel_x, rel_y)) {
                    precisa_redesenhar = true;
                    if (g_focused_control != NULL) ultimo_controle_focado = g_focused_control;
                }
            }
            IPC_WINDOW_LIST[my_app_slot].has_click_event = 0;
        }
        
        if (mouse_hold_timer > 0) {
            mouse_hold_timer--; 
            if (mouse_hold_timer == 0) {
                events_process_mouse(ultimo_x, ultimo_y, 0, 0); 
                precisa_redesenhar = true;
            }
        }
        
        if (precisa_redesenhar) Flush_Grafico_Janela();
        sys_sleep(euTenhoFocoJanelaReal ? 16 : 32); 
    }
    
    sys_exit(); 
    return 0;
}
