
// TN653E
// http://cdn.gowinsemi.com.cn/TN653E.pdf


///////////////////////////////////////////////////////////
#include "esp_log.h"
#include <stdio.h>
#include <assert.h>
#include "jtag.h"


extern const uint8_t _binary_bridge_bin_start[] asm("_binary_bridge_bin_start");
extern const uint8_t _binary_bridge_bin_end[]   asm("_binary_bridge_bin_end");


void JTAG_SendDataMSB(char* p, int bitlength, bool exit);



int JTAGAdapter::determineChainLength()
{
  int i;

  // Fill chain with 0s
  for(i = 0; i < 1000; i++) {
    pulse(0);
  }

  // Fill chain with 1s and stop when the first 1 is read at TDO
  for(i = 0; i < 1000; i++) {
    if (pulse(TDI)) break;
  }
  exitShift();
  return i;
}

int JTAGAdapter::scan(int expected_ir_len, int expected_devices, uint32_t expected_idcode)
{
  reset();
  enterShiftIR();
  if (determineChainLength() != expected_ir_len) {
    return ERR_JTAG_UNEXPECTED_IR_LENGTH;
  }

  // we are in BYPASS mode since JTAG_DetermineChainLength filled the IR chain full of 1's
  // now we can easily determine the number of devices (= DR chain length when all the devices are in BYPASS mode)
  enterShiftDR();
  int devices = determineChainLength();
  if (devices != expected_devices) {
    return ERR_JTAG_UNEXPECTED_DEVICES;
  }

  // read the IDCODEs (assume all devices support IDCODE, so read 32 bits per device)
  reset();
  uint32_t idcode[devices];
  readDR(idcode, 32 * devices);
  for(int i = 0; i < devices; i++) {
    if (idcode[i] != expected_idcode) {
      return ERR_JTAG_UNEXPECTED_IDCODE;
    }
  }
  return 0;
}

uint32_t JTAGAdapter::readStatusReg()
{
  uint32_t reg;
  uint8_t rx[4];

  setIR(STATUS_REGISTER);
  readDR(rx, sizeof(rx) * 8);
  reg = rx[3] << 24 | rx[2] << 16 | rx[1] << 8 | rx[0];
  return reg;
}

bool JTAGAdapter::pollFlag(uint32_t mask, uint32_t value) {
  uint32_t status;
  long timeout = 0;

  do {
    status = readStatusReg();
    if (timeout == 100000000){
      return false;
    }
    timeout++;
  } while ((status & mask) != value);

  return true;
}

bool JTAGAdapter::enableCfg()
{
  setIR(CONFIG_ENABLE);
  return pollFlag(STATUS_SYSTEM_EDIT_MODE, STATUS_SYSTEM_EDIT_MODE);
}

bool JTAGAdapter::disableCfg()
{
  setIR(CONFIG_DISABLE);
  setIR(NOOP);
  return pollFlag(STATUS_SYSTEM_EDIT_MODE, 0);
}

/* Erase SRAM:
 * TN653 p.9-10, 14 and 31
 */
bool JTAGAdapter::eraseSRAM()
{
  setIR(ERASE_SRAM);
  setIR(NOOP);

  /* TN653 specifies to wait for 4ms with
  * clock generated but
  * status register bit MEMORY_ERASE goes low when ERASE_SRAM
  * is send and goes high after erase
  * this check seems enough
  */
  if (pollFlag(STATUS_MEMORY_ERASE, STATUS_MEMORY_ERASE)) {
    return true;
  } else {
    return false;
  }
}

bool JTAGAdapter::check()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  
  setIR(READ_IDCODE);
  uint32_t id;
  readDR(&id, 32);
  bool result = (id == 0x081b);
  
  xSemaphoreGiveRecursive(mutex);
  return result;
}

bool JTAGAdapter::programToSRAMBegin()
{
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  
  if (!check()) {
    xSemaphoreGiveRecursive(mutex);
    return false;
  }

  /* The following instructions are not documented. The documented process
   * of programming SRAM does not work when the content of the flash is
   * corrupted (e.g., a CRC error). The following instructions were
   * reverse-engineered by capturing the JTAG protocol observing the
   * Gowin programmer program the SRAM.
   */
  setIR(CONFIG_DISABLE);
  setIR(0);
  ESP_LOGI("JTAG", "Status reg: %04x", readStatusReg());
  setIR(READ_IDCODE);
  setIR(CONFIG_ENABLE);
  setIR(RELOAD);
  setIR(NOOP);
  setIR(CONFIG_DISABLE);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  setIR(READ_IDCODE);
  setIR(NOOP);

  /* erase SRAM */
  if (!enableCfg()) {
    return false;
  }
  if (!eraseSRAM()) {
    return false;
  }
  if (!disableCfg()) {
    return false;
  }

  /* load bitstream in SRAM */
  if (!enableCfg()) {
    return false;
  }

  setIR(INIT_ADDR);
  setIR(XFER_WRITE);  // Transfer Configuration Data
  enterShiftDR();
  return true;
}

bool JTAGAdapter::programToSRAMEnd()
{
  pulse(TMS);
  pulse(0);  // go back to Run-Test/Idle

  setIR(XFER_DONE);  // XFER_DONE
  if (!pollFlag(STATUS_DONE_FINAL, STATUS_DONE_FINAL)) {
    xSemaphoreGiveRecursive(mutex);
    return false;
  }    
  bool ok = disableCfg();
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  
  xSemaphoreGiveRecursive(mutex);
  return ok;
}

bool JTAGAdapter::programToSRAMWrite(const uint8_t* data, size_t len)
{
  sendDataMSB((void*) data, len * 8, false);
  return true;
}

void JTAGAdapter::setup()
{
  gpio_config_t gpioConfig;

  gpioConfig.pin_bit_mask = TCK | TMS | TDI;
  gpioConfig.mode = GPIO_MODE_OUTPUT;
  gpioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
  gpioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpioConfig.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&gpioConfig);

  gpioConfig.pin_bit_mask = TDO;
  gpioConfig.mode = GPIO_MODE_INPUT;
  gpio_config(&gpioConfig);
}

bool JTAGAdapter::uploadBridge()
{
  size_t bridge_size = _binary_bridge_bin_end - _binary_bridge_bin_start;
  ESP_LOGI("JTAG", "Uploading bridge of size %u bytes", (unsigned)bridge_size);
  if (!programToSRAMBegin()) {
    ESP_LOGE("JTAG", "programToSRAMBegin() failed");
    return false;
  }
  if (!programToSRAMWrite(_binary_bridge_bin_start, bridge_size)) {
    ESP_LOGE("JTAG", "programToSRAMWrite() failed");
    return false;
  }
  if (!programToSRAMEnd()) {
    ESP_LOGE("JTAG", "programToSRAMEnd() failed");
    return false;
  }
  ESP_LOGI("JTAG", "Bridge upload completed");
  return true;
}

uint8_t reverse(uint8_t v)
{
  uint8_t b = 0;
  for(int i = 0; i < 8; i++) {
    b <<= 1;
    b |= (v & 1);
    v >>= 1;
  }
  return b;
}
