#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <vector>

namespace wavesieve
{
struct TableEntry
{
    juce::File   file;
    juce::String name;
    juce::String source;    // "Serum 2", "Vital", ...
    juce::String folder;    // first folder under the source's root
};

/** Finds every wavetable on the machine, on a background thread. Shared by all plugin instances. */
class TableLibrary : public juce::ChangeBroadcaster, private juce::Thread, private juce::AsyncUpdater
{
public:
    TableLibrary();
    ~TableLibrary() override;

    void rescan();
    void addFolder (const juce::File&);
    std::vector<TableEntry> getEntries() const;
    bool isScanning() const noexcept  { return scanning.load(); }

    /** Pure scan, exposed for tests. */
    struct Root { juce::File dir; juce::String source; bool allWavs; };
    static std::vector<TableEntry> scan (const std::vector<Root>&, juce::Thread* = nullptr);
    static std::vector<Root> defaultRoots();

private:
    void run() override;
    void handleAsyncUpdate() override  { sendChangeMessage(); }

    juce::CriticalSection lock;
    std::vector<TableEntry> entries;
    std::atomic<bool> scanning { false };
    std::unique_ptr<juce::PropertiesFile> settings;
};
} // namespace wavesieve
