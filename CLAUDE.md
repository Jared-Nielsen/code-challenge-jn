# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an embedded document signing web application that integrates with e-signature providers (DocuSign, HelloSign, Adobe Sign, PandaDoc, etc.). The architecture consists of:
- **Frontend**: Single `index.html` file with vanilla HTML, CSS, and JavaScript
- **Backend**: API server handling e-signature provider integration and authentication

## Key Architecture Points

### Frontend Architecture
- Pure HTML/CSS/JavaScript - no frameworks or build tools
- All code must be contained within `index.html`
- Communicates with backend via RESTful API endpoints
- Never handles API credentials directly

### Backend Architecture
- Handles all e-signature provider API authentication
- Manages document/envelope lifecycle
- Provides REST endpoints for:
  - Creating signing sessions
  - Generating embedded signing URLs
  - Retrieving signing status
  - Downloading completed documents

### Security Requirements
- API credentials must only exist on backend
- All provider API calls must originate from backend
- Frontend-backend communication should validate all inputs

## Common Development Tasks

### Running the Application
```bash
# Start the backend server (command depends on chosen language/framework)
# Then open index.html in a browser
```

### Testing Flow
1. Start backend server with one command
2. Open `index.html` in browser
3. Enter signer details (name, email, phone)
4. Click "Next" to proceed to embedded signing
5. Complete signature with autofilled information

## Important Implementation Notes

- The signer information collected in step 1 must be prefilled into the document during signing
- Use embedded signing (iframe/redirect), not email-based signing
- Real-time status updates should reflect the signing progress
- Support downloading the completed signed document