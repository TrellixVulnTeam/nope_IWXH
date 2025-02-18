// Copyright 2015 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "client/settings.h"

#include <limits>

#include <fcntl.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "util/numeric/in_range_cast.h"

namespace crashpad {

struct ALIGNAS(4) Settings::Data {
  static const uint32_t kSettingsMagic = 'CPds';
  static const uint32_t kSettingsVersion = 1;

  enum Options : uint32_t {
    kUploadsEnabled = 1 << 0,
  };

  Data() : magic(kSettingsMagic),
           version(kSettingsVersion),
           options(0),
           padding_0(0),
           last_upload_attempt_time(0),
           client_id() {}

  uint32_t magic;
  uint32_t version;
  uint32_t options;
  uint32_t padding_0;
  uint64_t last_upload_attempt_time;  // time_t
  UUID client_id;
};

Settings::Settings(const base::FilePath& file_path)
    : file_path_(file_path),
      initialized_() {
}

Settings::~Settings() {
}

bool Settings::Initialize() {
  INITIALIZATION_STATE_SET_INITIALIZING(initialized_);

  ScopedFileHandle handle(HANDLE_EINTR(
      open(file_path().value().c_str(),
           O_CREAT | O_EXCL | O_WRONLY | O_EXLOCK,
           0644)));

  // The file was created, so this is a new database that needs to be
  // initialized with a client ID.
  if (handle.is_valid()) {
    bool initialized = InitializeSettings(handle.get());
    if (initialized)
      INITIALIZATION_STATE_SET_VALID(initialized_);
    return initialized;
  }

  // The file wasn't created, try opening it for a write operation. If the file
  // needs to be recovered, writing is necessary. This also ensures that the
  // process has permission to write the file.
  Data settings;
  if (!OpenForWritingAndReadSettings(&settings).is_valid())
    return false;

  INITIALIZATION_STATE_SET_VALID(initialized_);
  return true;
}

bool Settings::GetClientID(UUID* client_id) {
  INITIALIZATION_STATE_DCHECK_VALID(initialized_);

  Data settings;
  if (!OpenAndReadSettings(&settings))
    return false;

  *client_id = settings.client_id;
  return true;
}

bool Settings::GetUploadsEnabled(bool* enabled) {
  INITIALIZATION_STATE_DCHECK_VALID(initialized_);

  Data settings;
  if (!OpenAndReadSettings(&settings))
    return false;

  *enabled = (settings.options & Data::Options::kUploadsEnabled) != 0;
  return true;
}

bool Settings::SetUploadsEnabled(bool enabled) {
  INITIALIZATION_STATE_DCHECK_VALID(initialized_);

  Data settings;
  ScopedFileHandle handle = OpenForWritingAndReadSettings(&settings);
  if (!handle.is_valid())
    return false;

  if (enabled)
    settings.options |= Data::Options::kUploadsEnabled;
  else
    settings.options &= ~Data::Options::kUploadsEnabled;

  return WriteSettings(handle.get(), settings);
}

bool Settings::GetLastUploadAttemptTime(time_t* time) {
  INITIALIZATION_STATE_DCHECK_VALID(initialized_);

  Data settings;
  if (!OpenAndReadSettings(&settings))
    return false;

  *time = InRangeCast<time_t>(settings.last_upload_attempt_time,
                              std::numeric_limits<time_t>::max());
  return true;
}

bool Settings::SetLastUploadAttemptTime(time_t time) {
  INITIALIZATION_STATE_DCHECK_VALID(initialized_);

  Data settings;
  ScopedFileHandle handle = OpenForWritingAndReadSettings(&settings);
  if (!handle.is_valid())
    return false;

  settings.last_upload_attempt_time = InRangeCast<uint64_t>(time, 0);

  return WriteSettings(handle.get(), settings);
}

ScopedFileHandle Settings::OpenForReading() {
  ScopedFileHandle handle(HANDLE_EINTR(
      open(file_path().value().c_str(), O_RDONLY | O_SHLOCK)));
  PLOG_IF(ERROR, !handle.is_valid()) << "open for reading";
  return handle.Pass();
}

ScopedFileHandle Settings::OpenForReadingAndWriting() {
  ScopedFileHandle handle(HANDLE_EINTR(
      open(file_path().value().c_str(), O_RDWR | O_EXLOCK | O_CREAT, 0644)));
  PLOG_IF(ERROR, !handle.is_valid()) << "open for writing";
  return handle.Pass();
}

bool Settings::OpenAndReadSettings(Data* out_data) {
  ScopedFileHandle handle = OpenForReading();
  if (!handle.is_valid())
    return false;

  if (ReadSettings(handle.get(), out_data))
    return true;

  // The settings file is corrupt, so reinitialize it.
  handle.reset();

  // The settings failed to be read, so re-initialize them.
  return RecoverSettings(kInvalidFileHandle, out_data);
}

ScopedFileHandle Settings::OpenForWritingAndReadSettings(Data* out_data) {
  ScopedFileHandle handle = OpenForReadingAndWriting();
  if (!handle.is_valid())
    return ScopedFileHandle();

  if (!ReadSettings(handle.get(), out_data)) {
    if (!RecoverSettings(handle.get(), out_data))
      return ScopedFileHandle();
  }

  return handle.Pass();
}

bool Settings::ReadSettings(FileHandle handle, Data* out_data) {
  if (LoggingSeekFile(handle, 0, SEEK_SET) != 0)
    return false;

  if (!LoggingReadFile(handle, out_data, sizeof(*out_data)))
    return false;

  if (out_data->magic != Data::kSettingsMagic) {
    LOG(ERROR) << "Settings magic is not " << Data::kSettingsMagic;
    return false;
  }

  if (out_data->version != Data::kSettingsVersion) {
    LOG(ERROR) << "Settings version is not " << Data::kSettingsVersion;
    return false;
  }

  return true;
}

bool Settings::WriteSettings(FileHandle handle, const Data& data) {
  if (LoggingSeekFile(handle, 0, SEEK_SET) != 0)
    return false;

  if (HANDLE_EINTR(ftruncate(handle, 0)) != 0) {
    PLOG(ERROR) << "ftruncate settings file";
    return false;
  }

  return LoggingWriteFile(handle, &data, sizeof(Data));
}

bool Settings::RecoverSettings(FileHandle handle, Data* out_data) {
  ScopedFileHandle scoped_handle;
  if (handle == kInvalidFileHandle) {
    scoped_handle = OpenForReadingAndWriting();
    handle = scoped_handle.get();

    // Test if the file has already been recovered now that the exclusive lock
    // is held.
    if (ReadSettings(handle, out_data))
      return true;
  }

  LOG(INFO) << "Recovering settings file " << file_path().value();

  if (handle == kInvalidFileHandle) {
    LOG(ERROR) << "Invalid file handle";
    return false;
  }

  if (!InitializeSettings(handle))
    return false;

  return ReadSettings(handle, out_data);
}

bool Settings::InitializeSettings(FileHandle handle) {
  uuid_t uuid;
  uuid_generate(uuid);

  Data settings;
  settings.client_id.InitializeFromBytes(uuid);

  return WriteSettings(handle, settings);
}

}  // namespace crashpad
