// HTTPS helpers: every TLS connection is verified against the Mozilla root
// store embedded in flash (data/cert/x509_crt_bundle.bin).
#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace tls {

void setup(WiFiClientSecure& client);

// GET into a String. Returns the HTTP status (negative on transport error).
int get(WiFiClientSecure& client, const String& url, String& body, uint16_t timeoutMs = 10000);

}  // namespace tls
