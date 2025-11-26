#!/usr/bin/env python3
"""Simple smoke test client for the mock server"""
import requests
import sys
BASE = 'http://127.0.0.1:8080'

def run():
    print('GET /')
    r = requests.get(BASE + '/')
    print('status', r.status_code)

    print('GET /status')
    r = requests.get(BASE + '/status')
    print(r.status_code, r.json())

    print('POST /wifi')
    r = requests.post(BASE + '/wifi', json={'ssid':'mock-net','password':'mypass'})
    print(r.status_code, r.json())

    print('POST /upload')
    files = {'file': ('hello.txt', b'hello world')}
    r = requests.post(BASE + '/upload?storage=flash', files=files)
    print(r.status_code, r.json())

    print('POST /upload (multi-file)')
    # send two files under the same field -> mock server should reject
    multi = [('file', ('a.txt', b'one')), ('file', ('b.txt', b'two'))]
    r = requests.post(BASE + '/upload?storage=flash', files=multi)
    print('status', r.status_code, r.json())

    print('DELETE /wifi')
    r = requests.delete(BASE + '/wifi')
    print(r.status_code, r.json())

if __name__ == '__main__':
    run()
