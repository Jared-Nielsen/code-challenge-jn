#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>
#include <map>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <regex>
#include <algorithm>
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;
using namespace httplib;

// Simple .env file loader
void load_env_file(const std::string& filename = ".env") {
    std::ifstream file(filename);
    if (!file.is_open()) {
        // Try parent directory
        file.open("../" + filename);
        if (!file.is_open()) {
            return; // .env file is optional
        }
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Find the = sign
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes if present
            if (value.length() >= 2 && 
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.length() - 2);
            }
            
            // Set environment variable if not already set
            if (std::getenv(key.c_str()) == nullptr) {
                #ifdef _WIN32
                    _putenv_s(key.c_str(), value.c_str());
                #else
                    setenv(key.c_str(), value.c_str(), 1);
                #endif
            }
        }
    }
}

struct SigningSession {
    std::string id;
    std::string signature_request_id;
    std::string signature_id;
    std::string status;
    json signer_info;
    json boldsign_response;
    int64_t created_at;
};

class DocumentSigningServer {
private:
    Server server;
    std::map<std::string, SigningSession> signing_sessions;
    std::mutex sessions_mutex;
    std::string api_key;
    std::string client_id;
    std::string signature_provider;
    bool is_demo_mode = false;
    
    std::string generate_session_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(100000, 999999);
        return std::to_string(dis(gen));
    }

    void setup_cors(Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    }
    
    void log_request(const Request& req, const std::string& action, bool include_body = false) {
        if (std::getenv("ENABLE_REQUEST_LOGGING") != nullptr) {
            std::cout << "[" << std::chrono::system_clock::now().time_since_epoch().count() << "] "
                      << req.method << " " << req.path << " - " << action;
            
            // Never log sensitive headers or API keys
            if (include_body && req.body.length() > 0 && req.body.length() < 1000) {
                // Parse and sanitize body
                try {
                    auto body = json::parse(req.body);
                    if (body.contains("email")) {
                        // Mask email
                        std::string email = body["email"];
                        size_t at_pos = email.find('@');
                        if (at_pos != std::string::npos && at_pos > 2) {
                            body["email"] = email.substr(0, 2) + "***" + email.substr(at_pos);
                        }
                    }
                    std::cout << " - Body: " << body.dump();
                } catch (...) {
                    std::cout << " - Body: [parse error]";
                }
            }
            std::cout << std::endl;
        }
    }

    bool validate_email(const std::string& email) {
        const std::regex pattern(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
        return std::regex_match(email, pattern);
    }

    std::string read_file_to_string(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        std::string content((std::istreambuf_iterator<char>(file)), 
                           std::istreambuf_iterator<char>());
        return content;
    }

    json call_signature_api(const std::string& endpoint, const std::string& method, 
                            const UploadFormDataItems& items = {}, 
                            const json& json_body = json()) {
        if (signature_provider == "boldsign") {
            return call_boldsign_api(endpoint, method, items, json_body);
        } else {
            return call_dropbox_sign_api(endpoint, method, items, json_body);
        }
    }
    
    json call_boldsign_api(const std::string& endpoint, const std::string& method,
                          const UploadFormDataItems& items = {},
                          const json& json_body = json()) {
        // In demo mode, return mock responses
        if (is_demo_mode) {
            if (endpoint == "/v1/document/send") {
                return {
                    {"documentId", "demo_doc_" + generate_session_id()}
                };
            } else if (endpoint.find("/v1/document/getEmbeddedSignLink") == 0) {
                return {
                    {"signLink", {
                        {"signUrl", "data:text/html;base64," + base64_encode(
                            "<html><body style='font-family:Arial;text-align:center;padding:50px;'>"
                            "<h1>Demo Signing Interface</h1>"
                            "<p>In a real implementation, this would be the BoldSign embedded signing interface.</p>"
                            "<p>The document would be pre-filled with your information.</p>"
                            "<button onclick='window.parent.postMessage(\"signing_complete\", \"*\")' "
                            "style='padding:10px 20px;font-size:16px;background:#3498db;color:white;border:none;border-radius:5px;cursor:pointer;'>"
                            "Complete Signing (Demo)</button>"
                            "</body></html>"
                        )}
                    }}
                };
            } else if (endpoint.find("/v1/document/properties") == 0) {
                return {
                    {"status", "Completed"}
                };
            }
        }
        
        // Real BoldSign API call
        httplib::SSLClient cli("api.boldsign.com");
        
        Headers headers;
        headers.emplace("X-API-KEY", api_key);
        Result res;
        
        if (method == "POST" && !items.empty()) {
            res = cli.Post(endpoint.c_str(), headers, items);
        } else if (method == "POST" && !json_body.empty()) {
            headers.emplace("Content-Type", "application/json");
            res = cli.Post(endpoint.c_str(), headers, json_body.dump(), "application/json");
        } else if (method == "GET") {
            res = cli.Get(endpoint.c_str(), headers);
        }
        
        if (res && res->status == 200) {
            return json::parse(res->body);
        } else if (res && res->status == 201) {
            return json::parse(res->body);
        } else if (res) {
            std::string error_msg = "API call failed: Status " + std::to_string(res->status);
            if (!res->body.empty()) {
                error_msg += " - " + res->body;
            }
            throw std::runtime_error(error_msg);
        } else {
            throw std::runtime_error("API call failed: Network error");
        }
    }
    
    json call_dropbox_sign_api(const std::string& endpoint, const std::string& method, 
                              const UploadFormDataItems& items = {}, 
                              const json& json_body = json()) {
        // In demo mode, return mock responses
        if (is_demo_mode) {
            if (endpoint == "/v3/signature_request/create_embedded") {
                return {
                    {"signature_request", {
                        {"signature_request_id", "demo_request_" + generate_session_id()},
                        {"signatures", {{
                            {"signature_id", "demo_sig_" + generate_session_id()}
                        }}}
                    }}
                };
            } else if (endpoint.find("/v3/embedded/sign_url/") == 0) {
                // Return a demo signing URL that shows a message
                return {
                    {"embedded", {
                        {"sign_url", "data:text/html;base64," + base64_encode(
                            "<html><body style='font-family:Arial;text-align:center;padding:50px;'>"
                            "<h1>Demo Signing Interface</h1>"
                            "<p>In a real implementation, this would be the Dropbox Sign embedded signing interface.</p>"
                            "<p>The document would be pre-filled with your information.</p>"
                            "<button onclick='window.parent.postMessage(\"signing_complete\", \"*\")' "
                            "style='padding:10px 20px;font-size:16px;background:#3498db;color:white;border:none;border-radius:5px;cursor:pointer;'>"
                            "Complete Signing (Demo)</button>"
                            "</body></html>"
                        )}
                    }}
                };
            } else if (endpoint.find("/v3/signature_request/") == 0) {
                // Check if enough time has passed to simulate signing
                return {
                    {"signature_request", {
                        {"is_complete", true}  // For demo, always return complete
                    }}
                };
            }
        }
        
        // Real API call
        httplib::SSLClient cli("api.hellosign.com");
        cli.set_basic_auth(api_key.c_str(), "");
        
        Headers headers;
        Result res;
        
        if (method == "POST" && !items.empty()) {
            res = cli.Post(endpoint.c_str(), headers, items);
        } else if (method == "POST" && !json_body.empty()) {
            headers.emplace("Content-Type", "application/json");
            res = cli.Post(endpoint.c_str(), headers, json_body.dump(), "application/json");
        } else if (method == "GET") {
            res = cli.Get(endpoint.c_str(), headers);
        }
        
        if (res && res->status == 200) {
            return json::parse(res->body);
        } else if (res && res->status == 201) {
            return json::parse(res->body);
        } else {
            std::string error_msg = "API call failed: ";
            if (res) {
                error_msg += "Status " + std::to_string(res->status) + " - " + res->body;
            } else {
                error_msg += "Network error";
            }
            throw std::runtime_error(error_msg);
        }
    }

    std::string get_file_binary(const std::string& request_id) {
        if (signature_provider == "boldsign") {
            // BoldSign download endpoint
            httplib::SSLClient cli("api.boldsign.com");
            
            Headers headers;
            headers.emplace("X-API-KEY", api_key);
            
            std::string endpoint = "/v1/document/download?documentId=" + request_id;
            auto res = cli.Get(endpoint.c_str(), headers);
            
            if (res && res->status == 200) {
                return res->body;
            } else if (res) {
                throw std::runtime_error("Failed to download file: Status " + std::to_string(res->status));
            } else {
                throw std::runtime_error("Failed to download file: Network error");
            }
        } else {
            // Dropbox Sign download
            httplib::SSLClient cli("api.hellosign.com");
            cli.set_basic_auth(api_key.c_str(), "");
            
            std::string endpoint = "/v3/signature_request/files/" + request_id + "?file_type=pdf";
            auto res = cli.Get(endpoint.c_str());
            
            if (res && res->status == 200) {
                return res->body;
            } else {
                throw std::runtime_error("Failed to download file");
            }
        }
    }

    bool is_valid_api_key(const std::string& key) {
        // Basic validation: not empty, not the example key, reasonable length
        if (key.empty() || key == "your_api_key_here" || key.length() < 20) {
            return false;
        }
        
        // Check if it contains only valid characters (alphanumeric, dash, underscore, base64 chars)
        return std::all_of(key.begin(), key.end(), [](char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '+' || c == '/' || c == '=';
        });
    }
    
    std::string mask_api_key(const std::string& key) {
        if (key.length() <= 8) return "***";
        return key.substr(0, 4) + "..." + key.substr(key.length() - 4);
    }
    
    std::string base64_encode(const std::string& input) {
        static const char* base64_chars = 
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
        
        std::string output;
        int i = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];
        int in_len = input.size();
        const unsigned char* bytes_to_encode = reinterpret_cast<const unsigned char*>(input.c_str());
        
        while (in_len--) {
            char_array_3[i++] = *(bytes_to_encode++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;
                
                for(i = 0; i < 4; i++)
                    output += base64_chars[char_array_4[i]];
                i = 0;
            }
        }
        
        if (i) {
            for(int j = i; j < 3; j++)
                char_array_3[j] = '\0';
            
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            
            for (int j = 0; j < i + 1; j++)
                output += base64_chars[char_array_4[j]];
            
            while((i++ < 3))
                output += '=';
        }
        
        return output;
    }

public:
    DocumentSigningServer() {
        // Load .env file if it exists
        load_env_file();
        
        // Determine signature provider
        const char* env_provider = std::getenv("SIGNATURE_PROVIDER");
        signature_provider = env_provider ? env_provider : "dropbox";
        
        // Load API key based on provider
        const char* env_api_key = nullptr;
        if (signature_provider == "boldsign") {
            env_api_key = std::getenv("BOLDSIGN_API_KEY");
            if (!env_api_key) {
                throw std::runtime_error(
                    "BOLDSIGN_API_KEY not found. Please set it in .env file or environment variable.\n"
                    "See .env.example for template."
                );
            }
        } else {
            env_api_key = std::getenv("DROPBOX_SIGN_API_KEY");
            if (!env_api_key) {
                throw std::runtime_error(
                    "DROPBOX_SIGN_API_KEY not found. Please set it in .env file or environment variable.\n"
                    "See .env.example for template."
                );
            }
        }
        
        api_key = env_api_key;
        
        // Validate API key
        if (!is_valid_api_key(api_key)) {
            std::string error_msg = "Invalid API key format.\n";
            error_msg += "Key length: " + std::to_string(api_key.length()) + "\n";
            if (api_key.length() < 20) {
                error_msg += "Key is too short (minimum 20 characters)\n";
            }
            // Check for invalid characters
            for (char c : api_key) {
                if (!std::isalnum(c) && c != '-' && c != '_' && c != '+' && c != '/' && c != '=') {
                    error_msg += "Invalid character found: '" + std::string(1, c) + "' (ASCII: " + std::to_string((int)c) + ")\n";
                    break;
                }
            }
            throw std::runtime_error(error_msg);
        }
        
        // Load client ID for Dropbox Sign embedded signing
        if (signature_provider == "dropbox") {
            const char* env_client_id = std::getenv("DROPBOX_SIGN_CLIENT_ID");
            if (env_client_id) {
                client_id = env_client_id;
                std::cout << "Client ID loaded for embedded signing" << std::endl;
            } else {
                std::cout << "Warning: DROPBOX_SIGN_CLIENT_ID not found. Embedded signing may not work properly." << std::endl;
                client_id = api_key;
            }
        }
        
        // Check if we're in demo mode
        is_demo_mode = (api_key.find("demo") != std::string::npos || 
                       api_key.find("test") != std::string::npos);
        
        if (is_demo_mode) {
            std::cout << "Running in DEMO mode - API calls will be simulated" << std::endl;
        }
        
        std::cout << "Using " << signature_provider << " as signature provider" << std::endl;
        std::cout << "API Key loaded: " << mask_api_key(api_key) << std::endl;
    }

    void setup_routes() {
        // Serve static files
        server.set_mount_point("/", "./public");
        
        // Handle CORS preflight
        server.Options("/api/.*", [this](const Request& req, Response& res) {
            setup_cors(res);
            res.status = 204;
        });
        
        // Create signing session
        server.Post("/api/sessions", [this](const Request& req, Response& res) {
            setup_cors(res);
            log_request(req, "Create signing session", true);
            
            try {
                auto body = json::parse(req.body);
                
                // Validate required fields
                if (!body.contains("name") || !body.contains("email") || !body.contains("phone")) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Missing required fields\"}", "application/json");
                    return;
                }
                
                std::string name = body["name"];
                std::string email = body["email"];
                std::string phone = body["phone"];
                
                // Validate inputs
                if (name.empty() || email.empty() || phone.empty()) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Fields cannot be empty\"}", "application/json");
                    return;
                }
                
                if (!validate_email(email)) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Invalid email format\"}", "application/json");
                    return;
                }
                
                // Read sample PDF
                std::string pdf_content;
                try {
                    pdf_content = read_file_to_string("./backend/sample.pdf");
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content("{\"error\":\"Sample PDF not found\"}", "application/json");
                    return;
                }
                
                std::string signature_request_id;
                std::string signature_id;
                
                if (signature_provider == "boldsign") {
                    // Create document for BoldSign
                    json request_body = {
                        {"title", "Document for Signing"},
                        {"message", "Please sign this document"},
                        {"signers", {{
                            {"name", name},
                            {"emailAddress", email},
                            {"signerOrder", 1},
                            {"signerType", "Signer"},
                            {"formFields", {
                                {
                                    {"fieldType", "Textbox"},
                                    {"pageNumber", 1},
                                    {"bounds", {{"x", 100}, {"y", 200}, {"width", 200}, {"height", 20}}},
                                    {"isRequired", true},
                                    {"id", "name_field"},
                                    {"value", name}
                                },
                                {
                                    {"fieldType", "Textbox"},
                                    {"pageNumber", 1},
                                    {"bounds", {{"x", 100}, {"y", 250}, {"width", 200}, {"height", 20}}},
                                    {"isRequired", true},
                                    {"id", "email_field"},
                                    {"value", email}
                                },
                                {
                                    {"fieldType", "Textbox"},
                                    {"pageNumber", 1},
                                    {"bounds", {{"x", 100}, {"y", 300}, {"width", 200}, {"height", 20}}},
                                    {"isRequired", true},
                                    {"id", "phone_field"},
                                    {"value", phone}
                                },
                                {
                                    {"fieldType", "Signature"},
                                    {"pageNumber", 1},
                                    {"bounds", {{"x", 100}, {"y", 400}, {"width", 200}, {"height", 60}}},
                                    {"isRequired", true},
                                    {"id", "signature_field"}
                                }
                            }}
                        }}},
                        {"disableEmails", true},  // For embedded signing
                        {"files", json::array({
                            "data:application/pdf;base64," + base64_encode(pdf_content)
                        })}
                    };
                    
                    json api_response = call_signature_api("/v1/document/send", "POST", {}, request_body);
                    
                    // Log the response for debugging
                    std::cout << "BoldSign create response: " << api_response.dump() << std::endl;
                    
                    // Extract document ID from response
                    if (api_response.contains("documentId")) {
                        signature_request_id = api_response["documentId"];
                    } else {
                        throw std::runtime_error("No documentId in BoldSign response");
                    }
                    
                    signature_id = signature_request_id; // BoldSign uses documentId for both
                } else {
                    // Create multipart form data for Dropbox Sign API
                    UploadFormDataItems items = {
                        {"test_mode", "1", "", ""},
                        {"client_id", client_id, "", ""},
                        {"subject", "Document for Signing", "", ""},
                        {"message", "Please sign this document", "", ""},
                        {"signers[0][email_address]", email, "", ""},
                        {"signers[0][name]", name, "", ""},
                        {"file[0]", pdf_content, "sample.pdf", "application/pdf"},
                        {"form_fields_per_document", json::array({
                            json::array({
                                {{"api_id", "name_field"}, {"name", "Name"}, {"type", "text"}, 
                                 {"x", 100}, {"y", 200}, {"width", 200}, {"height", 20}, 
                                 {"required", true}, {"signer", 0}, {"page", 1}, {"value", name}},
                                {{"api_id", "email_field"}, {"name", "Email"}, {"type", "text"}, 
                                 {"x", 100}, {"y", 250}, {"width", 200}, {"height", 20}, 
                                 {"required", true}, {"signer", 0}, {"page", 1}, {"value", email}},
                                {{"api_id", "phone_field"}, {"name", "Phone"}, {"type", "text"}, 
                                 {"x", 100}, {"y", 300}, {"width", 200}, {"height", 20}, 
                                 {"required", true}, {"signer", 0}, {"page", 1}, {"value", phone}},
                                {{"api_id", "signature_field"}, {"name", "Signature"}, {"type", "signature"}, 
                                 {"x", 100}, {"y", 400}, {"width", 200}, {"height", 60}, 
                                 {"required", true}, {"signer", 0}, {"page", 1}}
                            })
                        }).dump(), "", ""}
                    };
                    
                    json api_response = call_signature_api("/v3/signature_request/create_embedded", "POST", items);
                    signature_request_id = api_response["signature_request"]["signature_request_id"];
                    signature_id = api_response["signature_request"]["signatures"][0]["signature_id"];
                }
                
                // Create session
                std::string session_id = generate_session_id();
                SigningSession session;
                session.id = session_id;
                session.signature_request_id = signature_request_id;
                session.signature_id = signature_id;
                session.status = "pending";
                session.signer_info = {{"name", name}, {"email", email}, {"phone", phone}};
                session.created_at = std::chrono::system_clock::now().time_since_epoch().count();
                
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    signing_sessions[session_id] = session;
                }
                
                json response = {
                    {"session_id", session_id}
                };
                
                res.set_content(response.dump(), "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                json error_response = {{"error", e.what()}};
                res.set_content(error_response.dump(), "application/json");
            }
        });
        
        // Get signing URL
        server.Post("/api/sessions/:id/signing-url", [this](const Request& req, Response& res) {
            setup_cors(res);
            
            std::string session_id = req.path_params.at("id");
            
            try {
                SigningSession session;
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    auto it = signing_sessions.find(session_id);
                    if (it == signing_sessions.end()) {
                        res.status = 404;
                        res.set_content("{\"error\":\"Session not found\"}", "application/json");
                        return;
                    }
                    session = it->second;
                }
                
                // Get embedded signing URL
                std::string sign_url;
                
                if (signature_provider == "boldsign") {
                    // BoldSign processes documents asynchronously, add a small delay
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    
                    // BoldSign uses query parameters
                    std::cout << "Getting BoldSign signing URL for document: " << session.signature_request_id << std::endl;
                    std::cout << "Signer email: " << session.signer_info["email"].get<std::string>() << std::endl;
                    
                    // URL encode the email
                    std::string email = session.signer_info["email"].get<std::string>();
                    std::string encoded_email;
                    for (char c : email) {
                        if (c == '@') {
                            encoded_email += "%40";
                        } else if (c == '.') {
                            encoded_email += c;  // dots don't need encoding in query params
                        } else {
                            encoded_email += c;
                        }
                    }
                    
                    std::string endpoint = "/v1/document/getEmbeddedSignLink?documentId=" + 
                                         session.signature_request_id + 
                                         "&signerEmail=" + encoded_email;
                    
                    std::cout << "Full endpoint: " << endpoint << std::endl;
                    
                    json api_response = call_signature_api(endpoint, "GET");
                    
                    // Log the response to debug
                    std::cout << "BoldSign embedded sign link response: " << api_response.dump() << std::endl;
                    
                    // BoldSign returns the URL directly in "signLink" field
                    if (api_response.contains("signLink")) {
                        if (api_response["signLink"].is_string()) {
                            sign_url = api_response["signLink"];
                        } else if (api_response["signLink"].is_object() && api_response["signLink"].contains("signUrl")) {
                            sign_url = api_response["signLink"]["signUrl"];
                        } else {
                            throw std::runtime_error("Unexpected signLink format in BoldSign response");
                        }
                    } else {
                        throw std::runtime_error("No signLink in BoldSign response: " + api_response.dump());
                    }
                } else {
                    std::string endpoint = "/v3/embedded/sign_url/" + session.signature_id;
                    json api_response = call_signature_api(endpoint, "GET");
                    sign_url = api_response["embedded"]["sign_url"];
                }
                
                json response = {
                    {"sign_url", sign_url}
                };
                
                res.set_content(response.dump(), "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                json error_response = {{"error", e.what()}};
                res.set_content(error_response.dump(), "application/json");
            }
        });
        
        // Get session status
        server.Get("/api/sessions/:id/status", [this](const Request& req, Response& res) {
            setup_cors(res);
            
            std::string session_id = req.path_params.at("id");
            
            try {
                SigningSession session;
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    auto it = signing_sessions.find(session_id);
                    if (it == signing_sessions.end()) {
                        res.status = 404;
                        res.set_content("{\"error\":\"Session not found\"}", "application/json");
                        return;
                    }
                    session = it->second;
                }
                
                // Get signature request status from API
                std::string status;
                
                if (signature_provider == "boldsign") {
                    std::string endpoint = "/v1/document/properties?documentId=" + session.signature_request_id;
                    json api_response = call_signature_api(endpoint, "GET");
                    
                    std::string doc_status = api_response["status"];
                    status = (doc_status == "Completed") ? "signed" : "pending";
                } else {
                    std::string endpoint = "/v3/signature_request/" + session.signature_request_id;
                    json api_response = call_signature_api(endpoint, "GET");
                    
                    bool is_complete = api_response["signature_request"]["is_complete"];
                    status = is_complete ? "signed" : "pending";
                }
                
                // Update local status
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    signing_sessions[session_id].status = status;
                }
                
                json response = {
                    {"status", status}
                };
                
                res.set_content(response.dump(), "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                json error_response = {{"error", e.what()}};
                res.set_content(error_response.dump(), "application/json");
            }
        });
        
        // Get signed document
        server.Get("/api/documents/:id.pdf", [this](const Request& req, Response& res) {
            setup_cors(res);
            
            std::string session_id = req.path_params.at("id");
            session_id = session_id.substr(0, session_id.find(".pdf"));
            
            try {
                SigningSession session;
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    auto it = signing_sessions.find(session_id);
                    if (it == signing_sessions.end()) {
                        res.status = 404;
                        res.set_content("{\"error\":\"Session not found\"}", "application/json");
                        return;
                    }
                    session = it->second;
                }
                
                // Get the PDF from Dropbox Sign
                std::string pdf_content = get_file_binary(session.signature_request_id);
                
                res.set_header("Content-Type", "application/pdf");
                res.set_header("Content-Disposition", "attachment; filename=\"signed_document.pdf\"");
                res.set_content(pdf_content, "application/pdf");
            } catch (const std::exception& e) {
                res.status = 500;
                json error_response = {{"error", e.what()}};
                res.set_content(error_response.dump(), "application/json");
            }
        });
        
        // List all sessions (for debugging)
        server.Get("/api/sessions", [this](const Request& req, Response& res) {
            setup_cors(res);
            
            json sessions_array = json::array();
            {
                std::lock_guard<std::mutex> lock(sessions_mutex);
                for (const auto& [id, session] : signing_sessions) {
                    sessions_array.push_back({
                        {"id", session.id},
                        {"status", session.status},
                        {"signer", session.signer_info},
                        {"created_at", session.created_at}
                    });
                }
            }
            
            res.set_content(sessions_array.dump(), "application/json");
        });
    }
    
    void start(int port = 8080) {
        std::cout << "Document Signing Server starting on port " << port << std::endl;
        std::cout << "Frontend available at: http://localhost:" << port << "/" << std::endl;
        std::cout << "API endpoint: http://localhost:" << port << "/api/" << std::endl;
        std::cout << "Using " << signature_provider << " API for signatures" << std::endl;
        
        setup_routes();
        server.listen("0.0.0.0", port);
    }
};

int main() {
    try {
        DocumentSigningServer server;
        server.start(8080);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}