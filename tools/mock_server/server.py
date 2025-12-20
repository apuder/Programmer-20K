#!/usr/bin/env python3
"""Simple mock server for Programmer-20K UI.

Serves main/index.html from the repo and provides /status, /wifi, /upload endpoints.
"""

import argparse
import json
import os
import time
from pathlib import Path
from flask import Flask, request, jsonify, send_file, abort
from flask_sock import Sock

ROOT = Path(__file__).resolve().parents[2]
INDEX_PATH = ROOT / 'main' / 'index.html'
MOCK_NVS = Path(__file__).resolve().parents[0] / 'mock_nvs.json'
UPLOAD_DIR = Path(__file__).resolve().parents[0] / 'uploads'
UPLOAD_DIR.mkdir(parents=True, exist_ok=True)

app = Flask('programmer20k-mock')
sock = Sock(app)


def read_store():
    if not MOCK_NVS.exists():
        return {}
    try:
        return json.loads(MOCK_NVS.read_text())
    except Exception:
        return {}


def write_store(obj):
    MOCK_NVS.write_text(json.dumps(obj, indent=2))


def get_wifi_mode():
    """Return 'hotspot' if no credentials stored, 'access' if configured."""
    store = read_store()
    return 'access' if store.get('ssid') else 'hotspot'


@app.route('/')
@app.route('/index.html')
def index():
    if not INDEX_PATH.exists():
        abort(404, 'index.html not found in main/')
    return send_file(INDEX_PATH)



@app.route('/status', methods=['GET'])
def status():
    """Return minimal status without 'connected'."""
    wifi_mode = get_wifi_mode()
    return jsonify({'mode': wifi_mode, 'ip': '192.168.1.42'})


@app.route('/wifi', methods=['POST'])
def wifi_post():
    if not request.is_json:
        return jsonify({'success': False, 'error': 'expected json'}), 400
    body = request.get_json()
    ssid = body.get('ssid')
    password = body.get('password')
    if not ssid:
        return jsonify({'success': False, 'error': 'missing ssid'}), 400

    # store cred and pretend we connected
    store = {'ssid': ssid}
    if password:
        store['password'] = password
    store['ip'] = '192.168.1.100'
    write_store(store)

    return jsonify({'success': True, 'connected': True, 'ip': store['ip']})


@app.route('/wifi', methods=['DELETE'])
def wifi_delete():
    if MOCK_NVS.exists():
        try:
            MOCK_NVS.unlink()
        except Exception:
            pass
    return jsonify({'success': True})


@app.route('/upload', methods=['POST'])
def upload():
    # target from query or form
    target = request.args.get('target') or request.form.get('target') or 'flash'

    # defend: reject multiple files under the same form field (only single-file uploads supported)
    files = request.files.getlist('file')
    if not files:
        return jsonify({'success': False, 'error': 'missing file'}), 400
    if len(files) > 1:
        return jsonify({'success': False, 'error': 'only one file allowed'}), 400

    f = files[0]
    filename = f.filename or 'uploaded.bin'
    # sanitize filename minimally
    filename = os.path.basename(filename)
    target = UPLOAD_DIR / filename

    # stream to disk
    with open(target, 'wb') as fd:
        chunk = f.stream.read(8192)
        total = 0
        while chunk:
            fd.write(chunk)
            total += len(chunk)
            chunk = f.stream.read(8192)

    return jsonify({'success': True, 'message': 'received', 'filename': filename, 'bytes': total, 'target': target})


@sock.route('/serial')
def serial(ws):
    """Mock serial websocket that pushes a heartbeat string every 5 seconds."""
    try:
        while True:
            ws.send("Hello Programmer 20K\n")
            time.sleep(5)
    except Exception:
        # Client disconnected or socket error; just exit the handler
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()

    # Clear WiFi credentials on startup (simulating hotspot mode)
    if MOCK_NVS.exists():
        try:
            MOCK_NVS.unlink()
            print('Cleared WiFi credentials - starting in hotspot mode')
        except Exception as e:
            print(f'Warning: could not clear credentials: {e}')

    print('Mock server starting, serving', INDEX_PATH)
    app.run(host=args.host, port=args.port)


if __name__ == '__main__':
    main()
