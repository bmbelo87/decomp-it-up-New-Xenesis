#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
export LD_LIBRARY_PATH="$DIR:$LD_LIBRARY_PATH"
chmod +x "$DIR/Pumpy" 2>/dev/null || true
exec "$DIR/Pumpy" "$@"
