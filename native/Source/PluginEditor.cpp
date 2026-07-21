#include "PluginEditor.h"

#include "engine/Import.h"

namespace {
constexpr int kToolbarH = 30;
constexpr int kMixerW = 240;
}

SoundEffectAudioProcessorEditor::SoundEffectAudioProcessorEditor (SoundEffectAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p),
      canvas (p.engine()), mixer (p.engine())
{
    formatManager.registerBasicFormats();   // WAV, AIFF, and more

    addAndMakeVisible (newButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (mosaicButton);
    addAndMakeVisible (zoomResetButton);
    addAndMakeVisible (viewStatus);
    addAndMakeVisible (canvas);
    addAndMakeVisible (mixer);

    newButton.onClick       = [this] { newWindow(); };
    openButton.onClick      = [this] { openFile(); };
    mosaicButton.onClick    = [this] { canvas.mosaic(); };
    zoomResetButton.onClick = [this] { canvas.resetView(); };

    viewStatus.setFont (juce::FontOptions (11.0f));
    viewStatus.setColour (juce::Label::textColourId, juce::Colours::black);
    canvas.onViewChanged = [this] { updateViewStatus(); };
    updateViewStatus();

    setSize (960, 600);
    setResizable (true, true);
    startTimerHz (30);
}

void SoundEffectAudioProcessorEditor::updateViewStatus()
{
    juce::String s;
    s << juce::roundToInt (canvas.zoomLevel() * 100.0f) << "%   "
      << (canvas.isMosaicMode() ? "mosaic" : "free")
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
    newButton.setBounds    (bar.removeFromLeft (60));
    bar.removeFromLeft (4);
    openButton.setBounds   (bar.removeFromLeft (60));
    bar.removeFromLeft (4);
    mosaicButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    zoomResetButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (8);
    viewStatus.setBounds (bar);

    mixer.setBounds  (area.removeFromRight (kMixerW));
    canvas.setBounds (area);
}
