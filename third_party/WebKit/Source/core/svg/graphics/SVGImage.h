/*
 * Copyright (C) 2006 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SVGImage_h
#define SVGImage_h

#include "platform/graphics/Image.h"
#include "platform/graphics/paint/DisplayItemClient.h"
#include "platform/heap/Handle.h"
#include "platform/weborigin/KURL.h"

namespace blink {

class FrameView;
class Page;
class LayoutBox;
class SVGImageChromeClient;
class SVGImageForContainer;

class SVGImage final : public Image {
public:
    static PassRefPtr<SVGImage> create(ImageObserver* observer)
    {
        return adoptRef(new SVGImage(observer));
    }

    static bool isInSVGImage(const Node*);

    LayoutBox* embeddedContentBox() const;

    virtual bool isSVGImage() const override { return true; }
    virtual IntSize size() const override { return m_intrinsicSize; }
    void setURL(const KURL& url) { m_url = url; }

    virtual bool currentFrameHasSingleSecurityOrigin() const override;

    virtual void startAnimation(CatchUpAnimation = CatchUp) override;
    virtual void stopAnimation() override;
    virtual void resetAnimation() override;

    virtual bool bitmapForCurrentFrame(SkBitmap*) override;

    // Returns the SVG image document's frame.
    FrameView* frameView() const;

    // Does the SVG image/document contain any animations?
    bool hasAnimations() const;

private:
    friend class AXLayoutObject;
    friend class SVGImageChromeClient;
    friend class SVGImageForContainer;

    virtual ~SVGImage();


    virtual String filenameExtension() const override;

    virtual void setContainerSize(const IntSize&) override;
    IntSize containerSize() const;
    virtual bool usesContainerSize() const override { return true; }
    virtual void computeIntrinsicDimensions(Length& intrinsicWidth, Length& intrinsicHeight, FloatSize& intrinsicRatio) override;

    virtual bool dataChanged(bool allDataReceived) override;

    // FIXME: SVGImages are underreporting decoded sizes and will be unable
    // to prune because these functions are not implemented yet.
    virtual void destroyDecodedData(bool) override { }

    // FIXME: Implement this to be less conservative.
    virtual bool currentFrameKnownToBeOpaque() override { return false; }

    DisplayItemClient displayItemClient() const { return toDisplayItemClient(this); }

    SVGImage(ImageObserver*);
    void draw(GraphicsContext*, const FloatRect& fromRect, const FloatRect& toRect, SkXfermode::Mode, RespectImageOrientationEnum) override;
    void drawForContainer(GraphicsContext*, const FloatSize, float, const FloatRect&, const FloatRect&, SkXfermode::Mode);
    void drawPatternForContainer(GraphicsContext*, const FloatSize, float, const FloatRect&, const FloatSize&, const FloatPoint&,
        SkXfermode::Mode, const FloatRect&, const IntSize& repeatSpacing);

    OwnPtr<SVGImageChromeClient> m_chromeClient;
    OwnPtrWillBePersistent<Page> m_page;
    IntSize m_intrinsicSize;
    KURL m_url;
};

DEFINE_IMAGE_TYPE_CASTS(SVGImage);

class ImageObserverDisabler {
    WTF_MAKE_NONCOPYABLE(ImageObserverDisabler);
public:
    ImageObserverDisabler(Image* image)
        : m_image(image)
    {
        m_observer = m_image->imageObserver();
        m_image->setImageObserver(0);
    }

    ~ImageObserverDisabler()
    {
        m_image->setImageObserver(m_observer);
    }
private:
    Image* m_image;
    ImageObserver* m_observer;
};

}

#endif // SVGImage_h
