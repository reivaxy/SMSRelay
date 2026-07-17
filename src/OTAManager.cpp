#include "OTAManager.h"
#include "utilities.h"
#include <esp_wifi.h>
#include <esp_ota_ops.h>

OTAManager::OTAManager(SMSSender &sender, const String &targetNumber, ConfigManager &configManager, 
                       PhoneNumberManager &phoneNumberManager, AlertManager &alertManager)
    : _sender(sender), _targetNumber(targetNumber), _configManager(configManager),
      _phoneNumberManager(phoneNumberManager), _alertManager(alertManager),
      _webServer(nullptr), _isActive(false), _startTime(0), _uploadedBytes(0), _updateInProgress(false),
      _updateCompleted(false) {
    log_i("[OTA] OTAManager initialized");
}

OTAManager::~OTAManager() {
    stop();
}

bool OTAManager::init() {
    if (_isActive) {
        log_w("[OTA] OTA already active");
        return false;
    }

    log_i("[OTA] Starting OTA mode");
    _startTime = millis();
    _uploadedBytes = 0;
    _updateInProgress = false;
    _updateCompleted = false;

    // Connect to WiFi
    if (!connectToWiFi()) {
        log_e("[OTA] Failed to connect to WiFi");
        return false;
    }

    // Start web server
    if (!startWebServer()) {
        log_e("[OTA] Failed to start web server");
        stop();
        return false;
    }

    _isActive = true;
    log_i("[OTA] OTA mode active, IP: %s", getLocalIP().c_str());

    // Send SMS with URL
    sendURLtoSMS();

    return true;
}

bool OTAManager::connectToWiFi() {
    String ssid = _configManager.getWiFiSSID();
    String password = _configManager.getWiFiPassword();

    if (ssid.length() == 0 || password.length() == 0) {
        log_e("[OTA] WiFi SSID or password not configured");
        _sender.send(_targetNumber, "ERROR: WiFi SSID and password not configured. Use: CONFIG WIFI_SSID <ssid> and CONFIG WIFI_PASS <password>");
        return false;
    }

    log_i("[OTA] Connecting to WiFi: %s", ssid.c_str());

    // Disable modem WiFi interference
    WiFi.disconnect(true);  // Turn off and disable WiFi AP
    delay(100);

    // Set to STA mode
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for connection (up to 20 seconds)
    int attempts = 0;
    int maxAttempts = 40;  // 20 seconds (40 * 500ms)

    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        log_d("[OTA] Connecting to WiFi... %d/%d", attempts, maxAttempts);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        log_e("[OTA] Failed to connect to WiFi after %d seconds", maxAttempts / 2);
        _sender.send(_targetNumber, "ERROR: Failed to connect to WiFi '" + ssid + "'");
        return false;
    }

    log_i("[OTA] Connected to WiFi: %s", ssid.c_str());
    log_i("[OTA] Local IP: %s", WiFi.localIP().toString().c_str());
    log_i("[OTA] Signal strength: %d dBm", WiFi.RSSI());

    return true;
}

bool OTAManager::startWebServer() {
    if (_webServer != nullptr) {
        delete _webServer;
    }

    _webServer = new WebServer(80);

    if (_webServer == nullptr) {
        log_e("[OTA] Failed to allocate memory for WebServer");
        return false;
    }

    // Set up routes
    _webServer->on("/", HTTP_GET, [this]() { handleRoot(); });
    _webServer->on("/upload", HTTP_POST, [this]() { handleStatus(); }, [this]() { handleFileUpload(); });
    _webServer->onNotFound([this]() {
        _webServer->send(404, "text/plain", "Not Found");
    });

    _webServer->begin();
    log_i("[OTA] Web server started on port 80");

    return true;
}

void OTAManager::handleRoot() {
    log_d("[OTA] GET / request");
    _webServer->send(200, "text/html", generateUploadPage());
}

String OTAManager::generateUploadPage() {
    return R"(
<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>OTA Firmware Update</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 600px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f5f5f5;
        }
        .container {
            background-color: white;
            border-radius: 8px;
            padding: 30px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            text-align: center;
        }
        .form-group {
            margin: 20px 0;
        }
        label {
            display: block;
            margin-bottom: 8px;
            font-weight: bold;
            color: #555;
        }
        input[type='file'] {
            display: block;
            margin-bottom: 10px;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            width: 100%;
            box-sizing: border-box;
        }
        button {
            background-color: #4CAF50;
            color: white;
            padding: 12px 30px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
            width: 100%;
            margin-top: 10px;
        }
        button:hover {
            background-color: #45a049;
        }
        .info {
            background-color: #e7f3fe;
            border-left: 4px solid #2196F3;
            padding: 12px;
            margin-bottom: 20px;
            border-radius: 4px;
        }
        .progress {
            margin-top: 20px;
        }
        progress {
            width: 100%;
            height: 25px;
            border-radius: 4px;
        }
        #status {
            margin-top: 15px;
            padding: 10px;
            border-radius: 4px;
            text-align: center;
            font-weight: bold;
        }
        .success {
            background-color: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .error {
            background-color: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        .warning {
            background-color: #fff3cd;
            color: #856404;
            border: 1px solid #ffeaa7;
        }
    </style>
</head>
<body>
    <div class='container'>
        <h1>Firmware Update (OTA)</h1>
        
        <div class='info'>
            <strong>Device Local IP:</strong><br>
            http://)" + getLocalIP() + R"(
        </div>

        <div class='info'>
            <strong>Instructions:</strong><br>
            1. Select a compiled firmware binary file (.bin)<br>
            2. Click 'Upload and Update'<br>
            3. The device will restart automatically<br>
            4. WiFi will be disabled after update
        </div>

        <form id='uploadForm' enctype='multipart/form-data'>
            <div class='form-group'>
                <label for='firmwareFile'>Select Firmware File (.bin):</label>
                <input type='file' id='firmwareFile' name='firmware' accept='.bin' required>
            </div>
            <button type='button' onclick='uploadFirmware()'>Upload and Update</button>
        </form>

        <div id='progress' class='progress' style='display:none;'>
            <progress id='progressBar' value='0' max='100'></progress>
            <p id='progressText' style='text-align:center; margin-top:10px;'>0%</p>
        </div>

        <div id='status'></div>

        <script>
        function uploadFirmware() {
            const fileInput = document.getElementById('firmwareFile');
            const file = fileInput.files[0];
            const statusDiv = document.getElementById('status');
            const progressDiv = document.getElementById('progress');

            if (!file) {
                statusDiv.textContent = 'Please select a file';
                statusDiv.className = 'error';
                return;
            }

            if (!file.name.endsWith('.bin')) {
                statusDiv.textContent = 'File must be a .bin file';
                statusDiv.className = 'error';
                return;
            }

            statusDiv.textContent = 'Uploading...';
            statusDiv.className = 'warning';
            progressDiv.style.display = 'block';

            const xhr = new XMLHttpRequest();
            const formData = new FormData();
            formData.append('firmware', file);

            xhr.upload.addEventListener('progress', (e) => {
                if (e.lengthComputable) {
                    const percentComplete = (e.loaded / e.total) * 100;
                    document.getElementById('progressBar').value = percentComplete;
                    document.getElementById('progressText').textContent = percentComplete.toFixed(0) + '%';
                }
            });

            xhr.addEventListener('load', () => {
                if (xhr.status === 200) {
                    statusDiv.textContent = 'Upload successful! Device is updating and will restart...';
                    statusDiv.className = 'success';
                    progressDiv.style.display = 'none';
                    fileInput.disabled = true;
                    document.querySelector('button').disabled = true;
                } else {
                    const response = JSON.parse(xhr.responseText);
                    statusDiv.textContent = 'Error: ' + (response.message || 'Upload failed');
                    statusDiv.className = 'error';
                    progressDiv.style.display = 'none';
                }
            });

            xhr.addEventListener('error', () => {
                statusDiv.textContent = 'Connection error during upload';
                statusDiv.className = 'error';
                progressDiv.style.display = 'none';
            });

            xhr.open('POST', '/upload');
            xhr.send(formData);
        }
        </script>
    </div>
</body>
</html>
    )";
}

void OTAManager::handleFileUpload() {
    if (_webServer->uri() != "/upload") {
        return;
    }

    HTTPUpload &upload = _webServer->upload();

    if (upload.status == UPLOAD_FILE_START) {
        log_i("[OTA] File upload started: %s", upload.filename.c_str());
        _uploadedBytes = 0;
        _updateInProgress = true;

        // Get the size of the OTA partition from the partition table
        const esp_partition_t *running_partition = esp_ota_get_running_partition();
        const esp_partition_t *next_partition = esp_ota_get_next_update_partition(running_partition);
        
        if (next_partition == nullptr) {
            log_e("[OTA] No OTA partition available");
            Update.abort();
            _updateInProgress = false;
            return;
        }
        
        size_t partition_size = next_partition->size;
        log_i("[OTA] OTA partition size: %d bytes", partition_size);

        // Begin OTA update with partition size
        if (!Update.begin(partition_size)) {
            log_e("[OTA] Update.begin() failed: %s", Update.errorString());
            Update.printError(Serial);
            _updateInProgress = false;
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_updateInProgress) {
            size_t written = Update.write(upload.buf, upload.currentSize);
            _uploadedBytes += written;
            log_d("[OTA] Written %d bytes (total: %d/%d)", written, _uploadedBytes, upload.totalSize);

            if (written != upload.currentSize) {
                log_e("[OTA] Update.write() failed: %s", Update.errorString());
                Update.printError(Serial);
                _updateInProgress = false;
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (_updateInProgress && Update.end(true)) {
            log_i("[OTA] File upload complete and verified: %d bytes", _uploadedBytes);
            _updateInProgress = false;
            _updateCompleted = true;  // Mark update as completed for handleStatus
        } else {
            log_e("[OTA] Update failed: %s", Update.errorString());
            _updateInProgress = false;
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
        log_w("[OTA] File upload aborted");
        Update.abort();
        _updateInProgress = false;
    }
}

void OTAManager::handleStatus() {
    // Still uploading or writing firmware
    if (_updateInProgress || Update.isRunning()) {
        log_i("[OTA] Update still in progress");
        _webServer->send(202, "application/json", "{\"status\":\"updating\",\"bytes\":" + String(_uploadedBytes) + "}");
        return;
    }
    
    // Update encountered an error
    if (Update.hasError()) {
        String errMsg = Update.errorString();
        log_e("[OTA] Update error: %s", errMsg.c_str());
        _webServer->send(400, "application/json", "{\"error\":\"" + errMsg + "\"}");
        return;
    }
    
    // Update completed successfully (file was uploaded and verified)
    if (_updateCompleted) {
        log_i("[OTA] Update successful, restarting device...");
        _webServer->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Firmware updated successfully! Device restarting...\"}");
        delay(1000);
        restartDevice();
        return;
    }
    
    // No valid update occurred (no file uploaded)
    log_w("[OTA] No firmware update data received");
    _webServer->send(400, "application/json", "{\"error\":\"No firmware update data received\"}");
}

void OTAManager::handleUpload() {
    // This is called when upload is complete
    handleFileUpload();
}

void OTAManager::sendURLtoSMS() {
    String localIP = getLocalIP();
    String url = "http://" + localIP;
    String message = "OTA Mode Active!\nAccess: " + url;
    
    log_i("[OTA] Sending URL to %s: %s", _requesterNumber.c_str(), url.c_str());
    
    if (_sender.send(_requesterNumber, message)) {
        log_i("[OTA] URL sent successfully");
    } else {
        log_w("[OTA] Failed to send URL via SMS");
    }
}

String OTAManager::getLocalIP() const {
    return WiFi.localIP().toString();
}

void OTAManager::stop() {
    if (_webServer != nullptr) {
        _webServer->stop();
        delete _webServer;
        _webServer = nullptr;
        log_i("[OTA] Web server stopped");
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(true);  // Disconnect and turn off WiFi
        log_i("[OTA] WiFi disconnected and disabled");
    }

    _isActive = false;
    log_i("[OTA] OTA mode stopped");
}

void OTAManager::check() {
    if (!_isActive || _webServer == nullptr) {
        return;
    }

    // Handle web server requests
    _webServer->handleClient();
    // Restart is triggered by handleStatus() after file upload completes, not here
}

void OTAManager::restartDevice() {
    log_i("[OTA] Restarting device...");
    delay(1000);
    ESP.restart();
}
