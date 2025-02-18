// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/display/cursor_window_controller.h"

#include "ash/display/display_controller.h"
#include "ash/display/mirror_window_controller.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "ash/shell_window_ids.h"
#include "ui/aura/env.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/base/cursor/cursors_aura.h"
#include "ui/base/hit_test.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/compositor/dip_util.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/display.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace ash {

class CursorWindowDelegate : public aura::WindowDelegate {
 public:
  CursorWindowDelegate() : is_cursor_compositing_enabled_(false) {}
  ~CursorWindowDelegate() override {}

  // aura::WindowDelegate overrides:
  gfx::Size GetMinimumSize() const override { return size_; }
  gfx::Size GetMaximumSize() const override { return size_; }
  void OnBoundsChanged(const gfx::Rect& old_bounds,
                       const gfx::Rect& new_bounds) override {}
  ui::TextInputClient* GetFocusedTextInputClient() override { return nullptr; }
  gfx::NativeCursor GetCursor(const gfx::Point& point) override {
    return gfx::kNullCursor;
  }
  int GetNonClientComponent(const gfx::Point& point) const override {
    return HTNOWHERE;
  }
  bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) override {
    return false;
  }
  bool CanFocus() override { return false; }
  void OnCaptureLost() override {}
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->DrawImageInt(cursor_image_, 0, 0);
  }
  void OnDeviceScaleFactorChanged(float device_scale_factor) override {}
  void OnWindowDestroying(aura::Window* window) override {}
  void OnWindowDestroyed(aura::Window* window) override {}
  void OnWindowTargetVisibilityChanged(bool visible) override {}
  bool HasHitTestMask() const override { return false; }
  void GetHitTestMask(gfx::Path* mask) const override {}

  // Sets cursor compositing mode on/off.
  void SetCursorCompositingEnabled(bool enabled) {
    is_cursor_compositing_enabled_ = enabled;
  }

  // Sets the cursor image for the |display|'s scale factor.
  void SetCursorImage(const gfx::ImageSkia& image,
                      const gfx::Display& display) {
    float scale_factor = display.device_scale_factor();
    const gfx::ImageSkiaRep& image_rep = image.GetRepresentation(scale_factor);
    if (!is_cursor_compositing_enabled_) {
      // Note that mirror window's scale factor is always 1.0f, therefore we
      // need to take 2x's image and paint as if it's 1x image.
      size_ = image_rep.pixel_size();
      cursor_image_ = gfx::ImageSkia::CreateFrom1xBitmap(image_rep.sk_bitmap());
    } else {
      size_ = image.size();
      cursor_image_ = gfx::ImageSkia(
          gfx::ImageSkiaRep(image_rep.sk_bitmap(), scale_factor));
    }
  }

  const gfx::Size size() const { return size_; }

 private:
  bool is_cursor_compositing_enabled_;
  gfx::ImageSkia cursor_image_;
  gfx::Size size_;

  DISALLOW_COPY_AND_ASSIGN(CursorWindowDelegate);
};

CursorWindowController::CursorWindowController()
    : is_cursor_compositing_enabled_(false),
      container_(NULL),
      cursor_type_(ui::kCursorNone),
      visible_(true),
      cursor_set_(ui::CURSOR_SET_NORMAL),
      delegate_(new CursorWindowDelegate()) {
}

CursorWindowController::~CursorWindowController() {
  SetContainer(NULL);
}

void CursorWindowController::SetCursorCompositingEnabled(bool enabled) {
  if (is_cursor_compositing_enabled_ != enabled) {
    is_cursor_compositing_enabled_ = enabled;
    delegate_->SetCursorCompositingEnabled(enabled);
    UpdateCursorImage();
    UpdateContainer();
  }
}

void CursorWindowController::UpdateContainer() {
  if (is_cursor_compositing_enabled_) {
    gfx::Screen* screen = Shell::GetScreen();
    gfx::Display display = screen->GetDisplayNearestPoint(
        screen->GetCursorScreenPoint());
    DCHECK(display.is_valid());
    if (display.is_valid())
      SetDisplay(display);
  } else {
    aura::Window* mirror_window = Shell::GetInstance()->
        display_controller()->
        mirror_window_controller()->
        GetWindow();
    if (mirror_window)
      display_ = Shell::GetScreen()->GetPrimaryDisplay();
    SetContainer(mirror_window);
  }
  // Updates the hot point based on the current display.
  UpdateCursorImage();
}

void CursorWindowController::SetDisplay(const gfx::Display& display) {
  if (!is_cursor_compositing_enabled_)
    return;

  display_ = display;
  aura::Window* root_window = Shell::GetInstance()->display_controller()->
      GetRootWindowForDisplayId(display.id());
  if (!root_window)
    return;

  SetContainer(GetRootWindowController(root_window)->GetContainer(
      kShellWindowId_MouseCursorContainer));
  SetBoundsInScreen(display.bounds());
  // Updates the hot point based on the current display.
  UpdateCursorImage();
}

void CursorWindowController::UpdateLocation() {
  if (!cursor_window_)
    return;
  gfx::Point point = aura::Env::GetInstance()->last_mouse_location();
  if (!is_cursor_compositing_enabled_) {
    Shell::GetPrimaryRootWindow()->GetHost()->ConvertPointToHost(&point);
  } else {
    point.Offset(-bounds_in_screen_.x(), -bounds_in_screen_.y());
  }
  point.Offset(-hot_point_.x(), -hot_point_.y());
  gfx::Rect bounds = cursor_window_->bounds();
  bounds.set_origin(point);
  cursor_window_->SetBounds(bounds);
}

void CursorWindowController::SetCursor(gfx::NativeCursor cursor) {
  if (cursor_type_ == cursor.native_type())
    return;
  cursor_type_ = cursor.native_type();
  UpdateCursorImage();
  UpdateCursorVisibility();
}

void CursorWindowController::SetCursorSet(ui::CursorSetType cursor_set) {
  cursor_set_ = cursor_set;
  UpdateCursorImage();
}

void CursorWindowController::SetVisibility(bool visible) {
  if (!cursor_window_)
    return;
  visible_ = visible;
  UpdateCursorVisibility();
}

void CursorWindowController::SetContainer(aura::Window* container) {
  if (container_ == container)
    return;
  container_ = container;
  if (!container) {
    cursor_window_.reset();
    return;
  }

  // Reusing the window does not work when the display is disconnected.
  // Just creates a new one instead. crbug.com/384218.
  cursor_window_.reset(new aura::Window(delegate_.get()));
  cursor_window_->SetTransparent(true);
  cursor_window_->Init(aura::WINDOW_LAYER_TEXTURED);
  cursor_window_->set_ignore_events(true);
  cursor_window_->set_owned_by_parent(false);
  // Call UpdateCursorImage() to figure out |cursor_window_|'s desired size.
  UpdateCursorImage();

  container->AddChild(cursor_window_.get());
  UpdateCursorVisibility();
  SetBoundsInScreen(container->bounds());
}

void CursorWindowController::SetBoundsInScreen(const gfx::Rect& bounds) {
  bounds_in_screen_ = bounds;
  UpdateLocation();
}

void CursorWindowController::UpdateCursorImage() {
  int resource_id;
  // TODO(hshi): support custom cursor set.
  if (!ui::GetCursorDataFor(cursor_set_,
                            cursor_type_,
                            display_.device_scale_factor(),
                            &resource_id,
                            &hot_point_)) {
    return;
  }
  const gfx::ImageSkia* image =
      ResourceBundle::GetSharedInstance().GetImageSkiaNamed(resource_id);
  gfx::ImageSkia rotated = *image;
  if (!is_cursor_compositing_enabled_) {
    switch (display_.rotation()) {
      case gfx::Display::ROTATE_0:
        break;
      case gfx::Display::ROTATE_90:
        rotated = gfx::ImageSkiaOperations::CreateRotatedImage(
            *image, SkBitmapOperations::ROTATION_90_CW);
        hot_point_.SetPoint(
            rotated.width() - hot_point_.y(),
            hot_point_.x());
        break;
      case gfx::Display::ROTATE_180:
        rotated = gfx::ImageSkiaOperations::CreateRotatedImage(
            *image, SkBitmapOperations::ROTATION_180_CW);
        hot_point_.SetPoint(
            rotated.height() - hot_point_.x(),
            rotated.width() - hot_point_.y());
        break;
      case gfx::Display::ROTATE_270:
        rotated = gfx::ImageSkiaOperations::CreateRotatedImage(
            *image, SkBitmapOperations::ROTATION_270_CW);
        hot_point_.SetPoint(
            hot_point_.y(),
            rotated.height() - hot_point_.x());
        break;
    }
  } else {
    hot_point_ = ui::ConvertPointToDIP(Shell::GetPrimaryRootWindow()->layer(),
                                       hot_point_);
  }
  delegate_->SetCursorImage(rotated, display_);
  if (cursor_window_) {
    cursor_window_->SetBounds(gfx::Rect(delegate_->size()));
    cursor_window_->SchedulePaintInRect(
        gfx::Rect(cursor_window_->bounds().size()));
    UpdateLocation();
  }
}

void CursorWindowController::UpdateCursorVisibility() {
  if (!cursor_window_)
    return;
  bool visible = (visible_ && cursor_type_ != ui::kCursorNone);
  if (visible)
    cursor_window_->Show();
  else
    cursor_window_->Hide();
}

}  // namespace ash
