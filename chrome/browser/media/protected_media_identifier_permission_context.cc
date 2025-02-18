// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/protected_media_identifier_permission_context.h"

#include "base/prefs/pref_service.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/common/permission_request_id.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"

#if defined(OS_CHROMEOS)
#include <utility>

#include "chrome/browser/chromeos/attestation/platform_verification_dialog.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/user_prefs/user_prefs.h"
#include "ui/views/widget/widget.h"

using chromeos::attestation::PlatformVerificationDialog;
#endif

ProtectedMediaIdentifierPermissionContext::
    ProtectedMediaIdentifierPermissionContext(Profile* profile)
    : PermissionContextBase(profile,
                            CONTENT_SETTINGS_TYPE_PROTECTED_MEDIA_IDENTIFIER)
#if defined(OS_CHROMEOS)
      ,
      weak_factory_(this)
#endif
{
}

ProtectedMediaIdentifierPermissionContext::
    ~ProtectedMediaIdentifierPermissionContext() {
}

#if defined(OS_CHROMEOS)
// static
void ProtectedMediaIdentifierPermissionContext::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* prefs) {
  prefs->RegisterBooleanPref(prefs::kRAConsentGranted,
                             false,  // Default value.
                             user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}
#endif

void ProtectedMediaIdentifierPermissionContext::RequestPermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    bool user_gesture,
    const BrowserPermissionCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  GURL embedding_origin = web_contents->GetLastCommittedURL().GetOrigin();

  DVLOG(1) << __FUNCTION__ << ": (" << requesting_origin.spec() << ", "
           << embedding_origin.spec() << ")";

  ContentSetting content_setting =
      GetPermissionStatus(requesting_origin, embedding_origin);

  if (content_setting == CONTENT_SETTING_ALLOW ||
      content_setting == CONTENT_SETTING_BLOCK) {
    NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                        false /* persist */, content_setting);
    return;
  }

  DCHECK_EQ(CONTENT_SETTING_ASK, content_setting);

#if defined(OS_CHROMEOS)
  // Since the dialog is modal, we only support one prompt per |web_contents|.
  // Reject the new one if there is already one pending. See
  // http://crbug.com/447005
  if (pending_requests_.count(web_contents)) {
    callback.Run(CONTENT_SETTING_ASK);
    return;
  }

  // On ChromeOS, we don't use PermissionContextBase::RequestPermission() which
  // uses the standard permission infobar/bubble UI. See http://crbug.com/454847
  // Instead, we show the existing platform verification UI.
  // TODO(xhwang): Remove when http://crbug.com/454847 is fixed.
  views::Widget* widget = PlatformVerificationDialog::ShowDialog(
      web_contents, requesting_origin,
      base::Bind(&ProtectedMediaIdentifierPermissionContext::
                     OnPlatformVerificationConsentResponse,
                 weak_factory_.GetWeakPtr(), web_contents, id,
                 requesting_origin, embedding_origin, callback));
  pending_requests_.insert(
      std::make_pair(web_contents, std::make_pair(widget, id)));
#else
  PermissionContextBase::RequestPermission(web_contents, id, requesting_origin,
                                           user_gesture, callback);
#endif
}

ContentSetting ProtectedMediaIdentifierPermissionContext::GetPermissionStatus(
      const GURL& requesting_origin,
      const GURL& embedding_origin) const {
  DVLOG(1) << __FUNCTION__ << ": (" << requesting_origin.spec() << ", "
           << embedding_origin.spec() << ")";

  if (!requesting_origin.is_valid() || !embedding_origin.is_valid() ||
      !IsProtectedMediaIdentifierEnabled()) {
    return CONTENT_SETTING_BLOCK;
  }

  ContentSetting content_setting = PermissionContextBase::GetPermissionStatus(
      requesting_origin, embedding_origin);
  DCHECK(content_setting == CONTENT_SETTING_ALLOW ||
         content_setting == CONTENT_SETTING_BLOCK ||
         content_setting == CONTENT_SETTING_ASK);

#if defined(OS_CHROMEOS)
  if (content_setting == CONTENT_SETTING_ALLOW) {
    // Check kRAConsentGranted here because it's possible that user dismissed
    // the dialog triggered by RequestPermission() and the content setting is
    // set to "allow" by server sync. In this case, we should still "ask".
    if (profile()->GetPrefs()->GetBoolean(prefs::kRAConsentGranted))
      return CONTENT_SETTING_ALLOW;
    else
      return CONTENT_SETTING_ASK;
  }
#endif

  return content_setting;
}

void ProtectedMediaIdentifierPermissionContext::CancelPermissionRequest(
    content::WebContents* web_contents,
    const PermissionRequestID& id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

#if defined(OS_CHROMEOS)
  PendingRequestMap::iterator request = pending_requests_.find(web_contents);
  if (request == pending_requests_.end() || !request->second.second.Equals(id))
    return;

  // Close the |widget_|. OnPlatformVerificationConsentResponse() will be fired
  // during this process, but since |web_contents| is removed from
  // |pending_requests_|, the callback will simply be dropped.
  views::Widget* widget = request->second.first;
  pending_requests_.erase(request);
  widget->Close();
#else
  PermissionContextBase::CancelPermissionRequest(web_contents, id);
#endif
}

void ProtectedMediaIdentifierPermissionContext::UpdateTabContext(
    const PermissionRequestID& id,
    const GURL& requesting_frame,
    bool allowed) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // WebContents may have gone away.
  TabSpecificContentSettings* content_settings =
      TabSpecificContentSettings::Get(id.render_process_id(),
                                      id.render_view_id());
  if (content_settings) {
    content_settings->OnProtectedMediaIdentifierPermissionSet(
        requesting_frame.GetOrigin(), allowed);
  }
}

// TODO(xhwang): We should consolidate the "protected content" related pref
// across platforms.
bool ProtectedMediaIdentifierPermissionContext::
    IsProtectedMediaIdentifierEnabled() const {
#if defined(OS_ANDROID)
  if (!profile()->GetPrefs()->GetBoolean(
          prefs::kProtectedMediaIdentifierEnabled)) {
    DVLOG(1) << "Protected media identifier disabled by a user master switch.";
    return false;
  }
#elif defined(OS_CHROMEOS)
  // Platform verification is not allowed in incognito or guest mode.
  if (profile()->IsOffTheRecord() || profile()->IsGuestSession()) {
    DVLOG(1) << "Protected media identifier disabled in incognito or guest "
                "mode.";
    return false;
  }

  // This could be disabled by the device policy or by user's master switch.
  bool enabled_for_device = false;
  if (!chromeos::CrosSettings::Get()->GetBoolean(
          chromeos::kAttestationForContentProtectionEnabled,
          &enabled_for_device) ||
      !enabled_for_device ||
      !profile()->GetPrefs()->GetBoolean(prefs::kEnableDRM)) {
    DVLOG(1) << "Protected media identifier disabled by the user or by device "
                "policy.";
    return false;
  }
#endif

  return true;
}

#if defined(OS_CHROMEOS)
static void RecordRAConsentGranted(content::WebContents* web_contents) {
  PrefService* pref_service =
      user_prefs::UserPrefs::Get(web_contents->GetBrowserContext());
  if (!pref_service) {
    LOG(ERROR) << "Failed to get user prefs.";
    return;
  }
  pref_service->SetBoolean(prefs::kRAConsentGranted, true);
}

void ProtectedMediaIdentifierPermissionContext::
    OnPlatformVerificationConsentResponse(
        content::WebContents* web_contents,
        const PermissionRequestID& id,
        const GURL& requesting_origin,
        const GURL& embedding_origin,
        const BrowserPermissionCallback& callback,
        chromeos::attestation::PlatformVerificationDialog::ConsentResponse
            response) {
  // The request may have been canceled. Drop the callback in that case.
  PendingRequestMap::iterator request = pending_requests_.find(web_contents);
  if (request == pending_requests_.end())
    return;

  DCHECK(request->second.second.Equals(id));
  pending_requests_.erase(request);

  ContentSetting content_setting = CONTENT_SETTING_ASK;
  bool persist = false; // Whether the ContentSetting should be saved.
  switch (response) {
    case PlatformVerificationDialog::CONSENT_RESPONSE_NONE:
      content_setting = CONTENT_SETTING_ASK;
      persist = false;
      break;
    case PlatformVerificationDialog::CONSENT_RESPONSE_ALLOW:
      VLOG(1) << "Platform verification accepted by user.";
      content::RecordAction(
          base::UserMetricsAction("PlatformVerificationAccepted"));
      RecordRAConsentGranted(web_contents);
      content_setting = CONTENT_SETTING_ALLOW;
      persist = true;
      break;
    case PlatformVerificationDialog::CONSENT_RESPONSE_DENY:
      VLOG(1) << "Platform verification denied by user.";
      content::RecordAction(
          base::UserMetricsAction("PlatformVerificationRejected"));
      content_setting = CONTENT_SETTING_BLOCK;
      persist = true;
      break;
  }

  NotifyPermissionSet(
      id, requesting_origin, embedding_origin, callback,
      persist, content_setting);
}
#endif
