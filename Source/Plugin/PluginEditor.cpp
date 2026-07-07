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

    mainComponent.applySynthState(audioProcessor.getSynthState());
    addAndMakeVisible(mainComponent);
    setSize(1024, 760);
}

void Dx21NativeAudioProcessorEditor::resized()
{
    mainComponent.setBounds(getLocalBounds());
}
