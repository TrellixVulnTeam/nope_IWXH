// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/linux/mtp_device_delegate_impl_linux.h"

#include <fcntl.h>
#include <algorithm>
#include <vector>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/numerics/safe_conversions.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/media_galleries/linux/mtp_device_task_helper.h"
#include "chrome/browser/media_galleries/linux/mtp_device_task_helper_map_service.h"
#include "chrome/browser/media_galleries/linux/snapshot_file_details.h"
#include "net/base/io_buffer.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace {

// File path separator constant.
const char kRootPath[] = "/";

// Returns the device relative file path given |file_path|.
// E.g.: If the |file_path| is "/usb:2,2:12345/DCIM" and |registered_dev_path|
// is "/usb:2,2:12345", this function returns the device relative path which is
// "DCIM".
// In the special case when |registered_dev_path| and |file_path| are the same,
// return |kRootPath|.
std::string GetDeviceRelativePath(const base::FilePath& registered_dev_path,
                                  const base::FilePath& file_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!registered_dev_path.empty());
  DCHECK(!file_path.empty());
  std::string result;
  if (registered_dev_path == file_path) {
    result = kRootPath;
  } else {
    base::FilePath relative_path;
    if (registered_dev_path.AppendRelativePath(file_path, &relative_path)) {
      DCHECK(!relative_path.empty());
      result = relative_path.value();
    }
  }
  return result;
}

// Returns the MTPDeviceTaskHelper object associated with the MTP device
// storage.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// Returns NULL if the |storage_name| is no longer valid (e.g. because the
// corresponding storage device is detached, etc).
MTPDeviceTaskHelper* GetDeviceTaskHelperForStorage(
    const std::string& storage_name,
    const bool read_only) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return MTPDeviceTaskHelperMapService::GetInstance()->GetDeviceTaskHelper(
      storage_name,
      read_only);
}

// Opens the storage device for communication.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |reply_callback| is called when the OpenStorage request completes.
// |reply_callback| runs on the IO thread.
void OpenStorageOnUIThread(
    const std::string& storage_name,
    const bool read_only,
    const MTPDeviceTaskHelper::OpenStorageCallback& reply_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper) {
    task_helper =
        MTPDeviceTaskHelperMapService::GetInstance()->CreateDeviceTaskHelper(
            storage_name, read_only);
  }
  task_helper->OpenStorage(storage_name, read_only, reply_callback);
}

// Enumerates the |dir_id| directory file entries.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |directory_id| is an id of a directory to read.
// |max_size| is a maximum size to read. Set 0 not to specify the maximum size.
// |success_callback| is called when the ReadDirectory request succeeds.
// |error_callback| is called when the ReadDirectory request fails.
// |success_callback| and |error_callback| runs on the IO thread.
void ReadDirectoryOnUIThread(
    const std::string& storage_name,
    const bool read_only,
    const uint32 directory_id,
    const size_t max_size,
    const MTPDeviceTaskHelper::ReadDirectorySuccessCallback& success_callback,
    const MTPDeviceTaskHelper::ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->ReadDirectory(directory_id, max_size, success_callback,
                             error_callback);
}

// Gets the |file_path| details.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |success_callback| is called when the GetFileInfo request succeeds.
// |error_callback| is called when the GetFileInfo request fails.
// |success_callback| and |error_callback| runs on the IO thread.
void GetFileInfoOnUIThread(
    const std::string& storage_name,
    const bool read_only,
    uint32 file_id,
    const MTPDeviceTaskHelper::GetFileInfoSuccessCallback& success_callback,
    const MTPDeviceTaskHelper::ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->GetFileInfo(file_id, success_callback, error_callback);
}

// Copies the contents of |device_file_path| to |snapshot_file_path|.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |device_file_path| specifies the media device file path.
// |snapshot_file_path| specifies the platform path of the snapshot file.
// |file_size| specifies the number of bytes that will be written to the
// snapshot file.
// |success_callback| is called when the copy operation succeeds.
// |error_callback| is called when the copy operation fails.
// |success_callback| and |error_callback| runs on the IO thread.
void WriteDataIntoSnapshotFileOnUIThread(
    const std::string& storage_name,
    const bool read_only,
    const SnapshotRequestInfo& request_info,
    const base::File::Info& snapshot_file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->WriteDataIntoSnapshotFile(request_info, snapshot_file_info);
}

// Copies the contents of |device_file_path| to |snapshot_file_path|.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |request| is a struct containing details about the byte read request.
void ReadBytesOnUIThread(
    const std::string& storage_name,
    const bool read_only,
    const MTPDeviceAsyncDelegate::ReadBytesRequest& request) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->ReadBytes(request);
}

// Copies the file |source_file_descriptor| to |file_name| in |parent_id|.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |source_file_descriptor| file descriptor of source file.
// |parent_id| object id of a target directory.
// |file_name| file name of a target file.
// |success_callback| is called when the file is copied successfully.
// |error_callback| is called when it fails to copy file.
// Since this method does not close the file descriptor, callbacks are
// responsible for closing it.
void CopyFileFromLocalOnUIThread(
    const std::string& storage_name,
    const bool read_only,
    const int source_file_descriptor,
    const uint32 parent_id,
    const std::string& file_name,
    const MTPDeviceTaskHelper::CopyFileFromLocalSuccessCallback&
        success_callback,
    const MTPDeviceTaskHelper::ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->CopyFileFromLocal(storage_name, source_file_descriptor,
                                 parent_id, file_name, success_callback,
                                 error_callback);
}

// Deletes |object_id|.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
//
// |storage_name| specifies the name of the storage device.
// |read_only| specifies the mode of the storage device.
// |object_id| is the object to be deleted.
// |success_callback| is called when the object is deleted successfully.
// |error_callback| is called when it fails to delete the object.
// |success_callback| and |error_callback| runs on the IO thread.
void DeleteObjectOnUIThread(
    const std::string storage_name,
    const bool read_only,
    const uint32 object_id,
    const MTPDeviceTaskHelper::DeleteObjectSuccessCallback success_callback,
    const MTPDeviceTaskHelper::ErrorCallback error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->DeleteObject(object_id, success_callback, error_callback);
}

// Closes the device storage specified by the |storage_name| and destroys the
// MTPDeviceTaskHelper object associated with the device storage.
//
// Called on the UI thread to dispatch the request to the
// MediaTransferProtocolManager.
void CloseStorageAndDestroyTaskHelperOnUIThread(
    const std::string& storage_name,
    const bool read_only) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MTPDeviceTaskHelper* task_helper =
      GetDeviceTaskHelperForStorage(storage_name, read_only);
  if (!task_helper)
    return;
  task_helper->CloseStorage();
  MTPDeviceTaskHelperMapService::GetInstance()->DestroyDeviceTaskHelper(
      storage_name, read_only);
}

// Opens |file_path| with |flags|.
int OpenFileDescriptor(const char* file_path, const int flags) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::FILE);

  return open(file_path, flags);
}

// Closes |file_descriptor| on file thread.
void CloseFileDescriptor(const int file_descriptor) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::FILE);

  IGNORE_EINTR(close(file_descriptor));
}

// Deletes a temporary file |file_path|.
void DeleteTemporaryFile(const base::FilePath& file_path) {
  content::BrowserThread::PostBlockingPoolTask(
      FROM_HERE, base::Bind(base::IgnoreResult(base::DeleteFile), file_path,
                            false /* not recursive*/));
}

}  // namespace

MTPDeviceDelegateImplLinux::PendingTaskInfo::PendingTaskInfo(
    const base::FilePath& path,
    content::BrowserThread::ID thread_id,
    const tracked_objects::Location& location,
    const base::Closure& task)
    : path(path),
      thread_id(thread_id),
      location(location),
      task(task) {
}

MTPDeviceDelegateImplLinux::PendingTaskInfo::~PendingTaskInfo() {
}

// Represents a file on the MTP device.
// Lives on the IO thread.
class MTPDeviceDelegateImplLinux::MTPFileNode {
 public:
  MTPFileNode(uint32 file_id,
              const std::string& file_name,
              MTPFileNode* parent,
              FileIdToMTPFileNodeMap* file_id_to_node_map);
  ~MTPFileNode();

  const MTPFileNode* GetChild(const std::string& name) const;

  void EnsureChildExists(const std::string& name, uint32 id);

  // Clears all the children, except those in |children_to_keep|.
  void ClearNonexistentChildren(
      const std::set<std::string>& children_to_keep);

  bool DeleteChild(uint32 file_id);

  bool HasChildren() const;

  uint32 file_id() const { return file_id_; }
  const std::string& file_name() const { return file_name_; }
  MTPFileNode* parent() { return parent_; }

 private:
  // Container for holding a node's children.
  typedef base::ScopedPtrHashMap<std::string, MTPFileNode> ChildNodes;

  const uint32 file_id_;
  const std::string file_name_;

  ChildNodes children_;
  MTPFileNode* const parent_;
  FileIdToMTPFileNodeMap* file_id_to_node_map_;

  DISALLOW_COPY_AND_ASSIGN(MTPFileNode);
};

MTPDeviceDelegateImplLinux::MTPFileNode::MTPFileNode(
    uint32 file_id,
    const std::string& file_name,
    MTPFileNode* parent,
    FileIdToMTPFileNodeMap* file_id_to_node_map)
    : file_id_(file_id),
      file_name_(file_name),
      parent_(parent),
      file_id_to_node_map_(file_id_to_node_map) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(file_id_to_node_map_);
  DCHECK(!ContainsKey(*file_id_to_node_map_, file_id_));
  (*file_id_to_node_map_)[file_id_] = this;
}

MTPDeviceDelegateImplLinux::MTPFileNode::~MTPFileNode() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  size_t erased = file_id_to_node_map_->erase(file_id_);
  DCHECK_EQ(1U, erased);
}

const MTPDeviceDelegateImplLinux::MTPFileNode*
MTPDeviceDelegateImplLinux::MTPFileNode::GetChild(
    const std::string& name) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  return children_.get(name);
}

void MTPDeviceDelegateImplLinux::MTPFileNode::EnsureChildExists(
    const std::string& name,
    uint32 id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  const MTPFileNode* child = GetChild(name);
  if (child && child->file_id() == id)
    return;

  children_.set(
      name,
      make_scoped_ptr(new MTPFileNode(id, name, this, file_id_to_node_map_)));
}

void MTPDeviceDelegateImplLinux::MTPFileNode::ClearNonexistentChildren(
    const std::set<std::string>& children_to_keep) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  std::set<std::string> children_to_erase;
  for (ChildNodes::const_iterator it = children_.begin();
       it != children_.end(); ++it) {
    if (ContainsKey(children_to_keep, it->first))
      continue;
    children_to_erase.insert(it->first);
  }
  for (std::set<std::string>::iterator it = children_to_erase.begin();
       it != children_to_erase.end(); ++it) {
    children_.take_and_erase(*it);
  }
}

bool MTPDeviceDelegateImplLinux::MTPFileNode::DeleteChild(uint32 file_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  for (ChildNodes::iterator it = children_.begin();
       it != children_.end(); ++it) {
    if (it->second->file_id() == file_id) {
      DCHECK(!it->second->HasChildren());
      children_.erase(it);
      return true;
    }
  }
  return false;
}

bool MTPDeviceDelegateImplLinux::MTPFileNode::HasChildren() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  return children_.size() > 0;
}

MTPDeviceDelegateImplLinux::MTPDeviceDelegateImplLinux(
    const std::string& device_location,
    const bool read_only)
    : init_state_(UNINITIALIZED),
      task_in_progress_(false),
      device_path_(device_location),
      read_only_(read_only),
      root_node_(new MTPFileNode(mtpd::kRootFileId,
                                 "",    // Root node has no name.
                                 NULL,  // And no parent node.
                                 &file_id_to_node_map_)),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!device_path_.empty());
  base::RemoveChars(device_location, kRootPath, &storage_name_);
  DCHECK(!storage_name_.empty());
}

MTPDeviceDelegateImplLinux::~MTPDeviceDelegateImplLinux() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
}

void MTPDeviceDelegateImplLinux::GetFileInfo(
    const base::FilePath& file_path,
    const GetFileInfoSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!file_path.empty());

  // If a ReadDirectory operation is in progress, the file info may already be
  // cached.
  FileInfoCache::const_iterator it = file_info_cache_.find(file_path);
  if (it != file_info_cache_.end()) {
    // TODO(thestig): This code is repeated in several places. Combine them.
    // e.g. c/b/media_galleries/win/mtp_device_operations_util.cc
    const storage::DirectoryEntry& cached_file_entry = it->second;
    base::File::Info info;
    info.size = cached_file_entry.size;
    info.is_directory = cached_file_entry.is_directory;
    info.is_symbolic_link = false;
    info.last_modified = cached_file_entry.last_modified_time;
    info.creation_time = base::Time();

    success_callback.Run(info);
    return;
  }
  base::Closure closure =
      base::Bind(&MTPDeviceDelegateImplLinux::GetFileInfoInternal,
                 weak_ptr_factory_.GetWeakPtr(),
                 file_path,
                 success_callback,
                 error_callback);
  EnsureInitAndRunTask(PendingTaskInfo(file_path,
                                       content::BrowserThread::IO,
                                       FROM_HERE,
                                       closure));
}

void MTPDeviceDelegateImplLinux::ReadDirectory(
    const base::FilePath& root,
    const ReadDirectorySuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!root.empty());
  base::Closure closure =
      base::Bind(&MTPDeviceDelegateImplLinux::ReadDirectoryInternal,
                 weak_ptr_factory_.GetWeakPtr(),
                 root,
                 success_callback,
                 error_callback);
  EnsureInitAndRunTask(PendingTaskInfo(root,
                                       content::BrowserThread::IO,
                                       FROM_HERE,
                                       closure));
}

void MTPDeviceDelegateImplLinux::CreateSnapshotFile(
    const base::FilePath& device_file_path,
    const base::FilePath& local_path,
    const CreateSnapshotFileSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!device_file_path.empty());
  DCHECK(!local_path.empty());
  base::Closure closure =
      base::Bind(&MTPDeviceDelegateImplLinux::CreateSnapshotFileInternal,
                 weak_ptr_factory_.GetWeakPtr(),
                 device_file_path,
                 local_path,
                 success_callback,
                 error_callback);
  EnsureInitAndRunTask(PendingTaskInfo(device_file_path,
                                       content::BrowserThread::IO,
                                       FROM_HERE,
                                       closure));
}

bool MTPDeviceDelegateImplLinux::IsStreaming() {
  return true;
}

void MTPDeviceDelegateImplLinux::ReadBytes(
    const base::FilePath& device_file_path,
    const scoped_refptr<net::IOBuffer>& buf,
    int64 offset,
    int buf_len,
    const ReadBytesSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!device_file_path.empty());
  base::Closure closure =
      base::Bind(&MTPDeviceDelegateImplLinux::ReadBytesInternal,
                 weak_ptr_factory_.GetWeakPtr(),
                 device_file_path,
                 buf,
                 offset,
                 buf_len,
                 success_callback,
                 error_callback);
  EnsureInitAndRunTask(PendingTaskInfo(device_file_path,
                                       content::BrowserThread::IO,
                                       FROM_HERE,
                                       closure));
}

bool MTPDeviceDelegateImplLinux::IsReadOnly() const {
  return read_only_;
}

void MTPDeviceDelegateImplLinux::CopyFileLocal(
    const base::FilePath& source_file_path,
    const base::FilePath& device_file_path,
    const CreateTemporaryFileCallback& create_temporary_file_callback,
    const CopyFileProgressCallback& progress_callback,
    const CopyFileLocalSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!source_file_path.empty());
  DCHECK(!device_file_path.empty());

  // Create a temporary file for creating a copy of source file on local.
  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::FILE, FROM_HERE, create_temporary_file_callback,
      base::Bind(
          &MTPDeviceDelegateImplLinux::OnDidCreateTemporaryFileToCopyFileLocal,
          weak_ptr_factory_.GetWeakPtr(), source_file_path, device_file_path,
          progress_callback, success_callback, error_callback));
}

void MTPDeviceDelegateImplLinux::CopyFileFromLocal(
    const base::FilePath& source_file_path,
    const base::FilePath& device_file_path,
    const CopyFileFromLocalSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!source_file_path.empty());
  DCHECK(!device_file_path.empty());

  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::FILE,
      FROM_HERE,
      base::Bind(&OpenFileDescriptor,
                 source_file_path.value().c_str(),
                 O_RDONLY),
      base::Bind(&MTPDeviceDelegateImplLinux::CopyFileFromLocalInternal,
                 weak_ptr_factory_.GetWeakPtr(),
                 device_file_path,
                 success_callback,
                 error_callback));
}

void MTPDeviceDelegateImplLinux::DeleteFile(
    const base::FilePath& file_path,
    const DeleteFileSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!file_path.empty());

  const GetFileInfoSuccessCallback& success_callback_wrapper =
      base::Bind(&MTPDeviceDelegateImplLinux::DeleteFileInternal,
                 weak_ptr_factory_.GetWeakPtr(), file_path, success_callback,
                 error_callback);

  const base::Closure closure =
      base::Bind(&MTPDeviceDelegateImplLinux::GetFileInfoInternal,
                 weak_ptr_factory_.GetWeakPtr(), file_path,
                 success_callback_wrapper, error_callback);
  EnsureInitAndRunTask(PendingTaskInfo(file_path, content::BrowserThread::IO,
                                       FROM_HERE, closure));
}

void MTPDeviceDelegateImplLinux::DeleteDirectory(
    const base::FilePath& file_path,
    const DeleteDirectorySuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!file_path.empty());

  const GetFileInfoSuccessCallback& success_callback_wrapper =
      base::Bind(&MTPDeviceDelegateImplLinux::DeleteDirectoryInternal,
                 weak_ptr_factory_.GetWeakPtr(), file_path, success_callback,
                 error_callback);

  const base::Closure closure =
      base::Bind(&MTPDeviceDelegateImplLinux::GetFileInfoInternal,
                 weak_ptr_factory_.GetWeakPtr(), file_path,
                 success_callback_wrapper, error_callback);
  EnsureInitAndRunTask(PendingTaskInfo(file_path, content::BrowserThread::IO,
                                       FROM_HERE, closure));
}

void MTPDeviceDelegateImplLinux::CancelPendingTasksAndDeleteDelegate() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  // To cancel all the pending tasks, destroy the MTPDeviceTaskHelper object.
  content::BrowserThread::PostTask(
      content::BrowserThread::UI,
      FROM_HERE,
      base::Bind(&CloseStorageAndDestroyTaskHelperOnUIThread,
                 storage_name_,
                 read_only_));
  delete this;
}

void MTPDeviceDelegateImplLinux::GetFileInfoInternal(
    const base::FilePath& file_path,
    const GetFileInfoSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  uint32 file_id;
  if (CachedPathToId(file_path, &file_id)) {
    GetFileInfoSuccessCallback success_callback_wrapper =
        base::Bind(&MTPDeviceDelegateImplLinux::OnDidGetFileInfo,
                   weak_ptr_factory_.GetWeakPtr(),
                   success_callback);
    ErrorCallback error_callback_wrapper =
        base::Bind(&MTPDeviceDelegateImplLinux::HandleDeviceFileError,
                   weak_ptr_factory_.GetWeakPtr(),
                   error_callback,
                   file_id);


    base::Closure closure = base::Bind(&GetFileInfoOnUIThread,
                                       storage_name_,
                                       read_only_,
                                       file_id,
                                       success_callback_wrapper,
                                       error_callback_wrapper);
    EnsureInitAndRunTask(PendingTaskInfo(base::FilePath(),
                                         content::BrowserThread::UI,
                                         FROM_HERE,
                                         closure));
  } else {
    error_callback.Run(base::File::FILE_ERROR_NOT_FOUND);
  }
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::ReadDirectoryInternal(
    const base::FilePath& root,
    const ReadDirectorySuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  uint32 dir_id;
  if (CachedPathToId(root, &dir_id)) {
    GetFileInfoSuccessCallback success_callback_wrapper =
        base::Bind(&MTPDeviceDelegateImplLinux::OnDidGetFileInfoToReadDirectory,
                   weak_ptr_factory_.GetWeakPtr(),
                   dir_id,
                   success_callback,
                   error_callback);
    ErrorCallback error_callback_wrapper =
        base::Bind(&MTPDeviceDelegateImplLinux::HandleDeviceFileError,
                   weak_ptr_factory_.GetWeakPtr(),
                   error_callback,
                   dir_id);
    base::Closure closure = base::Bind(&GetFileInfoOnUIThread,
                                       storage_name_,
                                       read_only_,
                                       dir_id,
                                       success_callback_wrapper,
                                       error_callback_wrapper);
    EnsureInitAndRunTask(PendingTaskInfo(base::FilePath(),
                                         content::BrowserThread::UI,
                                         FROM_HERE,
                                         closure));
  } else {
    error_callback.Run(base::File::FILE_ERROR_NOT_FOUND);
  }
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::CreateSnapshotFileInternal(
    const base::FilePath& device_file_path,
    const base::FilePath& local_path,
    const CreateSnapshotFileSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  uint32 file_id;
  if (CachedPathToId(device_file_path, &file_id)) {
    scoped_ptr<SnapshotRequestInfo> request_info(
        new SnapshotRequestInfo(file_id,
                                local_path,
                                success_callback,
                                error_callback));
    GetFileInfoSuccessCallback success_callback_wrapper =
        base::Bind(
            &MTPDeviceDelegateImplLinux::OnDidGetFileInfoToCreateSnapshotFile,
            weak_ptr_factory_.GetWeakPtr(),
            base::Passed(&request_info));
    ErrorCallback error_callback_wrapper =
        base::Bind(&MTPDeviceDelegateImplLinux::HandleDeviceFileError,
                   weak_ptr_factory_.GetWeakPtr(),
                   error_callback,
                   file_id);
    base::Closure closure = base::Bind(&GetFileInfoOnUIThread,
                                       storage_name_,
                                       read_only_,
                                       file_id,
                                       success_callback_wrapper,
                                       error_callback_wrapper);
    EnsureInitAndRunTask(PendingTaskInfo(base::FilePath(),
                                         content::BrowserThread::UI,
                                         FROM_HERE,
                                         closure));
  } else {
    error_callback.Run(base::File::FILE_ERROR_NOT_FOUND);
  }
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::ReadBytesInternal(
    const base::FilePath& device_file_path,
    net::IOBuffer* buf, int64 offset, int buf_len,
    const ReadBytesSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  uint32 file_id;
  if (CachedPathToId(device_file_path, &file_id)) {
    ReadBytesRequest request(
        file_id, buf, offset, buf_len,
        base::Bind(&MTPDeviceDelegateImplLinux::OnDidReadBytes,
                   weak_ptr_factory_.GetWeakPtr(),
                   success_callback),
        base::Bind(&MTPDeviceDelegateImplLinux::HandleDeviceFileError,
                   weak_ptr_factory_.GetWeakPtr(),
                   error_callback,
                   file_id));

    base::Closure closure =
        base::Bind(&ReadBytesOnUIThread, storage_name_, read_only_, request);
    EnsureInitAndRunTask(PendingTaskInfo(base::FilePath(),
                                         content::BrowserThread::UI,
                                         FROM_HERE,
                                         closure));
  } else {
    error_callback.Run(base::File::FILE_ERROR_NOT_FOUND);
  }
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::CopyFileFromLocalInternal(
    const base::FilePath& device_file_path,
    const CopyFileFromLocalSuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const int source_file_descriptor) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (source_file_descriptor < 0) {
    error_callback.Run(base::File::FILE_ERROR_INVALID_OPERATION);
    PendingRequestDone();
    return;
  }

  uint32 parent_id;
  if (CachedPathToId(device_file_path.DirName(), &parent_id)) {
    CopyFileFromLocalSuccessCallback success_callback_wrapper =
        base::Bind(&MTPDeviceDelegateImplLinux::OnDidCopyFileFromLocal,
                   weak_ptr_factory_.GetWeakPtr(), success_callback,
                   source_file_descriptor);

    ErrorCallback error_callback_wrapper = base::Bind(
        &MTPDeviceDelegateImplLinux::HandleCopyFileFromLocalError,
        weak_ptr_factory_.GetWeakPtr(), error_callback, source_file_descriptor);

    base::Closure closure = base::Bind(&CopyFileFromLocalOnUIThread,
                                       storage_name_,
                                       read_only_,
                                       source_file_descriptor,
                                       parent_id,
                                       device_file_path.BaseName().value(),
                                       success_callback_wrapper,
                                       error_callback_wrapper);

    EnsureInitAndRunTask(PendingTaskInfo(
        base::FilePath(), content::BrowserThread::UI, FROM_HERE, closure));
  } else {
    HandleCopyFileFromLocalError(error_callback, source_file_descriptor,
                                 base::File::FILE_ERROR_INVALID_OPERATION);
  }

  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::DeleteFileInternal(
    const base::FilePath& file_path,
    const DeleteFileSuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const base::File::Info& file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (file_info.is_directory) {
    error_callback.Run(base::File::FILE_ERROR_NOT_A_FILE);
  } else {
    uint32 file_id;
    if (CachedPathToId(file_path, &file_id))
      RunDeleteObjectOnUIThread(file_id, success_callback, error_callback);
    else
      error_callback.Run(base::File::FILE_ERROR_NOT_FOUND);
  }

  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::DeleteDirectoryInternal(
    const base::FilePath& file_path,
    const DeleteDirectorySuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const base::File::Info& file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (!file_info.is_directory) {
    error_callback.Run(base::File::FILE_ERROR_NOT_A_DIRECTORY);
    PendingRequestDone();
    return;
  }

  uint32 directory_id;
  if (!CachedPathToId(file_path, &directory_id)) {
    error_callback.Run(base::File::FILE_ERROR_NOT_FOUND);
    PendingRequestDone();
    return;
  }

  // Checks the cache first. If it has children in cache, the directory cannot
  // be empty.
  FileIdToMTPFileNodeMap::const_iterator it =
      file_id_to_node_map_.find(directory_id);
  if (it != file_id_to_node_map_.end() && it->second->HasChildren()) {
    error_callback.Run(base::File::FILE_ERROR_NOT_EMPTY);
    PendingRequestDone();
    return;
  }

  // Since the directory can contain a file even if the cache returns it as
  // empty, read the directory and confirm the directory is actually empty.
  const MTPDeviceTaskHelper::ReadDirectorySuccessCallback
      success_callback_wrapper = base::Bind(
          &MTPDeviceDelegateImplLinux::OnDidReadDirectoryToDeleteDirectory,
          weak_ptr_factory_.GetWeakPtr(), directory_id, success_callback,
          error_callback);
  const MTPDeviceTaskHelper::ErrorCallback error_callback_wrapper =
      base::Bind(&MTPDeviceDelegateImplLinux::HandleDeviceFileError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback, directory_id);
  const base::Closure closure = base::Bind(
      &ReadDirectoryOnUIThread, storage_name_, read_only_, directory_id,
      1 /* max_size */, success_callback_wrapper, error_callback_wrapper);
  EnsureInitAndRunTask(PendingTaskInfo(
      base::FilePath(), content::BrowserThread::UI, FROM_HERE, closure));
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidReadDirectoryToDeleteDirectory(
    const uint32 directory_id,
    const DeleteDirectorySuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const storage::AsyncFileUtil::EntryList& entries,
    const bool has_more) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!has_more);

  if (entries.size() > 0)
    error_callback.Run(base::File::FILE_ERROR_NOT_EMPTY);
  else
    RunDeleteObjectOnUIThread(directory_id, success_callback, error_callback);

  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::RunDeleteObjectOnUIThread(
    const uint32 object_id,
    const DeleteObjectSuccessCallback& success_callback,
    const ErrorCallback& error_callback) {
  const MTPDeviceTaskHelper::DeleteObjectSuccessCallback
      success_callback_wrapper = base::Bind(
          &MTPDeviceDelegateImplLinux::OnDidDeleteObject,
          weak_ptr_factory_.GetWeakPtr(), object_id, success_callback);

  const MTPDeviceTaskHelper::ErrorCallback error_callback_wrapper =
      base::Bind(&MTPDeviceDelegateImplLinux::HandleDeleteFileOrDirectoryError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback);

  const base::Closure closure =
      base::Bind(&DeleteObjectOnUIThread, storage_name_, read_only_, object_id,
                 success_callback_wrapper, error_callback_wrapper);
  EnsureInitAndRunTask(PendingTaskInfo(
      base::FilePath(), content::BrowserThread::UI, FROM_HERE, closure));
}

void MTPDeviceDelegateImplLinux::EnsureInitAndRunTask(
    const PendingTaskInfo& task_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if ((init_state_ == INITIALIZED) && !task_in_progress_) {
    RunTask(task_info);
    return;
  }

  // Only *Internal functions have empty paths. Since they are the continuation
  // of the current running task, they get to cut in line.
  if (task_info.path.empty())
    pending_tasks_.push_front(task_info);
  else
    pending_tasks_.push_back(task_info);

  if (init_state_ == UNINITIALIZED) {
    init_state_ = PENDING_INIT;
    task_in_progress_ = true;
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&OpenStorageOnUIThread, storage_name_, read_only_,
                   base::Bind(&MTPDeviceDelegateImplLinux::OnInitCompleted,
                              weak_ptr_factory_.GetWeakPtr())));
  }
}

void MTPDeviceDelegateImplLinux::RunTask(const PendingTaskInfo& task_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK_EQ(INITIALIZED, init_state_);
  DCHECK(!task_in_progress_);
  task_in_progress_ = true;

  bool need_to_check_cache = !task_info.path.empty();
  if (need_to_check_cache) {
    base::FilePath uncached_path =
        NextUncachedPathComponent(task_info.path, task_info.cached_path);
    if (!uncached_path.empty()) {
      // Save the current task and do a cache lookup first.
      pending_tasks_.push_front(task_info);
      FillFileCache(uncached_path);
      return;
    }
  }

  content::BrowserThread::PostTask(task_info.thread_id,
                          task_info.location,
                          task_info.task);
}

void MTPDeviceDelegateImplLinux::WriteDataIntoSnapshotFile(
    const base::File::Info& file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(current_snapshot_request_info_.get());
  DCHECK_GT(file_info.size, 0);
  DCHECK(task_in_progress_);
  SnapshotRequestInfo request_info(
      current_snapshot_request_info_->file_id,
      current_snapshot_request_info_->snapshot_file_path,
      base::Bind(
          &MTPDeviceDelegateImplLinux::OnDidWriteDataIntoSnapshotFile,
          weak_ptr_factory_.GetWeakPtr()),
      base::Bind(
          &MTPDeviceDelegateImplLinux::OnWriteDataIntoSnapshotFileError,
          weak_ptr_factory_.GetWeakPtr()));

  base::Closure task_closure = base::Bind(&WriteDataIntoSnapshotFileOnUIThread,
                                          storage_name_,
                                          read_only_,
                                          request_info,
                                          file_info);
  content::BrowserThread::PostTask(content::BrowserThread::UI,
                                   FROM_HERE,
                                   task_closure);
}

void MTPDeviceDelegateImplLinux::PendingRequestDone() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(task_in_progress_);
  task_in_progress_ = false;
  ProcessNextPendingRequest();
}

void MTPDeviceDelegateImplLinux::ProcessNextPendingRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!task_in_progress_);
  if (pending_tasks_.empty())
    return;

  PendingTaskInfo task_info = pending_tasks_.front();
  pending_tasks_.pop_front();
  RunTask(task_info);
}

void MTPDeviceDelegateImplLinux::OnInitCompleted(bool succeeded) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  init_state_ = succeeded ? INITIALIZED : UNINITIALIZED;
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidGetFileInfo(
    const GetFileInfoSuccessCallback& success_callback,
    const base::File::Info& file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  success_callback.Run(file_info);
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidGetFileInfoToReadDirectory(
    uint32 dir_id,
    const ReadDirectorySuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const base::File::Info& file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(task_in_progress_);
  if (!file_info.is_directory) {
    return HandleDeviceFileError(error_callback,
                                 dir_id,
                                 base::File::FILE_ERROR_NOT_A_DIRECTORY);
  }

  base::Closure task_closure = base::Bind(
      &ReadDirectoryOnUIThread, storage_name_, read_only_, dir_id,
      0 /* max_size */,
      base::Bind(&MTPDeviceDelegateImplLinux::OnDidReadDirectory,
                 weak_ptr_factory_.GetWeakPtr(), dir_id, success_callback),
      base::Bind(&MTPDeviceDelegateImplLinux::HandleDeviceFileError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback, dir_id));
  content::BrowserThread::PostTask(content::BrowserThread::UI,
                                   FROM_HERE,
                                   task_closure);
}

void MTPDeviceDelegateImplLinux::OnDidGetFileInfoToCreateSnapshotFile(
    scoped_ptr<SnapshotRequestInfo> snapshot_request_info,
    const base::File::Info& file_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(!current_snapshot_request_info_.get());
  DCHECK(snapshot_request_info.get());
  DCHECK(task_in_progress_);
  base::File::Error error = base::File::FILE_OK;
  if (file_info.is_directory)
    error = base::File::FILE_ERROR_NOT_A_FILE;
  else if (file_info.size < 0 || file_info.size > kuint32max)
    error = base::File::FILE_ERROR_FAILED;

  if (error != base::File::FILE_OK)
    return HandleDeviceFileError(snapshot_request_info->error_callback,
                                 snapshot_request_info->file_id,
                                 error);

  base::File::Info snapshot_file_info(file_info);
  // Modify the last modified time to null. This prevents the time stamp
  // verfication in LocalFileStreamReader.
  snapshot_file_info.last_modified = base::Time();

  current_snapshot_request_info_.reset(snapshot_request_info.release());
  if (file_info.size == 0) {
    // Empty snapshot file.
    return OnDidWriteDataIntoSnapshotFile(
        snapshot_file_info, current_snapshot_request_info_->snapshot_file_path);
  }
  WriteDataIntoSnapshotFile(snapshot_file_info);
}

void MTPDeviceDelegateImplLinux::OnDidReadDirectory(
    uint32 dir_id,
    const ReadDirectorySuccessCallback& success_callback,
    const storage::AsyncFileUtil::EntryList& file_list,
    bool has_more) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  FileIdToMTPFileNodeMap::iterator it = file_id_to_node_map_.find(dir_id);
  DCHECK(it != file_id_to_node_map_.end());
  MTPFileNode* dir_node = it->second;

  // Traverse the MTPFileNode tree to reconstuct the full path for |dir_id|.
  std::deque<std::string> dir_path_parts;
  MTPFileNode* parent_node = dir_node;
  while (parent_node->parent()) {
    dir_path_parts.push_front(parent_node->file_name());
    parent_node = parent_node->parent();
  }
  base::FilePath dir_path = device_path_;
  for (size_t i = 0; i < dir_path_parts.size(); ++i)
    dir_path = dir_path.Append(dir_path_parts[i]);

  storage::AsyncFileUtil::EntryList normalized_file_list;
  for (size_t i = 0; i < file_list.size(); ++i) {
    normalized_file_list.push_back(file_list[i]);
    storage::DirectoryEntry& entry = normalized_file_list.back();

    // |entry.name| has the file id encoded in it. Decode here.
    size_t separator_idx = entry.name.find_last_of(',');
    DCHECK_NE(std::string::npos, separator_idx);
    std::string file_id_str = entry.name.substr(separator_idx);
    file_id_str = file_id_str.substr(1);  // Get rid of the comma.
    uint32 file_id = 0;
    bool ret = base::StringToUint(file_id_str, &file_id);
    DCHECK(ret);
    entry.name = entry.name.substr(0, separator_idx);

    // Refresh the in memory tree.
    dir_node->EnsureChildExists(entry.name, file_id);
    child_nodes_seen_.insert(entry.name);

    // Add to |file_info_cache_|.
    file_info_cache_[dir_path.Append(entry.name)] = entry;
  }

  success_callback.Run(normalized_file_list, has_more);
  if (has_more)
    return;  // Wait to be called again.

  // Last call, finish book keeping and continue with the next request.
  dir_node->ClearNonexistentChildren(child_nodes_seen_);
  child_nodes_seen_.clear();
  file_info_cache_.clear();

  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidWriteDataIntoSnapshotFile(
    const base::File::Info& file_info,
    const base::FilePath& snapshot_file_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(current_snapshot_request_info_.get());
  current_snapshot_request_info_->success_callback.Run(
      file_info, snapshot_file_path);
  current_snapshot_request_info_.reset();
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnWriteDataIntoSnapshotFileError(
    base::File::Error error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(current_snapshot_request_info_.get());
  current_snapshot_request_info_->error_callback.Run(error);
  current_snapshot_request_info_.reset();
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidReadBytes(
    const ReadBytesSuccessCallback& success_callback,
    const base::File::Info& file_info, int bytes_read) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  success_callback.Run(file_info, bytes_read);
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidFillFileCache(
    const base::FilePath& path,
    const storage::AsyncFileUtil::EntryList& /* file_list */,
    bool has_more) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(path.IsParent(pending_tasks_.front().path));
  if (has_more)
    return;  // Wait until all entries have been read.
  pending_tasks_.front().cached_path = path;
}

void MTPDeviceDelegateImplLinux::OnFillFileCacheFailed(
    base::File::Error /* error */) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  // When filling the cache fails for the task at the front of the queue, clear
  // the path of the task so it will not try to do any more caching. Instead,
  // the task will just run and fail the CachedPathToId() lookup.
  pending_tasks_.front().path.clear();
}

void MTPDeviceDelegateImplLinux::OnDidCreateTemporaryFileToCopyFileLocal(
    const base::FilePath& source_file_path,
    const base::FilePath& device_file_path,
    const CopyFileProgressCallback& progress_callback,
    const CopyFileLocalSuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const base::FilePath& temporary_file_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (temporary_file_path.empty()) {
    error_callback.Run(base::File::FILE_ERROR_FAILED);
    return;
  }

  CreateSnapshotFile(
      source_file_path, temporary_file_path,
      base::Bind(
          &MTPDeviceDelegateImplLinux::OnDidCreateSnapshotFileOfCopyFileLocal,
          weak_ptr_factory_.GetWeakPtr(), device_file_path, progress_callback,
          success_callback, error_callback),
      base::Bind(&MTPDeviceDelegateImplLinux::HandleCopyFileLocalError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback,
                 temporary_file_path));
}

void MTPDeviceDelegateImplLinux::OnDidCreateSnapshotFileOfCopyFileLocal(
    const base::FilePath& device_file_path,
    const CopyFileProgressCallback& progress_callback,
    const CopyFileLocalSuccessCallback& success_callback,
    const ErrorCallback& error_callback,
    const base::File::Info& file_info,
    const base::FilePath& temporary_file_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  // Consider that half of copy is completed by creating a temporary file.
  progress_callback.Run(file_info.size / 2);

  CopyFileFromLocal(
      temporary_file_path, device_file_path,
      base::Bind(
          &MTPDeviceDelegateImplLinux::OnDidCopyFileFromLocalOfCopyFileLocal,
          weak_ptr_factory_.GetWeakPtr(), success_callback,
          temporary_file_path),
      base::Bind(&MTPDeviceDelegateImplLinux::HandleCopyFileLocalError,
                 weak_ptr_factory_.GetWeakPtr(), error_callback,
                 temporary_file_path));
}

void MTPDeviceDelegateImplLinux::OnDidCopyFileFromLocalOfCopyFileLocal(
    const CopyFileFromLocalSuccessCallback success_callback,
    const base::FilePath& temporary_file_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  DeleteTemporaryFile(temporary_file_path);
  success_callback.Run();
}

void MTPDeviceDelegateImplLinux::OnDidCopyFileFromLocal(
    const CopyFileFromLocalSuccessCallback& success_callback,
    const int source_file_descriptor) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  const base::Closure closure = base::Bind(&CloseFileDescriptor,
                                           source_file_descriptor);

  content::BrowserThread::PostTask(content::BrowserThread::FILE, FROM_HERE,
                                   closure);

  success_callback.Run();
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::HandleCopyFileLocalError(
    const ErrorCallback& error_callback,
    const base::FilePath& temporary_file_path,
    const base::File::Error error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  DeleteTemporaryFile(temporary_file_path);
  error_callback.Run(error);
}

void MTPDeviceDelegateImplLinux::HandleCopyFileFromLocalError(
    const ErrorCallback& error_callback,
    const int source_file_descriptor,
    base::File::Error error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  const base::Closure closure = base::Bind(&CloseFileDescriptor,
                                           source_file_descriptor);

  content::BrowserThread::PostTask(content::BrowserThread::FILE, FROM_HERE,
                                   closure);

  error_callback.Run(error);
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::OnDidDeleteObject(
    const uint32 object_id,
    const DeleteObjectSuccessCallback success_callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  EvictCachedPathToId(object_id);
  success_callback.Run();
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::HandleDeleteFileOrDirectoryError(
    const ErrorCallback& error_callback,
    base::File::Error error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  error_callback.Run(error);
  PendingRequestDone();
}

void MTPDeviceDelegateImplLinux::HandleDeviceFileError(
    const ErrorCallback& error_callback,
    uint32 file_id,
    base::File::Error error) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  EvictCachedPathToId(file_id);
  error_callback.Run(error);
  PendingRequestDone();
}

base::FilePath MTPDeviceDelegateImplLinux::NextUncachedPathComponent(
    const base::FilePath& path,
    const base::FilePath& cached_path) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(cached_path.empty() || cached_path.IsParent(path));

  base::FilePath uncached_path;
  std::string device_relpath = GetDeviceRelativePath(device_path_, path);
  if (!device_relpath.empty() && device_relpath != kRootPath) {
    uncached_path = device_path_;
    std::vector<std::string> device_relpath_components;
    base::SplitString(device_relpath, '/', &device_relpath_components);
    DCHECK(!device_relpath_components.empty());
    bool all_components_cached = true;
    const MTPFileNode* current_node = root_node_.get();
    for (size_t i = 0; i < device_relpath_components.size(); ++i) {
      current_node = current_node->GetChild(device_relpath_components[i]);
      if (!current_node) {
        // With a cache miss, check if it is a genuine failure. If so, pretend
        // the entire |path| is cached, so there is no further attempt to do
        // more caching. The actual operation will then fail.
        all_components_cached =
            !cached_path.empty() && (uncached_path == cached_path);
        break;
      }
      uncached_path = uncached_path.Append(device_relpath_components[i]);
    }
    if (all_components_cached)
      uncached_path.clear();
  }
  return uncached_path;
}

void MTPDeviceDelegateImplLinux::FillFileCache(
    const base::FilePath& uncached_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(task_in_progress_);

  ReadDirectorySuccessCallback success_callback =
      base::Bind(&MTPDeviceDelegateImplLinux::OnDidFillFileCache,
                 weak_ptr_factory_.GetWeakPtr(),
                 uncached_path);
  ErrorCallback error_callback =
      base::Bind(&MTPDeviceDelegateImplLinux::OnFillFileCacheFailed,
                 weak_ptr_factory_.GetWeakPtr());
  ReadDirectoryInternal(uncached_path, success_callback, error_callback);
}


bool MTPDeviceDelegateImplLinux::CachedPathToId(const base::FilePath& path,
                                                uint32* id) const {
  DCHECK(id);

  std::string device_relpath = GetDeviceRelativePath(device_path_, path);
  if (device_relpath.empty())
    return false;
  std::vector<std::string> device_relpath_components;
  if (device_relpath != kRootPath)
    base::SplitString(device_relpath, '/', &device_relpath_components);
  const MTPFileNode* current_node = root_node_.get();
  for (size_t i = 0; i < device_relpath_components.size(); ++i) {
    current_node = current_node->GetChild(device_relpath_components[i]);
    if (!current_node)
      return false;
  }
  *id = current_node->file_id();
  return true;
}

void MTPDeviceDelegateImplLinux::EvictCachedPathToId(const uint32 id) {
  FileIdToMTPFileNodeMap::iterator it = file_id_to_node_map_.find(id);
  if (it != file_id_to_node_map_.end()) {
    DCHECK(!it->second->HasChildren());
    MTPFileNode* parent = it->second->parent();
    if (parent) {
      const bool ret = parent->DeleteChild(id);
      DCHECK(ret);
    }
  }
}

void CreateMTPDeviceAsyncDelegate(
    const std::string& device_location,
    const bool read_only,
    const CreateMTPDeviceAsyncDelegateCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  callback.Run(new MTPDeviceDelegateImplLinux(device_location, read_only));
}
