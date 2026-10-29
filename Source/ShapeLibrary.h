#pragma once

#include "Shape.h"

// Saves and loads user shapes as small JSON files (*.shape).
// macOS: ~/Library/Application Support/ShapeMod/Shapes
namespace ShapeLibrary
{
    inline juce::File folder()
    {
       #if JUCE_MAC
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Application Support");
       #else
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        auto dir = base.getChildFile ("ShapeMod").getChildFile ("Shapes");
        dir.createDirectory();
        return dir;
    }

    // Shapes saved directly in the folder (your own saves).
    inline juce::Array<juce::File> list()
    {
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.shape");
        files.sort();
        return files;
    }

    // Every shape, including those in subfolders (e.g. imported libraries), sorted by path.
    inline juce::Array<juce::File> listAll()
    {
        auto files = folder().findChildFiles (juce::File::findFiles, true, "*.shape");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            const auto root = folder();
            return a.getRelativePathFrom (root).compareNatural (b.getRelativePathFrom (root)) < 0;
        });
        return files;
    }

    inline juce::String safeFileName (const juce::String& name)
    {
        auto clean = juce::File::createLegalFileName (name.trim());
        return clean.isEmpty() ? juce::String ("Untitled") : clean;
    }

    inline bool save (const Shape& shape, const juce::String& name)
    {
        auto copy = shape;
        copy.name = name.trim().isEmpty() ? juce::String ("Untitled") : name.trim();
        auto file = folder().getChildFile (safeFileName (copy.name) + ".shape");
        return file.replaceWithText (copy.toJSON());
    }

    inline bool load (const juce::File& file, Shape& out)
    {
        if (! file.existsAsFile())
            return false;

        auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (! parsed.isObject())
            return false;

        out = Shape::fromVar (parsed);
        if (out.name.isEmpty() || out.name == "Untitled")
            out.name = file.getFileNameWithoutExtension();
        return true;
    }
}
