
#include "flash.h"
#include <cstdint>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"



#define FLASH_PAGE_PROGRAM         0x02
#define FLASH_READ                 0x03
#define FLASH_STATUS_REISTER_READ  0x05
#define FLASH_WRITE_ENABLE         0x06
#define FLASH_SECTOR_ERASE         0x20
#define FLASH_READ_MFG_DEV_ID      0x9f

#define FLASH_WIP                  0x01
/* Interface defines */
#define FLASH_CS    0x80
#define FLASH_WPN   0x40


#define FPGA_CMD_GET_COOKIE        0
#define FPGA_CMD_SET_SPI_CTRL_REG  1
#define FPGA_CMD_SET_SPI_DATA      2
#define FPGA_CMD_GET_SPI_DATA      3

#define UART_PORT       UART_NUM_2

uint8_t spi_get_cookie()
{
   uint8_t buf[1] = {FPGA_CMD_GET_COOKIE};
   uart_write_bytes(UART_PORT, (const char *) buf, sizeof(buf));
   int len = uart_read_bytes(UART_PORT, buf, 1, pdMS_TO_TICKS(1000));
   return (len == 1) ? buf[0] : 0xff;
}

static void spi_set_spi_ctrl_reg(uint8_t reg)
{
   uint8_t buf[2] = {FPGA_CMD_SET_SPI_CTRL_REG, reg};
   uart_write_bytes(UART_PORT, (const char *) buf, sizeof(buf));
}

static void spi_set_spi_data(uint8_t data)
{
   uint8_t buf[2] = {FPGA_CMD_SET_SPI_DATA, data};
   uart_write_bytes(UART_PORT, (const char *) buf, sizeof(buf));
}

static uint8_t spi_get_spi_data()
{
   uint8_t buf[1] = {FPGA_CMD_GET_SPI_DATA};
   uart_write_bytes(UART_PORT, (const char *) buf, sizeof(buf));
   int len = uart_read_bytes(UART_PORT, buf, 1, pdMS_TO_TICKS(1000));
   return (len == 1) ? buf[0] : 0xff;
}

static void spiExch(unsigned char *bfr, int n)
{
  while (n-- > 0) {
    spi_set_spi_data(*bfr);
    *bfr = spi_get_spi_data();
    bfr++;
  }
}

static void spiSend(unsigned char *bfr, int n)
{
  while (n-- > 0) {
    spi_set_spi_data(*bfr++);
  }
}

static void spiRecv(unsigned char *bfr, int n)
{
   while(n-- > 0) {
      *bfr++ = spi_get_spi_data();
   }
}

static void flashWriteEnable(void)
{
   unsigned char cmd[1] = { FLASH_WRITE_ENABLE, };

   spi_set_spi_ctrl_reg(FLASH_CS | FLASH_WPN);
   spiSend(cmd, 1);
   spi_set_spi_ctrl_reg(FLASH_WPN);
}

unsigned char flashStatusRegisterRead(void)
{
   unsigned char cmd_dta[2] = { FLASH_STATUS_REISTER_READ, 0, };

   spi_set_spi_ctrl_reg(FLASH_CS | FLASH_WPN);
   spiExch(cmd_dta, 2);
   spi_set_spi_ctrl_reg(FLASH_WPN);

   return cmd_dta[1];
}

void flashRead(unsigned long adr, unsigned char *bfr, int n)
{
   unsigned char cmd_adr[4] =
   {
      FLASH_READ, (uint8_t) ((adr >> 16) & 0xff), (uint8_t) ((adr >> 8) & 0xff), (uint8_t) (adr & 0xff),
   };
   spi_set_spi_ctrl_reg(FLASH_CS | FLASH_WPN);
   spiSend(cmd_adr, 4);
   spiRecv(bfr, n);
   spi_set_spi_ctrl_reg(FLASH_WPN);
}

void flashWrite(unsigned long adr, unsigned char *bfr, int n)
{

   unsigned char cmd_adr[4] =
   {
      FLASH_PAGE_PROGRAM,  (uint8_t) ((adr >> 16) & 0xff), (uint8_t) ((adr >> 8) & 0xff), (uint8_t) (adr & 0xff),
   };

   flashWriteEnable();

   spi_set_spi_ctrl_reg(FLASH_CS | FLASH_WPN);
   spiSend(cmd_adr, 4);
   spiSend(bfr, n);
   spi_set_spi_ctrl_reg(FLASH_WPN);

   while(flashStatusRegisterRead() & FLASH_WIP)
      ;
}

void flashSectorErase(unsigned long adr)
{

   unsigned char cmd_adr[4] =
   {
      FLASH_SECTOR_ERASE,  (uint8_t) ((adr >> 16) & 0xff), (uint8_t) ((adr >> 8) & 0xff), (uint8_t) (adr & 0xff),
   };

   flashWriteEnable();

   spi_set_spi_ctrl_reg(FLASH_CS | FLASH_WPN);
   spiSend(cmd_adr, 4);
   spi_set_spi_ctrl_reg(FLASH_WPN);

   while(flashStatusRegisterRead() & FLASH_WIP)
      ;
}

unsigned long flashReadMfdDevId(void)
{
   unsigned char cmd_dta[4] =
   {
      FLASH_READ_MFG_DEV_ID, 0, 0, 0,
   };

   spi_set_spi_ctrl_reg(FLASH_CS | FLASH_WPN);
   spiExch(cmd_dta, 4);
   spi_set_spi_ctrl_reg(FLASH_WPN);

   return ((unsigned long)cmd_dta[1] << 16) | (cmd_dta[2] << 8) | cmd_dta[3];
}

char *flashMfdDevIdStr(unsigned long mfg_dev_id)
{
   unsigned char mfg_id = mfg_dev_id >> 16;
   unsigned short dev_id = mfg_dev_id;

   if(mfg_id == 0xc2)
   {
      if(dev_id == 0x2016)
         return "Macronix MX25L3233F";
      else
         return "Macronix ?";
   }
   else if(mfg_id == 0x20)
   {
      if(dev_id == 0xba16)
         return "Micron N25Q032A";
      else
         return "Micron ?";
   }
   else if(mfg_id == 0x0b)
   {
      if(dev_id == 0x4016)
         return "XTX XT25F32B";
      else if(dev_id == 0x4017)
         return "XTX XT25F64";
      else
         return "XTX ?";
   }
   else
      return "Unknown";
}
