#include "model.h"
#include <ArduinoJson.h>

static void copyStr(char* dst, size_t size, const char* src) {
  strlcpy(dst, src ? src : "", size);
}

bool parseFrame(const char* line, Snapshot& out, String& error) {
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, line);
  if (de) {
    error = String("json ") + de.c_str();
    return false;
  }
  if ((doc["v"] | 0) != PROTOCOL_VERSION) {
    error = "version";
    return false;
  }

  Snapshot s{};
  s.now = doc["now"] | 0u;
  s.tzMin = doc["tz"] | 0;
  if (s.now == 0) {
    error = "now";
    return false;
  }

  for (JsonObject jp : doc["p"].as<JsonArray>()) {
    if (s.count >= MAX_PROVIDERS) break;
    Provider& p = s.p[s.count++];
    copyStr(p.id, sizeof p.id, jp["id"]);
    copyStr(p.name, sizeof p.name, jp["name"] | jp["id"].as<const char*>());
    copyStr(p.err, sizeof p.err, jp["err"]);
    p.stale = jp["stale"] | false;

    for (JsonObject jw : jp["w"].as<JsonArray>()) {
      if (p.count >= MAX_WINDOWS) break;
      QuotaWindow& w = p.w[p.count++];
      copyStr(w.title, sizeof w.title, jw["t"]);
      w.hasPct = jw["pct"].is<float>();
      w.pct = w.hasPct ? max(0.0f, jw["pct"].as<float>()) : 0.0f;
      w.sev = min<uint8_t>(jw["sev"] | 0, SEV_CRITICAL);
      w.reset = jw["reset"] | 0u;
      copyStr(w.lock, sizeof w.lock, jw["lock"]);
    }
  }

  out = s;
  return true;
}

Severity effectiveSeverity(const QuotaWindow& w) {
  Severity local = SEV_NORMAL;
  if (w.hasPct) {
    if (w.pct >= 90.0f) local = SEV_CRITICAL;
    else if (w.pct >= 75.0f) local = SEV_HIGH;
  }
  return (Severity)max<uint8_t>(local, w.sev);
}
