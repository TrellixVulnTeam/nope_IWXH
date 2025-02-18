// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search/local_ntp_source.h"

#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/search/instant_io_context.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/search_engines/template_url_prepopulate_data.h"
#include "components/search_engines/template_url_service.h"
#include "grit/browser_resources.h"
#include "grit/theme_resources.h"
#include "net/url_request/url_request.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/jstemplate_builder.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/resources/grit/ui_resources.h"
#include "url/gurl.h"

namespace {

// Signifies a locally constructed resource, i.e. not from grit/.
const int kLocalResource = -1;

const char kConfigDataFilename[] = "config.js";

const struct Resource{
  const char* filename;
  int identifier;
  const char* mime_type;
} kResources[] = {
  { "local-ntp.html", IDR_LOCAL_NTP_HTML, "text/html" },
  { "local-ntp.js", IDR_LOCAL_NTP_JS, "application/javascript" },
  { kConfigDataFilename, kLocalResource, "application/javascript" },
  { "local-ntp.css", IDR_LOCAL_NTP_CSS, "text/css" },
  { "images/close_2.png", IDR_CLOSE_2, "image/png" },
  { "images/close_2_hover.png", IDR_CLOSE_2_H, "image/png" },
  { "images/close_2_active.png", IDR_CLOSE_2_P, "image/png" },
  { "images/close_2_white.png", IDR_CLOSE_2_MASK, "image/png" },
  { "images/close_3_mask.png", IDR_CLOSE_3_MASK, "image/png" },
  { "images/google_logo.png", IDR_LOCAL_NTP_IMAGES_LOGO_PNG, "image/png" },
  { "images/white_google_logo.png",
    IDR_LOCAL_NTP_IMAGES_WHITE_LOGO_PNG, "image/png" },
  { "images/ntp_default_favicon.png", IDR_NTP_DEFAULT_FAVICON, "image/png" },
};

// Strips any query parameters from the specified path.
std::string StripParameters(const std::string& path) {
  return path.substr(0, path.find("?"));
}

bool DefaultSearchProviderIsGoogle(Profile* profile) {
  if (!profile)
    return false;

  TemplateURLService* template_url_service =
      TemplateURLServiceFactory::GetForProfile(profile);
  if (!template_url_service)
    return false;

  const TemplateURL* default_provider =
      template_url_service->GetDefaultSearchProvider();
  return default_provider &&
      (TemplateURLPrepopulateData::GetEngineType(
          *default_provider, template_url_service->search_terms_data()) ==
       SEARCH_ENGINE_GOOGLE);
}

// Adds a localized string keyed by resource id to the dictionary.
void AddString(base::DictionaryValue* dictionary,
               const std::string& key,
               int resource_id) {
  dictionary->SetString(key, l10n_util::GetStringUTF16(resource_id));
}

// Adds a localized string for the Google searchbox placeholder text.
void AddGoogleSearchboxPlaceholderString(base::DictionaryValue* dictionary) {
  base::string16 placeholder = l10n_util::GetStringFUTF16(
      IDS_OMNIBOX_EMPTY_HINT_WITH_DEFAULT_SEARCH_PROVIDER,
      base::ASCIIToUTF16("Google"));
  dictionary->SetString("searchboxPlaceholder", placeholder);
}

// Populates |translated_strings| dictionary for the local NTP. |is_google|
// indicates that this page is the Google Local NTP.
scoped_ptr<base::DictionaryValue> GetTranslatedStrings(bool is_google) {
  scoped_ptr<base::DictionaryValue> translated_strings(
      new base::DictionaryValue());

  AddString(translated_strings.get(), "thumbnailRemovedNotification",
            IDS_NEW_TAB_THUMBNAIL_REMOVED_NOTIFICATION);
  AddString(translated_strings.get(), "removeThumbnailTooltip",
            IDS_NEW_TAB_REMOVE_THUMBNAIL_TOOLTIP);
  AddString(translated_strings.get(), "undoThumbnailRemove",
            IDS_NEW_TAB_UNDO_THUMBNAIL_REMOVE);
  AddString(translated_strings.get(), "restoreThumbnailsShort",
            IDS_NEW_TAB_RESTORE_THUMBNAILS_SHORT_LINK);
  AddString(translated_strings.get(), "attributionIntro",
            IDS_NEW_TAB_ATTRIBUTION_INTRO);
  AddString(translated_strings.get(), "title", IDS_NEW_TAB_TITLE);
  if (is_google)
    AddGoogleSearchboxPlaceholderString(translated_strings.get());

  return translated_strings.Pass();
}

// Returns a JS dictionary of configuration data for the local NTP.
std::string GetConfigData(Profile* profile) {
  base::DictionaryValue config_data;
  bool is_google = DefaultSearchProviderIsGoogle(profile) &&
      chrome::ShouldShowGoogleLocalNTP();
  config_data.Set("translatedStrings",
                  GetTranslatedStrings(is_google).release());
  config_data.SetBoolean("isGooglePage", is_google);

  // Serialize the dictionary.
  std::string js_text;
  JSONStringValueSerializer serializer(&js_text);
  serializer.Serialize(config_data);

  std::string config_data_js;
  config_data_js.append("var configData = ");
  config_data_js.append(js_text);
  config_data_js.append(";");
  return config_data_js;
}

std::string GetLocalNtpPath() {
  return std::string(chrome::kChromeSearchScheme) + "://" +
         std::string(chrome::kChromeSearchLocalNtpHost) + "/";
}

}  // namespace

LocalNtpSource::LocalNtpSource(Profile* profile) : profile_(profile) {
}

LocalNtpSource::~LocalNtpSource() {
}

std::string LocalNtpSource::GetSource() const {
  return chrome::kChromeSearchLocalNtpHost;
}

void LocalNtpSource::StartDataRequest(
    const std::string& path,
    int render_process_id,
    int render_frame_id,
    const content::URLDataSource::GotDataCallback& callback) {
  const std::string stripped_path = StripParameters(path);
  if (stripped_path == kConfigDataFilename) {
    std::string config_data_js = GetConfigData(profile_);
    callback.Run(base::RefCountedString::TakeString(&config_data_js));
    return;
  }
  float scale = 1.0f;
  std::string filename;
  webui::ParsePathAndScale(
      GURL(GetLocalNtpPath() + stripped_path), &filename, &scale);
  ui::ScaleFactor scale_factor = ui::GetSupportedScaleFactor(scale);
  for (size_t i = 0; i < arraysize(kResources); ++i) {
    if (filename == kResources[i].filename) {
      scoped_refptr<base::RefCountedStaticMemory> response(
          ResourceBundle::GetSharedInstance().LoadDataResourceBytesForScale(
              kResources[i].identifier, scale_factor));
      callback.Run(response.get());
      return;
    }
  }
  callback.Run(NULL);
}

std::string LocalNtpSource::GetMimeType(
    const std::string& path) const {
  const std::string& stripped_path = StripParameters(path);
  std::string filename;
  webui::ParsePathAndScale(GURL(GetLocalNtpPath() + stripped_path), &filename,
                           NULL);
  for (size_t i = 0; i < arraysize(kResources); ++i) {
    if (filename == kResources[i].filename)
      return kResources[i].mime_type;
  }
  return std::string();
}

bool LocalNtpSource::ShouldServiceRequest(
    const net::URLRequest* request) const {
  DCHECK(request->url().host() == chrome::kChromeSearchLocalNtpHost);
  if (!InstantIOContext::ShouldServiceRequest(request))
    return false;

  if (request->url().SchemeIs(chrome::kChromeSearchScheme)) {
    std::string filename;
    webui::ParsePathAndScale(request->url(), &filename, NULL);
    for (size_t i = 0; i < arraysize(kResources); ++i) {
      if (filename == kResources[i].filename)
        return true;
    }
  }
  return false;
}

std::string LocalNtpSource::GetContentSecurityPolicyFrameSrc() const {
  // Allow embedding of most visited iframes.
  return base::StringPrintf("frame-src %s;",
                            chrome::kChromeSearchMostVisitedUrl);
}
