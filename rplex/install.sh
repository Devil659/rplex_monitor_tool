#!/bin/bash
# RPLEX Installation Script
echo "[+] Setting up RPLEX System Monitor..."

# Install dependencies
sudo apt update
sudo apt install -y g++ libncurses5-dev libncursesw5-dev

# Compile programs
cd rplex
g++ rplex_monitor.cpp -o rplex.out -std=c++11
g++ rplex_monitor3.cpp -o rplex3.out -std=c++11 -lncurses

# Set executable permissions
chmod +x rplex rplex3 rplex.out rplex3.out

echo "[+] Installation complete! Run with: ./rplex or ./rplex3"
