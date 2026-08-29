#include "OTAManager.h"
#include "MainPowerCheck.h"
#include "WebManager.h"
#include "utilities.h"
#include <esp_wifi.h>
#include <esp_ota_ops.h>

OTAManager::OTAManager(SMSSender &sender, const String &targetNumber, ConfigManager &configManager, 
                       PhoneNumberManager &phoneNumberManager, AlertManager &alertManager, MainPowerCheck &mainPowerCheck, Modem &modem, WebManager &webManager)
    : _sender(sender), _targetNumber(targetNumber), _configManager(configManager),
      _phoneNumberManager(phoneNumberManager), _alertManager(alertManager), _mainPowerCheck(mainPowerCheck), _modem(modem), _webManager(webManager),
      _webServer(nullptr), _isActive(false), _startTime(0), _uploadStarted(false), _uploadedBytes(0), _updateInProgress(false),
      _updateCompleted(false), _otaAccessToken("") {
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

    // Check if web configuration is active
    if (_webManager.isRunning()) {
        log_w("[OTA] OTA denied: WebManager is currently active");
        _sender.send(_targetNumber, "ERROR: Cannot activate OTA mode while web configuration is active. Stop web server first.");
        return false;
    }

    // Check if device is powered by main power
    if (!_mainPowerCheck.isMainPowerAvailable()) {
        log_w("[OTA] OTA denied: Device is not powered by main power");
        _sender.send(_targetNumber, "ERROR: OTA update denied. Device must be powered by main power, not battery.");
        return false;
    }

    log_i("[OTA] Starting OTA mode");
    _uploadStarted = false;
    _uploadedBytes = 0;
    _updateInProgress = false;
    _updateCompleted = false;

    // Generate random access token for OTA endpoint
    _otaAccessToken = generateRandomToken();
    log_i("[OTA] Generated access token: %s", _otaAccessToken.c_str());

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

    // Start timeout countdown after SMS is sent
    _startTime = millis();

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

    // Set up routes with random access token
    String rootPath = "/" + _otaAccessToken + "/";
    String uploadPath = "/" + _otaAccessToken + "/upload";
    String cancelPath = "/" + _otaAccessToken + "/cancel";
    
    _webServer->on(rootPath, HTTP_GET, [this]() { handleRoot(); });
    _webServer->on(uploadPath, HTTP_POST, [this]() { handleStatus(); }, [this]() { handleFileUpload(); });
    _webServer->on(cancelPath, HTTP_POST, [this]() { handleCancel(); });
    _webServer->onNotFound([this]() {
        log_w("[OTA] Access denied - invalid token in path: %s", _webServer->uri().c_str());
        _webServer->send(403, "text/plain", "Forbidden");
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
    String html = R"(
<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>SMSRelay OTA Firmware Update</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 600px;
            margin: 10px auto;
            padding: 0 10px;
            background-color: #f5f5f5;
        }
        .container {
            background-color: white;
            border-radius: 8px;
            padding: 0 30px;
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
        button.cancel-btn {
            background-color: #dc3545;
            margin-left: 10px;
        }
        button.cancel-btn:hover {
            background-color: #c82333;
        }
        .button-group {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-top: 10px;
        }
        .button-group button {
            margin-top: 0;
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
        .timer {
            background-color: #fff3cd;
            border-left: 4px solid #ff6b6b;
            padding: 12px;
            margin-bottom: 20px;
            border-radius: 4px;
            text-align: center;
            font-weight: bold;
        }
        .timer.expired {
            background-color: #f8d7da;
            color: #721c24;
            border-left-color: #721c24;
        }
        #timerValue {
            font-size: 24px;
            color: #ff6b6b;
        }
        .timer.expired #timerValue {
            color: #721c24;
        }
    </style>
</head>
<body>
    <div class='container'>
        <h1>SMSRelay OTA Firmware Update</h1>
        
        <div class='timer' id='timerDiv'>
            <strong>Time remaining to start upload:</strong><br>
            <span id='timerValue'>90</span> seconds
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
            <div class='button-group'>
                <button type='button' onclick='uploadFirmware()'>Upload and Update</button>
                <button type='button' class='cancel-btn' onclick='cancelOTA()'>Cancel OTA</button>
            </div>
        </form>

        <script>const OTA_ACCESS_TOKEN = ')" + _otaAccessToken + R"(';</script>

        <div id='progress' class='progress' style='display:none;'>
            <progress id='progressBar' value='0' max='100'></progress>
            <p id='progressText' style='text-align:center; margin-top:10px;'>0%</p>
        </div>

        <div id='status'></div>

        <script>
        const OTA_TIMEOUT_SECONDS = 90;
        const elapsedMillisAtPageLoad = )" + String(millis() - _startTime) + R"(;
        let remainingTime = OTA_TIMEOUT_SECONDS;
        let uploadStarted = false;
        let pageLoadTime = Date.now();

        function calculateRemainingTime() {
            const totalElapsedMs = elapsedMillisAtPageLoad + (Date.now() - pageLoadTime);
            const elapsedSeconds = Math.floor(totalElapsedMs / 1000);
            return Math.max(0, OTA_TIMEOUT_SECONDS - elapsedSeconds);
        }

        function startTimer() {
            // Initialize remaining time based on server start time
            remainingTime = calculateRemainingTime();
            document.getElementById('timerValue').textContent = remainingTime;

            const timerInterval = setInterval(() => {
                if (uploadStarted) {
                    clearInterval(timerInterval);
                    return;
                }

                remainingTime = calculateRemainingTime();
                document.getElementById('timerValue').textContent = remainingTime;

                if (remainingTime <= 0) {
                    clearInterval(timerInterval);
                    const timerDiv = document.getElementById('timerDiv');
                    timerDiv.classList.add('expired');
                    document.getElementById('timerValue').textContent = '0';
                    const statusDiv = document.getElementById('status');
                    statusDiv.textContent = 'OTA Session expired: No upload started within 90 seconds. Please request a new OTA session.';
                    statusDiv.className = 'error';
                    statusDiv.style.display = 'block';
                    
                    const fileInput = document.getElementById('firmwareFile');
                    fileInput.disabled = true;
                    document.querySelector('button').disabled = true;
                }
            }, 1000);
        }

        function uploadFirmware() {
            const fileInput = document.getElementById('firmwareFile');
            const file = fileInput.files[0];
            const statusDiv = document.getElementById('status');
            const progressDiv = document.getElementById('progress');

            if (!file) {
                statusDiv.textContent = 'Please select a file';
                statusDiv.className = 'error';
                statusDiv.style.display = 'block';
                return;
            }

            if (!file.name.endsWith('.bin')) {
                statusDiv.textContent = 'File must be a .bin file';
                statusDiv.className = 'error';
                statusDiv.style.display = 'block';
                return;
            }

            // All validations passed, now mark upload as started and hide timer
            uploadStarted = true;
            const timerDiv = document.getElementById('timerDiv');
            timerDiv.style.display = 'none';

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

            xhr.open('POST', '/' + OTA_ACCESS_TOKEN + '/upload');
            xhr.send(formData);
        }

        function cancelOTA() {
            if (!confirm('Cancel OTA mode?')) {
                return;
            }

            const statusDiv = document.getElementById('status');
            const fileInput = document.getElementById('firmwareFile');
            const buttons = document.querySelectorAll('button');

            statusDiv.textContent = 'Cancelling OTA mode...';
            statusDiv.className = 'warning';
            statusDiv.style.display = 'block';

            // Disable buttons
            buttons.forEach(btn => btn.disabled = true);
            fileInput.disabled = true;

            const xhr = new XMLHttpRequest();

            xhr.addEventListener('load', () => {
                if (xhr.status === 200) {
                    statusDiv.textContent = 'OTA mode cancelled. Device is still in OTA mode.';
                    statusDiv.className = 'success';
                } else {
                    const response = JSON.parse(xhr.responseText);
                    statusDiv.textContent = 'Error: ' + (response.message || 'Cancel failed');
                    statusDiv.className = 'error';
                    // Re-enable buttons on error
                    buttons.forEach(btn => btn.disabled = false);
                    fileInput.disabled = false;
                }
            });

            xhr.addEventListener('error', () => {
                statusDiv.textContent = 'Connection error during cancel';
                statusDiv.className = 'error';
                // Re-enable buttons on error
                buttons.forEach(btn => btn.disabled = false);
                fileInput.disabled = false;
            });

            xhr.open('POST', '/' + OTA_ACCESS_TOKEN + '/cancel');
            xhr.send();
        }

        // Start timer when page loads
        window.addEventListener('load', startTimer);
        </script>
    </div>
</body>
</html>
    )";
    
    return html;
}

void OTAManager::handleFileUpload() {
    String expectedUploadPath = "/" + _otaAccessToken + "/upload";
    if (_webServer->uri() != expectedUploadPath) {
        return;
    }

    HTTPUpload &upload = _webServer->upload();

    if (upload.status == UPLOAD_FILE_START) {
        log_i("[OTA] File upload started: %s", upload.filename.c_str());
        _uploadStarted = true;
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

void OTAManager::handleCancel() {
    log_i("[OTA] Cancel request received");
    String response = "{\"status\":\"ok\",\"message\":\"OTA mode cancelled\"}";
    
    _webServer->send(200, "application/json", response);
    
    delay(100);
    
    // Stop OTA after sending response
    stop();
}

void OTAManager::sendURLtoSMS() {
    String localIP = getLocalIP();
    String url = "http://" + localIP + "/" + _otaAccessToken + "/";
    String message = "OTA Mode Active for 90 seconds!\nAccess: " + url;
    
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

    // Check for OTA upload timeout (90 seconds) - only if upload hasn't started
    if (!_uploadStarted && (millis() - _startTime) >= OTA_UPLOAD_TIMEOUT_MS) {
        log_w("[OTA] Upload timeout: no file upload started within 90 seconds");
        
        // Send timeout SMS
        String timeoutMsg = "OTA timeout: No firmware upload started within 90 seconds. OTA mode cancelled.";
        if (!_requesterNumber.isEmpty()) {
            _sender.send(_requesterNumber, timeoutMsg);
            log_i("[OTA] Timeout SMS sent to %s", _requesterNumber.c_str());
        }
        
        // Stop OTA and disable WiFi
        stop();
        return;
    }

    // Handle web server requests
    _webServer->handleClient();
    // Restart is triggered by handleStatus() after file upload completes, not here
}


String OTAManager::generateRandomToken() {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    String token = "";
    
    for (int i = 0; i < 10; i++) {
        int index = random(0, sizeof(charset) - 1);
        token += charset[index];
    }
    
    return token;
}

void OTAManager::restartDevice() {
    log_i("[OTA] Device restart initiated");
    _modem.restartDevice();
}
