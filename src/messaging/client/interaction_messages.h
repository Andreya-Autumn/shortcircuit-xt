/*
 * Shortcircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2024, Various authors, as described in the github
 * transaction log.
 *
 * ShortcircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortcircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortcircuitXT is inspired by, and shares code with, the
 * commercial product Shortcircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for Shortcircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortcircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#ifndef SCXT_SRC_MESSAGING_CLIENT_INTERACTION_MESSAGES_H
#define SCXT_SRC_MESSAGING_CLIENT_INTERACTION_MESSAGES_H

#include "messaging/client/detail/client_json_details.h"
#include "json/selection_traits.h"
#include "selection/selection_manager.h"
#include "engine/engine.h"
#include "client_macros.h"
#include "patch_io/patch_io.h"

namespace scxt::messaging::client
{
typedef std::pair<std::string, std::string> s2cError_t;
SERIAL_TO_CLIENT(ReportError, s2c_report_error, s2cError_t, onErrorFromEngine);

inline void raiseDebugError(MessageController &c)
{
    c.reportErrorToClient("A Dummy Error",
                          "This is a dummy error. I chose to have it have "
                          "a very long message like this one so I can test multiline "
                          "string rendering in the error box. So this one has details like "
                          "this and that");
}
CLIENT_TO_SERIAL(RaiseDebugError, c2s_raise_debug_error, bool, raiseDebugError(cont))

CLIENT_TO_SERIAL(SetTuningMode, c2s_set_tuning_mode, int32_t,
                 engine.midikeyRetuner.setTuningMode((tuning::MidikeyRetuner::TuningMode)payload));

// note, 0...1 velocity, onoff
typedef std::tuple<int32_t, float, bool> noteOnOff_t;
inline void processMidiFromGUI(const noteOnOff_t &g, const engine::Engine &engine,
                               MessageController &cont)
{
    auto [n, v, onoff] = g;

    auto sel = engine.getSelectionManager()->selectedPart;
    if (sel < 0)
        return;

    auto p = sel;
    auto ch = engine.getPatch()->getPart(p)->configuration.channel;
    if (ch < 0)
        ch = 0;

    if (onoff)
    {
        cont.scheduleAudioThreadCallback([ch, vel = v, note = n](auto &eng) {
            eng.voiceManager.processNoteOnEvent(0, ch, note, -1, vel, 0.f);
        });
    }
    else
    {
        cont.scheduleAudioThreadCallback([ch, vel = v, note = n](auto &eng) {
            eng.voiceManager.processNoteOffEvent(0, ch, note, -1, vel);
        });
    }
}
CLIENT_TO_SERIAL(NoteFromGUI, c2s_noteonoff, noteOnOff_t, processMidiFromGUI(payload, engine, cont))

inline void doHostCallback(uint64_t pl, MessageController &cont)
{
    if (cont.requestHostCallback)
    {
        cont.requestHostCallback(pl);
    }
}
CLIENT_TO_SERIAL(RequestHostCallback, c2s_request_host_callback, uint64_t,
                 doHostCallback(payload, cont));

inline void doResetEngine(const std::string &fl, engine::Engine &e, MessageController &cont)
{
    scxt::patch_io::initFromResourceBundle(e, fl);
    e.sendFullRefreshToClient();
}
CLIENT_TO_SERIAL(ResetEngine, c2s_reset_engine, std::string, doResetEngine(payload, engine, cont));

} // namespace scxt::messaging::client
#endif // SHORTCIRCUIT_INTERACTION_MESSAGES_H
