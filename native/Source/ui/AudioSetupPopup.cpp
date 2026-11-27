#include "AudioSetupPopup.h"

#include "Os8Alert.h"
#include "Os8CheckList.h"

#include <cmath>

namespace colour = os8::colour;

namespace audiosetup {

namespace {

// 1447:9160, from the top of the rows: the controls' column starts 124 px in.
constexpr int kContentW = 362, kColumn = 124, kListW = kContentW - kColumn;
constexpr int kListH = 84, kMinListH = 46;
constexpr float kPitch = 58.0f / 3.0f;
/** The popup's height without its three lists. */
constexpr int kFixedH = 271;

/** The input's level as the design draws it: seven 5 px bars, 6 px apart. */
class LevelBars : public juce::Component,
                  private juce::Timer
{
public:
    explicit LevelBars (juce::AudioDeviceManager& d) : level (d.getInputLevelGetter())
    {
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < kBars; ++i)
        {
            g.setColour (i < lit ? juce::Colour (0xff333399) : colour::selectedTextField);
            g.fillRect (i * 6, 0, 5, getHeight());
        }
    }

private:
    static constexpr int kBars = 7;

    void timerCallback() override
    {
        // The cube root JUCE's own meter uses: quiet sounds still move the bars.
        const double now = level != nullptr ? level->getCurrentLevel() : 0.0;
        const int n = juce::jlimit (0, kBars, juce::roundToInt ((double) kBars * std::cbrt (std::max (0.0, now))));
        if (n != lit) { lit = n; repaint(); }
    }

    juce::AudioDeviceManager::LevelMeter::Ptr level;
    int lit = 0;
};

class Panel : public juce::Component,
              private juce::ChangeListener
{
public:
    Panel (juce::AudioDeviceManager& d, juce::Value mute, int listHeight)
        : devices (d), listH (listHeight), bars (d)
    {
        setMouseClickGrabsKeyboardFocus (false);

        quiet (muteBox);
        muteBox.getToggleStateValue().referTo (mute);
        muteBox.setBounds (kColumn, 4, muteBox.preferredWidth(), os8::kToggleSize);

        for (auto* m : { &output, &input, &rate, &buffer })
        {
            m->setWantsKeyboardFocus (false);
            addAndMakeVisible (*m);
        }
        output.onChange = [this] (int i) { pickDevice (false, i); };
        input.onChange  = [this] (int i) { pickDevice (true, i); };
        rate.onChange   = [this] (int i)
        {
            auto s = devices.getAudioDeviceSetup();
            if (juce::isPositiveAndBelow (i, rates.size())) { s.sampleRate = rates[i]; use (s); }
        };
        buffer.onChange = [this] (int i)
        {
            auto s = devices.getAudioDeviceSetup();
            if (juce::isPositiveAndBelow (i, sizes.size())) { s.bufferSize = sizes[i]; use (s); }
        };

        quiet (test);
        test.onClick = [this] { devices.playTestSound(); };
        addAndMakeVisible (bars);

        output.setBounds (kColumn, 34, 192, os8::kDropdownHeight);
        test.setBounds (320, 32, 42, os8::buttonHeight (os8::ButtonKind::secondary));
        input.setBounds (kColumn, 60, 193, os8::kDropdownHeight);
        bars.setBounds (321, 61, 41, 16);
        const int rateY = rateRowY();
        rate.setBounds (kColumn, rateY, 180, os8::kDropdownHeight);
        buffer.setBounds (kColumn, rateY + 25, 180, os8::kDropdownHeight);

        devices.addChangeListener (this);
        // A device plugged in or pulled out while the popup is up.
        midiList = juce::MidiDeviceListConnection::make ([this] { refresh(); });
        refresh();
    }

    ~Panel() override { devices.removeChangeListener (this); }

    static int rowsHeight (int listHeight) noexcept { return 189 + 3 * listHeight; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::black);
        const auto label = [&g] (const char* text, int y)
        {
            os8::drawText (g, text, os8::TextStyle::headline, { 0.0f, (float) y, (float) kColumn, 12.0f });
        };
        label ("Feedback Loop:", 4);
        label ("Output:", 36);
        label ("Input:", 63);
        const int middle = listH / 2 - 6;
        label ("Output channels:", outputListY() + middle);
        label ("Input channels:", outputListY() + listH + 8 + middle);
        const int rateY = rateRowY();
        label ("Sample rate:", rateY + 3);
        label ("Buffer Size:", rateY + 28);
        label ("Midi inputs:", midiListY() + middle);

        // "Menu_separator": four half-pixel rules.
        const juce::Colour rules[] = { colour::gray200, colour::gray400, colour::gray500, colour::white };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (rules[i]);
            g.fillRect (juce::Rectangle<float> (0.0f, (float) (rateY + 54) + (float) i * 0.5f,
                                                (float) kContentW, 0.5f));
        }
    }

private:
    int outputListY() const noexcept { return 93; }
    int rateRowY() const noexcept    { return outputListY() + 2 * (listH + 8) + 8; }
    int midiListY() const noexcept   { return rateRowY() + 68; }

    void quiet (juce::Button& b)
    {
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void use (const juce::AudioDeviceManager::AudioDeviceSetup& setup)
    {
        const auto error = devices.setAudioDeviceSetup (setup, true);
        // Later: this may be running inside a list's own click, and a device that
        // failed leaves other rows to show.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Panel> (this)]
        {
            if (safe != nullptr) safe->refresh();
        });
        if (error.isNotEmpty())
            os8alert::tell (*this, "Couldn't change the audio setup", error);
    }

    void pickDevice (bool isInput, int index)
    {
        const auto& names = isInput ? inputNames : outputNames;
        // One past the devices is "None".
        const auto name = juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
        auto s = devices.getAudioDeviceSetup();
        auto& current = isInput ? s.inputDeviceName : s.outputDeviceName;
        if (current == name) return;
        current = name;
        if (isInput) s.useDefaultInputChannels = true;
        else         s.useDefaultOutputChannels = true;
        use (s);
    }

    void pickPair (bool isInput, int row)
    {
        auto* list = isInput ? inputs.get() : outputs.get();
        if (list == nullptr) return;
        auto s = devices.getAudioDeviceSetup();
        auto& bits = isInput ? s.inputChannels : s.outputChannels;
        const int channels = isInput ? numInputs : numOutputs;
        const bool on = list->isTicked (row);
        bits.clear();
        if (on)
            for (int c = 2 * row; c < std::min (2 * row + 2, channels); ++c) bits.setBit (c);
        if (isInput) s.useDefaultInputChannels = false;
        else         s.useDefaultOutputChannels = false;
        use (s);
    }

    /** A new list when its names changed, otherwise the same one re-ticked, so that
        it keeps its place. A list is never replaced under its own click: refresh()
        after a click finds the same names. */
    void fill (std::unique_ptr<Os8CheckList>& list, juce::StringArray& shown,
               const juce::StringArray& names, const juce::Array<bool>& ticks, int y,
               std::function<void (int)> clicked)
    {
        if (list == nullptr || shown != names)
        {
            std::vector<Os8CheckList::Item> items;
            for (int i = 0; i < names.size(); ++i) items.push_back ({ names[i], {}, ticks[i] });
            list = std::make_unique<Os8CheckList> (std::move (items), std::vector<int>(), kPitch);
            list->onRowClicked = std::move (clicked);
            list->setBounds (kColumn, y, kListW, listH);
            addAndMakeVisible (*list);
            shown = names;
        }
        else
        {
            for (int i = 0; i < names.size(); ++i) list->setTicked (i, ticks[i]);
        }
    }

    void refresh()
    {
        const auto setup = devices.getAudioDeviceSetup();
        auto* type = devices.getCurrentDeviceTypeObject();
        auto* device = devices.getCurrentAudioDevice();

        const auto devicesMenu = [type] (Os8Dropdown& menu, juce::StringArray& names, bool isInput,
                                         const juce::String& current)
        {
            names = type != nullptr ? type->getDeviceNames (isInput) : juce::StringArray();
            auto items = names;
            items.add ("None");
            menu.setItems (items);
            const int at = names.indexOf (current);
            menu.setSelectedIndex (at >= 0 ? at : names.size());
        };
        devicesMenu (output, outputNames, false, setup.outputDeviceName);
        devicesMenu (input, inputNames, true, setup.inputDeviceName);

        const auto pairs = [this] (std::unique_ptr<Os8CheckList>& list, juce::StringArray& shown, bool isInput,
                                   int channels, const juce::BigInteger& active, int y)
        {
            juce::StringArray names;
            juce::Array<bool> ticks;
            const juce::String word = isInput ? "Input " : "Output ";
            for (int c = 0; c < channels; c += 2)
            {
                names.add (word + juce::String (c + 1)
                           + (c + 1 < channels ? " + " + juce::String (c + 2) : juce::String()));
                ticks.add (active[c] || (c + 1 < channels && active[c + 1]));
            }
            fill (list, shown, names, ticks, y, [this, isInput] (int row) { pickPair (isInput, row); });
        };
        numOutputs = device != nullptr ? device->getOutputChannelNames().size() : 0;
        numInputs  = device != nullptr ? device->getInputChannelNames().size() : 0;
        // The ticks are the setup's, not the device's: with every channel of both
        // unticked JUCE leaves the device closed, still reporting the last ones.
        const bool open = device != nullptr && device->isOpen();
        pairs (outputs, outputRows, false, numOutputs, setup.outputChannels, outputListY());
        pairs (inputs, inputRows, true, numInputs, setup.inputChannels, outputListY() + listH + 8);

        rates.clear();
        sizes.clear();
        juce::StringArray rateItems, sizeItems;
        double now = 0.0;
        if (device != nullptr)
        {
            now = open ? device->getCurrentSampleRate() : setup.sampleRate;
            for (double r : device->getAvailableSampleRates())
            {
                rates.add (r);
                rateItems.add (juce::String (juce::roundToInt (r)) + " Hz");
            }
            for (int n : device->getAvailableBufferSizes())
            {
                sizes.add (n);
                sizeItems.add (juce::String (n) + " samples ("
                               + juce::String (now > 0.0 ? n * 1000.0 / now : 0.0, 1) + " ms)");
            }
        }
        rate.setItems (rateItems);
        buffer.setItems (sizeItems);
        int rateAt = -1;
        for (int i = 0; i < rates.size(); ++i)
            if (std::abs (rates[i] - now) < 0.5) rateAt = i;
        rate.setSelectedIndex (rateAt);
        buffer.setSelectedIndex (device != nullptr
                                     ? sizes.indexOf (open ? device->getCurrentBufferSizeSamples() : setup.bufferSize)
                                     : -1);
        rate.setEnabled (device != nullptr);
        buffer.setEnabled (device != nullptr);
        test.setEnabled (open && ! setup.outputChannels.isZero());

        const auto midi = juce::MidiInput::getAvailableDevices();
        juce::StringArray midiNames, midiKeys;
        juce::Array<bool> midiTicks;
        midiIds.clear();
        for (const auto& m : midi)
        {
            midiNames.add (m.name);
            midiKeys.add (m.identifier);
            midiIds.add (m.identifier);
            midiTicks.add (devices.isMidiInputDeviceEnabled (m.identifier));
        }
        if (midiKeys != midiShown) midis.reset();     // two devices may share a name
        midiShown = midiKeys;
        juce::StringArray unused = midis != nullptr ? midiNames : juce::StringArray();
        fill (midis, unused, midiNames, midiTicks, midiListY(), [this] (int row)
        {
            if (midis != nullptr && juce::isPositiveAndBelow (row, midiIds.size()))
                devices.setMidiInputDeviceEnabled (midiIds[row], midis->isTicked (row));
        });
    }

    juce::AudioDeviceManager& devices;
    const int listH;

    Os8Toggle muteBox { "Mute audio input", Os8Toggle::Shape::checkbox, os8::TextStyle::headline };
    Os8Dropdown output, input, rate, buffer;
    Os8Button test { "Test", os8::ButtonKind::secondary };
    LevelBars bars;
    std::unique_ptr<Os8CheckList> outputs, inputs, midis;

    juce::StringArray outputNames, inputNames, outputRows, inputRows, midiShown, midiIds;
    juce::Array<double> rates;
    juce::Array<int> sizes;
    int numOutputs = 0, numInputs = 0;
    juce::MidiDeviceListConnection midiList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

} // namespace

std::unique_ptr<Os8Popup> build (juce::AudioDeviceManager& devices, juce::Value muteInput,
                                 int availableHeight, std::function<void()> closed)
{
    const int listH = juce::jlimit (kMinListH, kListH, (availableHeight - kFixedH) / 3);
    const int rows = Panel::rowsHeight (listH);

    auto popup = std::make_unique<Os8Popup> ("Audio/Midi Setup", rows, kContentW);
    popup->addContent (std::make_unique<Panel> (devices, muteInput, listH), { 0, 0, kContentW, rows });
    popup->setButtons ({}, "OK", 1);
    popup->onDismiss = [then = std::move (closed)] (int) { if (then) then(); };
    return popup;
}

} // namespace audiosetup
