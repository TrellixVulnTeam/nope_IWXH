/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller <mueller@kde.org>
    Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
    Copyright (C) 2004, 2005, 2006, 2007 Apple Inc. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef RawResource_h
#define RawResource_h

#include "core/fetch/ResourceClient.h"
#include "core/fetch/ResourcePtr.h"
#include "public/platform/WebDataConsumerHandle.h"
#include "wtf/PassOwnPtr.h"

namespace blink {
class RawResourceClient;

class RawResource final : public Resource {
public:
    typedef RawResourceClient ClientType;

    RawResource(const ResourceRequest&, Type);

    // FIXME: AssociatedURLLoader shouldn't be a DocumentThreadableLoader and therefore shouldn't
    // use RawResource. However, it is, and it needs to be able to defer loading.
    // This can be fixed by splitting CORS preflighting out of DocumentThreacableLoader.
    void setDefersLoading(bool);

    bool canReuse(const ResourceRequest&) const override;

private:
    void didAddClient(ResourceClient*) override;
    void appendData(const char*, unsigned) override;

    bool shouldIgnoreHTTPStatusCodeErrors() const override { return true; }

    void willFollowRedirect(ResourceRequest&, const ResourceResponse&) override;
    void updateRequest(const ResourceRequest&) override;
    void responseReceived(const ResourceResponse&, PassOwnPtr<WebDataConsumerHandle>) override;
    void setSerializedCachedMetadata(const char*, size_t) override;
    void didSendData(unsigned long long bytesSent, unsigned long long totalBytesToBeSent) override;
    void didDownloadData(int) override;
};

#if ENABLE(SECURITY_ASSERT)
inline bool isRawResource(const Resource& resource)
{
    Resource::Type type = resource.type();
    return type == Resource::MainResource || type == Resource::Raw || type == Resource::TextTrack || type == Resource::Media || type == Resource::ImportResource;
}
#endif
inline RawResource* toRawResource(const ResourcePtr<Resource>& resource)
{
    ASSERT_WITH_SECURITY_IMPLICATION(!resource || isRawResource(*resource.get()));
    return static_cast<RawResource*>(resource.get());
}

class RawResourceClient : public ResourceClient {
public:
    virtual ~RawResourceClient() { }
    static ResourceClientType expectedType() { return RawResourceType; }
    virtual ResourceClientType resourceClientType() const override final { return expectedType(); }

    virtual void dataSent(Resource*, unsigned long long /* bytesSent */, unsigned long long /* totalBytesToBeSent */) { }
    virtual void responseReceived(Resource*, const ResourceResponse&, PassOwnPtr<WebDataConsumerHandle>) { }
    virtual void setSerializedCachedMetadata(Resource*, const char*, size_t) { }
    virtual void dataReceived(Resource*, const char* /* data */, unsigned /* length */) { }
    virtual void redirectReceived(Resource*, ResourceRequest&, const ResourceResponse&) { }
    virtual void updateRequest(Resource*, const ResourceRequest&) { }
    virtual void dataDownloaded(Resource*, int) { }
};

}

#endif // RawResource_h
