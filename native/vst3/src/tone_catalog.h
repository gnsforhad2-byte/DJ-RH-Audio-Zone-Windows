#pragma once

#include "tone_files.h"
#include "tone_names.h"
#include "tone_roots.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace DJRH {

namespace fs = std::filesystem;

struct ToneEntry {
    fs::path filePath;
    std::string displayName;
    int rootMidiNote {60};
    bool builtIn {false};
};

inline fs::path externalToneDirectory() {
#ifdef _WIN32
    const wchar_t* appData = _wgetenv(L"APPDATA");
    if (!appData || !*appData)
        return {};
    return fs::path(appData) / L"DJ RH Audio Zone" / L"Tones";
#else
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData)
        return fs::path(appData) / "DJ RH Audio Zone" / "Tones";

    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return {};

    return fs::path(home) / ".config" / "DJ RH Audio Zone" / "Tones";
#endif
}

inline std::string lowerCase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );
    return value;
}

inline bool isSupportedToneFile(const fs::path& path) {
    const std::string ext = lowerCase(path.extension().string());
    return ext == ".wav" || ext == ".mp3";
}

inline std::vector<ToneEntry> buildToneCatalog(
    const fs::path& builtInDirectory
) {
    std::vector<ToneEntry> catalog;
    catalog.reserve(ToneFiles.size() + 128);

    // Keep all existing built-in tones in their original order.
    for (std::size_t i = 0; i < ToneFiles.size(); ++i) {
        ToneEntry entry;
        entry.filePath = builtInDirectory / ToneFiles[i];
        entry.displayName = ToneNames[i];
        entry.rootMidiNote = ToneRootMidi[i];
        entry.builtIn = true;
        catalog.push_back(std::move(entry));
    }

    // Create the external tone folder if it does not exist.
    const fs::path externalDirectory = externalToneDirectory();
    if (externalDirectory.empty())
        return catalog;

    std::error_code ec;
    fs::create_directories(externalDirectory, ec);
    if (ec)
        return catalog;

    std::vector<ToneEntry> externalTones;

    fs::directory_iterator it(externalDirectory, ec);
    const fs::directory_iterator end;

    while (!ec && it != end) {
        const fs::directory_entry& file = *it;

        std::error_code fileError;
        if (file.is_regular_file(fileError) &&
            !fileError &&
            isSupportedToneFile(file.path())) {
            ToneEntry entry;
            entry.filePath = file.path();
            entry.displayName = file.path().stem().u8string();
            entry.rootMidiNote = 60;
            entry.builtIn = false;
            externalTones.push_back(std::move(entry));
        }

        it.increment(ec);
    }

    // Stable alphabetical order for external files.
    std::sort(
        externalTones.begin(), externalTones.end(),
        [](const ToneEntry& a, const ToneEntry& b) {
            const std::string left = lowerCase(a.displayName);
            const std::string right = lowerCase(b.displayName);

            if (left != right)
                return left < right;

            return a.displayName < b.displayName;
        }
    );

    catalog.insert(
        catalog.end(),
        externalTones.begin(),
        externalTones.end()
    );

    return catalog;
}

} // namespace DJRH
