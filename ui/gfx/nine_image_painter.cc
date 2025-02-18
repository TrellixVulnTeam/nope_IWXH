// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/nine_image_painter.h"

#include <limits>

#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/safe_integer_conversions.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/skia_util.h"

namespace gfx {

namespace {

// The following functions width and height of the image in pixels for the
// scale factor in the Canvas.
int ImageWidthInPixels(const ImageSkia& i, Canvas* c) {
  if (i.isNull())
    return 0;
  return i.GetRepresentation(c->image_scale()).pixel_width();
}

int ImageHeightInPixels(const ImageSkia& i, Canvas* c) {
  if (i.isNull())
    return 0;
  return i.GetRepresentation(c->image_scale()).pixel_height();
}

// Stretches the given image over the specified canvas area.
void Fill(Canvas* c,
          const ImageSkia& i,
          int x,
          int y,
          int w,
          int h,
          const SkPaint& paint) {
  if (i.isNull())
    return;
  c->DrawImageIntInPixel(i, 0, 0, ImageWidthInPixels(i, c),
                         ImageHeightInPixels(i, c), x, y, w, h, false, paint);
}

}  // namespace

NineImagePainter::NineImagePainter(const std::vector<ImageSkia>& images) {
  DCHECK_EQ(arraysize(images_), images.size());
  for (size_t i = 0; i < arraysize(images_); ++i)
    images_[i] = images[i];
}

NineImagePainter::NineImagePainter(const ImageSkia& image,
                                   const Insets& insets) {
  std::vector<gfx::Rect> regions;
  GetSubsetRegions(image, insets, &regions);
  DCHECK_EQ(9u, regions.size());

  for (size_t i = 0; i < 9; ++i)
    images_[i] = ImageSkiaOperations::ExtractSubset(image, regions[i]);
}

NineImagePainter::~NineImagePainter() {
}

bool NineImagePainter::IsEmpty() const {
  return images_[0].isNull();
}

Size NineImagePainter::GetMinimumSize() const {
  return IsEmpty() ? Size() : Size(
      images_[0].width() + images_[1].width() + images_[2].width(),
      images_[0].height() + images_[3].height() + images_[6].height());
}

void NineImagePainter::Paint(Canvas* canvas, const Rect& bounds) {
  // When no alpha value is specified, use default value of 100% opacity.
  Paint(canvas, bounds, std::numeric_limits<uint8>::max());
}

void NineImagePainter::Paint(Canvas* canvas,
                             const Rect& bounds,
                             const uint8 alpha) {
  if (IsEmpty())
    return;

  ScopedCanvas scoped_canvas(canvas);
  canvas->Translate(bounds.OffsetFromOrigin());

  // Get the current transform from the canvas and apply it to the logical
  // bounds passed in. This will give us the pixel bounds which can be used
  // to draw the images at the correct locations.
  // We should not scale the bounds by the canvas->image_scale() as that can be
  // different from the real scale in the canvas transform.
  SkRect bounds_in_pixels_f;
  if (!canvas->sk_canvas()->getTotalMatrix().mapRect(
          &bounds_in_pixels_f, RectToSkRect(gfx::Rect(bounds.size()))))
    return;  // Invalid transform.

  SkIRect bounds_in_pixels;
  bounds_in_pixels_f.dround(&bounds_in_pixels);

  SkMatrix matrix = canvas->sk_canvas()->getTotalMatrix();
  matrix.setTranslateX(SkIntToScalar(bounds_in_pixels.x()));
  matrix.setTranslateY(SkIntToScalar(bounds_in_pixels.y()));
  canvas->sk_canvas()->setMatrix(matrix);

  const int width_in_pixels = bounds_in_pixels.width();
  const int height_in_pixels = bounds_in_pixels.height();

  // In case the corners and edges don't all have the same width/height, we draw
  // the center first, and extend it out in all directions to the edges of the
  // images with the smallest widths/heights.  This way there will be no
  // unpainted areas, though some corners or edges might overlap the center.
  int i0w = ImageWidthInPixels(images_[0], canvas);
  int i2w = ImageWidthInPixels(images_[2], canvas);
  int i3w = ImageWidthInPixels(images_[3], canvas);
  int i5w = ImageWidthInPixels(images_[5], canvas);
  int i6w = ImageWidthInPixels(images_[6], canvas);
  int i8w = ImageWidthInPixels(images_[8], canvas);

  int i4x = std::min(std::min(i0w, i3w), i6w);
  int i4w = width_in_pixels - i4x - std::min(std::min(i2w, i5w), i8w);

  int i0h = ImageHeightInPixels(images_[0], canvas);
  int i1h = ImageHeightInPixels(images_[1], canvas);
  int i2h = ImageHeightInPixels(images_[2], canvas);
  int i6h = ImageHeightInPixels(images_[6], canvas);
  int i7h = ImageHeightInPixels(images_[7], canvas);
  int i8h = ImageHeightInPixels(images_[8], canvas);

  int i4y = std::min(std::min(i0h, i1h), i2h);
  int i4h = height_in_pixels - i4y - std::min(std::min(i6h, i7h), i8h);

  SkPaint paint;
  paint.setAlpha(alpha);

  Fill(canvas, images_[4], i4x, i4y, i4w, i4h, paint);

  canvas->DrawImageIntInPixel(images_[0], 0, 0, i0w, i0h,
                              0, 0, i0w, i0h, false, paint);

  Fill(canvas, images_[1], i0w, 0, width_in_pixels - i0w - i2w, i1h, paint);

  Fill(canvas, images_[2], width_in_pixels - i2w, 0, i2w, i2h, paint);

  Fill(canvas, images_[3], 0, i0h, i3w, height_in_pixels - i0h - i6h, paint);

  Fill(canvas, images_[5], width_in_pixels - i5w, i2h, i5w,
       height_in_pixels - i2h - i8h, paint);

  Fill(canvas, images_[6], 0, height_in_pixels - i6h, i6w, i6h, paint);

  Fill(canvas, images_[7], i6w, height_in_pixels - i7h,
       width_in_pixels - i6w - i8w, i7h, paint);

  Fill(canvas, images_[8], width_in_pixels - i8w, height_in_pixels - i8h, i8w,
       i8h, paint);
}

// static
void NineImagePainter::GetSubsetRegions(const ImageSkia& image,
                                        const Insets& insets,
                                        std::vector<Rect>* regions) {
  DCHECK_GE(image.width(), insets.width());
  DCHECK_GE(image.height(), insets.height());

  std::vector<Rect> result(9);

  const int x[] = {
      0, insets.left(), image.width() - insets.right(), image.width()};
  const int y[] = {
      0, insets.top(), image.height() - insets.bottom(), image.height()};

  for (size_t j = 0; j < 3; ++j) {
    for (size_t i = 0; i < 3; ++i) {
      result[i + j * 3] = Rect(x[i], y[j], x[i + 1] - x[i], y[j + 1] - y[j]);
    }
  }
  result.swap(*regions);
}

}  // namespace gfx
