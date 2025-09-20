// NodeMCU (ESP8266) - Restaurant order queue with LittleFS + MQTT + 20x4 I2C LCD
// Features:
//  - WiFiManager for Wi-Fi provisioning and persistent reconnect
//  - Subscribes: KY/RESTO/ORDER/NEW
//  - Publishes: KY/RESTO/ORDER/DONE { "orderId": "..." }
//  - Queue stored as single JSON array in LittleFS (/orders.json). Cleared on startup.
//  - LCD shows only table number on line 1. D7 scrolls items on lines 2-4.
//  - D5 marks current order done. D6 buzzer beeps twice on new order.

// Libraries
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- user config ----------
const char* MQTT_SERVER = "test.mosquitto.org";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";   // set to "" if no auth
const char* MQTT_PASS = "";

const char* TOPIC_NEW  = "KY/RESTO/ORDER/NEW";
const char* TOPIC_DONE = "KY/RESTO/ORDER/DONE";

const uint8_t BUTTON_DONE_PIN   = D5; // mark done, active low (INPUT_PULLUP)
const uint8_t BUTTON_SCROLL_PIN = D7; // scroll items, active low (INPUT_PULLUP)
const uint8_t BUZZER_PIN        = D6; // buzzer (tone)
const uint8_t LCD_I2C_ADDR      = 0x27; // change if needed
const char* QUEUE_FILE = "/orders.json";
// ---------- end user config ----------

WiFiClient espClient;
PubSubClient mqtt(espClient);
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 20, 4);

// JSON storage: optimized for ESP8266 memory constraints
StaticJsonDocument<6144> queueDoc; // reduced from 12288 to 6144 bytes

// Memory monitoring
void printMemoryStatus(const char* context) {
  Serial.printf("📊 [%s] Free heap: %u bytes, Fragmentation: %u%%\n", 
    context, ESP.getFreeHeap(), ESP.getHeapFragmentation());
}

// Button debounce
const unsigned long DEBOUNCE_MS = 100; // Increased for better reliability
bool lastDoneState = HIGH;
bool lastScrollState = HIGH;
unsigned long lastDebounceDone = 0;
unsigned long lastDebounceScroll = 0;
bool donePressed = false;
bool scrollPressed = false;

// Track last button action times
unsigned long lastDoneAction = 0;
unsigned long lastScrollAction = 0;
const unsigned long DONE_COOLDOWN   = 3000; // 3 seconds
const unsigned long SCROLL_COOLDOWN = 1000; // 1 second

// Scrolling state
int itemScrollIndex = 0;

// Forward declarations
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void saveQueueToFile();
void loadQueueFromFile();
void displayCurrentOrder();
bool queueEmpty();
JsonObject getCurrentOrder();
void removeCurrentOrderFromQueue();
void publishOrderDone(const char* orderId);
void beepTwice();
void ensureQueueFileExistsAndClearOnStartup();
void testButtons(); // Test button hardware

void setup() {
  Serial.begin(115200);
  delay(100);
  
  // Print initial memory status
  printMemoryStatus("Startup");

  // LittleFS init
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed, formatting...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("LittleFS failed after format!");
    }
  }

  // Clear existing queue file and create fresh empty array (user requested)
  ensureQueueFileExistsAndClearOnStartup();
  queueDoc.clear();
  queueDoc.to<JsonArray>();
  saveQueueToFile();

  // WiFiManager for provisioning and persistent credentials
  WiFiManager wm;
  // you can set timeouts or custom AP name here
  if (!wm.autoConnect("RESTAURANT-ORDER-SETUP")) {
    // If provisioning fails, continue anyway (device will retry on reset)
    Serial.println("WiFiManager failed to connect");
  } else {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  }

  // MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Starting...");

  // Pins
  pinMode(BUTTON_DONE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SCROLL_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // load queue into memory (should be empty just after startup clearing)
  loadQueueFromFile();

  // show current
  itemScrollIndex = 0;
  displayCurrentOrder();
  
  // Test buttons on startup (uncomment next line for testing)
  // testButtons();
}



void loop() {
  static unsigned long lastMemCheck = 0;
  static int loopCount = 0;
  
  // Keep MQTT alive
  if (!mqtt.connected()) {
    connectMQTT();
  } else {
    mqtt.loop();
  }

  unsigned long now = millis();
  
  // Periodic memory monitoring (every 30 seconds)
  if (now - lastMemCheck > 30000) {
    lastMemCheck = now;
    printMemoryStatus("Loop check");
  }

  // DONE button (D5)
  if (digitalRead(BUTTON_DONE_PIN) == LOW && (now - lastDoneAction >= DONE_COOLDOWN)) {
    lastDoneAction = now;
    if (!queueEmpty()) {
      JsonObject cur = getCurrentOrder();
      const char* oid = cur["orderId"] | "";
      if (strlen(oid) > 0) {
        publishOrderDone(oid);
      }
      removeCurrentOrderFromQueue();
      itemScrollIndex = 0;
      displayCurrentOrder();
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("No orders queued");
      delay(500);
      displayCurrentOrder();
    }
  }

  // SCROLL button (D7)
  if (digitalRead(BUTTON_SCROLL_PIN) == LOW && (now - lastScrollAction >= SCROLL_COOLDOWN)) {
    lastScrollAction = now;
    if (!queueEmpty()) {
      JsonObject cur = getCurrentOrder();
      JsonArray items = cur["items"].as<JsonArray>();
      int itemCount = items.size();
      if (itemCount > 3) {
        int maxStart = itemCount - 3;
        itemScrollIndex++;
        if (itemScrollIndex > maxStart) itemScrollIndex = 0;
      } else {
        itemScrollIndex = 0;
      }
      displayCurrentOrder();
    }
  }

  delay(10); // small sleep
}


// ---------- MQTT ----------

void connectMQTT() {
  static unsigned long lastAttempt = 0;
  const unsigned long ATTEMPT_INTERVAL = 5000;
  if (millis() - lastAttempt < ATTEMPT_INTERVAL) return;
  lastAttempt = millis();

  // Use char buffer instead of String
  char clientId[20];
  snprintf(clientId, sizeof(clientId), "ESP-%06X", ESP.getChipId());
  
  printMemoryStatus("Before MQTT connect");
  Serial.print("Connecting to MQTT...");
  
  bool connected = false;
  if (strlen(MQTT_USER) > 0) {
    connected = mqtt.connect(clientId, MQTT_USER, MQTT_PASS);
  } else {
    connected = mqtt.connect(clientId);
  }
  
  if (connected) {
    Serial.println("connected");
    mqtt.subscribe(TOPIC_NEW);
    Serial.printf("Subscribed to %s\n", TOPIC_NEW);
    printMemoryStatus("After MQTT connect");
  } else {
    Serial.printf("failed, rc=%d\n", mqtt.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Check memory before processing
  Serial.printf("📊 Free heap before processing: %u bytes\n", ESP.getFreeHeap());
  
  // Bounds checking
  if (length == 0) {
    Serial.println("❌ Empty MQTT message received");
    return;
  }
  
  if (length > 3000) { // Reduced limit for better memory management
    Serial.printf("❌ Message too large: %u bytes (max 3000)\n", length);
    return;
  }

  Serial.printf("📨 MQTT message on %s (Length: %u bytes)\n", topic, length);

  // More efficient: create null-terminated char array directly
  char* msgBuffer = (char*)malloc(length + 1);
  if (!msgBuffer) {
    Serial.println("❌ Failed to allocate memory for message");
    return;
  }
  
  memcpy(msgBuffer, payload, length);
  msgBuffer[length] = '\0';
  
  // Debug: Show first part of message
  Serial.print("📄 Message content: ");
  if (length > 200) {
    Serial.printf("%.200s... (truncated, full length: %u)\n", msgBuffer, length);
  } else {
    Serial.println(msgBuffer);
  }

  // parse JSON directly from char array (optimized buffer size)
  StaticJsonDocument<3072> doc; // Reduced from 4096 to 3072 bytes
  DeserializationError err = deserializeJson(doc, msgBuffer);
  
  // Free the message buffer immediately after parsing
  free(msgBuffer);
  
  if (err) {
    Serial.printf("❌ JSON parse error: %s\n", err.c_str());
    Serial.printf("📊 Message length: %u bytes\n", length);
    return;
  }

  Serial.println("✅ JSON parsed successfully");
  Serial.printf("📊 Free heap after parsing: %u bytes\n", ESP.getFreeHeap());

  if (!doc.containsKey("orderId")) {
    Serial.println("❌ No orderId found; ignoring");
    return;
  }

  // Debug: Print order details
  const char* orderId = doc["orderId"];
  int tableNum = doc["tableNumber"];
  JsonArray items = doc["items"];
  Serial.printf("📋 Order: %s, Table: %d, Items: %d\n", orderId, tableNum, items.size());

  // Check if we have space in queue (reduced limit for memory optimization)
  JsonArray arr = queueDoc.as<JsonArray>();
  if (arr.size() >= 5) { // Reduced from 10 to 5 orders max
    Serial.println("⚠️ Queue full, removing oldest order");
    arr.remove(0);
    Serial.printf("📊 Queue size after removal: %d\n", arr.size());
  }

  // More efficient copying: create nested object and copy fields
  JsonObject newOrder = arr.createNestedObject();
  for (JsonPair kv : doc.as<JsonObject>()) {
    // copy values (supports nested items array)
    newOrder[kv.key()] = kv.value();
  }

  Serial.printf("📊 Free heap after adding to queue: %u bytes\n", ESP.getFreeHeap());

  saveQueueToFile();

  // beep twice for new order
  beepTwice();

  // if this is the only order, show it
  if (arr.size() == 1) {
    itemScrollIndex = 0;
    displayCurrentOrder();
  }
  
  Serial.printf("✅ Order processed successfully. Queue size: %d\n", arr.size());
}

// ---------- queue persistence ----------

void ensureQueueFileExistsAndClearOnStartup() {
  // remove existing and create empty array file
  if (LittleFS.exists(QUEUE_FILE)) {
    LittleFS.remove(QUEUE_FILE);
    Serial.println("Removed existing queue file on startup.");
  }
  // create a new empty array file
  File f = LittleFS.open(QUEUE_FILE, "w");
  if (!f) {
    Serial.println("Failed to create queue file");
    return;
  }
  f.print("[]");
  f.close();
  Serial.println("Created fresh queue file.");
}

void saveQueueToFile() {
  File f = LittleFS.open(QUEUE_FILE, "w");
  if (!f) {
    Serial.println("Failed to open queue file for writing");
    return;
  }
  if (serializeJson(queueDoc, f) == 0) {
    Serial.println("Failed to write queue JSON");
  } else {
    Serial.println("Queue saved to LittleFS");
  }
  f.close();
}

void loadQueueFromFile() {
  queueDoc.clear();
  if (!LittleFS.exists(QUEUE_FILE)) {
    // create file if missing
    File f = LittleFS.open(QUEUE_FILE, "w");
    if (f) {
      f.print("[]");
      f.close();
    }
    queueDoc.to<JsonArray>();
    return;
  }
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) {
    Serial.println("Failed to open queue file");
    queueDoc.to<JsonArray>();
    return;
  }
  DeserializationError err = deserializeJson(queueDoc, f);
  f.close();
  if (err) {
    Serial.print("Failed to parse queue file, err: ");
    Serial.println(err.c_str());
    queueDoc.clear();
    queueDoc.to<JsonArray>();
  }
}

// ---------- queue ops ----------

bool queueEmpty() {
  JsonArray arr = queueDoc.as<JsonArray>();
  return arr.size() == 0;
}

JsonObject getCurrentOrder() {
  JsonArray arr = queueDoc.as<JsonArray>();
  if (arr.size() == 0) {
    StaticJsonDocument<1> tmp;
    return tmp.to<JsonObject>(); // empty null object
  }
  return arr[0].as<JsonObject>();
}

void removeCurrentOrderFromQueue() {
  JsonArray arr = queueDoc.as<JsonArray>();
  if (arr.size() == 0) return;
  // remove index 0
  arr.remove(0);
  saveQueueToFile();
}

// ---------- display ----------

void displayCurrentOrder() {
  // ensure memory reflects file
  loadQueueFromFile();

  lcd.clear();
  if (queueEmpty()) {
    lcd.setCursor(0,0);
    lcd.print("No orders queued");
    return;
  }

  JsonObject cur = getCurrentOrder();
  int tableNum = cur["tableNumber"] | 0;

  // Line 1: only table number - use char buffer instead of String
  lcd.setCursor(0,0);
  char line1[21]; // 20 chars + null terminator
  snprintf(line1, sizeof(line1), "Table: %d", tableNum);
  lcd.print(line1);

  // Lines 2-4: items starting at itemScrollIndex
  JsonArray items = cur["items"].as<JsonArray>();
  int itemCount = items.size();
  char itemBuffer[21]; // Reuse buffer for each line
  
  for (int r = 0; r < 3; ++r) {
    lcd.setCursor(0, r + 1);
    int idx = itemScrollIndex + r;
    if (idx < itemCount) {
      const char* name = items[idx]["name"] | "";
      int qty = items[idx]["quantity"] | 0;
      
      // Use snprintf instead of String concatenation
      snprintf(itemBuffer, sizeof(itemBuffer), "%.15s x%d", name, qty);
      lcd.print(itemBuffer);
    } else {
      // clear line efficiently
      lcd.print("                    ");
    }
  }
}

// ---------- publish done ----------

void publishOrderDone(const char* orderId) {
  StaticJsonDocument<64> doc; // Reduced from 128 to 64
  doc["orderId"] = orderId;
  
  char jsonBuffer[80]; // Fixed-size buffer instead of String
  size_t len = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  
  if (len > 0 && mqtt.connected()) {
    if (mqtt.publish(TOPIC_DONE, jsonBuffer)) {
      Serial.printf("Published done: %s\n", jsonBuffer);
    } else {
      Serial.println("Failed to publish done");
    }
  } else if (!mqtt.connected()) {
    Serial.println("MQTT not connected; cannot publish done");
  } else {
    Serial.println("Failed to serialize done message");
  }
}

// ---------- buzzer ----------

void beepTwice() {
  // Using tone() (ESP8266 supports tone). two short beeps.
  const unsigned int freq = 2000;
  const unsigned int dur = 140; // ms
  tone(BUZZER_PIN, freq, dur);
  delay(dur + 80);
  tone(BUZZER_PIN, freq, dur);
  delay(dur + 20);
  noTone(BUZZER_PIN);
}

// ---------- button testing ----------

void testButtons() {
  Serial.println("=== BUTTON TEST MODE ===");
  Serial.println("Press buttons to test. Will run for 30 seconds...");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Button Test Mode");
  lcd.setCursor(0,1);
  lcd.print("D5=Done, D7=Scroll");
  
  unsigned long testStart = millis();
  const unsigned long TEST_DURATION = 30000; // 30 seconds
  
  bool lastDone = HIGH;
  bool lastScroll = HIGH;
  
  while (millis() - testStart < TEST_DURATION) {
    bool currentDone = digitalRead(BUTTON_DONE_PIN);
    bool currentScroll = digitalRead(BUTTON_SCROLL_PIN);
    
    // Check for state changes
    if (currentDone != lastDone) {
      Serial.printf("DONE button: %s\\n", currentDone == LOW ? "PRESSED" : "RELEASED");
      lcd.setCursor(0,2);
      lcd.print(currentDone == LOW ? "Done: PRESSED  " : "Done: released ");
      lastDone = currentDone;
    }
    
    if (currentScroll != lastScroll) {
      Serial.printf("SCROLL button: %s\\n", currentScroll == LOW ? "PRESSED" : "RELEASED");
      lcd.setCursor(0,3);
      lcd.print(currentScroll == LOW ? "Scroll: PRESSED" : "Scroll: released");
      lastScroll = currentScroll;
    }
    
    delay(50); // Small delay for debouncing
  }
  
  Serial.println("=== END BUTTON TEST ===");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Test Complete");
  delay(1000);
}
