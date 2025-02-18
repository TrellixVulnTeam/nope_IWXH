// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/content_settings/core/browser/host_content_settings_map.h"

#include <utility>

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/prefs/pref_service.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/clock.h"
#include "components/content_settings/core/browser/content_settings_default_provider.h"
#include "components/content_settings/core/browser/content_settings_details.h"
#include "components/content_settings/core/browser/content_settings_observable_provider.h"
#include "components/content_settings/core/browser/content_settings_policy_provider.h"
#include "components/content_settings/core/browser/content_settings_pref_provider.h"
#include "components/content_settings/core/browser/content_settings_provider.h"
#include "components/content_settings/core/browser/content_settings_rule.h"
#include "components/content_settings/core/browser/content_settings_utils.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "net/base/net_errors.h"
#include "net/base/static_cookie_policy.h"
#include "url/gurl.h"

namespace {

typedef std::vector<content_settings::Rule> Rules;

typedef std::pair<std::string, std::string> StringPair;

// TODO(bauerb): Expose constants.
const char* kProviderNames[] = {
  "platform_app",
  "policy",
  "supervised_user",
  "extension",
  "override",
  "preference",
  "default"
};

// These constants are copied from extensions/common/extension_constants.h and
// content/public/common/url_constants.h to avoid complicated dependencies.
// TODO(vabr): Get these constants through the ContentSettingsClient.
const char kChromeDevToolsScheme[] = "chrome-devtools";
const char kChromeUIScheme[] = "chrome";

#if defined(ENABLE_EXTENSIONS)
const char kExtensionScheme[] = "chrome-extension";
#endif

content_settings::SettingSource kProviderSourceMap[] = {
  content_settings::SETTING_SOURCE_EXTENSION,
  content_settings::SETTING_SOURCE_POLICY,
  content_settings::SETTING_SOURCE_SUPERVISED,
  content_settings::SETTING_SOURCE_EXTENSION,
  content_settings::SETTING_SOURCE_USER,
  content_settings::SETTING_SOURCE_USER,
  content_settings::SETTING_SOURCE_USER,
};
static_assert(arraysize(kProviderSourceMap) ==
                   HostContentSettingsMap::NUM_PROVIDER_TYPES,
              "kProviderSourceMap should have NUM_PROVIDER_TYPES elements");

// Returns true if the |content_type| supports a resource identifier.
// Resource identifiers are supported (but not required) for plug-ins.
bool SupportsResourceIdentifier(ContentSettingsType content_type) {
  return content_type == CONTENT_SETTINGS_TYPE_PLUGINS;
}

}  // namespace

HostContentSettingsMap::HostContentSettingsMap(PrefService* prefs,
                                               bool incognito)
    :
#ifndef NDEBUG
      used_from_thread_id_(base::PlatformThread::CurrentId()),
#endif
      prefs_(prefs),
      is_off_the_record_(incognito) {
  content_settings::ObservableProvider* policy_provider =
      new content_settings::PolicyProvider(prefs_);
  policy_provider->AddObserver(this);
  content_settings_providers_[POLICY_PROVIDER] = policy_provider;

  content_settings::ObservableProvider* pref_provider =
      new content_settings::PrefProvider(prefs_, is_off_the_record_);
  pref_provider->AddObserver(this);
  content_settings_providers_[PREF_PROVIDER] = pref_provider;

  content_settings::ObservableProvider* default_provider =
      new content_settings::DefaultProvider(prefs_, is_off_the_record_);
  default_provider->AddObserver(this);
  content_settings_providers_[DEFAULT_PROVIDER] = default_provider;

  content_settings_providers_[OVERRIDE_PROVIDER] =
      new content_settings::OverrideProvider(prefs_, is_off_the_record_);
}

// static
void HostContentSettingsMap::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(
      prefs::kContentSettingsWindowLastTabIndex,
      0,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);

  // Register the prefs for the content settings providers.
  content_settings::DefaultProvider::RegisterProfilePrefs(registry);
  content_settings::PrefProvider::RegisterProfilePrefs(registry);
  content_settings::PolicyProvider::RegisterProfilePrefs(registry);
  content_settings::OverrideProvider::RegisterProfilePrefs(registry);
}

void HostContentSettingsMap::RegisterProvider(
    ProviderType type,
    scoped_ptr<content_settings::ObservableProvider> provider) {
  DCHECK(!content_settings_providers_[type]);
  provider->AddObserver(this);
  content_settings_providers_[type] = provider.release();

#ifndef NDEBUG
  DCHECK_NE(used_from_thread_id_, base::kInvalidThreadId)
      << "Used from multiple threads before initialization complete.";
#endif

  OnContentSettingChanged(ContentSettingsPattern(),
                          ContentSettingsPattern(),
                          CONTENT_SETTINGS_TYPE_DEFAULT,
                          std::string());
}

ContentSetting HostContentSettingsMap::GetDefaultContentSettingFromProvider(
    ContentSettingsType content_type,
    content_settings::ProviderInterface* provider) const {
  scoped_ptr<content_settings::RuleIterator> rule_iterator(
      provider->GetRuleIterator(content_type, std::string(), false));

  ContentSettingsPattern wildcard = ContentSettingsPattern::Wildcard();
  while (rule_iterator->HasNext()) {
    content_settings::Rule rule = rule_iterator->Next();
    if (rule.primary_pattern == wildcard &&
        rule.secondary_pattern == wildcard) {
      return content_settings::ValueToContentSetting(rule.value.get());
    }
  }
  return CONTENT_SETTING_DEFAULT;
}

ContentSetting HostContentSettingsMap::GetDefaultContentSetting(
    ContentSettingsType content_type,
    std::string* provider_id) const {
  UsedContentSettingsProviders();

  // Iterate through the list of providers and return the first non-NULL value
  // that matches |primary_url| and |secondary_url|.
  for (ConstProviderIterator provider = content_settings_providers_.begin();
       provider != content_settings_providers_.end();
       ++provider) {
    if (provider->first == PREF_PROVIDER ||
        provider->first == OVERRIDE_PROVIDER)
      continue;
    ContentSetting default_setting =
        GetDefaultContentSettingFromProvider(content_type, provider->second);
    if (default_setting != CONTENT_SETTING_DEFAULT) {
      if (provider_id)
        *provider_id = kProviderNames[provider->first];
      return default_setting;
    }
  }

  return CONTENT_SETTING_DEFAULT;
}

ContentSetting HostContentSettingsMap::GetContentSetting(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    const std::string& resource_identifier) const {
  DCHECK(!ContentTypeHasCompoundValue(content_type));
  scoped_ptr<base::Value> value = GetWebsiteSetting(
      primary_url, secondary_url, content_type, resource_identifier, NULL);
  return content_settings::ValueToContentSetting(value.get());
}

void HostContentSettingsMap::GetSettingsForOneType(
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    ContentSettingsForOneType* settings) const {
  DCHECK(SupportsResourceIdentifier(content_type) ||
         resource_identifier.empty());
  DCHECK(settings);
  UsedContentSettingsProviders();

  settings->clear();
  for (ConstProviderIterator provider = content_settings_providers_.begin();
       provider != content_settings_providers_.end();
       ++provider) {
    if (provider->first == OVERRIDE_PROVIDER)
      continue;
    // For each provider, iterate first the incognito-specific rules, then the
    // normal rules.
    if (is_off_the_record_) {
      AddSettingsForOneType(provider->second,
                            provider->first,
                            content_type,
                            resource_identifier,
                            settings,
                            true);
    }
    AddSettingsForOneType(provider->second,
                          provider->first,
                          content_type,
                          resource_identifier,
                          settings,
                          false);
  }
}

void HostContentSettingsMap::SetDefaultContentSetting(
    ContentSettingsType content_type,
    ContentSetting setting) {
  DCHECK(IsSettingAllowedForType(prefs_, setting, content_type));

  base::Value* value = NULL;
  if (setting != CONTENT_SETTING_DEFAULT)
    value = new base::FundamentalValue(setting);
  SetWebsiteSetting(
      ContentSettingsPattern::Wildcard(),
      ContentSettingsPattern::Wildcard(),
      content_type,
      std::string(),
      value);
}

void HostContentSettingsMap::SetWebsiteSetting(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    base::Value* value) {
  DCHECK(IsValueAllowedForType(prefs_, value, content_type));
  DCHECK(SupportsResourceIdentifier(content_type) ||
         resource_identifier.empty());
  UsedContentSettingsProviders();

  for (ProviderIterator provider = content_settings_providers_.begin();
       provider != content_settings_providers_.end();
       ++provider) {
    if (provider->second->SetWebsiteSetting(primary_pattern,
                                            secondary_pattern,
                                            content_type,
                                            resource_identifier,
                                            value)) {
      return;
    }
  }
  NOTREACHED();
}

void HostContentSettingsMap::SetNarrowestWebsiteSetting(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    ContentSetting setting,
    content_settings::SettingInfo existing_info) {
  ContentSettingsPattern narrow_primary = primary_pattern;
  ContentSettingsPattern narrow_secondary = secondary_pattern;

  DCHECK_EQ(content_settings::SETTING_SOURCE_USER, existing_info.source);
  ContentSettingsPattern::Relation r1 =
      existing_info.primary_pattern.Compare(primary_pattern);
  if (r1 == ContentSettingsPattern::PREDECESSOR) {
    narrow_primary = existing_info.primary_pattern;
  } else if (r1 == ContentSettingsPattern::IDENTITY) {
    ContentSettingsPattern::Relation r2 =
        existing_info.secondary_pattern.Compare(secondary_pattern);
    DCHECK(r2 != ContentSettingsPattern::DISJOINT_ORDER_POST &&
           r2 != ContentSettingsPattern::DISJOINT_ORDER_PRE);
    if (r2 == ContentSettingsPattern::PREDECESSOR)
      narrow_secondary = existing_info.secondary_pattern;
  }

  SetContentSetting(
      narrow_primary, narrow_secondary, content_type, std::string(), setting);
}

void HostContentSettingsMap::SetContentSetting(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    ContentSetting setting) {
  DCHECK(!ContentTypeHasCompoundValue(content_type));

  if (setting == CONTENT_SETTING_ALLOW &&
      (content_type == CONTENT_SETTINGS_TYPE_GEOLOCATION ||
       content_type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS)) {
    UpdateLastUsageByPattern(primary_pattern, secondary_pattern, content_type);
  }

  base::Value* value = NULL;
  if (setting != CONTENT_SETTING_DEFAULT)
    value = new base::FundamentalValue(setting);
  SetWebsiteSetting(primary_pattern,
                    secondary_pattern,
                    content_type,
                    resource_identifier,
                    value);
}

ContentSetting HostContentSettingsMap::GetContentSettingAndMaybeUpdateLastUsage(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    const std::string& resource_identifier) {
  DCHECK(thread_checker_.CalledOnValidThread());

  ContentSetting setting = GetContentSetting(
      primary_url, secondary_url, content_type, resource_identifier);
  if (setting == CONTENT_SETTING_ALLOW) {
    UpdateLastUsageByPattern(
        ContentSettingsPattern::FromURLNoWildcard(primary_url),
        ContentSettingsPattern::FromURLNoWildcard(secondary_url),
        content_type);
  }
  return setting;
}

void HostContentSettingsMap::UpdateLastUsage(const GURL& primary_url,
                                             const GURL& secondary_url,
                                             ContentSettingsType content_type) {
  UpdateLastUsageByPattern(
      ContentSettingsPattern::FromURLNoWildcard(primary_url),
      ContentSettingsPattern::FromURLNoWildcard(secondary_url),
      content_type);
}

void HostContentSettingsMap::UpdateLastUsageByPattern(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type) {
  UsedContentSettingsProviders();

  GetPrefProvider()->UpdateLastUsage(
      primary_pattern, secondary_pattern, content_type);

  FOR_EACH_OBSERVER(
      content_settings::Observer,
      observers_,
      OnContentSettingUsed(primary_pattern, secondary_pattern, content_type));
}

base::Time HostContentSettingsMap::GetLastUsage(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type) {
  return GetLastUsageByPattern(
      ContentSettingsPattern::FromURLNoWildcard(primary_url),
      ContentSettingsPattern::FromURLNoWildcard(secondary_url),
      content_type);
}

base::Time HostContentSettingsMap::GetLastUsageByPattern(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type) {
  UsedContentSettingsProviders();

  return GetPrefProvider()->GetLastUsage(
      primary_pattern, secondary_pattern, content_type);
}

ContentSetting HostContentSettingsMap::GetContentSettingWithoutOverride(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    const std::string& resource_identifier) {
  scoped_ptr<base::Value> value(GetWebsiteSettingWithoutOverride(
      primary_url, secondary_url, content_type, resource_identifier, NULL));
  return content_settings::ValueToContentSetting(value.get());
}

scoped_ptr<base::Value>
HostContentSettingsMap::GetWebsiteSettingWithoutOverride(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    content_settings::SettingInfo* info) const {
  return GetWebsiteSettingInternal(primary_url,
                                   secondary_url,
                                   content_type,
                                   resource_identifier,
                                   info,
                                   false);
}

void HostContentSettingsMap::SetContentSettingOverride(
    ContentSettingsType content_type,
    bool is_enabled) {
  UsedContentSettingsProviders();

  content_settings::OverrideProvider* override =
      static_cast<content_settings::OverrideProvider*>(
          content_settings_providers_[OVERRIDE_PROVIDER]);
  override->SetOverrideSetting(content_type, is_enabled);
}

bool HostContentSettingsMap::GetContentSettingOverride(
    ContentSettingsType content_type) {
  UsedContentSettingsProviders();

  content_settings::OverrideProvider* override =
      static_cast<content_settings::OverrideProvider*>(
          content_settings_providers_[OVERRIDE_PROVIDER]);
  return override->IsEnabled(content_type);
}

void HostContentSettingsMap::AddObserver(content_settings::Observer* observer) {
  observers_.AddObserver(observer);
}

void HostContentSettingsMap::RemoveObserver(
    content_settings::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void HostContentSettingsMap::SetPrefClockForTesting(
    scoped_ptr<base::Clock> clock) {
  UsedContentSettingsProviders();

  GetPrefProvider()->SetClockForTesting(clock.Pass());
}

void HostContentSettingsMap::AddExceptionForURL(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    ContentSetting setting) {
  // TODO(markusheintz): Until the UI supports pattern pairs, both urls must
  // match.
  DCHECK(primary_url == secondary_url);
  DCHECK(!ContentTypeHasCompoundValue(content_type));

  // Make sure there is no entry that would override the pattern we are about
  // to insert for exactly this URL.
  SetContentSetting(ContentSettingsPattern::FromURLNoWildcard(primary_url),
                    ContentSettingsPattern::Wildcard(),
                    content_type,
                    std::string(),
                    CONTENT_SETTING_DEFAULT);

  SetContentSetting(ContentSettingsPattern::FromURL(primary_url),
                    ContentSettingsPattern::Wildcard(),
                    content_type,
                    std::string(),
                    setting);
}

void HostContentSettingsMap::ClearSettingsForOneType(
    ContentSettingsType content_type) {
  UsedContentSettingsProviders();
  for (ProviderIterator provider = content_settings_providers_.begin();
       provider != content_settings_providers_.end();
       ++provider) {
    provider->second->ClearAllContentSettingsRules(content_type);
  }
}

bool HostContentSettingsMap::IsValueAllowedForType(
    PrefService* prefs, const base::Value* value, ContentSettingsType type) {
  return ContentTypeHasCompoundValue(type) || IsSettingAllowedForType(
      prefs, content_settings::ValueToContentSetting(value), type);
}

// static
bool HostContentSettingsMap::IsSettingAllowedForType(
    PrefService* prefs,
    ContentSetting setting,
    ContentSettingsType content_type) {
  // We don't yet support stored content settings for mixed scripting.
  if (content_type == CONTENT_SETTINGS_TYPE_MIXEDSCRIPT)
    return false;

  // BLOCK semantics are not implemented for fullscreen.
  if (content_type == CONTENT_SETTINGS_TYPE_FULLSCREEN &&
      setting == CONTENT_SETTING_BLOCK) {
    return false;
  }

  // We don't support ALLOW for media default setting.
  if (content_type == CONTENT_SETTINGS_TYPE_MEDIASTREAM &&
      setting == CONTENT_SETTING_ALLOW) {
    return false;
  }

  // App banners store a dictionary.
  if (content_type == CONTENT_SETTINGS_TYPE_APP_BANNER)
    return false;

  // DEFAULT, ALLOW and BLOCK are always allowed.
  if (setting == CONTENT_SETTING_DEFAULT ||
      setting == CONTENT_SETTING_ALLOW ||
      setting == CONTENT_SETTING_BLOCK) {
    return true;
  }
  switch (content_type) {
    case CONTENT_SETTINGS_TYPE_COOKIES:
      return setting == CONTENT_SETTING_SESSION_ONLY;
    case CONTENT_SETTINGS_TYPE_PLUGINS:
      return setting == CONTENT_SETTING_ASK ||
             setting == CONTENT_SETTING_DETECT_IMPORTANT_CONTENT;
    case CONTENT_SETTINGS_TYPE_GEOLOCATION:
    case CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
    case CONTENT_SETTINGS_TYPE_FULLSCREEN:
    case CONTENT_SETTINGS_TYPE_MOUSELOCK:
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM:
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC:
    case CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA:
    case CONTENT_SETTINGS_TYPE_PPAPI_BROKER:
    case CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS:
    case CONTENT_SETTINGS_TYPE_MIDI_SYSEX:
    case CONTENT_SETTINGS_TYPE_PUSH_MESSAGING:
      return setting == CONTENT_SETTING_ASK;
    default:
      return false;
  }
}

// static
bool HostContentSettingsMap::ContentTypeHasCompoundValue(
    ContentSettingsType type) {
  // Values for content type CONTENT_SETTINGS_TYPE_AUTO_SELECT_CERTIFICATE,
  // CONTENT_SETTINGS_TYPE_MEDIASTREAM, and
  // CONTENT_SETTINGS_TYPE_SSL_CERT_DECISIONS are of type dictionary/map.
  // Compound types like dictionaries can't be mapped to the type
  // |ContentSetting|.
  if (type == CONTENT_SETTINGS_TYPE_APP_BANNER)
    return true;

  return (type == CONTENT_SETTINGS_TYPE_AUTO_SELECT_CERTIFICATE ||
          type == CONTENT_SETTINGS_TYPE_MEDIASTREAM ||
          type == CONTENT_SETTINGS_TYPE_SSL_CERT_DECISIONS);
}

void HostContentSettingsMap::OnContentSettingChanged(
    const ContentSettingsPattern& primary_pattern,
    const ContentSettingsPattern& secondary_pattern,
    ContentSettingsType content_type,
    std::string resource_identifier) {
  FOR_EACH_OBSERVER(content_settings::Observer,
                    observers_,
                    OnContentSettingChanged(primary_pattern,
                                            secondary_pattern,
                                            content_type,
                                            resource_identifier));
}

HostContentSettingsMap::~HostContentSettingsMap() {
  DCHECK(!prefs_);
  STLDeleteValues(&content_settings_providers_);
}

void HostContentSettingsMap::ShutdownOnUIThread() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(prefs_);
  prefs_ = NULL;
  for (ProviderIterator it = content_settings_providers_.begin();
       it != content_settings_providers_.end();
       ++it) {
    it->second->ShutdownOnUIThread();
  }
}

void HostContentSettingsMap::AddSettingsForOneType(
    const content_settings::ProviderInterface* provider,
    ProviderType provider_type,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    ContentSettingsForOneType* settings,
    bool incognito) const {
  scoped_ptr<content_settings::RuleIterator> rule_iterator(
      provider->GetRuleIterator(content_type,
                                resource_identifier,
                                incognito));
  while (rule_iterator->HasNext()) {
    const content_settings::Rule& rule = rule_iterator->Next();
    ContentSetting setting_value = CONTENT_SETTING_DEFAULT;
    // TODO(bauerb): Return rules as a list of values, not content settings.
    // Handle the case using compound values for its exceptions and arbitrary
    // values for its default setting. Here we assume all the exceptions
    // are granted as |CONTENT_SETTING_ALLOW|.
    if (ContentTypeHasCompoundValue(content_type) &&
        rule.value.get() &&
        rule.primary_pattern != ContentSettingsPattern::Wildcard()) {
      setting_value = CONTENT_SETTING_ALLOW;
    } else {
      setting_value = content_settings::ValueToContentSetting(rule.value.get());
    }
    settings->push_back(ContentSettingPatternSource(
        rule.primary_pattern, rule.secondary_pattern,
        setting_value,
        kProviderNames[provider_type],
        incognito));
  }
}

void HostContentSettingsMap::UsedContentSettingsProviders() const {
#ifndef NDEBUG
  if (used_from_thread_id_ == base::kInvalidThreadId)
    return;

  if (base::PlatformThread::CurrentId() != used_from_thread_id_)
    used_from_thread_id_ = base::kInvalidThreadId;
#endif
}

bool HostContentSettingsMap::ShouldAllowAllContent(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type) {
  if (content_type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS ||
      content_type == CONTENT_SETTINGS_TYPE_GEOLOCATION ||
      content_type == CONTENT_SETTINGS_TYPE_MIDI_SYSEX) {
    return false;
  }
#if defined(OS_ANDROID) || defined(OS_CHROMEOS)
  if (content_type == CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER) {
    return false;
  }
#endif
  if (secondary_url.SchemeIs(kChromeUIScheme) &&
      content_type == CONTENT_SETTINGS_TYPE_COOKIES &&
      primary_url.SchemeIsSecure()) {
    return true;
  }
#if defined(ENABLE_EXTENSIONS)
  if (primary_url.SchemeIs(kExtensionScheme)) {
    switch (content_type) {
      case CONTENT_SETTINGS_TYPE_PLUGINS:
      case CONTENT_SETTINGS_TYPE_MEDIASTREAM:
      case CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC:
      case CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA:
        return false;
      case CONTENT_SETTINGS_TYPE_COOKIES:
        return secondary_url.SchemeIs(kExtensionScheme);
      default:
        return true;
    }
  }
#endif
  return primary_url.SchemeIs(kChromeDevToolsScheme) ||
         primary_url.SchemeIs(kChromeUIScheme);
}

scoped_ptr<base::Value> HostContentSettingsMap::GetWebsiteSetting(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    content_settings::SettingInfo* info) const {
  DCHECK(SupportsResourceIdentifier(content_type) ||
         resource_identifier.empty());

  // Check if the scheme of the requesting url is whitelisted.
  if (ShouldAllowAllContent(primary_url, secondary_url, content_type)) {
    if (info) {
      info->source = content_settings::SETTING_SOURCE_WHITELIST;
      info->primary_pattern = ContentSettingsPattern::Wildcard();
      info->secondary_pattern = ContentSettingsPattern::Wildcard();
    }
    return scoped_ptr<base::Value>(
        new base::FundamentalValue(CONTENT_SETTING_ALLOW));
  }

  return GetWebsiteSettingInternal(primary_url,
                                   secondary_url,
                                   content_type,
                                   resource_identifier,
                                   info,
                                   true);
}

// static
HostContentSettingsMap::ProviderType
HostContentSettingsMap::GetProviderTypeFromSource(const std::string& source) {
  for (size_t i = 0; i < arraysize(kProviderNames); ++i) {
    if (source == kProviderNames[i])
      return static_cast<ProviderType>(i);
  }

  NOTREACHED();
  return DEFAULT_PROVIDER;
}

content_settings::PrefProvider* HostContentSettingsMap::GetPrefProvider() {
  return static_cast<content_settings::PrefProvider*>(
      content_settings_providers_[PREF_PROVIDER]);
}

scoped_ptr<base::Value> HostContentSettingsMap::GetWebsiteSettingInternal(
    const GURL& primary_url,
    const GURL& secondary_url,
    ContentSettingsType content_type,
    const std::string& resource_identifier,
    content_settings::SettingInfo* info,
    bool get_override) const {
  UsedContentSettingsProviders();
  ContentSettingsPattern* primary_pattern = NULL;
  ContentSettingsPattern* secondary_pattern = NULL;
  if (info) {
    primary_pattern = &info->primary_pattern;
    secondary_pattern = &info->secondary_pattern;
  }

  // The list of |content_settings_providers_| is ordered according to their
  // precedence.
  for (ConstProviderIterator provider = content_settings_providers_.begin();
       provider != content_settings_providers_.end();
       ++provider) {
    if (!get_override && provider->first == OVERRIDE_PROVIDER)
      continue;

    scoped_ptr<base::Value> value(
        content_settings::GetContentSettingValueAndPatterns(provider->second,
                                                            primary_url,
                                                            secondary_url,
                                                            content_type,
                                                            resource_identifier,
                                                            is_off_the_record_,
                                                            primary_pattern,
                                                            secondary_pattern));
    if (value) {
      if (info)
        info->source = kProviderSourceMap[provider->first];
      return value.Pass();
    }
  }

  if (info) {
    info->source = content_settings::SETTING_SOURCE_NONE;
    info->primary_pattern = ContentSettingsPattern();
    info->secondary_pattern = ContentSettingsPattern();
  }
  return scoped_ptr<base::Value>();
}
