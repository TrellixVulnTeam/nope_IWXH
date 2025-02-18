/*
 * Copyright (C) 2004, 2005 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
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

#include "core/svg/SVGSymbolElement.h"

#include "core/SVGNames.h"
#include "core/layout/svg/LayoutSVGHiddenContainer.h"

namespace blink {

inline SVGSymbolElement::SVGSymbolElement(Document& document)
    : SVGElement(SVGNames::symbolTag, document)
{
    SVGFitToViewBox::initialize(this);
}

DEFINE_TRACE(SVGSymbolElement)
{
    SVGElement::trace(visitor);
    SVGFitToViewBox::trace(visitor);
}

DEFINE_NODE_FACTORY(SVGSymbolElement)

void SVGSymbolElement::parseAttribute(const QualifiedName& name, const AtomicString& value)
{
    parseAttributeNew(name, value);
}

void SVGSymbolElement::svgAttributeChanged(const QualifiedName& attrName)
{
    if (SVGFitToViewBox::isKnownAttribute(attrName))
        invalidateInstances();
}

LayoutObject* SVGSymbolElement::createLayoutObject(const LayoutStyle&)
{
    return new LayoutSVGHiddenContainer(this);
}

}
