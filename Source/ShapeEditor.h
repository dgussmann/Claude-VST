#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Shape.h"

// Canvas for drawing a modulation shape.
//
// Points mode:
//   click empty space    -> add a point (and drag it)
//   drag a point         -> move it (end points only move up/down)
//   drag a diamond       -> bend the curve between two points
//   double-click a point -> delete it (right-click also offers Delete)
//   hold Shift           -> ignore the grid while dragging
// Draw mode:
//   click/drag           -> paint one step per grid cell
class ShapeEditor final : public juce::Component,
                          private juce::Timer
{
public:
    enum class Tool { points, draw };

    ShapeEditor();

    void setShape (const Shape&);
    const Shape& getShape() const noexcept { return shape; }

    void setGridDivisions (int n);
    int getGridDivisions() const noexcept { return gridDivisions; }
    void setSnap (bool shouldSnap) { snap = shouldSnap; }
    void setTool (Tool t) { tool = t; repaint(); }

    // Live playback position (phase 0..1 and value 0..1), polled from the processor.
    std::function<float()> getPhase;
    std::function<float()> getValue;

    std::function<void()> onEditStart;              // before a change (for undo)
    std::function<void (const Shape&)> onChange;    // during and after a change

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> toShape (juce::Point<float> screen) const;
    float snapX (float x, bool allowSnap) const;

    int hitPoint (juce::Point<float> screen) const;
    int hitCurveHandle (juce::Point<float> screen) const;
    juce::Point<float> curveHandlePosition (int segment) const;
    void drawStep (juce::Point<float> screen);
    void commit();

    Shape shape;
    Tool tool = Tool::points;
    int gridDivisions = 8;
    bool snap = true;

    int dragPoint = -1;
    int dragCurve = -1;
    float dragCurveStart = 0.0f;
    float dragStartY = 0.0f;
    int hoverPoint = -1;
    int hoverCurve = -1;

    std::vector<float> drawSteps;

    float lastPhase = -1.0f, lastValue = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeEditor)
};
