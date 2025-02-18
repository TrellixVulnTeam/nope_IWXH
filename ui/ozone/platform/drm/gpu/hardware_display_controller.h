// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_GPU_HARDWARE_DISPLAY_CONTROLLER_H_
#define UI_OZONE_PLATFORM_DRM_GPU_HARDWARE_DISPLAY_CONTROLLER_H_

#include <stddef.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include <deque>
#include <map>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/containers/scoped_ptr_hash_map.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "ui/ozone/ozone_export.h"
#include "ui/ozone/platform/drm/gpu/hardware_display_plane_manager.h"
#include "ui/ozone/platform/drm/gpu/overlay_plane.h"
#include "ui/ozone/platform/drm/gpu/page_flip_observer.h"

namespace gfx {
class Point;
}

namespace ui {

class CrtcController;
class ScanoutBuffer;
class DrmDevice;

// The HDCOz will handle modesettings and scannout operations for hardware
// devices.
//
// In the DRM world there are 3 components that need to be paired up to be able
// to display an image to the monitor: CRTC (cathode ray tube controller),
// encoder and connector. The CRTC determines which framebuffer to read, when
// to scanout and where to scanout. Encoders converts the stream from the CRTC
// to the appropriate format for the connector. The connector is the physical
// connection that monitors connect to.
//
// There is no 1:1:1 pairing for these components. It is possible for an encoder
// to be compatible to multiple CRTCs and each connector can be used with
// multiple encoders. In addition, it is possible to use one CRTC with multiple
// connectors such that we can display the same image on multiple monitors.
//
// For example, the following configuration shows 2 different screens being
// initialized separately.
// -------------      -------------
// | Connector |      | Connector |
// |   HDMI    |      |    VGA    |
// -------------      -------------
//       ^                  ^
//       |                  |
// -------------      -------------
// |  Encoder1  |     |  Encoder2 |
// -------------      -------------
//       ^                  ^
//       |                  |
// -------------      -------------
// |   CRTC1   |      |   CRTC2   |
// -------------      -------------
//
// In the following configuration 2 different screens are associated with the
// same CRTC, so on scanout the same framebuffer will be displayed on both
// monitors.
// -------------      -------------
// | Connector |      | Connector |
// |   HDMI    |      |    VGA    |
// -------------      -------------
//       ^                  ^
//       |                  |
// -------------      -------------
// |  Encoder1  |     |  Encoder2 |
// -------------      -------------
//       ^                  ^
//       |                  |
//      ----------------------
//      |        CRTC1       |
//      ----------------------
//
// Note that it is possible to have more connectors than CRTCs which means that
// only a subset of connectors can be active independently, showing different
// framebuffers. Though, in this case, it would be possible to have all
// connectors active if some use the same CRTC to mirror the display.
class OZONE_EXPORT HardwareDisplayController
    : public base::SupportsWeakPtr<HardwareDisplayController>,
      public PageFlipObserver {
 public:
  explicit HardwareDisplayController(scoped_ptr<CrtcController> controller);
  ~HardwareDisplayController() override;

  // Performs the initial CRTC configuration. If successful, it will display the
  // framebuffer for |primary| with |mode|.
  bool Modeset(const OverlayPlane& primary, drmModeModeInfo mode);

  // Reconfigures the CRTC with the current surface and mode.
  bool Enable();

  // Disables the CRTC.
  void Disable();

  void QueueOverlayPlane(const OverlayPlane& plane);

  // Schedules the |overlays|' framebuffers to be displayed on the next vsync
  // event. The event will be posted on the graphics card file descriptor |fd_|
  // and it can be read and processed by |drmHandleEvent|. That function can
  // define the callback for the page flip event. A generic data argument will
  // be presented to the callback. We use that argument to pass in the HDCO
  // object the event belongs to.
  //
  // Between this call and the callback, the framebuffers used in this call
  // should not be modified in any way as it would cause screen tearing if the
  // hardware performed the flip. Note that the frontbuffer should also not
  // be modified as it could still be displayed.
  //
  // Note that this function does not block. Also, this function should not be
  // called again before the page flip occurrs.
  //
  // Returns true if the page flip was successfully registered, false otherwise.
  bool SchedulePageFlip(bool is_sync, const base::Closure& callback);

  // Set the hardware cursor to show the contents of |surface|.
  bool SetCursor(const scoped_refptr<ScanoutBuffer>& buffer);

  bool UnsetCursor();

  // Moves the hardware cursor to |location|.
  bool MoveCursor(const gfx::Point& location);

  void AddCrtc(scoped_ptr<CrtcController> controller);
  scoped_ptr<CrtcController> RemoveCrtc(const scoped_refptr<DrmDevice>& drm,
                                        uint32_t crtc);
  bool HasCrtc(const scoped_refptr<DrmDevice>& drm, uint32_t crtc) const;
  bool IsMirrored() const;
  bool IsDisabled() const;
  gfx::Size GetModeSize() const;

  gfx::Point origin() const { return origin_; }
  void set_origin(const gfx::Point& origin) { origin_ = origin; }

  const drmModeModeInfo& get_mode() const { return mode_; };

  uint64_t GetTimeOfLastFlip() const;

  const std::vector<CrtcController*>& crtc_controllers() const {
    return crtc_controllers_.get();
  }

  scoped_refptr<DrmDevice> GetAllocationDrmDevice() const;

 private:
  // Returns true if any of the CRTCs is waiting for a page flip.
  bool HasPendingPageFlips() const;

  bool ActualSchedulePageFlip();

  void ProcessPageFlipRequest();

  void ClearPendingRequests();

  // PageFlipObserver:
  void OnPageFlipEvent() override;

  struct PageFlipRequest {
    PageFlipRequest(const OverlayPlaneList& planes,
                    bool is_sync,
                    const base::Closure& callback);
    ~PageFlipRequest();

    OverlayPlaneList planes;
    bool is_sync;
    base::Closure callback;
  };

  // Buffers need to be declared first so that they are destroyed last. Needed
  // since the controllers may reference the buffers.
  OverlayPlaneList current_planes_;
  OverlayPlaneList pending_planes_;
  std::deque<PageFlipRequest> requests_;
  scoped_refptr<ScanoutBuffer> cursor_buffer_;

  base::ScopedPtrHashMap<DrmDevice*, HardwareDisplayPlaneList>
      owned_hardware_planes_;

  // Stores the CRTC configuration. This is used to identify monitors and
  // configure them.
  ScopedVector<CrtcController> crtc_controllers_;

  // Location of the controller on the screen.
  gfx::Point origin_;

  // The mode used by the last modesetting operation.
  drmModeModeInfo mode_;

  bool is_disabled_;

  DISALLOW_COPY_AND_ASSIGN(HardwareDisplayController);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_GPU_HARDWARE_DISPLAY_CONTROLLER_H_
