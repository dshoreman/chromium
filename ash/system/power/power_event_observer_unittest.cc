// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/power/power_event_observer.h"

#include <memory>

#include "ash/session/session_controller.h"
#include "ash/shell.h"
#include "ash/system/power/power_event_observer_test_api.h"
#include "ash/test/ash_test_base.h"
#include "ash/wm/lock_state_controller.h"
#include "ash/wm/lock_state_controller_test_api.h"
#include "ash/wm/test_session_state_animator.h"
#include "base/time/time.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager/suspend.pb.h"
#include "chromeos/dbus/power_manager_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/compositor/compositor.h"

namespace ash {

class PowerEventObserverTest : public AshTestBase {
 public:
  PowerEventObserverTest() = default;
  ~PowerEventObserverTest() override = default;

  // AshTestBase:
  void SetUp() override {
    chromeos::DBusThreadManager::Initialize();
    AshTestBase::SetUp();
    observer_.reset(new PowerEventObserver());
  }

  void TearDown() override {
    observer_.reset();
    AshTestBase::TearDown();
    chromeos::DBusThreadManager::Shutdown();
  }

 protected:
  int GetNumVisibleCompositors() {
    int result = 0;
    for (auto* window : Shell::GetAllRootWindows()) {
      if (window->GetHost()->compositor()->IsVisible())
        ++result;
    }

    return result;
  }

  bool GetLockedState() {
    // LockScreen is an async mojo call.
    SessionController* const session_controller =
        Shell::Get()->session_controller();
    session_controller->FlushMojoForTest();
    return session_controller->IsScreenLocked();
  }

  std::unique_ptr<PowerEventObserver> observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PowerEventObserverTest);
};

TEST_F(PowerEventObserverTest, LockBeforeSuspend) {
  chromeos::PowerManagerClient* client =
      chromeos::DBusThreadManager::Get()->GetPowerManagerClient();
  ASSERT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());

  // Check that the observer requests a suspend-readiness callback when it hears
  // that the system is about to suspend.
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());

  // It should run the callback when it hears that the screen is locked and the
  // lock screen animations have completed.
  BlockUserSession(BLOCKED_BY_LOCK_SCREEN);

  PowerEventObserverTestApi test_api(observer_.get());

  ui::Compositor* compositor =
      Shell::GetPrimaryRootWindow()->GetHost()->compositor();

  test_api.CompositingDidCommit(compositor);
  observer_->OnLockAnimationsComplete();

  // Verify that CompositingStarted and CompositingEnded observed before
  // CompositingDidCommit are ignored.
  test_api.CompositingStarted(compositor);
  test_api.CompositingEnded(compositor);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(1, GetNumVisibleCompositors());

  // Suspend should remain delayed after first compositing cycle ends.
  test_api.CompositeFrame(compositor);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(1, GetNumVisibleCompositors());

  test_api.CompositingDidCommit(compositor);
  test_api.CompositingStarted(compositor);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(1, GetNumVisibleCompositors());

  test_api.CompositingEnded(compositor);
  EXPECT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());

  // If the system is already locked, no callback should be requested.
  observer_->SuspendDone(base::TimeDelta());
  EXPECT_EQ(1, GetNumVisibleCompositors());
  UnblockUserSession();
  BlockUserSession(BLOCKED_BY_LOCK_SCREEN);

  // Notify that lock animation is complete.
  observer_->OnLockAnimationsComplete();

  // Wait for a compositing after lock animation completes before suspending.
  // In this case compositors should be made invisible immediately
  test_api.CompositeFrame(compositor);
  test_api.CompositeFrame(compositor);

  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());

  // It also shouldn't request a callback if it isn't instructed to lock the
  // screen.
  observer_->SuspendDone(base::TimeDelta());
  UnblockUserSession();
  SetShouldLockScreenAutomatically(false);
  EXPECT_EQ(1, GetNumVisibleCompositors());
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());
}

TEST_F(PowerEventObserverTest, SetInvisibleBeforeSuspend) {
  // Tests that all the Compositors are marked invisible before a suspend
  // request when the screen is not supposed to be locked before a suspend.
  EXPECT_EQ(1, GetNumVisibleCompositors());

  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(0, GetNumVisibleCompositors());
  observer_->SuspendDone(base::TimeDelta());

  // Tests that all the Compositors are marked invisible _after_ the screen lock
  // animations have completed.
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);

  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, GetNumVisibleCompositors());

  BlockUserSession(BLOCKED_BY_LOCK_SCREEN);
  EXPECT_EQ(1, GetNumVisibleCompositors());

  observer_->OnLockAnimationsComplete();

  EXPECT_EQ(1, GetNumVisibleCompositors());
  ASSERT_TRUE(PowerEventObserverTestApi(observer_.get())
                  .SimulateCompositorsReadyForSuspend());
  EXPECT_EQ(0, GetNumVisibleCompositors());

  observer_->SuspendDone(base::TimeDelta());
  EXPECT_EQ(1, GetNumVisibleCompositors());
}

TEST_F(PowerEventObserverTest, CanceledSuspend) {
  // Tests that the Compositors are not marked invisible if a suspend is
  // canceled or the system resumes before the lock screen is ready.
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, GetNumVisibleCompositors());

  observer_->SuspendDone(base::TimeDelta());
  BlockUserSession(BLOCKED_BY_LOCK_SCREEN);
  observer_->OnLockAnimationsComplete();
  EXPECT_EQ(1, GetNumVisibleCompositors());
}

TEST_F(PowerEventObserverTest, DelayResuspendForLockAnimations) {
  // Tests that the following order of events is handled correctly:
  //
  // - A suspend request is started.
  // - The screen is locked.
  // - The suspend request is canceled.
  // - Another suspend request is started.
  // - The screen lock animations complete.
  // - The screen lock UI changes get composited
  //
  // In this case, the observer should block the second suspend request until
  // the screen lock compositing is done.
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);

  chromeos::PowerManagerClient* client =
      chromeos::DBusThreadManager::Get()->GetPowerManagerClient();
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());

  BlockUserSession(BLOCKED_BY_LOCK_SCREEN);
  observer_->SuspendDone(base::TimeDelta());
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);

  // The expected number of suspend readiness callbacks is 2 because the
  // observer has not run the callback that it got from the first suspend
  // request.  The real PowerManagerClient would reset its internal counter in
  // this situation but the stub client is not that smart.
  EXPECT_EQ(2, client->GetNumPendingSuspendReadinessCallbacks());

  observer_->OnLockAnimationsComplete();
  EXPECT_EQ(2, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(1, GetNumVisibleCompositors());

  ASSERT_TRUE(PowerEventObserverTestApi(observer_.get())
                  .SimulateCompositorsReadyForSuspend());
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());
}

// Tests that device suspend is delayed for screen lock until the screen lock
// changes are composited for all root windows.
TEST_F(PowerEventObserverTest, DelaySuspendForCompositing_MultiDisplay) {
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);

  UpdateDisplay("100x100,200x200");

  chromeos::PowerManagerClient* client =
      chromeos::DBusThreadManager::Get()->GetPowerManagerClient();
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());

  aura::Window::Windows windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2u, windows.size());

  ui::Compositor* primary_compositor = windows[0]->GetHost()->compositor();
  ui::Compositor* secondary_compositor = windows[1]->GetHost()->compositor();
  ASSERT_EQ(2, GetNumVisibleCompositors());
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());

  PowerEventObserverTestApi test_api(observer_.get());

  // Simulate a commit before lock animations complete, and verify associated
  // compositing ends are ignored.
  test_api.CompositingDidCommit(secondary_compositor);
  observer_->OnLockAnimationsComplete();

  test_api.CompositingStarted(secondary_compositor);
  test_api.CompositingEnded(secondary_compositor);

  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(2, GetNumVisibleCompositors());

  test_api.CompositeFrame(primary_compositor);
  test_api.CompositeFrame(primary_compositor);

  test_api.CompositeFrame(secondary_compositor);

  // Even though compositing for one display is done, changes to compositor
  // visibility, and suspend readines state should be delayed until compositing
  // for the other display finishes.
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(2, GetNumVisibleCompositors());

  test_api.CompositeFrame(secondary_compositor);
  EXPECT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());
}

TEST_F(PowerEventObserverTest,
       DelaySuspendForCompositing_PendingDisplayRemoved) {
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);

  UpdateDisplay("100x100,200x200");

  chromeos::PowerManagerClient* client =
      chromeos::DBusThreadManager::Get()->GetPowerManagerClient();
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());

  aura::Window::Windows windows = Shell::GetAllRootWindows();
  ASSERT_EQ(2u, windows.size());

  ui::Compositor* primary_compositor = windows[0]->GetHost()->compositor();
  ASSERT_EQ(2, GetNumVisibleCompositors());
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  observer_->OnLockAnimationsComplete();

  PowerEventObserverTestApi test_api(observer_.get());

  test_api.CompositeFrame(primary_compositor);
  test_api.CompositeFrame(primary_compositor);

  // Even though compositing for one display is done, changes to compositor
  // visibility, and suspend readines state should be delayed until compositing
  // for the other display finishes.
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(2, GetNumVisibleCompositors());

  // Remove the second display, and verify the remaining compositor is hidden
  // at this point.
  UpdateDisplay("100x100");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());
}

TEST_F(PowerEventObserverTest, CompositorNotVisibleAtLockAnimationsComplete) {
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);

  chromeos::PowerManagerClient* client =
      chromeos::DBusThreadManager::Get()->GetPowerManagerClient();
  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());

  Shell::GetPrimaryRootWindow()->GetHost()->compositor()->SetVisible(false);

  observer_->OnLockAnimationsComplete();
  EXPECT_EQ(1, client->GetNumPendingSuspendReadinessCallbacks());
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, client->GetNumPendingSuspendReadinessCallbacks());
  EXPECT_EQ(0, GetNumVisibleCompositors());
}

// Tests that for suspend imminent induced locking screen, locking animations
// are immediate.
TEST_F(PowerEventObserverTest, ImmediateLockAnimations) {
  TestSessionStateAnimator* test_animator = new TestSessionStateAnimator;
  LockStateController* lock_state_controller =
      Shell::Get()->lock_state_controller();
  lock_state_controller->set_animator_for_test(test_animator);
  LockStateControllerTestApi lock_state_test_api(lock_state_controller);
  SetCanLockScreen(true);
  SetShouldLockScreenAutomatically(true);
  ASSERT_FALSE(GetLockedState());

  observer_->SuspendImminent(power_manager::SuspendImminent_Reason_OTHER);
  // Tests that locking animation starts.
  EXPECT_TRUE(lock_state_test_api.is_animating_lock());

  // Tests that we have two active animation containers for pre-lock animation,
  // which are non lock screen containers and shelf container.
  EXPECT_EQ(2u, test_animator->GetAnimationCount());
  test_animator->AreContainersAnimated(
      LockStateController::kPreLockContainersMask,
      SessionStateAnimator::ANIMATION_HIDE_IMMEDIATELY);
  // Tests that after finishing immediate animation, we have no active
  // animations left.
  test_animator->Advance(test_animator->GetDuration(
      SessionStateAnimator::ANIMATION_SPEED_IMMEDIATE));
  EXPECT_EQ(0u, test_animator->GetAnimationCount());

  // Flushes locking screen async request to start post-lock animation.
  EXPECT_TRUE(GetLockedState());
  EXPECT_TRUE(lock_state_test_api.is_animating_lock());
  // Tests that we have two active animation container for post-lock animation,
  // which are lock screen containers and shelf container.
  EXPECT_EQ(2u, test_animator->GetAnimationCount());
  test_animator->AreContainersAnimated(
      SessionStateAnimator::LOCK_SCREEN_CONTAINERS,
      SessionStateAnimator::ANIMATION_RAISE_TO_SCREEN);
  test_animator->AreContainersAnimated(SessionStateAnimator::SHELF,
                                       SessionStateAnimator::ANIMATION_FADE_IN);
  // Tests that after finishing immediate animation, we have no active
  // animations left. Also checks that animation ends.
  test_animator->Advance(test_animator->GetDuration(
      SessionStateAnimator::ANIMATION_SPEED_IMMEDIATE));
  EXPECT_EQ(0u, test_animator->GetAnimationCount());
  EXPECT_FALSE(lock_state_test_api.is_animating_lock());
}

}  // namespace ash
