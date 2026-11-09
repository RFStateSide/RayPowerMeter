// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#pragma once
#include <Arduino.h>

/* ============================================================================
 *  RAY POWER METER — PIN UTILIZATION SUMMARY (ESP32-S3)
 * ============================================================================
 *  PURPOSE:
 *    • Documents all GPIO usage
 *    • Lists reserved pins (USB, Flash/PSRAM, strapping)
 *    • Identifies free pins
 *    • Clarifies ADC-capable pins (20 total)
 *
 * ----------------------------------------------------------------------------
 *  ESP32-S3 GPIO + ADC NOTES
 * ----------------------------------------------------------------------------
 *  • All pins are 3.3 V only.
 *  • USB Pins (DO NOT USE):
 *        GPIO19 = USB_D–
 *        GPIO20 = USB_D+
 *
 *  • Flash/PSRAM Pins (DO NOT USE):
 *        GPIO35 / GPIO36 / GPIO37
 *
 *  • Strapping Pins (DO NOT USE):
 *        GPIO0, GPIO3, GPIO45, GPIO46
 *
 *  • ADC CAPABILITY:
 *      ADC1_CH0–CH9 → GPIO1–GPIO10
 *      ADC2_CH0–CH9 → GPIO11–GPIO20
 *
 * ----------------------------------------------------------------------------
 *                             GPIOs USED BY PROJECT
 * ----------------------------------------------------------------------------
 *
 *  UART1 — Nextion HMI
 *      GPIO17  UART_HMI_RX_PIN
 *      GPIO18  UART_HMI_TX_PIN
 *
 *  UART2 — Remote Protocol
 *      GPIO6   UART_REMOTE_TX_PIN
 *      GPIO7   UART_REMOTE_RX_PIN
 *
 *  Coupler ADC Inputs (ADC1 + ADC2 channels)
 *      GPIO1   COUPLER1_FWD_PIN (ADC1_0)
 *      GPIO2   COUPLER1_REF_PIN (ADC1_1)
 *      GPIO4   COUPLER2_FWD_PIN (ADC1_3)
 *      GPIO5   COUPLER2_REF_PIN (ADC1_4)
 *      GPIO8   COUPLER3_FWD_PIN (ADC1_7)
 *      GPIO9   COUPLER3_REF_PIN (ADC1_8)
 *      GPIO10  COUPLER4_FWD_PIN (ADC1_9)
 *      GPIO11  COUPLER4_REF_PIN (ADC2_0)
 *
 *  AUX Analog Inputs (moved to real ADC-capable pins)
 *      GPIO13  AUX1_ADC_PIN  (ADC2_CH2)
 *      GPIO16  AUX2_ADC_PIN  (ADC2_CH5)
 *      GPIO12  AUX3_ADC_PIN  (ADC2_CH1)
 *
 *  Protection Interrupts
 *      GPIO14  INT_PROTECT_1_PIN
 *      GPIO15  INT_PROTECT_2_PIN
 *
 *  Relay Output (moved OFF ADC pin)
 *      GPIO40  ALARM_RELAY_PIN
 *
 *  LEDs
 *      GPIO38  LED_STATUS_PIN
 *      GPIO39  LED_ERROR_PIN
 *
 *  Frequency Counter Input (digital only)
 *     GPIO21  FREQ_INPUT_PIN
 *
 * ----------------------------------------------------------------------------
 *                          FREE / AVAILABLE GPIOs
 * ----------------------------------------------------------------------------
 *
 *  SAFE DIGITAL I/O:
 *      GPIO12  (was relay; now free)
 *
 *  USE WITH CAUTION:
 *      GPIO0   (strap)
 *      GPIO3   (strap; ADC1_CH2)
 *      GPIO45  (strap; input only)
 *      GPIO46  (strap; input only)
 *
 *  DO NOT USE:
 *      GPIO19 / GPIO20  (USB)
 *      GPIO35 / GPIO36 / GPIO37  (Flash/PSRAM)
 *
 * ----------------------------------------------------------------------------
 *                 BEST PINS FOR FUTURE ANALOG INPUT EXPANSION
 * ----------------------------------------------------------------------------
 *      ADC2: GPIO11, GPIO12, GPIO13, GPIO14, GPIO15, GPIO16, GPIO17, GPIO18
 *
 * ============================================================================
 */

// ======================= UARTs ==========================
#define UART_USB Serial     // Native USB CDC console/programming
#define UART_HMI Serial1    // Nextion HMI (optional)
#define UART_REMOTE Serial2 // Optional spare UART (used for comm / serial remote API)

// UART1 (Nextion) — use the pins that natively expose U1TXD/U1RXD on this board
constexpr int UART_HMI_RX_PIN = 17; // ESP32 RX1  <- Nextion TX (level shifted if Nextion = 5 V)
constexpr int UART_HMI_TX_PIN = 18; // ESP32 TX1  -> Nextion RX (3.3 V OK)
/*
 * Nextion 4-pin:
 *   1: GND  -> ESP32 GND
 *   2: RX   <- ESP32 TX1 (GPIO17)  [3.3 V OK] Yellow wire
 *   3: TX   -> ESP32 RX1 (GPIO18)  [use divider (3.3K/1.8K) Nextion TX is 5 V] Blue wire
 */

// UART2 (used for comm / serial remote API)
constexpr int UART_REMOTE_TX_PIN = 6;
constexpr int UART_REMOTE_RX_PIN = 7;

// ======================= Coupler ADC inputs ==========================
/*
 * 4 couplers, each with Forward and Reflected detector outputs.
 * These must be on ADC-capable pins.
 *
 * Coupler naming:
 *   COUPLER_1: typically "In-line #1" (e.g., exciter input)
 *   COUPLER_2: PA input
 *   COUPLER_3: PA output
 *   COUPLER_4: Antenna output / external sensor
 *
 * If hardware changes, only this file should need updates.
 */

// Coupler 1
constexpr int COUPLER1_FWD_PIN = 1; // ADC-capable GPIO
constexpr int COUPLER1_REF_PIN = 2;

// Coupler 2
constexpr int COUPLER2_FWD_PIN = 5;
constexpr int COUPLER2_REF_PIN = 4;

// Coupler 3
constexpr int COUPLER3_FWD_PIN = 9;
constexpr int COUPLER3_REF_PIN = 8;

// Coupler 4
constexpr int COUPLER4_FWD_PIN = 11;
constexpr int COUPLER4_REF_PIN = 10;

// Arrays for easy iteration in code
constexpr int COUPLER_FWD_PINS[4] = {
    COUPLER1_FWD_PIN,
    COUPLER2_FWD_PIN,
    COUPLER3_FWD_PIN,
    COUPLER4_FWD_PIN};

constexpr int COUPLER_REF_PINS[4] = {
    COUPLER1_REF_PIN,
    COUPLER2_REF_PIN,
    COUPLER3_REF_PIN,
    COUPLER4_REF_PIN};

// ======================= AUX analog inputs ==========================
/* Flexible AUX channels for voltage, current & temperature measurement.
 * Hardware can use jumpers to select different divider/shunt networks.
 */
constexpr int AUX1_ADC_PIN = 12; // ADC2_CH1 (Voltage)
constexpr int AUX2_ADC_PIN = 13; // ADC2_CH2 (Current)
constexpr int AUX3_ADC_PIN = 16; // ADC2_CH5 (Temperature)

// =========== AUX digital I/O channels ===================
// Simple logic-level inputs/outputs (3.3 V only).
constexpr int AUXD1_PIN = 43; // Shared with USB UART0 TX
constexpr int AUXD2_PIN = 44; // Shared with USB UART0 RX
constexpr int AUXD3_PIN = 42;
constexpr int AUXD4_PIN = 41;

// ======================= Interrupt lines ==========================
/*
 * Two generic interrupt lines for fast protection / event capture.
 * These might be driven by comparators (over-power, VSWR, etc.),
 * or by an external CPLD/logic block.
 */
constexpr int INT_PROTECT_1_PIN = 14; // e.g., over-power / SWR from final coupler
constexpr int INT_PROTECT_2_PIN = 15; // e.g., input mismatch / other fault

// Helper macros for attachInterrupt usage (rising edge as default)
constexpr int PROTECT_INT_MODE = RISING;

// ======================= Alarm relay ==========================
/*
 * Optional relay output to drive an external alarm / SSR for
 * disabling a power amplifier, etc.
 */
constexpr int ALARM_RELAY_PIN = 40;
#define RELAY_ON HIGH
#define RELAY_OFF LOW

// ======================= Status LEDs ==========================
/*
 * Board may already have USB TX/RX LEDs on 43/44.
 * These are for any external indicators you add.
 */
constexpr int LED_STATUS_PIN = 38; // "OK / heartbeat"
constexpr int LED_ERROR_PIN = 39;  // "Fault / alarm"

#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH 1
#endif

constexpr uint8_t LED_ON = LED_ACTIVE_HIGH ? HIGH : LOW;
constexpr uint8_t LED_OFF = LED_ACTIVE_HIGH ? LOW : HIGH;

// ======================= Other pins ==========================
// Frequency prescaler input (digital square wave from RayCoupler)
constexpr int FREQ_INPUT_PIN = 21;

// ======================= Sanity checks ==========================

static_assert(UART_HMI_RX_PIN != UART_HMI_TX_PIN, "HMI RX/TX pins must be distinct");
static_assert(LED_STATUS_PIN != LED_ERROR_PIN, "Status and error LEDs must not share a pin");

// Basic compile-time guard to catch obvious duplication mistakes:
static_assert(COUPLER1_FWD_PIN != COUPLER1_REF_PIN, "Coupler1 FWD/REF pins must differ");
static_assert(COUPLER2_FWD_PIN != COUPLER2_REF_PIN, "Coupler2 FWD/REF pins must differ");
static_assert(COUPLER3_FWD_PIN != COUPLER3_REF_PIN, "Coupler3 FWD/REF pins must differ");
static_assert(COUPLER4_FWD_PIN != COUPLER4_REF_PIN, "Coupler4 FWD/REF pins must differ");
static_assert(ALARM_RELAY_PIN != LED_STATUS_PIN, "Alarm relay pin must differ from status LED");
static_assert(ALARM_RELAY_PIN != LED_ERROR_PIN, "Alarm relay pin must differ from error LED");
