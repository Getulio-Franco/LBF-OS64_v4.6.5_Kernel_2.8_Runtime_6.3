# LBF-OS64_v4.6.5_Kernel_2.8_Runtime_6.3
SISTEMA OPERACIONAL  x86-64 BITS

**Kernel v2.9 | Runtime v6.3**

<p align="center">
  <em>"Pelo povo, para o povo e com o povo"</em>
</p>

---

## 🎉 Duas Grandes Conquistas em Um Lançamento

É com imensa alegria que anunciamos a maior atualização de hardware do LBF-OS até hoje! Após meses de engenharia reversa, depuração de baixo nível e desenvolvimento do zero, o **LBF-OS64 v4.6.5** traz dois subsistemas completos:

- 🎵 **Intel High Definition Audio (HDA)** — Áudio profissional com mixer multicanal e streaming contínuo
- 🌐 **Intel E1000 / 82545EM** — Pilha TCP/IP completa com acesso à internet

Esqueça os bipes simples do PC Speaker e as mensagens de "rede não disponível". O LBF-OS agora tem **voz** e está **conectado ao mundo**!

---

## 🎵 Driver Intel HDA — Áudio Profissional

### O que o Driver faz? (Arquitetura Ring 0)

O subsistema de áudio foi projetado para ser robusto, seguro e livre de Kernel Panics:

1. **Descoberta PCI e MMIO** (`hda_pci.c`)  
   Varre o barramento PCI (Class `0x04`, Subclass `0x03`), mapeia BAR0 e executa Global Reset.

2. **Transport Layer CORB/RIRB** (`hda_corb_rirb.c`)  
   Anéis de comando e resposta para comunicação assíncrona com o Codec.

3. **Enumeração de Codec** (`hda_codec.c`)  
   Descobre a árvore de nós (Audio Function Group), localiza DAC e PIN de saída, gerencia estados de energia (D0) e configura amplificadores.

4. **Motor DMA com Double Buffering** (`hda_dma.c`)  
   Buffer Descriptor List (BDL) com IRQ 11 e esquema Ping-Pong (A/B) para reprodução contínua sem glitches.

5. **Audio Server & Mixer** (`audio_server.c`)  
   Gerencia até 4 streams simultâneos com mixer digital integrado e buffers estáticos alinhados (eliminando Page Faults no Ring 3).

### Guia para Programadores (Ring 3)

**API Oficial** (`liblib.h`):
```c
int  sid = sys_audio_open();                  // Abre stream
int  w   = sys_audio_write(sid, buf, bytes);  // Envia PCM
void sys_audio_close(sid);                    // Fecha stream
void sys_audio_set_volume(pct, mute);         // Volume 1-100
