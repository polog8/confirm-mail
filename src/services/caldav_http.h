// HTTPS transport for CalDavClient on the ESP32 (Basic auth, verified TLS).
#pragma once

#include "../core/caldav.h"

class HttpsCalDavTransport : public CalDavTransport {
 public:
  HttpsCalDavTransport(const char* user, const char* password) : user_(user), password_(password) {}
  int request(const char* method, const std::string& url, const char* depth, const std::string& body, Sink sink,
              void* ctx, std::string* location) override;

 private:
  const char* user_;
  const char* password_;
};
