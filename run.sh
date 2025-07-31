#!/bin/bash

# Check if .env file exists
if [ -f .env ]; then
    echo "Loading environment from .env file..."
    # Don't use 'source' to avoid exposing variables in shell
else
    # Check for API key in environment
    if [ -z "$DROPBOX_SIGN_API_KEY" ]; then
        echo "Warning: DROPBOX_SIGN_API_KEY not found in environment"
        echo ""
        echo "To set up your API key securely:"
        echo "1. Copy .env.example to .env"
        echo "   cp .env.example .env"
        echo "2. Edit .env and add your API key"
        echo "3. Never commit .env to version control"
        echo ""
        echo "Get your API key from: https://app.hellosign.com/home/myAccount#api"
        echo ""
        # Don't exit - let the server handle the error
    fi
fi

# Build if not already built
if [ ! -f build/signing-server ]; then
    echo "Building server..."
    ./build.sh
fi

# Run the server (it will load .env internally)
echo "Starting Document Signing Server..."
./build/signing-server