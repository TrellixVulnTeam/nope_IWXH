// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/net_internals/net_internals_ui.h"

#include <algorithm>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_member.h"
#include "base/sequenced_task_runner_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/browser/net/connection_tester.h"
#include "chrome/browser/net/spdyproxy/data_reduction_proxy_chrome_settings.h"
#include "chrome/browser/net/spdyproxy/data_reduction_proxy_chrome_settings_factory.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/url_constants.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_network_delegate.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_service.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_statistics_prefs.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_event_store.h"
#include "components/onc/onc_constants.h"
#include "components/url_fixer/url_fixer.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "grit/net_internals_resources.h"
#include "net/base/net_errors.h"
#include "net/base/net_log_logger.h"
#include "net/base/net_log_util.h"
#include "net/base/net_util.h"
#include "net/disk_cache/disk_cache.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_cache.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_network_session.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_stream_factory.h"
#include "net/http/transport_security_state.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/file_manager/filesystem_api_util.h"
#include "chrome/browser/chromeos/net/onc_utils.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/system_logs/debug_log_writer.h"
#include "chrome/browser/net/nss_context.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/debug_daemon_client.h"
#include "chromeos/network/onc/onc_certificate_importer_impl.h"
#include "chromeos/network/onc/onc_utils.h"
#include "components/user_manager/user.h"
#endif

#if defined(OS_WIN)
#include "chrome/browser/net/service_providers_win.h"
#endif

#if defined(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/webui/extensions/extension_basic_info.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension_set.h"
#endif

using base::StringValue;
using content::BrowserThread;
using content::WebContents;
using content::WebUIMessageHandler;

namespace {

// Delay between when an event occurs and when it is passed to the Javascript
// page.  All events that occur during this period are grouped together and
// sent to the page at once, which reduces context switching and CPU usage.
const int kNetLogEventDelayMilliseconds = 100;

// Returns the HostCache for |context|'s primary HostResolver, or NULL if
// there is none.
net::HostCache* GetHostResolverCache(net::URLRequestContext* context) {
  return context->host_resolver()->GetHostCache();
}

std::string HashesToBase64String(const net::HashValueVector& hashes) {
  std::string str;
  for (size_t i = 0; i != hashes.size(); ++i) {
    if (i != 0)
      str += ",";
    str += hashes[i].ToString();
  }
  return str;
}

bool Base64StringToHashes(const std::string& hashes_str,
                          net::HashValueVector* hashes) {
  hashes->clear();
  std::vector<std::string> vector_hash_str;
  base::SplitString(hashes_str, ',', &vector_hash_str);

  for (size_t i = 0; i != vector_hash_str.size(); ++i) {
    std::string hash_str;
    base::RemoveChars(vector_hash_str[i], " \t\r\n", &hash_str);
    net::HashValue hash;
    // Skip past unrecognized hash algos
    // But return false on malformatted input
    if (hash_str.empty())
      return false;
    if (hash_str.compare(0, 5, "sha1/") != 0 &&
        hash_str.compare(0, 7, "sha256/") != 0) {
      continue;
    }
    if (!hash.FromString(hash_str))
      return false;
    hashes->push_back(hash);
  }
  return true;
}

// Returns the http network session for |context| if there is one.
// Otherwise, returns NULL.
net::HttpNetworkSession* GetHttpNetworkSession(
    net::URLRequestContext* context) {
  if (!context->http_transaction_factory())
    return NULL;

  return context->http_transaction_factory()->GetSession();
}

base::Value* ExperimentToValue(const ConnectionTester::Experiment& experiment) {
  base::DictionaryValue* dict = new base::DictionaryValue();

  if (experiment.url.is_valid())
    dict->SetString("url", experiment.url.spec());

  dict->SetString("proxy_settings_experiment",
                  ConnectionTester::ProxySettingsExperimentDescription(
                      experiment.proxy_settings_experiment));
  dict->SetString("host_resolver_experiment",
                  ConnectionTester::HostResolverExperimentDescription(
                      experiment.host_resolver_experiment));
  return dict;
}

content::WebUIDataSource* CreateNetInternalsHTMLSource() {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUINetInternalsHost);

  source->SetDefaultResource(IDR_NET_INTERNALS_INDEX_HTML);
  source->AddResourcePath("index.js", IDR_NET_INTERNALS_INDEX_JS);
  source->SetJsonPath("strings.js");
  return source;
}

// This class receives javascript messages from the renderer.
// Note that the WebUI infrastructure runs on the UI thread, therefore all of
// this class's methods are expected to run on the UI thread.
//
// Since the network code we want to run lives on the IO thread, we proxy
// almost everything over to NetInternalsMessageHandler::IOThreadImpl, which
// runs on the IO thread.
//
// TODO(eroman): Can we start on the IO thread to begin with?
class NetInternalsMessageHandler
    : public WebUIMessageHandler,
      public base::SupportsWeakPtr<NetInternalsMessageHandler> {
 public:
  NetInternalsMessageHandler();
  ~NetInternalsMessageHandler() override;

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;

  // Calls g_browser.receive in the renderer, passing in |command| and |arg|.
  // Takes ownership of |arg|.  If the renderer is displaying a log file, the
  // message will be ignored.
  void SendJavascriptCommand(const std::string& command, base::Value* arg);

  // Javascript message handlers.
  void OnRendererReady(const base::ListValue* list);
  void OnClearBrowserCache(const base::ListValue* list);
  void OnGetPrerenderInfo(const base::ListValue* list);
  void OnGetHistoricNetworkStats(const base::ListValue* list);
  void OnGetExtensionInfo(const base::ListValue* list);
  void OnGetDataReductionProxyInfo(const base::ListValue* list);
#if defined(OS_CHROMEOS)
  void OnImportONCFile(const base::ListValue* list);
  void OnStoreDebugLogs(const base::ListValue* list);
  void OnStoreDebugLogsCompleted(const base::FilePath& log_path,
                                 bool succeeded);
  void OnSetNetworkDebugMode(const base::ListValue* list);
  void OnSetNetworkDebugModeCompleted(const std::string& subsystem,
                                      bool succeeded);

  // Callback to |GetNSSCertDatabaseForProfile| used to retrieve the database
  // to which user's ONC defined certificates should be imported.
  // It parses and imports |onc_blob|.
  void ImportONCFileToNSSDB(const std::string& onc_blob,
                            const std::string& passcode,
                            net::NSSCertDatabase* nssdb);

  // Called back by the CertificateImporter when a certificate import finished.
  // |previous_error| contains earlier errors during this import.
  void OnCertificatesImported(
      const std::string& previous_error,
      bool success,
      const net::CertificateList& onc_trusted_certificates);
#endif

 private:
  class IOThreadImpl;

  // This is the "real" message handler, which lives on the IO thread.
  scoped_refptr<IOThreadImpl> proxy_;

  base::WeakPtr<prerender::PrerenderManager> prerender_manager_;

  DISALLOW_COPY_AND_ASSIGN(NetInternalsMessageHandler);
};

// This class is the "real" message handler. It is allocated and destroyed on
// the UI thread.  With the exception of OnAddEntry, OnWebUIDeleted, and
// SendJavascriptCommand, its methods are all expected to be called from the IO
// thread.  OnAddEntry and SendJavascriptCommand can be called from any thread,
// and OnWebUIDeleted can only be called from the UI thread.
class NetInternalsMessageHandler::IOThreadImpl
    : public base::RefCountedThreadSafe<
          NetInternalsMessageHandler::IOThreadImpl,
          BrowserThread::DeleteOnUIThread>,
      public net::NetLog::ThreadSafeObserver,
      public ConnectionTester::Delegate {
 public:
  // Type for methods that can be used as MessageHandler callbacks.
  typedef void (IOThreadImpl::*MessageHandler)(const base::ListValue*);

  // Creates a proxy for |handler| that will live on the IO thread.
  // |handler| is a weak pointer, since it is possible for the
  // WebUIMessageHandler to be deleted on the UI thread while we were executing
  // on the IO thread. |io_thread| is the global IOThread (it is passed in as
  // an argument since we need to grab it from the UI thread).
  IOThreadImpl(
      const base::WeakPtr<NetInternalsMessageHandler>& handler,
      IOThread* io_thread,
      net::URLRequestContextGetter* main_context_getter);

  // Called on UI thread just after creation, to add a ContextGetter to
  // |context_getters_|.
  void AddRequestContextGetter(net::URLRequestContextGetter* context_getter);

  // Helper method to enable a callback that will be executed on the IO thread.
  static void CallbackHelper(MessageHandler method,
                             scoped_refptr<IOThreadImpl> io_thread,
                             const base::ListValue* list);

  // Called once the WebUI has been deleted (i.e. renderer went away), on the
  // IO thread.
  void Detach();

  // Called when the WebUI is deleted.  Prevents calling Javascript functions
  // afterwards.  Called on UI thread.
  void OnWebUIDeleted();

  //--------------------------------
  // Javascript message handlers:
  //--------------------------------

  void OnRendererReady(const base::ListValue* list);

  void OnGetNetInfo(const base::ListValue* list);
  void OnReloadProxySettings(const base::ListValue* list);
  void OnClearBadProxies(const base::ListValue* list);
  void OnClearHostResolverCache(const base::ListValue* list);
  void OnEnableIPv6(const base::ListValue* list);
  void OnStartConnectionTests(const base::ListValue* list);
  void OnHSTSQuery(const base::ListValue* list);
  void OnHSTSAdd(const base::ListValue* list);
  void OnHSTSDelete(const base::ListValue* list);
  void OnGetSessionNetworkStats(const base::ListValue* list);
  void OnCloseIdleSockets(const base::ListValue* list);
  void OnFlushSocketPools(const base::ListValue* list);
#if defined(OS_WIN)
  void OnGetServiceProviders(const base::ListValue* list);
#endif
  void OnSetLogLevel(const base::ListValue* list);

  // ChromeNetLog::ThreadSafeObserver implementation:
  void OnAddEntry(const net::NetLog::Entry& entry) override;

  // ConnectionTester::Delegate implementation:
  void OnStartConnectionTestSuite() override;
  void OnStartConnectionTestExperiment(
      const ConnectionTester::Experiment& experiment) override;
  void OnCompletedConnectionTestExperiment(
      const ConnectionTester::Experiment& experiment,
      int result) override;
  void OnCompletedConnectionTestSuite() override;

  // Helper that calls g_browser.receive in the renderer, passing in |command|
  // and |arg|.  Takes ownership of |arg|.  If the renderer is displaying a log
  // file, the message will be ignored.  Note that this can be called from any
  // thread.
  void SendJavascriptCommand(const std::string& command, base::Value* arg);

 private:
  friend struct BrowserThread::DeleteOnThread<BrowserThread::UI>;
  friend class base::DeleteHelper<IOThreadImpl>;

  typedef std::list<scoped_refptr<net::URLRequestContextGetter> >
      ContextGetterList;

  ~IOThreadImpl() override;

  // Adds |entry| to the queue of pending log entries to be sent to the page via
  // Javascript.  Must be called on the IO Thread.  Also creates a delayed task
  // that will call PostPendingEntries, if there isn't one already.
  void AddEntryToQueue(base::Value* entry);

  // Sends all pending entries to the page via Javascript, and clears the list
  // of pending entries.  Sending multiple entries at once results in a
  // significant reduction of CPU usage when a lot of events are happening.
  // Must be called on the IO Thread.
  void PostPendingEntries();

  // Adds entries with the states of ongoing URL requests.
  void PrePopulateEventList();

  net::URLRequestContext* GetMainContext() {
    return main_context_getter_->GetURLRequestContext();
  }

  // |info_sources| is an or'd together list of the net::NetInfoSources to
  // send information about.  Information is sent to Javascript in the form of
  // a single dictionary with information about all requests sources.
  void SendNetInfo(int info_sources);

  // Pointer to the UI-thread message handler. Only access this from
  // the UI thread.
  base::WeakPtr<NetInternalsMessageHandler> handler_;

  // The global IOThread, which contains the global NetLog to observer.
  IOThread* io_thread_;

  // The main URLRequestContextGetter for the tab's profile.
  scoped_refptr<net::URLRequestContextGetter> main_context_getter_;

  // Helper that runs the suite of connection tests.
  scoped_ptr<ConnectionTester> connection_tester_;

  // True if the Web UI has been deleted.  This is used to prevent calling
  // Javascript functions after the Web UI is destroyed.  On refresh, the
  // messages can end up being sent to the refreshed page, causing duplicate
  // or partial entries.
  //
  // This is only read and written to on the UI thread.
  bool was_webui_deleted_;

  // Log entries that have yet to be passed along to Javascript page.  Non-NULL
  // when and only when there is a pending delayed task to call
  // PostPendingEntries.  Read and written to exclusively on the IO Thread.
  scoped_ptr<base::ListValue> pending_entries_;

  // Used for getting current status of URLRequests when net-internals is
  // opened.  |main_context_getter_| is automatically added on construction.
  // Duplicates are allowed.
  ContextGetterList context_getters_;

  DISALLOW_COPY_AND_ASSIGN(IOThreadImpl);
};

////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsMessageHandler
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsMessageHandler::NetInternalsMessageHandler() {}

NetInternalsMessageHandler::~NetInternalsMessageHandler() {
  if (proxy_.get()) {
    proxy_.get()->OnWebUIDeleted();
    // Notify the handler on the IO thread that the renderer is gone.
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            base::Bind(&IOThreadImpl::Detach, proxy_.get()));
  }
}

void NetInternalsMessageHandler::RegisterMessages() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  Profile* profile = Profile::FromWebUI(web_ui());

  proxy_ = new IOThreadImpl(this->AsWeakPtr(), g_browser_process->io_thread(),
                            profile->GetRequestContext());
  proxy_->AddRequestContextGetter(profile->GetMediaRequestContext());
#if defined(ENABLE_EXTENSIONS)
  proxy_->AddRequestContextGetter(profile->GetRequestContextForExtensions());
#endif

  prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(profile);
  if (prerender_manager) {
    prerender_manager_ = prerender_manager->AsWeakPtr();
  } else {
    prerender_manager_ = base::WeakPtr<prerender::PrerenderManager>();
  }

  web_ui()->RegisterMessageCallback(
      "notifyReady",
      base::Bind(&NetInternalsMessageHandler::OnRendererReady,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getNetInfo",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnGetNetInfo, proxy_));
  web_ui()->RegisterMessageCallback(
      "reloadProxySettings",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnReloadProxySettings, proxy_));
  web_ui()->RegisterMessageCallback(
      "clearBadProxies",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnClearBadProxies, proxy_));
  web_ui()->RegisterMessageCallback(
      "clearHostResolverCache",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnClearHostResolverCache, proxy_));
  web_ui()->RegisterMessageCallback(
      "enableIPv6",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnEnableIPv6, proxy_));
  web_ui()->RegisterMessageCallback(
      "startConnectionTests",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnStartConnectionTests, proxy_));
  web_ui()->RegisterMessageCallback(
      "hstsQuery",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnHSTSQuery, proxy_));
  web_ui()->RegisterMessageCallback(
      "hstsAdd",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnHSTSAdd, proxy_));
  web_ui()->RegisterMessageCallback(
      "hstsDelete",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnHSTSDelete, proxy_));
  web_ui()->RegisterMessageCallback(
      "getSessionNetworkStats",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnGetSessionNetworkStats, proxy_));
  web_ui()->RegisterMessageCallback(
      "closeIdleSockets",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnCloseIdleSockets, proxy_));
  web_ui()->RegisterMessageCallback(
      "flushSocketPools",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnFlushSocketPools, proxy_));
#if defined(OS_WIN)
  web_ui()->RegisterMessageCallback(
      "getServiceProviders",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnGetServiceProviders, proxy_));
#endif

  web_ui()->RegisterMessageCallback(
      "setLogLevel",
      base::Bind(&IOThreadImpl::CallbackHelper,
                 &IOThreadImpl::OnSetLogLevel, proxy_));
  web_ui()->RegisterMessageCallback(
      "clearBrowserCache",
      base::Bind(&NetInternalsMessageHandler::OnClearBrowserCache,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPrerenderInfo",
      base::Bind(&NetInternalsMessageHandler::OnGetPrerenderInfo,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getHistoricNetworkStats",
      base::Bind(&NetInternalsMessageHandler::OnGetHistoricNetworkStats,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getExtensionInfo",
      base::Bind(&NetInternalsMessageHandler::OnGetExtensionInfo,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDataReductionProxyInfo",
      base::Bind(&NetInternalsMessageHandler::OnGetDataReductionProxyInfo,
                 base::Unretained(this)));
#if defined(OS_CHROMEOS)
  web_ui()->RegisterMessageCallback(
      "importONCFile",
      base::Bind(&NetInternalsMessageHandler::OnImportONCFile,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "storeDebugLogs",
      base::Bind(&NetInternalsMessageHandler::OnStoreDebugLogs,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setNetworkDebugMode",
      base::Bind(&NetInternalsMessageHandler::OnSetNetworkDebugMode,
                 base::Unretained(this)));
#endif
}

void NetInternalsMessageHandler::SendJavascriptCommand(
    const std::string& command,
    base::Value* arg) {
  scoped_ptr<base::Value> command_value(new base::StringValue(command));
  scoped_ptr<base::Value> value(arg);
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (value.get()) {
    web_ui()->CallJavascriptFunction("g_browser.receive",
                                     *command_value.get(),
                                     *value.get());
  } else {
    web_ui()->CallJavascriptFunction("g_browser.receive",
                                     *command_value.get());
  }
}

void NetInternalsMessageHandler::OnRendererReady(const base::ListValue* list) {
  IOThreadImpl::CallbackHelper(&IOThreadImpl::OnRendererReady, proxy_, list);
}

void NetInternalsMessageHandler::OnClearBrowserCache(
    const base::ListValue* list) {
  BrowsingDataRemover* remover = BrowsingDataRemover::CreateForUnboundedRange(
      Profile::FromWebUI(web_ui()));
  remover->Remove(BrowsingDataRemover::REMOVE_CACHE,
                  BrowsingDataHelper::UNPROTECTED_WEB);
  // BrowsingDataRemover deletes itself.
}

void NetInternalsMessageHandler::OnGetPrerenderInfo(
    const base::ListValue* list) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  base::DictionaryValue* value = NULL;
  prerender::PrerenderManager* prerender_manager = prerender_manager_.get();
  if (!prerender_manager) {
    value = new base::DictionaryValue();
    value->SetBoolean("enabled", false);
    value->SetBoolean("omnibox_enabled", false);
  } else {
    value = prerender_manager->GetAsValue();
  }
  SendJavascriptCommand("receivedPrerenderInfo", value);
}

void NetInternalsMessageHandler::OnGetHistoricNetworkStats(
    const base::ListValue* list) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  base::Value* historic_network_info = NULL;
  Profile* profile = Profile::FromWebUI(web_ui());
  DataReductionProxyChromeSettings* data_reduction_proxy_settings =
        DataReductionProxyChromeSettingsFactory::GetForBrowserContext(profile);
  if (data_reduction_proxy_settings) {
    data_reduction_proxy::DataReductionProxyStatisticsPrefs* statistics_prefs =
        data_reduction_proxy_settings->data_reduction_proxy_service()->
        statistics_prefs();
    historic_network_info = statistics_prefs->HistoricNetworkStatsInfoToValue();
  }
  SendJavascriptCommand("receivedHistoricNetworkStats", historic_network_info);
}

void NetInternalsMessageHandler::OnGetExtensionInfo(
    const base::ListValue* list) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  base::ListValue* extension_list = new base::ListValue();
#if defined(ENABLE_EXTENSIONS)
  Profile* profile = Profile::FromWebUI(web_ui());
  extensions::ExtensionSystem* extension_system =
      extensions::ExtensionSystem::Get(profile);
  if (extension_system) {
    ExtensionService* extension_service = extension_system->extension_service();
    if (extension_service) {
      scoped_ptr<const extensions::ExtensionSet> extensions(
          extensions::ExtensionRegistry::Get(profile)
              ->GenerateInstalledExtensionsSet());
      for (extensions::ExtensionSet::const_iterator it = extensions->begin();
           it != extensions->end(); ++it) {
        base::DictionaryValue* extension_info = new base::DictionaryValue();
        bool enabled = extension_service->IsExtensionEnabled((*it)->id());
        extensions::GetExtensionBasicInfo(it->get(), enabled, extension_info);
        extension_list->Append(extension_info);
      }
    }
  }
#endif
  SendJavascriptCommand("receivedExtensionInfo", extension_list);
}

void NetInternalsMessageHandler::OnGetDataReductionProxyInfo(
    const base::ListValue* list) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  Profile* profile = Profile::FromWebUI(web_ui());
  DataReductionProxyChromeSettings* data_reduction_proxy_settings =
      DataReductionProxyChromeSettingsFactory::GetForBrowserContext(profile);
  data_reduction_proxy::DataReductionProxyEventStore* event_store =
      (data_reduction_proxy_settings == nullptr) ? nullptr :
          data_reduction_proxy_settings->GetEventStore();
  SendJavascriptCommand(
      "receivedDataReductionProxyInfo",
      (event_store == nullptr) ? nullptr : event_store->GetSummaryValue());
}

////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsMessageHandler::IOThreadImpl
//
////////////////////////////////////////////////////////////////////////////////

NetInternalsMessageHandler::IOThreadImpl::IOThreadImpl(
    const base::WeakPtr<NetInternalsMessageHandler>& handler,
    IOThread* io_thread,
    net::URLRequestContextGetter* main_context_getter)
    : handler_(handler),
      io_thread_(io_thread),
      main_context_getter_(main_context_getter),
      was_webui_deleted_(false) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  AddRequestContextGetter(main_context_getter);
}

NetInternalsMessageHandler::IOThreadImpl::~IOThreadImpl() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

void NetInternalsMessageHandler::IOThreadImpl::AddRequestContextGetter(
    net::URLRequestContextGetter* context_getter) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  context_getters_.push_back(context_getter);
}

void NetInternalsMessageHandler::IOThreadImpl::CallbackHelper(
    MessageHandler method,
    scoped_refptr<IOThreadImpl> io_thread,
    const base::ListValue* list) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // We need to make a copy of the value in order to pass it over to the IO
  // thread. |list_copy| will be deleted when the task is destroyed. The called
  // |method| cannot take ownership of |list_copy|.
  base::ListValue* list_copy =
      (list && list->GetSize()) ? list->DeepCopy() : NULL;

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(method, io_thread, base::Owned(list_copy)));
}

void NetInternalsMessageHandler::IOThreadImpl::Detach() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  // Unregister with network stack to observe events.
  if (net_log())
    net_log()->RemoveThreadSafeObserver(this);

  // Cancel any in-progress connection tests.
  connection_tester_.reset();
}

void NetInternalsMessageHandler::IOThreadImpl::OnWebUIDeleted() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  was_webui_deleted_ = true;
}

void NetInternalsMessageHandler::IOThreadImpl::OnRendererReady(
    const base::ListValue* list) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // If currently watching the NetLog, temporarily stop watching it and flush
  // pending events, so they won't appear before the status events created for
  // currently active network objects below.
  if (net_log()) {
    net_log()->RemoveThreadSafeObserver(this);
    PostPendingEntries();
  }

  SendJavascriptCommand("receivedConstants", NetInternalsUI::GetConstants());

  PrePopulateEventList();

  // Register with network stack to observe events.
  io_thread_->net_log()->AddThreadSafeObserver(this,
      net::NetLog::LOG_ALL_BUT_BYTES);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetNetInfo(
    const base::ListValue* list) {
  DCHECK(list);
  int info_sources;
  if (!list->GetInteger(0, &info_sources))
    return;
  SendNetInfo(info_sources);
}

void NetInternalsMessageHandler::IOThreadImpl::OnReloadProxySettings(
    const base::ListValue* list) {
  DCHECK(!list);
  GetMainContext()->proxy_service()->ForceReloadProxyConfig();

  // Cause the renderer to be notified of the new values.
  SendNetInfo(net::NET_INFO_PROXY_SETTINGS);
}

void NetInternalsMessageHandler::IOThreadImpl::OnClearBadProxies(
    const base::ListValue* list) {
  DCHECK(!list);
  GetMainContext()->proxy_service()->ClearBadProxiesCache();

  // Cause the renderer to be notified of the new values.
  SendNetInfo(net::NET_INFO_BAD_PROXIES);
}

void NetInternalsMessageHandler::IOThreadImpl::OnClearHostResolverCache(
    const base::ListValue* list) {
  DCHECK(!list);
  net::HostCache* cache = GetHostResolverCache(GetMainContext());

  if (cache)
    cache->clear();

  // Cause the renderer to be notified of the new values.
  SendNetInfo(net::NET_INFO_HOST_RESOLVER);
}

void NetInternalsMessageHandler::IOThreadImpl::OnEnableIPv6(
    const base::ListValue* list) {
  DCHECK(!list);
  net::HostResolver* host_resolver = GetMainContext()->host_resolver();

  host_resolver->SetDefaultAddressFamily(net::ADDRESS_FAMILY_UNSPECIFIED);

  // Cause the renderer to be notified of the new value.
  SendNetInfo(net::NET_INFO_HOST_RESOLVER);
}

void NetInternalsMessageHandler::IOThreadImpl::OnStartConnectionTests(
    const base::ListValue* list) {
  // |value| should be: [<URL to test>].
  base::string16 url_str;
  CHECK(list->GetString(0, &url_str));

  // Try to fix-up the user provided URL into something valid.
  // For example, turn "www.google.com" into "http://www.google.com".
  GURL url(url_fixer::FixupURL(base::UTF16ToUTF8(url_str), std::string()));

  connection_tester_.reset(new ConnectionTester(
      this,
      io_thread_->globals()->proxy_script_fetcher_context.get(),
      net_log()));
  connection_tester_->RunAllTests(url);
}

void NetInternalsMessageHandler::IOThreadImpl::OnHSTSQuery(
    const base::ListValue* list) {
  // |list| should be: [<domain to query>].
  std::string domain;
  CHECK(list->GetString(0, &domain));
  base::DictionaryValue* result = new base::DictionaryValue();

  if (!base::IsStringASCII(domain)) {
    result->SetString("error", "non-ASCII domain name");
  } else {
    net::TransportSecurityState* transport_security_state =
        GetMainContext()->transport_security_state();
    if (!transport_security_state) {
      result->SetString("error", "no TransportSecurityState active");
    } else {
      net::TransportSecurityState::DomainState static_state;
      const bool found_static = transport_security_state->GetStaticDomainState(
          domain, &static_state);
      if (found_static) {
        result->SetBoolean("has_static_sts",
                           found_static && static_state.ShouldUpgradeToSSL());
        result->SetInteger("static_upgrade_mode",
                           static_cast<int>(static_state.sts.upgrade_mode));
        result->SetBoolean("static_sts_include_subdomains",
                           static_state.sts.include_subdomains);
        result->SetDouble("static_sts_observed",
                          static_state.sts.last_observed.ToDoubleT());
        result->SetDouble("static_sts_expiry",
                          static_state.sts.expiry.ToDoubleT());
        result->SetBoolean("has_static_pkp",
                           found_static && static_state.HasPublicKeyPins());
        result->SetBoolean("static_pkp_include_subdomains",
                           static_state.pkp.include_subdomains);
        result->SetDouble("static_pkp_observed",
                          static_state.pkp.last_observed.ToDoubleT());
        result->SetDouble("static_pkp_expiry",
                          static_state.pkp.expiry.ToDoubleT());
        result->SetString("static_spki_hashes",
                          HashesToBase64String(static_state.pkp.spki_hashes));
        result->SetString("static_sts_domain", static_state.sts.domain);
        result->SetString("static_pkp_domain", static_state.pkp.domain);
      }

      net::TransportSecurityState::DomainState dynamic_state;
      const bool found_dynamic =
          transport_security_state->GetDynamicDomainState(domain,
                                                          &dynamic_state);
      if (found_dynamic) {
        result->SetInteger("dynamic_upgrade_mode",
                           static_cast<int>(dynamic_state.sts.upgrade_mode));
        result->SetBoolean("dynamic_sts_include_subdomains",
                           dynamic_state.sts.include_subdomains);
        result->SetBoolean("dynamic_pkp_include_subdomains",
                           dynamic_state.pkp.include_subdomains);
        result->SetDouble("dynamic_sts_observed",
                          dynamic_state.sts.last_observed.ToDoubleT());
        result->SetDouble("dynamic_pkp_observed",
                          dynamic_state.pkp.last_observed.ToDoubleT());
        result->SetDouble("dynamic_sts_expiry",
                          dynamic_state.sts.expiry.ToDoubleT());
        result->SetDouble("dynamic_pkp_expiry",
                          dynamic_state.pkp.expiry.ToDoubleT());
        result->SetString("dynamic_spki_hashes",
                          HashesToBase64String(dynamic_state.pkp.spki_hashes));
        result->SetString("dynamic_sts_domain", dynamic_state.sts.domain);
        result->SetString("dynamic_pkp_domain", dynamic_state.pkp.domain);
      }

      result->SetBoolean("result", found_static || found_dynamic);
    }
  }

  SendJavascriptCommand("receivedHSTSResult", result);
}

void NetInternalsMessageHandler::IOThreadImpl::OnHSTSAdd(
    const base::ListValue* list) {
  // |list| should be: [<domain to query>, <STS include subdomains>, <PKP
  // include subdomains>, <key pins>].
  std::string domain;
  CHECK(list->GetString(0, &domain));
  if (!base::IsStringASCII(domain)) {
    // Silently fail. The user will get a helpful error if they query for the
    // name.
    return;
  }
  bool sts_include_subdomains;
  CHECK(list->GetBoolean(1, &sts_include_subdomains));
  bool pkp_include_subdomains;
  CHECK(list->GetBoolean(2, &pkp_include_subdomains));
  std::string hashes_str;
  CHECK(list->GetString(3, &hashes_str));

  net::TransportSecurityState* transport_security_state =
      GetMainContext()->transport_security_state();
  if (!transport_security_state)
    return;

  base::Time expiry = base::Time::Now() + base::TimeDelta::FromDays(1000);
  net::HashValueVector hashes;
  if (!hashes_str.empty()) {
    if (!Base64StringToHashes(hashes_str, &hashes))
      return;
  }

  transport_security_state->AddHSTS(domain, expiry, sts_include_subdomains);
  transport_security_state->AddHPKP(domain, expiry, pkp_include_subdomains,
                                    hashes);
}

void NetInternalsMessageHandler::IOThreadImpl::OnHSTSDelete(
    const base::ListValue* list) {
  // |list| should be: [<domain to query>].
  std::string domain;
  CHECK(list->GetString(0, &domain));
  if (!base::IsStringASCII(domain)) {
    // There cannot be a unicode entry in the HSTS set.
    return;
  }
  net::TransportSecurityState* transport_security_state =
      GetMainContext()->transport_security_state();
  if (!transport_security_state)
    return;

  transport_security_state->DeleteDynamicDataForHost(domain);
}

void NetInternalsMessageHandler::IOThreadImpl::OnGetSessionNetworkStats(
    const base::ListValue* list) {
  DCHECK(!list);
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(main_context_getter_->GetURLRequestContext());

  base::Value* network_info = NULL;
  if (http_network_session) {
    data_reduction_proxy::DataReductionProxyNetworkDelegate* net_delegate =
        static_cast<data_reduction_proxy::DataReductionProxyNetworkDelegate*>(
            http_network_session->network_delegate());
    if (net_delegate) {
      network_info = net_delegate->SessionNetworkStatsInfoToValue();
    }
  }
  SendJavascriptCommand("receivedSessionNetworkStats", network_info);
}

void NetInternalsMessageHandler::IOThreadImpl::OnFlushSocketPools(
    const base::ListValue* list) {
  DCHECK(!list);
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(GetMainContext());

  if (http_network_session)
    http_network_session->CloseAllConnections();
}

void NetInternalsMessageHandler::IOThreadImpl::OnCloseIdleSockets(
    const base::ListValue* list) {
  DCHECK(!list);
  net::HttpNetworkSession* http_network_session =
      GetHttpNetworkSession(GetMainContext());

  if (http_network_session)
    http_network_session->CloseIdleConnections();
}

#if defined(OS_WIN)
void NetInternalsMessageHandler::IOThreadImpl::OnGetServiceProviders(
    const base::ListValue* list) {
  DCHECK(!list);

  base::DictionaryValue* service_providers = new base::DictionaryValue();

  WinsockLayeredServiceProviderList layered_providers;
  GetWinsockLayeredServiceProviders(&layered_providers);
  base::ListValue* layered_provider_list = new base::ListValue();
  for (size_t i = 0; i < layered_providers.size(); ++i) {
    base::DictionaryValue* service_dict = new base::DictionaryValue();
    service_dict->SetString("name", layered_providers[i].name);
    service_dict->SetInteger("version", layered_providers[i].version);
    service_dict->SetInteger("chain_length", layered_providers[i].chain_length);
    service_dict->SetInteger("socket_type", layered_providers[i].socket_type);
    service_dict->SetInteger("socket_protocol",
        layered_providers[i].socket_protocol);
    service_dict->SetString("path", layered_providers[i].path);

    layered_provider_list->Append(service_dict);
  }
  service_providers->Set("service_providers", layered_provider_list);

  WinsockNamespaceProviderList namespace_providers;
  GetWinsockNamespaceProviders(&namespace_providers);
  base::ListValue* namespace_list = new base::ListValue;
  for (size_t i = 0; i < namespace_providers.size(); ++i) {
    base::DictionaryValue* namespace_dict = new base::DictionaryValue();
    namespace_dict->SetString("name", namespace_providers[i].name);
    namespace_dict->SetBoolean("active", namespace_providers[i].active);
    namespace_dict->SetInteger("version", namespace_providers[i].version);
    namespace_dict->SetInteger("type", namespace_providers[i].type);

    namespace_list->Append(namespace_dict);
  }
  service_providers->Set("namespace_providers", namespace_list);

  SendJavascriptCommand("receivedServiceProviders", service_providers);
}
#endif

#if defined(OS_CHROMEOS)
void NetInternalsMessageHandler::ImportONCFileToNSSDB(
    const std::string& onc_blob,
    const std::string& passcode,
    net::NSSCertDatabase* nssdb) {
  const user_manager::User* user =
      chromeos::ProfileHelper::Get()->GetUserByProfile(
          Profile::FromWebUI(web_ui()));

  if (!user) {
    std::string error = "User not found.";
    SendJavascriptCommand("receivedONCFileParse", new base::StringValue(error));
    return;
  }

  std::string error;
  onc::ONCSource onc_source = onc::ONC_SOURCE_USER_IMPORT;
  base::ListValue network_configs;
  base::DictionaryValue global_network_config;
  base::ListValue certificates;
  if (!chromeos::onc::ParseAndValidateOncForImport(onc_blob,
                                                   onc_source,
                                                   passcode,
                                                   &network_configs,
                                                   &global_network_config,
                                                   &certificates)) {
    error = "Errors occurred during the ONC parsing. ";
  }

  std::string network_error;
  chromeos::onc::ImportNetworksForUser(user, network_configs, &network_error);
  if (!network_error.empty())
    error += network_error;

  chromeos::onc::CertificateImporterImpl cert_importer(
      BrowserThread::GetMessageLoopProxyForThread(BrowserThread::IO), nssdb);
  cert_importer.ImportCertificates(
      certificates,
      onc_source,
      base::Bind(&NetInternalsMessageHandler::OnCertificatesImported,
                 AsWeakPtr(),
                 error));
}

void NetInternalsMessageHandler::OnCertificatesImported(
    const std::string& previous_error,
    bool success,
    const net::CertificateList& /* unused onc_trusted_certificates */) {
  std::string error = previous_error;
  if (!success)
    error += "Some certificates couldn't be imported. ";

  SendJavascriptCommand("receivedONCFileParse", new base::StringValue(error));
}

void NetInternalsMessageHandler::OnImportONCFile(
    const base::ListValue* list) {
  std::string onc_blob;
  std::string passcode;
  if (list->GetSize() != 2 ||
      !list->GetString(0, &onc_blob) ||
      !list->GetString(1, &passcode)) {
    NOTREACHED();
  }

  GetNSSCertDatabaseForProfile(
      Profile::FromWebUI(web_ui()),
      base::Bind(&NetInternalsMessageHandler::ImportONCFileToNSSDB, AsWeakPtr(),
                 onc_blob, passcode));
}

void NetInternalsMessageHandler::OnStoreDebugLogs(const base::ListValue* list) {
  DCHECK(list);

  SendJavascriptCommand("receivedStoreDebugLogs",
                        new base::StringValue("Creating log file..."));
  Profile* profile = Profile::FromWebUI(web_ui());
  const DownloadPrefs* const prefs = DownloadPrefs::FromBrowserContext(profile);
  base::FilePath path = prefs->DownloadPath();
  if (file_manager::util::IsUnderNonNativeLocalPath(profile, path))
    path = prefs->GetDefaultDownloadDirectoryForProfile();
  chromeos::DebugLogWriter::StoreLogs(
      path,
      true,  // should_compress
      base::Bind(&NetInternalsMessageHandler::OnStoreDebugLogsCompleted,
                 AsWeakPtr()));
}

void NetInternalsMessageHandler::OnStoreDebugLogsCompleted(
    const base::FilePath& log_path, bool succeeded) {
  std::string status;
  if (succeeded)
    status = "Created log file: " + log_path.BaseName().AsUTF8Unsafe();
  else
    status = "Failed to create log file";
  SendJavascriptCommand("receivedStoreDebugLogs",
                        new base::StringValue(status));
}

void NetInternalsMessageHandler::OnSetNetworkDebugMode(
    const base::ListValue* list) {
  std::string subsystem;
  if (list->GetSize() != 1 || !list->GetString(0, &subsystem))
    NOTREACHED();
  chromeos::DBusThreadManager::Get()->GetDebugDaemonClient()->
      SetDebugMode(
          subsystem,
          base::Bind(
              &NetInternalsMessageHandler::OnSetNetworkDebugModeCompleted,
              AsWeakPtr(),
              subsystem));
}

void NetInternalsMessageHandler::OnSetNetworkDebugModeCompleted(
    const std::string& subsystem,
    bool succeeded) {
  std::string status;
  if (succeeded)
    status = "Debug mode is changed to " + subsystem;
  else
    status = "Failed to change debug mode to " + subsystem;
  SendJavascriptCommand("receivedSetNetworkDebugMode",
                        new base::StringValue(status));
}
#endif  // defined(OS_CHROMEOS)

void NetInternalsMessageHandler::IOThreadImpl::OnSetLogLevel(
    const base::ListValue* list) {
  int log_level;
  std::string log_level_string;
  if (!list->GetString(0, &log_level_string) ||
      !base::StringToInt(log_level_string, &log_level)) {
    NOTREACHED();
    return;
  }

  DCHECK_GE(log_level, net::NetLog::LOG_ALL);
  DCHECK_LT(log_level, net::NetLog::LOG_NONE);
  net_log()->SetObserverLogLevel(
      this, static_cast<net::NetLog::LogLevel>(log_level));
}

// Note that unlike other methods of IOThreadImpl, this function
// can be called from ANY THREAD.
void NetInternalsMessageHandler::IOThreadImpl::OnAddEntry(
    const net::NetLog::Entry& entry) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&IOThreadImpl::AddEntryToQueue, this, entry.ToValue()));
}

void NetInternalsMessageHandler::IOThreadImpl::OnStartConnectionTestSuite() {
  SendJavascriptCommand("receivedStartConnectionTestSuite", NULL);
}

void NetInternalsMessageHandler::IOThreadImpl::OnStartConnectionTestExperiment(
    const ConnectionTester::Experiment& experiment) {
  SendJavascriptCommand(
      "receivedStartConnectionTestExperiment",
      ExperimentToValue(experiment));
}

void
NetInternalsMessageHandler::IOThreadImpl::OnCompletedConnectionTestExperiment(
    const ConnectionTester::Experiment& experiment,
    int result) {
  base::DictionaryValue* dict = new base::DictionaryValue();

  dict->Set("experiment", ExperimentToValue(experiment));
  dict->SetInteger("result", result);

  SendJavascriptCommand(
      "receivedCompletedConnectionTestExperiment",
      dict);
}

void
NetInternalsMessageHandler::IOThreadImpl::OnCompletedConnectionTestSuite() {
  SendJavascriptCommand(
      "receivedCompletedConnectionTestSuite",
      NULL);
}

// Note that this can be called from ANY THREAD.
void NetInternalsMessageHandler::IOThreadImpl::SendJavascriptCommand(
    const std::string& command,
    base::Value* arg) {
  if (BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    if (handler_.get() && !was_webui_deleted_) {
      // We check |handler_| in case it was deleted on the UI thread earlier
      // while we were running on the IO thread.
      handler_->SendJavascriptCommand(command, arg);
    } else {
      delete arg;
    }
    return;
  }

  if (!BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&IOThreadImpl::SendJavascriptCommand, this, command, arg))) {
    // Failed posting the task, avoid leaking.
    delete arg;
  }
}

void NetInternalsMessageHandler::IOThreadImpl::AddEntryToQueue(
    base::Value* entry) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!pending_entries_.get()) {
    pending_entries_.reset(new base::ListValue());
    BrowserThread::PostDelayedTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&IOThreadImpl::PostPendingEntries, this),
        base::TimeDelta::FromMilliseconds(kNetLogEventDelayMilliseconds));
  }
  pending_entries_->Append(entry);
}

void NetInternalsMessageHandler::IOThreadImpl::PostPendingEntries() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (pending_entries_.get())
    SendJavascriptCommand("receivedLogEntries", pending_entries_.release());
}

void NetInternalsMessageHandler::IOThreadImpl::PrePopulateEventList() {
  // Using a set removes any duplicates.
  std::set<net::URLRequestContext*> contexts;
  for (ContextGetterList::const_iterator getter = context_getters_.begin();
       getter != context_getters_.end(); ++getter) {
    contexts.insert((*getter)->GetURLRequestContext());
  }
  contexts.insert(io_thread_->globals()->proxy_script_fetcher_context.get());
  contexts.insert(io_thread_->globals()->system_request_context.get());

  // Add entries for ongoing network objects.
  CreateNetLogEntriesForActiveObjects(contexts, this);
}

void NetInternalsMessageHandler::IOThreadImpl::SendNetInfo(int info_sources) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  SendJavascriptCommand(
      "receivedNetInfo",
      net::GetNetInfo(GetMainContext(), info_sources).release());
}

}  // namespace


////////////////////////////////////////////////////////////////////////////////
//
// NetInternalsUI
//
////////////////////////////////////////////////////////////////////////////////

// static
base::Value* NetInternalsUI::GetConstants() {
  scoped_ptr<base::DictionaryValue> constants_dict = net::GetNetConstants();
  DCHECK(constants_dict);

  // Add a dictionary with the version of the client and its command line
  // arguments.
  {
    base::DictionaryValue* dict = new base::DictionaryValue();

    chrome::VersionInfo version_info;

    // We have everything we need to send the right values.
    dict->SetString("name", version_info.Name());
    dict->SetString("version", version_info.Version());
    dict->SetString("cl", version_info.LastChange());
    dict->SetString("version_mod",
                    chrome::VersionInfo::GetVersionStringModifier());
    dict->SetString("official",
                    version_info.IsOfficialBuild() ? "official" : "unofficial");
    dict->SetString("os_type", version_info.OSType());
    dict->SetString(
        "command_line",
        base::CommandLine::ForCurrentProcess()->GetCommandLineString());

    constants_dict->Set("clientInfo", dict);

    data_reduction_proxy::DataReductionProxyEventStore::AddConstants(
        constants_dict.get());
  }

  return constants_dict.release();
}

NetInternalsUI::NetInternalsUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  web_ui->AddMessageHandler(new NetInternalsMessageHandler());

  // Set up the chrome://net-internals/ source.
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource::Add(profile, CreateNetInternalsHTMLSource());
}
