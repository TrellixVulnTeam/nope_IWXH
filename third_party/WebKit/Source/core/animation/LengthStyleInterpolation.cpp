// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/animation/LengthStyleInterpolation.h"

#include "core/animation/css/CSSAnimatableValueFactory.h"
#include "core/css/CSSCalculationValue.h"
#include "core/css/resolver/StyleBuilder.h"
#include "core/css/resolver/StyleResolverState.h"
#include "platform/CalculationValue.h"

namespace blink {

namespace {

bool pixelsForKeyword(CSSPropertyID property, CSSValueID valueID, double& result)
{
    switch (property) {
    case CSSPropertyBorderBottomWidth:
    case CSSPropertyBorderLeftWidth:
    case CSSPropertyBorderRightWidth:
    case CSSPropertyBorderTopWidth:
    case CSSPropertyWebkitColumnRuleWidth:
    case CSSPropertyOutlineWidth:
        if (valueID == CSSValueThin) {
            result = 1;
            return true;
        }
        if (valueID == CSSValueMedium) {
            result = 3;
            return true;
        }
        if (valueID == CSSValueThick) {
            result = 5;
            return true;
        }
        return false;
    case CSSPropertyLetterSpacing:
        if (valueID == CSSValueNormal) {
            result = 0;
            return true;
        }
        return false;
    default:
        return false;
    }
}

} // namespace

bool LengthStyleInterpolation::canCreateFrom(const CSSValue& value, CSSPropertyID property)
{
    if (value.isPrimitiveValue()) {
        const CSSPrimitiveValue& primitiveValue = blink::toCSSPrimitiveValue(value);
        if (primitiveValue.cssCalcValue())
            return true;

        if (primitiveValue.isValueID()) {
            CSSValueID valueID = primitiveValue.getValueID();
            double pixels;
            return pixelsForKeyword(property, valueID, pixels);
        }

        CSSPrimitiveValue::LengthUnitType type;
        // Only returns true if the type is a primitive length unit.
        return CSSPrimitiveValue::unitTypeToLengthUnitType(primitiveValue.primitiveType(), type);
    }
    return value.isCalcValue();
}

PassOwnPtrWillBeRawPtr<InterpolableValue> LengthStyleInterpolation::toInterpolableValue(const CSSValue& value, CSSPropertyID id)
{
    ASSERT(canCreateFrom(value, id));
    OwnPtrWillBeRawPtr<InterpolableList> listOfValuesAndTypes = InterpolableList::create(2);
    OwnPtrWillBeRawPtr<InterpolableList> listOfValues = InterpolableList::create(CSSPrimitiveValue::LengthUnitTypeCount);
    OwnPtrWillBeRawPtr<InterpolableList> listOfTypes = InterpolableList::create(CSSPrimitiveValue::LengthUnitTypeCount);

    const CSSPrimitiveValue& primitive = toCSSPrimitiveValue(value);

    CSSLengthArray arrayOfValues;
    CSSPrimitiveValue::CSSLengthTypeArray arrayOfTypes;
    for (size_t i = 0; i < CSSPrimitiveValue::LengthUnitTypeCount; i++)
        arrayOfValues.append(0);

    arrayOfTypes.ensureSize(CSSPrimitiveValue::LengthUnitTypeCount);
    if (primitive.isValueID()) {
        CSSValueID valueID = primitive.getValueID();
        double pixels;
        pixelsForKeyword(id, valueID, pixels);
        arrayOfTypes.set(CSSPrimitiveValue::UnitTypePixels);
        arrayOfValues[CSSPrimitiveValue::UnitTypePixels] = pixels;
    } else {
        primitive.accumulateLengthArray(arrayOfValues, arrayOfTypes);
    }

    for (size_t i = 0; i < CSSPrimitiveValue::LengthUnitTypeCount; i++) {
        listOfValues->set(i, InterpolableNumber::create(arrayOfValues.at(i)));
        listOfTypes->set(i, InterpolableNumber::create(arrayOfTypes.get(i)));
    }

    listOfValuesAndTypes->set(0, listOfValues.release());
    listOfValuesAndTypes->set(1, listOfTypes.release());

    return listOfValuesAndTypes.release();
}

bool LengthStyleInterpolation::isPixelsOrPercentOnly(const InterpolableValue& value)
{
    const InterpolableList& types = *toInterpolableList(toInterpolableList(value).get(1));
    bool result = false;
    for (size_t i = 0; i < CSSPrimitiveValue::LengthUnitTypeCount; i++) {
        bool typeIsPresent = toInterpolableNumber(types.get(i))->value();
        if (i == CSSPrimitiveValue::UnitTypePixels)
            result |= typeIsPresent;
        else if (i == CSSPrimitiveValue::UnitTypePercentage)
            result |= typeIsPresent;
        else if (typeIsPresent)
            return false;
    }
    return result;
}

LengthStyleInterpolation::LengthSetter LengthStyleInterpolation::lengthSetterForProperty(CSSPropertyID property)
{
    switch (property) {
    case CSSPropertyBottom:
        return &LayoutStyle::setBottom;
    case CSSPropertyCx:
        return &LayoutStyle::setCx;
    case CSSPropertyCy:
        return &LayoutStyle::setCy;
    case CSSPropertyFlexBasis:
        return &LayoutStyle::setFlexBasis;
    case CSSPropertyHeight:
        return &LayoutStyle::setHeight;
    case CSSPropertyLeft:
        return &LayoutStyle::setLeft;
    case CSSPropertyLineHeight:
        return &LayoutStyle::setLineHeight;
    case CSSPropertyMarginBottom:
        return &LayoutStyle::setMarginBottom;
    case CSSPropertyMarginLeft:
        return &LayoutStyle::setMarginLeft;
    case CSSPropertyMarginRight:
        return &LayoutStyle::setMarginRight;
    case CSSPropertyMarginTop:
        return &LayoutStyle::setMarginTop;
    case CSSPropertyMaxHeight:
        return &LayoutStyle::setMaxHeight;
    case CSSPropertyMaxWidth:
        return &LayoutStyle::setMaxWidth;
    case CSSPropertyMinHeight:
        return &LayoutStyle::setMinHeight;
    case CSSPropertyMinWidth:
        return &LayoutStyle::setMinWidth;
    case CSSPropertyMotionOffset:
        return &LayoutStyle::setMotionOffset;
    case CSSPropertyPaddingBottom:
        return &LayoutStyle::setPaddingBottom;
    case CSSPropertyPaddingLeft:
        return &LayoutStyle::setPaddingLeft;
    case CSSPropertyPaddingRight:
        return &LayoutStyle::setPaddingRight;
    case CSSPropertyPaddingTop:
        return &LayoutStyle::setPaddingTop;
    case CSSPropertyR:
        return &LayoutStyle::setR;
    case CSSPropertyRx:
        return &LayoutStyle::setRx;
    case CSSPropertyRy:
        return &LayoutStyle::setRy;
    case CSSPropertyRight:
        return &LayoutStyle::setRight;
    case CSSPropertyShapeMargin:
        return &LayoutStyle::setShapeMargin;
    case CSSPropertyStrokeDashoffset:
        return &LayoutStyle::setStrokeDashOffset;
    case CSSPropertyTop:
        return &LayoutStyle::setTop;
    case CSSPropertyWidth:
        return &LayoutStyle::setWidth;
    case CSSPropertyX:
        return &LayoutStyle::setX;
    case CSSPropertyY:
        return &LayoutStyle::setY;
    // These properties don't have a LayoutStyle setter with the signature void(*)(const Length&).
    case CSSPropertyBaselineShift:
    case CSSPropertyBorderBottomWidth:
    case CSSPropertyBorderLeftWidth:
    case CSSPropertyBorderRightWidth:
    case CSSPropertyBorderTopWidth:
    case CSSPropertyFontSize:
    case CSSPropertyLetterSpacing:
    case CSSPropertyOutlineOffset:
    case CSSPropertyOutlineWidth:
    case CSSPropertyPerspective:
    case CSSPropertyStrokeWidth:
    case CSSPropertyVerticalAlign:
    case CSSPropertyWebkitBorderHorizontalSpacing:
    case CSSPropertyWebkitBorderVerticalSpacing:
    case CSSPropertyWebkitColumnGap:
    case CSSPropertyWebkitColumnRuleWidth:
    case CSSPropertyWebkitColumnWidth:
    case CSSPropertyWordSpacing:
        return nullptr;
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

namespace {

static CSSPrimitiveValue::UnitType toUnitType(int lengthUnitType)
{
    return static_cast<CSSPrimitiveValue::UnitType>(CSSPrimitiveValue::lengthUnitTypeToUnitType(static_cast<CSSPrimitiveValue::LengthUnitType>(lengthUnitType)));
}

static PassRefPtrWillBeRawPtr<CSSCalcExpressionNode> constructCalcExpression(const InterpolableList* list)
{
    const InterpolableList* listOfValues = toInterpolableList(list->get(0));
    const InterpolableList* listOfTypes = toInterpolableList(list->get(1));
    RefPtrWillBeRawPtr<CSSCalcExpressionNode> expression = nullptr;
    for (size_t position = 0; position < CSSPrimitiveValue::LengthUnitTypeCount; position++) {
        const InterpolableNumber *subValueType = toInterpolableNumber(listOfTypes->get(position));
        if (!subValueType->value())
            continue;
        double value = toInterpolableNumber(listOfValues->get(position))->value();
        RefPtrWillBeRawPtr<CSSCalcExpressionNode> currentTerm = CSSCalcValue::createExpressionNode(CSSPrimitiveValue::create(value, toUnitType(position)));
        if (expression)
            expression = CSSCalcValue::createExpressionNode(expression.release(), currentTerm.release(), CalcAdd);
        else
            expression = currentTerm.release();
    }
    return expression.release();
}

static double clampToRange(double x, ValueRange range)
{
    return (range == ValueRangeNonNegative && x < 0) ? 0 : x;
}

static Length lengthFromInterpolableValue(const InterpolableValue& value, InterpolationRange interpolationRange, float zoom)
{
    const InterpolableList& values = *toInterpolableList(toInterpolableList(value).get(0));
    const InterpolableList& types = *toInterpolableList(toInterpolableList(value).get(1));
    bool hasPixels = toInterpolableNumber(types.get(CSSPrimitiveValue::UnitTypePixels))->value();
    bool hasPercent = toInterpolableNumber(types.get(CSSPrimitiveValue::UnitTypePercentage))->value();

    ValueRange range = (interpolationRange == RangeNonNegative) ? ValueRangeNonNegative : ValueRangeAll;
    PixelsAndPercent pixelsAndPercent(0, 0);
    if (hasPixels)
        pixelsAndPercent.pixels = toInterpolableNumber(values.get(CSSPrimitiveValue::UnitTypePixels))->value() * zoom;
    if (hasPercent)
        pixelsAndPercent.percent = toInterpolableNumber(values.get(CSSPrimitiveValue::UnitTypePercentage))->value();

    if (hasPixels && hasPercent)
        return Length(CalculationValue::create(pixelsAndPercent, range));
    if (hasPixels)
        return Length(clampToRange(pixelsAndPercent.pixels, range), Fixed);
    if (hasPercent)
        return Length(clampToRange(pixelsAndPercent.percent, range), Percent);
    ASSERT_NOT_REACHED();
    return Length(0, Fixed);
}

}

PassRefPtrWillBeRawPtr<CSSPrimitiveValue> LengthStyleInterpolation::fromInterpolableValue(const InterpolableValue& value, InterpolationRange range)
{
    const InterpolableList* listOfValuesAndTypes = toInterpolableList(&value);
    const InterpolableList* listOfValues = toInterpolableList(listOfValuesAndTypes->get(0));
    const InterpolableList* listOfTypes = toInterpolableList(listOfValuesAndTypes->get(1));
    unsigned unitTypeCount = 0;
    for (size_t i = 0; i < CSSPrimitiveValue::LengthUnitTypeCount; i++) {
        const InterpolableNumber* subType = toInterpolableNumber(listOfTypes->get(i));
        if (subType->value()) {
            unitTypeCount++;
        }
    }

    switch (unitTypeCount) {
    case 0:
        // TODO: this case should never be reached. This issue should be fixed once we have multiple interpolators.
        return CSSPrimitiveValue::create(0, CSSPrimitiveValue::CSS_PX);
    case 1:
        for (size_t i = 0; i < CSSPrimitiveValue::LengthUnitTypeCount; i++) {
            const InterpolableNumber *subValueType = toInterpolableNumber(listOfTypes->get(i));
            if (subValueType->value()) {
                double value = toInterpolableNumber(listOfValues->get(i))->value();
                if (range == RangeNonNegative && value < 0)
                    value = 0;
                return CSSPrimitiveValue::create(value, toUnitType(i));
            }
        }
        ASSERT_NOT_REACHED();
    default:
        ValueRange valueRange = (range == RangeNonNegative) ? ValueRangeNonNegative : ValueRangeAll;
        return CSSPrimitiveValue::create(CSSCalcValue::create(constructCalcExpression(listOfValuesAndTypes), valueRange));
    }
}

void LengthStyleInterpolation::apply(StyleResolverState& state) const
{
    if (m_lengthSetter) {
        (state.style()->*m_lengthSetter)(lengthFromInterpolableValue(*m_cachedValue, m_range, state.style()->effectiveZoom()));
#if ENABLE(ASSERT)
        RefPtrWillBeRawPtr<AnimatableValue> before = CSSAnimatableValueFactory::create(m_id, *state.style());
        StyleBuilder::applyProperty(m_id, state, fromInterpolableValue(*m_cachedValue, m_range).get());
        RefPtrWillBeRawPtr<AnimatableValue> after = CSSAnimatableValueFactory::create(m_id, *state.style());
        ASSERT(before->equals(*after));
#endif
    } else {
        StyleBuilder::applyProperty(m_id, state, fromInterpolableValue(*m_cachedValue, m_range).get());
    }
}

DEFINE_TRACE(LengthStyleInterpolation)
{
    StyleInterpolation::trace(visitor);
}

}
