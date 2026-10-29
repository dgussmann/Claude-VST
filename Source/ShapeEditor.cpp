#include "ShapeEditor.h"
#include "Theme.h"
#include <cmath>

ShapeEditor::ShapeEditor()
{
    setRepaintsOnMouseActivity (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

void ShapeEditor::setShape (const Shape& s)
{
    shape = s;
    shape.normalise();
    dragPoint = dragCurve = hoverPoint = hoverCurve = -1;
    repaint();
}

void ShapeEditor::setGridDivisions (int n)
{
    gridDivisions = juce::jlimit (1, 64, n);
    repaint();
}

//==============================================================================
juce::Rectangle<float> ShapeEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 10.0f);
}

juce::Point<float> ShapeEditor::toScreen (float x, float y) const
{
    auto a = plotArea();
    return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
}

juce::Point<float> ShapeEditor::toShape (juce::Point<float> p) const
{
    auto a = plotArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()),
             juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
}

float ShapeEditor::snapX (float x, bool allowSnap) const
{
    if (! (snap && allowSnap))
        return x;
    return std::round (x * (float) gridDivisions) / (float) gridDivisions;
}

int ShapeEditor::hitPoint (juce::Point<float> screen) const
{
    int best = -1;
    float bestDist = 9.0f;

    for (int i = 0; i < (int) shape.points.size(); ++i)
    {
        const auto d = toScreen (shape.points[(size_t) i].x, shape.points[(size_t) i].y).getDistanceFrom (screen);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

juce::Point<float> ShapeEditor::curveHandlePosition (int seg) const
{
    const auto& a = shape.points[(size_t) seg];
    const auto& b = shape.points[(size_t) seg + 1];
    const auto x = (a.x + b.x) * 0.5f;
    const auto y = a.y + (b.y - a.y) * Shape::applyCurve (0.5f, a.curve);
    return toScreen (x, y);
}

int ShapeEditor::hitCurveHandle (juce::Point<float> screen) const
{
    for (int i = 0; i + 1 < (int) shape.points.size(); ++i)
    {
        const auto& a = shape.points[(size_t) i];
        const auto& b = shape.points[(size_t) i + 1];
        if (b.x - a.x < 0.02f || std::abs (b.y - a.y) < 0.01f)
            continue;
        if (curveHandlePosition (i).getDistanceFrom (screen) < 8.0f)
            return i;
    }
    return -1;
}

//==============================================================================
void ShapeEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto a = plotArea();

    // Grid
    for (int i = 0; i <= gridDivisions; ++i)
    {
        const auto x = a.getX() + a.getWidth() * (float) i / (float) gridDivisions;
        const bool strong = gridDivisions >= 8 ? (i % (gridDivisions / 4) == 0) : true;
        g.setColour (strong ? Theme::gridStrong : Theme::grid);
        g.drawVerticalLine ((int) std::round (x), a.getY(), a.getBottom());
    }
    for (int i = 0; i <= 4; ++i)
    {
        const auto y = a.getBottom() - a.getHeight() * (float) i / 4.0f;
        g.setColour (i == 2 ? Theme::gridStrong : Theme::grid);
        g.drawHorizontalLine ((int) std::round (y), a.getX(), a.getRight());
    }

    // Curve
    Path line;
    const auto& pts = shape.points;
    line.startNewSubPath (toScreen (pts.front().x, pts.front().y));

    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const auto& p0 = pts[i];
        const auto& p1 = pts[i + 1];
        const int steps = (std::abs (p0.curve) < 0.001f || p1.x - p0.x < 1.0e-4f) ? 1 : 32;

        for (int s = 1; s <= steps; ++s)
        {
            const auto t = (float) s / (float) steps;
            line.lineTo (toScreen (p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * Shape::applyCurve (t, p0.curve)));
        }
    }

    Path fill (line);
    fill.lineTo (a.getRight(), a.getBottom());
    fill.lineTo (a.getX(), a.getBottom());
    fill.closeSubPath();

    g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.32f), a.getX(), a.getY(),
                                       Theme::accent.withAlpha (0.02f), a.getX(), a.getBottom(), false));
    g.fillPath (fill);
    g.setColour (Theme::accent);
    g.strokePath (line, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // Points and curve handles
    if (tool == Tool::points)
    {
        for (int i = 0; i + 1 < (int) pts.size(); ++i)
        {
            const auto& p0 = pts[(size_t) i];
            const auto& p1 = pts[(size_t) i + 1];
            if (p1.x - p0.x < 0.02f || std::abs (p1.y - p0.y) < 0.01f)
                continue;

            const auto c = curveHandlePosition (i);
            const bool active = (i == hoverCurve || i == dragCurve);
            Path diamond;
            const auto r = active ? 5.0f : 3.5f;
            diamond.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
            g.setColour (active ? Theme::accent2 : Theme::accent.withAlpha (0.45f));
            g.fillPath (diamond);
        }

        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const auto c = toScreen (pts[(size_t) i].x, pts[(size_t) i].y);
            const bool active = (i == hoverPoint || i == dragPoint);
            const auto r = active ? 6.0f : 4.5f;
            g.setColour (active ? Theme::accent : Theme::panel);
            g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c));
            g.setColour (Theme::accent);
            g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (c), 1.5f);
        }
    }

    // Playhead
    if (lastPhase >= 0.0f)
    {
        const auto x = a.getX() + lastPhase * a.getWidth();
        g.setColour (Theme::accent2.withAlpha (0.55f));
        g.drawVerticalLine ((int) std::round (x), a.getY(), a.getBottom());

        const auto dot = toScreen (lastPhase, shape.evaluate (lastPhase));
        g.setColour (Theme::accent2);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre (dot));
    }

    // Mode hint
    g.setColour (Theme::textDim.withAlpha (0.7f));
    g.setFont (FontOptions (11.0f));
    const auto hint = tool == Tool::points
        ? String ("Click: add point  -  Drag diamond: curve  -  Double-click: delete  -  Shift: no snap")
        : String ("Draw: click and drag to paint steps");
    g.drawText (hint, a.reduced (6.0f, 4.0f), Justification::topRight, true);
}

void ShapeEditor::timerCallback()
{
    if (! getPhase)
        return;

    const auto p = getPhase();
    if (std::abs (p - lastPhase) > 0.0005f)
    {
        lastPhase = p;
        repaint();
    }
}

//==============================================================================
void ShapeEditor::mouseMove (const juce::MouseEvent& e)
{
    if (tool != Tool::points)
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        return;
    }

    const auto pos = e.position;
    const auto hp = hitPoint (pos);
    const auto hc = hp < 0 ? hitCurveHandle (pos) : -1;

    if (hp != hoverPoint || hc != hoverCurve)
    {
        hoverPoint = hp;
        hoverCurve = hc;
        repaint();
    }

    setMouseCursor (hp >= 0 ? juce::MouseCursor::DraggingHandCursor
                  : hc >= 0 ? juce::MouseCursor::UpDownResizeCursor
                            : juce::MouseCursor::CrosshairCursor);
}

void ShapeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverPoint >= 0 || hoverCurve >= 0)
    {
        hoverPoint = hoverCurve = -1;
        repaint();
    }
}

void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;

    if (e.mods.isPopupMenu())
    {
        const auto hp = hitPoint (pos);
        const auto hc = hp < 0 ? hitCurveHandle (pos) : -1;
        const auto last = (int) shape.points.size() - 1;

        juce::PopupMenu m;
        if (hp > 0 && hp < last)  m.addItem (1, "Delete point");
        if (hp >= 0 && hp < last) m.addItem (2, "Straighten curve");
        if (hc >= 0)              m.addItem (2, "Straighten curve");
        if (m.getNumItems() == 0)
            return;

        const auto target = hp >= 0 ? hp : hc;
        juce::Component::SafePointer<ShapeEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                         [safe, target] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& self = *safe;
            if (self.onEditStart) self.onEditStart();
            if (r == 1) self.shape.points.erase (self.shape.points.begin() + target);
            if (r == 2) self.shape.points[(size_t) target].curve = 0.0f;
            self.hoverPoint = self.hoverCurve = -1;
            self.commit();
        });
        return;
    }

    if (tool == Tool::draw)
    {
        if (onEditStart) onEditStart();
        drawSteps.assign ((size_t) gridDivisions, 0.0f);
        for (int i = 0; i < gridDivisions; ++i)
            drawSteps[(size_t) i] = shape.evaluate (((float) i + 0.5f) / (float) gridDivisions);
        drawStep (pos);
        return;
    }

    dragPoint = hitPoint (pos);
    if (dragPoint >= 0)
    {
        if (onEditStart) onEditStart();
        return;
    }

    dragCurve = hitCurveHandle (pos);
    if (dragCurve >= 0)
    {
        if (onEditStart) onEditStart();
        dragCurveStart = shape.points[(size_t) dragCurve].curve;
        dragStartY = pos.y;
        return;
    }

    // Add a new point.
    if (onEditStart) onEditStart();
    auto s = toShape (pos);
    s.x = snapX (s.x, ! e.mods.isShiftDown());
    s.x = juce::jlimit (0.0001f, 0.9999f, s.x);

    size_t insertAt = 1;
    while (insertAt < shape.points.size() - 1 && shape.points[insertAt].x <= s.x)
        ++insertAt;

    shape.points.insert (shape.points.begin() + (long) insertAt, { s.x, s.y, 0.0f });
    dragPoint = (int) insertAt;
    hoverPoint = dragPoint;
    commit();
}

void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.position;

    if (tool == Tool::draw)
    {
        drawStep (pos);
        return;
    }

    if (dragPoint >= 0)
    {
        auto& pts = shape.points;
        const auto last = (int) pts.size() - 1;
        auto s = toShape (pos);
        auto& p = pts[(size_t) dragPoint];

        if (dragPoint > 0 && dragPoint < last)
        {
            const auto lo = pts[(size_t) dragPoint - 1].x;
            const auto hi = pts[(size_t) dragPoint + 1].x;
            p.x = juce::jlimit (lo, hi, snapX (s.x, ! e.mods.isShiftDown()));
        }

        auto y = s.y;
        if (snap && ! e.mods.isShiftDown())
        {
            const auto nearest = std::round (y * 8.0f) / 8.0f;   // gentle magnet to eighths
            if (std::abs (nearest - y) < 0.02f)
                y = nearest;
        }
        p.y = y;
        commit();
        return;
    }

    if (dragCurve >= 0)
    {
        const auto& a = shape.points[(size_t) dragCurve];
        const auto& b = shape.points[(size_t) dragCurve + 1];
        const auto dy = (dragStartY - pos.y) / plotArea().getHeight();
        const auto direction = b.y >= a.y ? 1.0f : -1.0f;
        shape.points[(size_t) dragCurve].curve = juce::jlimit (-1.0f, 1.0f, dragCurveStart - dy * 2.5f * direction);
        commit();
    }
}

void ShapeEditor::mouseUp (const juce::MouseEvent&)
{
    dragPoint = dragCurve = -1;
    repaint();
}

void ShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tool != Tool::points)
        return;

    const auto hp = hitPoint (e.position);
    const auto last = (int) shape.points.size() - 1;

    if (hp > 0 && hp < last)
    {
        if (onEditStart) onEditStart();
        shape.points.erase (shape.points.begin() + hp);
        hoverPoint = dragPoint = -1;
        commit();
        return;
    }

    if (const auto hc = hitCurveHandle (e.position); hc >= 0)
    {
        if (onEditStart) onEditStart();
        shape.points[(size_t) hc].curve = 0.0f;
        commit();
    }
}

void ShapeEditor::drawStep (juce::Point<float> pos)
{
    const auto s = toShape (pos);
    const auto n = (int) drawSteps.size();
    if (n == 0)
        return;

    const auto cell = juce::jlimit (0, n - 1, (int) (s.x * (float) n));
    drawSteps[(size_t) cell] = s.y;

    auto name = shape.name;
    shape = Shape::fromSteps (drawSteps, name);
    commit();
}

void ShapeEditor::commit()
{
    shape.normalise();
    repaint();
    if (onChange)
        onChange (shape);
}
