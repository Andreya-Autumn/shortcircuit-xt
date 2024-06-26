//
// Created by Paul Walker on 10/25/23.
//

#include "scxt-plugin.h"
#include "version.h"
#include "components/SCXTEditor.h"

#include "sst/voicemanager/midi1_to_voicemanager.h"

namespace scxt::clap_first::scxt_plugin
{
const clap_plugin_descriptor *getDescription()
{
    static const char *features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SAMPLER,
                                     CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
    static clap_plugin_descriptor desc = {CLAP_VERSION,
                                          "org.surge-synth-team.scxt.clap-first.scxt-plugin",
                                          "Shortcircuit XT (Clap First Version)",
                                          "Surge Synth Team",
                                          "https://surge-synth-team.org",
                                          "",
                                          "",
                                          scxt::build::FullVersionStr,
                                          "The Flagship Creative Sampler from the Surge Synth Team",
                                          &features[0]};
    return &desc;
}

const clap_plugin *makeSCXTPlugin(const clap_host *host)
{
    auto p = new scxt::clap_first::scxt_plugin::SCXTPlugin(host);
    return p->clapPlugin();
}

SCXTPlugin::SCXTPlugin(const clap_host *h) : plugHelper_t(getDescription(), h)
{
    engine = std::make_unique<scxt::engine::Engine>();

    clapJuceShim = std::make_unique<sst::clap_juce_shim::ClapJuceShim>(this);
    clapJuceShim->setResizable(true);
}

SCXTPlugin::~SCXTPlugin()
{
    engine.reset(nullptr);
    scxt::showLeakLog();
}

std::unique_ptr<juce::Component> SCXTPlugin::createEditor()
{
    auto ed = std::make_unique<scxt::ui::SCXTEditor>(
        *(engine->getMessageController()), *(engine->defaults), *(engine->getSampleManager()),
        *(engine->getBrowser()), engine->sharedUIMemoryState);
    ed->onZoomChanged = [this](auto f) {
        if (_host.canUseGui() && clapJuceShim->isEditorAttached())
        {
            _host.guiRequestResize(scxt::ui::SCXTEditor::edWidth * f,
                                   scxt::ui::SCXTEditor::edHeight * f);
        }
    };
    ed->setSize(scxt::ui::SCXTEditor::edWidth, scxt::ui::SCXTEditor::edHeight);
    return ed;
}
bool SCXTPlugin::stateSave(const clap_ostream *ostream) noexcept
{
    engine->getSampleManager()->purgeUnreferencedSamples();
    try
    {
        auto xml = scxt::json::streamEngineState(*engine);

        auto c = xml.c_str();
        auto s = xml.length(); // write the null terminator
        while (s > 0)
        {
            auto r = ostream->write(ostream, c, s);
            if (r < 0)
                return false;
            s -= r;
            c += r;
        }
    }
    catch (const std::runtime_error &err)
    {
        SCLOG("Streaming exception [" << err.what() << "]");
        return false;
    }
    return true;
}
bool SCXTPlugin::stateLoad(const clap_istream *istream) noexcept
{
    static constexpr uint32_t initSize = 1 << 16, chunkSize = 1 << 8;
    std::vector<char> buffer;
    buffer.resize(initSize);

    int64_t rd{0};
    int64_t totalRd{0};
    auto bp = buffer.data();

    while ((rd = istream->read(istream, bp, chunkSize)) > 0)
    {
        bp += rd;
        totalRd += rd;
        if (totalRd >= buffer.size() - chunkSize - 1)
        {
            buffer.resize(buffer.size() * 2);
            bp = buffer.data() + totalRd;
        }
    }
    buffer[totalRd] = 0;

    auto xml = std::string(buffer.data());

    scxt::messaging::client::clientSendToSerialization(
        scxt::messaging::client::UnstreamIntoEngine{xml}, *engine->getMessageController());
    return true;
}

uint32_t SCXTPlugin::audioPortsCount(bool isInput) const noexcept
{
    if (isInput)
        return 0;
    else
        return numPluginOutputs;
}
bool SCXTPlugin::audioPortsInfo(uint32_t index, bool isInput,
                                clap_audio_port_info *info) const noexcept
{
    if (isInput)
        return false;

    if (index == 0)
    {
        info->id = 0;
        info->in_place_pair = CLAP_INVALID_ID;
        strncpy(info->name, "Main Output", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
    }
    else if (index < numPluginOutputs)
    {
        info->id = 1000 + index - 1;
        info->in_place_pair = CLAP_INVALID_ID;
        snprintf(info->name, sizeof(info->name) - 1, "Output %02d", index);
        info->flags = 0;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
    }

    return true;
}

uint32_t SCXTPlugin::notePortsCount(bool isInput) const noexcept { return isInput ? 1 : 0; }
bool SCXTPlugin::notePortsInfo(uint32_t index, bool isInput,
                               clap_note_port_info *info) const noexcept
{
    if (isInput)
    {
        info->id = 1;
        info->supported_dialects =
            CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE | CLAP_NOTE_DIALECT_CLAP;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        strncpy(info->name, "Note Input", CLAP_NAME_SIZE - 1);
        return true;
    }
    else
    {
        return false;
    }
    return true;
}
clap_process_status SCXTPlugin::process(const clap_process *process) noexcept
{
    float **out = process->audio_outputs[0].data32;
    auto chans = process->audio_outputs->channel_count;
    if (chans != 2)
    {
        return CLAP_PROCESS_SLEEP;
    }

    if (process->transport)
    {
        auto &t = process->transport;
        engine->transport.tempo = t->tempo;

        // isRecording should always imply isPlaying but better safe than sorry
        if (t->flags & CLAP_TRANSPORT_IS_PLAYING || t->flags & CLAP_TRANSPORT_IS_RECORDING)
        {
            engine->transport.hostTimeInBeats = 1.0 * t->song_pos_beats / CLAP_BEATTIME_FACTOR;
            engine->transport.lastBarStartInBeats = 1.0 * t->bar_start / CLAP_BEATTIME_FACTOR;
            engine->transport.timeInBeats = engine->transport.hostTimeInBeats;
        }

        engine->transport.status = scxt::engine::Transport::Status::STOPPED;
        if (t->flags & CLAP_TRANSPORT_IS_PLAYING)
            engine->transport.status &= scxt::engine::Transport::Status::PLAYING;
        if (t->flags & CLAP_TRANSPORT_IS_RECORDING)
            engine->transport.status &= scxt::engine::Transport::Status::RECORDING;
        if (t->flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE)
            engine->transport.status &= scxt::engine::Transport::Status::LOOPING;

        engine->transport.signature.numerator = t->tsig_num;
        engine->transport.signature.denominator = t->tsig_denom;
        engine->onTransportUpdated();
    }
    else
    {
        engine->transport.tempo = 120;
        engine->transport.signature.numerator = 4;
        engine->transport.signature.denominator = 4;
        engine->onTransportUpdated();
    }

    auto &ptch = engine->getPatch();
    auto &main = ptch->busses.mainBus.output;

    auto ev = process->in_events;
    auto sz = ev->size(ev);

    const clap_event_header_t *nextEvent{nullptr};
    uint32_t nextEventIndex{0};
    if (sz != 0)
    {
        nextEvent = ev->get(ev, nextEventIndex);
    }

    for (auto s = 0U; s < process->frames_count; ++s)
    {
        if (blockPos == 0)
        {
            // Only realy need to run events when we do the block process
            while (nextEvent && nextEvent->time <= s)
            {
                handleEvent(nextEvent);
                nextEventIndex++;
                if (nextEventIndex < sz)
                    nextEvent = ev->get(ev, nextEventIndex);
                else
                    nextEvent = nullptr;
            }

            engine->processAudio();
            engine->transport.timeInBeats += (double)scxt::blockSize * engine->transport.tempo *
                                             engine->getSampleRateInv() / 60.f;
        }

        // TODO: this can be way more efficient and block wise and stuff
        out[0][s] = main[0][blockPos];
        out[1][s] = main[1][blockPos];
        for (int i = 0; i < scxt::numNonMainPluginOutputs; ++i)
        {
            float **pout = process->audio_outputs[i + 1].data32;

            if (pout)
            {
                pout[0][s] = 0.f;
                pout[1][s] = 0.f;
            }
            if (engine->getPatch()->usesOutputBus(i + 1))
            {
                if (pout)
                {
                    pout[0][s] = engine->getPatch()->busses.pluginNonMainOutputs[i][0][blockPos];
                    pout[1][s] = engine->getPatch()->busses.pluginNonMainOutputs[i][0][blockPos];
                }
            }
        }

        blockPos = (blockPos + 1) & (scxt::blockSize - 1);
    }

    // CLean up past-last-process events since we only sweep when processing in main loop to avoid
    // per sample if
    while (nextEvent)
    {
        handleEvent(nextEvent);
        nextEventIndex++;
        if (nextEventIndex < sz)
            nextEvent = ev->get(ev, nextEventIndex);
        else
            nextEvent = nullptr;
    }

    assert(!nextEvent);

    return CLAP_PROCESS_CONTINUE;
}

bool SCXTPlugin::handleEvent(const clap_event_header_t *nextEvent)
{
    /*
     * This body is a hack until we do the voice manager
     */
    if (nextEvent->space_id == CLAP_CORE_EVENT_SPACE_ID)
    {
        switch (nextEvent->type)
        {
        case CLAP_EVENT_MIDI:
        {
            auto mevt = reinterpret_cast<const clap_event_midi *>(nextEvent);
            sst::voicemanager::applyMidi1Message(engine->voiceManager, mevt->port_index,
                                                 mevt->data);
        }
        break;

        case CLAP_EVENT_NOTE_ON:
        {
            auto nevt = reinterpret_cast<const clap_event_note *>(nextEvent);
            engine->voiceManager.processNoteOnEvent(nevt->port_index, nevt->channel, nevt->key,
                                                    nevt->note_id, nevt->velocity, 0.f);
        }
        break;

        case CLAP_EVENT_NOTE_OFF:
        {
            auto nevt = reinterpret_cast<const clap_event_note *>(nextEvent);
            engine->voiceManager.processNoteOffEvent(nevt->port_index, nevt->channel, nevt->key,
                                                     nevt->note_id, nevt->velocity);
        }
        break;
        }
    }
    return true;
}

bool SCXTPlugin::activate(double sampleRate, uint32_t minFrameCount,
                          uint32_t maxFrameCount) noexcept
{
    engine->prepareToPlay(sampleRate);
    return true;
}

} // namespace scxt::clap_first::scxt_plugin