


#define BLYNK_TEMPLATE_ID "TMPL6zSsTrVXM"
#define BLYNK_TEMPLATE_NAME "MonitoringSystem"
#define BLYNK_AUTH_TOKEN "xtVA8QBnk8vzow7ooSaV2w0jWA_IJTsc"

#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <BlynkSimpleEsp8266.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// GPIO
#define RED_LED       D1
#define PIR_SENSOR    D3
#define DOOR_SENSOR   D2
#define RX_FINGER     D5
#define TX_FINGER     D6

SoftwareSerial mySerial(RX_FINGER, TX_FINGER);
Adafruit_Fingerprint finger(&mySerial);
AsyncWebServer server(80);
WidgetTerminal terminal(V4);

bool alarmEnabled = true; // Trạng thái hệ thống báo động

// ======= Biến trạng thái trước đó ======= //
bool prevDoorClosed = false;
bool prevLedState = false;
String prevMotionStatus = "";
String prevFingerprintStatus = "";

// Biến cấu hình
String ssid_sta, pass_sta, blynk_token, device_location;
bool configMode = false;

// Trạng thái hệ thống
unsigned long previousMillis = 0;
unsigned long lastMotionMillis = 0;
bool allowScan = true;
bool ignoreMotion = false;
bool motionDetected = false;

// Giao diện cấu hình
const char* configPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Thiết lập WiFi & Blynk</title>
</head>
<body>
  <h2>Thiết lập WiFi & Blynk</h2>
  <form action="/save" method="GET">
    SSID: <input type="text" name="ssid"><br><br>
    Mật khẩu: <input type="password" name="pass"><br><br>
    Blynk Token: <input type="text" name="blynk"><br><br>
    Vị trí thiết bị: <input type="text" name="location"><br><br>
    <input type="submit" value="Lưu cấu hình">
  </form>
</body>
</html>
)rawliteral";


// ======= Hàm lưu/đọc cấu hình ======= //
void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["ssid"] = ssid_sta;
  doc["pass"] = pass_sta;
  doc["blynk"] = blynk_token;
  doc["location"] = device_location;
  File file = LittleFS.open("/config.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    if (file) {
      StaticJsonDocument<256> doc;
      if (!deserializeJson(doc, file)) {
        ssid_sta = doc["ssid"].as<String>();
        pass_sta = doc["pass"].as<String>();
        blynk_token = doc["blynk"].as<String>();
        device_location = doc["location"].as<String>();
      }
      file.close();
    }
  }
}

// ======= Setup chính ======= //
void setup() {
  pinMode(RED_LED, OUTPUT);
  pinMode(PIR_SENSOR, INPUT);
  pinMode(DOOR_SENSOR, INPUT_PULLUP);
  digitalWrite(RED_LED, LOW);

  Serial.begin(9600);
  mySerial.begin(57600);
  finger.begin(57600);

  LittleFS.begin();
  loadConfig();

  if (ssid_sta == "" || blynk_token == "") {
    configMode = true;
    WiFi.softAP("ESP_Config");
    IPAddress IP = WiFi.softAPIP();
    Serial.println("Cấu hình tại: http://" + IP.toString());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", configPage);
    });

    server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
      ssid_sta = request->getParam("ssid")->value();
      pass_sta = request->getParam("pass")->value();
      blynk_token = request->getParam("blynk")->value();
      device_location = request->getParam("location")->value();
      saveConfig();
      request->send(200, "text/html", "<h3>Đã nhận cấu hình. Đang khởi động lại...</h3>");
      delay(1000);
      ESP.restart();
    });

    server.begin();
    return;
  }

  WiFi.begin(ssid_sta.c_str(), pass_sta.c_str());
  Serial.print("Đang kết nối WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    Blynk.begin(blynk_token.c_str(), ssid_sta.c_str(), pass_sta.c_str(), "blynk.cloud", 80);
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // Múi giờ GMT+7
  } else {
    Serial.println("\nKết nối WiFi thất bại. Vào lại chế độ cấu hình.");
    LittleFS.remove("/config.json");
    delay(2000);
    ESP.restart();
  }

  if (!finger.verifyPassword()) {
    Serial.println("Không tìm thấy cảm biến vân tay!");
  } else {
    Serial.println("Cảm biến vân tay sẵn sàng!");
  }
}



// ======= Hàm kiểm tra vân tay ======= //
bool checkFingerprint() {
  if (finger.getImage() != FINGERPRINT_OK) return false;
  if (finger.image2Tz() != FINGERPRINT_OK) return false;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return false;

  Serial.printf("Khớp ID: %d - Độ chính xác: %d\n", finger.fingerID, finger.confidence);
  return true;
}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[25];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", 
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  return String(buffer);
}


BLYNK_WRITE(V6) {
  int state = param.asInt();
  alarmEnabled = (state == 1);
  if (alarmEnabled) {
    Serial.println("🔔 Hệ thống cảnh báo ĐANG hoạt động.");
    terminal.println("🔔 Cảnh báo ĐƯỢC BẬT");
  } else {
    Serial.println("🔕 Hệ thống cảnh báo TẮT.");
    terminal.println("🔕 Cảnh báo ĐÃ TẮT");
  }
  terminal.flush();
}


bool authorizedEntry = false;     // Đã được xác thực vân tay
unsigned long fingerprintMillis = 0;  // Thời điểm xác thực gần nhất
bool fingerprintChecked = false;
int failedAttempts = 0;  // Biến đếm số lần thử không thành công
const int maxAttempts = 5;  // Giới hạn số lần thử tối đa
bool lockoutActive = false;
unsigned long lockoutStart = 0;
const unsigned long lockoutDuration = 30000; // 60 giây

bool isFingerPressed() {
  return (finger.getImage() == FINGERPRINT_OK);
}


// ======= Loop chính ======= //
void loop() {
  if (!configMode) Blynk.run();
  unsigned long now = millis();

  // ======= Kiểm tra trạng thái cửa ======= //
  bool doorClosed = (digitalRead(DOOR_SENSOR) == LOW);
  if (doorClosed != prevDoorClosed) {
    Blynk.virtualWrite(V1, doorClosed ? "Cửa Đóng" : "Cửa Mở");
    Serial.println(doorClosed ? "Cửa Đóng" : "Cửa Mở");

    terminal.println("🚪 " + getTimestamp() + " - " + device_location + ": " + (doorClosed ? "Đóng cửa" : "Mở cửa"));
    terminal.flush();
    prevDoorClosed = doorClosed;

    // Khi cửa ĐÓNG LẠI -> kích hoạt lại cảnh báo
    if (doorClosed) {
      authorizedEntry = false;
      allowScan = true;
      Serial.println("✅ Hệ thống cảnh báo đã kích hoạt lại sau khi cửa đóng.");
    }
  }

  // ======= Quản lý đèn LED ======= //
  bool ledState;
  if (!alarmEnabled) {
    ledState = true;  // Luôn bật nếu cảnh báo tắt
  } else {
    ledState = doorClosed;  // LED theo cửa nếu cảnh báo bật
  }

  if (ledState != prevLedState) {
    Blynk.virtualWrite(V3, ledState ? 255 : 0);
    digitalWrite(RED_LED, ledState ? HIGH : LOW);
    prevLedState = ledState;
  }

  if (!alarmEnabled) return;

  // ======= Tự động cho phép xác thực lại sau 30s ======= //
  if (!allowScan && now - fingerprintMillis >= 30000) {
    allowScan = true;
  }

  // ======= Kiểm tra khóa xác thực (sau khi vượt quá số lần) ======= //
  if (lockoutActive) {
    if (now - lockoutStart >= lockoutDuration) {
      lockoutActive = false;
      Blynk.virtualWrite(V0, "🔓 Cho phép xác thực lại.");
      terminal.println("🔓 " + getTimestamp() + " - " + device_location + ": Hết thời gian khoá. Có thể xác thực lại.");
      terminal.flush();
      Serial.println("🔓 Hết thời gian khoá. Cho phép xác thực lại.");
    } else {
      return; // Đang khoá => không kiểm tra tiếp
    }
  }

  
  // ======= Kiểm tra vân tay khi cửa ĐÓNG và cho phép xác thực ======= //
  if (allowScan && doorClosed && !authorizedEntry && isFingerPressed()) {
    if (checkFingerprint()) {
      authorizedEntry = true;
      allowScan = false;
      fingerprintMillis = now;
      failedAttempts = 0;

      Blynk.virtualWrite(V0, "✅ Fingerprint OK");
      terminal.println("✅ " + getTimestamp() + " - " + device_location + ": Vân tay hợp lệ!");
      terminal.flush();
      Serial.println("✅ Vân tay hợp lệ!");
      prevFingerprintStatus = "ok";
    } else {
      failedAttempts++;
      Blynk.virtualWrite(V0, "❌ Fingerprint FAIL");
      terminal.println("❌ " + getTimestamp() + " - " + device_location + ": Vân tay không hợp lệ. Thử lại (" + String(failedAttempts) + "/" + String(maxAttempts) + ")");
      terminal.flush();
      Serial.println("❌ Vân tay không hợp lệ. Số lần: " + String(failedAttempts));

      if (failedAttempts >= maxAttempts) {
        Blynk.virtualWrite(V0, "❌ Quá số lần xác thực! Hệ thống khoá 30 giây.");
        terminal.println("🚨 " + getTimestamp() + " - " + device_location + ": Quá 5 lần xác thực sai. Khoá hệ thống 30 giây.");
        terminal.flush();       
        Serial.println("🚨 Quá 5 lần xác thực. Khoá hệ thống trong 30 giây.");
        lockoutActive = true;
        lockoutStart = now;
        failedAttempts = 0;
      }
    }
  }




  // ======= Nếu đã mở cửa và chưa xác thực: kiểm tra chuyển động ======= //
  if (!doorClosed && !authorizedEntry) {
    if (digitalRead(PIR_SENSOR) == HIGH && !motionDetected) {
      motionDetected = true;
      lastMotionMillis = now;

      if (prevMotionStatus != "motion") {
        Blynk.virtualWrite(V2, "Phát hiện chuyển động");
        terminal.println("🚨 " + getTimestamp() + " - " + device_location + ": Phát hiện chuyển động!");
        String msg = "❌ " + getTimestamp() + " - " + device_location + ": Phát hiện chuyển động!";
        Blynk.logEvent("notifications", msg);
        terminal.flush();
        Serial.println("Phát hiện chuyển động!");    
        prevMotionStatus = "motion";
      }
    }

    // Sau 30s không có chuyển động -> cập nhật lại
    if (motionDetected && now - lastMotionMillis >= 30000) {
      motionDetected = false;
      if (prevMotionStatus != "no_motion") {
        Blynk.virtualWrite(V2, "Chưa có phát hiện mới");
        prevMotionStatus = "no_motion";
      }
    }
  }
}


