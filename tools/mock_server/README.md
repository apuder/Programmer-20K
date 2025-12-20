# Programmer-20K Mock Server

This lightweight Python mock server serves the project's `index.html` and implements a simple mock API compatible with the UI.

Features
- Serves `/` and `/index.html` from `main/index.html` so you can test the real UI in your browser
- GET `/status` — returns {mode, ip}
- POST `/wifi` — accepts JSON {ssid,password}, stores credentials into a local mock NVS file and returns {success,connected,ip}
- DELETE `/wifi` — clears stored credentials
- POST `/upload` — accepts multipart/form-data file field `file` and optional `storage` query/form param; writes to `uploads/` and replies with JSON
- WebSocket `/serial` — streams `"Hello Programmer 20K"` every 5 seconds for the Serial Monitor UI

Quick setup

1. Create a virtualenv and install deps:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/mock_server/requirements.txt
```

2. Run the server (from repo root):

```bash
python tools/mock_server/server.py --host 0.0.0.0 --port 8080
```

3. Open the UI in a browser: http://localhost:8080/

API examples

POST /wifi
```bash
curl -X POST -H 'Content-Type: application/json' -d '{"ssid":"mynet","password":"mypass"}' http://localhost:8080/wifi
```

POST /upload (multipart)
```bash
curl -F "file=@example.bin" -F "storage=flash" http://localhost:8080/upload
```

This mock server stores credentials in `tools/mock_server/mock_nvs.json` and saves uploaded files to `tools/mock_server/uploads/`.
