// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_GPU_HARDWARE_DISPLAY_PLANE_MANAGER_ATOMIC_H_
#define UI_OZONE_PLATFORM_DRM_GPU_HARDWARE_DISPLAY_PLANE_MANAGER_ATOMIC_H_

#include "ui/ozone/ozone_export.h"
#include "ui/ozone/platform/drm/gpu/hardware_display_plane_manager.h"

namespace ui {

class OZONE_EXPORT HardwareDisplayPlaneManagerLegacy
    : public HardwareDisplayPlaneManager {
 public:
  HardwareDisplayPlaneManagerLegacy();
  ~HardwareDisplayPlaneManagerLegacy() override;

  // HardwareDisplayPlaneManager:
  bool Commit(HardwareDisplayPlaneList* plane_list, bool is_sync) override;

 private:
  bool SetPlaneData(HardwareDisplayPlaneList* plane_list,
                    HardwareDisplayPlane* hw_plane,
                    const OverlayPlane& overlay,
                    uint32_t crtc_id,
                    const gfx::Rect& src_rect,
                    CrtcController* crtc) override;

  DISALLOW_COPY_AND_ASSIGN(HardwareDisplayPlaneManagerLegacy);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_GPU_HARDWARE_DISPLAY_PLANE_MANAGER_ATOMIC_H_
