# Programmer-20K ESP32 — Web API reference (compact)

This file documents the POST and related endpoints used by the inlined frontend at `main/index.html`.

1) POST /wifi
  - Description: configure station Wi‑Fi credentials.
  - Content-Type: application/json
  - Request example:
    {
      "ssid": "MyHomeWiFi",
      "password": "secretpass"
    }
  - Success: 200 OK, body: { "success": true, "connected": true, "ip": "192.168.1.23" }
  - Failure: 4xx/5xx, body: { "success": false, "error": "informational" }

2) DELETE /wifi
  - Description: forget/clear saved Wi‑Fi credentials (optional but supported by UI as "Forget").
  - Success: 200 { "success": true }

3) GET /status
  - Description: simple status probe used by the UI to check whether the device is connected.
  - Success: 200 { "connected": true, "ip": "192.168.1.23", "mode": "access" }
  - Fields:
    - connected: boolean indicating if 20K device is connected
    - ip: (optional) IP address when connected
    - mode: "hotspot" (no WiFi credentials configured) or "access" (connected to access point)
  - Note: The WiFi configuration section is hidden in the UI when mode is "access"

4) POST /upload
  - Description: upload a single file via multipart/form-data. The UI posts each file individually.
  - Method: POST
  - Content-Type: multipart/form-data
  - Form fields:
    - file — (required) binary file to store
    - target — (optional) "flash" or "sram" (UI will set this inline and/or via query param)
    - name — (optional) filename override
  - Query example: POST /upload?target=flash
  - Successful reply (example):
    HTTP/1.1 200 OK
    { "success": true, "message": "received", "filename": "example.bin" }
  - Errors: Reply with appropriate 4xx/5xx and a JSON body: { "success": false, "error": "message" }
