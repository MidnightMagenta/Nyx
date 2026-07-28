#include <asi/io.h>
#include <nyx/early_serial.h>
#include <nyx/errno.h>
#include <nyx/linkage.h>
#include <nyx/string.h>
#include <nyx/types.h>

#define COM1_PORT     0x3F8
#define COM1_REG(reg) (u16)(COM1_PORT + reg)

#define COM_R_RX_BUFF                      0
#define COM_W_TX_BUFF                      0
#define COM_RW_INTERUPT_ENABLE_REG         1
#define COM_RW_LEAST_SIGNIFICANT_BYTE_BAUD 0
#define COM_RW_MOST_SIGNIFICANT_BYTE_BAUD  1
#define COM_R_INT_ID                       2
#define COM_W_FIFO_CONTROL_REG             2
#define COM_RW_LINE_CONTROL_REG            3
#define COM_RW_MODEM_CONTROL_REG           4
#define COM_R_LINE_STATUS_REG              5
#define COM_R_MODEM_STATUS_REG             6
#define COM_RW_SCRATCH                     7

static bool __initdata early_serial_initialized = false;

int __init early_serial_init() {
    if (early_serial_initialized) { return 0; }

    outb(COM1_REG(COM_RW_INTERUPT_ENABLE_REG), 0x00);
    outb(COM1_REG(COM_RW_LINE_CONTROL_REG), 0x80);
    outb(COM1_REG(COM_RW_LEAST_SIGNIFICANT_BYTE_BAUD), 0x03);
    outb(COM1_REG(COM_RW_MOST_SIGNIFICANT_BYTE_BAUD), 0x00);
    outb(COM1_REG(COM_RW_LINE_CONTROL_REG), 0x03);
    outb(COM1_REG(COM_W_FIFO_CONTROL_REG), 0xC7);
    outb(COM1_REG(COM_RW_MODEM_CONTROL_REG), 0x1E);

    outb(COM1_REG(COM_W_TX_BUFF), 0xAE);
    if (inb(COM1_REG(COM_R_RX_BUFF)) != 0xAE) { return -ENODEV; }

    outb(COM1_REG(COM_RW_MODEM_CONTROL_REG), 0x0F);
    early_serial_initialized = true;
    return 0;
}

inline static bool __init early_serial_is_tx_empty() {
    return inb(COM1_REG(COM_R_LINE_STATUS_REG)) & 0x20;
}

void __init early_serial_putc(char c) {
    if (!early_serial_initialized) {
        if (early_serial_init() < 0) { return; }
    }

    if (c == '\n') { early_serial_putc('\r'); }
    while (!early_serial_is_tx_empty());
    outb(COM1_REG(COM_W_TX_BUFF), (u8) c);
}

static int __init early_serial_write_str(const char *s, int len) {
    for (int i = 0; i < len; ++i) { early_serial_putc(s[i]); }
    return len;
}

int __init early_serial_write(const char *s) {
    int len = strlen(s);
    early_serial_write_str(s, len);
    return len;
}
