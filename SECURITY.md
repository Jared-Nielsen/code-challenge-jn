# Security Best Practices

This document outlines the security measures implemented in this application to protect sensitive data, particularly the Dropbox Sign API key.

## API Key Protection

### 1. Environment Variables
- API keys are **never** hardcoded in source code
- Keys are loaded from environment variables or `.env` files
- The `.env` file is excluded from version control via `.gitignore`

### 2. .env File Setup
```bash
# Copy the template
cp .env.example .env

# Edit .env and add your actual API key
# NEVER commit this file to version control
```

### 3. Key Validation
The backend validates API keys to prevent common mistakes:
- Rejects empty keys
- Rejects placeholder keys like "your_api_key_here"
- Validates key format (alphanumeric with dashes/underscores)
- Masks keys in logs (shows only first/last 4 characters)

### 4. Frontend Security
- API key is **never** sent to or stored in the frontend
- All API calls to Dropbox Sign are made from the backend
- Frontend communicates only with our backend API
- CORS is configured to allow only necessary origins

## Additional Security Measures

### Request Logging
When enabled via `ENABLE_REQUEST_LOGGING=true`:
- Logs are sanitized to mask sensitive data
- Email addresses are partially masked (e.g., `jo***@example.com`)
- API keys are never logged in full
- Request bodies over 1KB are not logged

### Input Validation
- Email addresses are validated using regex
- All required fields are checked for non-empty values
- File uploads are restricted to PDF format
- Session IDs are randomly generated

### HTTPS Recommendations
For production deployment:
1. Always use HTTPS for the server
2. Configure proper SSL/TLS certificates
3. Enable HSTS headers
4. Use secure cookie flags if implementing authentication

### Git Security Checklist
Before committing:
- [ ] Ensure `.env` is in `.gitignore`
- [ ] Check no API keys are in code
- [ ] Review all configuration files
- [ ] Verify no sensitive data in logs

### Environment Variable Best Practices
1. **Development**: Use `.env` file locally
2. **CI/CD**: Use secure environment variable storage
3. **Production**: Use platform-specific secret management:
   - AWS: AWS Secrets Manager or Parameter Store
   - Azure: Azure Key Vault
   - GCP: Secret Manager
   - Heroku: Config Vars
   - Docker: Docker Secrets

### Monitoring Recommendations
1. Monitor for exposed keys in version control
2. Rotate API keys regularly
3. Use API key scoping if available
4. Monitor unusual API usage patterns
5. Set up alerts for failed authentication attempts

## Security Incident Response
If an API key is exposed:
1. Immediately revoke the compromised key
2. Generate a new API key
3. Update all environments with the new key
4. Review logs for any unauthorized usage
5. Remove the exposed key from version history if needed

## Additional Resources
- [Dropbox Sign Security](https://www.hellosign.com/security)
- [OWASP API Security Top 10](https://owasp.org/www-project-api-security/)
- [Git Secret Scanning](https://docs.github.com/en/code-security/secret-scanning)