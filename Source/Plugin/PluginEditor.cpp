#include "PluginEditor.h"

#include "PluginProcessor.h"

OpalineAudioProcessorEditor::OpalineAudioProcessorEditor(OpalineAudioProcessor& processor)
    : AudioProcessorEditor(processor),
      audioProcessor(processor),
      mainComponent(MainComponent::HostMode::PluginEditor,
                    processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
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
    mainComponent.setWavRecordingCallbacks([this]
    {
        audioProcessor.startWavRecording();
    },
    [this]
    {
        audioProcessor.stopWavRecording();
    },
    [this](const juce::File& file)
    {
        return audioProcessor.stopWavRecordingAndSaveToFile(file);
    });

    mainComponent.applySynthState(audioProcessor.getSynthState(), true);
    audioProcessor.setSynthStateFromEditor(mainComponent.captureSynthState());
    audioProcessor.setProgramNameFromEditor(mainComponent.currentProgramName());
    audioProcessor.setScopeCaptureEnabled(true);
    addAndMakeVisible(mainComponent);
    setSize(1024, 668);
    startTimerHz(60);
}

OpalineAudioProcessorEditor::~OpalineAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.setScopeCaptureEnabled(false);
}

void OpalineAudioProcessorEditor::resized()
{
    mainComponent.setBounds(getLocalBounds());
}

void OpalineAudioProcessorEditor::visibilityChanged()
{
    audioProcessor.setScopeCaptureEnabled(isShowing());
}

void OpalineAudioProcessorEditor::timerCallback()
{
    if (!isShowing())
        return;

    if (auto* modalManager = juce::ModalComponentManager::getInstanceWithoutCreating())
    {
        if (modalManager->getNumModalComponents() > 0)
        {
            externalStateSyncHoldoffFrames = 12;
            return;
        }
    }

    if (externalStateSyncHoldoffFrames > 0)
    {
        --externalStateSyncHoldoffFrames;
        return;
    }

    mainComponent.applySynthState(audioProcessor.getSynthState());
    mainComponent.setExternalMidiNoteState(audioProcessor.getMidiUiVelocities());
    mainComponent.setExternalControllerState(audioProcessor.getCurrentPitchBend(),
                                             audioProcessor.getCurrentModWheel());
    mainComponent.setExternalScopeSamples(audioProcessor.getScopeSamples(), audioProcessor.getSampleRate());
}
