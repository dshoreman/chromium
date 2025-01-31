// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/detachable_base/detachable_base_notification_controller.h"

#include <memory>
#include <utility>

#include "ash/detachable_base/detachable_base_handler.h"
#include "ash/detachable_base/detachable_base_pairing_status.h"
#include "ash/public/cpp/vector_icons/vector_icons.h"
#include "ash/session/session_controller.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "base/strings/string16.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_types.h"

namespace ash {

namespace {

constexpr char kDetachableBaseNotifierId[] = "ash.system.detachable_base";

}  // namespace

const char DetachableBaseNotificationController::kBaseChangedNotificationId[] =
    "chrome://settings/detachable_base/detachable_base_changed";

DetachableBaseNotificationController::DetachableBaseNotificationController(
    DetachableBaseHandler* detachable_base_handler)
    : detachable_base_handler_(detachable_base_handler),
      detachable_base_observer_(this),
      session_observer_(this) {
  detachable_base_observer_.Add(detachable_base_handler);
  ShowPairingNotificationIfNeeded();
}

DetachableBaseNotificationController::~DetachableBaseNotificationController() =
    default;

void DetachableBaseNotificationController::OnDetachableBasePairingStatusChanged(
    DetachableBasePairingStatus status) {
  ShowPairingNotificationIfNeeded();
}

void DetachableBaseNotificationController::OnActiveUserSessionChanged(
    const AccountId& account_id) {
  // Remove notification shown for the provious user.
  RemovePairingNotification();

  ShowPairingNotificationIfNeeded();
}

void DetachableBaseNotificationController::OnSessionStateChanged(
    session_manager::SessionState state) {
  // Remove the existing notification if the session gets blocked - lock UI
  // displays its own warning for base changes, when needed.
  RemovePairingNotification();

  ShowPairingNotificationIfNeeded();
}

void DetachableBaseNotificationController::ShowPairingNotificationIfNeeded() {
  // Do not show the notification if the session is blocked - login/lock UI have
  // their own UI for notifying the user of the detachable base change.
  if (Shell::Get()->session_controller()->IsUserSessionBlocked())
    return;

  const mojom::UserSession* active_session =
      Shell::Get()->session_controller()->GetUserSession(0);
  if (!active_session || !active_session->user_info)
    return;

  DetachableBasePairingStatus pairing_status =
      detachable_base_handler_->GetPairingStatus();
  if (pairing_status == DetachableBasePairingStatus::kNone)
    return;

  const mojom::UserInfo& user_info = *active_session->user_info;
  if (pairing_status == DetachableBasePairingStatus::kAuthenticated &&
      detachable_base_handler_->PairedBaseMatchesLastUsedByUser(user_info)) {
    // Set the current base as last used by the user.
    // PairedBaseMatchesLastUsedByUser returns true if the user has not
    // previously used a base, so make sure the last used base value is actually
    // set.
    detachable_base_handler_->SetPairedBaseAsLastUsedByUser(user_info);
    return;
  }

  // Remove any previously added notifications to ensure the new notification is
  // shown to the user as a pop-up.
  RemovePairingNotification();

  message_center::RichNotificationData options;
  options.never_timeout = true;
  options.priority = message_center::MAX_PRIORITY;

  base::string16 title = l10n_util::GetStringUTF16(
      IDS_ASH_DETACHABLE_BASE_NOTIFICATION_DEVICE_CHANGED_TITLE);
  base::string16 message = l10n_util::GetStringUTF16(
      IDS_ASH_DETACHABLE_BASE_NOTIFICATION_DEVICE_CHANGED_MESSAGE);

  std::unique_ptr<message_center::Notification> notification =
      message_center::Notification::CreateSystemNotification(
          message_center::NOTIFICATION_TYPE_SIMPLE, kBaseChangedNotificationId,
          title, message, gfx::Image(), base::string16(), GURL(),
          message_center::NotifierId(
              message_center::NotifierId::SYSTEM_COMPONENT,
              kDetachableBaseNotifierId),
          options, nullptr, kNotificationWarningIcon,
          message_center::SystemNotificationWarningLevel::CRITICAL_WARNING);

  message_center::MessageCenter::Get()->AddNotification(
      std::move(notification));

  // At this point the session is unblocked - mark the current base as used by
  // user (as they have just been notified about the base change).
  if (pairing_status == DetachableBasePairingStatus::kAuthenticated)
    detachable_base_handler_->SetPairedBaseAsLastUsedByUser(user_info);
}

void DetachableBaseNotificationController::RemovePairingNotification() {
  message_center::MessageCenter::Get()->RemoveNotification(
      kBaseChangedNotificationId, false);
}

}  // namespace ash
