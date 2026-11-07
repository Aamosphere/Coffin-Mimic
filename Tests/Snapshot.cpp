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
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setVisible (true);

    if (auto* timer = dynamic_cast<juce::Timer*> (editor.get()))
        for (int i = 0; i < 30; ++i)
            timer->timerCallback();

    // A third argument opens the Advanced page.
    if (argc > 3)
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "ADVANCED")
                button->onClick();


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
