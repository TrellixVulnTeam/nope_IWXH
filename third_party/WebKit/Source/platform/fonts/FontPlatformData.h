/*
 * Copyright (c) 2006, 2007, 2008, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef FontPlatformData_h
#define FontPlatformData_h

#include "SkPaint.h"
#include "platform/PlatformExport.h"
#include "platform/SharedBuffer.h"
#include "platform/fonts/FontDescription.h"
#include "platform/fonts/FontOrientation.h"
#include "platform/fonts/FontRenderStyle.h"
#include "platform/fonts/opentype/OpenTypeVerticalData.h"
#include "wtf/Forward.h"
#include "wtf/HashTableDeletedValueType.h"
#include "wtf/RefPtr.h"
#include "wtf/text/CString.h"
#include "wtf/text/StringImpl.h"

#if OS(MACOSX)
OBJC_CLASS NSFont;

typedef struct CGFont* CGFontRef;
typedef const struct __CTFont* CTFontRef;

#include "platform/fonts/mac/MemoryActivatedFont.h"
#include <CoreFoundation/CFBase.h>
#include <objc/objc-auto.h>

inline CTFontRef toCTFontRef(NSFont *nsFont) { return reinterpret_cast<CTFontRef>(nsFont); }
#endif // OS(MACOSX)

class SkTypeface;
typedef uint32_t SkFontID;

namespace blink {

class Font;
class GraphicsContext;
class HarfBuzzFace;

class PLATFORM_EXPORT FontPlatformData {
public:
    // Used for deleted values in the font cache's hash tables. The hash table
    // will create us with this structure, and it will compare other values
    // to this "Deleted" one. It expects the Deleted one to be differentiable
    // from the 0 one (created with the empty constructor), so we can't just
    // set everything to 0.
    FontPlatformData(WTF::HashTableDeletedValueType);
    FontPlatformData();
    FontPlatformData(const FontPlatformData&);
    FontPlatformData(float size, bool syntheticBold, bool syntheticItalic, FontOrientation = Horizontal);
    FontPlatformData(const FontPlatformData& src, float textSize);
#if OS(MACOSX)
    FontPlatformData(NSFont*, float size, bool syntheticBold = false, bool syntheticItalic = false, FontOrientation = Horizontal);
    FontPlatformData(CGFontRef, PassRefPtr<SkTypeface>, float size, bool syntheticBold, bool syntheticOblique, FontOrientation);
#else
    FontPlatformData(PassRefPtr<SkTypeface>, const char* name, float textSize, bool syntheticBold, bool syntheticItalic, FontOrientation = Horizontal, bool subpixelTextPosition = defaultUseSubpixelPositioning());
#endif
    ~FontPlatformData();

#if OS(MACOSX)
    NSFont* font() const { return m_font; }
    void setFont(NSFont*);

    CGFontRef cgFont() const { return m_cgFont.get(); }
    CTFontRef ctFont() const;

    bool roundsGlyphAdvances() const;
    bool allowsLigatures() const;

    bool isColorBitmapFont() const { return m_isColorBitmapFont; }
    bool isCompositeFontReference() const { return m_isCompositeFontReference; }
#endif

    String fontFamilyName() const;
    float size() const { return m_textSize; }
    bool isFixedPitch() const;
    bool syntheticBold() const { return m_syntheticBold; }
    bool syntheticItalic() const { return m_syntheticItalic; }

    SkTypeface* typeface() const;
    HarfBuzzFace* harfBuzzFace() const;
    SkFontID uniqueID() const;
    unsigned hash() const;

    FontOrientation orientation() const { return m_orientation; }
    void setOrientation(FontOrientation orientation) { m_orientation = orientation; }
    void setSyntheticBold(bool syntheticBold) { m_syntheticBold = syntheticBold; }
    void setSyntheticItalic(bool syntheticItalic) { m_syntheticItalic = syntheticItalic; }
    bool operator==(const FontPlatformData&) const;
    const FontPlatformData& operator=(const FontPlatformData&);

    bool isHashTableDeletedValue() const { return m_isHashTableDeletedValue; }
#if OS(WIN)
    void setMinSizeForAntiAlias(unsigned size) { m_minSizeForAntiAlias = size; }
    unsigned minSizeForAntiAlias() const { return m_minSizeForAntiAlias; }
    void setMinSizeForSubpixel(float size) { m_minSizeForSubpixel = size; }
    float minSizeForSubpixel() const { return m_minSizeForSubpixel; }
    void setHinting(SkPaint::Hinting style)
    {
        m_style.useAutoHint = 0;
        m_style.hintStyle = style;
    }
#endif
    bool fontContainsCharacter(UChar32 character);

    PassRefPtr<OpenTypeVerticalData> verticalData() const;
    PassRefPtr<SharedBuffer> openTypeTable(uint32_t table) const;

#if !OS(MACOSX)
    // The returned styles are all actual styles without FontRenderStyle::NoPreference.
    const FontRenderStyle& fontRenderStyle() const { return m_style; }
#endif
    void setupPaint(SkPaint*, float deviceScaleFactor = 1, const Font* = 0) const;

#if OS(WIN)
    int paintTextFlags() const { return m_paintTextFlags; }
#else
    static void setHinting(SkPaint::Hinting);
    static void setAutoHint(bool);
    static void setUseBitmaps(bool);
    static void setAntiAlias(bool);
    static void setSubpixelRendering(bool);
#endif

private:
#if !OS(MACOSX)
    bool static defaultUseSubpixelPositioning();
    void querySystemForRenderStyle(bool useSkiaSubpixelPositioning);
#else
    // Load various data about the font specified by |nsFont| with the size fontSize into the following output paramters:
    // Note: Callers should always take into account that for the Chromium port, |outNSFont| isn't necessarily the same
    // font as |nsFont|. This because the sandbox may block loading of the original font.
    // * outNSFont - The font that was actually loaded, for the Chromium port this may be different than nsFont.
    // The caller is responsible for calling CFRelease() on this parameter when done with it.
    // * cgFont - CGFontRef representing the input font at the specified point size.
    void loadFont(NSFont*, float fontSize, NSFont*& outNSFont, CGFontRef&);
    void platformDataInit(const FontPlatformData&);
    const FontPlatformData& platformDataAssign(const FontPlatformData&);
    bool isAATFont(CTFontRef) const;
#endif

    mutable RefPtr<SkTypeface> m_typeface;
#if !OS(WIN)
    CString m_family;
#endif

public:
    float m_textSize;
    bool m_syntheticBold;
    bool m_syntheticItalic;
    FontOrientation m_orientation;
#if OS(MACOSX)
    bool m_isColorBitmapFont;
    bool m_isCompositeFontReference;
#endif
private:
#if OS(MACOSX)
    NSFont* m_font;
    RetainPtr<CGFontRef> m_cgFont;
    mutable RetainPtr<CTFontRef> m_CTFont;
    RefPtr<MemoryActivatedFont> m_inMemoryFont;
#else
    FontRenderStyle m_style;
#endif

    mutable RefPtr<HarfBuzzFace> m_harfBuzzFace;
    bool m_isHashTableDeletedValue;
#if OS(WIN)
    int m_paintTextFlags;
    bool m_useSubpixelPositioning;
    unsigned m_minSizeForAntiAlias;
    float m_minSizeForSubpixel;
#endif
};

} // namespace blink

#endif // ifdef FontPlatformData_h
