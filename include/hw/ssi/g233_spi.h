#ifndef __G233_SPI_H__
#define __G233_SPI_H__
#include "qemu/fifo8.h"
#include "hw/sysbus.h"

#define TYPE_G233_SPI "g233.spi"
#define G233_SPI(obj) \
    OBJECT_CHECK(G233SpiState, (obj), TYPE_G233_SPI)

enum{
    G233_SPI_CR1 = 0x0,
    G233_SPI_CR2,
    G233_SPI_SR,
    G233_SPI_DR,
    G233_SPI_CSCTRL,
    G233_REG_NUM
};

typedef struct G233SpiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;


    qemu_irq irq;
    qemu_irq *cs_lines;

    SSIBus *spi;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint32_t regs[G233_REG_NUM];


} G233SpiState;


#endif