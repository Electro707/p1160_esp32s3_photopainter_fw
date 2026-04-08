#!/bin/python3
"""
This script runs the webserver in this directory.

It also handles forwarding/receiving requests to/from the esp32 directly, as if it was running
on it. This allows to test the webpage directly without needing to update the esp32

This was partially generated with ClaudeAI, with human oversight
"""
from http.server import HTTPServer, SimpleHTTPRequestHandler
from socketserver import BaseServer
from urllib.parse import urlparse, parse_qs
import threading
import urllib.request
import json
import os
import random

MODE = "sim"
# MODE = "esp32"

ESP32_IP = "http://p1160.local"


class Esp32DeviceHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/api/"):
            self.proxy_request("GET")
        else:
            super().do_GET()  # serve static files normally
    
    def do_POST(self):
        if self.path.startswith("/api/"):
            self.proxy_request("POST")

    def proxy_request(self, method):
        esp_url = ESP32_IP + self.path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else None

        try:
            req = urllib.request.Request(esp_url, data=body, method=method)
            with urllib.request.urlopen(req) as resp:
                data = resp.read()
                self.send_response(resp.status)
                self.send_header("Content-Type", resp.headers.get("Content-Type", "application/json"))
                self.end_headers()
                self.wfile.write(data)
        except Exception as e:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(str(e).encode())

class SimHttpHandlerVars:
    imgList = ["Image1.RAW", "Image2.RAW", "Image3.RAW"]

    @classmethod
    def reset(cls):
        cls.imgList = ["Image1.RAW", "Image2.RAW", "Image3.RAW"]


class SimHttpHandler(SimpleHTTPRequestHandler):
    """A simulated HTTP server to develop the webpage independent of the device"""
    def _json(self, code: int = 200, payload: dict = None):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        if payload is not None:
            self.wfile.write(json.dumps(payload).encode())

    def do_GET(self):
        p = urlparse(self.path).path

        if p == "/api/v1/version":
            return self._json(payload={
                'stat': 'ok',
                'version': 'p1160 Rev 1-test',
            })
        elif p == '/api/v1/wifi/info':
            return self._json(payload={
                'stat': 'ok',
                'currentMode': 'sta',
                'staSSID': 'someSSID',
                'staPass': 'somePass',
            })
        elif p == '/api/v1/mode':
            return self._json(payload={
                'stat': 'ok',
                'mode': "playlist",
                'playlist': {
                    'mode': "all",
                    'duration': 2.0,
                },
            })
        elif p == '/api/v1/pmic':
            return self._json(payload={
                'stat': 'ok',
                'battVolt': 4.2 + random.uniform(-0.5, 0.5),
                'sysVolt': 5.1 + random.uniform(-0.5, 0.5),
                'vBusVolt': 4.2 + random.uniform(-0.5, 0.5),
                'battPercentage': 85 + random.randint(-25, 10),
                'vBusGood': True,
                'battPresent': True,
                'currLimited': False,
            })
        elif p == '/api/v1/img/available':
            return self._json(payload={
                'stat': 'ok',
                'img': SimHttpHandlerVars.imgList,
            })
        else:
            if p.startswith("/api"):
                print(f"UNKNOWN_API: {p}")
            super().do_GET()

    def do_POST(self):
        p = urlparse(self.path).path
        cntLen = int(self.headers.get("Content-Length", 0))
        b = self.rfile.read(cntLen)
        print(f"Mode POST request with url {p} with data {b}")
        self._json(payload={'stat': 'ok'})
        if p == '/api/v1/img/delete':
            dat = json.loads(b)
            SimHttpHandlerVars.imgList.remove(dat['name'])


def main():
    if MODE == 'sim':
        reqH = SimHttpHandler
    elif MODE == 'esp32':
        reqH = Esp32DeviceHandler
    else:
        raise UserWarning("Invalid MODE")
    
    print("Hosting web locally, go to http://localhost:8080")
    server = HTTPServer(("", 8080), reqH)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    try:
        while True:
            cmd = input(">")
            cmd = cmd.lower()
            if cmd == 'quit':
                print("Exiting")
                break
            elif cmd == 'reset':
                print("Resetting image list")
                SimHttpHandlerVars.reset()
            else:
                print("Unknown cmd")
    except KeyboardInterrupt:
        print("Exiting")

    server.shutdown()



if __name__ == "__main__":
    main()