// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_STORAGE_MONITOR_TEST_MEDIA_TRANSFER_PROTOCOL_MANAGER_LINUX_H_
#define COMPONENTS_STORAGE_MONITOR_TEST_MEDIA_TRANSFER_PROTOCOL_MANAGER_LINUX_H_

#include "device/media_transfer_protocol/media_transfer_protocol_manager.h"

namespace storage_monitor {

// A dummy MediaTransferProtocolManager implementation.
class TestMediaTransferProtocolManagerLinux
    : public device::MediaTransferProtocolManager {
 public:
  TestMediaTransferProtocolManagerLinux();
  ~TestMediaTransferProtocolManagerLinux() override;

 private:
  // device::MediaTransferProtocolManager implementation.
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  const std::vector<std::string> GetStorages() const override;
  const MtpStorageInfo* GetStorageInfo(
      const std::string& storage_name) const override;
  void OpenStorage(const std::string& storage_name,
                   const std::string& mode,
                   const OpenStorageCallback& callback) override;
  void CloseStorage(const std::string& storage_handle,
                    const CloseStorageCallback& callback) override;
  void ReadDirectory(const std::string& storage_handle,
                     const uint32 file_id,
                     const size_t max_size,
                     const ReadDirectoryCallback& callback) override;
  void ReadFileChunk(const std::string& storage_handle,
                     uint32 file_id,
                     uint32 offset,
                     uint32 count,
                     const ReadFileCallback& callback) override;
  void GetFileInfo(const std::string& storage_handle,
                   uint32 file_id,
                   const GetFileInfoCallback& callback) override;
  void CopyFileFromLocal(const std::string& storage_handle,
                         const int source_file_descriptor,
                         const uint32 parent_id,
                         const std::string& file_name,
                         const CopyFileFromLocalCallback& callback) override;
  void DeleteObject(const std::string& storage_handle,
                    const uint32 object_id,
                    const DeleteObjectCallback& callback) override;

  DISALLOW_COPY_AND_ASSIGN(TestMediaTransferProtocolManagerLinux);
};

}  // namespace storage_monitor

#endif  // COMPONENTS_STORAGE_MONITOR_TEST_MEDIA_TRANSFER_PROTOCOL_MANAGER_LINUX_H_
