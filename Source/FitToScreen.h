#pragma once

#include <JuceHeader.h>

// Shared by the Aamosphere plugins: how wide an editor may be so that all of
// it fits on the screen it is on (the main screen, before it is shown), with
// room left for the host's title bar and the Dock. The editor keeps its
// aspect ratio, so the width decides the height too.
namespace aamo
{
    inline int widthThatFits (const juce::Component* editor, int designWidth, int designHeight, int wanted)
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const juce::Displays::Display* display = nullptr;

        if (editor != nullptr && editor->isShowing())
            display = displays.getDisplayForRect (editor->getScreenBounds());

        if (display == nullptr)
            display = displays.getPrimaryDisplay();

        if (display == nullptr)
            return wanted;

        const auto area = display->userBounds;
        const double byWidth = (double) area.getWidth() * 0.96;
        const double byHeight = ((double) area.getHeight() - 70.0) * (double) designWidth / (double) designHeight;
        const int fits = (int) std::floor (juce::jmin (byWidth, byHeight));
        return juce::jmax (designWidth / 2, juce::jmin (wanted, fits));
    }
}
