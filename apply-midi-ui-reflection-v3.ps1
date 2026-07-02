Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-Lf([string]$text) {
    return (($text -replace "`r`n", "`n") -replace "`r", "`n")
}

function Write-Utf8NoBom([string]$path, [string]$text) {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $resolved = (Resolve-Path $path).Path
    [System.IO.File]::WriteAllText($resolved, $text, $utf8NoBom)
}

$headerPath = "Source/App/MainComponent.h"
$cppPath = "Source/App/MainComponent.cpp"

if (!(Test-Path $headerPath)) {
    throw "Source/App/MainComponent.h not found. Run from repository root."
}
if (!(Test-Path $cppPath)) {
    throw "Source/App/MainComponent.cpp not found. Run from repository root."
}

$header = Normalize-Lf (Get-Content -Raw -Encoding UTF8 $headerPath)
$cpp = Normalize-Lf (Get-Content -Raw -Encoding UTF8 $cppPath)

# Header: add methods after mouseExit line.
if ($header -notmatch "setExternalNoteState") {
    $mouseExitPattern = '(?m)^(?<indent>\s*)void mouseExit\(const juce::MouseEvent& event\) override;\s*$'
    if ($header -notmatch $mouseExitPattern) {
        throw "Could not find mouseExit declaration in MainComponent.h."
    }
    $header = [regex]::Replace(
        $header,
        $mouseExitPattern,
        '${indent}void mouseExit(const juce::MouseEvent& event) override;' + "`n" +
        '${indent}void setExternalNoteState(int note, bool active);' + "`n" +
        '${indent}void clearExternalNotes();',
        1
    )
}

# Header: add external note array after heldNote.
if ($header -notmatch "externalNotes") {
    $heldPattern = '(?m)^(?<indent>\s*)int heldNote = -1;\s*$'
    if ($header -notmatch $heldPattern) {
        throw "Could not find heldNote declaration in MainComponent.h."
    }
    $header = [regex]::Replace(
        $header,
        $heldPattern,
        '${indent}int heldNote = -1;' + "`n" +
        '${indent}std::array<bool, 128> externalNotes {};',
        1
    )
}

# Cpp: keyboard highlight condition.
$heldOldPattern = '(?m)^(\s*)const bool held = note == heldNote;\s*$'
if ($cpp -match $heldOldPattern) {
    $cpp = [regex]::Replace(
        $cpp,
        $heldOldPattern,
        '$1const bool held = note == heldNote || externalNotes[static_cast<std::size_t>(note)];',
        1
    )
}

# Cpp: add methods before mouseDown.
if ($cpp -notmatch "KeyboardComponent::setExternalNoteState") {
    $mouseDownPattern = '(?m)^void MainComponent::KeyboardComponent::mouseDown\(const juce::MouseEvent& event\)\s*$'
    if ($cpp -notmatch $mouseDownPattern) {
        throw "Could not find KeyboardComponent::mouseDown in MainComponent.cpp."
    }

    $methods = @'
void MainComponent::KeyboardComponent::setExternalNoteState(const int note, const bool active)
{
    if (!juce::isPositiveAndBelow(note, static_cast<int>(externalNotes.size())))
        return;

    externalNotes[static_cast<std::size_t>(note)] = active;
    repaint();
}

void MainComponent::KeyboardComponent::clearExternalNotes()
{
    externalNotes.fill(false);
    repaint();
}

void MainComponent::KeyboardComponent::mouseDown(const juce::MouseEvent& event)
'@
    $cpp = [regex]::Replace($cpp, $mouseDownPattern, $methods, 1)
}

# Cpp: replace MIDI callback body.
$newMidiHandlerAndNext = @'
void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        const int velocity = static_cast<int>(message.getVelocity() * 127.0f);

        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<MainComponent>(this), note]
            {
                if (safeThis != nullptr)
                    safeThis->keyboard.setExternalNoteState(note, true);
            });

        if (!powerOn)
        {
            juce::MessageManager::callAsync(
                [safeThis = juce::Component::SafePointer<MainComponent>(this), note, velocity]
                {
                    if (safeThis != nullptr)
                        safeThis->noteOn(note, velocity);
                });
            return;
        }

        noteOn(note, velocity);
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();

        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<MainComponent>(this), note]
            {
                if (safeThis != nullptr)
                    safeThis->keyboard.setExternalNoteState(note, false);
            });

        noteOff(note);
    }
    else if (message.isPitchWheel())
    {
        const double value = juce::jlimit(-1.0,
                                          1.0,
                                          (static_cast<double>(message.getPitchWheelValue()) - 8192.0) / 8192.0);
        {
            std::lock_guard<std::mutex> lock(engineMutex);
            engine.setPitchBend(value);
        }

        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<MainComponent>(this), value]
            {
                if (safeThis != nullptr)
                    safeThis->pitchWheelSlider.setValue(value, juce::dontSendNotification);
            });
    }
    else if (message.isController() && message.getControllerNumber() == 1)
    {
        const double value = static_cast<double>(message.getControllerValue()) / 127.0;
        {
            std::lock_guard<std::mutex> lock(engineMutex);
            engine.setModWheel(value);
        }

        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<MainComponent>(this), value]
            {
                if (safeThis != nullptr)
                    safeThis->modWheelSlider.setValue(value, juce::dontSendNotification);
            });
    }
    else if (message.isAllNotesOff())
    {
        allNotesOff();
        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<MainComponent>(this)]
            {
                if (safeThis != nullptr)
                    safeThis->keyboard.clearExternalNotes();
            });
    }
}

void MainComponent::comboBoxChanged
'@

$callbackPattern = '(?s)void MainComponent::handleIncomingMidiMessage\(juce::MidiInput\*, const juce::MidiMessage& message\)\s*\{.*?\n\}\s*\n\s*void MainComponent::comboBoxChanged'
if ($cpp -notmatch $callbackPattern) {
    throw "Could not locate handleIncomingMidiMessage in MainComponent.cpp."
}
$cpp = [regex]::Replace($cpp, $callbackPattern, $newMidiHandlerAndNext, 1)

Write-Utf8NoBom $headerPath $header
Write-Utf8NoBom $cppPath $cpp

Write-Host "OK: MIDI UI reflection changes applied."
Write-Host "Next: git diff"
