// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_HOST_H_
#define EXTENSIONS_BROWSER_EXTENSION_HOST_H_

#include <set>
#include <string>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/timer/elapsed_timer.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/deferred_start_render_host.h"
#include "extensions/browser/extension_function_dispatcher.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/stack_frame.h"
#include "extensions/common/view_type.h"

class PrefsTabHelper;

namespace content {
class BrowserContext;
class RenderProcessHost;
class RenderWidgetHostView;
class SiteInstance;
}

namespace extensions {
class Extension;
class ExtensionHostDelegate;
class ExtensionHostObserver;
class ExtensionHostQueue;
class WindowController;

// This class is the browser component of an extension component's RenderView.
// It handles setting up the renderer process, if needed, with special
// privileges available to extensions.  It may have a view to be shown in the
// browser UI, or it may be hidden.
//
// If you are adding code that only affects visible extension views (and not
// invisible background pages) you should add it to ExtensionViewHost.
class ExtensionHost : public DeferredStartRenderHost,
                      public content::WebContentsDelegate,
                      public content::WebContentsObserver,
                      public ExtensionFunctionDispatcher::Delegate,
                      public ExtensionRegistryObserver {
 public:
  ExtensionHost(const Extension* extension,
                content::SiteInstance* site_instance,
                const GURL& url, ViewType host_type);
  ~ExtensionHost() override;

  const Extension* extension() const { return extension_; }
  const std::string& extension_id() const { return extension_id_; }
  content::WebContents* host_contents() const { return host_contents_.get(); }
  content::RenderViewHost* render_view_host() const;
  content::RenderProcessHost* render_process_host() const;
  bool has_loaded_once() const { return has_loaded_once_; }
  bool document_element_available() const {
    return document_element_available_;
  }

  content::BrowserContext* browser_context() { return browser_context_; }

  ViewType extension_host_type() const { return extension_host_type_; }
  const GURL& GetURL() const;

  // Returns true if the render view is initialized and didn't crash.
  bool IsRenderViewLive() const;

  // Prepares to initializes our RenderViewHost by creating its RenderView and
  // navigating to this host's url. Uses host_view for the RenderViewHost's view
  // (can be NULL). This happens delayed to avoid locking the UI.
  void CreateRenderViewSoon();

  // Closes this host (results in [possibly asynchronous] deletion).
  void Close();

  // Typical observer interface.
  void AddObserver(ExtensionHostObserver* observer);
  void RemoveObserver(ExtensionHostObserver* observer);

  // Called when an event is dispatched to the event page associated with this
  // ExtensionHost.
  void OnBackgroundEventDispatched(const std::string& event_name, int event_id);

  // Called by the ProcessManager when a network request is started by the
  // extension corresponding to this ExtensionHost.
  void OnNetworkRequestStarted(uint64 request_id);

  // Called by the ProcessManager when a previously started network request is
  // finished.
  void OnNetworkRequestDone(uint64 request_id);

  // content::WebContentsObserver:
  bool OnMessageReceived(const IPC::Message& message) override;
  void RenderViewCreated(content::RenderViewHost* render_view_host) override;
  void RenderViewDeleted(content::RenderViewHost* render_view_host) override;
  void RenderViewReady() override;
  void RenderProcessGone(base::TerminationStatus status) override;
  void DocumentAvailableInMainFrame() override;
  void DidStartLoading(content::RenderViewHost* render_view_host) override;
  void DidStopLoading(content::RenderViewHost* render_view_host) override;

  // content::WebContentsDelegate:
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  void AddNewContents(content::WebContents* source,
                      content::WebContents* new_contents,
                      WindowOpenDisposition disposition,
                      const gfx::Rect& initial_rect,
                      bool user_gesture,
                      bool* was_blocked) override;
  void CloseContents(content::WebContents* contents) override;
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      const content::MediaResponseCallback& callback) override;
  bool CheckMediaAccessPermission(content::WebContents* web_contents,
                                  const GURL& security_origin,
                                  content::MediaStreamType type) override;
  bool IsNeverVisible(content::WebContents* web_contents) override;

  // ExtensionRegistryObserver:
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const Extension* extension,
                           UnloadedExtensionInfo::Reason reason) override;

 protected:
  // Called each time this ExtensionHost completes a load finishes loading,
  // before any stop-loading notifications or observer methods are called.
  virtual void OnDidStopLoading();

  // Navigates to the initial page.
  virtual void LoadInitialURL();

  // Returns true if we're hosting a background page.
  virtual bool IsBackgroundPage() const;

 private:
  // DeferredStartRenderHost:
  void CreateRenderViewNow() override;
  void AddDeferredStartRenderHostObserver(
      DeferredStartRenderHostObserver* observer) override;
  void RemoveDeferredStartRenderHostObserver(
      DeferredStartRenderHostObserver* observer) override;

  // Message handlers.
  void OnRequest(const ExtensionHostMsg_Request_Params& params);
  void OnEventAck(int event_id);
  void OnIncrementLazyKeepaliveCount();
  void OnDecrementLazyKeepaliveCount();

  // Records UMA for load events.
  void RecordStopLoadingUMA();

  // Delegate for functionality that cannot exist in the extensions module.
  scoped_ptr<ExtensionHostDelegate> delegate_;

  // The extension that we're hosting in this view.
  const Extension* extension_;

  // Id of extension that we're hosting in this view.
  const std::string extension_id_;

  // The browser context that this host is tied to.
  content::BrowserContext* browser_context_;

  // The host for our HTML content.
  scoped_ptr<content::WebContents> host_contents_;

  // A weak pointer to the current or pending RenderViewHost. We don't access
  // this through the host_contents because we want to deal with the pending
  // host, so we can send messages to it before it finishes loading.
  content::RenderViewHost* render_view_host_;

  // Whether the ExtensionHost has finished loading some content at least once.
  // There may be subsequent loads - such as reloads and navigations - and this
  // will not affect its value (it will remain true).
  bool has_loaded_once_;

  // True if the main frame has finished parsing.
  bool document_element_available_;

  // The original URL of the page being hosted.
  GURL initial_url_;

  // Messages sent out to the renderer that have not been acknowledged yet.
  std::set<int> unacked_messages_;

  ExtensionFunctionDispatcher extension_function_dispatcher_;

  // The type of view being hosted.
  ViewType extension_host_type_;

  // Measures how long since the initial URL started loading.
  scoped_ptr<base::ElapsedTimer> load_start_;

  ObserverList<ExtensionHostObserver> observer_list_;
  ObserverList<DeferredStartRenderHostObserver>
      deferred_start_render_host_observer_list_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionHost);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_HOST_H_
