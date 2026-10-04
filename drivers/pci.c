#include "pci.h"
#include "io.h"
#include "serial.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVS 64

static pci_device_t devices[PCI_MAX_DEVS];
static int          devices_count = 0;

static uint32_t config_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return (uint32_t)(0x80000000u
        | ((uint32_t)bus  << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (off & 0xFC));
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(PCI_CONFIG_ADDR, config_addr(bus, slot, func, off));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v) {
    outl(PCI_CONFIG_ADDR, config_addr(bus, slot, func, off));
    outl(PCI_CONFIG_DATA, v);
}

static uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, slot, func, off);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

static uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t v = pci_read32(bus, slot, func, off);
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

static void probe_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor = pci_read16(bus, slot, func, 0x00);
    if (vendor == 0xFFFF) return;
    if (devices_count >= PCI_MAX_DEVS) return;

    pci_device_t* d = &devices[devices_count++];
    d->bus         = bus;
    d->slot        = slot;
    d->func        = func;
    d->vendor      = vendor;
    d->device      = pci_read16(bus, slot, func, 0x02);
    d->revision    = pci_read8 (bus, slot, func, 0x08);
    d->prog_if     = pci_read8 (bus, slot, func, 0x09);
    d->subclass    = pci_read8 (bus, slot, func, 0x0A);
    d->class_code  = pci_read8 (bus, slot, func, 0x0B);
    d->header_type = pci_read8 (bus, slot, func, 0x0E) & 0x7F;
    d->irq_line    = pci_read8 (bus, slot, func, 0x3C);

    for (int i = 0; i < 6; i++) {
        d->bar[i] = pci_read32(bus, slot, func, (uint8_t)(0x10 + i * 4));
    }
}

static void probe_slot(uint8_t bus, uint8_t slot) {
    uint16_t vendor = pci_read16(bus, slot, 0, 0x00);
    if (vendor == 0xFFFF) return;

    uint8_t header = pci_read8(bus, slot, 0, 0x0E);

    probe_function(bus, slot, 0);

    if (header & 0x80) {
        for (uint8_t func = 1; func < 8; func++) {
            uint16_t v = pci_read16(bus, slot, func, 0x00);
            if (v != 0xFFFF) probe_function(bus, slot, func);
        }
    }
}

void pci_init(void) {
    devices_count = 0;

    uint8_t header = pci_read8(0, 0, 0, 0x0E);
    if (header & 0x80) {
        for (uint8_t func = 0; func < 8; func++) {
            uint16_t v = pci_read16(0, 0, func, 0x00);
            if (v == 0xFFFF) continue;

            uint8_t class_code = pci_read8(0, 0, func, 0x0B);
            uint8_t subclass   = pci_read8(0, 0, func, 0x0A);
            if (class_code != 0x06 || subclass != 0x04) continue;

            uint8_t sec_bus = pci_read8(0, 0, func, 0x19);
            if (sec_bus == 0) continue;

            for (uint8_t slot = 0; slot < 32; slot++)
                probe_slot(sec_bus, slot);
        }
    }

    for (uint8_t slot = 0; slot < 32; slot++)
        probe_slot(0, slot);
}

int pci_find(uint8_t class_code, uint8_t subclass,
             pci_device_t* out, int max) {
    int n = 0;
    for (int i = 0; i < devices_count && n < max; i++) {
        if (devices[i].class_code != class_code) continue;
        if (subclass != 0xFF && devices[i].subclass != subclass) continue;
        out[n++] = devices[i];
    }
    return n;
}

int pci_find_vendor(uint16_t vendor, uint16_t device,
                    pci_device_t* out, int max) {
    int n = 0;
    for (int i = 0; i < devices_count && n < max; i++) {
        if (devices[i].vendor != vendor) continue;
        if (device != 0xFFFF && devices[i].device != device) continue;
        out[n++] = devices[i];
    }
    return n;
}

int pci_count(void) { return devices_count; }

const pci_device_t* pci_get(int idx) {
    if (idx < 0 || idx >= devices_count) return 0;
    return &devices[idx];
}

const char* pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent IO";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal Processing";
        case 0x12: return "Processing Accel";
        case 0x13: return "Instrumentation";
        case 0x40: return "Coprocessor";
        case 0xFF: return "Unassigned";
        default:   return "Unknown";
    }
}
