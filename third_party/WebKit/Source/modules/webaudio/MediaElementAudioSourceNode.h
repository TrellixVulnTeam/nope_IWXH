/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MediaElementAudioSourceNode_h
#define MediaElementAudioSourceNode_h

#if ENABLE(WEB_AUDIO)

#include "modules/webaudio/AudioSourceNode.h"
#include "platform/audio/AudioSourceProviderClient.h"
#include "platform/audio/MultiChannelResampler.h"
#include "wtf/OwnPtr.h"
#include "wtf/PassRefPtr.h"
#include "wtf/ThreadingPrimitives.h"

namespace blink {

class AudioContext;
class HTMLMediaElement;

class MediaElementAudioSourceNode final : public AudioSourceNode, public AudioSourceProviderClient {
    DEFINE_WRAPPERTYPEINFO();
    USING_GARBAGE_COLLECTED_MIXIN(MediaElementAudioSourceNode);
public:
    static MediaElementAudioSourceNode* create(AudioContext*, HTMLMediaElement*);

    virtual ~MediaElementAudioSourceNode();

    HTMLMediaElement* mediaElement() { return m_mediaElement.get(); }

    // AudioNode
    virtual void dispose() override;
    virtual void process(size_t framesToProcess) override;

    // AudioSourceProviderClient
    virtual void setFormat(size_t numberOfChannels, float sampleRate) override;

    virtual void lock() override;
    virtual void unlock() override;

    DECLARE_VIRTUAL_TRACE();

private:
    MediaElementAudioSourceNode(AudioContext*, HTMLMediaElement*);

    // As an audio source, we will never propagate silence.
    virtual bool propagatesSilence() const override { return false; }

    bool passesCORSAccessCheck();

    RefPtrWillBeMember<HTMLMediaElement> m_mediaElement;
    Mutex m_processLock;

    unsigned m_sourceNumberOfChannels;
    double m_sourceSampleRate;

    OwnPtr<MultiChannelResampler> m_multiChannelResampler;
};

} // namespace blink

#endif // ENABLE(WEB_AUDIO)

#endif // MediaElementAudioSourceNode_h
