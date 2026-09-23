"""Loopback HTTPS fixture for the Miniblink certificate probe.

Usage: python https_server.py CERT_PEM KEY_PEM [PORT]
The certificate and private key are generated in an ignored output directory.
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import sys


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", f"https://127.0.0.1:{self.server.server_port}/")
            self.end_headers()
            return
        body = b"<html><head><title>TLS probe</title></head><body>trusted-local-test</body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: https_server.py CERT_PEM KEY_PEM [PORT]")
    cert = Path(sys.argv[1]).resolve(strict=True)
    key = Path(sys.argv[2]).resolve(strict=True)
    port = int(sys.argv[3]) if len(sys.argv) == 4 else 18443
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"ready 127.0.0.1:{port}", flush=True)
    server.serve_forever()
