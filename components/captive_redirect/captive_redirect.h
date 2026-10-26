#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::captive_redirect {

class CaptiveRedirect : public Component, public AsyncWebHandler {
 public:
  explicit CaptiveRedirect(web_server_base::WebServerBase *base) : base_(base) {}

  void setup() override;
  void loop() override;
  // Before web_server, so this handler sees requests first
  float get_setup_priority() const override { return setup_priority::WIFI + 1.0f; }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  void start_dns_();
  void answer_dns_();

  web_server_base::WebServerBase *base_;
  int dns_fd_{-1};
  uint32_t ap_ip_{0};  // network byte order
  char ap_ip_str_[16]{};
  uint8_t buffer_[512];
};

}  // namespace esphome::captive_redirect
