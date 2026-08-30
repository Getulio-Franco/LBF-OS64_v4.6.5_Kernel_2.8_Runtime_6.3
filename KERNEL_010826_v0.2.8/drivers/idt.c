#include <stdint.h>
#include <stddef.h>
#include "idt.h"
#include "drivers/video.h"
#include "drivers/shell/shell.h"
#include "drivers/proc.h"
#include "drivers/hw/serial.h"
#include "mem/vmm.h"
#include "drivers/kernel_try.h"
#include "drivers/audio/hda_dma.h"

extern void kernel_main_return_point(void); 
extern process_t* current_process;
extern void isr_mouse();
extern void isr_timer();
extern void isr_keyboard();
extern void isr_serial();
extern void isr_hda(); // Stub Assembly da interrupção HDA
extern void syscall_int_handler();

// Instância global do Stream de Áudio para o Handler de Interrupção
extern hda_dma_stream_t g_hda_stream;

#define MAX_SERIAL_PORTS 4
extern SerialPort system_serial_ports[MAX_SERIAL_PORTS];

k_exception_frame_t g_k_exception_env = {0};

extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idtr idtr_ptr;

const char *exception_messages[] = {
    "Division By Zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 Float", "Alignment Check", "Machine Check",
    "SIMD Float", "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor Err", "VMM Err",
    "Security", "Reserved"
};

void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_irq_handler(void) {
    driver_serial_handle_interrupt(&system_serial_ports[0]);
}

// Handler da Interrupção de Hardware do Áudio Intel HDA
void hda_irq_handler(void) {
    hda_dma_handle_irq(&g_hda_stream);
}

void exception_handler(registers_t *regs) {
    __asm__ volatile("cli");

    uint64_t user_cr3;
    __asm__ volatile("mov %%cr2, %0" : "=r"(user_cr3));

    const char* error_name = (regs->int_no < 32) ? exception_messages[regs->int_no] : "Unknown Exception";

    if (regs->int_no == 14) {
        uint64_t fault_address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));

        if ((regs->cs & 0x3) != 0) { 
            if (handle_page_fault(user_cr3, fault_address)) {
                return; 
            }
        }
    }

    if ((regs->cs & 0x3) == 0) {
        if (g_k_exception_env.active) {
            g_k_exception_env.active = 0;
            g_k_exception_env.last_error = regs->int_no;

            regs->rip = g_k_exception_env.rip;
            regs->rsp = g_k_exception_env.rsp;
            regs->rbp = g_k_exception_env.rbp;
            regs->rbx = g_k_exception_env.rbx;
            regs->r12 = g_k_exception_env.r12;
            regs->r13 = g_k_exception_env.r13;
            regs->r14 = g_k_exception_env.r14;
            regs->r15 = g_k_exception_env.r15;
            regs->rax = regs->int_no; 

            if (regs->int_no == 14) {
                __asm__ volatile("mov %0, %%cr3" :: "r"((uint64_t)PML4_ADDR) : "memory");
            }

            return;
        }

        vga_print_string("\n!!! KERNEL PANIC FATAL NO RING 0 !!!\nErro: ", 0, 39);
        vga_print_string((char*)error_name, 0, 39);
        while(1) { __asm__ volatile("hlt"); }
    } else {
        vga_print_string("\n[IDT] App de Ring 3 travou: ", 0, 39);
        vga_print_string((char*)error_name, 0, 39);

        if (user_cr3 != PML4_ADDR) {
            __asm__ volatile("mov %0, %%cr3" :: "r"(user_cr3) : "memory");
        }

        process_t* proc = get_current_process();
        if (proc && proc->pid > 2) {
            terminate_current_process();
        } else {
            while(1) { __asm__ volatile("cli; hlt"); }
        }
    }
}

void set_idt_gate(int n, uint64_t handler, uint8_t flags) {
    idt[n].base_low  = (uint16_t)(handler & 0xFFFF);
    idt[n].selector  = 0x08; 
    idt[n].ist       = 0;
    idt[n].flags     = flags; 
    idt[n].base_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].base_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[n].reserved  = 0;
}

void init_idt(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    
    // IRQ4 (Serial) desmascarada na Master (0xE8)
    outb(0x21, 0xE8); 
    // Desmascara IRQ11 (HDA) e IRQ12 (Mouse) no PIC Slave (0xE7 = 1110 0111)
    outb(0xA1, 0xE7); 

    uint64_t isr_pointers[32] = {
        (uint64_t)isr0,  (uint64_t)isr1,  (uint64_t)isr2,  (uint64_t)isr3,
        (uint64_t)isr4,  (uint64_t)isr5,  (uint64_t)isr6,  (uint64_t)isr7,
        (uint64_t)isr8,  (uint64_t)isr9,  (uint64_t)isr10, (uint64_t)isr11,
        (uint64_t)isr12, (uint64_t)isr13, (uint64_t)isr14, (uint64_t)isr15,
        (uint64_t)isr16, (uint64_t)isr17, (uint64_t)isr18, (uint64_t)isr19,
        (uint64_t)isr20, (uint64_t)isr21, (uint64_t)isr22, (uint64_t)isr23,
        (uint64_t)isr24, (uint64_t)isr25, (uint64_t)isr26, (uint64_t)isr27,
        (uint64_t)isr28, (uint64_t)isr29, (uint64_t)isr30, (uint64_t)isr31
    };

    for (int i = 0; i < 32; i++) {
        set_idt_gate(i, isr_pointers[i], 0x8E);
    }

    set_idt_gate(32,  (uint64_t)isr_timer,    0x8E);
    set_idt_gate(33,  (uint64_t)isr_keyboard, 0x8E);
    set_idt_gate(36,  (uint64_t)isr_serial,   0x8E);
    set_idt_gate(43,  (uint64_t)isr_hda,      0x8E); // <-- Vetor IRQ11 para Intel HDA
    set_idt_gate(44,  (uint64_t)isr_mouse,    0x8E); 

    set_idt_gate(128, (uint64_t)syscall_int_handler, 0xEE);

    idtr_ptr.limit = (uint16_t)(sizeof(struct idt_entry) * 256) - 1;
    idtr_ptr.base = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idtr_ptr));
    __asm__ volatile("sti");
}
