#pragma once
// Non-destructive key layer on top of a baked clip (Maya-style additive animation layer).
// The lip-sync generator writes dense baked curves; the user's timeline edits live here as sparse
// keys with tangents. Composite = baked(t) + layer(t), so regenerating the baked clip keeps the
// hand-made fixes. Bone rotations are edited as Euler degrees per axis and composed as
// baked * quat(euler(layer)).
#include <cstdint>
#include <string>
#include <vector>

namespace fr {

enum class TangentMode : uint8_t { Auto, Flat, Linear, Free };
const char* tangentModeName(TangentMode m);

struct Key {
    float time = 0.0f;
    float value = 0.0f;              ///< delta on top of the baked curve
    float inSlope = 0.0f, outSlope = 0.0f;   ///< value units per second (Free / cached Auto)
    TangentMode inMode = TangentMode::Auto, outMode = TangentMode::Auto;
    bool broken = false;             ///< in/out edited independently
};

struct KeyCurve {
    std::string target;              ///< blendshape or bone name
    int axis = -1;                   ///< -1 blend weight; 0/1/2 bone rotation X/Y/Z (degrees)
    std::vector<Key> keys;           ///< sorted by time

    bool empty() const { return keys.empty(); }
    /// Cubic Hermite between keys, constant outside. 0 when there are no keys.
    float evaluate(float t) const;
    /// Inserts (or replaces a key within `eps` seconds) and returns its index.
    int addKey(float t, float value, float eps = 1e-4f);
    void removeKey(int index);
    int findKey(float t, float eps = 1e-4f) const;
    /// Moves a key in time keeping the array sorted; returns the new index.
    int moveKey(int index, float newTime);
    void setTangentMode(int index, TangentMode m, bool inOnly = false, bool outOnly = false);
    /// Recomputes Auto/Linear slopes (Free slopes are left alone). Called by the editing functions.
    void updateTangents();
};

struct KeyLayer {
    std::vector<KeyCurve> curves;
    float weight = 1.0f;
    bool enabled = true;
    bool empty() const;
    int keyCount() const;
    const KeyCurve* find(const std::string& target, int axis = -1) const;
    KeyCurve* find(const std::string& target, int axis = -1);
    KeyCurve& get(const std::string& target, int axis = -1);   ///< creates when missing
    /// Layer contribution (0 when disabled / missing).
    float evaluate(const std::string& target, int axis, float t) const;
    void clear() { curves.clear(); }
    /// Drops curves that have no keys.
    void prune();
    /// JSON object text `{"weight":..,"enabled":..,"curves":[{"target":..,"axis":..,"keys":[[t,v,in,out,inMode,outMode,broken],..]}]}`.
    std::string toJson() const;
    /// Parses the object produced by toJson from `text` starting at `pos` (advanced past it). Returns false on malformed input.
    bool fromJson(const std::string& text, size_t& pos);
};

} // namespace fr
