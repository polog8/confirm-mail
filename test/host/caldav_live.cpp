// Runs CalDavClient against a real CalDAV server over plain HTTP (used with a
// local Radicale instance by caldav_e2e.py). Prints one line per occurrence:
//   <calendar name>|<start>|<end>|<allday>|<title>
// Usage: caldav_live http://127.0.0.1:5232/ user password window_start window_end
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include "../../src/core/caldav.h"

namespace {

std::string base64(const std::string& in) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                 static_cast<unsigned char>(in[i + 2]);
    out += t[v >> 18];
    out += t[(v >> 12) & 63];
    out += t[(v >> 6) & 63];
    out += t[v & 63];
  }
  if (i < in.size()) {
    unsigned v = static_cast<unsigned char>(in[i]) << 16;
    if (i + 1 < in.size()) v |= static_cast<unsigned char>(in[i + 1]) << 8;
    out += t[v >> 18];
    out += t[(v >> 12) & 63];
    out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

struct SocketTransport : CalDavTransport {
  std::string auth;

  int request(const char* method, const std::string& url, const char* depth, const std::string& body, Sink sink,
              void* ctx, std::string* location) override {
    // http://host:port/path
    if (url.compare(0, 7, "http://") != 0) return -1;
    size_t slash = url.find('/', 7);
    std::string hostport = url.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
    std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    std::string host = hostport, port = "80";
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
      host = hostport.substr(0, colon);
      port = hostport.substr(colon + 1);
    }
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
      freeaddrinfo(res);
      return -1;
    }
    freeaddrinfo(res);
    std::string req = std::string(method) + " " + path + " HTTP/1.0\r\nHost: " + hostport +
                      "\r\nAuthorization: Basic " + auth + "\r\nDepth: " + depth +
                      "\r\nContent-Type: application/xml; charset=utf-8\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    if (write(fd, req.data(), req.size()) != static_cast<ssize_t>(req.size())) {
      close(fd);
      return -1;
    }
    std::string head;
    char buf[777];
    bool inBody = false;
    int status = -1;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
      if (inBody) {
        sink(ctx, buf, static_cast<size_t>(n));
        continue;
      }
      head.append(buf, static_cast<size_t>(n));
      size_t end = head.find("\r\n\r\n");
      if (end == std::string::npos) continue;
      inBody = true;
      status = atoi(head.c_str() + head.find(' ') + 1);
      size_t loc = head.find("\r\nLocation: ");
      if (loc == std::string::npos) loc = head.find("\r\nlocation: ");
      if (loc != std::string::npos && location) {
        size_t s = loc + 12;
        *location = head.substr(s, head.find("\r\n", s) - s);
      }
      if (head.size() > end + 4) sink(ctx, head.data() + end + 4, head.size() - end - 4);
    }
    close(fd);
    return status;
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    fprintf(stderr, "usage: %s root_url user password start end\n", argv[0]);
    return 2;
  }
  SocketTransport t;
  t.auth = base64(std::string(argv[2]) + ":" + argv[3]);
  CalDavClient client(t);
  CalDavCalendar cals[8];
  int n = client.discover(argv[1], cals, 8);
  if (n < 0) {
    printf("ERROR %s\n", client.error());
    return 1;
  }
  PosixTz tz("CET-1CEST,M3.5.0,M10.5.0/3");
  const int64_t ws = atoll(argv[4]), we = atoll(argv[5]);
  for (int i = 0; i < n; ++i) {
    printf("CALENDAR %s|%06X|%d|%s\n", cals[i].name, cals[i].color, cals[i].hasColor, cals[i].url);
    IcsParser* p = new IcsParser(tz, ws, we);
    if (!client.fetchEvents(cals[i].url, ws, we, *p)) {
      printf("ERROR %s\n", client.error());
      delete p;
      return 1;
    }
    p->finish();
    for (int k = 0; k < p->count(); ++k) {
      const IcsOccurrence& o = p->at(k);
      printf("%s|%lld|%lld|%d|%s\n", cals[i].name, static_cast<long long>(o.start), static_cast<long long>(o.end),
             o.allDay, o.title);
    }
    delete p;
  }
  return 0;
}
