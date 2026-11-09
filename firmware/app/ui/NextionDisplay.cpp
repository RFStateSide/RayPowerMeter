// RayPowerMeter | RF StateSide LLC
// Public Engineering Reference — AS-IS
// Copyright (c) 2025–2026 RF StateSide LLC.
// SPDX-License-Identifier: LGPL-3.0-only

#include "ui/NextionDisplay.h"
#include "core/Constants.h"

namespace ui
{

    // Helper: send raw Nextion command (adds 3 x 0xFF terminators)
    static inline void sendRawCmd(HardwareSerial &ser, const String &cmd)
    {
        ser.print(cmd);
        ser.write(0xFF);
        ser.write(0xFF);
        ser.write(0xFF);
    }

    NextionDisplay::NextionDisplay(HardwareSerial &serial) : _ser(serial)
    {
    }

    bool NextionDisplay::begin(uint32_t baud)
    {
        _baud = baud;
        // Configure UART pins from pinout.h constants
        _ser.begin(_baud, SERIAL_8N1, UART_HMI_RX_PIN, UART_HMI_TX_PIN);

        _inited = true;

        if (_inited)
        {
            // Optional: turn off Nextion command ACKs (reduces noise)
            sendCommand("bkcmd=0");

            // Ask the panel which page it's currently on (important when panel stayed powered)
            sendCommand("sendme"); // panel replies: 0x66 <pageId> FF FF FF

            // Track page state, mark unknown now (will be set on 0x66)
            _currentPage = 0xFF;
            _lastSendmeMs = millis();
        }
        return _inited;
    }

    void NextionDisplay::setPage(uint8_t page)
    {
        if (!_inited)
            return;
        String cmd = "page " + String(page);
        sendCommand(cmd.c_str());
    }

    bool NextionDisplay::getText(const char *comp, String &out, uint32_t timeoutMs)
    {
        if (!_inited || !comp)
            return false;

        char cmd[64];
        snprintf(cmd, sizeof(cmd), "get %s.txt", comp);
        // sendCommand() appends 0xFFx3
        sendCommand(cmd);

        const uint32_t start = millis();

        enum
        {
            WAIT_START,
            READ_PAYLOAD,
            WAIT_FF
        } st = WAIT_START;

        out = "";
        int ffCount = 0;

        while (millis() - start < timeoutMs)
        {
            if (_ser.available())
            {
                uint8_t b = (uint8_t)_ser.read();
                if (st == WAIT_START)
                {
                    if (b == 0x70)
                    {
                        st = READ_PAYLOAD;
                        ffCount = 0;
                    }
                }
                else if (st == READ_PAYLOAD)
                {
                    if (b == 0xFF)
                    {
                        st = WAIT_FF;
                        ffCount = 1;
                    }
                    else
                    {
                        out += (char)b;
                    }
                }
                else
                { // WAIT_FF
                    if (b == 0xFF)
                    {
                        if (++ffCount >= 3)
                            return true; // done
                    }
                    else
                    {
                        // stray 0xFF inside payload; keep it and continue
                        out += (char)0xFF;
                        out += (char)b;
                        st = READ_PAYLOAD;
                        ffCount = 0;
                    }
                }
            }
            else
            {
                delay(1);
            }
        }
        return false;
    }

    void NextionDisplay::onCustomCommand(std::function<void(uint8_t, uint8_t, uint8_t)> cb)
    {
        _customCommandCb = std::move(cb);
    }

    void NextionDisplay::onTouchCommand(std::function<void(uint8_t, uint8_t, uint8_t)> cb)
    {
        _onTouchCb = std::move(cb);
    }

    void NextionDisplay::setText(const String &component, const String &text)
    {
        if (!_inited)
            return;
        String cmd = component + ".txt=\"" + text + "\"";
        sendCommand(cmd.c_str());
    }

    void NextionDisplay::sendCommand(const String &cmd)
    {
        if (!_inited)
            return;

        // Remember last command for debugging (e.g., 0x1A invalid variable)
        _lastCmd = cmd;

        _ser.print(cmd);
        _ser.write(0xFF);
        _ser.write(0xFF);
        _ser.write(0xFF);
        // delay(10);
    }

    void NextionDisplay::setNumber(const String &component, int32_t value)
    {
        if (!_inited)
            return;
        String cmd = component + ".val=" + String(value);
        sendCommand(cmd.c_str());
    }

    void NextionDisplay::setFloat(const String &component, float value, uint8_t decimals)
    {
        if (!_inited)
            return;
        char buf[24];
        dtostrf(value, 0, decimals, buf);
        setText(component, String(buf));
    }

    void NextionDisplay::setProgress(const String &component, uint8_t percent)
    {
        if (!_inited)
            return;
        percent = constrain(percent, (uint8_t)0, (uint8_t)100);
        String cmd = component + ".val=" + String(percent);
        sendCommand(cmd.c_str());
    }

    void NextionDisplay::setPic(const String &component, uint16_t picId)
    {
        if (!_inited)
            return;

        String cmd = component + ".pic=" + String(picId);
        sendCommand(cmd.c_str());
    }

    void NextionDisplay::setTextPco(const char *objName, uint16_t color)
    {
        if (!_inited || !objName)
            return;

        char cmd[64];
        snprintf(cmd, sizeof(cmd), "%s.pco=%u", objName, color);

        sendCommand(cmd);
    }

    bool NextionDisplay::getNumber(const char *expr, uint32_t &out, uint16_t timeoutMs)
    {
        if (!expr || !_inited)
            return false;

        // Flush any stale bytes first
        while (_ser.available())
            (void)_ser.read();

        // Remember last command as a readable string
        _lastCmd = F("get ");
        _lastCmd += expr;

        _ser.print(F("get "));
        _ser.print(expr);
        _ser.write(0xFF);
        _ser.write(0xFF);
        _ser.write(0xFF);

        const uint32_t start = millis();
        uint8_t buf[8];
        size_t n = 0;

        while (millis() - start < timeoutMs)
        {
            while (_ser.available())
            {
                const uint8_t b = (uint8_t)_ser.read();
                if (n < sizeof(buf))
                    buf[n++] = b;
                // Expect: 0x71 + 4 data + 0xFF 0xFF 0xFF
                if (n >= 8 && buf[0] == 0x71 && buf[5] == 0xFF && buf[6] == 0xFF && buf[7] == 0xFF)
                {
                    out = (uint32_t)buf[1] |
                          ((uint32_t)buf[2] << 8) |
                          ((uint32_t)buf[3] << 16) |
                          ((uint32_t)buf[4] << 24);
                    return true;
                }
            }
            delay(1);
        }
        return false;
    }

    bool NextionDisplay::readRtcUTC(uint16_t &y, uint8_t &m, uint8_t &d,
                                    uint8_t &hh, uint8_t &mm, uint8_t &ss)
    {
        uint32_t v = 0;
        if (!getNumber("rtc0", v))
            return false;
        y = (uint16_t)v;
        if (!getNumber("rtc1", v))
            return false;
        m = (uint8_t)v;
        if (!getNumber("rtc2", v))
            return false;
        d = (uint8_t)v;
        if (!getNumber("rtc3", v))
            return false;
        hh = (uint8_t)v;
        if (!getNumber("rtc4", v))
            return false;
        mm = (uint8_t)v;
        if (!getNumber("rtc5", v))
            return false;
        ss = (uint8_t)v;
        return true;
    }

    void NextionDisplay::setRtcUTC(uint16_t y, uint8_t m, uint8_t d,
                                   uint8_t hh, uint8_t mm, uint8_t ss)
    {
        // Nextion RTC valid range is roughly:
        //   year: 2000–2099, month: 1–12, day: 1–31, hh: 0–23, mm/ss: 0–59
        if (y < 2000)
            y = 2000;
        if (y > 2099)
            y = 2099;
        if (m < 1)
            m = 1;
        if (m > 12)
            m = 12;
        if (d < 1)
            d = 1;
        if (d > 31)
            d = 31;
        if (hh > 23)
            hh = 23;
        if (mm > 59)
            mm = 59;
        if (ss > 59)
            ss = 59;

        char cmd[32];

        snprintf(cmd, sizeof(cmd), "rtc0=%u", (unsigned)y);
        sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rtc1=%u", (unsigned)m);
        sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rtc2=%u", (unsigned)d);
        sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rtc3=%u", (unsigned)hh);
        sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rtc4=%u", (unsigned)mm);
        sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "rtc5=%u", (unsigned)ss);
        sendCommand(cmd);
    }

    void NextionDisplay::setRtcEpoch(time_t epoch)
    {
        if (epoch <= 0)
        {
            return;
        }

        // Convert epoch (UTC) -> broken-down UTC time
        struct tm *ptm = gmtime(&epoch);
        if (!ptm)
        {
            return;
        }
        struct tm tmUtc = *ptm;

        uint16_t y = (uint16_t)(tmUtc.tm_year + 1900);
        uint8_t m = (uint8_t)(tmUtc.tm_mon + 1);
        uint8_t d = (uint8_t)tmUtc.tm_mday;
        uint8_t hh = (uint8_t)tmUtc.tm_hour;
        uint8_t mm = (uint8_t)tmUtc.tm_min;
        uint8_t ss = (uint8_t)tmUtc.tm_sec;

        setRtcUTC(y, m, d, hh, mm, ss);
    }

    void NextionDisplay::enableRxDump(bool on)
    {
        _dumpRx = on;
        _dumpCol = 0;
    }

    // -------- Raw 0x62/0x65/0x66 event polling --------
    // 0x62 <pageId> <type> <instance>    0xFF 0xFF 0xFF   (custom command)
    // 0x65 <pageId> <componentId> <state>0xFF 0xFF 0xFF   (touch)
    // 0x66 <pageId>                      0xFF 0xFF 0xFF   (page report)
    void NextionDisplay::parseIncoming(std::function<void(uint8_t, uint8_t, uint8_t)> cb)
    {
        if (!_inited)
            return;

        for (;;)
        {
            int avail = _ser.available();
            if (avail < 4)
                break; // not enough even for a status frame

            uint8_t cmd = (uint8_t)_ser.peek();

            // --- Long-ish frames: 0x62 (custom), 0x65 (touch), 0x66 (page) ---
            if (cmd == 0x62 || cmd == 0x65 || cmd == 0x66)
            {
                // Determine expected length per command type
                uint8_t frameLen = 0;
                if (cmd == 0x62 || cmd == 0x65)
                {
                    // 0x62,0x65: 0xPP 0xAA 0xBB 0xCC 0xFF 0xFF 0xFF (7 bytes)
                    frameLen = 7;
                }
                else if (cmd == 0x66)
                {
                    // 0x66: 0x66 0x<page> 0xFF 0xFF 0xFF (5 bytes)
                    frameLen = 5;
                }

                if (avail < frameLen)
                    break; // wait for full frame

                uint8_t buf[7]; // max needed
                size_t n = _ser.readBytes(buf, frameLen);
                if (n != frameLen)
                    break;

                if (_dumpRx)
                {
                    Serial.print(F("[HMI RX] "));
                    for (uint8_t i = 0; i < frameLen; ++i)
                    {
                        char hx[4];
                        snprintf(hx, sizeof(hx), "%02X", buf[i]);
                        Serial.print(hx);
                        Serial.print(' ');
                    }
                    Serial.println();
                }

                cmd = buf[0];

                if (cmd == 0x62)
                {
                    // 0x62 <page> <type> <inst> FF FF FF
                    const uint8_t page = buf[1];
                    const uint8_t type = buf[2];
                    const uint8_t inst = buf[3];

                    if (type == 'T' && _toastReturnPage != 0xFF)
                    {
                        setPage(_toastReturnPage);
                        _toastReturnPage = 0xFF;
                    }

                    if (_customCommandCb)
                        _customCommandCb(page, type, inst);

                    if (cb)
                        cb(page, type, inst);
                }
                else if (cmd == 0x65)
                {
                    // 0x65 <page> <comp> <state> FF FF FF
                    const uint8_t page = buf[1];
                    const uint8_t comp = buf[2];
                    const uint8_t st = buf[3]; // 1=press, 0=release
                    emitTouch_(page, comp, st, cb);
                }
                else if (cmd == 0x66)
                {
                    // 0x66 <page> FF FF FF
                    const uint8_t page = buf[1];
                    handlePageSelected_(page);
                }

                continue; // loop for more frames
            }

            // --- Short frames: status / error codes (status + 0xFF 0xFF 0xFF) ---
            if (avail < 4)
                break;

            uint8_t s0 = (uint8_t)_ser.read();
            uint8_t s1 = (uint8_t)_ser.read();
            uint8_t s2 = (uint8_t)_ser.read();
            uint8_t s3 = (uint8_t)_ser.read();

            if (_dumpRx)
            {
                if (s0 == 0x1A || s1 == 0x1A || s2 == 0x1A || s3 == 0x1A)
                {
                    Serial.print(F("[HMI LAST CMD] "));
                    Serial.println(_lastCmd);
                }

                Serial.printf("[HMI RX] %02X %02X %02X %02X (status)\n",
                              s0, s1, s2, s3);
            }

            // Discard and loop again
        }
    }

    void NextionDisplay::loop()
    {
        if (!_inited)
            return;

        // 0x65 touch / 0x62 custom / 0x66 page events
        parseIncoming(nullptr);

        // 0x66 page-selected keep-alive
        keepPageSynced();

        // MCU-controlled toast timeout:
        // when the deadline passes, go back to the remembered page.
        if (_toastReturnPage != 0xFF && _toastDeadlineMs != 0)
        {
            const int32_t diff = (int32_t)(millis() - _toastDeadlineMs);
            if (diff >= 0)
            {
                Serial.printf("[ND] Toast done, returning to page %u\n",
                              _toastReturnPage);
                setPage(_toastReturnPage);
                _toastReturnPage = 0xFF;
                _toastDeadlineMs = 0;
            }
        }

        // No external Nextion library: we do all parsing ourselves.
    }

    void NextionDisplay::emitTouch_(uint8_t page, uint8_t comp, uint8_t state,
                                    std::function<void(uint8_t, uint8_t, uint8_t)> cb)
    {
        const uint32_t now = millis();
        // Drop exact duplicates that arrive within 60 ms (double parsing)
        if (page == _lastPg && comp == _lastComp && state == _lastState &&
            (now - _lastEvMs) < 60)
            return;

        _lastPg = page;
        _lastComp = comp;
        _lastState = state;
        _lastEvMs = now;

        // Local default handling (optional)
        handleTouchEvent(page, comp, state);

        // New dedicated touch callback
        if (_onTouchCb)
            _onTouchCb(page, comp, state);

        // Optional per-call cb for compatibility
        if (cb)
            cb(page, comp, state);
    }

    void NextionDisplay::handleTouchEvent(uint8_t pageId, uint8_t componentId, uint8_t value)
    {
        // Generic debug only; RayPowerMeter UI-specific logic will live elsewhere
        Serial.printf("[ND] Touch event: page=%u comp=%u val=%u\n",
                      pageId, componentId, value);
    }

    void NextionDisplay::onPageSelected(std::function<void(uint8_t)> cb)
    {
        _onPageSelected = std::move(cb);
    }

    void NextionDisplay::handlePageSelected_(uint8_t page)
    {
        if (page != _currentPage)
        {
            _currentPage = page;
            Serial.printf("[ND] Page selected: %u\n", page);
            if (_onPageSelected)
            {
                _onPageSelected(page);
            }
        }
        else
        {
            // Helpful while we debug: see when 0x66 arrives but is a duplicate
            Serial.printf("[ND] Page re-selected: %u\n", page);
        }
    }

    void NextionDisplay::requestCurrentPage()
    {
        // Nextion reply: 0x66 <pageId> FF FF FF
        sendCommand("sendme");
    }

    void NextionDisplay::keepPageSynced()
    {
        if (_currentPage != 0xFF)
            return; // already known
        const uint32_t now = millis();
        if (now - _lastSendmeMs >= 300)
        { // gentle retry cadence
            requestCurrentPage();
            _lastSendmeMs = now;
        }
    }

    void NextionDisplay::toast(const char *msg, uint16_t ms, uint8_t returnPage)
    {
        if (!_inited)
            return;

        // sanitize inputs
        if (!msg)
            msg = "";
        if (ms < 400)
            ms = 400;
        if (ms > 10000)
            ms = 10000;

        // Decide where to go back when toast ends
        if (returnPage == 0xFF)
        {
            // If we don't know the current page yet, just go back to page 0
            // (or whatever page makes sense as a fallback).
            returnPage = (_currentPage != 0xFF) ? _currentPage : 0;
        }
        setToastReturnPage(returnPage);

        // MCU owns the timeout — remember deadline
        _toastDeadlineMs = millis() + ms;

        // 1) Switch to the toast page
        sendCommand("page pToast");

        // 2) Escape quotes for Nextion string
        char esc[96];
        size_t ei = 0;
        for (const char *p = msg; *p && ei < sizeof(esc) - 1; ++p)
        {
            if (*p == '\"')
            {
                if (ei + 2 < sizeof(esc))
                {
                    esc[ei++] = '\\';
                    esc[ei++] = '\"';
                }
            }
            else
            {
                esc[ei] = *p;
                ++ei;
            }
        }
        esc[ei] = '\0';

        // 3) Update text and force redraw
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "tToast.txt=\"%s\"", esc);
        sendCommand(cmd);
        sendCommand("ref tToast");

        // NOTE:
        // We NO LONGER rely on a Nextion timer (tmToast) or a custom 'T' frame.
        // The MCU timer in loop() will switch back to _toastReturnPage when ms expires.
    }

    void NextionDisplay::setBrightness(uint8_t pct)
    {
        if (pct < 10)
            pct = 10;
        if (pct > 100)
            pct = 100;
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "dims=%u", (unsigned)pct);
        sendCommand(cmd);
        snprintf(cmd, sizeof(cmd), "dim=%u", (unsigned)pct);
        sendCommand(cmd);
    }

    // Non-blocking scanner for: 0x62 <pageId> <type> <instance> FF FF FF
    void NextionDisplay::pollCustom_(std::function<void(uint8_t, uint8_t, uint8_t)> cb)
    {
        // We’ll scan the RX stream for a 0x62 header and only consume a frame
        // when at least 7 bytes are available and the last 3 are 0xFF.
        for (;;)
        {
            if (_ser.available() < 7)
                break;

            // Walk forward until we see a 0x62 at the head
            int b = _ser.peek();
            if (b != 0x62)
            {
                _ser.read();
                continue;
            }

            // We have 0x62 at head and at least 7 bytes buffered — try to read a frame
            uint8_t buf[7];
            size_t n = 0;
            while (_ser.available() && n < sizeof(buf))
            {
                buf[n++] = (uint8_t)_ser.read();
            }
            if (n < 7)
            {
                // not enough (shouldn’t happen due to available() check); bail this tick
                break;
            }
            // Validate trailer
            if (buf[4] != 0xFF || buf[5] != 0xFF || buf[6] != 0xFF)
            {
                // Not a well-formed 0x62 frame — drop first byte and continue scanning
                continue;
            }

            const uint8_t page = buf[1];
            const uint8_t type = buf[2];
            const uint8_t inst = buf[3];

            // Handle toast-finished internally
            if (type == 'T' && _toastReturnPage != 0xFF)
            {
                setPage(_toastReturnPage);
                _toastReturnPage = 0xFF;
            }

            // Forward to user callback (if any)
            if (_customCommandCb)
                _customCommandCb(page, type, inst);
            if (cb)
                cb(page, type, inst);

            // loop again in case multiple frames are buffered
        }
    }

} // namespace ui
