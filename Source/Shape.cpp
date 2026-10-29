#include "Shape.h"
#include <algorithm>
#include <cmath>

Shape::Shape()
{
    points = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
    name = "Triangle";
}

float Shape::applyCurve (float t, float curve) noexcept
{
    if (std::abs (curve) < 0.001f)
        return t;

    // curve > 0: slow start, fast end; curve < 0: fast start, slow end.
    const auto exponent = std::exp2 (curve * 3.0f);
    return std::pow (juce::jlimit (0.0f, 1.0f, t), exponent);
}

float Shape::evaluate (float phase) const noexcept
{
    const auto n = points.size();

    if (n == 0) return 0.5f;
    if (n == 1) return points[0].y;
    if (phase <= points[0].x) return points[0].y;

    for (size_t i = 0; i + 1 < n; ++i)
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];

        if (phase < b.x || i + 2 == n)
        {
            const auto width = b.x - a.x;

            if (width <= 1.0e-6f)
                return b.y;

            const auto t = juce::jlimit (0.0f, 1.0f, (phase - a.x) / width);
            return a.y + (b.y - a.y) * applyCurve (t, a.curve);
        }
    }

    return points.back().y;
}

void Shape::normalise()
{
    if (points.empty())
        points = { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.0f } };

    for (auto& p : points)
    {
        p.x = juce::jlimit (0.0f, 1.0f, p.x);
        p.y = juce::jlimit (0.0f, 1.0f, p.y);
        p.curve = juce::jlimit (-1.0f, 1.0f, p.curve);
    }

    std::stable_sort (points.begin(), points.end(),
                      [] (const ShapePoint& a, const ShapePoint& b) { return a.x < b.x; });

    if (points.size() == 1)
        points.push_back (points.front());

    points.front().x = 0.0f;
    points.back().x = 1.0f;
}

void Shape::invert()
{
    for (auto& p : points)
        p.y = 1.0f - p.y;
}

void Shape::reverse()
{
    std::vector<ShapePoint> r;
    r.reserve (points.size());

    for (size_t i = points.size(); i-- > 0;)
    {
        auto p = points[i];
        p.x = 1.0f - p.x;
        // The segment that used to end at this point now starts at it, and is mirrored.
        p.curve = (i > 0) ? -points[i - 1].curve : 0.0f;
        r.push_back (p);
    }

    points = std::move (r);
    normalise();
}

juce::var Shape::toVar() const
{
    juce::Array<juce::var> pts;

    for (const auto& p : points)
        pts.add (juce::Array<juce::var> { p.x, p.y, p.curve });

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("version", 1);
    obj->setProperty ("points", pts);
    return juce::var (obj);
}

Shape Shape::fromVar (const juce::var& v)
{
    Shape s;
    s.points.clear();

    if (auto* arr = v.getProperty ("points", {}).getArray())
    {
        for (const auto& pv : *arr)
        {
            if (auto* p = pv.getArray(); p != nullptr && p->size() >= 2)
                s.points.push_back ({ (float) (double) (*p)[0],
                                      (float) (double) (*p)[1],
                                      p->size() > 2 ? (float) (double) (*p)[2] : 0.0f });
        }
    }

    s.name = v.getProperty ("name", "Untitled").toString();
    s.normalise();
    return s;
}

juce::String Shape::toJSON() const
{
    return juce::JSON::toString (toVar(), true);
}

Shape Shape::fromJSON (const juce::String& json)
{
    return fromVar (juce::JSON::parse (json));
}

Shape Shape::fromSteps (const std::vector<float>& values, const juce::String& shapeName)
{
    Shape s;
    s.points.clear();
    s.name = shapeName;

    const auto n = (int) values.size();

    for (int i = 0; i < n; ++i)
    {
        const auto x0 = (float) i / (float) n;
        const auto x1 = (float) (i + 1) / (float) n;
        s.points.push_back ({ x0, values[(size_t) i], 0.0f });
        s.points.push_back ({ x1, values[(size_t) i], 0.0f });
    }

    s.normalise();
    return s;
}

juce::StringArray Shape::factoryNames()
{
    return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pulse 25%",
             "Exp Rise", "Exp Fall", "Sidechain Duck", "Stairs Up", "Flat" };
}

Shape Shape::factory (const juce::String& n)
{
    Shape s;
    s.points.clear();
    s.name = n;

    if (n == "Sine")
    {
        constexpr int steps = 32;
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = (float) i / (float) steps;
            s.points.push_back ({ x, 0.5f + 0.5f * std::sin (x * juce::MathConstants<float>::twoPi), 0.0f });
        }
    }
    else if (n == "Triangle")       s.points = { { 0, 0, 0 }, { 0.5f, 1, 0 }, { 1, 0, 0 } };
    else if (n == "Saw Up")         s.points = { { 0, 0, 0 }, { 1, 1, 0 } };
    else if (n == "Saw Down")       s.points = { { 0, 1, 0 }, { 1, 0, 0 } };
    else if (n == "Square")         s.points = { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } };
    else if (n == "Pulse 25%")      s.points = { { 0, 1, 0 }, { 0.25f, 1, 0 }, { 0.25f, 0, 0 }, { 1, 0, 0 } };
    else if (n == "Exp Rise")       s.points = { { 0, 0, 0.8f }, { 1, 1, 0 } };
    else if (n == "Exp Fall")       s.points = { { 0, 1, -0.8f }, { 1, 0, 0 } };
    else if (n == "Sidechain Duck") s.points = { { 0, 0, -0.6f }, { 0.45f, 1, 0 }, { 1, 1, 0 } };
    else if (n == "Stairs Up")      return fromSteps ({ 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.75f, 0.5f, 0.25f }, n);
    else                            s.points = { { 0, 0.5f, 0 }, { 1, 0.5f, 0 } };

    s.normalise();
    return s;
}

Shape Shape::random (int numSteps, bool smooth, juce::Random& rng)
{
    std::vector<float> values;
    for (int i = 0; i < numSteps; ++i)
        values.push_back (rng.nextFloat());

    if (! smooth)
        return fromSteps (values, "Random Steps");

    Shape s;
    s.points.clear();
    s.name = "Random Smooth";

    for (int i = 0; i <= numSteps; ++i)
        s.points.push_back ({ (float) i / (float) numSteps, values[(size_t) (i % numSteps)], 0.0f });

    s.normalise();
    return s;
}

bool Shape::operator== (const Shape& o) const
{
    if (points.size() != o.points.size())
        return false;

    for (size_t i = 0; i < points.size(); ++i)
        if (! juce::exactlyEqual (points[i].x, o.points[i].x) || ! juce::exactlyEqual (points[i].y, o.points[i].y)
             || ! juce::exactlyEqual (points[i].curve, o.points[i].curve))
            return false;

    return true;
}
