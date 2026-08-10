#include "precomp.h"
#include "MidiAudio.hpp"

int main()
{
    MidiAudio midi;
    midi.BeginSkip();
    midi.PlayNote(nullptr, 69, 127, std::chrono::hours{ 1 });
    midi.EndSkip();
    return 0;
}
