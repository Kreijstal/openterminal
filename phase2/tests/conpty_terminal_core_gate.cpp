// A live ConPTY session whose output is ingested by TerminalCore.
//
// This is not a unit test of either half. It spawns a real shell through the
// pseudoconsole that phase 2 builds from microsoft/terminal's src/winconpty,
// types a command into the pseudoconsole's input pipe, and feeds every byte
// the pseudoconsole emits into a Microsoft::Terminal::Core::Terminal the way
// TermControl does -- UTF-8 to UTF-16 through til::u8u16, then
// Terminal::Write under the terminal's own write lock. The question it answers
// is whether the shell's output reaches TerminalCore's text buffer, so the
// answer is read back out of that buffer and nowhere else.
//
// It writes one JSON object to stdout and exits 0 only when the marker the
// shell produced is in that buffer.
//
// The ingestion step itself is not implemented yet: with
// --ingestion terminal-core the harness refuses by name rather than
// pretending a buffer nothing wrote to is a measurement.
//
// Two pseudoconsole creation paths are measured, not assumed:
//
// - "create": ConptyCreatePseudoConsole, the one every ConPTY client uses. It
//   fails on Wine 11.13/11.14 because winconpty opens the console reference as
//   the relative name "\Reference" under the server handle, and Wine's
//   nt_to_unix_file_name() rejects any relative name that starts with a
//   backslash with STATUS_INVALID_PARAMETER before the wineserver sees it
//   (dlls/ntdll/unix/file.c, "if (name_len && name[0] == '\\') return
//   STATUS_INVALID_PARAMETER;"). Windows resolves the same name through the
//   ConDrv parse routine.
// - "pack": the same library's ConptyPackPseudoConsole, the entry point the
//   console-handoff path uses, over a server handle, a "Reference" client
//   handle and a signal pipe this harness opens itself, with the console host
//   spawned with winconpty's own --headless/--signal/--server command line.
//
// "auto" uses "create" and falls back to "pack" only when "create" fails, and
// the JSON says which one carried the session. The day Wine stops rejecting
// the name, "auto" silently goes back to the unmodified path and --pty-path
// create starts succeeding.
//
// --ingestion none is a negative control: everything else runs unchanged and
// the pseudoconsole bytes are dropped instead of written to the terminal. It
// must fail. A gate that cannot be made to fail is not measuring anything.

#include "pch.h"
#include "Terminal.hpp"

#include <DummyRenderer.hpp>
#include <DeviceHandle.h>
#include <conpty-static.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::Terminal::Core::Terminal;
using Clock = std::chrono::steady_clock;

namespace
{
    struct Report
    {
        std::string refusal;
        std::string detail;
        std::string pty_path;
        long long create_hresult = 0;
        long long pack_hresult = 0;
        bool pseudoconsole_created = false;
        bool shell_started = false;
        unsigned long shell_pid = 0;
        unsigned long long pty_bytes_read = 0;
        unsigned long long wide_chars_written = 0;
        bool marker_in_terminal_buffer = false;
        int marker_row = -1;
        long long elapsed_ms = 0;
        std::wstring buffer_text;
    };

    std::string narrow(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const auto needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return {};
        }
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
        return out;
    }

    std::string json_string(std::string_view text)
    {
        std::string out{ "\"" };
        for (const auto c : text)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char escape[7];
                    std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned char>(c));
                    out += escape;
                }
                else
                {
                    out += c;
                }
                break;
            }
        }
        out += '"';
        return out;
    }

    void emit(const Report& report, std::string_view marker, std::string_view ingestion)
    {
        std::string out{ "{\n" };
        out += "  \"ingestion\": " + json_string(ingestion) + ",\n";
        out += "  \"marker\": " + json_string(marker) + ",\n";
        out += "  \"refusal\": " + json_string(report.refusal) + ",\n";
        out += "  \"detail\": " + json_string(report.detail) + ",\n";
        out += "  \"pty_path\": " + json_string(report.pty_path) + ",\n";
        out += "  \"conpty_create_hresult\": " + std::to_string(report.create_hresult) + ",\n";
        out += "  \"conpty_pack_hresult\": " + std::to_string(report.pack_hresult) + ",\n";
        out += "  \"pseudoconsole_created\": " + std::string(report.pseudoconsole_created ? "true" : "false") + ",\n";
        out += "  \"shell_started\": " + std::string(report.shell_started ? "true" : "false") + ",\n";
        out += "  \"shell_pid\": " + std::to_string(report.shell_pid) + ",\n";
        out += "  \"pty_bytes_read\": " + std::to_string(report.pty_bytes_read) + ",\n";
        out += "  \"wide_chars_written\": " + std::to_string(report.wide_chars_written) + ",\n";
        out += "  \"marker_in_terminal_buffer\": " + std::string(report.marker_in_terminal_buffer ? "true" : "false") + ",\n";
        out += "  \"marker_row\": " + std::to_string(report.marker_row) + ",\n";
        out += "  \"elapsed_ms\": " + std::to_string(report.elapsed_ms) + ",\n";
        out += "  \"terminal_buffer\": " + json_string(narrow(report.buffer_text)) + "\n";
        out += "}\n";
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);
    }

    // Everything the terminal has: every row of its text buffer, trailing
    // blanks trimmed, joined with newlines. Read through the terminal's own
    // read lock and its own text buffer -- never from the pseudoconsole pipe.
    std::wstring read_terminal_buffer(Terminal& terminal, int* markerRow, std::wstring_view marker)
    {
        const auto lock = terminal.LockForReading();
        auto& buffer = terminal.GetTextBuffer();
        const auto lastRow = buffer.GetLastNonSpaceCharacter().y;
        std::wstring text;
        for (til::CoordType y = 0; y <= lastRow; ++y)
        {
            std::wstring row{ buffer.GetRowByOffset(y).GetText() };
            while (!row.empty() && row.back() == L' ')
            {
                row.pop_back();
            }
            if (markerRow != nullptr && *markerRow < 0 && !marker.empty() &&
                row.find(marker) != std::wstring::npos)
            {
                *markerRow = static_cast<int>(y);
            }
            text += row;
            text += L'\n';
        }
        return text;
    }

    bool write_all(HANDLE pipe, std::string_view bytes)
    {
        while (!bytes.empty())
        {
            DWORD written = 0;
            if (!WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written == 0)
            {
                return false;
            }
            bytes.remove_prefix(written);
        }
        return true;
    }

    std::string argument_value(int argc, wchar_t** argv, std::wstring_view name, std::string_view fallback)
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (name == argv[i])
            {
                return narrow(argv[i + 1]);
            }
        }
        return std::string{ fallback };
    }

    // The console host reads these with wcstol(argv, &end, 0) and rejects any
    // trailing character, so the value has to be exactly "0x" and hex digits.
    std::wstring handle_argument(HANDLE handle)
    {
        auto value = reinterpret_cast<uintptr_t>(handle);
        std::wstring digits;
        do
        {
            digits.insert(digits.begin(), L"0123456789abcdef"[value & 0xf]);
            value >>= 4;
        } while (value != 0);
        return L"0x" + digits;
    }

    std::wstring console_host_path()
    {
        std::wstring directory(MAX_PATH, L'\0');
        const auto length = GetSystemDirectoryW(directory.data(), MAX_PATH);
        directory.resize(length);
        return directory + L"\\conhost.exe";
    }

    // winconpty's own console-host command line, over handles this harness
    // opened. Everything the pseudoconsole *is* afterwards -- the signal
    // protocol, the reference handle's lifetime, ConptyClosePseudoConsole --
    // is still the phase-2 winconpty library's.
    HRESULT pack_pseudoconsole(COORD size, HANDLE input, HANDLE output, HPCON* pseudoConsole, std::string& detail)
    {
        HANDLE server{ nullptr };
        auto status = DeviceHandle::CreateServerHandle(&server, TRUE);
        if (!NT_SUCCESS(status))
        {
            detail = "DeviceHandle::CreateServerHandle status " + std::to_string(static_cast<long>(status));
            return HRESULT_FROM_NT(status);
        }

        // "Reference", not winconpty's "\Reference": see the note at the top.
        HANDLE reference{ nullptr };
        status = DeviceHandle::CreateClientHandle(&reference, server, L"Reference", FALSE);
        if (!NT_SUCCESS(status))
        {
            CloseHandle(server);
            detail = "DeviceHandle::CreateClientHandle(Reference) status " + std::to_string(static_cast<long>(status));
            return HRESULT_FROM_NT(status);
        }

        // An overlapped *named* pipe, not winconpty's anonymous one. Wine's
        // conhost arms its signal read with an asynchronous NtReadFile before
        // it enters main_loop (programs/conhost/conhost.c); on a synchronous
        // anonymous pipe that call simply blocks, so the console host stays
        // alive, never serves the console and never writes a byte to the
        // pseudoconsole. This is the second Wine defect this gate had to route
        // around, and it is independent of the "\Reference" one: fixing that
        // alone would still leave ConptyCreatePseudoConsole silent.
        SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        const auto pipeName = L"\\\\.\\pipe\\openterminal-conpty-gate-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetCurrentThreadId());
        auto signalHostSide = CreateNamedPipeW(pipeName.c_str(),
                                               PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                               PIPE_TYPE_BYTE, 1, 4096, 4096, 0, &inheritable);
        auto signalOurSide = signalHostSide == INVALID_HANDLE_VALUE ?
                                 INVALID_HANDLE_VALUE :
                                 CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (signalHostSide == INVALID_HANDLE_VALUE || signalOurSide == INVALID_HANDLE_VALUE)
        {
            CloseHandle(reference);
            CloseHandle(server);
            detail = "signal pipe creation failed, error " + std::to_string(GetLastError());
            return HRESULT_FROM_WIN32(GetLastError());
        }

        const auto host = console_host_path();
        // Assembled rather than printf'd: mingw's wide printf reads %s as a
        // narrow string, which silently produced an unopenable command line.
        auto commandLine = L"\"" + host + L"\" --headless" +
                           L" --width " + std::to_wstring(size.X) +
                           L" --height " + std::to_wstring(size.Y) +
                           L" --signal " + handle_argument(signalHostSide) +
                           L" --server " + handle_argument(server);

        HANDLE inherited[4]{ server, input, output, signalHostSide };
        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
        std::vector<char> attributeStorage(attributeSize);
        auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.hStdInput = input;
        startup.StartupInfo.hStdOutput = output;
        startup.StartupInfo.hStdError = output;
        startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startup.lpAttributeList = attributes;

        PROCESS_INFORMATION host_process{};
        const auto spawned =
            InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize) &&
            UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      inherited, sizeof(inherited), nullptr, nullptr) &&
            CreateProcessW(host.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                           DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                           &startup.StartupInfo, &host_process);
        const auto spawnError = GetLastError();
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(signalHostSide);
        CloseHandle(server);

        if (!spawned)
        {
            CloseHandle(reference);
            CloseHandle(signalOurSide);
            detail = "console host did not start, error " + std::to_string(spawnError);
            return HRESULT_FROM_WIN32(spawnError);
        }
        CloseHandle(host_process.hThread);

        const auto packed = ConptyPackPseudoConsole(host_process.hProcess, reference, signalOurSide, pseudoConsole);
        if (FAILED(packed))
        {
            detail = "ConptyPackPseudoConsole refused the handles";
        }
        return packed;
    }
}

int wmain(int argc, wchar_t** argv)
{
    const auto markerPrefix = argument_value(argc, argv, L"--marker-prefix", "CONPTY_GATE_");
    const auto markerValue = argument_value(argc, argv, L"--marker-value", "1138");
    const auto marker = markerPrefix + markerValue;
    const auto ingestion = argument_value(argc, argv, L"--ingestion", "terminal-core");
    const auto ptyPath = argument_value(argc, argv, L"--pty-path", "auto");
    const auto timeoutMs = std::atoi(argument_value(argc, argv, L"--timeout-ms", "30000").c_str());

    Report report;
    const auto started = Clock::now();
    const auto finish = [&](int code) {
        report.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        emit(report, marker, ingestion);
        return code;
    };

    if (ingestion != "terminal-core" && ingestion != "none")
    {
        report.refusal = "unknown-ingestion-mode";
        report.detail = ingestion;
        return finish(2);
    }
    if (ptyPath != "auto" && ptyPath != "create" && ptyPath != "pack")
    {
        report.refusal = "unknown-pty-path";
        report.detail = ptyPath;
        return finish(2);
    }

    Terminal terminal;
    DummyRenderer renderer{ &terminal };
    terminal.Create({ 80, 30 }, 200, renderer);

    // The console host's ends are made inheritable and ours are not. Wine only
    // hands a child the handles that are both listed in
    // PROC_THREAD_ATTRIBUTE_HANDLE_LIST and marked inheritable; leaving the
    // pipes at CreatePipe's default gave the host no standard output, so it
    // wrote nothing and the read side saw ERROR_BROKEN_PIPE immediately.
    SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE inputRead{ nullptr };
    HANDLE inputWrite{ nullptr };
    HANDLE outputRead{ nullptr };
    HANDLE outputWrite{ nullptr };
    if (!CreatePipe(&inputRead, &inputWrite, &inheritable, 0) ||
        !CreatePipe(&outputRead, &outputWrite, &inheritable, 0) ||
        !SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0))
    {
        report.refusal = "createpipe-failed";
        report.detail = std::to_string(GetLastError());
        return finish(3);
    }

    constexpr COORD size{ 80, 30 };
    HPCON pseudoConsole{ nullptr };
    if (ptyPath == "auto" || ptyPath == "create")
    {
        report.create_hresult = ConptyCreatePseudoConsole(size, inputRead, outputWrite, 0, &pseudoConsole);
        if (SUCCEEDED(report.create_hresult) && pseudoConsole != nullptr)
        {
            report.pty_path = "create";
        }
        else
        {
            pseudoConsole = nullptr;
        }
    }
    if (pseudoConsole == nullptr && (ptyPath == "auto" || ptyPath == "pack"))
    {
        report.pack_hresult = pack_pseudoconsole(size, inputRead, outputWrite, &pseudoConsole, report.detail);
        if (SUCCEEDED(report.pack_hresult) && pseudoConsole != nullptr)
        {
            report.pty_path = "pack";
            report.detail.clear();
        }
        else
        {
            pseudoConsole = nullptr;
        }
    }
    if (pseudoConsole == nullptr)
    {
        report.refusal = "no-pseudoconsole";
        if (report.detail.empty())
        {
            report.detail = "no requested pseudoconsole creation path succeeded";
        }
        return finish(4);
    }
    report.pseudoconsole_created = true;

    // The pseudoconsole owns these ends now. Holding our copies would keep the
    // read side from ever seeing end-of-file.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<char> attributeStorage(attributeSize);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   pseudoConsole, sizeof(pseudoConsole), nullptr, nullptr))
    {
        report.refusal = "pseudoconsole-attribute-failed";
        report.detail = std::to_string(GetLastError());
        return finish(5);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    // Null standard handles, explicitly. Without STARTF_USESTDHANDLES the
    // shell inherits this harness's own stdout -- which is a pipe, not a
    // console handle, so Wine hands it straight through -- and the shell's
    // output never enters the pseudoconsole at all. Nulled here, the shell
    // opens its standard handles from the console it was attached to.
    startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullptr;
    startup.StartupInfo.hStdOutput = nullptr;
    startup.StartupInfo.hStdError = nullptr;

    PROCESS_INFORMATION shell{};
    std::wstring commandLine{ L"cmd.exe /q" };
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                        nullptr, nullptr, &startup.StartupInfo, &shell))
    {
        report.refusal = "createprocess-failed";
        report.detail = std::to_string(GetLastError());
        return finish(6);
    }
    report.shell_started = true;
    report.shell_pid = shell.dwProcessId;
    DeleteProcThreadAttributeList(attributes);

    // The shell must produce the marker itself. Neither line that gets typed
    // contains it: the console echoes back "set OTGATE=1138" and
    // "echo CONPTY_GATE_%OTGATE%", and only the shell's own expansion of
    // that variable can put "CONPTY_GATE_1138" on a row. Finding the marker
    // therefore cannot be satisfied by the input echo.
    const auto script = "set OTGATE=" + markerValue + "\r\necho " + markerPrefix + "%OTGATE%\r\n";
    const std::wstring wideMarker(marker.begin(), marker.end());

    bool typed = false;
    const auto deadline = started + std::chrono::milliseconds(timeoutMs);

    while (Clock::now() < deadline)
    {
        if (!typed && (report.pty_bytes_read > 0 || Clock::now() - started > std::chrono::seconds(2)))
        {
            if (!write_all(inputWrite, script))
            {
                report.refusal = "pseudoconsole-input-write-failed";
                report.detail = std::to_string(GetLastError());
                break;
            }
            typed = true;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr))
        {
            report.refusal = "pseudoconsole-output-closed";
            report.detail = std::to_string(GetLastError());
            break;
        }

        if (available > 0)
        {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!ReadFile(outputRead, chunk.data(), available, &read, nullptr) || read == 0)
            {
                report.refusal = "pseudoconsole-output-read-failed";
                report.detail = std::to_string(GetLastError());
                break;
            }
            chunk.resize(read);
            report.pty_bytes_read += read;

            if (ingestion == "terminal-core")
            {
                // Not implemented yet. The pseudoconsole side is real and the
                // bytes are here; what is missing is the step that turns them
                // into terminal input. Refuse by name rather than invent a
                // buffer that nothing wrote to.
                report.refusal = "terminalcore-ingestion-not-implemented";
                report.detail = "the pseudoconsole's bytes are not written to "
                                "Microsoft::Terminal::Core::Terminal yet";
                break;
            }
        }
        else
        {
            Sleep(20);
        }

        int markerRow = -1;
        auto text = read_terminal_buffer(terminal, &markerRow, wideMarker);
        if (markerRow >= 0)
        {
            report.marker_in_terminal_buffer = true;
            report.marker_row = markerRow;
            report.buffer_text = std::move(text);
            break;
        }
    }

    if (!report.marker_in_terminal_buffer)
    {
        int markerRow = -1;
        report.buffer_text = read_terminal_buffer(terminal, &markerRow, wideMarker);
        report.marker_row = markerRow;
        report.marker_in_terminal_buffer = markerRow >= 0;
        if (report.refusal.empty() && !report.marker_in_terminal_buffer)
        {
            report.refusal = "marker-absent-from-terminalcore-buffer";
            report.detail = "the shell's output never reached TerminalCore's text buffer before the timeout";
        }
    }

    TerminateProcess(shell.hProcess, 0);
    CloseHandle(shell.hThread);
    CloseHandle(shell.hProcess);
    ConptyClosePseudoConsole(pseudoConsole);
    CloseHandle(inputWrite);
    CloseHandle(outputRead);

    return finish(report.marker_in_terminal_buffer ? 0 : 1);
}
