#pragma once

#include "Engine/OpalineVoiceLibrary.h"

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace opalineapp
{
std::unique_ptr<juce::XmlElement> voiceLibraryToXml(const opaline::OpalineVoiceLibrary& library);
bool voiceLibraryFromXml(const juce::XmlElement& xml, opaline::OpalineVoiceLibrary& library);
} // namespace opalineapp
