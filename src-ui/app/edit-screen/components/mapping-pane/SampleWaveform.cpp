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

#include "SampleWaveform.h"
#include "app/SCXTEditor.h"

#include "VariantDisplay.h"

namespace scxt::ui::app::edit_screen
{

SampleWaveform::SampleWaveform(VariantDisplay *d) : display(d), HasEditor(d->editor)
{
    addAndMakeVisible(samplePlaybackPosition);
}

void SampleWaveform::rebuildHotZones()
{
    rebuildEnvelopePaths();
    static constexpr int hotZoneSize{10};
    auto &v = display->variantView.variants[display->selectedVariation];
    auto samp = editor->sampleManager.getSample(v.sampleID);
    if (!samp)
    {
        return;
    }
    auto r = getLocalBounds();

    auto fade = xPixelForSampleDistance(v.loopFade);
    auto start = xPixelForSample(v.startSample);
    auto end = xPixelForSample(v.endSample);
    auto ls = xPixelForSample(v.startLoop);
    auto le = xPixelForSample(v.endLoop);

    startSampleHZ = juce::Rectangle<int>(start + r.getX(), r.getBottom() - hotZoneSize, hotZoneSize,
                                         hotZoneSize);
    endSampleHZ = juce::Rectangle<int>(end + r.getX() - hotZoneSize, r.getBottom() - hotZoneSize,
                                       hotZoneSize, hotZoneSize);
    startLoopHZ = juce::Rectangle<int>(ls + r.getX(), r.getY(), hotZoneSize, hotZoneSize);
    endLoopHZ =
        juce::Rectangle<int>(le + r.getX() - hotZoneSize, r.getY(), hotZoneSize, hotZoneSize);

    fadeLoopHz = juce::Rectangle<int>(r.getX() + ls - fade, r.getY(), fade, r.getHeight());
    repaint();
}

int64_t SampleWaveform::sampleForXPixel(float xpos)
{
    auto r = getLocalBounds();
    auto &v = display->variantView.variants[display->selectedVariation];
    auto samp = editor->sampleManager.getSample(v.sampleID);
    if (!samp)
        return -1;

    // OK so going the other way
    // (sp / l - start) * zf * width = px (taht's below) so
    // px / (zf * width) = sp / l - start
    // (px / (zf * width) + start) * l = sp

    auto l = samp->getSampleLength();
    auto res = (xpos / (zoomFactor * getWidth()) + pctStart) * l;
    return (int64_t)std::clamp(res, 0.f, l * 1.f);
}

int SampleWaveform::xPixelForSampleDistance(int64_t sampleDistance)
{
    auto r = getLocalBounds();
    auto &v = display->variantView.variants[display->selectedVariation];
    auto sample = editor->sampleManager.getSample(v.sampleID);
    if (sample)
    {
        auto l = sample->getSampleLength();
        float sPct = 1.0 * sampleDistance / l;
        sPct *= zoomFactor;
        sPct *= getWidth();
        return sPct;
    }
    return 0;
}
int SampleWaveform::xPixelForSample(int64_t samplePos, bool doClamp)
{
    auto r = getLocalBounds();
    auto &v = display->variantView.variants[display->selectedVariation];
    auto sample = editor->sampleManager.getSample(v.sampleID);
    if (sample)
    {
        auto l = sample->getSampleLength();
        float sPct = 1.0 * samplePos / l;
        sPct -= pctStart;
        sPct *= zoomFactor;

        if (doClamp)
            return std::clamp(static_cast<int>(r.getWidth() * sPct), 0, r.getWidth());
        else
            return static_cast<int>(r.getWidth() * sPct);
    }
    else
    {
        return -1;
    }
}

void SampleWaveform::rebuildEnvelopePaths()
{
    if (display->selectedVariation < 0 || display->selectedVariation > scxt::maxVariantsPerZone)
        return;

    auto r = getLocalBounds();
    auto &v = display->variantView.variants[display->selectedVariation];
    auto samp = editor->sampleManager.getSample(v.sampleID);
    if (!samp)
    {
        return;
    }

    /*
     * OK so we have pctStart and zoomFactor so whats that in sample space
     */
    auto l = samp->getSampleLength();
    int samplePad{10}; // a fudge for very very high zooms stops start glitches
    auto startSample = std::clamp((int)std::floor(l * pctStart) - samplePad, 0, (int)l);
    auto numSamples = (int)std::ceil(1.f * l / zoomFactor);
    auto endSample = std::clamp(startSample + numSamples + 2 * samplePad, 0, (int)l);

    std::vector<std::pair<size_t, float>> topLine, bottomLine;
    auto fac = std::max(1.0 * numSamples / r.getWidth(), 1.0);

    auto downSampleForUI = [startSample, endSample, fac, &topLine, &bottomLine](auto *data) {
        using T = std::remove_pointer_t<decltype(data)>;
        double c = startSample;
        int ct = 0;
        auto seedmx = std::numeric_limits<T>::min();
        auto seedmn = std::numeric_limits<T>::max();

        if constexpr (std::is_same_v<T, float>)
        {
            seedmx = -100.f;
            seedmn = 100.f;
        }
        auto mx = seedmn;
        auto mn = seedmn;

        T normFactor{1};
        if constexpr (std::is_same_v<T, int16_t>)
        {
            normFactor = std::numeric_limits<T>::max();
        }
        for (int s = startSample; s < endSample; ++s)
        {
            if (c + fac < s)
            {
                double nmx = mx * 1.0 / normFactor;
                double nmn = mn * 1.0 / normFactor;

                topLine.emplace_back(s, nmx);
                bottomLine.emplace_back(s, nmn);

                c += fac;
                ct++;
                mx = seedmx;
                mn = seedmn;
            }
            mx = std::max(data[s], mx);
            mn = std::min(data[s], mn);
        }
    };

    if (samp->bitDepth == sample::Sample::BD_I16)
    {
        auto d = samp->GetSamplePtrI16(0);
        downSampleForUI(d);
    }
    else if (samp->bitDepth == sample::Sample::BD_F32)
    {
        auto d = samp->GetSamplePtrF32(0);
        downSampleForUI(d);
    }
    else
    {
        jassertfalse;
    }

    bool first{true};
    upperFill = juce::Path();
    upperStroke = juce::Path();
    lowerFill = juce::Path();
    lowerStroke = juce::Path();

    auto sVToPx = [this](float val) {
        // val is -1..1 so move it to 0..1 inverted
        auto nval = (-val + 1) * 0.5;
        // then scale it by height
        return nval * getHeight();
    };
    for (const auto &[smp, val] : topLine)
    {
        auto pos = xPixelForSample(smp);
        auto uval = std::max(val, 0.0f);

        if (first)
        {
            upperStroke.startNewSubPath(pos, sVToPx(val));
            upperFill.startNewSubPath(pos, sVToPx(uval));
            first = false;
        }
        else
        {
            upperStroke.lineTo(pos, sVToPx(val));
            upperFill.lineTo(pos, sVToPx(uval));
        }
    }

    for (const auto &[smp, val] : bottomLine)
    {
        auto pos = xPixelForSample(smp);
        auto lval = std::min(val, 0.0f);
        if (first)
        {
            lowerStroke.startNewSubPath(pos, sVToPx(val));
            lowerFill.startNewSubPath(pos, sVToPx(lval));
            first = false;
        }
        else
        {
            lowerStroke.lineTo(pos, sVToPx(val));
            lowerFill.lineTo(pos, sVToPx(lval));
        }
    }

    std::reverse(bottomLine.begin(), bottomLine.end());
    std::reverse(topLine.begin(), topLine.end());

    first = true;
    for (const auto &[smp, val] : bottomLine)
    {
        auto pos = xPixelForSample(smp);
        auto uval = std::max(val, 0.f);
        upperFill.lineTo(pos, sVToPx(uval));
    }

    for (const auto &[smp, val] : topLine)
    {
        auto pos = xPixelForSample(smp);
        auto uval = std::min(val, 0.f);
        lowerFill.lineTo(pos, sVToPx(uval));
    }

    upperFill.closeSubPath();
    lowerFill.closeSubPath();
}

void SampleWaveform::mouseDown(const juce::MouseEvent &e)
{
    auto posi = e.position.roundToInt();
    if (startSampleHZ.contains(posi))
        mouseState = MouseState::HZ_DRAG_SAMPSTART;
    else if (endSampleHZ.contains(posi))
        mouseState = MouseState::HZ_DRAG_SAMPEND;
    // TODO loopActive check here
    else if (startLoopHZ.contains(posi))
        mouseState = MouseState::HZ_DRAG_LOOPSTART;
    else if (endLoopHZ.contains(posi))
        mouseState = MouseState::HZ_DRAG_LOOPEND;
    else
        mouseState = MouseState::NONE;

    // TODO cursor change and so on
}

void SampleWaveform::mouseDrag(const juce::MouseEvent &e)
{
    if (mouseState == MouseState::NONE)
        return;

    switch (mouseState)
    {
    case MouseState::HZ_DRAG_SAMPSTART:
        display->variantView.variants[display->selectedVariation].startSample =
            sampleForXPixel(e.position.x);
        break;
    case MouseState::HZ_DRAG_SAMPEND:
        display->variantView.variants[display->selectedVariation].endSample =
            sampleForXPixel(e.position.x);
        break;
    case MouseState::HZ_DRAG_LOOPSTART:
        display->variantView.variants[display->selectedVariation].startLoop =
            sampleForXPixel(e.position.x);
        break;
    case MouseState::HZ_DRAG_LOOPEND:
        display->variantView.variants[display->selectedVariation].endLoop =
            sampleForXPixel(e.position.x);
        break;
    default:
        break;
    }
    display->onSamplePointChangedFromGUI();
}

void SampleWaveform::mouseUp(const juce::MouseEvent &e)
{
    if (mouseState != MouseState::NONE)
        display->onSamplePointChangedFromGUI();
}

void SampleWaveform::mouseMove(const juce::MouseEvent &e)
{
    auto posi = e.position.roundToInt();
    if (startSampleHZ.contains(posi) || endSampleHZ.contains(posi) || startLoopHZ.contains(posi) ||
        endLoopHZ.contains(posi))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SampleWaveform::mouseDoubleClick(const juce::MouseEvent &e)
{
    display->parentPane->selectTab(1);
}

void SampleWaveform::paint(juce::Graphics &g)
{
    g.fillAll(editor->themeColor(theme::ColorMap::bg_2));
    if (!display->active)
        return;

    auto r = getLocalBounds();
    auto &v = display->variantView.variants[display->selectedVariation];
    auto samp = editor->sampleManager.getSample(v.sampleID);
    if (!samp)
    {
        return;
    }

    g.setColour(editor->themeColor(theme::ColorMap::grid_secondary));
    g.drawHorizontalLine(getHeight() / 2, 0, getWidth());

    auto l = samp->getSampleLength();

    auto ssp = xPixelForSample(v.startSample);
    auto esp = xPixelForSample(v.endSample);

    auto a1b = editor->themeColor(theme::ColorMap::accent_1b);
    auto gTop = juce::ColourGradient{a1b, 0, 0, a1b.withAlpha(0.32f), 0, getHeight() / 2.f, false};
    auto gBot = juce::ColourGradient{a1b.withAlpha(0.32f), 0,    getHeight() / 2.f, a1b, 0,
                                     getHeight() * 1.f,    false};

    auto a1a = editor->themeColor(theme::ColorMap::accent_1a);
    auto sTop = juce::ColourGradient{a1a, 0, 0, a1a.withAlpha(0.32f), 0, getHeight() / 2.f, false};
    auto sBot = juce::ColourGradient{a1a.withAlpha(0.32f), 0,    getHeight() / 2.f, a1a, 0,
                                     getHeight() * 1.f,    false};

    auto a2a = editor->themeColor(theme::ColorMap::accent_2a);
    auto lTop = juce::ColourGradient{a2a, 0, 0, a2a.withAlpha(0.32f), 0, getHeight() / 2.f, false};
    auto lBot = juce::ColourGradient{a2a.withAlpha(0.32f), 0,    getHeight() / 2.f, a2a, 0,
                                     getHeight() * 1.f,    false};

    if (ssp >= 0)
    {
        juce::Graphics::ScopedSaveState gs(g);
        auto cr = r.withRight(ssp);
        g.reduceClipRegion(cr);

        g.setGradientFill(gTop);
        g.fillPath(upperFill);

        g.setGradientFill(gBot);
        g.fillPath(lowerFill);
    }
    if (esp <= getWidth())
    {
        juce::Graphics::ScopedSaveState gs(g);
        auto cr = r.withTrimmedLeft(esp);
        g.reduceClipRegion(cr);

        g.setGradientFill(gTop);
        g.fillPath(upperFill);

        g.setGradientFill(gBot);
        g.fillPath(lowerFill);
    }

    auto spC = std::clamp(ssp, 0, getWidth());
    auto epC = std::clamp(esp, 0, getWidth());
    {
        juce::Graphics::ScopedSaveState gs(g);
        auto cr = r.withLeft(spC).withRight(epC);
        g.reduceClipRegion(cr);

        g.setGradientFill(sTop);
        g.fillPath(upperFill);

        g.setGradientFill(sBot);
        g.fillPath(lowerFill);

        g.setColour(a1a);
        g.strokePath(upperStroke, juce::PathStrokeType(1));
        g.strokePath(lowerStroke, juce::PathStrokeType(1));
    }

    if (v.loopActive)
    {
        auto ls = std::clamp(xPixelForSample(v.startLoop), 0, getWidth());
        auto le = std::clamp(xPixelForSample(v.endLoop), 0, getWidth());

        if (!((ls < 0 && le < 0) || (ls > getWidth() && le > getWidth())))
        {
            auto a2b = editor->themeColor(theme::ColorMap::accent_2b);
            auto dr = r.withLeft(ls).withRight(le);
            g.setColour(editor->themeColor(theme::ColorMap::bg_2));
            g.fillRect(dr);
            g.setColour(a2b.withAlpha(0.2f));
            g.fillRect(dr);

            juce::Graphics::ScopedSaveState gs(g);
            g.reduceClipRegion(dr);

            g.setGradientFill(lTop);
            g.fillPath(upperFill);

            g.setGradientFill(lBot);
            g.fillPath(lowerFill);

            g.setColour(a1a);
            g.strokePath(upperStroke, juce::PathStrokeType(1));
            g.strokePath(lowerStroke, juce::PathStrokeType(1));
        }
    }

    auto ss = xPixelForSample(v.startSample, false);
    auto se = xPixelForSample(v.endSample, false);
    auto bg1 = editor->themeColor(theme::ColorMap::bg_1);
    auto imf = editor->themeApplier.interBoldFor(10);
    if (ss >= 0 && ss <= getWidth())
    {
        g.setColour(a1a);
        g.fillRect(startSampleHZ);
        g.drawVerticalLine(startSampleHZ.getX(), 0, getHeight());
        g.setColour(bg1);
        g.setFont(imf);
        g.drawText("S", startSampleHZ, juce::Justification::centred);
    }
    if (se >= 0 && se <= getWidth())
    {
        g.setColour(a1a);
        g.fillRect(endSampleHZ);
        g.drawVerticalLine(endSampleHZ.getRight(), 0, getHeight());
        g.setColour(bg1);
        g.setFont(imf);
        g.drawText("E", endSampleHZ, juce::Justification::centred);
    }
    if (v.loopActive)
    {
        auto ls = xPixelForSample(v.startLoop, false);
        auto fs = xPixelForSample(v.startLoop - v.loopFade, false);
        auto fe = xPixelForSample(v.startLoop + v.loopFade, false);
        auto le = xPixelForSample(v.endLoop, false);

        g.setColour(juce::Colours::aliceblue);
        if (ls >= 0 && ls <= getWidth())
        {
            g.setColour(a2a);

            g.fillRect(startLoopHZ);
            g.drawVerticalLine(startLoopHZ.getX(), 0, getHeight());

            g.setColour(bg1);
            g.setFont(imf);
            g.drawText(">", startLoopHZ, juce::Justification::centred);
        }
        if (le >= 0 && le <= getWidth())
        {
            g.setColour(a2a);

            g.fillRect(endLoopHZ);
            g.drawVerticalLine(endLoopHZ.getRight(), 0, getHeight());

            g.setColour(bg1);
            g.setFont(imf);
            g.drawText("<", endLoopHZ, juce::Justification::centred);
        }
        if (v.loopFade > 0 && ((fe >= 0 && fe <= getWidth()) || (fs >= 0 && fs <= getWidth())))
        {
            g.setColour(editor->themeColor(theme::ColorMap::generic_content_medium));
            g.drawLine(fs, getHeight(), ls, 0);
            g.drawLine(ls, 0, fe, getHeight());
        }
    }

    if (samp->meta.n_slices > 0)
    {
        g.setColour(editor->themeColor(theme::ColorMap::grid_primary));
        for (int i = 0; i < samp->meta.n_slices; ++i)
        {
            auto sp = xPixelForSample(samp->meta.slice_start[i]);
            auto ep = xPixelForSample(samp->meta.slice_end[i]);
            g.drawVerticalLine(sp, 0, getHeight());
            g.drawVerticalLine(ep, 0, getHeight());
        }
    }
}

void SampleWaveform::resized()
{
    rebuildHotZones();
    samplePlaybackPosition.setSize(1, getHeight());
}

void SampleWaveform::updateSamplePlaybackPosition(int64_t samplePos)
{
    auto x = xPixelForSample(samplePos);
    samplePlaybackPosition.setTopLeftPosition(x, 0);
}
} // namespace scxt::ui::app::edit_screen