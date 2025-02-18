/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2012 Intel Inc. All rights reserved.
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

#ifndef PerformanceEntry_h
#define PerformanceEntry_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/text/WTFString.h"

namespace blink {

class PerformanceEntry : public RefCountedWillBeGarbageCollectedFinalized<PerformanceEntry>, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    virtual ~PerformanceEntry();

    String name() const;
    String entryType() const;
    double startTime() const;
    double duration() const;

    virtual bool isResource() { return false; }
    virtual bool isMark() { return false; }
    virtual bool isMeasure() { return false; }

    static bool startTimeCompareLessThan(PassRefPtrWillBeRawPtr<PerformanceEntry> a, PassRefPtrWillBeRawPtr<PerformanceEntry> b)
    {
        return a->startTime() < b->startTime();
    }

    DEFINE_INLINE_VIRTUAL_TRACE() { }

protected:
    PerformanceEntry(const String& name, const String& entryType, double startTime, double finishTime);

private:
    const String m_name;
    const String m_entryType;
    const double m_startTime;
    const double m_duration;
};

} // namespace blink

#endif // PerformanceEntry_h
