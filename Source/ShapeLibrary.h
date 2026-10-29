#pragma once

#include "Shape.h"

// Saves and loads user shapes as small JSON files (*.shape).
// macOS: ~/Library/Audio/Presets/ShapeMod/Shapes (older builds used ~/Library/Application Support/ShapeMod/Shapes)
namespace ShapeLibrary
{
    // Where shapes live. macOS: ~/Library/Audio/Presets/ShapeMod/Shapes, the usual place for plugin presets.
    inline juce::File folder()
    {
       #if JUCE_MAC
        auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Audio/Presets/ShapeMod/Shapes");
       #else
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("ShapeMod").getChildFile ("Shapes");
       #endif
        dir.createDirectory();
        return dir;
    }

    // Builds up to 7 saved shapes in ~/Library/Application Support/ShapeMod/Shapes; still read from there.
    inline juce::File legacyFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/ShapeMod/Shapes");
       #else
        return {};
       #endif
    }

    inline juce::Array<juce::File> looseShapesIn (const juce::File& dir)
    {
        if (dir == juce::File() || ! dir.isDirectory())
            return {};
        return dir.findChildFiles (juce::File::findFiles, false, "*.shape");
    }

    // Shapes saved directly in the folder (your own saves), including those from the old location.
    inline juce::Array<juce::File> list()
    {
        auto files = looseShapesIn (folder());
        if (legacyFolder() != folder())
            files.addArray (looseShapesIn (legacyFolder()));
        std::sort (files.begin(), files.end(), [] (const juce::File& x, const juce::File& y)
        {
            return x.getFileName().compareNatural (y.getFileName()) < 0;
        });
        return files;
    }

    // Every shape: your own saves, then everything in subfolders (imported libraries), sorted by path.
    inline juce::Array<juce::File> listAll()
    {
        const auto root = folder();
        auto files = list();
        auto nested = root.findChildFiles (juce::File::findFiles, true, "*.shape");
        nested.removeIf ([&] (const juce::File& f) { return f.getParentDirectory() == root; });
        std::sort (nested.begin(), nested.end(), [&] (const juce::File& x, const juce::File& y)
        {
            return x.getRelativePathFrom (root).compareNatural (y.getRelativePathFrom (root)) < 0;
        });
        files.addArray (nested);
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
