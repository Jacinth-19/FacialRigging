#include "anim/key_layer.h"
#include "core/json_io.h"
#include <algorithm>
#include <cmath>

namespace fr {

const char* tangentModeName(TangentMode m) {
    switch (m) { case TangentMode::Auto: return "Auto"; case TangentMode::Flat: return "Flat"; case TangentMode::Linear: return "Linear"; default: return "Free"; }
}

float KeyCurve::evaluate(float t) const {
    if (keys.empty()) return 0.0f;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key& k) { return v < k.time; });
    const Key& b = *it; const Key& a = *(it - 1);
    const float dt = b.time - a.time; if (dt <= 1e-6f) return b.value;
    const float u = (t - a.time) / dt, u2 = u * u, u3 = u2 * u;
    const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    return h00 * a.value + h10 * dt * a.outSlope + h01 * b.value + h11 * dt * b.inSlope;
}

int KeyCurve::findKey(float t, float eps) const {
    for (size_t i = 0; i < keys.size(); ++i) if (std::fabs(keys[i].time - t) <= eps) return int(i);
    return -1;
}

int KeyCurve::addKey(float t, float value, float eps) {
    int ex = findKey(t, eps);
    if (ex >= 0) { keys[size_t(ex)].value = value; updateTangents(); return ex; }
    Key k; k.time = t; k.value = value;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key& kk) { return v < kk.time; });
    int idx = int(it - keys.begin());
    keys.insert(it, k);
    updateTangents();
    return idx;
}

void KeyCurve::removeKey(int index) {
    if (index < 0 || size_t(index) >= keys.size()) return;
    keys.erase(keys.begin() + index); updateTangents();
}

int KeyCurve::moveKey(int index, float newTime) {
    if (index < 0 || size_t(index) >= keys.size()) return index;
    Key k = keys[size_t(index)]; k.time = newTime;
    keys.erase(keys.begin() + index);
    // don't land on top of another key
    for (const Key& o : keys) if (std::fabs(o.time - newTime) < 1e-4f) k.time = newTime + 2e-4f;
    auto it = std::upper_bound(keys.begin(), keys.end(), k.time, [](float v, const Key& kk) { return v < kk.time; });
    int idx = int(it - keys.begin()); keys.insert(it, k); updateTangents(); return idx;
}

void KeyCurve::setTangentMode(int index, TangentMode m, bool inOnly, bool outOnly) {
    if (index < 0 || size_t(index) >= keys.size()) return;
    Key& k = keys[size_t(index)];
    if (!outOnly) k.inMode = m;
    if (!inOnly) k.outMode = m;
    if (inOnly || outOnly) k.broken = true;
    updateTangents();
}

void KeyCurve::updateTangents() {
    const size_t n = keys.size();
    for (size_t i = 0; i < n; ++i) {
        Key& k = keys[i];
        const Key* p = i > 0 ? &keys[i - 1] : nullptr; const Key* q = i + 1 < n ? &keys[i + 1] : nullptr;
        const float sIn = p ? (k.value - p->value) / std::max(1e-6f, k.time - p->time) : 0.0f;
        const float sOut = q ? (q->value - k.value) / std::max(1e-6f, q->time - k.time) : 0.0f;
        // Auto: Catmull-Rom slope, zeroed at extrema and clamped so the segment can't overshoot (Fritsch-Carlson)
        float autoS = 0.0f;
        if (p && q) {
            if ((sIn > 0) == (sOut > 0) && sIn != 0 && sOut != 0) {
                autoS = (q->value - p->value) / std::max(1e-6f, q->time - p->time);
                const float lim = 3.0f * std::min(std::fabs(sIn), std::fabs(sOut));
                autoS = std::copysign(std::min(std::fabs(autoS), lim), autoS);
            }
        }
        auto resolve = [&](TangentMode m, float linear, float current) { switch (m) { case TangentMode::Auto: return autoS; case TangentMode::Flat: return 0.0f; case TangentMode::Linear: return linear; default: return current; } };
        k.inSlope = resolve(k.inMode, sIn, k.inSlope);
        k.outSlope = resolve(k.outMode, sOut, k.outSlope);
        if (!k.broken && k.inMode == TangentMode::Free && k.outMode == TangentMode::Free) k.inSlope = k.outSlope;
    }
}

bool KeyLayer::empty() const { for (const auto& c : curves) if (!c.keys.empty()) return false; return true; }
int KeyLayer::keyCount() const { int n = 0; for (const auto& c : curves) n += int(c.keys.size()); return n; }
const KeyCurve* KeyLayer::find(const std::string& target, int axis) const { for (const auto& c : curves) if (c.axis == axis && c.target == target) return &c; return nullptr; }
KeyCurve* KeyLayer::find(const std::string& target, int axis) { for (auto& c : curves) if (c.axis == axis && c.target == target) return &c; return nullptr; }
KeyCurve& KeyLayer::get(const std::string& target, int axis) { if (KeyCurve* c = find(target, axis)) return *c; KeyCurve c; c.target = target; c.axis = axis; curves.push_back(c); return curves.back(); }
float KeyLayer::evaluate(const std::string& target, int axis, float t) const {
    if (!enabled) return 0.0f;
    const KeyCurve* c = find(target, axis);
    return c ? weight * c->evaluate(t) : 0.0f;
}
void KeyLayer::prune() { curves.erase(std::remove_if(curves.begin(), curves.end(), [](const KeyCurve& c) { return c.keys.empty(); }), curves.end()); }

std::string KeyLayer::toJson() const {
    std::string o = "{\"weight\":" + json::num(weight) + ",\"enabled\":" + (enabled ? "true" : "false") + ",\"curves\":[";
    bool first = true;
    for (const KeyCurve& c : curves) {
        if (c.keys.empty()) continue;
        if (!first) o += ',';
        first = false;
        o += "{\"target\":" + json::str(c.target) + ",\"axis\":" + std::to_string(c.axis) + ",\"keys\":[";
        for (size_t i = 0; i < c.keys.size(); ++i) {
            const Key& k = c.keys[i]; if (i) o += ',';
            o += "[" + json::num(k.time) + "," + json::num(k.value) + "," + json::num(k.inSlope) + "," + json::num(k.outSlope) + "," + std::to_string(int(k.inMode)) + "," + std::to_string(int(k.outMode)) + "," + (k.broken ? "1" : "0") + "]";
        }
        o += "]}";
    }
    return o + "]}";
}

bool KeyLayer::fromJson(const std::string& text, size_t& pos) {
    json::Cursor p(text); p.i = pos;
    KeyLayer L;
    bool ok = json::objEach(p, [&](const std::string& k) {
        if (k == "weight") return p.num(L.weight);
        if (k == "enabled") return json::boolean(p, L.enabled);
        if (k == "curves") return json::arrEach(p, [&]() {
            KeyCurve c; std::vector<std::vector<float>> keys;
            bool r = json::objEach(p, [&](const std::string& ck) {
                if (ck == "target") return p.str(c.target);
                if (ck == "axis") { float v; if (!p.num(v)) return false; c.axis = int(v); return true; }
                if (ck == "keys") return p.vecArr(keys);
                return p.skipValue(); });
            for (const auto& v : keys) {
                if (v.size() < 2) return false;
                Key key; key.time = v[0]; key.value = v[1];
                if (v.size() >= 4) { key.inSlope = v[2]; key.outSlope = v[3]; }
                if (v.size() >= 6) { key.inMode = TangentMode(std::clamp(int(v[4]), 0, 3)); key.outMode = TangentMode(std::clamp(int(v[5]), 0, 3)); }
                if (v.size() >= 7) key.broken = v[6] != 0;
                c.keys.push_back(key);
            }
            std::sort(c.keys.begin(), c.keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
            c.updateTangents(); L.curves.push_back(std::move(c)); return r; });
        return p.skipValue(); });
    if (!ok) return false;
    pos = p.i; *this = std::move(L); return true;
}

} // namespace fr
