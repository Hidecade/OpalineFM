#include "PluginEditor.h"

#include "PluginProcessor.h"

Dx21NativeAudioProcessorEditor::Dx21NativeAudioProcessorEditor(Dx21NativeAudioProcessor& processor)
    : AudioProcessorEditor(processor),
      audioProcessor(processor)
{
    mainComponent.setStateChangedCallback([this](const dx21app::SynthState& state)
    {
        audioProcessor.setSynthStateFromEditor(state);
    });
    mainComponent.setRenderModelChangedCallback([this](const dx21::Dx21RenderModel renderModel)
    {
        audioProcessor.setRenderModelFromEditor(renderModel);
    });
    mainComponent.setNoteOnCallback([this](const int note, const int velocity)
    {
        audioProcessor.noteOnFromEditor(note, velocity);
    });
    mainComponent.setNoteOffCallback([this](const int note)
    {
        audioProcessor.noteOffFromEditor(note);
    });
    mainComponent.setAllNotesOffCallback([this]
    {
        audioProcessor.allNotesOffFromEditor();
    });
    mainComponent.setPitchBendCallback([this](const double value)
    {
        audioProcessor.setPitchBendFromEditor(value);
    });
    mainComponent.setModWheelCallback([this](const double value)
    {
        audioProcessor.setModWheelFromEditor(value);
    });
    mainComponent.setProgramNameChangedCallback([this](const juce::String& name)
    {
        audioProcessor.setProgramNameFromEditor(name);
    });

    mainComponent.applySynthState(audioProcessor.getSynthState());
    audioProcessor.setProgramNameFromEditor(mainComponent.currentProgramName());
    addAndMakeVisible(mainComponent);
    setSize(1024, 760);
    startTimerHz(12);
}

Dx21NativeAudioProcessorEditor::~Dx21NativeAudioProcessorEditor()
{
    stopTimer();
}

void Dx21NativeAudioProcessorEditor::resized()
{
    mainComponent.setBounds(getLocalBounds());
}

void Dx21NativeAudioProcessorEditor::timerCallback()
{
    mainComponent.applySynthState(audioProcessor.getSynthState());
    mainComponent.setExternalMidiNoteState(audioProcessor.getMidiUiVelocities());
    mainComponent.setExternalControllerState(audioProcessor.getCurrentPitchBend(),
                                             audioProcessor.getCurrentModWheel());
}
