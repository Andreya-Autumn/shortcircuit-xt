//
// Created by Paul Walker on 2/1/23.
//

#ifndef __SCXT_VOICE_MATRIX_H
#define __SCXT_VOICE_MATRIX_H

#include <string>
#include <array>
#include "utils.h"

namespace scxt::engine
{
struct Zone;
}
namespace scxt::voice
{
struct Voice;
}

namespace scxt::modulation
{

static constexpr int numVoiceRoutingSlots{32};

// These values are streamed so order matters. Basically "always add at the end" is the answer
enum VoiceModMatrixDestination
{
    vmd_none,

    vmd_LFO1_Rate,
    vmd_LFO2_Rate,
    vmd_LFO3_Rate, // We assume these are contiguous in voice.cpp (that is LVO3 = LVO1 + 2)

    vmd_Filter1_Mix,
    vmd_Filter2_Mix,

    numVoiceMatrixDestinations
};

// These values are streamed so order matters. Basically "always add at the end" is the answer
enum VoiceModMatrixSource
{
    vms_none,

    vms_LFO1,
    vms_LFO2,
    vms_LFO3,

    numVoiceMatrixSources,
};

struct VoiceModMatrix : public NonCopyable<VoiceModMatrix>
{
    VoiceModMatrix() { clear(); }
    struct Routing
    {
        VoiceModMatrixSource src{vms_none};
        VoiceModMatrixDestination dst{vmd_none};
        float depth{0};
    };

    std::array<Routing, numVoiceRoutingSlots> routingTable;

    const float *getValuePtr(VoiceModMatrixDestination dest) const
    {
        return &modulatedValues[dest];
    }

    float getValue(VoiceModMatrixDestination dest) const
    {
        return modulatedValues[dest];
    }


    void clear();
    void snapRoutingFromZone(engine::Zone *z);
    void copyBaseValuesFromZone(engine::Zone *z);
    void attachSourcesFromVoice(voice::Voice *v);
    void process();

  protected:
    float *sourcePointers[numVoiceMatrixSources];
    float baseValues[numVoiceMatrixDestinations];
    float modulatedValues[numVoiceMatrixDestinations];
};
} // namespace scxt::modulation

#endif // __SCXT_VOICE_MATRIX_H