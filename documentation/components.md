# Hardware Components

| Component | Purpose |
|---|---|
| ESP32 | Main microcontroller |
| DS3231 RTC Module | Maintains real-time clock |
| Stepper Motor | Rotates the pill dispenser mechanism |
| 15-Slot Rotating Mechanism | Holds and organizes medication doses |
| 16x2 I2C LCD | Displays system messages |
| Push Button | Confirms medication collection |
| Red LED | Status indication |
| Green LED | Status indication |
| Wi-Fi Connection | Intended for network connectivity |

## Software and Libraries

- Arduino IDE
- ESP32 Arduino Core
- WiFi
- WiFiClientSecure
- UniversalTelegramBot
- ArduinoJson
- ESPAsyncWebServer
- AsyncTCP
- RTClib
- LiquidCrystal_I2C
- AccelStepper
- Preferences

*Note: The list reflects the libraries included in the supplied source code. Not all declared libraries are actively used in this version.*
