// Prints the occurrences IcsParser finds in a file, one per line:
//   <start_utc> <end_utc> <allday> <title>
// Usage: ics_dump file.ics <window_start_utc> <window_end_utc> [posix_tz]
// Used by diff_test.py to compare against the python reference implementation.
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "../../src/core/ics.h"

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s file.ics start end [tz]\n", argv[0]);
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 2;
  PosixTz tz(argc > 4 ? argv[4] : "CET-1CEST,M3.5.0,M10.5.0/3");
  auto* p = new IcsParser(tz, atoll(argv[2]), atoll(argv[3]));
  char buf[333];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) p->feed(buf, n);
  fclose(f);
  p->finish();
  std::vector<IcsOccurrence> v;
  for (int i = 0; i < p->count(); ++i) v.push_back(p->at(i));
  std::sort(v.begin(), v.end(), [](const IcsOccurrence& a, const IcsOccurrence& b) {
    return a.start != b.start ? a.start < b.start : a.uid < b.uid;
  });
  for (auto& o : v) printf("%lld %lld %d %s\n", (long long)o.start, (long long)o.end, o.allDay, o.title);
  delete p;
  return 0;
}
