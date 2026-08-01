#include "PluginEditor.h"

#include "engine/Import.h"

namespace {
constexpr int kToolbarH = 30;
}

SoundEffectAudioProcessorEditor::SoundEffectAudioProcessorEditor (SoundEffectAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p),
      canvas (p.engine()), mixer (p.engine()), effects (p.engine())
{
    formatManager.registerBasicFormats();   // WAV, AIFF, and more

    addAndMakeVisible (mosaicButton);
    addAndMakeVisible (zoomResetButton);
    addAndMakeVisible (viewStatus);
    addAndMakeVisible (canvas);
    addAndMakeVisible (mixer);
    addAndMakeVisible (effects);

    // The effects panel acts on the canvas's SELECTED Window — not the focused
    // one. Clicking into the panel takes keyboard focus away from the Window, and
    // what the panel operates on has to survive that.
    canvas.onSelectionChanged = [this]
    {
        effects.setTargetWindow (canvas.selectedWindowId());
        mixer.setSelectedWindow (canvas.selectedWindowId());
    };
    effects.setTargetWindow (canvas.selectedWindowId());
    mixer.setSelectedWindow (canvas.selectedWindowId());

    // The mixer is a list OF the Windows: clicking a channel selects its Window and
    // brings it to the front — and, if it had been scrolled off the board, back into
    // view. Selection flows the other way through onSelectionChanged above, so the
    // framed strip and the framed Window are always the same one.
    mixer.onChannelClicked = [this] (uint64_t id) { canvas.focusWindow (id); };

    // An effect rewrites the audio IN PLACE, so the Window's cached waveform peaks
    // — keyed on track length — cannot tell anything happened. Drop them
    // explicitly. There is no undo (§1); this is the only redraw there is.
    effects.onDocumentChanged = [this] (uint64_t id)
    {
        canvas.invalidateWaveform (id);
        canvas.refresh();
    };

    // New / Open live at the top of the mixer panel, not in the toolbar.
    mixer.onNew  = [this] { newWindow(); };
    mixer.onOpen = [this] { openFile(); };

    mosaicButton.onClick    = [this] { canvas.setMosaicMode (! canvas.isMosaicMode()); };
    zoomResetButton.onClick = [this] { canvas.resetView(); };

    viewStatus.setFont (juce::FontOptions (11.0f));
    viewStatus.setColour (juce::Label::textColourId, juce::Colours::black);
    canvas.onViewChanged = [this] { updateViewStatus(); };
    updateViewStatus();

    setSize (960, 600);
    setResizable (true, true);
    setWantsKeyboardFocus (true);   // so ⌘N / ⌘O still land with no Window focused
    startTimerHz (30);
}

bool SoundEffectAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    if (! k.getModifiers().isCommandDown()) return false;

    // A Command-combo produces no text character on macOS, so match the key code
    // (modifier-independent) and accept either case.
    const int code = k.getKeyCode();
    if (code == 'n' || code == 'N') { newWindow(); return true; }
    if (code == 'o' || code == 'O') { openFile();  return true; }
    return false;
}

void SoundEffectAudioProcessorEditor::updateViewStatus()
{
    juce::String s;
    s << juce::roundToInt (canvas.zoomLevel() * 100.0f) << "%   "
      << (canvas.isMosaicMode() ? "grid" : "free")
      << (canvas.isPanMode() ? "   pan (V to exit)" : "   H to pan");
    viewStatus.setText (s, juce::dontSendNotification);
}

SoundEffectAudioProcessorEditor::~SoundEffectAudioProcessorEditor()
{
    stopTimer();
}

void SoundEffectAudioProcessorEditor::timerCallback()
{
    canvas.refresh();
    mixer.refresh();
    effects.refresh();
}

void SoundEffectAudioProcessorEditor::newWindow()
{
    se::DocumentInit init;
    init.name = "untitled sound";
    init.numTracks = 1;
    processorRef.engine().addWindow (std::make_unique<se::SoundDocument> (init));
}

void SoundEffectAudioProcessorEditor::openFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a sound", juce::File(),
                                                   formatManager.getWildcardForAllFormats());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr) return;

        const int numCh = (int) reader->numChannels;
        const int frames = (int) reader->lengthInSamples;
        if (numCh <= 0 || frames <= 0) return;

        juce::AudioBuffer<float> buf (numCh, frames);
        reader->read (&buf, 0, frames, 0, true, true);

        // Planar floats → one Track per channel.
        std::vector<std::vector<float>> channels ((size_t) numCh);
        for (int c = 0; c < numCh; ++c)
            channels[(size_t) c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + frames);

        const int bps = reader->bitsPerSample >= 16 ? 16 : 8;
        auto doc = se::makeDocumentFromFloat (channels, reader->sampleRate, bps,
                                              file.getFileNameWithoutExtension().toStdString());
        processorRef.engine().addWindow (std::move (doc));
    });
}

void SoundEffectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xffbfbfbf));
}

void SoundEffectAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto bar = area.removeFromTop (kToolbarH).reduced (4, 3);
    mosaicButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    zoomResetButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (8);
    viewStatus.setBounds (bar);

    // The two panels are fixed columns, kMaxWidth at most and never more than a
    // third of the editor each — so a small host window still leaves a canvas to
    // work on between them.
    mixer.setBounds   (area.removeFromLeft  (juce::jmin (MixerComponent::kMaxWidth, area.getWidth() / 3)));
    effects.setBounds (area.removeFromRight (juce::jmin (EffectsPanelComponent::kMaxWidth, area.getWidth() / 2)));
    canvas.setBounds  (area);
}
