// Minimal CalDAV (RFC 4791) client: account discovery and time-range queries.
// Transport agnostic so it can be tested on a PC; the firmware supplies an
// HTTPS transport. Event data is streamed into IcsParser, so memory use does
// not depend on the size of the calendars.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "ics.h"

struct CalDavCalendar {
  char name[48] = {0};
  char url[200] = {0};
  uint32_t color = 0;
  bool hasColor = false;
};

class CalDavTransport {
 public:
  using Sink = void (*)(void* ctx, const char* data, size_t len);
  virtual ~CalDavTransport() = default;
  // Sends one request (Basic auth is the transport's business). The body of
  // the response is passed to `sink` in chunks. For 3xx answers, `location`
  // receives the Location header. Returns the HTTP status, <= 0 on failure.
  virtual int request(const char* method, const std::string& url, const char* depth, const std::string& body,
                      Sink sink, void* ctx, std::string* location) = 0;
};

// Streams the text of every <calendar-data> element (CDATA or entity-escaped)
// found in a multistatus document. Each element is terminated with CRLF.
class CalendarDataExtractor {
 public:
  CalendarDataExtractor(CalDavTransport::Sink sink, void* ctx) : sink_(sink), ctx_(ctx) {}
  void feed(const char* data, size_t len);
  void finish() { flush(); }
  int blocks() const { return blocks_; }

 private:
  enum class State : uint8_t { Text, Tag, Data, Markup, CData, Entity };
  void emit(char c);
  void emitStr(const char* s);
  void flush();
  void endTag();

  CalDavTransport::Sink sink_;
  void* ctx_;
  State state_ = State::Text;
  char tag_[48];
  size_t tagLen_ = 0;
  bool tagNameDone_ = false;
  char prevTagChar_ = 0;
  char markup_[12];
  size_t markupLen_ = 0;
  bool closing_ = false;
  char entity_[12];
  size_t entityLen_ = 0;
  int brackets_ = 0;  // pending ']' while in CDATA
  char out_[256];
  size_t outLen_ = 0;
  int blocks_ = 0;
};

class CalDavClient {
 public:
  explicit CalDavClient(CalDavTransport& t) : t_(t) {}

  // Finds the account's event calendars (collections supporting VEVENT).
  // Returns how many were written to `out`, or -1 on error (see error()).
  int discover(const std::string& rootUrl, CalDavCalendar* out, int max);
  // Runs a calendar-query REPORT for [start, end) and feeds the events to
  // `parser` (call parser.finish() afterwards).
  bool fetchEvents(const std::string& calendarUrl, int64_t start, int64_t end, IcsParser& parser);

  int lastStatus() const { return status_; }
  const char* error() const { return error_; }

 private:
  // Follows redirects and buffers the whole (small) response.
  int buffered(const char* method, std::string& url, const char* depth, const std::string& body, std::string& out);
  void fail(const char* what, int status);

  CalDavTransport& t_;
  int status_ = 0;
  char error_[48] = {0};
};

namespace caldav {

// Resolves an href found in a response against the URL that was queried.
std::string resolve(const std::string& base, const std::string& href);
// "#RRGGBB" or "#RRGGBBAA" -> 0xRRGGBB
bool parseColor(const char* s, uint32_t* out);
// "20261020T000000Z"
std::string utcStamp(int64_t t);

// Tiny XML helpers working on a buffer (namespace prefixes are ignored).
bool findElement(const char* b, const char* e, const char* localName, const char** innerB, const char** innerE,
                 const char** after = nullptr);
std::string elementText(const char* b, const char* e);  // CDATA/entities decoded, trimmed

}  // namespace caldav
