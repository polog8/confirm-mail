#include "caldav.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "civil.h"

namespace {

const char kPrincipalBody[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:current-user-principal/></d:prop></d:propfind>";

const char kHomeBody[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><c:calendar-home-set/></d:prop></d:propfind>";

const char kListBody[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:a=\"http://apple.com/ns/ical/\">"
    "<d:prop><d:displayname/><d:resourcetype/><c:supported-calendar-component-set/><a:calendar-color/>"
    "</d:prop></d:propfind>";

void appendSink(void* ctx, const char* data, size_t len) { static_cast<std::string*>(ctx)->append(data, len); }
void extractorSink(void* ctx, const char* data, size_t len) {
  static_cast<CalendarDataExtractor*>(ctx)->feed(data, len);
}
void parserSink(void* ctx, const char* data, size_t len) { static_cast<IcsParser*>(ctx)->feed(data, len); }

// Local part of a (possibly prefixed) XML name.
bool localNameIs(const char* b, const char* e, const char* local) {
  const char* colon = nullptr;
  for (const char* p = b; p < e; ++p) {
    if (*p == ':') colon = p;
  }
  const char* n = colon ? colon + 1 : b;
  size_t len = strlen(local);
  return static_cast<size_t>(e - n) == len && strncmp(n, local, len) == 0;
}

// Appends the UTF-8 encoding of a code point.
void appendUtf8(std::string& s, unsigned long cp) {
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Decodes an entity name (without & and ;). Returns false if unknown.
bool decodeEntity(const char* name, std::string& out) {
  if (!strcmp(name, "lt")) out = "<";
  else if (!strcmp(name, "gt")) out = ">";
  else if (!strcmp(name, "amp")) out = "&";
  else if (!strcmp(name, "quot")) out = "\"";
  else if (!strcmp(name, "apos")) out = "'";
  else if (name[0] == '#') {
    char* end = nullptr;
    unsigned long cp = (name[1] == 'x' || name[1] == 'X') ? strtoul(name + 2, &end, 16) : strtoul(name + 1, &end, 10);
    if (!end || *end || cp == 0 || cp > 0x10FFFF) return false;
    out.clear();
    appendUtf8(out, cp);
  } else {
    return false;
  }
  return true;
}

std::string pathOf(const std::string& url) {
  size_t scheme = url.find("://");
  size_t start = scheme == std::string::npos ? 0 : url.find('/', scheme + 3);
  std::string p = start == std::string::npos ? "/" : url.substr(start);
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// CalendarDataExtractor

void CalendarDataExtractor::emit(char c) {
  out_[outLen_++] = c;
  if (outLen_ == sizeof(out_)) flush();
}

void CalendarDataExtractor::emitStr(const char* s) {
  while (*s) emit(*s++);
}

void CalendarDataExtractor::flush() {
  if (outLen_) sink_(ctx_, out_, outLen_);
  outLen_ = 0;
}

void CalendarDataExtractor::endTag() {
  emitStr("\r\n");  // keep consecutive VCALENDARs on separate lines
  flush();
  ++blocks_;
  state_ = State::Text;
}

void CalendarDataExtractor::feed(const char* data, size_t len) {
  static const char kCData[] = "![CDATA[";
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    switch (state_) {
      case State::Text:
        if (c == '<') {
          state_ = State::Tag;
          tagLen_ = 0;
          tagNameDone_ = false;
          prevTagChar_ = 0;
        }
        break;
      case State::Tag:
        if (c == '>') {
          tag_[tagLen_] = '\0';
          const bool selfClosing = prevTagChar_ == '/';
          const bool special = tag_[0] == '/' || tag_[0] == '?' || tag_[0] == '!';
          if (!special && !selfClosing && localNameIs(tag_, tag_ + tagLen_, "calendar-data")) {
            state_ = State::Data;
          } else {
            state_ = State::Text;
          }
        } else {
          if (!tagNameDone_) {
            if (isspace(static_cast<unsigned char>(c)) || (c == '/' && tagLen_ > 0)) tagNameDone_ = true;
            else if (tagLen_ < sizeof(tag_) - 1) tag_[tagLen_++] = c;
          }
          prevTagChar_ = c;
        }
        break;
      case State::Data:
        if (c == '<') {
          state_ = State::Markup;
          markupLen_ = 0;
          closing_ = false;
        } else if (c == '&') {
          state_ = State::Entity;
          entityLen_ = 0;
        } else {
          emit(c);
        }
        break;
      case State::Markup:
        if (markupLen_ == 0 && c == '/') {
          closing_ = true;
          markupLen_ = 1;
        } else if (closing_) {
          if (c == '>') endTag();
        } else if (markupLen_ < 8 && c == kCData[markupLen_]) {
          markup_[markupLen_++] = c;
          if (markupLen_ == 8) {
            state_ = State::CData;
            brackets_ = 0;
          }
        } else if (c == '>') {
          state_ = State::Data;  // some other element: ignore it
        } else {
          markupLen_ = 100;  // skipping until '>'
        }
        break;
      case State::CData:
        if (c == ']') {
          ++brackets_;
        } else if (c == '>' && brackets_ >= 2) {
          for (int k = 0; k < brackets_ - 2; ++k) emit(']');
          brackets_ = 0;
          state_ = State::Data;
        } else {
          for (int k = 0; k < brackets_; ++k) emit(']');
          brackets_ = 0;
          emit(c);
        }
        break;
      case State::Entity:
        if (c == ';') {
          entity_[entityLen_] = '\0';
          std::string decoded;
          if (decodeEntity(entity_, decoded)) {
            emitStr(decoded.c_str());
          } else {
            emit('&');
            emitStr(entity_);
            emit(';');
          }
          state_ = State::Data;
        } else if (entityLen_ < sizeof(entity_) - 1) {
          entity_[entityLen_++] = c;
        } else {
          entity_[entityLen_] = '\0';
          emit('&');
          emitStr(entity_);
          emit(c);
          state_ = State::Data;
        }
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// helpers

namespace caldav {

std::string resolve(const std::string& base, const std::string& href) {
  if (href.compare(0, 7, "http://") == 0 || href.compare(0, 8, "https://") == 0) return href;
  const size_t scheme = base.find("://");
  const size_t pathStart = scheme == std::string::npos ? std::string::npos : base.find('/', scheme + 3);
  const std::string origin = pathStart == std::string::npos ? base : base.substr(0, pathStart);
  if (!href.empty() && href[0] == '/') return origin + href;
  const size_t slash = base.rfind('/');
  if (pathStart == std::string::npos || slash < pathStart) return origin + "/" + href;
  return base.substr(0, slash + 1) + href;
}

bool parseColor(const char* s, uint32_t* out) {
  while (*s == ' ') ++s;
  if (*s != '#') return false;
  ++s;
  size_t n = 0;
  while (isxdigit(static_cast<unsigned char>(s[n]))) ++n;
  if (n != 6 && n != 8) return false;
  char hex[7];
  memcpy(hex, s, 6);
  hex[6] = '\0';
  *out = static_cast<uint32_t>(strtoul(hex, nullptr, 16));
  return true;
}

std::string utcStamp(int64_t t) {
  civ::DateTime d = civ::fromEpoch(t);
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ", d.y, d.m, d.d, d.hh, d.mm, d.ss);
  return buf;
}

bool findElement(const char* b, const char* e, const char* localName, const char** innerB, const char** innerE,
                 const char** after) {
  const char* p = b;
  while (p < e) {
    const char* lt = static_cast<const char*>(memchr(p, '<', static_cast<size_t>(e - p)));
    if (!lt || lt + 1 >= e) return false;
    const char* q = lt + 1;
    if (*q == '/' || *q == '?' || *q == '!') {
      p = q;
      continue;
    }
    const char* nameB = q;
    while (q < e && !isspace(static_cast<unsigned char>(*q)) && *q != '>' && *q != '/') ++q;
    const char* nameE = q;
    char quote = 0;
    while (q < e && (quote || *q != '>')) {
      if (quote && *q == quote) quote = 0;
      else if (!quote && (*q == '"' || *q == '\'')) quote = *q;
      ++q;
    }
    if (q >= e) return false;
    const char* gt = q;
    const bool selfClosing = gt[-1] == '/';
    if (!localNameIs(nameB, nameE, localName)) {
      p = gt + 1;
      continue;
    }
    if (selfClosing) {
      *innerB = *innerE = gt + 1;
      if (after) *after = gt + 1;
      return true;
    }
    // Matching end tag, accounting for nested elements with the same name.
    int depth = 1;
    const char* s = gt + 1;
    while (s < e) {
      const char* l2 = static_cast<const char*>(memchr(s, '<', static_cast<size_t>(e - s)));
      if (!l2) return false;
      if (static_cast<size_t>(e - l2) >= 9 && strncmp(l2, "<![CDATA[", 9) == 0) {
        const char* endC = l2 + 9;
        while (endC + 2 < e && strncmp(endC, "]]>", 3) != 0) ++endC;
        s = endC + 3;
        continue;
      }
      const bool close = l2 + 1 < e && l2[1] == '/';
      const char* n = l2 + (close ? 2 : 1);
      const char* ne = n;
      while (ne < e && !isspace(static_cast<unsigned char>(*ne)) && *ne != '>' && *ne != '/') ++ne;
      const char* g = static_cast<const char*>(memchr(ne, '>', static_cast<size_t>(e - ne)));
      if (!g) return false;
      if (localNameIs(n, ne, localName)) {
        if (close) {
          if (--depth == 0) {
            *innerB = gt + 1;
            *innerE = l2;
            if (after) *after = g + 1;
            return true;
          }
        } else if (g[-1] != '/') {
          ++depth;
        }
      }
      s = g + 1;
    }
    return false;
  }
  return false;
}

std::string elementText(const char* b, const char* e) {
  std::string out;
  const char* p = b;
  while (p < e) {
    if (*p == '<') {
      if (static_cast<size_t>(e - p) >= 9 && strncmp(p, "<![CDATA[", 9) == 0) {
        const char* c = p + 9;
        while (c + 2 < e && strncmp(c, "]]>", 3) != 0) out += *c++;
        p = c + 3;
      } else {
        const char* g = static_cast<const char*>(memchr(p, '>', static_cast<size_t>(e - p)));
        p = g ? g + 1 : e;
      }
    } else if (*p == '&') {
      const char* semi = static_cast<const char*>(memchr(p, ';', static_cast<size_t>(e - p)));
      std::string decoded;
      if (semi && semi - p < 12) {
        std::string name(p + 1, semi);
        if (decodeEntity(name.c_str(), decoded)) {
          out += decoded;
          p = semi + 1;
          continue;
        }
      }
      out += *p++;
    } else {
      out += *p++;
    }
  }
  size_t s = 0, t = out.size();
  while (s < t && isspace(static_cast<unsigned char>(out[s]))) ++s;
  while (t > s && isspace(static_cast<unsigned char>(out[t - 1]))) --t;
  return out.substr(s, t - s);
}

}  // namespace caldav

// ---------------------------------------------------------------------------
// CalDavClient

void CalDavClient::fail(const char* what, int status) {
  status_ = status;
  if (status == 401 || status == 403) {
    snprintf(error_, sizeof(error_), "identifiants refusés");
  } else if (status > 0) {
    snprintf(error_, sizeof(error_), "%s (HTTP %d)", what, status);
  } else {
    snprintf(error_, sizeof(error_), "%s (réseau %d)", what, status);
  }
}

int CalDavClient::buffered(const char* method, std::string& url, const char* depth, const std::string& body,
                           std::string& out) {
  int code = 0;
  for (int hop = 0; hop < 5; ++hop) {
    out.clear();
    std::string location;
    code = t_.request(method, url, depth, body, appendSink, &out, &location);
    status_ = code;
    if (code >= 300 && code < 400 && !location.empty()) {
      url = caldav::resolve(url, location);
      continue;
    }
    break;
  }
  return code;
}

int CalDavClient::discover(const std::string& rootUrl, CalDavCalendar* out, int max) {
  using caldav::elementText;
  using caldav::findElement;
  error_[0] = '\0';
  std::string body;
  const char *ib, *ie, *hb, *he;

  std::string url = rootUrl;
  int code = buffered("PROPFIND", url, "0", kPrincipalBody, body);
  if (code != 207) {
    fail("découverte", code);
    return -1;
  }
  const char* b = body.data();
  const char* e = b + body.size();
  std::string principal;
  if (findElement(b, e, "current-user-principal", &ib, &ie) && findElement(ib, ie, "href", &hb, &he)) {
    principal = elementText(hb, he);
  }
  if (principal.empty()) {
    fail("compte introuvable", code);
    return -1;
  }

  std::string principalUrl = caldav::resolve(url, principal);
  code = buffered("PROPFIND", principalUrl, "0", kHomeBody, body);
  if (code != 207) {
    fail("compte", code);
    return -1;
  }
  b = body.data();
  e = b + body.size();
  std::string home;
  if (findElement(b, e, "calendar-home-set", &ib, &ie) && findElement(ib, ie, "href", &hb, &he)) {
    home = elementText(hb, he);
  }
  if (home.empty()) {
    fail("calendriers introuvables", code);
    return -1;
  }

  std::string homeUrl = caldav::resolve(principalUrl, home);
  code = buffered("PROPFIND", homeUrl, "1", kListBody, body);
  if (code != 207) {
    fail("liste", code);
    return -1;
  }
  b = body.data();
  e = b + body.size();
  const std::string homePath = pathOf(homeUrl);
  int n = 0;
  const char* cursor = b;
  const char *rb, *re, *next;
  while (n < max && findElement(cursor, e, "response", &rb, &re, &next)) {
    cursor = next;
    if (!findElement(rb, re, "href", &hb, &he)) continue;
    const std::string calUrl = caldav::resolve(homeUrl, elementText(hb, he));
    if (pathOf(calUrl) == homePath) continue;
    const char *tb, *te;
    if (!findElement(rb, re, "resourcetype", &tb, &te)) continue;
    const char *xb, *xe;
    if (!findElement(tb, te, "calendar", &xb, &xe)) continue;
    if (findElement(rb, re, "supported-calendar-component-set", &tb, &te) && tb != te) {
      std::string comps(tb, te);
      if (comps.find("VEVENT") == std::string::npos) continue;  // e.g. Reminders lists
    }
    if (calUrl.size() >= sizeof(out[n].url)) continue;
    CalDavCalendar& c = out[n];
    c = CalDavCalendar();
    snprintf(c.url, sizeof(c.url), "%s", calUrl.c_str());
    std::string name;
    if (findElement(rb, re, "displayname", &tb, &te)) name = elementText(tb, te);
    if (name.empty()) {
      std::string p = pathOf(calUrl);
      name = p.substr(p.rfind('/') + 1);
    }
    utf8Copy(c.name, sizeof(c.name), name.c_str());
    if (findElement(rb, re, "calendar-color", &tb, &te)) {
      c.hasColor = caldav::parseColor(elementText(tb, te).c_str(), &c.color);
    }
    ++n;
  }
  return n;
}

bool CalDavClient::fetchEvents(const std::string& calendarUrl, int64_t start, int64_t end, IcsParser& parser) {
  error_[0] = '\0';
  std::string body =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:prop><c:calendar-data/></d:prop>"
      "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\">"
      "<c:time-range start=\"" +
      caldav::utcStamp(start) + "\" end=\"" + caldav::utcStamp(end) +
      "\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>";
  std::string url = calendarUrl;
  int code = 0;
  for (int hop = 0; hop < 5; ++hop) {
    CalendarDataExtractor extractor(parserSink, &parser);
    std::string location;
    code = t_.request("REPORT", url, "1", body, extractorSink, &extractor, &location);
    extractor.finish();
    if (code >= 300 && code < 400 && !location.empty()) {
      url = caldav::resolve(url, location);
      continue;
    }
    break;
  }
  status_ = code;
  if (code != 207) {
    fail("événements", code);
    return false;
  }
  return true;
}
