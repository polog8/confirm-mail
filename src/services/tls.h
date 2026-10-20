// HTTPS helpers: every TLS connection is verified against the Mozilla root
// store embedded in flash (data/cert/x509_crt_bundle.bin).
#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace tls {

// Registers the root store. Call once, before any task opens a connection.
void begin();

// TLS client verified against the root store. The framework keeps a single,
// global copy of the bundle and WiFiClientSecure::setCACertBundle() frees and
// rebuilds it on every call, which races with handshakes running in other
// tasks. Clients therefore only switch verification on.
class Client : public WiFiClientSecure {
 public:
  Client() {
    _use_ca_bundle = true;
    setHandshakeTimeout(15);
  }
};

// GET into a String. Returns the HTTP status (negative on transport error).
int get(Client& client, const String& url, String& body, uint16_t timeoutMs = 10000);

}  // namespace tls
