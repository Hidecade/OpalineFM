#include "PluginEditor.h"

#include "PluginProcessor.h"

OpalineAudioProcessorEditor::OpalineAudioProcessorEditor(OpalineAudioProcessor& processor)
    : AudioProcessorEditor(processor),
      audioProcessor(processor)
{
    mainComponent.setStateChangedCallback([this](const opalineapp::SynthState& state)
    {
        audioProcessor.setSynthStateFromEditor(state);
    });
    mainComponent.setRenderModelChangedCallback([this](const opaline::OpalineRenderModel renderModel)
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

OpalineAudioProcessorEditor::~OpalineAudioProcessorEditor()
{
    stopTimer();
}

void OpalineAudioProcessorEditor::resized()
{
    mainComponent.setBounds(getLocalBounds());
}

void OpalineAudioProcessorEditor::timerCallback()
{
    mainComponent.applySynthState(audioProcessor.getSynthState());
    mainComponent.setExternalMidiNoteState(audioProcessor.getMidiUiVelocities());
    mainComponent.setExternalControllerState(audioProcessor.getCurrentPitchBend(),
                                             audioProcessor.getCurrentModWheel());
}
