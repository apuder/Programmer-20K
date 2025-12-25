
#include "programmer.h"
#include "serial.h"
#include "flash.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>

static const char *TAG = "Programmer";

bool ProgrammerFlash::flush_sector_buffer() {
    if (sector_fill == 0) {
        return true;
    }

    // Always erase the sector before programming it
    flashSectorErase(sector_base);

    // Program in page-sized chunks (<= FLASH_PAGE_SIZE)
    size_t offset = 0;
    while (offset < sector_fill) {
        size_t chunk = std::min(static_cast<size_t>(FLASH_PAGE_SIZE), sector_fill - offset);
        // Each write stays within the sector and respects the page size limit
        flashWrite(sector_base + offset, sector_buf.data() + offset, static_cast<int>(chunk));
        offset += chunk;
    }

    sector_fill = 0;
    return true;
}

bool ProgrammerFlash::init() {
    current_address = 0;
    sector_base = 0;
    sector_fill = 0;
    
    stop_serial_monitor();

    // UART configuration: 115200-8-N-1
    uart_config_t uart_config = {0};
    uart_config.baud_rate = 115200;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;

    // Apply config
    esp_err_t ret = uart_param_config(UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Map UART signals to GPIOs
    ret = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Install UART driver
    ret = uart_driver_install(UART_PORT, BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return false;
    }

    int i = 0;
    while (i < 10) {
        uint8_t cookie = get_cookie();
        ESP_LOGI(TAG, "Flash programmer cookie: 0x%02X", cookie);
        if (cookie == 0xAF) {
            break;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
        i++;
    }
    if (i == 10) {
        ESP_LOGE(TAG, "Flash programmer bridge not responding");
        uart_driver_delete(UART_PORT);
        start_serial_monitor();
        return false;
    }

    i = 0;
    while (i < 10) {
        unsigned long dev_id = flashReadMfdDevId();
        ESP_LOGI(TAG, "Flash MFG/DEV ID: 0x%06lX (%s)", dev_id, flashMfdDevIdStr(dev_id));
        if (dev_id != 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        i++;
    }
    if (i == 10) {
        ESP_LOGE(TAG, "Flash not responding");
        uart_driver_delete(UART_PORT);
        start_serial_monitor();
        return false;
    }
    return true;
}

ProgrammerFlash::~ProgrammerFlash() {
    uart_driver_delete(UART_PORT);
    start_serial_monitor();
    ESP_LOGI(TAG, "Reloading FPGA");
    JTAGAdapter* jtag = new JTAGAdapter();
    jtag->setIR(RELOAD);
    jtag->setIR(NOOP);
    delete jtag;
}

uint8_t ProgrammerFlash::get_cookie() {
    return spi_get_cookie();
}

bool ProgrammerFlash::begin_upload() {
    current_address = 0;
    sector_base = 0;
    sector_fill = 0;
    return true;
}

bool ProgrammerFlash::write_chunk(const uint8_t* data, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        // Initialize buffer for the current sector
        if (sector_fill == 0) {
            sector_base = (current_address / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
        }

        size_t sector_space = FLASH_SECTOR_SIZE - sector_fill;
        size_t to_copy = std::min(len - offset, sector_space);

        std::memcpy(sector_buf.data() + sector_fill, data + offset, to_copy);
        sector_fill += to_copy;
        current_address += to_copy;
        offset += to_copy;

        // If the sector buffer is full, erase and program it now
        if (sector_fill == FLASH_SECTOR_SIZE) {
            flush_sector_buffer();
        }
    }

    return true;
}

bool ProgrammerFlash::end_upload(bool success) {
    if (!success) {
        // Do not program trailing data on failure
        sector_fill = 0;
        return false;
    }

    // Flush any remaining buffered data (partial sector)
    flush_sector_buffer();
    return true;
}


// ProgrammerJTAG implementation

ProgrammerJTAG::~ProgrammerJTAG() {
    if (jtag_adapter) {
        delete jtag_adapter;
        jtag_adapter = nullptr;
    }
}

bool ProgrammerJTAG::init() {
    if (jtag_adapter == nullptr) {
        jtag_adapter = new JTAGAdapter();
    }

    // Check if 20K is connected
    if (!jtag_adapter->check()) {
        delete jtag_adapter;
        jtag_adapter = nullptr;
        return false;
    }
    return true;
}

bool ProgrammerJTAG::begin_upload() {
    if (jtag_adapter == nullptr) {
        return false;
    }
    return jtag_adapter->programToSRAMBegin();
}

bool ProgrammerJTAG::write_chunk(const uint8_t* data, size_t size) {
    if (jtag_adapter == nullptr) {
        return false;
    }
    return jtag_adapter->programToSRAMWrite(data, size);
}

bool ProgrammerJTAG::end_upload(bool success) {
    if (jtag_adapter == nullptr) {
        return false;
    }
    
    bool result = false;
    if (success) {
        result = jtag_adapter->programToSRAMEnd();
    }
    
    // Clean up the adapter
    delete jtag_adapter;
    jtag_adapter = nullptr;
    
    return result;
}

bool ProgrammerJTAG::uploadBridge() {
    if (jtag_adapter == nullptr) {
        return false;
    }
    return jtag_adapter->uploadBridge();
}