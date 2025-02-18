// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <vector>

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/browser/component_updater/flash_component_installer.h"
#include "chrome/browser/plugins/plugin_prefs.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pepper_flash.h"
#include "chrome/common/ppapi_utils.h"
#include "components/component_updater/component_updater_service.h"
#include "components/update_client/update_client.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/pepper_plugin_info.h"
#include "flapper_version.h"  // In SHARED_INTERMEDIATE_DIR.  NOLINT
#include "ppapi/shared_impl/ppapi_permissions.h"

using content::BrowserThread;
using content::PluginService;

namespace component_updater {

namespace {

#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)
// CRX hash. The extension id is: mimojjlkmoijpicakmndhoigimigcmbb.
const uint8_t kSha2Hash[] = {0xc8, 0xce, 0x99, 0xba, 0xce, 0x89, 0xf8, 0x20,
                             0xac, 0xd3, 0x7e, 0x86, 0x8c, 0x86, 0x2c, 0x11,
                             0xb9, 0x40, 0xc5, 0x55, 0xaf, 0x08, 0x63, 0x70,
                             0x54, 0xf9, 0x56, 0xd3, 0xe7, 0x88, 0xba, 0x8c};

// If we don't have a Pepper Flash component, this is the version we claim.
const char kNullVersion[] = "0.0.0.0";

#endif  // defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)

// The base directory on Windows looks like:
// <profile>\AppData\Local\Google\Chrome\User Data\PepperFlash\.
base::FilePath GetPepperFlashBaseDirectory() {
  base::FilePath result;
  PathService::Get(chrome::DIR_COMPONENT_UPDATED_PEPPER_FLASH_PLUGIN, &result);
  return result;
}

#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)
// Pepper Flash plugins have the version encoded in the path itself
// so we need to enumerate the directories to find the full path.
// On success, |latest_dir| returns something like:
// <profile>\AppData\Local\Google\Chrome\User Data\PepperFlash\10.3.44.555\.
// |latest_version| returns the corresponding version number. |older_dirs|
// returns directories of all older versions.
bool GetPepperFlashDirectory(base::FilePath* latest_dir,
                             Version* latest_version,
                             std::vector<base::FilePath>* older_dirs) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  base::FilePath base_dir = GetPepperFlashBaseDirectory();
  bool found = false;
  base::FileEnumerator file_enumerator(
      base_dir, false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = file_enumerator.Next(); !path.value().empty();
       path = file_enumerator.Next()) {
    Version version(path.BaseName().MaybeAsASCII());
    if (!version.IsValid())
      continue;
    if (found) {
      if (version.CompareTo(*latest_version) > 0) {
        older_dirs->push_back(*latest_dir);
        *latest_dir = path;
        *latest_version = version;
      } else {
        older_dirs->push_back(path);
      }
    } else {
      *latest_dir = path;
      *latest_version = version;
      found = true;
    }
  }
  return found;
}
#endif  // defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)

bool MakePepperFlashPluginInfo(const base::FilePath& flash_path,
                               const Version& flash_version,
                               bool out_of_process,
                               content::PepperPluginInfo* plugin_info) {
  if (!flash_version.IsValid())
    return false;
  const std::vector<uint32_t> ver_nums = flash_version.components();
  if (ver_nums.size() < 3)
    return false;

  plugin_info->is_internal = false;
  plugin_info->is_out_of_process = out_of_process;
  plugin_info->path = flash_path;
  plugin_info->name = content::kFlashPluginName;
  plugin_info->permissions = chrome::kPepperFlashPermissions;

  // The description is like "Shockwave Flash 10.2 r154".
  plugin_info->description = base::StringPrintf("%s %d.%d r%d",
                                                content::kFlashPluginName,
                                                ver_nums[0],
                                                ver_nums[1],
                                                ver_nums[2]);

  plugin_info->version = flash_version.GetString();

  content::WebPluginMimeType swf_mime_type(content::kFlashPluginSwfMimeType,
                                           content::kFlashPluginSwfExtension,
                                           content::kFlashPluginName);
  plugin_info->mime_types.push_back(swf_mime_type);
  content::WebPluginMimeType spl_mime_type(content::kFlashPluginSplMimeType,
                                           content::kFlashPluginSplExtension,
                                           content::kFlashPluginName);
  plugin_info->mime_types.push_back(spl_mime_type);
  return true;
}

bool IsPepperFlash(const content::WebPluginInfo& plugin) {
  // We try to recognize Pepper Flash by the following criteria:
  // * It is a Pepper plug-in.
  // * It has the special Flash permissions.
  return plugin.is_pepper_plugin() &&
         (plugin.pepper_permissions & ppapi::PERMISSION_FLASH);
}

void RegisterPepperFlashWithChrome(const base::FilePath& path,
                                   const Version& version) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  content::PepperPluginInfo plugin_info;
  if (!MakePepperFlashPluginInfo(path, version, true, &plugin_info))
    return;

  std::vector<content::WebPluginInfo> plugins;
  PluginService::GetInstance()->GetInternalPlugins(&plugins);
  for (std::vector<content::WebPluginInfo>::const_iterator it =
            plugins.begin();
        it != plugins.end();
        ++it) {
    if (!IsPepperFlash(*it))
      continue;

    // Do it only if the version we're trying to register is newer.
    Version registered_version(base::UTF16ToUTF8(it->version));
    if (registered_version.IsValid() &&
        version.CompareTo(registered_version) <= 0) {
      return;
    }

    // If the version is newer, remove the old one first.
    PluginService::GetInstance()->UnregisterInternalPlugin(it->path);
    break;
  }

  PluginService::GetInstance()->RegisterInternalPlugin(
      plugin_info.ToWebPluginInfo(), true);
  PluginService::GetInstance()->RefreshPlugins();
}

}  // namespace

class PepperFlashComponentInstaller : public update_client::ComponentInstaller {
 public:
  explicit PepperFlashComponentInstaller(const Version& version);

  // ComponentInstaller implementation:
  void OnUpdateError(int error) override;

  bool Install(const base::DictionaryValue& manifest,
               const base::FilePath& unpack_path) override;

  bool GetInstalledFile(const std::string& file,
                        base::FilePath* installed_file) override;

  bool Uninstall() override;

 private:
  ~PepperFlashComponentInstaller() override {}

  Version current_version_;
};

PepperFlashComponentInstaller::PepperFlashComponentInstaller(
    const Version& version)
    : current_version_(version) {
  DCHECK(version.IsValid());
}

void PepperFlashComponentInstaller::OnUpdateError(int error) {
  NOTREACHED() << "Pepper Flash update error: " << error;
}

bool PepperFlashComponentInstaller::Install(
    const base::DictionaryValue& manifest,
    const base::FilePath& unpack_path) {
  Version version;
  if (!chrome::CheckPepperFlashManifest(manifest, &version))
    return false;
  if (current_version_.CompareTo(version) > 0)
    return false;
  if (!base::PathExists(unpack_path.Append(chrome::kPepperFlashPluginFilename)))
    return false;
  // Passed the basic tests. Time to install it.
  base::FilePath path =
      GetPepperFlashBaseDirectory().AppendASCII(version.GetString());
  if (base::PathExists(path))
    return false;
  if (!base::Move(unpack_path, path))
    return false;
  // Installation is done. Now tell the rest of chrome. Both the path service
  // and to the plugin service.
  current_version_ = version;
  PathService::Override(chrome::DIR_PEPPER_FLASH_PLUGIN, path);
  path = path.Append(chrome::kPepperFlashPluginFilename);
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&RegisterPepperFlashWithChrome, path, version));
  return true;
}

bool PepperFlashComponentInstaller::GetInstalledFile(
    const std::string& file,
    base::FilePath* installed_file) {
  return false;
}

bool PepperFlashComponentInstaller::Uninstall() {
  return false;
}



namespace {

#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)
void FinishPepperFlashUpdateRegistration(ComponentUpdateService* cus,
                                         const Version& version) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  update_client::CrxComponent pepflash;
  pepflash.name = "pepper_flash";
  pepflash.installer = new PepperFlashComponentInstaller(version);
  pepflash.version = version;
  pepflash.pk_hash.assign(kSha2Hash, &kSha2Hash[sizeof(kSha2Hash)]);
  if (cus->RegisterComponent(pepflash) != ComponentUpdateService::kOk) {
    NOTREACHED() << "Pepper Flash component registration failed.";
  }
}

void StartPepperFlashUpdateRegistration(ComponentUpdateService* cus) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  base::FilePath path = GetPepperFlashBaseDirectory();
  if (!base::PathExists(path)) {
    if (!base::CreateDirectory(path)) {
      NOTREACHED() << "Could not create Pepper Flash directory.";
      return;
    }
  }

  Version version(kNullVersion);
  std::vector<base::FilePath> older_dirs;
  if (GetPepperFlashDirectory(&path, &version, &older_dirs)) {
    path = path.Append(chrome::kPepperFlashPluginFilename);
    if (base::PathExists(path)) {
      BrowserThread::PostTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&RegisterPepperFlashWithChrome, path, version));
    } else {
      version = Version(kNullVersion);
    }
  }

  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&FinishPepperFlashUpdateRegistration, cus, version));

  // Remove older versions of Pepper Flash.
  for (std::vector<base::FilePath>::iterator iter = older_dirs.begin();
       iter != older_dirs.end();
       ++iter) {
    base::DeleteFile(*iter, true);
  }
}
#endif  // defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)

}  // namespace

void RegisterPepperFlashComponent(ComponentUpdateService* cus) {
#if defined(GOOGLE_CHROME_BUILD) && !defined(OS_LINUX)
  // Component updated flash supersedes bundled flash therefore if that one
  // is disabled then this one should never install.
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(switches::kDisableBundledPpapiFlash))
    return;
  BrowserThread::PostTask(BrowserThread::FILE,
                          FROM_HERE,
                          base::Bind(&StartPepperFlashUpdateRegistration, cus));
#endif
}

}  // namespace component_updater
