#include "TableLibrary.h"
#include "WaveTable.h"
#include <set>

namespace wavesieve
{
TableLibrary::TableLibrary() : juce::Thread ("Wavesieve table scan")
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Wavesieve";
    o.filenameSuffix = ".settings";
    o.folderName = "Wavesieve";
    o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (o);
    rescan();
}

TableLibrary::~TableLibrary()
{
    stopThread (4000);
    cancelPendingUpdate();
}

void TableLibrary::rescan()
{
    if (isThreadRunning()) return;
    scanning = true;
    startThread();
}

void TableLibrary::addFolder (const juce::File& dir)
{
    juce::StringArray folders;
    folders.addLines (settings->getValue ("extraFolders"));
    folders.addIfNotAlreadyThere (dir.getFullPathName());
    folders.removeEmptyStrings();
    settings->setValue ("extraFolders", folders.joinIntoString ("\n"));
    settings->saveIfNeeded();
    stopThread (4000);
    rescan();
}

std::vector<TableEntry> TableLibrary::getEntries() const
{
    const juce::ScopedLock sl (lock);
    return entries;
}

std::vector<TableLibrary::Root> TableLibrary::defaultRoots()
{
    const auto home  = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto music = home.getChildFile ("Music");
    const auto docs  = home.getChildFile ("Documents");
    const juce::File lib ("/Library");
    return {
        { lib.getChildFile ("Audio/Presets/Xfer Records/Serum 2 Presets/Tables"),  "Serum 2", true },
        { home.getChildFile ("Library/Audio/Presets/Xfer Records/Serum 2 Presets/Tables"), "Serum 2", true },
        { docs.getChildFile ("Xfer/Serum 2 Presets/Tables"),                       "Serum 2", true },
        { lib.getChildFile ("Audio/Presets/Xfer Records/Serum Presets/Tables"),    "Serum",   true },
        { docs.getChildFile ("Xfer/Serum Presets/Tables"),                         "Serum",   true },
        { music.getChildFile ("Vital"),                                            "Vital",   false },
        { music.getChildFile ("Wavetable Morpher"),                                "Wavetable Morpher", true },
        { docs.getChildFile ("Surge XT/Wavetables"),                               "Surge XT", true },
        { lib.getChildFile ("Application Support/Surge XT/wavetables"),            "Surge XT", true },
    };
}

std::vector<TableEntry> TableLibrary::scan (const std::vector<Root>& roots, juce::Thread* thread)
{
    std::vector<TableEntry> out;
    std::set<juce::String> seen;

    for (const auto& root : roots)
    {
        if (! root.dir.isDirectory()) continue;

        for (const auto& it : juce::RangedDirectoryIterator (root.dir, true, "*.wav;*.wt;*.vitaltable", juce::File::findFiles, juce::File::FollowSymlinks::noCycles))
        {
            if (thread != nullptr && thread->threadShouldExit()) return out;
            const auto f = it.getFile();

            if (f.hasFileExtension (".wav") && ! root.allWavs)
            {
                // Vital folders also hold samples, so a plain .wav must look like a wavetable.
                const auto rel = f.getRelativePathFrom (root.dir).toLowerCase();
                if (! rel.contains ("wavetable") && readWavFrameSizeHint (f) == 0) continue;
            }

            // The same table often exists in several synths' folders: list it once.
            const auto key = f.getFileName().toLowerCase() + "|" + juce::String (it.getFileSize());
            if (! seen.insert (key).second) continue;

            TableEntry e;
            e.file = f;
            e.name = f.getFileNameWithoutExtension();
            e.source = root.source;
            const auto rel = f.getRelativePathFrom (root.dir);
            e.folder = rel.containsChar (juce::File::getSeparatorChar()) ? rel.upToFirstOccurrenceOf (juce::File::getSeparatorString(), false, false) : juce::String();
            out.push_back (std::move (e));
        }
    }

    std::sort (out.begin(), out.end(), [] (const TableEntry& a, const TableEntry& b)
    {
        if (a.source != b.source) return a.source < b.source;
        if (a.folder != b.folder) return a.folder.compareNatural (b.folder) < 0;
        return a.name.compareNatural (b.name) < 0;
    });
    return out;
}

void TableLibrary::run()
{
    auto roots = defaultRoots();
    juce::StringArray extra;
    extra.addLines (settings->getValue ("extraFolders"));
    for (const auto& path : extra)
        if (path.isNotEmpty())
            roots.push_back ({ juce::File (path), "Added", true });

    auto result = scan (roots, this);
    {
        const juce::ScopedLock sl (lock);
        entries = std::move (result);
    }
    scanning = false;
    triggerAsyncUpdate();
}
} // namespace wavesieve
