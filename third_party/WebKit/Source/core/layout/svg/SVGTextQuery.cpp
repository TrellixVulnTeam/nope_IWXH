/*
 * Copyright (C) Research In Motion Limited 2010-2012. All rights reserved.
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
#include "core/layout/svg/SVGTextQuery.h"

#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/line/InlineFlowBox.h"
#include "core/layout/svg/LayoutSVGInlineText.h"
#include "core/layout/svg/SVGTextMetrics.h"
#include "core/layout/svg/line/SVGInlineTextBox.h"
#include "platform/FloatConversion.h"
#include "wtf/MathExtras.h"

namespace blink {

// Base structure for callback user data
struct SVGTextQuery::Data {
    Data()
        : isVerticalText(false)
        , processedCharacters(0)
        , textRenderer(0)
        , textBox(0)
    {
    }

    bool isVerticalText;
    unsigned processedCharacters;
    LayoutSVGInlineText* textRenderer;
    const SVGInlineTextBox* textBox;
};

static inline InlineFlowBox* flowBoxForRenderer(LayoutObject* renderer)
{
    if (!renderer)
        return 0;

    if (renderer->isLayoutBlock()) {
        // If we're given a block element, it has to be a LayoutSVGText.
        ASSERT(renderer->isSVGText());
        LayoutBlockFlow* layoutBlockFlow = toLayoutBlockFlow(renderer);

        // LayoutSVGText only ever contains a single line box.
        InlineFlowBox* flowBox = layoutBlockFlow->firstLineBox();
        ASSERT(flowBox == layoutBlockFlow->lastLineBox());
        return flowBox;
    }

    if (renderer->isLayoutInline()) {
        // We're given a LayoutSVGInline or objects that derive from it (LayoutSVGTSpan / LayoutSVGTextPath)
        LayoutInline* layoutInline = toLayoutInline(renderer);

        // LayoutSVGInline only ever contains a single line box.
        InlineFlowBox* flowBox = layoutInline->firstLineBox();
        ASSERT(flowBox == layoutInline->lastLineBox());
        return flowBox;
    }

    ASSERT_NOT_REACHED();
    return 0;
}

SVGTextQuery::SVGTextQuery(LayoutObject* renderer)
{
    collectTextBoxesInFlowBox(flowBoxForRenderer(renderer));
}

void SVGTextQuery::collectTextBoxesInFlowBox(InlineFlowBox* flowBox)
{
    if (!flowBox)
        return;

    for (InlineBox* child = flowBox->firstChild(); child; child = child->nextOnLine()) {
        if (child->isInlineFlowBox()) {
            // Skip generated content.
            if (!child->layoutObject().node())
                continue;

            collectTextBoxesInFlowBox(toInlineFlowBox(child));
            continue;
        }

        if (child->isSVGInlineTextBox())
            m_textBoxes.append(toSVGInlineTextBox(child));
    }
}

bool SVGTextQuery::executeQuery(Data* queryData, ProcessTextFragmentCallback fragmentCallback) const
{
    unsigned processedCharacters = 0;
    unsigned textBoxCount = m_textBoxes.size();

    // Loop over all text boxes
    for (unsigned textBoxPosition = 0; textBoxPosition < textBoxCount; ++textBoxPosition) {
        queryData->textBox = m_textBoxes.at(textBoxPosition);
        queryData->textRenderer = &toLayoutSVGInlineText(queryData->textBox->layoutObject());
        ASSERT(queryData->textRenderer->style());

        queryData->isVerticalText = queryData->textRenderer->style()->svgStyle().isVerticalWritingMode();
        const Vector<SVGTextFragment>& fragments = queryData->textBox->textFragments();

        // Loop over all text fragments in this text box, firing a callback for each.
        unsigned fragmentCount = fragments.size();
        for (unsigned i = 0; i < fragmentCount; ++i) {
            const SVGTextFragment& fragment = fragments.at(i);
            if ((this->*fragmentCallback)(queryData, fragment))
                return true;

            processedCharacters += fragment.length;
        }

        queryData->processedCharacters = processedCharacters;
    }

    return false;
}

bool SVGTextQuery::mapStartEndPositionsIntoFragmentCoordinates(Data* queryData, const SVGTextFragment& fragment, int& startPosition, int& endPosition) const
{
    // Reuse the same logic used for text selection & painting, to map our query start/length into start/endPositions of the current text fragment.
    startPosition -= queryData->processedCharacters;
    endPosition -= queryData->processedCharacters;

    // <startPosition, endPosition> is now a tuple of offsets relative to the current text box.
    // Compute the offsets of the fragment in the same offset space.
    int fragmentStartInBox = fragment.characterOffset - queryData->textBox->start();
    int fragmentEndInBox = fragmentStartInBox + fragment.length;

    // Check if the ranges intersect.
    startPosition = std::max(startPosition, fragmentStartInBox);
    endPosition = std::min(endPosition, fragmentEndInBox);

    if (startPosition >= endPosition)
        return false;

    modifyStartEndPositionsRespectingLigatures(queryData, fragment, startPosition, endPosition);
    if (!queryData->textBox->mapStartEndPositionsIntoFragmentCoordinates(fragment, startPosition, endPosition))
        return false;

    ASSERT(startPosition < endPosition);
    return true;
}

void SVGTextQuery::modifyStartEndPositionsRespectingLigatures(Data* queryData, const SVGTextFragment& fragment, int& startPosition, int& endPosition) const
{
    SVGTextLayoutAttributes* layoutAttributes = queryData->textRenderer->layoutAttributes();
    Vector<SVGTextMetrics>& textMetricsValues = layoutAttributes->textMetricsValues();

    unsigned textMetricsOffset = fragment.metricsListOffset;

    // Compute the offset of the fragment within the box, since that's the
    // space <startPosition, endPosition> is in (and that's what we need).
    int fragmentOffsetInBox = fragment.characterOffset - queryData->textBox->start();
    int fragmentEndInBox = fragmentOffsetInBox + fragment.length;

    // Find the text metrics cell that start at or contain the character startPosition.
    while (fragmentOffsetInBox < fragmentEndInBox) {
        SVGTextMetrics& metrics = textMetricsValues[textMetricsOffset];
        int glyphEnd = fragmentOffsetInBox + metrics.length();
        if (startPosition < glyphEnd)
            break;
        fragmentOffsetInBox = glyphEnd;
        textMetricsOffset++;
    }

    startPosition = fragmentOffsetInBox;

    // Find the text metrics cell that contain or ends at the character endPosition.
    while (fragmentOffsetInBox < fragmentEndInBox) {
        SVGTextMetrics& metrics = textMetricsValues[textMetricsOffset];
        fragmentOffsetInBox += metrics.length();
        if (fragmentOffsetInBox >= endPosition)
            break;
        textMetricsOffset++;
    }

    endPosition = fragmentOffsetInBox;
}

// numberOfCharacters() implementation
bool SVGTextQuery::numberOfCharactersCallback(Data*, const SVGTextFragment&) const
{
    // no-op
    return false;
}

unsigned SVGTextQuery::numberOfCharacters() const
{
    Data data;
    executeQuery(&data, &SVGTextQuery::numberOfCharactersCallback);
    return data.processedCharacters;
}

// textLength() implementation
struct TextLengthData : SVGTextQuery::Data {
    TextLengthData()
        : textLength(0)
    {
    }

    float textLength;
};

bool SVGTextQuery::textLengthCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    TextLengthData* data = static_cast<TextLengthData*>(queryData);
    data->textLength += queryData->isVerticalText ? fragment.height : fragment.width;
    return false;
}

float SVGTextQuery::textLength() const
{
    TextLengthData data;
    executeQuery(&data, &SVGTextQuery::textLengthCallback);
    return data.textLength;
}

// subStringLength() implementation
struct SubStringLengthData : SVGTextQuery::Data {
    SubStringLengthData(unsigned queryStartPosition, unsigned queryLength)
        : startPosition(queryStartPosition)
        , length(queryLength)
        , subStringLength(0)
    {
    }

    unsigned startPosition;
    unsigned length;

    float subStringLength;
};

bool SVGTextQuery::subStringLengthCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    SubStringLengthData* data = static_cast<SubStringLengthData*>(queryData);

    int startPosition = data->startPosition;
    int endPosition = startPosition + data->length;
    if (!mapStartEndPositionsIntoFragmentCoordinates(queryData, fragment, startPosition, endPosition))
        return false;

    SVGTextMetrics metrics = SVGTextMetrics::measureCharacterRange(queryData->textRenderer, fragment.characterOffset + startPosition, endPosition - startPosition);
    data->subStringLength += queryData->isVerticalText ? metrics.height() : metrics.width();
    return false;
}

float SVGTextQuery::subStringLength(unsigned startPosition, unsigned length) const
{
    SubStringLengthData data(startPosition, length);
    executeQuery(&data, &SVGTextQuery::subStringLengthCallback);
    return data.subStringLength;
}

// startPositionOfCharacter() implementation
struct StartPositionOfCharacterData : SVGTextQuery::Data {
    StartPositionOfCharacterData(unsigned queryPosition)
        : position(queryPosition)
    {
    }

    unsigned position;
    FloatPoint startPosition;
};

bool SVGTextQuery::startPositionOfCharacterCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    StartPositionOfCharacterData* data = static_cast<StartPositionOfCharacterData*>(queryData);

    int startPosition = data->position;
    int endPosition = startPosition + 1;
    if (!mapStartEndPositionsIntoFragmentCoordinates(queryData, fragment, startPosition, endPosition))
        return false;

    data->startPosition = FloatPoint(fragment.x, fragment.y);

    if (startPosition) {
        SVGTextMetrics metrics = SVGTextMetrics::measureCharacterRange(queryData->textRenderer, fragment.characterOffset, startPosition);
        if (queryData->isVerticalText)
            data->startPosition.move(0, metrics.height());
        else
            data->startPosition.move(metrics.width(), 0);
    }

    AffineTransform fragmentTransform;
    fragment.buildFragmentTransform(fragmentTransform, SVGTextFragment::TransformIgnoringTextLength);
    if (fragmentTransform.isIdentity())
        return true;

    data->startPosition = fragmentTransform.mapPoint(data->startPosition);
    return true;
}

FloatPoint SVGTextQuery::startPositionOfCharacter(unsigned position) const
{
    StartPositionOfCharacterData data(position);
    executeQuery(&data, &SVGTextQuery::startPositionOfCharacterCallback);
    return data.startPosition;
}

// endPositionOfCharacter() implementation
struct EndPositionOfCharacterData : SVGTextQuery::Data {
    EndPositionOfCharacterData(unsigned queryPosition)
        : position(queryPosition)
    {
    }

    unsigned position;
    FloatPoint endPosition;
};

bool SVGTextQuery::endPositionOfCharacterCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    EndPositionOfCharacterData* data = static_cast<EndPositionOfCharacterData*>(queryData);

    int startPosition = data->position;
    int endPosition = startPosition + 1;
    if (!mapStartEndPositionsIntoFragmentCoordinates(queryData, fragment, startPosition, endPosition))
        return false;

    data->endPosition = FloatPoint(fragment.x, fragment.y);

    SVGTextMetrics metrics = SVGTextMetrics::measureCharacterRange(queryData->textRenderer, fragment.characterOffset, startPosition + 1);
    if (queryData->isVerticalText)
        data->endPosition.move(0, metrics.height());
    else
        data->endPosition.move(metrics.width(), 0);

    AffineTransform fragmentTransform;
    fragment.buildFragmentTransform(fragmentTransform, SVGTextFragment::TransformIgnoringTextLength);
    if (fragmentTransform.isIdentity())
        return true;

    data->endPosition = fragmentTransform.mapPoint(data->endPosition);
    return true;
}

FloatPoint SVGTextQuery::endPositionOfCharacter(unsigned position) const
{
    EndPositionOfCharacterData data(position);
    executeQuery(&data, &SVGTextQuery::endPositionOfCharacterCallback);
    return data.endPosition;
}

// rotationOfCharacter() implementation
struct RotationOfCharacterData : SVGTextQuery::Data {
    RotationOfCharacterData(unsigned queryPosition)
        : position(queryPosition)
        , rotation(0)
    {
    }

    unsigned position;
    float rotation;
};

bool SVGTextQuery::rotationOfCharacterCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    RotationOfCharacterData* data = static_cast<RotationOfCharacterData*>(queryData);

    int startPosition = data->position;
    int endPosition = startPosition + 1;
    if (!mapStartEndPositionsIntoFragmentCoordinates(queryData, fragment, startPosition, endPosition))
        return false;

    AffineTransform fragmentTransform;
    fragment.buildFragmentTransform(fragmentTransform, SVGTextFragment::TransformIgnoringTextLength);
    if (fragmentTransform.isIdentity()) {
        data->rotation = 0;
    } else {
        fragmentTransform.scale(1 / fragmentTransform.xScale(), 1 / fragmentTransform.yScale());
        data->rotation = narrowPrecisionToFloat(rad2deg(atan2(fragmentTransform.b(), fragmentTransform.a())));
    }

    return true;
}

float SVGTextQuery::rotationOfCharacter(unsigned position) const
{
    RotationOfCharacterData data(position);
    executeQuery(&data, &SVGTextQuery::rotationOfCharacterCallback);
    return data.rotation;
}

// extentOfCharacter() implementation
struct ExtentOfCharacterData : SVGTextQuery::Data {
    ExtentOfCharacterData(unsigned queryPosition)
        : position(queryPosition)
    {
    }

    unsigned position;
    FloatRect extent;
};

static inline void calculateGlyphBoundaries(SVGTextQuery::Data* queryData, const SVGTextFragment& fragment, int startPosition, FloatRect& extent)
{
    float scalingFactor = queryData->textRenderer->scalingFactor();
    ASSERT(scalingFactor);

    extent.setLocation(FloatPoint(fragment.x, fragment.y - queryData->textRenderer->scaledFont().fontMetrics().floatAscent() / scalingFactor));

    if (startPosition) {
        SVGTextMetrics metrics = SVGTextMetrics::measureCharacterRange(queryData->textRenderer, fragment.characterOffset, startPosition);
        if (queryData->isVerticalText)
            extent.move(0, metrics.height());
        else
            extent.move(metrics.width(), 0);
    }

    SVGTextMetrics metrics = SVGTextMetrics::measureCharacterRange(queryData->textRenderer, fragment.characterOffset + startPosition, 1);
    extent.setSize(FloatSize(metrics.width(), metrics.height()));

    AffineTransform fragmentTransform;
    fragment.buildFragmentTransform(fragmentTransform, SVGTextFragment::TransformIgnoringTextLength);

    extent = fragmentTransform.mapRect(extent);
}

static inline FloatRect calculateFragmentBoundaries(const LayoutSVGInlineText& textRenderer, const SVGTextFragment& fragment)
{
    float scalingFactor = textRenderer.scalingFactor();
    ASSERT(scalingFactor);
    float baseline = textRenderer.scaledFont().fontMetrics().floatAscent() / scalingFactor;

    AffineTransform fragmentTransform;
    FloatRect fragmentRect(fragment.x, fragment.y - baseline, fragment.width, fragment.height);
    fragment.buildFragmentTransform(fragmentTransform);
    return fragmentTransform.mapRect(fragmentRect);
}

bool SVGTextQuery::extentOfCharacterCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    ExtentOfCharacterData* data = static_cast<ExtentOfCharacterData*>(queryData);

    int startPosition = data->position;
    int endPosition = startPosition + 1;
    if (!mapStartEndPositionsIntoFragmentCoordinates(queryData, fragment, startPosition, endPosition))
        return false;

    calculateGlyphBoundaries(queryData, fragment, startPosition, data->extent);
    return true;
}

FloatRect SVGTextQuery::extentOfCharacter(unsigned position) const
{
    ExtentOfCharacterData data(position);
    executeQuery(&data, &SVGTextQuery::extentOfCharacterCallback);
    return data.extent;
}

// characterNumberAtPosition() implementation
struct CharacterNumberAtPositionData : SVGTextQuery::Data {
    CharacterNumberAtPositionData(const FloatPoint& queryPosition)
        : position(queryPosition)
    {
    }

    FloatPoint position;
};

bool SVGTextQuery::characterNumberAtPositionCallback(Data* queryData, const SVGTextFragment& fragment) const
{
    CharacterNumberAtPositionData* data = static_cast<CharacterNumberAtPositionData*>(queryData);

    // Test the query point against the bounds of the entire fragment first.
    FloatRect fragmentExtents = calculateFragmentBoundaries(*queryData->textRenderer, fragment);
    if (!fragmentExtents.contains(data->position))
        return false;

    // Iterate through the glyphs in this fragment, and check if their extents
    // contain the query point.
    FloatRect extent;
    const Vector<SVGTextMetrics>& textMetrics = queryData->textRenderer->layoutAttributes()->textMetricsValues();
    unsigned textMetricsOffset = fragment.metricsListOffset;
    unsigned fragmentOffset = 0;
    while (fragmentOffset < fragment.length) {
        calculateGlyphBoundaries(queryData, fragment, fragmentOffset, extent);
        if (extent.contains(data->position)) {
            // Compute the character offset of the glyph within the text box
            // and add to processedCharacters.
            unsigned characterOffset = fragment.characterOffset + fragmentOffset;
            data->processedCharacters += characterOffset - data->textBox->start();
            return true;
        }
        fragmentOffset += textMetrics[textMetricsOffset].length();
        textMetricsOffset++;
    }
    return false;
}

int SVGTextQuery::characterNumberAtPosition(const FloatPoint& position) const
{
    CharacterNumberAtPositionData data(position);
    if (!executeQuery(&data, &SVGTextQuery::characterNumberAtPositionCallback))
        return -1;

    return data.processedCharacters;
}

}
