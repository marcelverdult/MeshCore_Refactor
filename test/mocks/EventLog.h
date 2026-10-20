#pragma once

// One ordered record of the calls a role makes on its collaborators while it boots:
// the file system lookups, the radio and board set-up, the sensor manager and (in the
// repeater with a bridge) the bridge. Each mock appends a short tag when it is
// called, so a test can say which call came before which, which no single mock can
// tell on its own. A test clears the log before the step it wants to watch.

#include <string>
#include <vector>

inline std::vector<std::string> g_events;

inline void logEvent(const std::string& what) { g_events.push_back(what); }

// Position of the first event that equals `what`, or -1 if it never happened.
inline int eventIndex(const std::string& what) {
  for (size_t i = 0; i < g_events.size(); i++) {
    if (g_events[i] == what) return (int)i;
  }
  return -1;
}

// How many times `what` was logged.
inline int eventCount(const std::string& what) {
  int n = 0;
  for (const std::string& e : g_events) {
    if (e == what) n++;
  }
  return n;
}
