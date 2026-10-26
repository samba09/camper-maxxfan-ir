#include "captive_redirect.h"

#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#include <cstring>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

namespace esphome::captive_redirect {

static const char *const TAG = "captive_redirect";

static constexpr uint16_t DNS_PORT = 53;
static constexpr size_t DNS_HEADER_SIZE = 12;
static constexpr uint16_t DNS_TYPE_A = 1;
static constexpr uint32_t DNS_TTL = 60;

void CaptiveRedirect::setup() { this->base_->add_handler(this); }

void CaptiveRedirect::loop() {
  // The network stack only exists once the hotspot is up
  if (this->dns_fd_ < 0) {
    if (wifi::global_wifi_component->is_ap_active())
      this->start_dns_();
    return;
  }
  // A few packets per loop is plenty for a handful of phones
  for (int i = 0; i < 4; i++)
    this->answer_dns_();
}

void CaptiveRedirect::start_dns_() {
  network::IPAddress ip = wifi::global_wifi_component->wifi_soft_ap_ip();
  char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ip.str_to(ip_buf);
  strncpy(this->ap_ip_str_, ip_buf, sizeof(this->ap_ip_str_) - 1);
  this->ap_ip_ = inet_addr(this->ap_ip_str_);

  int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    ESP_LOGE(TAG, "DNS socket failed: %d", errno);
    this->mark_failed();
    return;
  }
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(DNS_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
    ESP_LOGE(TAG, "DNS bind failed: %d", errno);
    ::close(fd);
    this->mark_failed();
    return;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  this->dns_fd_ = fd;
  ESP_LOGI(TAG, "Answering all DNS names with %s", this->ap_ip_str_);
}

// Answers every A query with the hotspot's own address. Other types get an
// empty answer, so phones fall back to IPv4 instead of waiting.
void CaptiveRedirect::answer_dns_() {
  struct sockaddr_in client = {};
  socklen_t client_len = sizeof(client);
  ssize_t len = recvfrom(this->dns_fd_, this->buffer_, sizeof(this->buffer_), 0, (struct sockaddr *) &client,
                         &client_len);
  if (len < (ssize_t) DNS_HEADER_SIZE)
    return;

  uint8_t *b = this->buffer_;
  bool is_query = (b[2] & 0x80) == 0 && (b[2] & 0x78) == 0;  // QR = 0, opcode = 0
  uint16_t questions = (b[4] << 8) | b[5];
  if (!is_query || questions != 1)
    return;

  // Skip the queried name
  size_t pos = DNS_HEADER_SIZE;
  while (pos < (size_t) len && b[pos] != 0) {
    if (b[pos] > 63)
      return;
    pos += b[pos] + 1;
  }
  pos += 1;  // terminating zero
  if (pos + 4 > (size_t) len)
    return;
  uint16_t qtype = (b[pos] << 8) | b[pos + 1];
  pos += 4;  // type and class; anything after (e.g. EDNS) is dropped

  bool answer = qtype == DNS_TYPE_A;
  b[2] = 0x84 | (b[2] & 0x01);  // response, authoritative, keep "recursion desired"
  b[3] = 0x80;                  // recursion available, no error
  b[6] = 0;
  b[7] = answer ? 1 : 0;
  memset(b + 8, 0, 4);  // no authority or additional records

  if (answer) {
    const uint8_t record[] = {
        0xC0, 0x0C,                                // name: pointer to the question
        0x00, 0x01,                                // type A
        0x00, 0x01,                                // class IN
        0x00, 0x00, 0x00, (uint8_t) DNS_TTL,       // TTL
        0x00, 0x04,                                // 4 address bytes
    };
    memcpy(b + pos, record, sizeof(record));
    pos += sizeof(record);
    memcpy(b + pos, &this->ap_ip_, 4);
    pos += 4;
  }
  sendto(this->dns_fd_, b, pos, 0, (struct sockaddr *) &client, client_len);
}

// Requests for any other host come from the phone's internet check (e.g.
// connectivitycheck.gstatic.com, captive.apple.com). Redirecting them makes
// the phone open the page; requests for the ESP itself go to web_server.
bool CaptiveRedirect::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() != HTTP_GET || this->ap_ip_str_[0] == '\0')
    return false;
  auto host = request->get_header("Host");
  if (!host.has_value())
    return false;
  std::string name = host->substr(0, host->find(':'));
  return name != this->ap_ip_str_;
}

void CaptiveRedirect::handleRequest(AsyncWebServerRequest *request) {
  request->redirect(std::string("http://") + this->ap_ip_str_ + "/");
}

}  // namespace esphome::captive_redirect
