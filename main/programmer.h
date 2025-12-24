
#pragma once

#include "jtag.h"
#include "flash.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>

class Programmer {
public:
    virtual ~Programmer() = default;

    /// Initialize the programmer with configuration
    virtual bool init() = 0;

    /// Start receiving a new upload session
    virtual bool begin_upload() = 0;

    /// Write a chunk of data during upload
    virtual bool write_chunk(const uint8_t* data, size_t size) = 0;

    /// Finalize and verify the upload
    virtual bool end_upload(bool success) = 0;
};

class ProgrammerFlash : public Programmer {
private:
    uint32_t current_address = 0;
    uint32_t sector_base = 0;              // Aligned base address for the buffered sector
    size_t sector_fill = 0;                // Bytes currently buffered for the active sector
    std::array<uint8_t, FLASH_SECTOR_SIZE> sector_buf{}; // Holds up to one sector before flushing

    bool flush_sector_buffer();            // Erase + program the buffered sector (page-limited writes)

public:
    ~ProgrammerFlash() override;
    bool init() override;
    uint8_t get_cookie();
    bool begin_upload() override;
    bool write_chunk(const uint8_t* data, size_t size) override;
    bool end_upload(bool success) override;
};

class ProgrammerJTAG : public Programmer {
private:
    JTAGAdapter* jtag_adapter = nullptr;

public:
    ~ProgrammerJTAG() override;
    bool init() override;
    bool begin_upload() override;
    bool write_chunk(const uint8_t* data, size_t size) override;
    bool end_upload(bool success) override;
    bool uploadBridge();
};