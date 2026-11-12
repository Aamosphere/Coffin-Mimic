// Renders the editor to a PNG: MimicSnapshot <out.png> [preset index]
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    MimicProcessor processor;

    if (argc > 2)
        processor.presets.loadFactory (juce::String (argv[2]).getIntValue());

    processor.prepareToPlay (48000.0, 512);

    // A few bars of clicks, so the pattern display has something to show.
    {
        juce::AudioBuffer<float> block (4, 512);
        juce::MidiBuffer midi;
        juce::Random r (3);

        for (int b = 0; b < 700; ++b)
        {
            block.clear();

            for (int i = 0; i < 512; ++i)
            {
                const int n = b * 512 + i;
                const float x = (n % 12000) < 2000 ? 0.5f * (r.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) (n % 12000) / 2000.0f) : 0.0f;
                block.setSample (0, i, x);
                block.setSample (1, i, x);
            }

            processor.processBlock (block, midi);
        }
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setVisible (true);

    if (auto* timer = dynamic_cast<juce::Timer*> (editor.get()))
        for (int i = 0; i < 30; ++i)
            timer->timerCallback();

    // A third argument opens the Advanced drawer; "fill" also switches to Fill.
    if (argc > 3 && juce::String (argv[3]) == "fill")
        if (auto* p = processor.parameters.getParameter (mimic::ids::mode))
            p->setValueNotifyingHost (1.0f);

    if (argc > 3)
    {
        std::function<void (juce::Component&)> openDrawer = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "ADVANCED")
                {
                    button->onClick();
                    return;
                }

                openDrawer (*child);
            }
        };

        openDrawer (*editor);
    }


    // SNAPSHOT_WIDTH: show it resized, as a user dragging its corner would.


    if (auto* w = std::getenv ("SNAPSHOT_WIDTH"))


        editor->setSize (std::atoi (w), juce::roundToInt (std::atoi (w) * (double) editor->getHeight() / editor->getWidth()));



    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    juce::File out (juce::String (argc > 1 ? argv[1] : "mimic.png"));
    out.deleteFile();
    {
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        stream.flush();
    }
    editor.reset();
    processor.releaseResources();
    std::_Exit (0);
}
