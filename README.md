# IoT-Based Smart Pill Dispenser

An academic biomedical engineering project focused on developing an automated pill dispensing system using an ESP32 microcontroller.

## Project Overview

The Smart Pill Dispenser is designed to help organize medication schedules and simplify daily medicine collection. It uses a rotating 15-slot mechanism controlled by a motor, with an RTC module to maintain time.

## Key Features

- 15-slot rotating pill storage mechanism
- Scheduled morning and evening dispensing
- ESP32-based control system
- DS3231 real-time clock module
- LCD display for system messages
- Push-button confirmation after dispensing

## Hardware Components

- ESP32
- DS3231 RTC module
- Stepper motor
- 15-slot rotating mechanism
- 16x2 I2C LCD
- Push button
- Red and green LEDs


## Software and Libraries

Developed using Arduino IDE and ESP32 Arduino/C++ libraries.

The supplied source code includes libraries for Wi-Fi, Telegram integration, asynchronous web server functionality, RTC, LCD, stepper motor control and preferences storage. Some declared integrations are not implemented in the supplied code version.

## Working Principle

1. The RTC module provides the current time.
2. The ESP32 checks the configured morning and evening medication schedules.
3. When a scheduled time is reached, the stepper motor rotates the mechanism by one slot.
4. The LCD displays a message asking the user to collect the medicine.
5. The user presses the confirmation button after collecting the dose.

## Project Structure

- `code/` – Arduino source code
- `documentation/` – Project documentation

## Team Contributions

This was a group academic project. The main system implementation and integration were carried out by me, while teammates contributed to project cost analysis and balancing.

## Development Assistance

AI tools were used as supporting resources for technical guidance, troubleshooting and development assistance.

## Disclaimer

This project is an academic prototype and is not a certified medical device. It should not be relied upon for actual medication management without appropriate safety validation and supervision.

## Future Improvements

- Complete Telegram notification integration
- Implement a functional web dashboard
- Add persistent medication schedule configuration
- Improve dispensing accuracy and safety mechanisms
