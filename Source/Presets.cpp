#include "Presets.h"
#include "Parameters.h"

namespace mimic
{
    namespace
    {
        const juce::Identifier presetNameId ("presetName");
        const juce::String fileExtension (".mimic");

        // Writes plain parameter values while building a factory preset.
        struct Builder
        {
            juce::AudioProcessorValueTreeState& state;

            void set (const char* id, float plainValue)
            {
                if (auto* p = state.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
                else
                    jassertfalse;
            }

            // Every content on.
            void all()
            {
                for (auto* id : ids::contentIds)
                    set (id, 1.0f);
            }

            // Turns on only the listed fill contents.
            void only (std::initializer_list<const char*> contents)
            {
                for (auto* id : ids::contentIds)
                    set (id, 0.0f);

                for (auto* id : contents)
                    set (id, 1.0f);
            }

            void fill (int everyIndex, int length, float bite)
            {
                set (ids::mode, ids::modeFill);
                set (ids::fillEvery, (float) everyIndex);
                set (ids::fillLength, (float) length);
                set (ids::bite, bite);
            }

            void wild (float bite, float speed)
            {
                set (ids::mode, ids::modeWild);
                set (ids::bite, bite);
                set (ids::speed, speed);
            }
        };

        struct Factory
        {
            const char* name;
            const char* tags;
            bool fill;
            std::function<void (Builder&)> build;
        };

        // fillEvery indices: 0 = 1 bar, 1 = 2, 2 = 4, 3 = 8, 4 = 16, 5 = 32.
        const std::vector<Factory>& factoryPresets()
        {
            using namespace ids;

            static const std::vector<Factory> presets {
                { "Init", "wild, subtle", false, [] (Builder&) {} },

                // Fill machines: the track stays clean until the phrase ends.
                { "Lurking Fill", "subtle, fill", true, [] (Builder& b)
                {
                    b.fill (2, lastBeat, 0.3f);
                    b.only ({ useStutter, useReverse, useGate });
                    b.set (speed, 0.6f);
                    b.set (breaks, 0.3f);
                } },

                { "Snare Roll Trap", "rolling, fill", true, [] (Builder& b)
                {
                    b.fill (2, lastHalfBar, 0.7f);
                    b.only ({ useRoll, useStutter });
                    b.set (speed, 0.6f);
                    b.set (anticipation, 0.5f);
                    b.set (breaks, 0.6f);
                } },

                { "Tape Ghost", "dragging, echo, fill", true, [] (Builder& b)
                {
                    b.fill (3, lastHalfBar, 0.5f);
                    b.only ({ useTape, useReverse });
                    b.set (seed, 17.0f);
                    b.set (throwAmount, 0.45f);
                } },

                { "Scatter Brain", "broken, fill", true, [] (Builder& b)
                {
                    b.fill (2, lastHalfBar, 0.8f);
                    b.only ({ useScatter, useStutter, useReverse });
                    b.set (speed, 0.6f);
                    b.set (escalate, 1.0f);
                    b.set (seed, 303.0f);
                } },

                { "Grain Swallower", "cloudy, fill", true, [] (Builder& b)
                {
                    b.fill (3, lastBar, 0.6f);
                    b.only ({ useGrains, useReverse, useTape });
                    b.set (anticipation, 0.6f);
                    b.set (seed, 42.0f);
                } },

                { "Breakcore Ambush", "broken, wild, fill", true, [] (Builder& b)
                {
                    b.fill (1, lastHalfBar, 1.0f);
                    b.all();
                    b.set (speed, 0.85f);
                    b.set (escalate, 1.0f);
                    b.set (seed, 666.0f);
                } },

                { "Seventy Percent", "subtle, fill", true, [] (Builder& b)
                {
                    b.fill (1, lastBeat, 0.45f);
                    b.set (probability, 0.7f);
                    b.only ({ useStutter, useRoll, useGate, useScatter });
                    b.set (seed, 70.0f);
                } },

                { "Swelling Jaws", "rising, echo, fill", true, [] (Builder& b)
                {
                    b.all();
                    b.set (throwAmount, 0.7f);
                    b.fill (3, lastBar, 0.75f);
                    b.only ({ useRoll, useReverse, useGate });
                    b.set (anticipation, 1.0f);
                    b.set (replace, 0.0f);
                    b.set (seed, 9.0f);
                } },

                // Wild effects: the track is always being chewed.
                { "Restless Groove", "subtle, varying", false, [] (Builder& b)
                {
                    b.wild (0.15f, 0.6f);
                    b.only ({ useMash, useStutter, useGate, useReverse });
                    b.set (swing, 0.3f);
                } },

                { "Venetian Teeth", "broken, choppy", false, [] (Builder& b)
                {
                    b.wild (0.95f, 0.6f);
                    b.all();
                    b.set (filter, 0.0f);
                    b.set (seed, 1993.0f);
                } },

                { "Choppy Jaws", "choppy", false, [] (Builder& b)
                {
                    b.wild (0.6f, 0.6f);
                    b.only ({ useGate, useStutter });
                } },

                { "Broken Reverse", "broken, smeared", false, [] (Builder& b)
                {
                    b.wild (0.5f, 0.4f);
                    b.only ({ useReverse, useScatter });
                    b.set (seed, 512.0f);
                } },

                { "Pumping Prey", "pumping, sidechain", false, [] (Builder& b)
                {
                    b.wild (0.8f, 0.6f);
                    b.only ({});
                    b.set (prey, preySidechain);
                    b.set (shaper, 1.0f);
                    b.set (shaperInvert, 1.0f);
                    b.set (filter, 0.0f);
                    b.set (slicer, 0.0f);
                } },

                { "Brightness Thief", "filtered, sidechain", false, [] (Builder& b)
                {
                    b.wild (0.8f, 0.6f);
                    b.only ({});
                    b.set (prey, preySidechain);
                    b.set (filter, 1.0f);
                    b.set (shaper, 0.4f);
                    b.set (slicer, 0.0f);
                } },

                { "Copycat Chop", "choppy, sidechain", false, [] (Builder& b)
                {
                    b.wild (0.7f, 0.6f);
                    b.only ({ useStutter });
                    b.set (prey, preySidechain);
                    b.set (slicer, 1.0f);
                    b.set (shaper, 0.5f);
                } },

                { "Puppet Strings", "pitched, midi", false, [] (Builder& b)
                {
                    b.wild (0.6f, 0.6f);
                    b.only ({ useStutter });
                    b.set (prey, preyMidi);
                    b.set (pitcher, 1.0f);
                    b.set (slicer, 0.8f);
                    b.set (shaper, 0.6f);
                } },

                // v0.2
                { "Dub Throw", "echo, subtle, fill", true, [] (Builder& b)
                {
                    b.fill (2, lastBeat, 0.4f);
                    b.only ({ useStutter, useReverse });
                    b.set (throwAmount, 0.9f);
                    b.set (breaks, 0.4f);
                    b.set (seed, 808.0f);
                } },

                { "Arp Parasite", "pitched, melodic, fill", true, [] (Builder& b)
                {
                    b.fill (1, lastHalfBar, 0.7f);
                    b.only ({ useStutter, useTape });
                    b.set (material, materialMelodic);
                    b.set (seed, 12.0f);
                } },

                // v0.3: the beat masher and the throat
                { "Amen Masher", "breakbeat, mashed", false, [] (Builder& b)
                {
                    b.wild (0.55f, 0.6f);
                    b.only ({ useMash, useStutter, useRoll, useReverse });
                    b.set (variation, 2.0f);
                    b.set (breaks, 0.25f);
                    b.set (seed, 1969.0f);
                } },

                { "Ratchet Saint", "breakbeat, wild, idm", false, [] (Builder& b)
                {
                    b.wild (1.0f, 0.6f);
                    b.only ({ useMash, useStutter, useRoll, useReverse, useGate });
                    b.set (variation, 1.0f);
                    b.set (breaks, 0.5f);
                    b.set (throwAmount, 0.3f);
                    b.set (seed, 2002.0f);
                } },

                { "Night Throat", "filtered, moving", false, [] (Builder& b)
                {
                    b.wild (0.35f, 0.6f);
                    b.only ({ useMash, useStutter });
                    b.set (filterType, filterLow);
                    b.set (cutoff, 900.0f);
                    b.set (resonance, 0.55f);
                    b.set (filterMove, 0.8f);
                    b.set (seed, 31.0f);
                } },

                { "Throat Sweep", "filtered, rising, fill", true, [] (Builder& b)
                {
                    b.fill (2, lastHalfBar, 0.6f);
                    b.only ({ useMash, useStutter, useRoll });
                    b.set (filterType, filterHigh);
                    b.set (cutoff, 200.0f);
                    b.set (resonance, 0.45f);
                    b.set (filterMove, 0.9f);
                    b.set (seed, 77.0f);
                } },

                { "Swung Ambush", "swung, broken", false, [] (Builder& b)
                {
                    b.wild (0.5f, 0.6f);
                    b.only ({ useStutter, useScatter, useGate, useRoll });
                    b.set (swing, 0.6f);
                    b.set (breaks, 0.45f);
                    b.set (seed, 1312.0f);
                } },

                // v0.5
                { "Acapella Eater", "vocal, chopped", false, [] (Builder& b)
                {
                    b.wild (0.5f, 0.6f);
                    b.only ({ useStutter, useReverse, useGrains, useTape });
                    b.set (material, materialVoice);
                    b.set (throwAmount, 0.35f);
                    b.set (seed, 4.0f);
                } },

                { "Hole Puncher", "broken, breaks", false, [] (Builder& b)
                {
                    b.wild (0.3f, 0.6f);
                    b.only ({ useMash, useStutter, useRoll });
                    b.set (breaks, 0.7f);
                    b.set (seed, 99.0f);
                } }
            };

            return presets;
        }
    }

    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) {}

    const juce::Array<PresetManager::Info>& PresetManager::getFactory()
    {
        static const juce::Array<Info> infos = []
        {
            juce::Array<Info> list;

            for (auto& p : factoryPresets())
                list.add ({ p.name, p.tags, p.fill });

            return list;
        }();

        return infos;
    }

    void PresetManager::resetToDefaults()
    {
        for (auto* p : state.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());
    }

    void PresetManager::setName (const juce::String& name)
    {
        state.state.setProperty (presetNameId, name, nullptr);
    }

    juce::String PresetManager::getCurrentName() const
    {
        return state.state.getProperty (presetNameId, "Init").toString();
    }

    void PresetManager::loadFactory (int index)
    {
        auto& presets = factoryPresets();

        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;

        resetToDefaults();
        Builder builder { state };
        presets[(size_t) index].build (builder);
        setName (presets[(size_t) index].name);
    }

    juce::File PresetManager::userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Presets/Aamosphere/Mimic");
    }

    juce::Array<juce::File> PresetManager::getUserPresets() const
    {
        auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*" + fileExtension);
        files.sort();
        return files;
    }

    void PresetManager::loadUser (const juce::File& file)
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            if (xml->hasTagName (state.state.getType()))
            {
                state.replaceState (juce::ValueTree::fromXml (*xml));
                setName (file.getFileNameWithoutExtension());
            }
        }
    }

    bool PresetManager::saveUser (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());

        if (clean.isEmpty())
            return false;

        auto folder = userFolder();

        if (! folder.createDirectory())
            return false;

        setName (clean);

        if (auto xml = state.copyState().createXml())
            return xml->writeTo (folder.getChildFile (clean + fileExtension));

        return false;
    }

    void PresetManager::step (int delta)
    {
        const auto& factory = getFactory();
        const auto user = getUserPresets();
        const int total = factory.size() + user.size();
        const auto current = getCurrentName();
        int index = -1;

        for (int i = 0; i < factory.size(); ++i)
            if (factory[i].name == current)
                index = i;

        if (index < 0)
            for (int i = 0; i < user.size(); ++i)
                if (user[i].getFileNameWithoutExtension() == current)
                    index = factory.size() + i;

        index = ((juce::jmax (0, index) + delta) % total + total) % total;

        if (index < factory.size())
            loadFactory (index);
        else
            loadUser (user[index - factory.size()]);
    }
}
