#include "qemu/log.h"
#include "hw/ssi/g233_spi.h"



static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned int size)
{

} 

static void g233_spi_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    uint32_t reg_type = addr >> 2;
    switch (reg_type)
    {
    case G233_SPI_CR1:
        s->regs[G233_SPI_CR1] = value & 0x00000044;
        break;
    case G233_SPI_CR2:
        s->regs[G233_SPI_CR2] = value & 0x00000040;
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

        break;
    case G233_SPI_CSCTRL:
        s->regs[G233_SPI_CSCTRL] = value & 0x000000FF;

        break;
    default:
        break;
    }
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
    SysBusDeviceClass *dc = SYS_BUS_DEVICE_CLASS(klass);

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