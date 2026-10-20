#include "caldav_http.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "tls.h"

namespace {

// Lets HTTPClient::writeToStream() (which undoes chunked encoding) push the
// body straight into the XML extractor.
class SinkStream : public Stream {
 public:
  SinkStream(CalDavTransport::Sink sink, void* ctx) : sink_(sink), ctx_(ctx) {}
  size_t write(uint8_t c) override {
    sink_(ctx_, reinterpret_cast<const char*>(&c), 1);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    sink_(ctx_, reinterpret_cast<const char*>(buf), size);
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  CalDavTransport::Sink sink_;
  void* ctx_;
};

}  // namespace

int HttpsCalDavTransport::request(const char* method, const std::string& url, const char* depth,
                                  const std::string& body, Sink sink, void* ctx, std::string* location) {
  tls::Client secure;
  WiFiClient plain;
  const bool https = url.rfind("https://", 0) == 0;
  HTTPClient http;
  http.setReuse(false);  // "Connection: close": the server ends the body for us
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // CalDavClient follows them, keeping the method
  http.setUserAgent("M5DeskStation/1.0 (CalDAV)");
  const String u(url.c_str());
  if (!(https ? http.begin(secure, u) : http.begin(plain, u))) return -1;
  http.setAuthorization(user_, password_);
  http.addHeader("Depth", depth);
  http.addHeader("Content-Type", "application/xml; charset=utf-8");
  static const char* kHeaders[] = {"Location"};
  http.collectHeaders(kHeaders, 1);
  const int code = http.sendRequest(method, reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
  if (code >= 300 && code < 400 && location) *location = http.header("Location").c_str();
  if (code > 0 && code != 204 && code != 304) {
    SinkStream stream(sink, ctx);
    if (http.writeToStream(&stream) < 0 && code == 207) {
      http.end();
      return -2;  // body cut short
    }
  }
  http.end();
  return code;
}
