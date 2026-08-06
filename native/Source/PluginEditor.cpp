#include "PluginEditor.h"

#include "engine/Import.h"

namespace {
/** How much canvas stays visible below the floating view palette. */
constexpr int kPaletteBottomGap = 50;
/** ...and beside it, before it starts giving width back to the viewport. */
constexpr int kPaletteSideGap = 12;
}

SoundEffectAudioProcessorEditor::SoundEffectAudioProcessorEditor (SoundEffectAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processorRef (p),
      canvas (p.engine()), mixer (p.engine()), effects (p.engine())
{
    formatManager.registerBasicFormats();   // WAV, AIFF, and more

    addAndMakeVisible (canvas);
    addAndMakeVisible (mixer);
    addAndMakeVisible (effects);
    addAndMakeVisible (palette);   // last, so it floats over the canvas

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

    // New / Open live at the top of the mixer panel, not in any toolbar.
    mixer.onNew  = [this] { newWindow(); };
    mixer.onOpen = [this] { openFile(); };

    // A Window's record button is greyed — recording is not implemented — and
    // clicking it asks for the audio device settings instead. Only the standalone
    // app can serve that, so it forwards to a hook the app installs.
    canvas.onAudioSettingsRequested = [this] { if (onShowAudioSettings) onShowAudioSettings(); };

    // The palette and the canvas are two faces of one state: the palette asks, the
    // canvas decides — it clamps the zoom to its own range — and what it settled on
    // comes straight back to the palette's readouts, whether the palette asked for it
    // or a pinch did. Nothing loops: setZoom / setMosaicMode on the palette are silent.
    palette.onMosaicModeChanged = [this] (bool grid) { canvas.setMosaicMode (grid); };
    palette.onZoomChanged       = [this] (float z)   { canvas.setZoom (z); };
    palette.onZoomReset         = [this]             { canvas.setZoom (1.0f); };

    canvas.onViewChanged = [this]
    {
        palette.setMosaicMode (canvas.isMosaicMode());
        palette.setZoom (canvas.zoomLevel());
    };
    palette.setMosaicMode (canvas.isMosaicMode());
    palette.setZoom (canvas.zoomLevel());

    setSize (960, 600);
    setResizable (true, true);
    setWantsKeyboardFocus (true);   // so ⌘N / ⌘O still land with no Window focused
    startTimerHz (30);
}

bool SoundEffectAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    // Return applies the open effect from anywhere in the editor — the usual move is
    // to drag a selection in a Window and then run the form that is already open, and
    // that leaves focus on the Window, not on the panel. It only fires when there IS
    // something runnable; otherwise the key falls through untouched. A Window being
    // renamed keeps its own Return (WindowComponent's name editor consumes it).
    if (k.getKeyCode() == juce::KeyPress::returnKey
        && ! k.getModifiers().isAnyModifierKeyDown()
        && effects.applyFromKeyboard())
        return true;

    if (! k.getModifiers().isCommandDown()) return false;

    // A Command-combo produces no text character on macOS, so match the key code
    // (modifier-independent) and accept either case.
    const int code = k.getKeyCode();
    if (code == 'n' || code == 'N') { newWindow(); return true; }
    if (code == 'o' || code == 'O') { openFile();  return true; }
    return false;
}

SoundEffectAudioProcessorEditor::~SoundEffectAudioProcessorEditor()
{
    stopTimer();
}

void SoundEffectAudioProcessorEditor::timerCallback()
{
    // Muted unless something says otherwise: in a plugin there is no hook to ask,
    // and the record button should be grey there too.
    canvas.setRecordEnabled (isAudioInputActive != nullptr && isAudioInputActive());

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

    // The two panels are fixed columns, kMaxWidth at most and never more than a
    // third of the editor each — so a small host window still leaves a canvas to
    // work on between them.
    mixer.setBounds   (area.removeFromLeft  (juce::jmin (MixerComponent::kMaxWidth, area.getWidth() / 3)));
    effects.setBounds (area.removeFromRight (juce::jmin (EffectsPanelComponent::kMaxWidth, area.getWidth() / 2)));
    canvas.setBounds  (area);

    // The palette hangs over the canvas — centred on the VIEWPORT, not on the
    // editor, so it stays over the board it drives rather than drifting under a
    // panel as the panels take their share. It narrows with the viewport too, down
    // to the width below which its slider stops being a slider.
    const int paletteW = juce::jlimit (ViewPalette::kMinWidth, ViewPalette::kMaxWidth,
                                       area.getWidth() - 2 * kPaletteSideGap);
    palette.setBounds (area.getCentreX() - paletteW / 2,
                       area.getBottom() - kPaletteBottomGap - ViewPalette::kHeight,
                       paletteW, ViewPalette::kHeight);
}
