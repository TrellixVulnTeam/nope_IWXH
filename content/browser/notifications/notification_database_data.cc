// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/notifications/notification_database_data.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/notifications/notification_database_data.pb.h"

namespace content {

NotificationDatabaseData::NotificationDatabaseData()
    : notification_id(0),
      service_worker_registration_id(0) {
}

NotificationDatabaseData::~NotificationDatabaseData() {}

bool NotificationDatabaseData::ParseFromString(const std::string& input) {
  NotificationDatabaseDataProto message;
  if (!message.ParseFromString(input))
    return false;

  notification_id = message.notification_id();
  origin = GURL(message.origin());
  service_worker_registration_id = message.service_worker_registration_id();

  const NotificationDatabaseDataProto::NotificationData& payload =
      message.notification_data();

  notification_data.title = base::UTF8ToUTF16(payload.title());
  notification_data.direction =
      payload.direction() ==
          NotificationDatabaseDataProto::NotificationData::RIGHT_TO_LEFT ?
              PlatformNotificationData::NotificationDirectionRightToLeft :
              PlatformNotificationData::NotificationDirectionLeftToRight;
  notification_data.lang = payload.lang();
  notification_data.body = base::UTF8ToUTF16(payload.body());
  notification_data.tag = payload.tag();
  notification_data.icon = GURL(payload.icon());
  notification_data.silent = payload.silent();

  return true;
}

bool NotificationDatabaseData::SerializeToString(std::string* output) const {
  DCHECK(output);

  scoped_ptr<NotificationDatabaseDataProto::NotificationData> payload(
      new NotificationDatabaseDataProto::NotificationData());
  payload->set_title(base::UTF16ToUTF8(notification_data.title));
  payload->set_direction(
      notification_data.direction ==
          PlatformNotificationData::NotificationDirectionRightToLeft ?
              NotificationDatabaseDataProto::NotificationData::RIGHT_TO_LEFT :
              NotificationDatabaseDataProto::NotificationData::LEFT_TO_RIGHT);
  payload->set_lang(notification_data.lang);
  payload->set_body(base::UTF16ToUTF8(notification_data.body));
  payload->set_tag(notification_data.tag);
  payload->set_icon(notification_data.icon.spec());
  payload->set_silent(notification_data.silent);

  NotificationDatabaseDataProto message;
  message.set_notification_id(notification_id);
  message.set_origin(origin.spec());
  message.set_service_worker_registration_id(
      service_worker_registration_id);
  message.set_allocated_notification_data(payload.release());

  return message.SerializeToString(output);
}

}  // namespace content
