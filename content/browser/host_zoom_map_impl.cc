// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/host_zoom_map_impl.h"

#include <algorithm>
#include <cmath>

#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "content/browser/frame_host/navigation_entry_impl.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/page_zoom.h"
#include "content/public/common/url_constants.h"
#include "net/base/net_util.h"

namespace content {

namespace {

std::string GetHostFromProcessView(int render_process_id, int render_view_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  RenderViewHost* render_view_host =
      RenderViewHost::FromID(render_process_id, render_view_id);
  if (!render_view_host)
    return std::string();

  WebContents* web_contents = WebContents::FromRenderViewHost(render_view_host);

  NavigationEntry* entry =
      web_contents->GetController().GetLastCommittedEntry();
  if (!entry)
    return std::string();

  return net::GetHostOrSpecFromURL(HostZoomMap::GetURLFromEntry(entry));
}

}  // namespace

GURL HostZoomMap::GetURLFromEntry(const NavigationEntry* entry) {
  switch (entry->GetPageType()) {
    case PAGE_TYPE_ERROR:
      return GURL(kUnreachableWebDataURL);
    // TODO(wjmaclean): In future, give interstitial pages special treatment as
    // well.
    default:
      return entry->GetURL();
  }
}

HostZoomMap* HostZoomMap::GetDefaultForBrowserContext(BrowserContext* context) {
  StoragePartition* partition =
      BrowserContext::GetDefaultStoragePartition(context);
  DCHECK(partition);
  return partition->GetHostZoomMap();
}

HostZoomMap* HostZoomMap::Get(SiteInstance* instance) {
  StoragePartition* partition = BrowserContext::GetStoragePartition(
      instance->GetBrowserContext(), instance);
  DCHECK(partition);
  return partition->GetHostZoomMap();
}

HostZoomMap* HostZoomMap::GetForWebContents(const WebContents* contents) {
  StoragePartition* partition =
      BrowserContext::GetStoragePartition(contents->GetBrowserContext(),
                                          contents->GetSiteInstance());
  DCHECK(partition);
  return partition->GetHostZoomMap();
}

// Helper function for setting/getting zoom levels for WebContents without
// having to import HostZoomMapImpl everywhere.
double HostZoomMap::GetZoomLevel(const WebContents* web_contents) {
  HostZoomMapImpl* host_zoom_map = static_cast<HostZoomMapImpl*>(
      HostZoomMap::GetForWebContents(web_contents));
  return host_zoom_map->GetZoomLevelForWebContents(
      *static_cast<const WebContentsImpl*>(web_contents));
}

bool HostZoomMap::PageScaleFactorIsOne(const WebContents* web_contents) {
  HostZoomMapImpl* host_zoom_map = static_cast<HostZoomMapImpl*>(
      HostZoomMap::GetForWebContents(web_contents));
  return host_zoom_map->PageScaleFactorIsOneForWebContents(
      *static_cast<const WebContentsImpl*>(web_contents));
}

void HostZoomMap::SetZoomLevel(const WebContents* web_contents, double level) {
  HostZoomMapImpl* host_zoom_map = static_cast<HostZoomMapImpl*>(
      HostZoomMap::GetForWebContents(web_contents));
  host_zoom_map->SetZoomLevelForWebContents(
      *static_cast<const WebContentsImpl*>(web_contents), level);
}

void HostZoomMap::SendErrorPageZoomLevelRefresh(
    const WebContents* web_contents) {
  HostZoomMapImpl* host_zoom_map =
      static_cast<HostZoomMapImpl*>(HostZoomMap::GetDefaultForBrowserContext(
          web_contents->GetBrowserContext()));
  host_zoom_map->SendErrorPageZoomLevelRefresh();
}

HostZoomMapImpl::HostZoomMapImpl()
    : default_zoom_level_(0.0) {
  registrar_.Add(
      this, NOTIFICATION_RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW,
      NotificationService::AllSources());
}

void HostZoomMapImpl::CopyFrom(HostZoomMap* copy_interface) {
  // This can only be called on the UI thread to avoid deadlocks, otherwise
  //   UI: a.CopyFrom(b);
  //   IO: b.CopyFrom(a);
  // can deadlock.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  HostZoomMapImpl* copy = static_cast<HostZoomMapImpl*>(copy_interface);
  base::AutoLock auto_lock(lock_);
  base::AutoLock copy_auto_lock(copy->lock_);
  host_zoom_levels_.
      insert(copy->host_zoom_levels_.begin(), copy->host_zoom_levels_.end());
  for (SchemeHostZoomLevels::const_iterator i(copy->
           scheme_host_zoom_levels_.begin());
       i != copy->scheme_host_zoom_levels_.end(); ++i) {
    scheme_host_zoom_levels_[i->first] = HostZoomLevels();
    scheme_host_zoom_levels_[i->first].
        insert(i->second.begin(), i->second.end());
  }
  default_zoom_level_ = copy->default_zoom_level_;
}

double HostZoomMapImpl::GetZoomLevelForHost(const std::string& host) const {
  base::AutoLock auto_lock(lock_);
  return GetZoomLevelForHostInternal(host);
}

double HostZoomMapImpl::GetZoomLevelForHostInternal(
    const std::string& host) const {
  HostZoomLevels::const_iterator i(host_zoom_levels_.find(host));
  return (i == host_zoom_levels_.end()) ? default_zoom_level_ : i->second;
}

bool HostZoomMapImpl::HasZoomLevel(const std::string& scheme,
                                   const std::string& host) const {
  base::AutoLock auto_lock(lock_);

  SchemeHostZoomLevels::const_iterator scheme_iterator(
      scheme_host_zoom_levels_.find(scheme));

  const HostZoomLevels& zoom_levels =
      (scheme_iterator != scheme_host_zoom_levels_.end())
          ? scheme_iterator->second
          : host_zoom_levels_;

  HostZoomLevels::const_iterator i(zoom_levels.find(host));
  return i != zoom_levels.end();
}

double HostZoomMapImpl::GetZoomLevelForHostAndSchemeInternal(
    const std::string& scheme,
    const std::string& host) const {
  SchemeHostZoomLevels::const_iterator scheme_iterator(
      scheme_host_zoom_levels_.find(scheme));
  if (scheme_iterator != scheme_host_zoom_levels_.end()) {
    HostZoomLevels::const_iterator i(scheme_iterator->second.find(host));
    if (i != scheme_iterator->second.end())
      return i->second;
  }

  return GetZoomLevelForHostInternal(host);
}

double HostZoomMapImpl::GetZoomLevelForHostAndScheme(
    const std::string& scheme,
    const std::string& host) const {
  base::AutoLock auto_lock(lock_);
  return GetZoomLevelForHostAndSchemeInternal(scheme, host);
}

HostZoomMap::ZoomLevelVector HostZoomMapImpl::GetAllZoomLevels() const {
  HostZoomMap::ZoomLevelVector result;
  {
    base::AutoLock auto_lock(lock_);
    result.reserve(host_zoom_levels_.size() + scheme_host_zoom_levels_.size());
    for (HostZoomLevels::const_iterator i = host_zoom_levels_.begin();
         i != host_zoom_levels_.end();
         ++i) {
      ZoomLevelChange change = {HostZoomMap::ZOOM_CHANGED_FOR_HOST,
                                i->first,       // host
                                std::string(),  // scheme
                                i->second       // zoom level
      };
      result.push_back(change);
    }
    for (SchemeHostZoomLevels::const_iterator i =
             scheme_host_zoom_levels_.begin();
         i != scheme_host_zoom_levels_.end();
         ++i) {
      const std::string& scheme = i->first;
      const HostZoomLevels& host_zoom_levels = i->second;
      for (HostZoomLevels::const_iterator j = host_zoom_levels.begin();
           j != host_zoom_levels.end();
           ++j) {
        ZoomLevelChange change = {HostZoomMap::ZOOM_CHANGED_FOR_SCHEME_AND_HOST,
                                  j->first,  // host
                                  scheme,    // scheme
                                  j->second  // zoom level
        };
        result.push_back(change);
      }
    }
  }
  return result;
}

void HostZoomMapImpl::SetZoomLevelForHost(const std::string& host,
                                          double level) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  {
    base::AutoLock auto_lock(lock_);

    if (ZoomValuesEqual(level, default_zoom_level_))
      host_zoom_levels_.erase(host);
    else
      host_zoom_levels_[host] = level;
  }

  // TODO(wjmaclean) Should we use a GURL here? crbug.com/384486
  SendZoomLevelChange(std::string(), host, level);

  HostZoomMap::ZoomLevelChange change;
  change.mode = HostZoomMap::ZOOM_CHANGED_FOR_HOST;
  change.host = host;
  change.zoom_level = level;

  zoom_level_changed_callbacks_.Notify(change);
}

void HostZoomMapImpl::SetZoomLevelForHostAndScheme(const std::string& scheme,
                                                   const std::string& host,
                                                   double level) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  {
    base::AutoLock auto_lock(lock_);
    scheme_host_zoom_levels_[scheme][host] = level;
  }

  SendZoomLevelChange(scheme, host, level);

  HostZoomMap::ZoomLevelChange change;
  change.mode = HostZoomMap::ZOOM_CHANGED_FOR_SCHEME_AND_HOST;
  change.host = host;
  change.scheme = scheme;
  change.zoom_level = level;

  zoom_level_changed_callbacks_.Notify(change);
}

double HostZoomMapImpl::GetDefaultZoomLevel() const {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  return default_zoom_level_;
}

void HostZoomMapImpl::SetDefaultZoomLevel(double level) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  default_zoom_level_ = level;
}

scoped_ptr<HostZoomMap::Subscription>
HostZoomMapImpl::AddZoomLevelChangedCallback(
    const ZoomLevelChangedCallback& callback) {
  return zoom_level_changed_callbacks_.Add(callback);
}

double HostZoomMapImpl::GetZoomLevelForWebContents(
    const WebContentsImpl& web_contents_impl) const {
  int render_process_id = web_contents_impl.GetRenderProcessHost()->GetID();
  int routing_id = web_contents_impl.GetRenderViewHost()->GetRoutingID();

  if (UsesTemporaryZoomLevel(render_process_id, routing_id))
    return GetTemporaryZoomLevel(render_process_id, routing_id);

  // Get the url from the navigation controller directly, as calling
  // WebContentsImpl::GetLastCommittedURL() may give us a virtual url that
  // is different than is stored in the map.
  GURL url;
  NavigationEntry* entry =
      web_contents_impl.GetController().GetLastCommittedEntry();
  // It is possible for a WebContent's zoom level to be queried before
  // a navigation has occurred.
  if (entry)
    url = GetURLFromEntry(entry);
  return GetZoomLevelForHostAndScheme(url.scheme(),
                                      net::GetHostOrSpecFromURL(url));
}

void HostZoomMapImpl::SetZoomLevelForWebContents(
    const WebContentsImpl& web_contents_impl,
    double level) {
  int render_process_id = web_contents_impl.GetRenderProcessHost()->GetID();
  int render_view_id = web_contents_impl.GetRenderViewHost()->GetRoutingID();
  if (UsesTemporaryZoomLevel(render_process_id, render_view_id)) {
    SetTemporaryZoomLevel(render_process_id, render_view_id, level);
  } else {
    // Get the url from the navigation controller directly, as calling
    // WebContentsImpl::GetLastCommittedURL() may give us a virtual url that
    // is different than what the render view is using. If the two don't match,
    // the attempt to set the zoom will fail.
    NavigationEntry* entry =
        web_contents_impl.GetController().GetLastCommittedEntry();
    // Tests may invoke this function with a null entry, but we don't
    // want to save zoom levels in this case.
    if (!entry)
      return;

    GURL url = GetURLFromEntry(entry);
    SetZoomLevelForHost(net::GetHostOrSpecFromURL(url), level);
  }
}

void HostZoomMapImpl::SetZoomLevelForView(int render_process_id,
                                          int render_view_id,
                                          double level,
                                          const std::string& host) {
  if (UsesTemporaryZoomLevel(render_process_id, render_view_id))
    SetTemporaryZoomLevel(render_process_id, render_view_id, level);
  else
    SetZoomLevelForHost(host, level);
}

void HostZoomMapImpl::SetPageScaleFactorIsOneForView(int render_process_id,
                                                     int render_view_id,
                                                     bool is_one) {
  {
    base::AutoLock auto_lock(lock_);
    view_page_scale_factors_are_one_[RenderViewKey(render_process_id,
                                                   render_view_id)] = is_one;
  }
  HostZoomMap::ZoomLevelChange change;
  change.mode = HostZoomMap::PAGE_SCALE_IS_ONE_CHANGED;
  zoom_level_changed_callbacks_.Notify(change);
}

bool HostZoomMapImpl::PageScaleFactorIsOneForWebContents(
    const WebContentsImpl& web_contents_impl) const {
  if (!web_contents_impl.GetRenderProcessHost())
    return true;
  base::AutoLock auto_lock(lock_);
  auto found = view_page_scale_factors_are_one_.find(
      RenderViewKey(web_contents_impl.GetRenderProcessHost()->GetID(),
                    web_contents_impl.GetRoutingID()));
  if (found == view_page_scale_factors_are_one_.end())
    return true;
  return found->second;
}

void HostZoomMapImpl::ClearPageScaleFactorIsOneForView(int render_process_id,
                                                       int render_view_id) {
  base::AutoLock auto_lock(lock_);
  view_page_scale_factors_are_one_.erase(
      RenderViewKey(render_process_id, render_view_id));
}

bool HostZoomMapImpl::UsesTemporaryZoomLevel(int render_process_id,
                                             int render_view_id) const {
  RenderViewKey key(render_process_id, render_view_id);

  base::AutoLock auto_lock(lock_);
  return ContainsKey(temporary_zoom_levels_, key);
}

double HostZoomMapImpl::GetTemporaryZoomLevel(int render_process_id,
                                              int render_view_id) const {
  base::AutoLock auto_lock(lock_);
  RenderViewKey key(render_process_id, render_view_id);
  if (!ContainsKey(temporary_zoom_levels_, key))
    return 0;

  return temporary_zoom_levels_.find(key)->second;
}

void HostZoomMapImpl::SetTemporaryZoomLevel(int render_process_id,
                                            int render_view_id,
                                            double level) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  {
    RenderViewKey key(render_process_id, render_view_id);
    base::AutoLock auto_lock(lock_);
    temporary_zoom_levels_[key] = level;
  }

  RenderViewHost* host =
      RenderViewHost::FromID(render_process_id, render_view_id);
  host->Send(new ViewMsg_SetZoomLevelForView(render_view_id, true, level));

  HostZoomMap::ZoomLevelChange change;
  change.mode = HostZoomMap::ZOOM_CHANGED_TEMPORARY_ZOOM;
  change.host = GetHostFromProcessView(render_process_id, render_view_id);
  change.zoom_level = level;

  zoom_level_changed_callbacks_.Notify(change);
}

double HostZoomMapImpl::GetZoomLevelForView(const GURL& url,
                                            int render_process_id,
                                            int render_view_id) const {
  RenderViewKey key(render_process_id, render_view_id);
  base::AutoLock auto_lock(lock_);

  if (ContainsKey(temporary_zoom_levels_, key))
    return temporary_zoom_levels_.find(key)->second;

  return GetZoomLevelForHostAndSchemeInternal(url.scheme(),
                                              net::GetHostOrSpecFromURL(url));
}

void HostZoomMapImpl::Observe(int type,
                              const NotificationSource& source,
                              const NotificationDetails& details) {
  switch (type) {
    case NOTIFICATION_RENDER_VIEW_HOST_WILL_CLOSE_RENDER_VIEW: {
      int render_view_id = Source<RenderViewHost>(source)->GetRoutingID();
      int render_process_id =
          Source<RenderViewHost>(source)->GetProcess()->GetID();
      ClearTemporaryZoomLevel(render_process_id, render_view_id);
      ClearPageScaleFactorIsOneForView(render_process_id, render_view_id);
      break;
    }
    default:
      NOTREACHED() << "Unexpected preference observed.";
  }
}

void HostZoomMapImpl::ClearTemporaryZoomLevel(int render_process_id,
                                              int render_view_id) {
  {
    base::AutoLock auto_lock(lock_);
    RenderViewKey key(render_process_id, render_view_id);
    TemporaryZoomLevels::iterator it = temporary_zoom_levels_.find(key);
    if (it == temporary_zoom_levels_.end())
      return;
    temporary_zoom_levels_.erase(it);
  }
  RenderViewHost* host =
      RenderViewHost::FromID(render_process_id, render_view_id);
  DCHECK(host);
  // Send a new zoom level, host-specific if one exists.
  host->Send(new ViewMsg_SetZoomLevelForView(
      render_view_id,
      false,
      GetZoomLevelForHost(
          GetHostFromProcessView(render_process_id, render_view_id))));
}

void HostZoomMapImpl::SendZoomLevelChange(const std::string& scheme,
                                          const std::string& host,
                                          double level) {
  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    RenderProcessHost* render_process_host = i.GetCurrentValue();
    // TODO(wjmaclean) This will need to be cleaned up when
    // RenderProcessHost::GetStoragePartition() goes away. Perhaps have
    // RenderProcessHost expose a GetHostZoomMap() function?
    if (render_process_host->GetStoragePartition()->GetHostZoomMap() == this) {
      render_process_host->Send(
          new ViewMsg_SetZoomLevelForCurrentURL(scheme, host, level));
    }
  }
}

void HostZoomMapImpl::SendErrorPageZoomLevelRefresh() {
  GURL error_url(kUnreachableWebDataURL);
  std::string host = net::GetHostOrSpecFromURL(error_url);
  double error_page_zoom_level = GetZoomLevelForHost(host);

  SendZoomLevelChange(std::string(), host, error_page_zoom_level);
}

HostZoomMapImpl::~HostZoomMapImpl() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

}  // namespace content
