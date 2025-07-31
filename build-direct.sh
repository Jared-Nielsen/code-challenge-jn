#!/bin/bash

echo "Building server directly with g++..."

# Create build directory if it doesn't exist
mkdir -p build

# Compile the server
g++ -std=c++17 -O2 -pthread \
    -DCPPHTTPLIB_OPENSSL_SUPPORT \
    -I backend/include \
    backend/src/main.cpp \
    -o build/signing-server \
    -lssl -lcrypto

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Run ./run.sh to start the server"
else
    echo "Build failed. Please check for errors."
    exit 1
fi