#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CLASS_STORAGE_IDE   0x01
#define PCI_CLASS_SERIAL_USB    0x0C
#define PCI_CLASS_NETWORK       0x02

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor;
    uint16_t device;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint8_t  irq_line;
    uint32_t bar[6];
} pci_device_t;

void     pci_init(void);

uint32_t pci_read32 (uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v);

int      pci_find(uint8_t class_code, uint8_t subclass,
                  pci_device_t* out, int max);

int      pci_find_vendor(uint16_t vendor, uint16_t device,
                         pci_device_t* out, int max);

int      pci_count(void);
const pci_device_t* pci_get(int idx);

const char* pci_class_name(uint8_t class_code);

#endif
