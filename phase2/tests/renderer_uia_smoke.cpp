#include "precomp.h"
#include "UiaRenderer.hpp"

class Dispatcher final : public Microsoft::Console::Types::IUiaEventDispatcher
{
public:
    void SignalSelectionChanged() override
    {
        ++selection_events;
    }

    void SignalTextChanged() override
    {
        ++text_events;
    }

    void SignalCursorChanged() override
    {
        ++cursor_events;
    }

    void NotifyNewOutput(const std::wstring_view new_output) override
    {
        output.append(new_output);
    }

    int selection_events = 0;
    int text_events = 0;
    int cursor_events = 0;
    std::wstring output;
};

int main()
{
    Dispatcher dispatcher;
    Microsoft::Console::Render::UiaEngine engine{ &dispatcher };

    if (engine.StartPaint() != S_FALSE)
    {
        return 1;
    }
    if (engine.NotifyNewText(L"hello") != S_OK || engine.StartPaint() != S_OK)
    {
        return 2;
    }
    if (engine.EndPaint() != S_OK || engine.Present() != S_OK)
    {
        return 3;
    }
    if (dispatcher.text_events != 1 || dispatcher.output != L"hello\n")
    {
        return 4;
    }
    if (engine.Disable() != S_OK || engine.NotifyNewText(L"ignored") != S_FALSE)
    {
        return 5;
    }

    return 0;
}
