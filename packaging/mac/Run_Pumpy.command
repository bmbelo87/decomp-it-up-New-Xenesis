#!/bin/bash
# Script de inicialização sem bloqueios do macOS Gatekeeper
DIR="$(cd "$(dirname "$0")" && pwd)"
echo "Configurando permissões do Pumpy..."
xattr -cr "$DIR/Pumpy.app" 2>/dev/null || true
xattr -cr "$DIR/Pumpy" 2>/dev/null || true
chmod +x "$DIR/Pumpy.app/Contents/MacOS/Pumpy" 2>/dev/null || true
chmod +x "$DIR/Pumpy" 2>/dev/null || true
echo "Iniciando Pumpy..."
open "$DIR/Pumpy.app"
