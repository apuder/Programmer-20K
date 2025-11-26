#include "upload_receiver.h"
#include "esp_log.h"
#include "jtag.h"
#include <vector>
#include <cstring>
#include <string>

static const char *TAG = "upload_receiver";
static size_t g_total = 0;
static JTAGAdapter* g_jtag_adapter = nullptr;
static bool g_fpga_mode = false;

extern "C" bool upload_receiver_init(const char *target, const char *filename_hint)
{
    g_total = 0;
    g_fpga_mode = false;
    
    ESP_LOGI(TAG, "upload receiver init: target=%s filename_hint=%s", 
             target ? target : "(null)", filename_hint ? filename_hint : "(none)");
    
    // Check if this is an FPGA bitstream upload (target hint or filename)
    bool is_sram = (target && strcmp(target, "sram") == 0);
    
    if (is_sram) {
        // Instantiate JTAG adapter and begin FPGA programming
        if (g_jtag_adapter == nullptr) {
            g_jtag_adapter = new JTAGAdapter();
        }
        
        if (g_jtag_adapter->programToSRAMBegin()) {
            g_fpga_mode = true;
            ESP_LOGI(TAG, "FPGA programming mode initialized");
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to begin FPGA programming");
            delete g_jtag_adapter;
            g_jtag_adapter = nullptr;
            return false;
        }
    }
    
    return true;
}

extern "C" size_t upload_receiver_write(const uint8_t *buf, size_t len)
{
    // If FPGA mode is active, write directly to FPGA
    if (g_fpga_mode && g_jtag_adapter != nullptr) {
        if (g_jtag_adapter->programToSRAMWrite(buf, len)) {
            g_total += len;
            ESP_LOGI(TAG, "wrote %zu bytes to FPGA (total: %u)", len, (unsigned)g_total);
            return len;
        } else {
            ESP_LOGE(TAG, "failed to write %zu bytes to FPGA", len);
            return 0;  // signal failure
        }
    }
    
    // Default behaviour: accept and count, but do not store
    g_total += len;
    return len;
}

extern "C" void upload_receiver_finish(bool success)
{
    ESP_LOGI(TAG, "upload finished: success=%d bytes_received=%u", 
             success ? 1 : 0, (unsigned)g_total);
    
    // If FPGA mode, end the programming
    if (g_fpga_mode && g_jtag_adapter != nullptr) {
        if (success) {
            if (g_jtag_adapter->programToSRAMEnd()) {
                ESP_LOGI(TAG, "FPGA programming succeeded");
            } else {
                ESP_LOGE(TAG, "FPGA programming end failed");
            }
        } else {
            ESP_LOGI(TAG, "upload failed, FPGA programming cancelled");
        }
        
        delete g_jtag_adapter;
        g_jtag_adapter = nullptr;
        g_fpga_mode = false;
    }
}
