/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2010 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "core/svg/graphics/filters/SVGFEImage.h"

#include "core/layout/LayoutObject.h"
#include "core/paint/SVGPaintContext.h"
#include "core/paint/TransformRecorder.h"
#include "core/svg/SVGElement.h"
#include "core/svg/SVGLengthContext.h"
#include "core/svg/SVGURIReference.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/filters/Filter.h"
#include "platform/graphics/filters/SkiaImageFilterBuilder.h"
#include "platform/graphics/paint/DisplayItemList.h"
#include "platform/text/TextStream.h"
#include "platform/transforms/AffineTransform.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/effects/SkBitmapSource.h"
#include "third_party/skia/include/effects/SkPictureImageFilter.h"

namespace blink {

FEImage::FEImage(Filter* filter, PassRefPtr<Image> image, PassRefPtrWillBeRawPtr<SVGPreserveAspectRatio> preserveAspectRatio)
    : FilterEffect(filter)
    , m_image(image)
    , m_treeScope(0)
    , m_preserveAspectRatio(preserveAspectRatio)
{
    FilterEffect::setOperatingColorSpace(ColorSpaceDeviceRGB);
}

FEImage::FEImage(Filter* filter, TreeScope& treeScope, const String& href, PassRefPtrWillBeRawPtr<SVGPreserveAspectRatio> preserveAspectRatio)
    : FilterEffect(filter)
    , m_treeScope(&treeScope)
    , m_href(href)
    , m_preserveAspectRatio(preserveAspectRatio)
{
    FilterEffect::setOperatingColorSpace(ColorSpaceDeviceRGB);
}

DEFINE_TRACE(FEImage)
{
    visitor->trace(m_preserveAspectRatio);
    FilterEffect::trace(visitor);
}

PassRefPtrWillBeRawPtr<FEImage> FEImage::createWithImage(Filter* filter, PassRefPtr<Image> image, PassRefPtrWillBeRawPtr<SVGPreserveAspectRatio> preserveAspectRatio)
{
    return adoptRefWillBeNoop(new FEImage(filter, image, preserveAspectRatio));
}

PassRefPtrWillBeRawPtr<FEImage> FEImage::createWithIRIReference(Filter* filter, TreeScope& treeScope, const String& href, PassRefPtrWillBeRawPtr<SVGPreserveAspectRatio> preserveAspectRatio)
{
    return adoptRefWillBeNoop(new FEImage(filter, treeScope, href, preserveAspectRatio));
}

static FloatRect getRendererRepaintRect(LayoutObject* renderer)
{
    return renderer->localToParentTransform().mapRect(
        renderer->paintInvalidationRectInLocalCoordinates());
}

AffineTransform makeMapBetweenRects(const FloatRect& source, const FloatRect& dest)
{
    AffineTransform transform;
    transform.translate(dest.x() - source.x(), dest.y() - source.y());
    transform.scale(dest.width() / source.width(), dest.height() / source.height());
    return transform;
}

FloatRect FEImage::determineAbsolutePaintRect(const FloatRect& originalRequestedRect)
{
    LayoutObject* renderer = referencedRenderer();
    if (!m_image && !renderer)
        return FloatRect();

    FloatRect requestedRect = originalRequestedRect;
    if (clipsToBounds())
        requestedRect.intersect(maxEffectRect());

    FloatRect destRect = filter()->mapLocalRectToAbsoluteRect(filterPrimitiveSubregion());
    FloatRect srcRect;
    if (renderer) {
        srcRect = getRendererRepaintRect(renderer);
        SVGElement* contextNode = toSVGElement(renderer->node());

        if (contextNode->hasRelativeLengths()) {
            // FIXME: This fixes relative lengths but breaks non-relative ones (see crbug/260709).
            SVGLengthContext lengthContext(contextNode);
            FloatSize viewportSize;
            if (lengthContext.determineViewport(viewportSize)) {
                srcRect = makeMapBetweenRects(FloatRect(FloatPoint(), viewportSize), destRect).mapRect(srcRect);
            }
        } else {
            srcRect = filter()->mapLocalRectToAbsoluteRect(srcRect);
            srcRect.move(destRect.x(), destRect.y());
        }
        destRect.intersect(srcRect);
    } else {
        srcRect = FloatRect(FloatPoint(), m_image->size());
        m_preserveAspectRatio->transformRect(destRect, srcRect);
    }

    destRect.intersect(requestedRect);
    addAbsolutePaintRect(destRect);
    return destRect;
}

LayoutObject* FEImage::referencedRenderer() const
{
    if (!m_treeScope)
        return 0;
    Element* hrefElement = SVGURIReference::targetElementFromIRIString(m_href, *m_treeScope);
    if (!hrefElement || !hrefElement->isSVGElement())
        return 0;
    return hrefElement->layoutObject();
}

TextStream& FEImage::externalRepresentation(TextStream& ts, int indent) const
{
    IntSize imageSize;
    if (m_image)
        imageSize = m_image->size();
    else if (LayoutObject* renderer = referencedRenderer())
        imageSize = enclosingIntRect(getRendererRepaintRect(renderer)).size();
    writeIndent(ts, indent);
    ts << "[feImage";
    FilterEffect::externalRepresentation(ts);
    ts << " image-size=\"" << imageSize.width() << "x" << imageSize.height() << "\"]\n";
    // FIXME: should this dump also object returned by SVGFEImage::image() ?
    return ts;
}

PassRefPtr<SkImageFilter> FEImage::createImageFilterForRenderer(LayoutObject* renderer, SkiaImageFilterBuilder* builder)
{
    FloatRect dstRect = filterPrimitiveSubregion();

    AffineTransform transform;
    SVGElement* contextNode = toSVGElement(renderer->node());

    if (contextNode->hasRelativeLengths()) {
        SVGLengthContext lengthContext(contextNode);
        FloatSize viewportSize;

        // If we're referencing an element with percentage units, eg. <rect with="30%"> those values were resolved against the viewport.
        // Build up a transformation that maps from the viewport space to the filter primitive subregion.
        if (lengthContext.determineViewport(viewportSize))
            transform = makeMapBetweenRects(FloatRect(FloatPoint(), viewportSize), dstRect);
    } else {
        transform.translate(dstRect.x(), dstRect.y());
    }

    GraphicsContext* context = builder->context();
    if (!context)
        return adoptRef(SkBitmapSource::Create(SkBitmap()));

    OwnPtr<DisplayItemList> displayItemList;
    OwnPtr<GraphicsContext> recordingContext;
    if (RuntimeEnabledFeatures::slimmingPaintEnabled()) {
        displayItemList = DisplayItemList::create();
        recordingContext = adoptPtr(new GraphicsContext(nullptr, displayItemList.get()));
        context = recordingContext.get();
    }

    context->beginRecording(FloatRect(FloatPoint(), dstRect.size()));
    {
        TransformRecorder transformRecorder(*context, renderer->displayItemClient(), transform);
        SVGPaintContext::paintSubtree(context, renderer);
    }
    if (displayItemList)
        displayItemList->replay(context);

    RefPtr<const SkPicture> recording = context->endRecording();
    RefPtr<SkImageFilter> result = adoptRef(SkPictureImageFilter::Create(recording.get(), dstRect));
    return result.release();
}

PassRefPtr<SkImageFilter> FEImage::createImageFilter(SkiaImageFilterBuilder* builder)
{
    LayoutObject* renderer = referencedRenderer();
    if (!m_image && !renderer)
        return adoptRef(SkBitmapSource::Create(SkBitmap()));

    if (renderer)
        return createImageFilterForRenderer(renderer, builder);

    FloatRect srcRect = FloatRect(FloatPoint(), m_image->size());
    FloatRect dstRect = filterPrimitiveSubregion();

    // FIXME: CSS image filters currently do not seem to set filter primitive
    // subregion correctly if unspecified. So default to srcRect size if so.
    if (dstRect.isEmpty())
        dstRect = srcRect;

    m_preserveAspectRatio->transformRect(dstRect, srcRect);

    SkBitmap bitmap;
    if (!m_image->bitmapForCurrentFrame(&bitmap))
        return adoptRef(SkBitmapSource::Create(SkBitmap()));

    return adoptRef(SkBitmapSource::Create(bitmap, srcRect, dstRect));
}

} // namespace blink
