// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Transform3DRecorder_h
#define Transform3DRecorder_h

#include "platform/graphics/paint/DisplayItem.h"

namespace blink {

class GraphicsContext;
class TransformationMatrix;

class Transform3DRecorder {
public:
    Transform3DRecorder(GraphicsContext&, DisplayItemClient, const TransformationMatrix&);
    ~Transform3DRecorder();

private:
    GraphicsContext& m_context;
    DisplayItemClient m_client;
    bool m_skipRecordingForIdentityTransform;
};

} // namespace blink

#endif // Transform3DRecorder_h
