// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_LOCAL_DISCOVERY_LOCAL_DISCOVERY_UI_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_LOCAL_DISCOVERY_LOCAL_DISCOVERY_UI_HANDLER_H_

#include <map>
#include <string>
#include <vector>

#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/printing/cloud_print/cloud_print_printer_list.h"
#include "chrome/browser/printing/cloud_print/privet_device_lister.h"
#include "chrome/browser/printing/cloud_print/privet_http.h"
#include "components/signin/core/browser/signin_manager.h"
#include "content/public/browser/web_ui_message_handler.h"

#if defined(ENABLE_PRINT_PREVIEW) && !defined(OS_CHROMEOS)
#define CLOUD_PRINT_CONNECTOR_UI_AVAILABLE
#endif

class CloudPrintProxyService;

namespace cloud_print {
class PrivetHTTPAsynchronousFactory;
class PrivetHTTPResolution;
class PrivetV1HTTPClient;
}

// TODO(noamsml): Factor out full registration flow into single class
namespace local_discovery {

class ServiceDiscoverySharedClient;

// UI Handler for chrome://devices/
// It listens to local discovery notifications and passes those notifications
// into the Javascript to update the page.
class LocalDiscoveryUIHandler
    : public content::WebUIMessageHandler,
      public cloud_print::PrivetRegisterOperation::Delegate,
      public cloud_print::PrivetDeviceLister::Delegate,
      public cloud_print::CloudPrintPrinterList::Delegate,
      public SigninManagerBase::Observer {
 public:
  LocalDiscoveryUIHandler();
  ~LocalDiscoveryUIHandler() override;

  static bool GetHasVisible();

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;
  // PrivetRegisterOperation::Delegate implementation.
  void OnPrivetRegisterClaimToken(
      cloud_print::PrivetRegisterOperation* operation,
      const std::string& token,
      const GURL& url) override;
  void OnPrivetRegisterError(
      cloud_print::PrivetRegisterOperation* operation,
      const std::string& action,
      cloud_print::PrivetRegisterOperation::FailureReason reason,
      int printer_http_code,
      const base::DictionaryValue* json) override;
  void OnPrivetRegisterDone(cloud_print::PrivetRegisterOperation* operation,
                            const std::string& device_id) override;

  // PrivetDeviceLister::Delegate implementation.
  void DeviceChanged(
      bool added,
      const std::string& name,
      const cloud_print::DeviceDescription& description) override;
  void DeviceRemoved(const std::string& name) override;
  void DeviceCacheFlushed() override;

  // CloudPrintPrinterList::Delegate implementation.
  void OnDeviceListReady(
      const cloud_print::CloudPrintPrinterList::DeviceList& devices) override;
  void OnDeviceListUnavailable() override;

  // SigninManagerBase::Observer implementation.
  void GoogleSigninSucceeded(const std::string& account_id,
                             const std::string& username,
                             const std::string& password) override;
  void GoogleSignedOut(const std::string& account_id,
                       const std::string& username) override;

 private:
  using DeviceDescriptionMap =
      std::map<std::string, cloud_print::DeviceDescription>;
  using ResultCallback = base::Callback<void(bool result)>;

  // Message handlers:
  // For when the page is ready to receive device notifications.
  void HandleStart(const base::ListValue* args);

  // For when a visibility change occurs.
  void HandleIsVisible(const base::ListValue* args);

  // For when a user choice is made.
  void HandleRegisterDevice(const base::ListValue* args);

  // For when a cancellation is made.
  void HandleCancelRegistration(const base::ListValue* args);

  // For requesting the device list.
  void HandleRequestDeviceList(const base::ListValue* args);

  // For opening URLs (relative to the Google Cloud Print base URL) in a new
  // tab.
  void HandleOpenCloudPrintURL(const base::ListValue* args);

  // For showing sync login UI.
  void HandleShowSyncUI(const base::ListValue* args);

  // For when the IP address of the printer has been resolved for registration.
  void StartRegisterHTTP(
      std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);

  // For when the confirm operation on the cloudprint server has finished
  // executing.
  void OnConfirmDone(cloud_print::GCDApiFlow::Status status);

  // Signal to the web interface an error has ocurred while registering.
  void SendRegisterError();

  // Singal to the web interface that registration has finished.
  void SendRegisterDone(const std::string& service_name);

  // Set the visibility of the page.
  void SetIsVisible(bool visible);

  // Get the sync account email.
  std::string GetSyncAccount() const;

  // Reset and cancel the current registration.
  void ResetCurrentRegistration();

  std::unique_ptr<cloud_print::GCDApiFlow> CreateApiFlow();
  void OnSetupError();

  // Announcement hasn't been sent for a certain time after registration
  // finished. Consider it failed.
  // TODO(noamsml): Re-resolve service first.
  void OnAnnouncementTimeoutReached();

  void CheckUserLoggedIn();

  void CheckListingDone();

  bool IsUserSupervisedOrOffTheRecord();

#if defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
  void StartCloudPrintConnector();
  void OnCloudPrintPrefsChanged();
  void ShowCloudPrintSetupDialog(const base::ListValue* args);
  void HandleDisableCloudPrintConnector(const base::ListValue* args);
  void SetupCloudPrintConnectorSection();
  void RefreshCloudPrintStatusFromService();
  CloudPrintProxyService* GetCloudPrintProxyService();
#endif

  // A map of current device descriptions provided by the PrivetDeviceLister.
  DeviceDescriptionMap device_descriptions_;

  // The service discovery client used listen for devices on the local network.
  scoped_refptr<ServiceDiscoverySharedClient> service_discovery_client_;

  // A factory for creating the privet HTTP Client.
  std::unique_ptr<cloud_print::PrivetHTTPAsynchronousFactory>
      privet_http_factory_;

  // An object representing the resolution process for the privet_http_factory.
  std::unique_ptr<cloud_print::PrivetHTTPResolution> privet_resolution_;

  // The current HTTP client (used for the current operation).
  std::unique_ptr<cloud_print::PrivetV1HTTPClient> current_http_client_;

  // The current register operation. Only one allowed at any time.
  std::unique_ptr<cloud_print::PrivetRegisterOperation>
      current_register_operation_;

  // The current confirm call used during the registration flow.
  std::unique_ptr<cloud_print::GCDApiFlow> confirm_api_call_flow_;

  // The device lister used to list devices on the local network.
  std::unique_ptr<cloud_print::PrivetDeviceLister> privet_lister_;

  // Whether or not the page is marked as visible.
  bool is_visible_;

  // List of printers from cloud print.
  std::unique_ptr<cloud_print::GCDApiFlow> cloud_print_printer_list_;
  std::vector<cloud_print::CloudPrintPrinterList::Device> cloud_devices_;
  int failed_list_count_;
  int succeded_list_count_;

#if defined(CLOUD_PRINT_CONNECTOR_UI_AVAILABLE)
  StringPrefMember cloud_print_connector_email_;
  BooleanPrefMember cloud_print_connector_enabled_;
#endif

  DISALLOW_COPY_AND_ASSIGN(LocalDiscoveryUIHandler);
};

#undef CLOUD_PRINT_CONNECTOR_UI_AVAILABLE

}  // namespace local_discovery

#endif  // CHROME_BROWSER_UI_WEBUI_LOCAL_DISCOVERY_LOCAL_DISCOVERY_UI_HANDLER_H_
