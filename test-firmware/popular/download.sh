#!/bin/bash
# Download popular CYD firmware binaries for emulator testing.
# Requires: gh CLI (or swap in curl with the URLs printed below).
set -e
cd "$(dirname "$0")"

echo "== ESP32 Marauder (CYD 2432S028) =="
mkdir -p marauder
gh release download -R justcallmekoko/ESP32Marauder \
    -p 'esp32_marauder_*_cyd_2432S028.bin' -D marauder --clobber
#   manual: https://github.com/justcallmekoko/ESP32Marauder/releases/latest

echo "== NerdMiner v2 (ESP32-2432S028R) =="
mkdir -p nerdminer
gh release download -R BitMaker-hub/NerdMiner_v2 \
    -p 'ESP32-2432S028R_firmware.bin' -D nerdminer --clobber
gh release download -R BitMaker-hub/NerdMiner_v2 \
    -p 'ESP32-2432S028R_factory.bin' -D nerdminer --clobber || true
#   manual: https://github.com/BitMaker-hub/NerdMiner_v2/releases/latest

echo ""
echo "Run examples:"
echo "  ./build/cyd-emulator --firmware test-firmware/popular/marauder/esp32_marauder_*_cyd_2432S028.bin"
echo "  ./build/cyd-emulator --firmware test-firmware/popular/nerdminer/ESP32-2432S028R_factory.bin"
