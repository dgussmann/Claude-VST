#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    inline const juce::Colour background   { 0xff121418 };
    inline const juce::Colour panel        { 0xff1a1d23 };
    inline const juce::Colour panelRaised  { 0xff22262e };
    inline const juce::Colour outline      { 0xff2e333d };
    inline const juce::Colour grid         { 0xff252a32 };
    inline const juce::Colour gridStrong   { 0xff353b46 };
    inline const juce::Colour text         { 0xffe7e9ed };
    inline const juce::Colour textDim      { 0xff8b929e };
    inline const juce::Colour accent       { 0xffff9a3c };   // shape / primary
    inline const juce::Colour accent2      { 0xff4fd6c4 };   // playhead / active
    inline const juce::Colour danger       { 0xffff5d5d };
}

class ShapeModLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ShapeModLookAndFeel()
    {
        using namespace juce;
        setColourScheme ({ Theme::background, Theme::panel, Theme::panelRaised, Theme::outline,
                           Theme::text, Theme::panelRaised, Theme::text, Theme::accent, Theme::text });

        setColour (ResizableWindow::backgroundColourId, Theme::background);
        setColour (TextButton::buttonColourId, Theme::panelRaised);
        setColour (TextButton::buttonOnColourId, Theme::accent.withAlpha (0.85f));
        setColour (TextButton::textColourOffId, Theme::text);
        setColour (TextButton::textColourOnId, Colours::black);
        setColour (ComboBox::backgroundColourId, Theme::panelRaised);
        setColour (ComboBox::outlineColourId, Theme::outline);
        setColour (ComboBox::textColourId, Theme::text);
        setColour (ComboBox::arrowColourId, Theme::textDim);
        setColour (PopupMenu::backgroundColourId, Theme::panelRaised);
        setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.85f));
        setColour (PopupMenu::highlightedTextColourId, Colours::black);
        setColour (PopupMenu::textColourId, Theme::text);
        setColour (Slider::rotarySliderFillColourId, Theme::accent);
        setColour (Slider::rotarySliderOutlineColourId, Theme::outline);
        setColour (Slider::thumbColourId, Theme::accent);
        setColour (Slider::trackColourId, Theme::accent.withAlpha (0.7f));
        setColour (Slider::backgroundColourId, Theme::outline);
        setColour (Slider::textBoxTextColourId, Theme::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Label::textColourId, Theme::text);
        setColour (ToggleButton::textColourId, Theme::text);
        setColour (ToggleButton::tickColourId, Theme::accent);
        setColour (ToggleButton::tickDisabledColourId, Theme::textDim);
        setColour (AlertWindow::backgroundColourId, Theme::panel);
        setColour (AlertWindow::textColourId, Theme::text);
        setColour (TextEditor::backgroundColourId, Theme::panelRaised);
        setColour (TextEditor::textColourId, Theme::text);
        setColour (TextEditor::outlineColourId, Theme::outline);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        auto radius = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto lineW = jmax (2.5f, radius * 0.16f);
        auto arcRadius = radius - lineW * 0.5f;

        Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::outline);
        g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        auto angle = startAngle + pos * (endAngle - startAngle);
        Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (slider.isEnabled() ? Theme::accent : Theme::textDim);
        g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        auto knobR = arcRadius - lineW * 1.4f;
        g.setColour (Theme::panelRaised);
        g.fillEllipse (Rectangle<float> (knobR * 2, knobR * 2).withCentre (centre));

        Point<float> tip (centre.x + (knobR - 3.0f) * std::sin (angle), centre.y - (knobR - 3.0f) * std::cos (angle));
        g.setColour (Theme::text);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.35f, angle), tip }, 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bg;
        if (down) c = c.brighter (0.15f);
        else if (highlighted) c = c.brighter (0.07f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.55f));
    }
};
