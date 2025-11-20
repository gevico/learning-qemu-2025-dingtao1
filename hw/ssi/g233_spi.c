#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/g233_spi.h"


static void g233_spi_flush_txfifo(G233SpiState *s)
{
    uint8_t tx;
    uint8_t rx;
    while(!fifo8_is_empty(&s->tx_fifo)){
        tx = fifo8_pop(&s->tx_fifo);
        rx = ssi_transfer(s->spi, tx);
        if(!fifo8_is_full(&s->rx_fifo)){
            fifo8_push(&s->rx_fifo, rx);
        }
        qemu_log("G233_SPI: write 0x%02x to SPI, read 0x%02x\n", tx, rx);
    }

    if(fifo8_is_empty(&s->tx_fifo)){
        s->regs[G233_SPI_SR] |= (1 << 1);
    }
    
    if(!fifo8_is_empty(&s->rx_fifo)){
        s->regs[G233_SPI_SR] |= (1 << 0);
    }
    else{
        s->regs[G233_SPI_SR] &= ~(1 << 0);
    }
}

static void g233_spi_update_irq(G233SpiState *s)
{
    int level = 0;
    // TXE
    // qemu_log("G233_SPI: TXE %d, RXNE %d, CR2_TXEIE %d\n",
    //          s->regs[G233_SPI_SR] & (1 << 1),
    //          s->regs[G233_SPI_SR] & (1 << 0), s->regs[G233_SPI_CR2] & (1 << 7));
    if(s->regs[G233_SPI_SR] & (1 << 1) && s->regs[G233_SPI_CR2] & (1 << 7)){
        level = 1;
    }
    // RXNE
    if(s->regs[G233_SPI_SR] & (1 << 0) && s->regs[G233_SPI_CR2] & (1 << 6)){
        level = 2;
    }
    // ERR
    if((s->regs[G233_SPI_SR] & (1 << 2) || s->regs[G233_SPI_SR] & (1 << 3)) && s->regs[G233_SPI_CR2] & (1 << 5)){
        level = 3;
    }
    if(level){
        qemu_log("G233_SPI: interrupt level %d\n", level);
    }
    qemu_set_irq(s->irq, level);
}

static void g233_spi_update_cs(G233SpiState *s)
{
    uint32_t value = s->regs[G233_SPI_CSCTRL];
    int i;
    for(i = 0; i < 4; i++){
        if(value & (1 << i) && value & (1 << (i + 4))){
            qemu_set_irq(s->cs_lines[i], 0);
        }
        else{
            qemu_set_irq(s->cs_lines[i], 1);
        }
    }

}

static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    G233SpiState *s = G233_SPI(opaque);
    addr >>= 2;
    uint64_t ret = 0;
    switch(addr){
        case G233_SPI_CR1:
            ret = s->regs[G233_SPI_CR1];
        break;
        case G233_SPI_CR2:
            ret = s->regs[G233_SPI_CR2];
        break;
        case G233_SPI_SR:
            ret = s->regs[G233_SPI_SR];
        break;
        case G233_SPI_DR:
            if(!fifo8_is_empty(&s->rx_fifo)){
                ret = fifo8_pop(&s->rx_fifo);
            }
            else{
                qemu_log("G233_SPI: error read from empty RX FIFO\n");
                s->regs[G233_SPI_SR] |= (1 << 2);
            }
            if(fifo8_is_empty(&s->rx_fifo)){
                s->regs[G233_SPI_SR] &= ~(1 << 0);
            }
        break;
        case G233_SPI_CSCTRL:
            ret = s->regs[G233_SPI_CSCTRL];
        break;
    }
    g233_spi_update_irq(s);
    return ret;
} 

static void g233_spi_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    uint32_t reg_type = addr >> 2;
    G233SpiState *s = G233_SPI(opaque);
    qemu_log("G233_SPI: write to reg 0x%08" PRIx64 " with value 0x%08" PRIx64 "\n", addr, value);
    switch (reg_type)
    {
    case G233_SPI_CR1:
        s->regs[G233_SPI_CR1] = value & 0x00000044;
        break;
    case G233_SPI_CR2:
        s->regs[G233_SPI_CR2] = value & 0x000000F0;
        break;
    case G233_SPI_SR:
        if(value & (1 << 2)){
            s->regs[G233_SPI_SR] &= ~(1 << 2);
        }
        if(value & (1 << 3)){
            s->regs[G233_SPI_SR] &= ~(1 << 3);
        }
        break;
    case G233_SPI_DR:
        value = value & 0x000000FF;
        if(!fifo8_is_full(&s->tx_fifo)){
            fifo8_push(&s->tx_fifo, (uint8_t)value);
            s->regs[G233_SPI_SR] &= ~(1 << 1);
            g233_spi_flush_txfifo(s);
        }
        else{
            s->regs[G233_SPI_SR] |= (1 << 3);
        }
        break;
    case G233_SPI_CSCTRL:
        s->regs[G233_SPI_CSCTRL] = value & 0x000000FF;
        g233_spi_update_cs(s);
        break;
    default:
        break;
    }

    g233_spi_update_irq(s);
} 

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    G233SpiState *s = G233_SPI(dev);

    s->spi =ssi_create_bus(dev, "spi");
    sysbus_init_irq(sbd, &s->irq);

    s->cs_lines = g_new(qemu_irq, 4);
    for(int i = 0; i < 4; i++){
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_spi_ops, s, TYPE_G233_SPI, 0x100);
    sysbus_init_mmio(sbd, &s->mmio);
    
    fifo8_create(&s->rx_fifo, 8);
    fifo8_create(&s->tx_fifo, 8);

    s->regs[G233_SPI_CR1] = 0x00000000;
    s->regs[G233_SPI_CR2] = 0x00000000;
    s->regs[G233_SPI_SR] = 0x00000002;
    s->regs[G233_SPI_DR] = 0x0000000C;
    s->regs[G233_SPI_CSCTRL] = 0x00000000;

}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_spi_realize;
}

static void g233_spi_init(Object *obj)
{
    
}

static const TypeInfo g233_spi_info = {
    .name = TYPE_G233_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SpiState),
    .class_init = g233_spi_class_init,
    .instance_init = g233_spi_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}


type_init(g233_spi_register_types)