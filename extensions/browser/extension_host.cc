// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_host.h"

#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/profiler/scoped_tracker.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_error.h"
#include "extensions/browser/extension_host_delegate.h"
#include "extensions/browser/extension_host_observer.h"
#include "extensions/browser/extension_host_queue.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/load_monitoring_extension_host_queue.h"
#include "extensions/browser/notification_types.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/runtime_data.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"

using content::BrowserContext;
using content::OpenURLParams;
using content::RenderProcessHost;
using content::RenderViewHost;
using content::SiteInstance;
using content::WebContents;

namespace extensions {

ExtensionHost::ExtensionHost(const Extension* extension,
                             SiteInstance* site_instance,
                             const GURL& url,
                             ViewType host_type)
    : delegate_(ExtensionsBrowserClient::Get()->CreateExtensionHostDelegate()),
      extension_(extension),
      extension_id_(extension->id()),
      browser_context_(site_instance->GetBrowserContext()),
      render_view_host_(nullptr),
      has_loaded_once_(false),
      document_element_available_(false),
      initial_url_(url),
      extension_function_dispatcher_(browser_context_, this),
      extension_host_type_(host_type) {
  // Not used for panels, see PanelHost.
  DCHECK(host_type == VIEW_TYPE_EXTENSION_BACKGROUND_PAGE ||
         host_type == VIEW_TYPE_EXTENSION_DIALOG ||
         host_type == VIEW_TYPE_EXTENSION_POPUP);
  host_contents_.reset(WebContents::Create(
      WebContents::CreateParams(browser_context_, site_instance))),
  content::WebContentsObserver::Observe(host_contents_.get());
  host_contents_->SetDelegate(this);
  SetViewType(host_contents_.get(), host_type);

  render_view_host_ = host_contents_->GetRenderViewHost();

  // Listen for when an extension is unloaded from the same profile, as it may
  // be the same extension that this points to.
  ExtensionRegistry::Get(browser_context_)->AddObserver(this);

  // Set up web contents observers and pref observers.
  delegate_->OnExtensionHostCreated(host_contents());
}

ExtensionHost::~ExtensionHost() {
  ExtensionRegistry::Get(browser_context_)->RemoveObserver(this);

  if (extension_host_type_ == VIEW_TYPE_EXTENSION_BACKGROUND_PAGE &&
      extension_ && BackgroundInfo::HasLazyBackgroundPage(extension_) &&
      load_start_.get()) {
    UMA_HISTOGRAM_LONG_TIMES("Extensions.EventPageActiveTime2",
                             load_start_->Elapsed());
  }

  content::NotificationService::current()->Notify(
      extensions::NOTIFICATION_EXTENSION_HOST_DESTROYED,
      content::Source<BrowserContext>(browser_context_),
      content::Details<ExtensionHost>(this));
  FOR_EACH_OBSERVER(ExtensionHostObserver, observer_list_,
                    OnExtensionHostDestroyed(this));
  FOR_EACH_OBSERVER(DeferredStartRenderHostObserver,
                    deferred_start_render_host_observer_list_,
                    OnDeferredStartRenderHostDestroyed(this));

  // Remove ourselves from the queue as late as possible (before effectively
  // destroying self, but after everything else) so that queues that are
  // monitoring lifetime get a chance to see stop-loading events.
  delegate_->GetExtensionHostQueue()->Remove(this);

  // Deliberately stop observing |host_contents_| because its destruction
  // events (like DidStopLoading, it turns out) can call back into
  // ExtensionHost re-entrantly, when anything declared after |host_contents_|
  // has already been destroyed.
  content::WebContentsObserver::Observe(nullptr);
}

content::RenderProcessHost* ExtensionHost::render_process_host() const {
  return render_view_host()->GetProcess();
}

RenderViewHost* ExtensionHost::render_view_host() const {
  // TODO(mpcomplete): This can be null. How do we handle that?
  return render_view_host_;
}

bool ExtensionHost::IsRenderViewLive() const {
  return render_view_host()->IsRenderViewLive();
}

void ExtensionHost::CreateRenderViewSoon() {
  if (render_process_host() && render_process_host()->HasConnection()) {
    // If the process is already started, go ahead and initialize the RenderView
    // synchronously. The process creation is the real meaty part that we want
    // to defer.
    CreateRenderViewNow();
  } else {
    delegate_->GetExtensionHostQueue()->Add(this);
  }
}

void ExtensionHost::CreateRenderViewNow() {
  // TODO(robliao): Remove ScopedTracker below once crbug.com/464206 is fixed.
  tracked_objects::ScopedTracker tracking_profile1(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "464206 ExtensionHost::CreateRenderViewNow1"));
  LoadInitialURL();
  if (IsBackgroundPage()) {
    // TODO(robliao): Remove ScopedTracker below once crbug.com/464206 is fixed.
    tracked_objects::ScopedTracker tracking_profile2(
        FROM_HERE_WITH_EXPLICIT_FUNCTION(
            "464206 ExtensionHost::CreateRenderViewNow2"));
    DCHECK(IsRenderViewLive());
    if (extension_) {
      std::string group_name = base::FieldTrialList::FindFullName(
          "ThrottleExtensionBackgroundPages");
      if ((group_name == "ThrottlePersistent" &&
           extensions::BackgroundInfo::HasPersistentBackgroundPage(
               extension_)) ||
          group_name == "ThrottleAll") {
        host_contents_->WasHidden();
      }
    }
    // TODO(robliao): Remove ScopedTracker below once crbug.com/464206 is fixed.
    tracked_objects::ScopedTracker tracking_profile3(
        FROM_HERE_WITH_EXPLICIT_FUNCTION(
            "464206 ExtensionHost::CreateRenderViewNow3"));
    // Connect orphaned dev-tools instances.
    delegate_->OnRenderViewCreatedForBackgroundPage(this);
  }
}

void ExtensionHost::AddDeferredStartRenderHostObserver(
    DeferredStartRenderHostObserver* observer) {
  deferred_start_render_host_observer_list_.AddObserver(observer);
}

void ExtensionHost::RemoveDeferredStartRenderHostObserver(
    DeferredStartRenderHostObserver* observer) {
  deferred_start_render_host_observer_list_.RemoveObserver(observer);
}

void ExtensionHost::Close() {
  content::NotificationService::current()->Notify(
      extensions::NOTIFICATION_EXTENSION_HOST_VIEW_SHOULD_CLOSE,
      content::Source<BrowserContext>(browser_context_),
      content::Details<ExtensionHost>(this));
}

void ExtensionHost::AddObserver(ExtensionHostObserver* observer) {
  observer_list_.AddObserver(observer);
}

void ExtensionHost::RemoveObserver(ExtensionHostObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

void ExtensionHost::OnBackgroundEventDispatched(const std::string& event_name,
                                                int event_id) {
  CHECK(IsBackgroundPage());
  unacked_messages_.insert(event_id);
  FOR_EACH_OBSERVER(ExtensionHostObserver, observer_list_,
                    OnBackgroundEventDispatched(this, event_name, event_id));
}

void ExtensionHost::OnNetworkRequestStarted(uint64 request_id) {
  FOR_EACH_OBSERVER(ExtensionHostObserver, observer_list_,
                    OnNetworkRequestStarted(this, request_id));
}

void ExtensionHost::OnNetworkRequestDone(uint64 request_id) {
  FOR_EACH_OBSERVER(ExtensionHostObserver, observer_list_,
                    OnNetworkRequestDone(this, request_id));
}

const GURL& ExtensionHost::GetURL() const {
  return host_contents()->GetURL();
}

void ExtensionHost::LoadInitialURL() {
  load_start_.reset(new base::ElapsedTimer());
  host_contents_->GetController().LoadURL(
      initial_url_, content::Referrer(), ui::PAGE_TRANSITION_LINK,
      std::string());
}

bool ExtensionHost::IsBackgroundPage() const {
  DCHECK_EQ(extension_host_type_, VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
  return true;
}

void ExtensionHost::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const Extension* extension,
    UnloadedExtensionInfo::Reason reason) {
  // The extension object will be deleted after this notification has been sent.
  // Null it out so that dirty pointer issues don't arise in cases when multiple
  // ExtensionHost objects pointing to the same Extension are present.
  if (extension_ == extension) {
    extension_ = nullptr;
  }
}

void ExtensionHost::RenderProcessGone(base::TerminationStatus status) {
  // During browser shutdown, we may use sudden termination on an extension
  // process, so it is expected to lose our connection to the render view.
  // Do nothing.
  RenderProcessHost* process_host = host_contents_->GetRenderProcessHost();
  if (process_host && process_host->FastShutdownStarted())
    return;

  // In certain cases, multiple ExtensionHost objects may have pointed to
  // the same Extension at some point (one with a background page and a
  // popup, for example). When the first ExtensionHost goes away, the extension
  // is unloaded, and any other host that pointed to that extension will have
  // its pointer to it null'd out so that any attempt to unload a dirty pointer
  // will be averted.
  if (!extension_)
    return;

  // TODO(aa): This is suspicious. There can be multiple views in an extension,
  // and they aren't all going to use ExtensionHost. This should be in someplace
  // more central, like EPM maybe.
  content::NotificationService::current()->Notify(
      extensions::NOTIFICATION_EXTENSION_PROCESS_TERMINATED,
      content::Source<BrowserContext>(browser_context_),
      content::Details<ExtensionHost>(this));
}

void ExtensionHost::DidStartLoading(content::RenderViewHost* render_view_host) {
  FOR_EACH_OBSERVER(DeferredStartRenderHostObserver,
                    deferred_start_render_host_observer_list_,
                    OnDeferredStartRenderHostDidStartLoading(this));
}

void ExtensionHost::DidStopLoading(content::RenderViewHost* render_view_host) {
  // Only record UMA for the first load. Subsequent loads will likely behave
  // quite different, and it's first load we're most interested in.
  if (!has_loaded_once_)
    RecordStopLoadingUMA();
  has_loaded_once_ = true;
  OnDidStopLoading();
  content::NotificationService::current()->Notify(
      extensions::NOTIFICATION_EXTENSION_HOST_DID_STOP_LOADING,
      content::Source<BrowserContext>(browser_context_),
      content::Details<ExtensionHost>(this));
  FOR_EACH_OBSERVER(DeferredStartRenderHostObserver,
                    deferred_start_render_host_observer_list_,
                    OnDeferredStartRenderHostDidStopLoading(this));
}

void ExtensionHost::OnDidStopLoading() {
  DCHECK_EQ(extension_host_type_, VIEW_TYPE_EXTENSION_BACKGROUND_PAGE);
  // Nothing to do for background pages.
}

void ExtensionHost::DocumentAvailableInMainFrame() {
  // If the document has already been marked as available for this host, then
  // bail. No need for the redundant setup. http://crbug.com/31170
  if (document_element_available_)
    return;
  document_element_available_ = true;

  if (extension_host_type_ == VIEW_TYPE_EXTENSION_BACKGROUND_PAGE) {
    ExtensionSystem::Get(browser_context_)
        ->runtime_data()
        ->SetBackgroundPageReady(extension_->id(), true);
    content::NotificationService::current()->Notify(
        extensions::NOTIFICATION_EXTENSION_BACKGROUND_PAGE_READY,
        content::Source<const Extension>(extension_),
        content::NotificationService::NoDetails());
  }
}

void ExtensionHost::CloseContents(WebContents* contents) {
  Close();
}

bool ExtensionHost::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ExtensionHost, message)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_Request, OnRequest)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_EventAck, OnEventAck)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_IncrementLazyKeepaliveCount,
                        OnIncrementLazyKeepaliveCount)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_DecrementLazyKeepaliveCount,
                        OnDecrementLazyKeepaliveCount)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ExtensionHost::OnRequest(const ExtensionHostMsg_Request_Params& params) {
  extension_function_dispatcher_.Dispatch(params, render_view_host());
}

void ExtensionHost::OnEventAck(int event_id) {
  EventRouter* router = EventRouter::Get(browser_context_);
  if (router)
    router->OnEventAck(browser_context_, extension_id());

  // This should always be false since event acks are only sent by extensions
  // with lazy background pages but it doesn't hurt to be extra careful.
  if (!IsBackgroundPage()) {
    NOTREACHED() << "Received EventAck from extension " << extension_id()
                 << ", which does not have a lazy background page.";
    return;
  }

  // A compromised renderer could start sending out arbitrary event ids, which
  // may affect other renderers by causing downstream methods to think that
  // events for other extensions have been acked.  Make sure that the event id
  // sent by the renderer is one that this ExtensionHost expects to receive.
  // This way if a renderer _is_ compromised, it can really only affect itself.
  if (unacked_messages_.erase(event_id) > 0) {
    FOR_EACH_OBSERVER(ExtensionHostObserver, observer_list_,
                      OnBackgroundEventAcked(this, event_id));
  } else {
    // We have received an unexpected event id from the renderer.  It might be
    // compromised or it might have some other issue.  Kill it just to be safe.
    DCHECK(render_process_host());
    LOG(ERROR) << "Killing renderer for extension " << extension_id() << " for "
               << "sending an EventAck message with a bad event id.";
    render_process_host()->ReceivedBadMessage();
  }
}

void ExtensionHost::OnIncrementLazyKeepaliveCount() {
  ProcessManager::Get(browser_context_)
      ->IncrementLazyKeepaliveCount(extension());
}

void ExtensionHost::OnDecrementLazyKeepaliveCount() {
  ProcessManager::Get(browser_context_)
      ->DecrementLazyKeepaliveCount(extension());
}

// content::WebContentsObserver

void ExtensionHost::RenderViewCreated(RenderViewHost* render_view_host) {
  render_view_host_ = render_view_host;
}

void ExtensionHost::RenderViewDeleted(RenderViewHost* render_view_host) {
  // If our RenderViewHost is deleted, fall back to the host_contents' current
  // RVH. There is sometimes a small gap between the pending RVH being deleted
  // and RenderViewCreated being called, so we update it here.
  if (render_view_host == render_view_host_)
    render_view_host_ = host_contents_->GetRenderViewHost();
}

content::JavaScriptDialogManager* ExtensionHost::GetJavaScriptDialogManager(
    WebContents* source) {
  return delegate_->GetJavaScriptDialogManager();
}

void ExtensionHost::AddNewContents(WebContents* source,
                                   WebContents* new_contents,
                                   WindowOpenDisposition disposition,
                                   const gfx::Rect& initial_rect,
                                   bool user_gesture,
                                   bool* was_blocked) {
  // First, if the creating extension view was associated with a tab contents,
  // use that tab content's delegate. We must be careful here that the
  // associated tab contents has the same profile as the new tab contents. In
  // the case of extensions in 'spanning' incognito mode, they can mismatch.
  // We don't want to end up putting a normal tab into an incognito window, or
  // vice versa.
  // Note that we don't do this for popup windows, because we need to associate
  // those with their extension_app_id.
  if (disposition != NEW_POPUP) {
    WebContents* associated_contents = GetAssociatedWebContents();
    if (associated_contents &&
        associated_contents->GetBrowserContext() ==
            new_contents->GetBrowserContext()) {
      WebContentsDelegate* delegate = associated_contents->GetDelegate();
      if (delegate) {
        delegate->AddNewContents(
            associated_contents, new_contents, disposition, initial_rect,
            user_gesture, was_blocked);
        return;
      }
    }
  }

  delegate_->CreateTab(
      new_contents, extension_id_, disposition, initial_rect, user_gesture);
}

void ExtensionHost::RenderViewReady() {
  content::NotificationService::current()->Notify(
      extensions::NOTIFICATION_EXTENSION_HOST_CREATED,
      content::Source<BrowserContext>(browser_context_),
      content::Details<ExtensionHost>(this));
}

void ExtensionHost::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    const content::MediaResponseCallback& callback) {
  delegate_->ProcessMediaAccessRequest(
      web_contents, request, callback, extension());
}

bool ExtensionHost::CheckMediaAccessPermission(
    content::WebContents* web_contents,
    const GURL& security_origin,
    content::MediaStreamType type) {
  return delegate_->CheckMediaAccessPermission(
      web_contents, security_origin, type, extension());
}

bool ExtensionHost::IsNeverVisible(content::WebContents* web_contents) {
  ViewType view_type = extensions::GetViewType(web_contents);
  return view_type == extensions::VIEW_TYPE_EXTENSION_BACKGROUND_PAGE;
}

void ExtensionHost::RecordStopLoadingUMA() {
  CHECK(load_start_.get());
  if (extension_host_type_ == VIEW_TYPE_EXTENSION_BACKGROUND_PAGE) {
    if (extension_ && BackgroundInfo::HasLazyBackgroundPage(extension_)) {
      UMA_HISTOGRAM_MEDIUM_TIMES("Extensions.EventPageLoadTime2",
                                 load_start_->Elapsed());
    } else {
      UMA_HISTOGRAM_MEDIUM_TIMES("Extensions.BackgroundPageLoadTime2",
                                 load_start_->Elapsed());
    }
  } else if (extension_host_type_ == VIEW_TYPE_EXTENSION_POPUP) {
    UMA_HISTOGRAM_MEDIUM_TIMES("Extensions.PopupLoadTime2",
                               load_start_->Elapsed());
  }
}

}  // namespace extensions
