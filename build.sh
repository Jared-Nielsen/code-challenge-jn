#!/bin/bash

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make -j$(nproc)

echo "Build complete! Run ./run.sh to start the server"