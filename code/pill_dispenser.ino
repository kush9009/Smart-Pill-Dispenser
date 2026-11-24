// ESP32 Smart Pill Dispenser - Full Firmware
// 15-Slot Side Gear Drive (14 Medicine + 1 Refill Slot)
// Small gear: 8T | Big ring: 60T (7.5:1 ratio)
// Half-step (4096 steps/rev) -> 2048 steps/slot

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <AccelStepper.h>
#include <Preferences.h>

// === FILL THESE ===
const char* WIFI_SSID = "Apple";
const char* WIFI_PASS = "qwerty123";
const String BOT_TOKEN = "YOUR_BOT_TOKEN";
const String CHAT_ID = "YOUR_CHAT_ID";

// Pins
#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27
#define BUTTON_PIN 4
#define LED_RED 2
#define LED_GREEN 15

// Wheel & Schedule Constants
#define TOTAL_SLOTS 15
#define STEPS_PER_SLOT 2048
#define REFILL_SLOT 1
#define LOW_SUPPLY_THRESHOLD 4
#define TELEGRAM_POLL_MS 3000
#define MISSED_REMINDER_INTERVAL_MS 600000UL // 10 minutes
#define MAX_MISSED_REMINDERS 3

RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
AsyncWebServer server(80);
Preferences prefs;

// Schedule (default 08:00 and 21:00)
int morningHour = 8, morningMin = 0;
int eveningHour = 21, eveningMin = 0;

// State
int currentSlot = 1;
int lastDay = -1;
bool morningDone = false;
bool eveningDone = false;
bool waitingForBtn = false;
String doseLabel = "";
int missedRemindersSent = 0;
unsigned long lastDispenseMillis = 0;
unsigned long lastReminderMillis = 0;
unsigned long lastTelegramPoll = 0;

// --- Utility Functions ---
String pad(int n) {
  return (n < 10 ? "0" : "") + String(n);
}

String formatTime(int h, int m) {
  return pad(h) + ":" + pad(m);
}

void lcdShow(String a, String b) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(a.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(b.substring(0, 16));
}

void saveState() {
  prefs.begin("pill_disp", false);
  prefs.putInt("slot", currentSlot);
  prefs.putInt("mHour", morningHour);
  prefs.putInt("mMin", morningMin);
  prefs.putInt("eHour", eveningHour);
  prefs.putInt("eMin", eveningMin);
  prefs.end();
}

void loadState() {
  prefs.begin("pill_disp", true);
  currentSlot = prefs.getInt("slot", 1);
  if (currentSlot < 1 || currentSlot > TOTAL_SLOTS) currentSlot = 1;
  morningHour = prefs.getInt("mHour", 8);
  morningMin = prefs.getInt("mMin", 0);
  eveningHour = prefs.getInt("eHour", 21);
  eveningMin = prefs.getInt("eMin", 0);
  prefs.end();
}

int getRemainingMedicineSlots() {
  // Slot 1 is the refill slot; slots 2..15 hold medicine (14 total)
  if (currentSlot == REFILL_SLOT) return 0;
  if (currentSlot > REFILL_SLOT) {
    return TOTAL_SLOTS - currentSlot + 1;
  }
  return 0;
}

void sendTelegram(const String& msg) {
  if (WiFi.status() == WL_CONNECTED && BOT_TOKEN != "YOUR_BOT_TOKEN" && CHAT_ID != "YOUR_CHAT_ID") {
    bot.sendMessage(CHAT_ID, msg, "");
  }
}

// --- Mechanical & Dispense Logic ---
void advanceOneSlot() {
  stepper.move(STEPS_PER_SLOT);
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  currentSlot++;
  if (currentSlot > TOTAL_SLOTS) {
    currentSlot = 1;
  }
  saveState();
}

void dispense(String label) {
  int nextSlot = currentSlot + 1;
  if (nextSlot > TOTAL_SLOTS) nextSlot = 1;

  advanceOneSlot();

  if (currentSlot == REFILL_SLOT) {
    waitingForBtn = false;
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, LOW);
    lcdShow("Refill Needed!", "Slot 1 Empty");
    sendTelegram("Refill Alert: Pill wheel has reached the empty refill slot (Slot " + String(currentSlot) + "). Please refill all medicine slots.");
    return;
  }

  doseLabel = label;
  waitingForBtn = true;
  missedRemindersSent = 0;
  lastDispenseMillis = millis();
  lastReminderMillis = millis();

  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, LOW);

  lcdShow("Take " + label, "Press button");
  sendTelegram("Dose Alert: Time for your " + label + " medicine (Slot " + String(currentSlot) + "/" + String(TOTAL_SLOTS) + "). Please press the button after taking it.");

  int remaining = getRemainingMedicineSlots();
  if (remaining > 0 && remaining <= LOW_SUPPLY_THRESHOLD) {
    sendTelegram("Low Supply Warning: Only " + String(remaining) + " medicine slot(s) remaining before refill.");
  }
}

void confirmDoseTaken() {
  waitingForBtn = false;
  missedRemindersSent = 0;
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, HIGH);

  lcdShow("Taken", "Good job");
  sendTelegram("Confirmed: " + doseLabel + " dose (Slot " + String(currentSlot) + ") was marked as taken.");
  delay(2000);
  digitalWrite(LED_GREEN, LOW);
}

void resetRefillSlot() {
  currentSlot = 1;
  waitingForBtn = false;
  missedRemindersSent = 0;
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, HIGH);
  saveState();
  lcdShow("Refill Reset", "Slot set to 1");
  sendTelegram("Refill Reset: Slot counter reset to Slot 1.");
  delay(1500);
  digitalWrite(LED_GREEN, LOW);
}

bool parseHHMM(const String& input, int& outH, int& outM) {
  int colonIdx = input.indexOf(':');
  if (colonIdx < 1) return false;
  int h = input.substring(0, colonIdx).toInt();
  int m = input.substring(colonIdx + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  outH = h;
  outM = m;
  return true;
}

// --- Telegram Bot Command Handler ---
String buildStatusText() {
  DateTime now = rtc.now();
  String status = "ESP32 Pill Dispenser Status\n";
  status += "Time: " + pad(now.hour()) + ":" + pad(now.minute()) + ":" + pad(now.second()) + "\n";
  status += "Current Slot: " + String(currentSlot) + " / " + String(TOTAL_SLOTS) + "\n";
  status += "Medicine Slots Left: " + String(getRemainingMedicineSlots()) + "\n";
  status += "Morning (" + formatTime(morningHour, morningMin) + "): " + (morningDone ? "Done" : "Pending") + "\n";
  status += "Evening (" + formatTime(eveningHour, eveningMin) + "): " + (eveningDone ? "Done" : "Pending") + "\n";
  status += "Awaiting Confirmation: " + String(waitingForBtn ? ("Yes (" + doseLabel + ")") : "No");
  return status;
}

void handleTelegramMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chatId = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    text.trim();

    if (CHAT_ID != "YOUR_CHAT_ID" && chatId != CHAT_ID) {
      continue;
    }

    if (text == "/help" || text == "/start") {
      String helpMsg = "Available Commands:\n";
      helpMsg += "/help - List commands\n";
      helpMsg += "/status - Current time, slot & dose status\n";
      helpMsg += "/setmorning HH:MM - Set morning dose time\n";
      helpMsg += "/setevening HH:MM - Set evening dose time\n";
      helpMsg += "/reset - Reset slot counter after refill\n";
      helpMsg += "/calibrate N - Set current slot to N (1..15)";
      bot.sendMessage(chatId, helpMsg, "");
    } else if (text == "/status") {
      bot.sendMessage(chatId, buildStatusText(), "");
    } else if (text.startsWith("/setmorning ")) {
      String timeStr = text.substring(12);
      timeStr.trim();
      int h, m;
      if (parseHHMM(timeStr, h, m)) {
        morningHour = h;
        morningMin = m;
        saveState();
        bot.sendMessage(chatId, "Morning dose updated to " + formatTime(morningHour, morningMin), "");
      } else {
        bot.sendMessage(chatId, "Invalid format. Use /setmorning HH:MM (e.g., /setmorning 08:30)", "");
      }
    } else if (text.startsWith("/setevening ")) {
      String timeStr = text.substring(12);
      timeStr.trim();
      int h, m;
      if (parseHHMM(timeStr, h, m)) {
        eveningHour = h;
        eveningMin = m;
        saveState();
        bot.sendMessage(chatId, "Evening dose updated to " + formatTime(eveningHour, eveningMin), "");
      } else {
        bot.sendMessage(chatId, "Invalid format. Use /setevening HH:MM (e.g., /setevening 21:00)", "");
      }
    } else if (text == "/reset") {
      resetRefillSlot();
      bot.sendMessage(chatId, "Slot counter reset to 1.", "");
    } else if (text.startsWith("/calibrate ")) {
      int n = text.substring(11).toInt();
      if (n >= 1 && n <= TOTAL_SLOTS) {
        currentSlot = n;
        saveState();
        bot.sendMessage(chatId, "Calibrated current slot to " + String(currentSlot) + ".", "");
      } else {
        bot.sendMessage(chatId, "Invalid slot number. Enter a value between 1 and " + String(TOTAL_SLOTS) + ".", "");
      }
    } else {
      bot.sendMessage(chatId, "Unknown command. Send /help for options.", "");
    }
  }
}

// --- Web Dashboard ---
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Pill Dispenser</title>
<style>
  body { font-family: Arial, sans-serif; background: #f4f6f9; color: #222; margin: 0; padding: 16px; }
  .container { max-width: 520px; margin: auto; }
  .card { background: #fff; border-radius: 12px; padding: 16px; margin-bottom: 14px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }
  h1 { font-size: 1.35rem; margin: 0 0 12px 0; }
  .row { display: flex; justify-content: space-between; margin: 6px 0; font-size: 0.95rem; }
  .bar-bg { background: #e5e7eb; border-radius: 999px; height: 14px; overflow: hidden; margin: 10px 0; }
  .bar-fill { background: #2563eb; height: 100%; width: 0%; transition: width 0.3s ease; }
  label { display: block; margin-top: 8px; font-size: 0.9rem; }
  input[type="time"] { width: 100%; padding: 8px; margin-top: 4px; border: 1px solid #ccc; border-radius: 6px; box-sizing: border-box; }
  button { margin-top: 12px; width: 100%; padding: 10px; border: none; border-radius: 8px; font-size: 0.95rem; cursor: pointer; }
  .btn-primary { background: #2563eb; color: #fff; }
  .btn-warn { background: #d97706; color: #fff; }
  .badge-done { color: #15803d; font-weight: bold; }
  .badge-pend { color: #b45309; font-weight: bold; }
</style>
</head>
<body>
<div class="container">
  <div class="card">
    <h1>Smart Pill Dispenser</h1>
    <div class="row"><span>Date &amp; Time:</span><strong id="dt">--</strong></div>
    <div class="row"><span>Current Slot:</span><strong id="slot">--</strong></div>
    <div class="row"><span>Medicine Remaining:</span><strong id="daysLeft">--</strong></div>
    <div class="bar-bg"><div class="bar-fill" id="progressBar"></div></div>
  </div>

  <div class="card">
    <div class="row"><span>Morning Dose (<span id="mTime">--:--</span>):</span><span id="mStatus">--</span></div>
    <div class="row"><span>Evening Dose (<span id="eTime">--:--</span>):</span><span id="eStatus">--</span></div>
    <div class="row"><span>Awaiting Button Press:</span><strong id="waitBtn">--</strong></div>
  </div>

  <div class="card">
    <strong>Schedule Editor</strong>
    <label>Morning Time <input type="time" id="mInput"></label>
    <label>Evening Time <input type="time" id="eInput"></label>
    <button class="btn-primary" onclick="saveSchedule()">Save Schedule</button>
    <button class="btn-warn" onclick="resetRefill()">Refill Reset (Slot 1)</button>
  </div>
</div>

<script>
async function refreshState() {
  try {
    const res = await fetch('/status');
    const d = await res.json();
    document.getElementById('dt').textContent = d.datetime;
    document.getElementById('slot').textContent = d.slot + ' / ' + d.totalSlots;
    const medSlots = d.medSlotsLeft;
    const days = (medSlots / 2).toFixed(1);
    document.getElementById('daysLeft').textContent = medSlots + ' slots (~' + days + ' days)';
    const pct = Math.round((medSlots / 14) * 100);
    document.getElementById('progressBar').style.width = pct + '%';

    document.getElementById('mTime').textContent = d.morningTime;
    document.getElementById('eTime').textContent = d.eveningTime;
    document.getElementById('mStatus').textContent = d.morningDone ? 'Taken' : 'Pending';
    document.getElementById('mStatus').className = d.morningDone ? 'badge-done' : 'badge-pend';
    document.getElementById('eStatus').textContent = d.eveningDone ? 'Taken' : 'Pending';
    document.getElementById('eStatus').className = d.eveningDone ? 'badge-done' : 'badge-pend';
    document.getElementById('waitBtn').textContent = d.waitingForBtn ? ('Yes (' + d.doseLabel + ')') : 'No';

    if (document.activeElement.tagName !== 'INPUT') {
      document.getElementById('mInput').value = d.morningTime;
      document.getElementById('eInput').value = d.eveningTime;
    }
  } catch (e) {}
}

async function saveSchedule() {
  const m = document.getElementById('mInput').value;
  const e = document.getElementById('eInput').value;
  if (!m || !e) return;
  await fetch('/set?morning=' + encodeURIComponent(m) + '&evening=' + encodeURIComponent(e));
  refreshState();
}

async function resetRefill() {
  await fetch('/reset');
  refreshState();
}

refreshState();
setInterval(refreshState, 10000);
</script>
</body>
</html>
)rawliteral";

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", DASHBOARD_HTML);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    DateTime now = rtc.now();
    String dt = String(now.year()) + "-" + pad(now.month()) + "-" + pad(now.day()) + " " +
                pad(now.hour()) + ":" + pad(now.minute()) + ":" + pad(now.second());

    String json = "{";
    json += "\"datetime\":\"" + dt + "\",";
    json += "\"slot\":" + String(currentSlot) + ",";
    json += "\"totalSlots\":" + String(TOTAL_SLOTS) + ",";
    json += "\"medSlotsLeft\":" + String(getRemainingMedicineSlots()) + ",";
    json += "\"morningTime\":\"" + formatTime(morningHour, morningMin) + "\",";
    json += "\"eveningTime\":\"" + formatTime(eveningHour, eveningMin) + "\",";
    json += "\"morningDone\":" + String(morningDone ? "true" : "false") + ",";
    json += "\"eveningDone\":" + String(eveningDone ? "true" : "false") + ",";
    json += "\"waitingForBtn\":" + String(waitingForBtn ? "true" : "false") + ",";
    json += "\"doseLabel\":\"" + doseLabel + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest* request) {
    int h, m;
    if (request->hasParam("morning") && parseHHMM(request->getParam("morning")->value(), h, m)) {
      morningHour = h;
      morningMin = m;
    }
    if (request->hasParam("evening") && parseHHMM(request->getParam("evening")->value(), h, m)) {
      eveningHour = h;
      eveningMin = m;
    }
    saveState();
    request->send(200, "text/plain", "OK");
  });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
    resetRefillSlot();
    request->send(200, "text/plain", "OK");
  });

  server.begin();
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);

  loadState();

  stepper.setMaxSpeed(600);
  stepper.setAcceleration(200);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  if (!rtc.begin()) {
    lcdShow("RTC Error", "Check wiring");
    while (1);
  }

  // Uncomment once to set RTC time to compile time, then re-comment and re-upload:
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  DateTime now = rtc.now();
  lastDay = now.day();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcdShow("Connecting", "WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  client.setInsecure();
  setupWebServer();

  lcdShow("IP: " + WiFi.localIP().toString(), "Slot " + String(currentSlot) + " Ready");
  sendTelegram("System Online. IP: " + WiFi.localIP().toString() + " | Current Slot: " + String(currentSlot));
}

// --- Loop ---
void loop() {
  DateTime now = rtc.now();

  // Reset daily flags when date changes
  if (now.day() != lastDay) {
    lastDay = now.day();
    morningDone = false;
    eveningDone = false;
  }

  // Scheduled Morning Dispense
  if (now.hour() == morningHour && now.minute() == morningMin && !morningDone) {
    morningDone = true;
    dispense("Morning");
  }

  // Scheduled Evening Dispense
  if (now.hour() == eveningHour && now.minute() == eveningMin && !eveningDone) {
    eveningDone = true;
    dispense("Evening");
  }

  // Confirmation Button Check (active LOW with INPUT_PULLUP)
  if (waitingForBtn && digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      confirmDoseTaken();
    }
  }

  // Missed Dose Reminders (up to 3 times at configured interval)
  if (waitingForBtn && missedRemindersSent < MAX_MISSED_REMINDERS) {
    if (millis() - lastReminderMillis >= MISSED_REMINDER_INTERVAL_MS) {
      lastReminderMillis = millis();
      missedRemindersSent++;
      sendTelegram("Missed Dose Reminder (" + String(missedRemindersSent) + "/" + String(MAX_MISSED_REMINDERS) + "): " + doseLabel + " dose (Slot " + String(currentSlot) + ") has not been confirmed yet.");
    }
  }

  // Poll Telegram Bot every 3 seconds
  if (millis() - lastTelegramPoll >= TELEGRAM_POLL_MS) {
    lastTelegramPoll = millis();
    if (WiFi.status() == WL_CONNECTED && BOT_TOKEN != "YOUR_BOT_TOKEN") {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        handleTelegramMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
    }
  }

  // Idle LCD display when not waiting on button
  if (!waitingForBtn) {
    static unsigned long lastLcdUpdate = 0;
    if (millis() - lastLcdUpdate >= 1000) {
      lastLcdUpdate = millis();
      lcd.setCursor(0, 0);
      lcd.print(("Time " + pad(now.hour()) + ":" + pad(now.minute()) + ":" + pad(now.second()) + "   ").substring(0, 16));
      lcd.setCursor(0, 1);
      lcd.print(("Slot:" + String(currentSlot) + "/" + String(TOTAL_SLOTS) + " Med:" + String(getRemainingMedicineSlots()) + "   ").substring(0, 16));
    }
  }
}
