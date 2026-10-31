// Os8Gallery — every Os8 building block on one page, in every state.
//
// A workbench, not a feature: it is how each piece of the design system gets
// checked against its Figma component before any panel is built from it. Launch the
// standalone with `--gallery` to get it in a window of its own, or with
// `--gallery-snapshot <file.png>` to render it to an image and quit.

#pragma once

#include "Os8Widgets.h"

#include <memory>
#include <vector>

class Os8Gallery : public juce::Component
{
public:
    Os8Gallery();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Render the gallery (or just `area` of it) off-screen at `scale` and write
        it as a PNG. */
    static bool writeSnapshot (const juce::File& png, float scale, juce::Rectangle<int> area = {});

    /** Where the UI Elements section sits — snapshotted on its own, magnified, to
        compare with Figma's zoomed renders. */
    static juce::Rectangle<int> uiElementsArea();

    /** The fields-and-dropdowns row, below the UI elements. */
    static juce::Rectangle<int> fieldsArea();

    /** Sliders, icon buttons and the canvas toggle. */
    static juce::Rectangle<int> slidersArea();

    /** Transport buttons, the combo play and the direction menu. */
    static juce::Rectangle<int> transportArea();
    static juce::Rectangle<int> windowButtonsArea();

private:
    // Live controls, to be clicked: the painters above show every state at once,
    // these show the transitions between them (press, Done! flash, radio exclusion).
    Os8Button liveApply { "Apply", os8::ButtonKind::primary };
    Os8Button liveCancel { "Cancel" };
    Os8Toggle liveCheck { "Activate Auto-Apply", Os8Toggle::Shape::checkbox };
    Os8Toggle liveRadioA { "New Channel", Os8Toggle::Shape::radio };
    Os8Toggle liveRadioB { "New Document", Os8Toggle::Shape::radio };
    Os8Toggle liveDisabled { "Allow the sound to get longer", Os8Toggle::Shape::checkbox };
    Os8TextField liveLarge { os8::FieldSize::large }, liveSmall { os8::FieldSize::small },
                 liveXSmall { os8::FieldSize::xSmall };
    Os8Dropdown liveUnits, liveMini { Os8Dropdown::Style::mini };
    Os8Slider liveVolume { os8::SliderKind::volume }, livePan { os8::SliderKind::pan },
              liveAmplify { os8::SliderKind::amplify };
    Os8IconButton liveKeyboard { os8::icon::Id::keyboard };
    Os8CanvasToggle liveCanvas;
    Os8TransportButton liveRecord { os8::icon::Id::record }, liveStop { os8::icon::Id::stop },
                       livePause { os8::icon::Id::pause }, liveFF { os8::icon::Id::fastForward },
                       liveLoop { os8::icon::Id::loop };
    Os8ComboPlay livePlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Gallery)
};
