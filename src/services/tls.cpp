#include "tls.h"

extern const uint8_t kCertBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
// From the WiFiClientSecure library (esp_crt_bundle.c).
extern "C" void arduino_esp_crt_bundle_set(const uint8_t* x509_bundle);

namespace tls {

void begin() { arduino_esp_crt_bundle_set(kCertBundle); }

int get(Client& client, const String& url, String& body, uint16_t timeoutMs) {
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("M5DeskStation/1.0");
  if (!http.begin(client, url)) return -1;
  int code = http.GET();
  if (code > 0) body = http.getString();
  http.end();
  return code;
}

}  // namespace tls
