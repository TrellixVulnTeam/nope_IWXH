/*
 * Copyright (C) 2006 Oliver Hunt <ojh16@student.canterbury.ac.nz>
 * Copyright (C) 2006 Apple Computer Inc.
 * Copyright (C) 2009 Google Inc.
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

#ifndef LayoutSVGTSpan_h
#define LayoutSVGTSpan_h

#include "core/layout/svg/LayoutSVGInline.h"

namespace blink {
class LayoutSVGTSpan final : public LayoutSVGInline {
public:
    explicit LayoutSVGTSpan(Element*);

    virtual bool isChildAllowed(LayoutObject*, const LayoutStyle&) const override;

    virtual const char* name() const override { return "LayoutSVGTSpan"; }
};
}

#endif // LayoutSVGTSpan_h
