// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include <Arduino.h>
#include "hw/pinout.h"

// RayPowerMeter: NextionDisplay uses ONLY raw serial commands.
// No external Nextion libraries required (no EasyNextionLibrary, no Nextion.h).

// Profile page component names
// (keep in sync with .HMI: tvProfile, tvStatus, bScanCan, bScanI2C)

namespace ui
{

  class NextionDisplay
  {
  public:
    // Pass in the HardwareSerial you wired to the Nextion (e.g., UART_HMI from pinout.h)
    explicit NextionDisplay(HardwareSerial &serial = UART_HMI);

    // Initialize UART and Nextion. Returns true if panel init looks OK.
    bool begin(uint32_t baud = 115200);

    // Page management
    void setPage(uint8_t page);

    // ---- Generic raw setters (for your RayPowerMeter HMI) ----
    void sendCommand(const String &cmd);
    void setText(const String &component, const String &text);
    void setNumber(const String &component, int32_t value);
    void setFloat(const String &component, float value, uint8_t decimals = 2);
    void setProgress(const String &component, uint8_t percent); // 0..100
    void setPic(const String &component, uint16_t picId);
    void setTextPco(const char *objName, uint16_t color);


    // ---- Generic getters ----
    bool getText(const char *comp, String &out, uint32_t timeoutMs = 300);
    bool getNumber(const char *expr, uint32_t &out, uint16_t timeoutMs = 80);

    // --- Quick toast helper (centralizes page return & timing) ---
    // msg: text for toast
    // ms:  duration to show (400–10000 ms)
    // returnPage: page to return to when toast timer fires (0xFF = stay)
    void toast(const char *msg, uint16_t ms, uint8_t returnPage);

    void setToastReturnPage(uint8_t page) { _toastReturnPage = page; }
    uint8_t toastReturnPage() const { return _toastReturnPage; }
    uint8_t consumeToastReturnPage()
    {
      uint8_t p = _toastReturnPage;
      _toastReturnPage = 0xFF;
      return p;
    }

    // --- Runtime brightness (0..100) ---
    void setBrightness(uint8_t pct);

    // --- Read Nextion RTC as UTC fields (rtc0..rtc5) ---
    // returns true on success; fields are: y, m(1-12), d(1-31), hh(0-23), mm, ss
    bool readRtcUTC(uint16_t &y, uint8_t &m, uint8_t &d,
                    uint8_t &hh, uint8_t &mm, uint8_t &ss);

        // --- Set Nextion RTC from UTC fields ---
    void setRtcUTC(uint16_t y, uint8_t m, uint8_t d,
                   uint8_t hh, uint8_t mm, uint8_t ss);

    // --- Set Nextion RTC from Unix epoch (UTC seconds) ---
    void setRtcEpoch(time_t epoch);                    

    // Touch / custom events
    // 0x65 frames: cb(pageId, componentId, state)
    void parseIncoming(std::function<void(uint8_t, uint8_t, uint8_t)> cb);

    // 0x62 frames: cb(pageId, type, instance)
    void onCustomCommand(std::function<void(uint8_t pageId, uint8_t type, uint8_t instance)> cb);

    // Page selected callback (0x66)
    void onPageSelected(std::function<void(uint8_t)> cb);

    // High-level touch callback for 0x65 frames
    void onTouchCommand(std::function<void(uint8_t pageId, uint8_t componentId, uint8_t state)> cb);

    uint8_t currentPage() const { return _currentPage; }

    void requestCurrentPage(); // send 'sendme' once
    void keepPageSynced();     // resend if unknown for a bit

    // Poll Nextion touch/events (non-blocking). Call from loop().
    void loop();

    void enableRxDump(bool on);
    const String &lastCommand() const { return _lastCmd; }

  private:
    HardwareSerial &_ser;
    bool _inited{false};
    uint32_t _baud{115200};

    // tiny state for parsing 0x65 touch frames
    uint8_t _tchBuf[32]{};
    uint8_t _tchLen{0};

    bool _dumpRx{false};
    uint8_t _dumpCol{0};

    uint8_t _currentPage{0xFF}; // 0xFF = unknown at boot
    uint32_t _lastSendmeMs{0};
    uint8_t _toastReturnPage{0xFF};
    uint32_t _toastDeadlineMs = 0;

    std::function<void(uint8_t, uint8_t, uint8_t)> _customCommandCb;
    std::function<void(uint8_t)> _onPageSelected{};
    std::function<void(uint8_t, uint8_t, uint8_t)> _onTouchCb;

    void handleTouchEvent(uint8_t pageId, uint8_t componentId, uint8_t value = 0);
    void handlePageSelected_(uint8_t page);

    uint8_t _lastPg{0}, _lastComp{0}, _lastState{0};
    uint32_t _lastEvMs{0};
    void emitTouch_(uint8_t page, uint8_t comp, uint8_t state,
                    std::function<void(uint8_t, uint8_t, uint8_t)> cb);

    void pollCustom_(std::function<void(uint8_t, uint8_t, uint8_t)> cb = nullptr);

    String _lastCmd;
  };

} // namespace ui
