#!/bin/bash
# Simple local server script for testing
# Usage: ./serve.sh [port]
# Default port is 8000

PORT=${1:-8000}

echo "Starting local server on http://localhost:$PORT"
echo "Press Ctrl+C to stop"
echo ""

# Try python3 first, then python, then node
if command -v python3 &> /dev/null; then
    python3 -m http.server $PORT
elif command -v python &> /dev/null; then
    python -m http.server $PORT
elif command -v node &> /dev/null; then
    npx http-server -p $PORT
else
    echo "Error: No suitable server found."
    echo "Please install Python 3 or Node.js"
    exit 1
fi
