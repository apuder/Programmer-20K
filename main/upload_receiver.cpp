#include "upload_receiver.h"
#include "esp_log.h"
#include "programmer.h"
#include "serial.h"
#include "event.h"
#include <vector>
#include <cstring>
#include <string>

static const char *TAG = "upload_receiver";
static size_t g_total = 0;
static Programmer* programmer = nullptr;
static bool is_sram = false;

extern "C" bool upload_receiver_init(const char *target, const char *filename_hint)
{
    g_total = 0;
    is_sram = false;
    
    ESP_LOGI(TAG, "upload receiver init: target=%s filename_hint=%s", 
             target ? target : "(null)", filename_hint ? filename_hint : "(none)");
    
    evt_signal(EVT_UPLOAD_START);

    ProgrammerJTAG* jtag_programmer = new ProgrammerJTAG();

    // Check if 20K is connected
    if (!jtag_programmer->init()) {
        ESP_LOGE(TAG, "JTAG programmer initialization failed");
        delete jtag_programmer;
        return false;
    }    

    // Check if this is an FPGA bitstream upload (target hint or filename)
    is_sram = (target && strcmp(target, "sram") == 0);
    ESP_LOGI(TAG, "Target: %s", is_sram ? "sram" : "flash");
    
    if (is_sram) {
        jtag_programmer->begin_upload();
        programmer = jtag_programmer;
        return true;
    }

    // Program to flash
    if (!jtag_programmer->uploadBridge()) {
        ESP_LOGE(TAG, "Failed to upload JTAG bridge to FPGA");
        delete jtag_programmer;
        return false;
    }
    delete jtag_programmer;

    // Initialize flash programmer
    ProgrammerFlash* flash_programmer = new ProgrammerFlash();
    if (!flash_programmer->init()) {
        ESP_LOGE(TAG, "Flash programmer initialization failed");
        delete flash_programmer;
        return false;
    }

    flash_programmer->begin_upload();
    programmer = flash_programmer;
    
    return true;
}

extern "C" size_t upload_receiver_write(const uint8_t *buf, size_t len)
{
    // If FPGA mode is active, write directly to FPGA
    if (programmer->write_chunk(buf, len)) {
        g_total += len;
        ESP_LOGI(TAG, "wrote %zu bytes to FPGA (total: %u)", len, (unsigned)g_total);
        return len;
    } else {
        ESP_LOGE(TAG, "failed to write %zu bytes to FPGA", len);
        return 0;  // signal failure
    }
}

extern "C" void upload_receiver_finish(bool success)
{
    ESP_LOGI(TAG, "upload finished: success=%d bytes_received=%u", 
             success ? 1 : 0, (unsigned) g_total);
    
    evt_signal(success ? EVT_UPLOAD_SUCCESS : EVT_UPLOAD_FAIL);

    // If FPGA mode, end the programming
    if (programmer != nullptr) {
        if (success) {
            if (programmer->end_upload(success)) {
                ESP_LOGI(TAG, "FPGA programming succeeded");
            } else {
                ESP_LOGE(TAG, "FPGA programming end failed");
            }
        } else {
            ESP_LOGI(TAG, "upload failed, FPGA programming cancelled");
        }
        
        delete programmer;
        programmer = nullptr;
    }
}
