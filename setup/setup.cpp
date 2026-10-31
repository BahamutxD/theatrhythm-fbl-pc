// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace setup {
namespace {

// What run.py reads from the dump to prepare the image (IMAGE_INPUTS), and the title's param.sfo.
constexpr const wchar_t* kGameFiles[] = {
    L"eboot.bin",
    L"sce_sys\\param.sfo",
    L"sce_module\\libc.prx",
    L"sce_module\\libSceFios2.prx",
    L"Media\\Modules\\PS4Util.prx",
    L"Media\\Modules\\Il2CppUserAssemblies.prx",
};
constexpr wchar_t kGameExe[] = L"theatrhythm-fbl.exe";
constexpr wchar_t kIncomplete[] = L"\n\nThis folder is incomplete: unpack the whole release again.";

[[noreturn]] void fail(std::wstring message) {
    throw Error{std::move(message)};
}

std::wstring widen(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), u.data(), n, nullptr, nullptr);
    return u;
}

// Absolute and normalized, without a trailing separator.
fs::path clean(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec).lexically_normal();
    std::wstring s = a.wstring();
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/'))
        s.pop_back();
    return s;
}

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// `inner` is `outer` or a folder inside it.
bool inside(const fs::path& inner, const fs::path& outer) {
    std::wstring i = lower(clean(inner).wstring()), o = lower(clean(outer).wstring());
    return i == o || i.rfind(o + L"\\", 0) == 0;
}

std::wstring join(const std::vector<std::wstring>& items) {
    std::wstring s;
    for (const std::wstring& item : items)
        s += (s.empty() ? L"" : L", ") + item;
    return s;
}

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        fail(L"cannot read " + p.wstring());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void write_text(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f)
        fail(L"cannot write " + p.wstring() + L"\nIf the game is running, close it and try again.");
}

// --- child processes ----------------------------------------------------------------------------

// One argument as CommandLineToArgvW reads it back.
std::wstring quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
        return arg;
    std::wstring q = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        q.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        q += c;
    }
    q.append(slashes * 2, L'\\');
    return q + L"\"";
}

// Runs `args` in `cwd` without a console window and passes each line it prints to `log`.
// Returns the exit code; `output` (if given) collects the text.
DWORD run(const std::vector<std::wstring>& args, const fs::path& cwd, const Log& log, std::wstring* output = nullptr) {
    std::wstring line;
    for (const std::wstring& a : args)
        line += (line.empty() ? L"" : L" ") + quote(a);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0))
        fail(L"cannot start " + args[0]);
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write;
    si.hStdError = write;
    PROCESS_INFORMATION pi = {};
    BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  cwd.c_str(), &si, &pi);
    CloseHandle(write);
    if (!started) {
        CloseHandle(read);
        fail(L"cannot start " + line);
    }
    auto emit = [&](std::string_view bytes) {
        std::wstring text = widen(bytes);
        while (!text.empty() && text.back() == L'\r')
            text.pop_back();
        log(L"  " + text);
        if (output)
            *output += text + L"\n";
    };
    std::string pending;
    char buf[4096];
    DWORD n;
    while (ReadFile(read, buf, sizeof(buf), &n, nullptr) && n) {
        pending.append(buf, n);
        for (size_t end; (end = pending.find('\n')) != std::string::npos; pending.erase(0, end + 1))
            emit(std::string_view(pending).substr(0, end));
    }
    if (!pending.empty())
        emit(pending);
    CloseHandle(read);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

fs::path search_path(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(std::size(buf)), buf, nullptr);
    return n && n < std::size(buf) ? fs::path(std::wstring(buf, n)) : fs::path();
}

// The Python that runs prepare\run.py: the release's own (prepare\python, the python.org embeddable
// package), else the player's: the py launcher, else python.exe (not the Store's WindowsApps alias).
std::vector<std::wstring> python_command(const fs::path& prepare) {
    std::error_code ec;
    if (fs::path own = prepare / L"python" / L"python.exe"; fs::is_regular_file(own, ec))
        return {own.wstring()};
    if (fs::path py = search_path(L"py.exe"); !py.empty())
        return {py.wstring(), L"-3"};
    if (fs::path python = search_path(L"python.exe");
        !python.empty() && lower(python.wstring()).find(L"\\windowsapps\\") == std::wstring::npos)
        return {python.wstring()};
    fail(L"Setup needs Python 3.10 or newer to prepare the game. Install it from python.org, then run setup again.");
}

// --- paths.ini ----------------------------------------------------------------------------------

std::wstring strip(const std::wstring& s) {
    constexpr wchar_t ws[] = L" \t\r\n\"";
    size_t a = s.find_first_not_of(ws);
    if (a == std::wstring::npos)
        return {};
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

void write_paths(const fs::path& ini, const fs::path& game, const fs::path& save) {
    write_text(ini, "; Written by theatrhythm-setup.exe. Run it again to change these folders.\r\n"
                    "; game: your dump (eboot.bin, sce_module, sce_sys, Media). It is only ever read.\r\n"
                    "; save: your saves, and theatrhythm.log in its log folder.\r\n"
                    "; Shader caches are shared by every copy of the game: %LOCALAPPDATA%\\Theatrhythm Final Bar Line\r\n"
                    "; (cache=<folder> moves them).\r\n"
                    "[paths]\r\n"
                    "game=" + narrow(game.wstring()) + "\r\n"
                    "save=" + narrow(save.wstring()) + "\r\n");
}

} // namespace

fs::path exe_folder() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

bool same_path(const fs::path& a, const fs::path& b) {
    return lower(clean(a).wstring()) == lower(clean(b).wstring());
}

fs::path game_folder(const fs::path& path) {
    std::error_code ec;
    fs::path p = clean(path);
    return fs::is_regular_file(p, ec) ? p.parent_path() : p;
}

std::optional<Paths> read_paths(const fs::path& root) {
    std::error_code ec;
    fs::path ini = root / L"paths.ini";
    if (!fs::is_regular_file(ini, ec))
        return std::nullopt;
    std::string text = read_text(ini);
    if (text.rfind("\xEF\xBB\xBF", 0) == 0)
        text.erase(0, 3);
    Paths paths;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        std::wstring line = widen(std::string_view(text).substr(start, end == std::string::npos ? end : end - start));
        start = end == std::string::npos ? text.size() : end + 1;
        size_t eq = line.find(L'=');
        std::wstring key = lower(strip(line.substr(0, eq)));
        if (eq == std::wstring::npos || key.empty() || key[0] == L';' || key[0] == L'#')
            continue;
        std::wstring value = strip(line.substr(eq + 1));
        if (value.empty())
            continue;
        if (key == L"game")
            paths.game = clean(root / value);
        else if (key == L"save")
            paths.save = clean(root / value);
    }
    if (paths.game.empty() || paths.save.empty())
        return std::nullopt;
    return paths;
}

fs::path default_save() {
    return exe_folder() / L"save";
}

bool ready(const fs::path& root) {
    std::error_code ec;
    fs::path stamp = root / L"game" / L"release.txt", release = root / L"release.txt";
    try {
        return read_paths(root) && fs::is_regular_file(root / kGameExe, ec) && fs::is_regular_file(stamp, ec) &&
               fs::is_regular_file(release, ec) && read_text(stamp) == read_text(release);
    } catch (const Error&) {
        return false;
    }
}

bool has_saves(const fs::path& save) {
    std::error_code ec;
    fs::path data = save / L"savedata";
    return fs::is_directory(data, ec) && !fs::is_empty(data, ec);
}

fs::path install(const Options& opt, const Log& log) {
    fs::path root = clean(opt.root), game = game_folder(opt.game), save = clean(opt.save);
    fs::path prepare = root / L"prepare", image = root / L"game", exe = root / kGameExe;
    if (inside(save, game))
        fail(L"Choose a save folder outside the game folder. Setup never writes into the game folder.");
    if (inside(root, game) || inside(game, root))
        fail(L"Keep the game folder and this folder apart (neither inside the other).");

    std::error_code ec;
    std::vector<std::wstring> missing;
    for (const wchar_t* f : kGameFiles)
        if (!fs::is_regular_file(game / f, ec))
            missing.push_back(f);
    if (!missing.empty())
        fail(L"This folder isn't a decrypted Theatrhythm Final Bar Line dump:\n" + game.wstring() +
             L"\n\nMissing: " + join(missing));
    for (const fs::path& f : {exe, prepare / L"run.py", root / L"release.txt"})
        if (!fs::is_regular_file(f, ec))
            fail(f.wstring() + L" is missing." + kIncomplete);
    std::vector<std::wstring> python = python_command(prepare);

    log(L"Preparing the game from " + game.wstring());
    fs::remove(image / L"release.txt", ec); // the image is this release's only once it is complete
    std::vector<std::wstring> command = python;
    command.insert(command.end(), {(prepare / L"run.py").wstring(), L"prepare", L"--game", game.wstring(),
                                   L"--out", image.wstring()});
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    if (DWORD code = run(command, prepare, log))
        fail(L"Preparing the game failed (exit code " + std::to_wstring(code) + L"). The log says why.");

    // On a CPU without some instruction sets the game uses, the sites that use them are rewritten
    // (run.py's play does this at every start; here once).
    std::wstring cpu;
    DWORD code = run({exe.wstring(), L"--cpu-features"}, root, log, &cpu);
    if (code == 20)
        fail(L"This PC's CPU cannot run the game:\n" + cpu);
    if (code)
        fail(L"The game does not start (exit code " + std::to_wstring(code) + L")." + kIncomplete);
    if (cpu.find(L"CPU: missing") != std::wstring::npos) {
        command = python;
        command.insert(command.end(), {(prepare / L"scripts" / L"isa_rewrite.py").wstring(),
                                       (image / L"boot-linked.bin").wstring(), L"--out", image.wstring()});
        if (run(command, prepare, log))
            fail(L"This CPU lacks instructions the game uses, and rewriting them failed. That step needs the "
                 L"Python package iced-x86 (pip install iced-x86); the log says more.");
    } else {
        fs::remove(image / L"isa-rewrite.bin", ec);
    }
    write_text(image / L"release.txt", read_text(root / L"release.txt"));
    log(L"prepared " + image.wstring());

    fs::path settings = root / L"settings.ini";
    if (!fs::exists(settings, ec)) {
        if (!CopyFileW((prepare / L"settings.example.ini").c_str(), settings.c_str(), TRUE))
            fail(L"cannot write " + settings.wstring());
        log(L"wrote    settings.ini");
    }

    fs::create_directories(save, ec);
    if (!fs::is_directory(save, ec))
        fail(L"Cannot create the save folder " + save.wstring());
    if (opt.copy_saves_from && !same_path(*opt.copy_saves_from, save)) {
        for (const wchar_t* f : kSaveFolders) {
            fs::path from = *opt.copy_saves_from / f;
            if (!fs::is_directory(from, ec))
                continue;
            fs::copy(from, save / f, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
            if (ec)
                fail(L"Cannot copy " + from.wstring() + L" to " + save.wstring() + L": " + widen(ec.message()));
        }
        log(L"copied   saves from " + opt.copy_saves_from->wstring());
    }
    write_paths(root / L"paths.ini", game, save);
    log(L"wrote    paths.ini (game " + game.wstring() + L", save " + save.wstring() + L")");
    return exe;
}

void create_shortcut(const fs::path& target, const fs::path& lnk) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr;
    IPersistFile* file = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(hr)) {
        link->SetPath(target.c_str());
        link->SetWorkingDirectory(target.parent_path().c_str());
        link->SetIconLocation(target.c_str(), 0);
        link->SetDescription(L"Theatrhythm Final Bar Line");
        hr = link->QueryInterface(IID_PPV_ARGS(&file));
    }
    if (SUCCEEDED(hr))
        hr = file->Save(lnk.c_str(), TRUE);
    if (file)
        file->Release();
    if (link)
        link->Release();
    if (SUCCEEDED(init))
        CoUninitialize();
    if (FAILED(hr)) {
        wchar_t code[16];
        swprintf(code, std::size(code), L"%08X", static_cast<unsigned>(hr));
        fail(L"Cannot create the shortcut " + lnk.wstring() + L" (error 0x" + code + L")");
    }
}

fs::path desktop_folder() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p))) {
        CoTaskMemFree(p);
        fail(L"Cannot find the Desktop folder");
    }
    fs::path desktop = p;
    CoTaskMemFree(p);
    return desktop;
}

} // namespace setup
