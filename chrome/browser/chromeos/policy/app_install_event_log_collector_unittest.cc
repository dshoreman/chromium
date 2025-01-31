// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/app_install_event_log_collector.h"

#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_power_manager_client.h"
#include "chromeos/dbus/shill_service_client.h"
#include "chromeos/network/network_handler.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace em = enterprise_management;

namespace policy {

namespace {

constexpr char kEthernetServicePath[] = "/service/eth1";
constexpr char kWifiServicePath[] = "/service/wifi1";

constexpr char kPackageName[] = "com.example.app";

class FakeAppInstallEventLogCollectorDelegate
    : public AppInstallEventLogCollector::Delegate {
 public:
  FakeAppInstallEventLogCollectorDelegate() = default;
  ~FakeAppInstallEventLogCollectorDelegate() override = default;

  // AppInstallEventLogCollector::Delegate:
  void AddForAllPackages(
      std::unique_ptr<em::AppInstallReportLogEvent> event) override {
    ++add_for_all_count_;
    last_event_ = *event;
  }

  void Add(const std::string& package,
           bool add_disk_space_info,
           std::unique_ptr<em::AppInstallReportLogEvent> event) override {
    ++add_count_;
    last_event_ = *event;
  }

  int add_for_all_count() const { return add_for_all_count_; }

  int add_count() const { return add_count_; }

  const em::AppInstallReportLogEvent& last_event() const { return last_event_; }

 private:
  int add_for_all_count_ = 0;
  int add_count_ = 0;
  em::AppInstallReportLogEvent last_event_;

  DISALLOW_COPY_AND_ASSIGN(FakeAppInstallEventLogCollectorDelegate);
};

}  // namespace

class AppInstallEventLogCollectorTest : public testing::Test {
 protected:
  AppInstallEventLogCollectorTest() = default;
  ~AppInstallEventLogCollectorTest() override = default;

  void SetUp() override {
    RegisterLocalState(pref_service_.registry());
    TestingBrowserProcess::GetGlobal()->SetLocalState(&pref_service_);
    std::unique_ptr<chromeos::FakePowerManagerClient> power_manager_client =
        std::make_unique<chromeos::FakePowerManagerClient>();
    power_manager_client_ = power_manager_client.get();
    chromeos::DBusThreadManager::GetSetterForTesting()->SetPowerManagerClient(
        std::move(power_manager_client));

    chromeos::DBusThreadManager::Initialize();
    chromeos::NetworkHandler::Initialize();
    profile_ = std::make_unique<TestingProfile>();
    network_change_notifier_ =
        base::WrapUnique(net::NetworkChangeNotifier::CreateMock());

    service_test_ = chromeos::DBusThreadManager::Get()
                        ->GetShillServiceClient()
                        ->GetTestInterface();
    service_test_->AddService(kEthernetServicePath, "eth1_guid", "eth1",
                              shill::kTypeEthernet, shill::kStateOffline,
                              true /* visible */);
    service_test_->AddService(kWifiServicePath, "wifi1_guid", "wifi1",
                              shill::kTypeEthernet, shill::kStateOffline,
                              true /* visible */);
    base::RunLoop().RunUntilIdle();
  }

  void TearDown() override {
    profile_.reset();
    chromeos::NetworkHandler::Shutdown();
    chromeos::DBusThreadManager::Shutdown();
    TestingBrowserProcess::GetGlobal()->SetLocalState(nullptr);
  }

  void SetNetworkState(const std::string& service_path,
                       const std::string& state) {
    service_test_->SetServiceProperty(service_path, shill::kStateProperty,
                                      base::Value(state));
    base::RunLoop().RunUntilIdle();

    net::NetworkChangeNotifier::ConnectionType connection_type =
        net::NetworkChangeNotifier::CONNECTION_NONE;
    std::string network_state;
    service_test_->GetServiceProperties(kWifiServicePath)
        ->GetString(shill::kStateProperty, &network_state);
    if (network_state == shill::kStateOnline) {
      connection_type = net::NetworkChangeNotifier::CONNECTION_WIFI;
    }
    service_test_->GetServiceProperties(kEthernetServicePath)
        ->GetString(shill::kStateProperty, &network_state);
    if (network_state == shill::kStateOnline) {
      connection_type = net::NetworkChangeNotifier::CONNECTION_ETHERNET;
    }
    net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
        connection_type);
    base::RunLoop().RunUntilIdle();
  }

  TestingProfile* profile() { return profile_.get(); }
  FakeAppInstallEventLogCollectorDelegate* delegate() { return &delegate_; }
  chromeos::FakePowerManagerClient* power_manager_client() {
    return power_manager_client_;
  }

  const std::set<std::string> packages_ = {kPackageName};

  std::unique_ptr<net::NetworkChangeNotifier> network_change_notifier_;
  chromeos::ShillServiceClient::TestInterface* service_test_ = nullptr;

 private:
  content::TestBrowserThreadBundle thread_bundle_;
  std::unique_ptr<TestingProfile> profile_;
  FakeAppInstallEventLogCollectorDelegate delegate_;
  TestingPrefServiceSimple pref_service_;
  chromeos::FakePowerManagerClient* power_manager_client_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(AppInstallEventLogCollectorTest);
};

// Test the case when collector is created and destroyed inside the one user
// session. In this case no event is generated. This happens for example when
// all apps are installed in context of the same user session.
TEST_F(AppInstallEventLogCollectorTest, NoEventsByDefault) {
  std::unique_ptr<AppInstallEventLogCollector> collector =
      std::make_unique<AppInstallEventLogCollector>(delegate(), profile(),
                                                    packages_);
  collector.reset();

  EXPECT_EQ(0, delegate()->add_count());
  EXPECT_EQ(0, delegate()->add_for_all_count());
}

TEST_F(AppInstallEventLogCollectorTest, LoginLogout) {
  std::unique_ptr<AppInstallEventLogCollector> collector =
      std::make_unique<AppInstallEventLogCollector>(delegate(), profile(),
                                                    packages_);

  EXPECT_EQ(0, delegate()->add_for_all_count());

  collector->AddLoginEvent();
  EXPECT_EQ(1, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::SESSION_STATE_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_EQ(em::AppInstallReportLogEvent::LOGIN,
            delegate()->last_event().session_state_change_type());
  EXPECT_TRUE(delegate()->last_event().has_online());
  EXPECT_FALSE(delegate()->last_event().online());

  collector->AddLogoutEvent();
  EXPECT_EQ(2, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::SESSION_STATE_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_EQ(em::AppInstallReportLogEvent::LOGOUT,
            delegate()->last_event().session_state_change_type());
  EXPECT_FALSE(delegate()->last_event().has_online());

  collector.reset();

  EXPECT_EQ(2, delegate()->add_for_all_count());
  EXPECT_EQ(0, delegate()->add_count());
}

TEST_F(AppInstallEventLogCollectorTest, LoginTypes) {
  {
    AppInstallEventLogCollector collector(delegate(), profile(), packages_);
    collector.AddLoginEvent();
    EXPECT_EQ(1, delegate()->add_for_all_count());
    EXPECT_EQ(em::AppInstallReportLogEvent::SESSION_STATE_CHANGE,
              delegate()->last_event().event_type());
    EXPECT_EQ(em::AppInstallReportLogEvent::LOGIN,
              delegate()->last_event().session_state_change_type());
    EXPECT_TRUE(delegate()->last_event().has_online());
    EXPECT_FALSE(delegate()->last_event().online());
  }

  {
    // Check login after restart. No log is expected.
    AppInstallEventLogCollector collector(delegate(), profile(), packages_);
    base::CommandLine::ForCurrentProcess()->AppendSwitch(
        chromeos::switches::kLoginUser);
    collector.AddLoginEvent();
    EXPECT_EQ(1, delegate()->add_for_all_count());
  }

  {
    // Check logout on restart. No log is expected.
    AppInstallEventLogCollector collector(delegate(), profile(), packages_);
    g_browser_process->local_state()->SetBoolean(prefs::kWasRestarted, true);
    collector.AddLogoutEvent();
    EXPECT_EQ(1, delegate()->add_for_all_count());
  }

  EXPECT_EQ(0, delegate()->add_count());
}

TEST_F(AppInstallEventLogCollectorTest, SuspendResume) {
  std::unique_ptr<AppInstallEventLogCollector> collector =
      std::make_unique<AppInstallEventLogCollector>(delegate(), profile(),
                                                    packages_);

  power_manager_client()->SendSuspendImminent(
      power_manager::SuspendImminent_Reason_OTHER);
  EXPECT_EQ(1, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::SESSION_STATE_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_EQ(em::AppInstallReportLogEvent::SUSPEND,
            delegate()->last_event().session_state_change_type());

  power_manager_client()->SendSuspendDone();
  EXPECT_EQ(2, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::SESSION_STATE_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_EQ(em::AppInstallReportLogEvent::RESUME,
            delegate()->last_event().session_state_change_type());

  collector.reset();

  EXPECT_EQ(0, delegate()->add_count());
}

// Connect to Ethernet. Start log collector. Verify that a login event with
// network state online is recorded. Then, connect to WiFi and disconnect from
// Ethernet, in this order. Verify that no event is recorded. Then, disconnect
// from WiFi. Verify that a connectivity change event is recorded. Then, connect
// to WiFi with a pending captive portal. Verify that no event is recorded.
// Then, pass the captive portal. Verify that a connectivity change is recorded.
TEST_F(AppInstallEventLogCollectorTest, ConnectivityChanges) {
  SetNetworkState(kEthernetServicePath, shill::kStateOnline);

  std::unique_ptr<AppInstallEventLogCollector> collector =
      std::make_unique<AppInstallEventLogCollector>(delegate(), profile(),
                                                    packages_);

  EXPECT_EQ(0, delegate()->add_for_all_count());

  collector->AddLoginEvent();
  EXPECT_EQ(1, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::SESSION_STATE_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_EQ(em::AppInstallReportLogEvent::LOGIN,
            delegate()->last_event().session_state_change_type());
  EXPECT_TRUE(delegate()->last_event().online());

  SetNetworkState(kWifiServicePath, shill::kStateOnline);
  EXPECT_EQ(1, delegate()->add_for_all_count());

  SetNetworkState(kEthernetServicePath, shill::kStateOffline);
  EXPECT_EQ(1, delegate()->add_for_all_count());

  SetNetworkState(kWifiServicePath, shill::kStateOffline);
  EXPECT_EQ(2, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::CONNECTIVITY_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_FALSE(delegate()->last_event().online());

  SetNetworkState(kWifiServicePath, shill::kStatePortal);
  EXPECT_EQ(2, delegate()->add_for_all_count());

  SetNetworkState(kWifiServicePath, shill::kStateOnline);
  EXPECT_EQ(3, delegate()->add_for_all_count());
  EXPECT_EQ(em::AppInstallReportLogEvent::CONNECTIVITY_CHANGE,
            delegate()->last_event().event_type());
  EXPECT_TRUE(delegate()->last_event().online());

  collector.reset();

  EXPECT_EQ(3, delegate()->add_for_all_count());
  EXPECT_EQ(0, delegate()->add_count());
}

}  // namespace policy
