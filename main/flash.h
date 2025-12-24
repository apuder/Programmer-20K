#ifndef _FLASH_H_
#define _FLASH_H_

#include <cstdint>

#define FLASH_SECTOR_SIZE          0x1000 // 4k
#define FLASH_PAGE_SIZE            0x100  // 256 bytes

uint8_t spi_get_cookie();

unsigned char flashStatusRegisterRead(void);
void flashRead(unsigned long adr, unsigned char *bfr, int n);
void flashWrite(unsigned long adr, unsigned char *bfr, int n);
void flashSectorErase(unsigned long adr);
unsigned long flashReadMfdDevId(void);
char *flashMfdDevIdStr(unsigned long mfg_dev_id);

#endif
