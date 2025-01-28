from http.server import HTTPServer, SimpleHTTPRequestHandler

class CustomHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # Add required headers
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

# Start the server
port = 8000
server = HTTPServer(("0.0.0.0", port), CustomHandler)
print(f"Serving on port {port}")
server.serve_forever()
