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
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    mainComponent.setWantsKeyboardFocus(true);
    mainComponent.setMouseClickGrabsKeyboardFocus(true);
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<OpalineAudioProcessorEditor>(this)]
    {
        if (safeThis != nullptr)
        {
            safeThis->grabKeyboardFocus();
            safeThis->mainComponent.grabKeyboardFocus();
        }
    });
    setSize(1024, 668);
    startTimerHz(60);
}

OpalineAudioProcessorEditor::~OpalineAudioProcessorEditor()
{
    stopTimer();
}

void OpalineAudioProcessorEditor::resized()
{
    mainComponent.setBounds(getLocalBounds());
}

void OpalineAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus();
    mainComponent.grabKeyboardFocus();
}

void OpalineAudioProcessorEditor::timerCallback()
{
    mainComponent.applySynthState(audioProcessor.getSynthState());
    mainComponent.setExternalMidiNoteState(audioProcessor.getMidiUiVelocities());
    mainComponent.setExternalControllerState(audioProcessor.getCurrentPitchBend(),
                                             audioProcessor.getCurrentModWheel());
    mainComponent.setExternalScopeSamples(audioProcessor.getScopeSamples());
}
