#include "quota_state.h"
#include <cJSON.h>
#include <cmath>
#include <cstring>
namespace quota {
namespace {
const cJSON* field(const cJSON* obj, const char* key) { return cJSON_GetObjectItemCaseSensitive(obj, key); }
bool finite(const cJSON* obj) { return cJSON_IsNumber(obj) && std::isfinite(obj->valuedouble); }
bool integer(const cJSON* obj, double min, double max) {
    return finite(obj) && obj->valuedouble >= min && obj->valuedouble <= max && std::floor(obj->valuedouble) == obj->valuedouble;
}
bool parseWindow(const cJSON* obj, Window& win, int64_t sentMs, int64_t nowUs) {
    if (cJSON_IsNull(obj)) return true;
    if (!cJSON_IsObject(obj)) return false;
    auto u = field(obj, "u"), r = field(obj, "r"), s = field(obj, "s");
    if (!u || !r || !cJSON_IsString(s)) return false;
    if (!cJSON_IsNull(u) && (!finite(u) || u->valuedouble < 0 || u->valuedouble > 100)) return false;
    if (!cJSON_IsNull(r) && !integer(r, 1, 99999999999.)) return false;
    if (!strcmp(s->valuestring, "api")) win.source = Source::Api;
    else if (!strcmp(s->valuestring, "estimated")) win.source = Source::Estimated;
    else if (!strcmp(s->valuestring, "stale")) win.source = Source::Stale;
    else if (!strcmp(s->valuestring, "unavailable")) win.source = Source::Unavailable;
    else return false;
    if (win.source == Source::Api && !finite(u)) return false;
    if (win.source == Source::Estimated && (!cJSON_IsNull(u) || !integer(r, 1, 99999999999.))) return false;
    win.hasUsage = finite(u) && (win.source == Source::Api || win.source == Source::Stale);
    win.used = win.hasUsage ? u->valuedouble : 0;
    win.resetSec = cJSON_IsNumber(r) ? static_cast<int64_t>(r->valuedouble) : 0;
    win.updatedUs = nowUs;
    win.clock.anchor(win.resetSec, sentMs, nowUs);
    return true;
}
}
bool Store::apply(const char* line, size_t length, int64_t receivedUs, bool resetConfirmed[2]) {
    resetConfirmed[0] = resetConfirmed[1] = false;
    if (!line || !length || length >= 256 || memchr(line, '\0', length)) return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(line, length + 1, &end, true);
    if (!root) return false;
    Window next[2];
    auto v = field(root, "v"), q = field(root, "q"), t = field(root, "t");
    bool valid = cJSON_IsObject(root) && integer(v, 1, 1) && integer(q, 1, 9007199254740991.) &&
        q->valuedouble > sequence && integer(t, 1, 99999999999999.) &&
        parseWindow(field(root, "h"), next[0], t->valuedouble, receivedUs) &&
        parseWindow(field(root, "w"), next[1], t->valuedouble, receivedUs);
    if (valid) {
        for (int i = 0; i < 2; ++i) {
            resetConfirmed[i] = lastApiReset[i] > 0 && next[i].source == Source::Api && next[i].resetSec > lastApiReset[i];
            // Stale resends must never make cached API data fresh.
            if (next[i].source == Source::Stale && windows[i].resetSec == next[i].resetSec)
                next[i].updatedUs = windows[i].updatedUs;
            if (next[i].source == Source::Api && next[i].resetSec > 0) lastApiReset[i] = next[i].resetSec;
            windows[i] = next[i];
        }
        sequence = q->valuedouble;
    }
    cJSON_Delete(root);
    return valid;
}
}
