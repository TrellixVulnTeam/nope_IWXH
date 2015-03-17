// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DevToolsEmulator_h
#define DevToolsEmulator_h

#include "wtf/Forward.h"
#include "wtf/OwnPtr.h"

namespace blink {

class IntPoint;
class WebInputEvent;
class WebViewImpl;

struct WebDeviceEmulationParams;

class DevToolsEmulator final {
public:
    explicit DevToolsEmulator(WebViewImpl*);
    ~DevToolsEmulator();

    // Settings overrides.
    void setTextAutosizingEnabled(bool);
    void setDeviceScaleAdjustment(float);
    void setPreferCompositingToLCDTextEnabled(bool);
    void setUseMobileViewportStyle(bool);

    // Emulation.
    void enableDeviceEmulation(const WebDeviceEmulationParams&);
    void disableDeviceEmulation();
    bool deviceEmulationEnabled() { return m_deviceMetricsEnabled; }
    void setTouchEventEmulationEnabled(bool);
    bool handleInputEvent(const WebInputEvent&);

private:
    void enableMobileEmulation();
    void disableMobileEmulation();

    WebViewImpl* m_webViewImpl;
    bool m_deviceMetricsEnabled;
    bool m_emulateMobileEnabled;
    bool m_originalViewportEnabled;
    bool m_isOverlayScrollbarsEnabled;
    float m_originalDefaultMinimumPageScaleFactor;
    float m_originalDefaultMaximumPageScaleFactor;
    bool m_embedderTextAutosizingEnabled;
    float m_embedderDeviceScaleAdjustment;
    bool m_embedderPreferCompositingToLCDTextEnabled;
    bool m_embedderUseMobileViewport;

    bool m_touchEventEmulationEnabled;
    bool m_originalTouchEnabled;
    bool m_originalDeviceSupportsMouse;
    bool m_originalDeviceSupportsTouch;
    int m_originalMaxTouchPoints;
    OwnPtr<IntPoint> m_lastPinchAnchorCss;
    OwnPtr<IntPoint> m_lastPinchAnchorDip;
};

} // namespace blink

#endif
