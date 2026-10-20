#include "tls.h"

extern const uint8_t kCertBundle[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

namespace tls {

void setup(WiFiClientSecure& client) {
  client.setCACertBundle(kCertBundle);
  client.setHandshakeTimeout(15);
}

int get(WiFiClientSecure& client, const String& url, String& body, uint16_t timeoutMs) {
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
