#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// One breakpoint of a modulation shape.
// x = position in the cycle (0..1), y = value (0..1),
// curve = bend of the segment that STARTS at this point (-1..1, 0 = straight).
struct ShapePoint
{
    float x = 0.0f;
    float y = 0.0f;
    float curve = 0.0f;
};

// A drawable, loopable modulation shape made of breakpoints.
// The first point always sits at x = 0 and the last at x = 1.
// Two points may share the same x to make an instant jump (square / steps).
class Shape
{
public:
    Shape();

    std::vector<ShapePoint> points;
    juce::String name { "Untitled" };

    // Value of the shape at a phase in [0, 1).
    float evaluate (float phase) const noexcept;

    // Keeps points sorted, clamped and anchored at both ends.
    void normalise();

    void invert();
    void reverse();

    // Bend applied to a 0..1 position inside a segment.
    static float applyCurve (float t, float curve) noexcept;

    // JSON round trip (used for shape files and plugin state).
    juce::var toVar() const;
    static Shape fromVar (const juce::var&);
    juce::String toJSON() const;
    static Shape fromJSON (const juce::String&);

    // Builds a stepped shape from per-step values.
    static Shape fromSteps (const std::vector<float>& values, const juce::String& name);

    // Factory shapes.
    static juce::StringArray factoryNames();
    static Shape factory (const juce::String& name);
    static Shape random (int numSteps, bool smooth, juce::Random&);

    bool operator== (const Shape&) const;
    bool operator!= (const Shape& o) const { return ! operator== (o); }
};
