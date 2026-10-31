// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The install itself, in the release folder the setup exe sits in (tools/package.py): prepares the game
// image from the player's dump with prepare\run.py, and writes paths.ini (the dump and the save folder,
// read by src/launch.c). The dump is only ever read.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace setup {

// Why an install stopped; the text is meant for the user.
struct Error {
    std::wstring message;
};

struct Options {
    std::filesystem::path root; // the release folder: the game exe, prepare\, and what the setup adds
    std::filesystem::path game; // the dump folder or its eboot.bin
    std::filesystem::path save; // saves and the launch log (run.py's --user)
    std::optional<std::filesystem::path> copy_saves_from; // the old save folder, whose saves go to `save`
};

// The folders a release folder's paths.ini names.
struct Paths {
    std::filesystem::path game, save;
};

// Save data in a save folder (what the runtime keeps there apart from the log).
constexpr const wchar_t* kSaveFolders[] = {L"savedata", L"download0"};

using Log = std::function<void(const std::wstring&)>;

constexpr wchar_t kShortcutName[] = L"Theatrhythm Final Bar Line.lnk";

// The folder this exe is in.
std::filesystem::path exe_folder();
// The dump folder for a path to it or to its eboot.bin (absolute, normalized).
std::filesystem::path game_folder(const std::filesystem::path& path);
// What `root`\paths.ini names, if it names both folders.
std::optional<Paths> read_paths(const std::filesystem::path& root);
// save\ in the release folder: a release is never zipped with one, so unpacking a newer release over
// the folder keeps the saves.
std::filesystem::path default_save();
// True when `root` is set up for its release: paths.ini names both folders and game\ was prepared
// for this release.txt (what src/launch.c checks before it starts the game).
bool ready(const std::filesystem::path& root);
// True when `save` holds game saves (a savedata folder with something in it).
bool has_saves(const std::filesystem::path& save);
bool same_path(const std::filesystem::path& a, const std::filesystem::path& b);

// Sets up opt.root, reporting each step through `log`. Throws Error. Returns the game exe's path.
std::filesystem::path install(const Options& opt, const Log& log);

// Writes a shortcut at `lnk` that starts `target` in its own folder. Throws Error.
void create_shortcut(const std::filesystem::path& target, const std::filesystem::path& lnk);
// The user's Desktop folder. Throws Error.
std::filesystem::path desktop_folder();

} // namespace setup
