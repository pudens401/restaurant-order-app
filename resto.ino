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

// JSON storage: adjust if you expect huge queues (ESP8266 limited RAM)
StaticJsonDocument<12288> queueDoc; // holds the array of orders

// Button debounce
const unsigned long DEBOUNCE_MS = 100; // Increased for better reliability
bool lastDoneState = HIGH;
bool lastScrollState = HIGH;
unsigned long lastDebounceDone = 0;
unsigned long lastDebounceScroll = 0;
bool donePressed = false;
bool scrollPressed = false;

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
  // Ensure WiFi (WiFiManager keeps credentials, WiFi reconnects automatically in background)
  if (WiFi.status() != WL_CONNECTED) {
    // optional: you could call WiFi.reconnect(); but WiFiManager/ESP stack usually handles it.
  }

  // MQTT reconnect / loop
  if (!mqtt.connected()) {
    connectMQTT();
  } else {
    mqtt.loop();
  }

  // Button done (D5) debounce
  bool readingDone = digitalRead(BUTTON_DONE_PIN);
  if (readingDone != lastDoneState) {
    lastDebounceDone = millis();
  }
  if ((millis() - lastDebounceDone) > DEBOUNCE_MS) {
    if (readingDone != lastDoneState) {
      lastDoneState = readingDone;
      if (readingDone == LOW) { // Button pressed (active LOW)
        donePressed = true;
        Serial.println("DEBUG: Done button pressed!");
      }
    }
  }

  // Button scroll (D7) debounce
  bool readingScroll = digitalRead(BUTTON_SCROLL_PIN);
  if (readingScroll != lastScrollState) {
    lastDebounceScroll = millis();
  }
  if ((millis() - lastDebounceScroll) > DEBOUNCE_MS) {
    if (readingScroll != lastScrollState) {
      lastScrollState = readingScroll;
      if (readingScroll == LOW) { // Button pressed (active LOW)
        scrollPressed = true;
        Serial.println("DEBUG: Scroll button pressed!");
      }
    }
  }

  // handle done press
  if (donePressed) {
    donePressed = false;
    Serial.println("DEBUG: Processing done button press");
    if (!queueEmpty()) {
      JsonObject cur = getCurrentOrder();
      const char* oid = cur["orderId"] | "";
      Serial.printf("DEBUG: Marking order %s as done\n", oid);
      if (strlen(oid) > 0) {
        publishOrderDone(oid);
      }
      removeCurrentOrderFromQueue();
      itemScrollIndex = 0;
      displayCurrentOrder();
    } else {
      // no orders - display message briefly
      Serial.println("DEBUG: No orders to mark as done");
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("No orders queued");
      delay(500);
      displayCurrentOrder();
    }
  }

  // handle scroll press
  if (scrollPressed) {
    scrollPressed = false;
    Serial.println("DEBUG: Processing scroll button press");
    if (!queueEmpty()) {
      JsonObject cur = getCurrentOrder();
      JsonArray items = cur["items"].as<JsonArray>();
      int itemCount = items.size();
      Serial.printf("DEBUG: Item count: %d, current scroll index: %d\n", itemCount, itemScrollIndex);
      if (itemCount > 3) {
        int maxStart = itemCount - 3;
        itemScrollIndex++;
        if (itemScrollIndex > maxStart) itemScrollIndex = 0;
        Serial.printf("DEBUG: New scroll index: %d\n", itemScrollIndex);
      } else {
        itemScrollIndex = 0; // nothing to scroll
        Serial.println("DEBUG: Not enough items to scroll");
      }
      displayCurrentOrder();
    } else {
      Serial.println("DEBUG: No orders to scroll through");
    }
  }

  // short small sleep to reduce CPU hogging
  delay(10);
}

// ---------- MQTT ----------

void connectMQTT() {
  static unsigned long lastAttempt = 0;
  const unsigned long ATTEMPT_INTERVAL = 5000;
  if (millis() - lastAttempt < ATTEMPT_INTERVAL) return;
  lastAttempt = millis();

  String clientId = "ESP-" + String(ESP.getChipId(), HEX);
  Serial.print("Connecting to MQTT...");
  if (strlen(MQTT_USER) > 0) {
    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      mqtt.subscribe(TOPIC_NEW);
      Serial.printf("Subscribed to %s\n", TOPIC_NEW);
    } else {
      Serial.printf("failed, rc=%d\n", mqtt.state());
    }
  } else {
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("connected");
      mqtt.subscribe(TOPIC_NEW);
      Serial.printf("Subscribed to %s\n", TOPIC_NEW);
    } else {
      Serial.printf("failed, rc=%d\n", mqtt.state());
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // parse payload as string
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.print("MQTT message on ");
  Serial.print(topic);
  Serial.printf(" (Length: %u bytes): ", length);
  Serial.println(msg);
  
  // Debug: Check if message is too large
  if (length > 3500) {
    Serial.println("WARNING: Message size approaching JSON buffer limit!");
  }

  // parse JSON (message size may vary - increased for multi-item orders)
  StaticJsonDocument<4096> doc; // Increased from 2048 to 4096 bytes
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    Serial.printf("Message length was: %u bytes\n", length);
    Serial.println("First 200 chars of message:");
    Serial.println(msg.substring(0, 200));
    return;
  }
  
  Serial.println("✅ JSON parsed successfully");
  Serial.printf("📊 Free heap: %u bytes\n", ESP.getFreeHeap());

  if (!doc.containsKey("orderId")) {
    Serial.println("No orderId found; ignoring");
    return;
  }

  // Debug: Print order details
  const char* orderId = doc["orderId"];
  int tableNum = doc["tableNumber"];
  JsonArray items = doc["items"];
  Serial.printf("📋 Order: %s, Table: %d, Items: %d\n", orderId, tableNum, items.size());

  // append to queueDoc array
  JsonArray arr = queueDoc.as<JsonArray>();
  // create nested object and copy fields
  JsonObject newOrder = arr.createNestedObject();
  for (JsonPair kv : doc.as<JsonObject>()) {
    // copy values (supports nested items array)
    newOrder[kv.key()] = kv.value();
  }

  saveQueueToFile();

  // beep twice for new order
  beepTwice();

  // if this is the only order, show it
  if (arr.size() == 1) {
    itemScrollIndex = 0;
    displayCurrentOrder();
  }
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

  // Line 1: only table number
  lcd.setCursor(0,0);
  String line1 = "Table: " + String(tableNum);
  if (line1.length() > 20) line1 = line1.substring(0, 20);
  lcd.print(line1);

  // Lines 2-4: items starting at itemScrollIndex
  JsonArray items = cur["items"].as<JsonArray>();
  int itemCount = items.size();
  for (int r = 0; r < 3; ++r) {
    lcd.setCursor(0, r + 1);
    int idx = itemScrollIndex + r;
    if (idx < itemCount) {
      const char* name = items[idx]["name"] | "";
      int qty = items[idx]["quantity"] | 0;
      String l = String(name) + " x" + String(qty);
      if (l.length() > 20) l = l.substring(0, 20);
      lcd.print(l);
    } else {
      // clear line
      lcd.print("                    ");
    }
  }
}

// ---------- publish done ----------

void publishOrderDone(const char* orderId) {
  StaticJsonDocument<128> doc;
  doc["orderId"] = orderId;
  String out;
  serializeJson(doc, out);
  if (mqtt.connected()) {
    if (mqtt.publish(TOPIC_DONE, out.c_str())) {
      Serial.printf("Published done: %s\n", out.c_str());
    } else {
      Serial.println("Failed to publish done");
    }
  } else {
    Serial.println("MQTT not connected; cannot publish done");
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
