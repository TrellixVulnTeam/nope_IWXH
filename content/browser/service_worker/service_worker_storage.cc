// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_storage.h"

#include "base/bind_helpers.h"
#include "base/files/file_util.h"
#include "base/message_loop/message_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/task_runner_util.h"
#include "base/trace_event/trace_event.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_disk_cache.h"
#include "content/browser/service_worker/service_worker_info.h"
#include "content/browser/service_worker/service_worker_metrics.h"
#include "content/browser/service_worker/service_worker_registration.h"
#include "content/browser/service_worker/service_worker_utils.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/common/service_worker/service_worker_types.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/completion_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "storage/browser/quota/quota_manager_proxy.h"
#include "storage/browser/quota/special_storage_policy.h"

namespace content {

namespace {

void RunSoon(const tracked_objects::Location& from_here,
             const base::Closure& closure) {
  base::MessageLoop::current()->PostTask(from_here, closure);
}

void CompleteFindNow(
    const scoped_refptr<ServiceWorkerRegistration>& registration,
    ServiceWorkerStatusCode status,
    const ServiceWorkerStorage::FindRegistrationCallback& callback) {
  if (registration && registration->is_deleted()) {
    // It's past the point of no return and no longer findable.
    callback.Run(SERVICE_WORKER_ERROR_NOT_FOUND, nullptr);
    return;
  }
  callback.Run(status, registration);
}

void CompleteFindSoon(
    const tracked_objects::Location& from_here,
    const scoped_refptr<ServiceWorkerRegistration>& registration,
    ServiceWorkerStatusCode status,
    const ServiceWorkerStorage::FindRegistrationCallback& callback) {
  RunSoon(from_here,
          base::Bind(&CompleteFindNow, registration, status, callback));
}

const base::FilePath::CharType kDatabaseName[] =
    FILE_PATH_LITERAL("Database");
const base::FilePath::CharType kDiskCacheName[] =
    FILE_PATH_LITERAL("Cache");

const int kMaxMemDiskCacheSize = 10 * 1024 * 1024;
const int kMaxDiskCacheSize = 250 * 1024 * 1024;

ServiceWorkerStatusCode DatabaseStatusToStatusCode(
    ServiceWorkerDatabase::Status status) {
  switch (status) {
    case ServiceWorkerDatabase::STATUS_OK:
      return SERVICE_WORKER_OK;
    case ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND:
      return SERVICE_WORKER_ERROR_NOT_FOUND;
    case ServiceWorkerDatabase::STATUS_ERROR_MAX:
      NOTREACHED();
    default:
      return SERVICE_WORKER_ERROR_FAILED;
  }
}

class ResponseComparer : public base::RefCounted<ResponseComparer> {
 public:
  ResponseComparer(
      base::WeakPtr<ServiceWorkerStorage> owner,
      scoped_ptr<ServiceWorkerResponseReader> lhs,
      scoped_ptr<ServiceWorkerResponseReader> rhs,
      const ServiceWorkerStorage::CompareCallback& callback)
      : owner_(owner),
        completion_callback_(callback),
        lhs_reader_(lhs.release()),
        rhs_reader_(rhs.release()),
        completion_count_(0),
        previous_result_(0) {
  }

  void Start();

 private:
  friend class base::RefCounted<ResponseComparer>;

  static const int kBufferSize = 16 * 1024;

  ~ResponseComparer() {}
  void ReadInfos();
  void OnReadInfoComplete(int result);
  void ReadSomeData();
  void OnReadDataComplete(int result);

  base::WeakPtr<ServiceWorkerStorage> owner_;
  ServiceWorkerStorage::CompareCallback completion_callback_;
  scoped_ptr<ServiceWorkerResponseReader> lhs_reader_;
  scoped_refptr<HttpResponseInfoIOBuffer> lhs_info_;
  scoped_refptr<net::IOBuffer> lhs_buffer_;
  scoped_ptr<ServiceWorkerResponseReader> rhs_reader_;
  scoped_refptr<HttpResponseInfoIOBuffer> rhs_info_;
  scoped_refptr<net::IOBuffer> rhs_buffer_;
  int completion_count_;
  int previous_result_;
  DISALLOW_COPY_AND_ASSIGN(ResponseComparer);
};

void ResponseComparer::Start() {
  lhs_buffer_ = new net::IOBuffer(kBufferSize);
  lhs_info_ = new HttpResponseInfoIOBuffer();
  rhs_buffer_ = new net::IOBuffer(kBufferSize);
  rhs_info_ = new HttpResponseInfoIOBuffer();

  ReadInfos();
}

void ResponseComparer::ReadInfos() {
  lhs_reader_->ReadInfo(
      lhs_info_.get(), base::Bind(&ResponseComparer::OnReadInfoComplete, this));
  rhs_reader_->ReadInfo(
      rhs_info_.get(), base::Bind(&ResponseComparer::OnReadInfoComplete, this));
}

void ResponseComparer::OnReadInfoComplete(int result) {
  if (completion_callback_.is_null() || !owner_)
    return;
  if (result < 0) {
    completion_callback_.Run(SERVICE_WORKER_ERROR_FAILED, false);
    completion_callback_.Reset();
    return;
  }
  if (++completion_count_ != 2)
    return;

  if (lhs_info_->response_data_size != rhs_info_->response_data_size) {
    completion_callback_.Run(SERVICE_WORKER_OK, false);
    return;
  }
  ReadSomeData();
}

void ResponseComparer::ReadSomeData() {
  completion_count_ = 0;
  lhs_reader_->ReadData(
      lhs_buffer_.get(),
      kBufferSize,
      base::Bind(&ResponseComparer::OnReadDataComplete, this));
  rhs_reader_->ReadData(
      rhs_buffer_.get(),
      kBufferSize,
      base::Bind(&ResponseComparer::OnReadDataComplete, this));
}

void ResponseComparer::OnReadDataComplete(int result) {
  if (completion_callback_.is_null() || !owner_)
    return;
  if (result < 0) {
    completion_callback_.Run(SERVICE_WORKER_ERROR_FAILED, false);
    completion_callback_.Reset();
    return;
  }
  if (++completion_count_ != 2) {
    previous_result_ = result;
    return;
  }

  // TODO(michaeln): Probably shouldn't assume that the amounts read from
  // each reader will always be the same. This would wrongly signal false
  // in that case.
  if (result != previous_result_) {
    completion_callback_.Run(SERVICE_WORKER_OK, false);
    return;
  }

  if (result == 0) {
    completion_callback_.Run(SERVICE_WORKER_OK, true);
    return;
  }

  int compare_result =
      memcmp(lhs_buffer_->data(), rhs_buffer_->data(), result);
  if (compare_result != 0) {
    completion_callback_.Run(SERVICE_WORKER_OK, false);
    return;
  }

  ReadSomeData();
}

}  // namespace

ServiceWorkerStorage::InitialData::InitialData()
    : next_registration_id(kInvalidServiceWorkerRegistrationId),
      next_version_id(kInvalidServiceWorkerVersionId),
      next_resource_id(kInvalidServiceWorkerResourceId) {
}

ServiceWorkerStorage::InitialData::~InitialData() {
}

ServiceWorkerStorage::
DidDeleteRegistrationParams::DidDeleteRegistrationParams()
    : registration_id(kInvalidServiceWorkerRegistrationId) {
}

ServiceWorkerStorage::
DidDeleteRegistrationParams::~DidDeleteRegistrationParams() {
}

ServiceWorkerStorage::~ServiceWorkerStorage() {
  ClearSessionOnlyOrigins();
  weak_factory_.InvalidateWeakPtrs();
  database_task_manager_->GetTaskRunner()->DeleteSoon(FROM_HERE,
                                                      database_.release());
}

// static
scoped_ptr<ServiceWorkerStorage> ServiceWorkerStorage::Create(
    const base::FilePath& path,
    base::WeakPtr<ServiceWorkerContextCore> context,
    scoped_ptr<ServiceWorkerDatabaseTaskManager> database_task_manager,
    const scoped_refptr<base::SingleThreadTaskRunner>& disk_cache_thread,
    storage::QuotaManagerProxy* quota_manager_proxy,
    storage::SpecialStoragePolicy* special_storage_policy) {
  return make_scoped_ptr(new ServiceWorkerStorage(path,
                                                  context,
                                                  database_task_manager.Pass(),
                                                  disk_cache_thread,
                                                  quota_manager_proxy,
                                                  special_storage_policy));
}

// static
scoped_ptr<ServiceWorkerStorage> ServiceWorkerStorage::Create(
    base::WeakPtr<ServiceWorkerContextCore> context,
    ServiceWorkerStorage* old_storage) {
  return make_scoped_ptr(
      new ServiceWorkerStorage(old_storage->path_,
                               context,
                               old_storage->database_task_manager_->Clone(),
                               old_storage->disk_cache_thread_,
                               old_storage->quota_manager_proxy_.get(),
                               old_storage->special_storage_policy_.get()));
}

void ServiceWorkerStorage::FindRegistrationForDocument(
    const GURL& document_url,
    const FindRegistrationCallback& callback) {
  DCHECK(!document_url.has_ref());
  if (!LazyInitialize(base::Bind(
          &ServiceWorkerStorage::FindRegistrationForDocument,
          weak_factory_.GetWeakPtr(), document_url, callback))) {
    if (state_ != INITIALIZING || !context_) {
      CompleteFindNow(scoped_refptr<ServiceWorkerRegistration>(),
                      SERVICE_WORKER_ERROR_FAILED, callback);
    }
    TRACE_EVENT_INSTANT1(
        "ServiceWorker",
        "ServiceWorkerStorage::FindRegistrationForDocument:LazyInitialize",
        TRACE_EVENT_SCOPE_THREAD,
        "URL", document_url.spec());
    return;
  }
  DCHECK_EQ(INITIALIZED, state_);

  // See if there are any stored registrations for the origin.
  if (!ContainsKey(registered_origins_, document_url.GetOrigin())) {
    // Look for something currently being installed.
    scoped_refptr<ServiceWorkerRegistration> installing_registration =
        FindInstallingRegistrationForDocument(document_url);
    ServiceWorkerStatusCode status = installing_registration.get() ?
        SERVICE_WORKER_OK : SERVICE_WORKER_ERROR_NOT_FOUND;
    TRACE_EVENT_INSTANT2(
        "ServiceWorker",
        "ServiceWorkerStorage::FindRegistrationForDocument:CheckInstalling",
        TRACE_EVENT_SCOPE_THREAD,
        "URL", document_url.spec(),
        "Status", ServiceWorkerStatusToString(status));
    CompleteFindNow(installing_registration,
                    status,
                    callback);
    return;
  }

  // To connect this TRACE_EVENT with the callback, TimeTicks is used for
  // callback id.
  int64 callback_id = base::TimeTicks::Now().ToInternalValue();
  TRACE_EVENT_ASYNC_BEGIN1(
      "ServiceWorker",
      "ServiceWorkerStorage::FindRegistrationForDocument",
      callback_id,
      "URL", document_url.spec());
  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          &FindForDocumentInDB,
          database_.get(),
          base::MessageLoopProxy::current(),
          document_url,
          base::Bind(&ServiceWorkerStorage::DidFindRegistrationForDocument,
                     weak_factory_.GetWeakPtr(),
                     document_url,
                     callback,
                     callback_id)));
}

void ServiceWorkerStorage::FindRegistrationForPattern(
    const GURL& scope,
    const FindRegistrationCallback& callback) {
  if (!LazyInitialize(base::Bind(
          &ServiceWorkerStorage::FindRegistrationForPattern,
          weak_factory_.GetWeakPtr(), scope, callback))) {
    if (state_ != INITIALIZING || !context_) {
      CompleteFindSoon(FROM_HERE, scoped_refptr<ServiceWorkerRegistration>(),
                       SERVICE_WORKER_ERROR_FAILED, callback);
    }
    return;
  }
  DCHECK_EQ(INITIALIZED, state_);

  // See if there are any stored registrations for the origin.
  if (!ContainsKey(registered_origins_, scope.GetOrigin())) {
    // Look for something currently being installed.
    scoped_refptr<ServiceWorkerRegistration> installing_registration =
        FindInstallingRegistrationForPattern(scope);
    CompleteFindSoon(FROM_HERE,
                     installing_registration,
                     installing_registration.get()
                         ? SERVICE_WORKER_OK
                         : SERVICE_WORKER_ERROR_NOT_FOUND,
                     callback);
    return;
  }

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          &FindForPatternInDB,
          database_.get(),
          base::MessageLoopProxy::current(),
          scope,
          base::Bind(&ServiceWorkerStorage::DidFindRegistrationForPattern,
                     weak_factory_.GetWeakPtr(), scope, callback)));
}

ServiceWorkerRegistration* ServiceWorkerStorage::GetUninstallingRegistration(
    const GURL& scope) {
  if (state_ != INITIALIZED || !context_)
    return NULL;
  for (RegistrationRefsById::const_iterator it =
           uninstalling_registrations_.begin();
       it != uninstalling_registrations_.end();
       ++it) {
    if (it->second->pattern() == scope) {
      DCHECK(it->second->is_uninstalling());
      return it->second.get();
    }
  }
  return NULL;
}

void ServiceWorkerStorage::FindRegistrationForId(
    int64 registration_id,
    const GURL& origin,
    const FindRegistrationCallback& callback) {
  if (!LazyInitialize(base::Bind(
          &ServiceWorkerStorage::FindRegistrationForId,
          weak_factory_.GetWeakPtr(), registration_id, origin, callback))) {
    if (state_ != INITIALIZING || !context_) {
      CompleteFindNow(scoped_refptr<ServiceWorkerRegistration>(),
                      SERVICE_WORKER_ERROR_FAILED, callback);
    }
    return;
  }
  DCHECK_EQ(INITIALIZED, state_);

  // See if there are any stored registrations for the origin.
  if (!ContainsKey(registered_origins_, origin)) {
    // Look for something currently being installed.
    scoped_refptr<ServiceWorkerRegistration> installing_registration =
        FindInstallingRegistrationForId(registration_id);
    CompleteFindNow(installing_registration,
                    installing_registration.get()
                        ? SERVICE_WORKER_OK
                        : SERVICE_WORKER_ERROR_NOT_FOUND,
                    callback);
    return;
  }

  scoped_refptr<ServiceWorkerRegistration> registration =
      context_->GetLiveRegistration(registration_id);
  if (registration.get()) {
    CompleteFindNow(registration, SERVICE_WORKER_OK, callback);
    return;
  }

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&FindForIdInDB,
                 database_.get(),
                 base::MessageLoopProxy::current(),
                 registration_id, origin,
                 base::Bind(&ServiceWorkerStorage::DidFindRegistrationForId,
                            weak_factory_.GetWeakPtr(), callback)));
}

void ServiceWorkerStorage::FindRegistrationForIdOnly(
    int64 registration_id,
    const FindRegistrationCallback& callback) {
  if (!LazyInitialize(
          base::Bind(&ServiceWorkerStorage::FindRegistrationForIdOnly,
                     weak_factory_.GetWeakPtr(), registration_id, callback))) {
    if (state_ != INITIALIZING || !context_) {
      CompleteFindNow(nullptr, SERVICE_WORKER_ERROR_FAILED, callback);
    }
    return;
  }
  DCHECK_EQ(INITIALIZED, state_);

  scoped_refptr<ServiceWorkerRegistration> registration =
      context_->GetLiveRegistration(registration_id);
  if (registration) {
    // Delegate to FindRegistrationForId to make sure the same subset of live
    // registrations is returned.
    // TODO(mek): CompleteFindNow should really do all the required checks, so
    // calling that directly here should be enough.
    FindRegistrationForId(registration_id, registration->pattern().GetOrigin(),
                          callback);
    return;
  }

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&FindForIdOnlyInDB, database_.get(),
                 base::MessageLoopProxy::current(), registration_id,
                 base::Bind(&ServiceWorkerStorage::DidFindRegistrationForId,
                            weak_factory_.GetWeakPtr(), callback)));
}

void ServiceWorkerStorage::GetRegistrationsForOrigin(
    const GURL& origin, const GetRegistrationsInfosCallback& callback) {
  if (!LazyInitialize(base::Bind(
          &ServiceWorkerStorage::GetRegistrationsForOrigin,
          weak_factory_.GetWeakPtr(), origin, callback))) {
    if (state_ != INITIALIZING || !context_) {
      RunSoon(FROM_HERE, base::Bind(
          callback, std::vector<ServiceWorkerRegistrationInfo>()));
    }
    return;
  }
  DCHECK_EQ(INITIALIZED, state_);

  RegistrationList* registrations = new RegistrationList;
  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&ServiceWorkerDatabase::GetRegistrationsForOrigin,
                 base::Unretained(database_.get()),
                 origin,
                 base::Unretained(registrations)),
      base::Bind(&ServiceWorkerStorage::DidGetRegistrations,
                 weak_factory_.GetWeakPtr(),
                 callback,
                 base::Owned(registrations),
                 origin));
}

void ServiceWorkerStorage::GetAllRegistrations(
    const GetRegistrationsInfosCallback& callback) {
  if (!LazyInitialize(base::Bind(
          &ServiceWorkerStorage::GetAllRegistrations,
          weak_factory_.GetWeakPtr(), callback))) {
    if (state_ != INITIALIZING || !context_) {
      RunSoon(FROM_HERE, base::Bind(
          callback, std::vector<ServiceWorkerRegistrationInfo>()));
    }
    return;
  }
  DCHECK_EQ(INITIALIZED, state_);

  RegistrationList* registrations = new RegistrationList;
  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&ServiceWorkerDatabase::GetAllRegistrations,
                 base::Unretained(database_.get()),
                 base::Unretained(registrations)),
      base::Bind(&ServiceWorkerStorage::DidGetRegistrations,
                 weak_factory_.GetWeakPtr(),
                 callback,
                 base::Owned(registrations),
                 GURL()));
}

void ServiceWorkerStorage::StoreRegistration(
    ServiceWorkerRegistration* registration,
    ServiceWorkerVersion* version,
    const StatusCallback& callback) {
  DCHECK(registration);
  DCHECK(version);

  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  ServiceWorkerDatabase::RegistrationData data;
  data.registration_id = registration->id();
  data.scope = registration->pattern();
  data.script = version->script_url();
  data.has_fetch_handler = true;
  data.version_id = version->version_id();
  data.last_update_check = registration->last_update_check();
  data.is_active = (version == registration->active_version());

  ResourceList resources;
  version->script_cache_map()->GetResources(&resources);

  uint64 resources_total_size_bytes = 0;
  for (const auto& resource : resources) {
    resources_total_size_bytes += resource.size_bytes;
  }
  data.resources_total_size_bytes = resources_total_size_bytes;

  if (!has_checked_for_stale_resources_)
    DeleteStaleResources();

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&WriteRegistrationInDB,
                 database_.get(),
                 base::MessageLoopProxy::current(),
                 data,
                 resources,
                 base::Bind(&ServiceWorkerStorage::DidStoreRegistration,
                            weak_factory_.GetWeakPtr(),
                            callback,
                            data)));

  registration->set_is_deleted(false);
}

void ServiceWorkerStorage::UpdateToActiveState(
    ServiceWorkerRegistration* registration,
    const StatusCallback& callback) {
  DCHECK(registration);

  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&ServiceWorkerDatabase::UpdateVersionToActive,
                 base::Unretained(database_.get()),
                 registration->id(),
                 registration->pattern().GetOrigin()),
      base::Bind(&ServiceWorkerStorage::DidUpdateToActiveState,
                 weak_factory_.GetWeakPtr(),
                 callback));
}

void ServiceWorkerStorage::UpdateLastUpdateCheckTime(
    ServiceWorkerRegistration* registration) {
  DCHECK(registration);

  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_)
    return;

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          base::IgnoreResult(&ServiceWorkerDatabase::UpdateLastCheckTime),
          base::Unretained(database_.get()),
          registration->id(),
          registration->pattern().GetOrigin(),
          registration->last_update_check()));
}

void ServiceWorkerStorage::DeleteRegistration(
    int64 registration_id,
    const GURL& origin,
    const StatusCallback& callback) {
  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  if (!has_checked_for_stale_resources_)
    DeleteStaleResources();

  DidDeleteRegistrationParams params;
  params.registration_id = registration_id;
  params.origin = origin;
  params.callback = callback;

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&DeleteRegistrationFromDB,
                 database_.get(),
                 base::MessageLoopProxy::current(),
                 registration_id, origin,
                 base::Bind(&ServiceWorkerStorage::DidDeleteRegistration,
                            weak_factory_.GetWeakPtr(), params)));

  // The registration should no longer be findable.
  pending_deletions_.insert(registration_id);
  ServiceWorkerRegistration* registration =
      context_->GetLiveRegistration(registration_id);
  if (registration)
    registration->set_is_deleted(true);
}

scoped_ptr<ServiceWorkerResponseReader>
ServiceWorkerStorage::CreateResponseReader(int64 response_id) {
  return make_scoped_ptr(
      new ServiceWorkerResponseReader(response_id, disk_cache()));
}

scoped_ptr<ServiceWorkerResponseWriter>
ServiceWorkerStorage::CreateResponseWriter(int64 response_id) {
  return make_scoped_ptr(
      new ServiceWorkerResponseWriter(response_id, disk_cache()));
}

scoped_ptr<ServiceWorkerResponseMetadataWriter>
ServiceWorkerStorage::CreateResponseMetadataWriter(int64 response_id) {
  return make_scoped_ptr(
      new ServiceWorkerResponseMetadataWriter(response_id, disk_cache()));
}

void ServiceWorkerStorage::StoreUncommittedResponseId(int64 id) {
  DCHECK_NE(kInvalidServiceWorkerResponseId, id);
  DCHECK_EQ(INITIALIZED, state_);

  if (!has_checked_for_stale_resources_)
    DeleteStaleResources();

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(
          &ServiceWorkerDatabase::WriteUncommittedResourceIds),
          base::Unretained(database_.get()),
          std::set<int64>(&id, &id + 1)));
}

void ServiceWorkerStorage::DoomUncommittedResponse(int64 id) {
  DCHECK_NE(kInvalidServiceWorkerResponseId, id);
  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(
          &ServiceWorkerDatabase::PurgeUncommittedResourceIds),
          base::Unretained(database_.get()),
          std::set<int64>(&id, &id + 1)));
  StartPurgingResources(std::vector<int64>(1, id));
}

void ServiceWorkerStorage::CompareScriptResources(
    int64 lhs_id, int64 rhs_id,
    const CompareCallback& callback) {
  DCHECK(!callback.is_null());
  scoped_refptr<ResponseComparer> comparer =
      new ResponseComparer(weak_factory_.GetWeakPtr(),
                           CreateResponseReader(lhs_id),
                           CreateResponseReader(rhs_id),
                           callback);
  comparer->Start();  // It deletes itself when done.
}

void ServiceWorkerStorage::StoreUserData(
    int64 registration_id,
    const GURL& origin,
    const std::string& key,
    const std::string& data,
    const StatusCallback& callback) {
  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  if (registration_id == kInvalidServiceWorkerRegistrationId || key.empty()) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&ServiceWorkerDatabase::WriteUserData,
                 base::Unretained(database_.get()),
                 registration_id, origin, key, data),
      base::Bind(&ServiceWorkerStorage::DidStoreUserData,
                 weak_factory_.GetWeakPtr(),
                 callback));
}

void ServiceWorkerStorage::GetUserData(
    int64 registration_id,
    const std::string& key,
    const GetUserDataCallback& callback) {
  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE,
            base::Bind(callback, std::string(), SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  if (registration_id == kInvalidServiceWorkerRegistrationId || key.empty()) {
    RunSoon(FROM_HERE,
            base::Bind(callback, std::string(), SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&ServiceWorkerStorage::GetUserDataInDB,
                 database_.get(),
                 base::MessageLoopProxy::current(),
                 registration_id,
                 key,
                 base::Bind(&ServiceWorkerStorage::DidGetUserData,
                            weak_factory_.GetWeakPtr(), callback)));
}

void ServiceWorkerStorage::ClearUserData(
    int64 registration_id,
    const std::string& key,
    const StatusCallback& callback) {
  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  if (registration_id == kInvalidServiceWorkerRegistrationId || key.empty()) {
    RunSoon(FROM_HERE, base::Bind(callback, SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&ServiceWorkerDatabase::DeleteUserData,
                 base::Unretained(database_.get()),
                 registration_id, key),
      base::Bind(&ServiceWorkerStorage::DidDeleteUserData,
                 weak_factory_.GetWeakPtr(),
                 callback));
}

void ServiceWorkerStorage::GetUserDataForAllRegistrations(
    const std::string& key,
    const ServiceWorkerStorage::GetUserDataForAllRegistrationsCallback&
        callback) {
  DCHECK(state_ == INITIALIZED || state_ == DISABLED) << state_;
  if (IsDisabled() || !context_) {
    RunSoon(FROM_HERE,
            base::Bind(callback, std::vector<std::pair<int64, std::string>>(),
                       SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  if (key.empty()) {
    RunSoon(FROM_HERE,
            base::Bind(callback, std::vector<std::pair<int64, std::string>>(),
                       SERVICE_WORKER_ERROR_FAILED));
    return;
  }

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          &ServiceWorkerStorage::GetUserDataForAllRegistrationsInDB,
          database_.get(), base::MessageLoopProxy::current(), key,
          base::Bind(&ServiceWorkerStorage::DidGetUserDataForAllRegistrations,
                     weak_factory_.GetWeakPtr(), callback)));
}

void ServiceWorkerStorage::DeleteAndStartOver(const StatusCallback& callback) {
  Disable();

  // Delete the database on the database thread.
  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&ServiceWorkerDatabase::DestroyDatabase,
                 base::Unretained(database_.get())),
      base::Bind(&ServiceWorkerStorage::DidDeleteDatabase,
                 weak_factory_.GetWeakPtr(),
                 callback));
}

int64 ServiceWorkerStorage::NewRegistrationId() {
  if (state_ == DISABLED)
    return kInvalidServiceWorkerRegistrationId;
  DCHECK_EQ(INITIALIZED, state_);
  return next_registration_id_++;
}

int64 ServiceWorkerStorage::NewVersionId() {
  if (state_ == DISABLED)
    return kInvalidServiceWorkerVersionId;
  DCHECK_EQ(INITIALIZED, state_);
  return next_version_id_++;
}

int64 ServiceWorkerStorage::NewResourceId() {
  if (state_ == DISABLED)
    return kInvalidServiceWorkerResourceId;
  DCHECK_EQ(INITIALIZED, state_);
  return next_resource_id_++;
}

void ServiceWorkerStorage::NotifyInstallingRegistration(
      ServiceWorkerRegistration* registration) {
  DCHECK(installing_registrations_.find(registration->id()) ==
         installing_registrations_.end());
  installing_registrations_[registration->id()] = registration;
}

void ServiceWorkerStorage::NotifyDoneInstallingRegistration(
      ServiceWorkerRegistration* registration,
      ServiceWorkerVersion* version,
      ServiceWorkerStatusCode status) {
  installing_registrations_.erase(registration->id());
  if (status != SERVICE_WORKER_OK && version) {
    ResourceList resources;
    version->script_cache_map()->GetResources(&resources);

    std::set<int64> ids;
    for (size_t i = 0; i < resources.size(); ++i)
      ids.insert(resources[i].resource_id);

    database_task_manager_->GetTaskRunner()->PostTask(
        FROM_HERE,
        base::Bind(base::IgnoreResult(
            &ServiceWorkerDatabase::PurgeUncommittedResourceIds),
            base::Unretained(database_.get()),
            ids));
  }
}

void ServiceWorkerStorage::NotifyUninstallingRegistration(
    ServiceWorkerRegistration* registration) {
  DCHECK(uninstalling_registrations_.find(registration->id()) ==
         uninstalling_registrations_.end());
  uninstalling_registrations_[registration->id()] = registration;
}

void ServiceWorkerStorage::NotifyDoneUninstallingRegistration(
    ServiceWorkerRegistration* registration) {
  uninstalling_registrations_.erase(registration->id());
}

void ServiceWorkerStorage::Disable() {
  state_ = DISABLED;
  if (disk_cache_)
    disk_cache_->Disable();
}

bool ServiceWorkerStorage::IsDisabled() const {
  return state_ == DISABLED;
}

void ServiceWorkerStorage::PurgeResources(const ResourceList& resources) {
  if (!has_checked_for_stale_resources_)
    DeleteStaleResources();
  StartPurgingResources(resources);
}

ServiceWorkerStorage::ServiceWorkerStorage(
    const base::FilePath& path,
    base::WeakPtr<ServiceWorkerContextCore> context,
    scoped_ptr<ServiceWorkerDatabaseTaskManager> database_task_manager,
    const scoped_refptr<base::SingleThreadTaskRunner>& disk_cache_thread,
    storage::QuotaManagerProxy* quota_manager_proxy,
    storage::SpecialStoragePolicy* special_storage_policy)
    : next_registration_id_(kInvalidServiceWorkerRegistrationId),
      next_version_id_(kInvalidServiceWorkerVersionId),
      next_resource_id_(kInvalidServiceWorkerResourceId),
      state_(UNINITIALIZED),
      path_(path),
      context_(context),
      database_task_manager_(database_task_manager.Pass()),
      disk_cache_thread_(disk_cache_thread),
      quota_manager_proxy_(quota_manager_proxy),
      special_storage_policy_(special_storage_policy),
      is_purge_pending_(false),
      has_checked_for_stale_resources_(false),
      weak_factory_(this) {
  database_.reset(new ServiceWorkerDatabase(GetDatabasePath()));
}

base::FilePath ServiceWorkerStorage::GetDatabasePath() {
  if (path_.empty())
    return base::FilePath();
  return path_.Append(ServiceWorkerContextCore::kServiceWorkerDirectory)
      .Append(kDatabaseName);
}

base::FilePath ServiceWorkerStorage::GetDiskCachePath() {
  if (path_.empty())
    return base::FilePath();
  return path_.Append(ServiceWorkerContextCore::kServiceWorkerDirectory)
      .Append(kDiskCacheName);
}

bool ServiceWorkerStorage::LazyInitialize(const base::Closure& callback) {
  if (!context_)
    return false;

  switch (state_) {
    case INITIALIZED:
      return true;
    case DISABLED:
      return false;
    case INITIALIZING:
      pending_tasks_.push_back(callback);
      return false;
    case UNINITIALIZED:
      pending_tasks_.push_back(callback);
      // Fall-through.
  }

  state_ = INITIALIZING;
  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&ReadInitialDataFromDB,
                 database_.get(),
                 base::MessageLoopProxy::current(),
                 base::Bind(&ServiceWorkerStorage::DidReadInitialData,
                            weak_factory_.GetWeakPtr())));
  return false;
}

void ServiceWorkerStorage::DidReadInitialData(
    InitialData* data,
    ServiceWorkerDatabase::Status status) {
  DCHECK(data);
  DCHECK_EQ(INITIALIZING, state_);

  if (status == ServiceWorkerDatabase::STATUS_OK) {
    next_registration_id_ = data->next_registration_id;
    next_version_id_ = data->next_version_id;
    next_resource_id_ = data->next_resource_id;
    registered_origins_.swap(data->origins);
    state_ = INITIALIZED;
  } else {
    DVLOG(2) << "Failed to initialize: "
             << ServiceWorkerDatabase::StatusToString(status);
    ScheduleDeleteAndStartOver();
  }

  for (std::vector<base::Closure>::const_iterator it = pending_tasks_.begin();
       it != pending_tasks_.end(); ++it) {
    RunSoon(FROM_HERE, *it);
  }
  pending_tasks_.clear();
}

void ServiceWorkerStorage::DidFindRegistrationForDocument(
    const GURL& document_url,
    const FindRegistrationCallback& callback,
    int64 callback_id,
    const ServiceWorkerDatabase::RegistrationData& data,
    const ResourceList& resources,
    ServiceWorkerDatabase::Status status) {
  if (status == ServiceWorkerDatabase::STATUS_OK) {
    ReturnFoundRegistration(callback, data, resources);
    TRACE_EVENT_ASYNC_END1(
        "ServiceWorker",
        "ServiceWorkerStorage::FindRegistrationForDocument",
        callback_id,
        "Status", ServiceWorkerDatabase::StatusToString(status));
    return;
  }

  if (status == ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    // Look for something currently being installed.
    scoped_refptr<ServiceWorkerRegistration> installing_registration =
        FindInstallingRegistrationForDocument(document_url);
    ServiceWorkerStatusCode installing_status = installing_registration.get() ?
        SERVICE_WORKER_OK : SERVICE_WORKER_ERROR_NOT_FOUND;
    callback.Run(installing_status, installing_registration);
    TRACE_EVENT_ASYNC_END2(
        "ServiceWorker",
        "ServiceWorkerStorage::FindRegistrationForDocument",
        callback_id,
        "Status", ServiceWorkerDatabase::StatusToString(status),
        "Info",
        (installing_status == SERVICE_WORKER_OK) ?
            "Installing registration is found" :
            "Any registrations are not found");
    return;
  }

  ScheduleDeleteAndStartOver();
  callback.Run(DatabaseStatusToStatusCode(status),
               scoped_refptr<ServiceWorkerRegistration>());
  TRACE_EVENT_ASYNC_END1(
      "ServiceWorker",
      "ServiceWorkerStorage::FindRegistrationForDocument",
      callback_id,
      "Status", ServiceWorkerDatabase::StatusToString(status));
}

void ServiceWorkerStorage::DidFindRegistrationForPattern(
    const GURL& scope,
    const FindRegistrationCallback& callback,
    const ServiceWorkerDatabase::RegistrationData& data,
    const ResourceList& resources,
    ServiceWorkerDatabase::Status status) {
  if (status == ServiceWorkerDatabase::STATUS_OK) {
    ReturnFoundRegistration(callback, data, resources);
    return;
  }

  if (status == ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    scoped_refptr<ServiceWorkerRegistration> installing_registration =
        FindInstallingRegistrationForPattern(scope);
    callback.Run(installing_registration.get() ? SERVICE_WORKER_OK
                                               : SERVICE_WORKER_ERROR_NOT_FOUND,
                 installing_registration);
    return;
  }

  ScheduleDeleteAndStartOver();
  callback.Run(DatabaseStatusToStatusCode(status),
               scoped_refptr<ServiceWorkerRegistration>());
}

void ServiceWorkerStorage::DidFindRegistrationForId(
    const FindRegistrationCallback& callback,
    const ServiceWorkerDatabase::RegistrationData& data,
    const ResourceList& resources,
    ServiceWorkerDatabase::Status status) {
  if (status == ServiceWorkerDatabase::STATUS_OK) {
    ReturnFoundRegistration(callback, data, resources);
    return;
  }

  if (status == ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    // TODO(nhiroki): Find a registration in |installing_registrations_|.
    callback.Run(DatabaseStatusToStatusCode(status),
                 scoped_refptr<ServiceWorkerRegistration>());
    return;
  }

  ScheduleDeleteAndStartOver();
  callback.Run(DatabaseStatusToStatusCode(status),
               scoped_refptr<ServiceWorkerRegistration>());
}

void ServiceWorkerStorage::ReturnFoundRegistration(
    const FindRegistrationCallback& callback,
    const ServiceWorkerDatabase::RegistrationData& data,
    const ResourceList& resources) {
  scoped_refptr<ServiceWorkerRegistration> registration =
      GetOrCreateRegistration(data, resources);
  CompleteFindNow(registration, SERVICE_WORKER_OK, callback);
}

void ServiceWorkerStorage::DidGetRegistrations(
    const GetRegistrationsInfosCallback& callback,
    RegistrationList* registrations,
    const GURL& origin_filter,
    ServiceWorkerDatabase::Status status) {
  DCHECK(registrations);
  if (status != ServiceWorkerDatabase::STATUS_OK &&
      status != ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    ScheduleDeleteAndStartOver();
    callback.Run(std::vector<ServiceWorkerRegistrationInfo>());
    return;
  }

  // Add all stored registrations.
  std::set<int64> pushed_registrations;
  std::vector<ServiceWorkerRegistrationInfo> infos;
  for (const auto& registration_data : *registrations) {
    const bool inserted =
        pushed_registrations.insert(registration_data.registration_id).second;
    DCHECK(inserted);

    ServiceWorkerRegistration* registration =
        context_->GetLiveRegistration(registration_data.registration_id);
    if (registration) {
      infos.push_back(registration->GetInfo());
      continue;
    }

    ServiceWorkerRegistrationInfo info;
    info.pattern = registration_data.scope;
    info.registration_id = registration_data.registration_id;
    info.stored_version_size_bytes =
        registration_data.resources_total_size_bytes;
    if (ServiceWorkerVersion* version =
            context_->GetLiveVersion(registration_data.version_id)) {
      if (registration_data.is_active)
        info.active_version = version->GetInfo();
      else
        info.waiting_version = version->GetInfo();
      infos.push_back(info);
      continue;
    }

    if (registration_data.is_active) {
      info.active_version.status = ServiceWorkerVersion::ACTIVATED;
      info.active_version.script_url = registration_data.script;
      info.active_version.version_id = registration_data.version_id;
    } else {
      info.waiting_version.status = ServiceWorkerVersion::INSTALLED;
      info.waiting_version.script_url = registration_data.script;
      info.waiting_version.version_id = registration_data.version_id;
    }
    infos.push_back(info);
  }

  // Add unstored registrations that are being installed.
  for (RegistrationRefsById::const_iterator it =
           installing_registrations_.begin();
       it != installing_registrations_.end(); ++it) {
    if ((!origin_filter.is_valid() ||
         it->second->pattern().GetOrigin() == origin_filter) &&
        pushed_registrations.insert(it->first).second) {
      infos.push_back(it->second->GetInfo());
    }
  }

  callback.Run(infos);
}

void ServiceWorkerStorage::DidStoreRegistration(
    const StatusCallback& callback,
    const ServiceWorkerDatabase::RegistrationData& new_version,
    const GURL& origin,
    const ServiceWorkerDatabase::RegistrationData& deleted_version,
    const std::vector<int64>& newly_purgeable_resources,
    ServiceWorkerDatabase::Status status) {
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    ScheduleDeleteAndStartOver();
    callback.Run(DatabaseStatusToStatusCode(status));
    return;
  }
  registered_origins_.insert(origin);

  scoped_refptr<ServiceWorkerRegistration> registration =
      context_->GetLiveRegistration(new_version.registration_id);
  registration->set_resources_total_size_bytes(
      new_version.resources_total_size_bytes);
  if (quota_manager_proxy_.get()) {
    // Can be nullptr in tests.
    quota_manager_proxy_->NotifyStorageModified(
        storage::QuotaClient::kServiceWorker,
        origin,
        storage::StorageType::kStorageTypeTemporary,
        new_version.resources_total_size_bytes -
            deleted_version.resources_total_size_bytes);
  }

  callback.Run(SERVICE_WORKER_OK);

  if (!context_ || !context_->GetLiveVersion(deleted_version.version_id))
    StartPurgingResources(newly_purgeable_resources);
}

void ServiceWorkerStorage::DidUpdateToActiveState(
    const StatusCallback& callback,
    ServiceWorkerDatabase::Status status) {
  if (status != ServiceWorkerDatabase::STATUS_OK &&
      status != ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    ScheduleDeleteAndStartOver();
  }
  callback.Run(DatabaseStatusToStatusCode(status));
}

void ServiceWorkerStorage::DidDeleteRegistration(
    const DidDeleteRegistrationParams& params,
    bool origin_is_deletable,
    const ServiceWorkerDatabase::RegistrationData& deleted_version,
    const std::vector<int64>& newly_purgeable_resources,
    ServiceWorkerDatabase::Status status) {
  pending_deletions_.erase(params.registration_id);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    ScheduleDeleteAndStartOver();
    params.callback.Run(DatabaseStatusToStatusCode(status));
    return;
  }
  if (quota_manager_proxy_.get()) {
    // Can be nullptr in tests.
    quota_manager_proxy_->NotifyStorageModified(
        storage::QuotaClient::kServiceWorker,
        params.origin,
        storage::StorageType::kStorageTypeTemporary,
        -deleted_version.resources_total_size_bytes);
  }
  if (origin_is_deletable)
    registered_origins_.erase(params.origin);
  params.callback.Run(SERVICE_WORKER_OK);

  if (!context_ || !context_->GetLiveVersion(deleted_version.version_id))
    StartPurgingResources(newly_purgeable_resources);
}

void ServiceWorkerStorage::DidStoreUserData(
    const StatusCallback& callback,
    ServiceWorkerDatabase::Status status) {
  // |status| can be NOT_FOUND when the associated registration did not exist in
  // the database. In the case, we don't have to schedule the corruption
  // recovery.
  if (status != ServiceWorkerDatabase::STATUS_OK &&
      status != ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    ScheduleDeleteAndStartOver();
  }
  callback.Run(DatabaseStatusToStatusCode(status));
}

void ServiceWorkerStorage::DidGetUserData(
    const GetUserDataCallback& callback,
    const std::string& data,
    ServiceWorkerDatabase::Status status) {
  if (status != ServiceWorkerDatabase::STATUS_OK &&
      status != ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND) {
    ScheduleDeleteAndStartOver();
  }
  callback.Run(data, DatabaseStatusToStatusCode(status));
}

void ServiceWorkerStorage::DidDeleteUserData(
    const StatusCallback& callback,
    ServiceWorkerDatabase::Status status) {
  if (status != ServiceWorkerDatabase::STATUS_OK)
    ScheduleDeleteAndStartOver();
  callback.Run(DatabaseStatusToStatusCode(status));
}

void ServiceWorkerStorage::DidGetUserDataForAllRegistrations(
    const GetUserDataForAllRegistrationsCallback& callback,
    const std::vector<std::pair<int64, std::string>>& user_data,
    ServiceWorkerDatabase::Status status) {
  if (status != ServiceWorkerDatabase::STATUS_OK)
    ScheduleDeleteAndStartOver();
  callback.Run(user_data, DatabaseStatusToStatusCode(status));
}

scoped_refptr<ServiceWorkerRegistration>
ServiceWorkerStorage::GetOrCreateRegistration(
    const ServiceWorkerDatabase::RegistrationData& data,
    const ResourceList& resources) {
  scoped_refptr<ServiceWorkerRegistration> registration =
      context_->GetLiveRegistration(data.registration_id);
  if (registration.get())
    return registration;

  registration = new ServiceWorkerRegistration(
      data.scope, data.registration_id, context_);
  registration->set_resources_total_size_bytes(data.resources_total_size_bytes);
  registration->set_last_update_check(data.last_update_check);
  if (pending_deletions_.find(data.registration_id) !=
      pending_deletions_.end()) {
    registration->set_is_deleted(true);
  }
  scoped_refptr<ServiceWorkerVersion> version =
      context_->GetLiveVersion(data.version_id);
  if (!version.get()) {
    version = new ServiceWorkerVersion(
        registration.get(), data.script, data.version_id, context_);
    version->SetStatus(data.is_active ?
        ServiceWorkerVersion::ACTIVATED : ServiceWorkerVersion::INSTALLED);
    version->script_cache_map()->SetResources(resources);
  }

  if (version->status() == ServiceWorkerVersion::ACTIVATED)
    registration->SetActiveVersion(version.get());
  else if (version->status() == ServiceWorkerVersion::INSTALLED)
    registration->SetWaitingVersion(version.get());
  else
    NOTREACHED();

  return registration;
}

ServiceWorkerRegistration*
ServiceWorkerStorage::FindInstallingRegistrationForDocument(
    const GURL& document_url) {
  DCHECK(!document_url.has_ref());

  LongestScopeMatcher matcher(document_url);
  ServiceWorkerRegistration* match = NULL;

  // TODO(nhiroki): This searches over installing registrations linearly and it
  // couldn't be scalable. Maybe the regs should be partitioned by origin.
  for (RegistrationRefsById::const_iterator it =
           installing_registrations_.begin();
       it != installing_registrations_.end(); ++it) {
    if (matcher.MatchLongest(it->second->pattern()))
      match = it->second.get();
  }
  return match;
}

ServiceWorkerRegistration*
ServiceWorkerStorage::FindInstallingRegistrationForPattern(
    const GURL& scope) {
  for (RegistrationRefsById::const_iterator it =
           installing_registrations_.begin();
       it != installing_registrations_.end(); ++it) {
    if (it->second->pattern() == scope)
      return it->second.get();
  }
  return NULL;
}

ServiceWorkerRegistration*
ServiceWorkerStorage::FindInstallingRegistrationForId(
    int64 registration_id) {
  RegistrationRefsById::const_iterator found =
      installing_registrations_.find(registration_id);
  if (found == installing_registrations_.end())
    return NULL;
  return found->second.get();
}

ServiceWorkerDiskCache* ServiceWorkerStorage::disk_cache() {
  if (disk_cache_)
    return disk_cache_.get();

  disk_cache_.reset(new ServiceWorkerDiskCache);

  base::FilePath path = GetDiskCachePath();
  if (path.empty()) {
    int rv = disk_cache_->InitWithMemBackend(kMaxMemDiskCacheSize,
                                             net::CompletionCallback());
    DCHECK_EQ(net::OK, rv);
    return disk_cache_.get();
  }

  int rv = disk_cache_->InitWithDiskBackend(
      path,
      kMaxDiskCacheSize,
      false,
      disk_cache_thread_,
      base::Bind(&ServiceWorkerStorage::OnDiskCacheInitialized,
                 weak_factory_.GetWeakPtr()));
  if (rv != net::ERR_IO_PENDING)
    OnDiskCacheInitialized(rv);

  return disk_cache_.get();
}

void ServiceWorkerStorage::OnDiskCacheInitialized(int rv) {
  if (rv != net::OK) {
    LOG(ERROR) << "Failed to open the serviceworker diskcache: "
               << net::ErrorToString(rv);
    ScheduleDeleteAndStartOver();
  }
  ServiceWorkerMetrics::CountInitDiskCacheResult(rv == net::OK);
}

void ServiceWorkerStorage::StartPurgingResources(
    const std::vector<int64>& ids) {
  DCHECK(has_checked_for_stale_resources_);
  for (size_t i = 0; i < ids.size(); ++i)
    purgeable_resource_ids_.push_back(ids[i]);
  ContinuePurgingResources();
}

void ServiceWorkerStorage::StartPurgingResources(
    const ResourceList& resources) {
  DCHECK(has_checked_for_stale_resources_);
  for (size_t i = 0; i < resources.size(); ++i)
    purgeable_resource_ids_.push_back(resources[i].resource_id);
  ContinuePurgingResources();
}

void ServiceWorkerStorage::ContinuePurgingResources() {
  if (purgeable_resource_ids_.empty() || is_purge_pending_)
    return;

  // Do one at a time until we're done, use RunSoon to avoid recursion when
  // DoomEntry returns immediately.
  is_purge_pending_ = true;
  int64 id = purgeable_resource_ids_.front();
  purgeable_resource_ids_.pop_front();
  RunSoon(FROM_HERE,
          base::Bind(&ServiceWorkerStorage::PurgeResource,
                     weak_factory_.GetWeakPtr(), id));
}

void ServiceWorkerStorage::PurgeResource(int64 id) {
  DCHECK(is_purge_pending_);
  int rv = disk_cache()->DoomEntry(
      id, base::Bind(&ServiceWorkerStorage::OnResourcePurged,
                     weak_factory_.GetWeakPtr(), id));
  if (rv != net::ERR_IO_PENDING)
    OnResourcePurged(id, rv);
}

void ServiceWorkerStorage::OnResourcePurged(int64 id, int rv) {
  DCHECK(is_purge_pending_);
  is_purge_pending_ = false;

  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(
          &ServiceWorkerDatabase::ClearPurgeableResourceIds),
          base::Unretained(database_.get()),
          std::set<int64>(&id, &id + 1)));

  ContinuePurgingResources();
}

void ServiceWorkerStorage::DeleteStaleResources() {
  DCHECK(!has_checked_for_stale_resources_);
  has_checked_for_stale_resources_ = true;
  database_task_manager_->GetTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&ServiceWorkerStorage::CollectStaleResourcesFromDB,
                 database_.get(),
                 base::MessageLoopProxy::current(),
                 base::Bind(&ServiceWorkerStorage::DidCollectStaleResources,
                            weak_factory_.GetWeakPtr())));
}

void ServiceWorkerStorage::DidCollectStaleResources(
    const std::vector<int64>& stale_resource_ids,
    ServiceWorkerDatabase::Status status) {
  DCHECK_EQ(ServiceWorkerDatabase::STATUS_OK, status);
  if (status != ServiceWorkerDatabase::STATUS_OK)
    return;
  StartPurgingResources(stale_resource_ids);
}

void ServiceWorkerStorage::ClearSessionOnlyOrigins() {
  // Can be null in tests.
  if (!special_storage_policy_.get())
    return;

  if (!special_storage_policy_->HasSessionOnlyOrigins())
    return;

  std::set<GURL> session_only_origins;
  for (const GURL& origin : registered_origins_) {
    if (special_storage_policy_->IsStorageSessionOnly(origin))
      session_only_origins.insert(origin);
  }

  database_task_manager_->GetShutdownBlockingTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&DeleteAllDataForOriginsFromDB,
                 database_.get(),
                 session_only_origins));
}

void ServiceWorkerStorage::CollectStaleResourcesFromDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    const GetResourcesCallback& callback) {
  std::set<int64> ids;
  ServiceWorkerDatabase::Status status =
      database->GetUncommittedResourceIds(&ids);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(
            callback, std::vector<int64>(ids.begin(), ids.end()), status));
    return;
  }

  status = database->PurgeUncommittedResourceIds(ids);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(
            callback, std::vector<int64>(ids.begin(), ids.end()), status));
    return;
  }

  ids.clear();
  status = database->GetPurgeableResourceIds(&ids);
  original_task_runner->PostTask(
      FROM_HERE,
      base::Bind(callback, std::vector<int64>(ids.begin(), ids.end()), status));
}

void ServiceWorkerStorage::ReadInitialDataFromDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    const InitializeCallback& callback) {
  DCHECK(database);
  scoped_ptr<ServiceWorkerStorage::InitialData> data(
      new ServiceWorkerStorage::InitialData());

  ServiceWorkerDatabase::Status status =
      database->GetNextAvailableIds(&data->next_registration_id,
                                    &data->next_version_id,
                                    &data->next_resource_id);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE, base::Bind(callback, base::Owned(data.release()), status));
    return;
  }

  status = database->GetOriginsWithRegistrations(&data->origins);
  original_task_runner->PostTask(
      FROM_HERE, base::Bind(callback, base::Owned(data.release()), status));
}

void ServiceWorkerStorage::DeleteRegistrationFromDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    int64 registration_id,
    const GURL& origin,
    const DeleteRegistrationCallback& callback) {
  DCHECK(database);

  ServiceWorkerDatabase::RegistrationData deleted_version;
  std::vector<int64> newly_purgeable_resources;
  ServiceWorkerDatabase::Status status = database->DeleteRegistration(
      registration_id, origin, &deleted_version, &newly_purgeable_resources);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(
            callback, false, deleted_version, std::vector<int64>(), status));
    return;
  }

  // TODO(nhiroki): Add convenient method to ServiceWorkerDatabase to check the
  // unique origin list.
  std::vector<ServiceWorkerDatabase::RegistrationData> registrations;
  status = database->GetRegistrationsForOrigin(origin, &registrations);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(
            callback, false, deleted_version, std::vector<int64>(), status));
    return;
  }

  bool deletable = registrations.empty();
  original_task_runner->PostTask(FROM_HERE,
                                 base::Bind(callback,
                                            deletable,
                                            deleted_version,
                                            newly_purgeable_resources,
                                            status));
}

void ServiceWorkerStorage::WriteRegistrationInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    const ServiceWorkerDatabase::RegistrationData& data,
    const ResourceList& resources,
    const WriteRegistrationCallback& callback) {
  DCHECK(database);
  ServiceWorkerDatabase::RegistrationData deleted_version;
  std::vector<int64> newly_purgeable_resources;
  ServiceWorkerDatabase::Status status = database->WriteRegistration(
      data, resources, &deleted_version, &newly_purgeable_resources);
  original_task_runner->PostTask(FROM_HERE,
                                 base::Bind(callback,
                                            data.script.GetOrigin(),
                                            deleted_version,
                                            newly_purgeable_resources,
                                            status));
}

void ServiceWorkerStorage::FindForDocumentInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    const GURL& document_url,
    const FindInDBCallback& callback) {
  GURL origin = document_url.GetOrigin();
  RegistrationList registrations;
  ServiceWorkerDatabase::Status status =
      database->GetRegistrationsForOrigin(origin, &registrations);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(callback,
                   ServiceWorkerDatabase::RegistrationData(),
                   ResourceList(),
                   status));
    return;
  }

  ServiceWorkerDatabase::RegistrationData data;
  ResourceList resources;
  status = ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND;

  // Find one with a pattern match.
  LongestScopeMatcher matcher(document_url);
  int64 match = kInvalidServiceWorkerRegistrationId;
  for (size_t i = 0; i < registrations.size(); ++i) {
    if (matcher.MatchLongest(registrations[i].scope))
      match = registrations[i].registration_id;
  }

  if (match != kInvalidServiceWorkerRegistrationId)
    status = database->ReadRegistration(match, origin, &data, &resources);

  original_task_runner->PostTask(
      FROM_HERE,
      base::Bind(callback, data, resources, status));
}

void ServiceWorkerStorage::FindForPatternInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    const GURL& scope,
    const FindInDBCallback& callback) {
  GURL origin = scope.GetOrigin();
  std::vector<ServiceWorkerDatabase::RegistrationData> registrations;
  ServiceWorkerDatabase::Status status =
      database->GetRegistrationsForOrigin(origin, &registrations);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(callback,
                   ServiceWorkerDatabase::RegistrationData(),
                   ResourceList(),
                   status));
    return;
  }

  // Find one with an exact matching scope.
  ServiceWorkerDatabase::RegistrationData data;
  ResourceList resources;
  status = ServiceWorkerDatabase::STATUS_ERROR_NOT_FOUND;
  for (RegistrationList::const_iterator it = registrations.begin();
       it != registrations.end(); ++it) {
    if (scope != it->scope)
      continue;
    status = database->ReadRegistration(it->registration_id, origin,
                                        &data, &resources);
    break;  // We're done looping.
  }

  original_task_runner->PostTask(
      FROM_HERE,
      base::Bind(callback, data, resources, status));
}

void ServiceWorkerStorage::FindForIdInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    int64 registration_id,
    const GURL& origin,
    const FindInDBCallback& callback) {
  ServiceWorkerDatabase::RegistrationData data;
  ResourceList resources;
  ServiceWorkerDatabase::Status status =
      database->ReadRegistration(registration_id, origin, &data, &resources);
  original_task_runner->PostTask(
      FROM_HERE, base::Bind(callback, data, resources, status));
}

void ServiceWorkerStorage::FindForIdOnlyInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    int64 registration_id,
    const FindInDBCallback& callback) {
  GURL origin;
  ServiceWorkerDatabase::Status status =
      database->ReadRegistrationOrigin(registration_id, &origin);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    original_task_runner->PostTask(
        FROM_HERE,
        base::Bind(callback, ServiceWorkerDatabase::RegistrationData(),
                   ResourceList(), status));
    return;
  }
  FindForIdInDB(database, original_task_runner, registration_id, origin,
                callback);
}

void ServiceWorkerStorage::GetUserDataInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    int64 registration_id,
    const std::string& key,
    const GetUserDataInDBCallback& callback) {
  std::string data;
  ServiceWorkerDatabase::Status status =
      database->ReadUserData(registration_id, key, &data);
  original_task_runner->PostTask(
      FROM_HERE, base::Bind(callback, data, status));
}

void ServiceWorkerStorage::GetUserDataForAllRegistrationsInDB(
    ServiceWorkerDatabase* database,
    scoped_refptr<base::SequencedTaskRunner> original_task_runner,
    const std::string& key,
    const GetUserDataForAllRegistrationsInDBCallback& callback) {
  std::vector<std::pair<int64, std::string>> user_data;
  ServiceWorkerDatabase::Status status =
      database->ReadUserDataForAllRegistrations(key, &user_data);
  original_task_runner->PostTask(FROM_HERE,
                                 base::Bind(callback, user_data, status));
}

void ServiceWorkerStorage::DeleteAllDataForOriginsFromDB(
    ServiceWorkerDatabase* database,
    const std::set<GURL>& origins) {
  DCHECK(database);

  std::vector<int64> newly_purgeable_resources;
  database->DeleteAllDataForOrigins(origins, &newly_purgeable_resources);
}

// TODO(nhiroki): The corruption recovery should not be scheduled if the error
// is transient and it can get healed soon (e.g. IO error). To do that, the
// database should not disable itself when an error occurs and the storage
// controls it instead.
void ServiceWorkerStorage::ScheduleDeleteAndStartOver() {
  // TODO(dmurph): Notify the quota manager somehow that all of our data is now
  // removed.
  if (state_ == DISABLED) {
    // Recovery process has already been scheduled.
    return;
  }
  Disable();

  DVLOG(1) << "Schedule to delete the context and start over.";
  context_->ScheduleDeleteAndStartOver();
}

void ServiceWorkerStorage::DidDeleteDatabase(
    const StatusCallback& callback,
    ServiceWorkerDatabase::Status status) {
  DCHECK_EQ(DISABLED, state_);
  if (status != ServiceWorkerDatabase::STATUS_OK) {
    // Give up the corruption recovery until the browser restarts.
    LOG(ERROR) << "Failed to delete the database: "
               << ServiceWorkerDatabase::StatusToString(status);
    callback.Run(DatabaseStatusToStatusCode(status));
    return;
  }
  DVLOG(1) << "Deleted ServiceWorkerDatabase successfully.";

  // Delete the disk cache on the cache thread.
  // TODO(nhiroki): What if there is a bunch of files in the cache directory?
  // Deleting the directory could take a long time and restart could be delayed.
  // We should probably rename the directory and delete it later.
  PostTaskAndReplyWithResult(
      database_task_manager_->GetTaskRunner(),
      FROM_HERE,
      base::Bind(&base::DeleteFile, GetDiskCachePath(), true),
      base::Bind(&ServiceWorkerStorage::DidDeleteDiskCache,
                 weak_factory_.GetWeakPtr(),
                 callback));
}

void ServiceWorkerStorage::DidDeleteDiskCache(
    const StatusCallback& callback, bool result) {
  DCHECK_EQ(DISABLED, state_);
  if (!result) {
    // Give up the corruption recovery until the browser restarts.
    LOG(ERROR) << "Failed to delete the diskcache.";
    callback.Run(SERVICE_WORKER_ERROR_FAILED);
    return;
  }
  DVLOG(1) << "Deleted ServiceWorkerDiskCache successfully.";
  callback.Run(SERVICE_WORKER_OK);
}

}  // namespace content
