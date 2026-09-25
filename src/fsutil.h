// A little portable filesystem for a processor: every path is a UTF-8 std::string, '/' separated, on every
// system. Windows gets the wide (UTF-16) API underneath, so a game named in Japanese or Polish works there too.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fsutil {

//******************
// the protocol's output
//******************
// one line to the launcher, flushed at once: "#<stage>", "n/m", "0".."100", "#WARN - <text>", "#DONE", ...
void say(const std::string &line);

// The percent line: call it with the bytes done so far, it say()s 0..100 of `total`, only when the number
// changes (not a line per chunk).
struct Percent {
    uint64_t total;
    int last = -1;
    void operator()(uint64_t done);
};

// The launcher's rule: say something at least every processor.ini `Timeout=` seconds, or it assumes you
// hung and kills you. A step with no natural progress to report (scanning a big tree, waiting on an
// external tool) can stall past that in silence; ping() catches it - call it wherever you loop or poll, it
// only actually say()s once `intervalSeconds` has passed since the last time (construction counts as one):
//
//     fsutil::Heartbeat beat{60};                    // Timeout=120 in processor.ini: ping well under half
//     for (const string &pkg : files) {
//         beat.ping();                                // resets the clock only when it actually says something
//         ...
//     }
struct Heartbeat {
    double intervalSeconds;
    double last; // seconds since an unspecified epoch (monotonic on one run); set by the constructor
    explicit Heartbeat(double seconds, const std::string &line = "#Working...");
    void ping(const std::string &line = "#Working...");
};

// The processor protocol's environment (the launcher's docs/scanner-processors-plan.md): set for every run,
// UTF-8 in and out. Empty when the variable is not set - true of every one of these only under a very old
// launcher, so check for that if it matters:
//
//     string tmp = env("AB_TMP");                    // a scratch folder off the stick - small files only
//     if (tmp.empty()) tmp = tempDir();               // older launcher: fall back to the OS temp dir
//
// AB_PROCESSOR_PROTOCOL=1, AB_ROOT, AB_GAMES_DIR, AB_ROMS_DIR, AB_RDB_DIR, AB_TMP, AB_PLATFORM,
// AB_PLATFORM_KEYS, AB_LANGUAGE, AB_VERSION.
std::string env(const std::string &name);

//******************
// UTF-8
//******************
#ifdef _WIN32
std::wstring wide(const std::string &s);   // UTF-8 -> UTF-16
std::string narrow(const std::wstring &w); // UTF-16 -> UTF-8
#endif

// the program's arguments as UTF-8. On Windows argv is in the ANSI code page and loses whatever the code page
// cannot show; this reads the real command line instead. Elsewhere it is argv as it is.
std::vector<std::string> utf8Args(int argc, char **argv);

//******************
// memory
//******************
// a byte buffer, freed by itself when it goes out of scope
using Buffer = std::unique_ptr<uint8_t[]>;

// `size` bytes, not zeroed; nullptr when there is not that much memory (no exception)
Buffer makeBuffer(size_t size);

//******************
// names (no disk access)
//******************
std::string lower(std::string s);                               // ASCII only; UTF-8 bytes above 0x7F kept
bool endsWith(const std::string &s, const std::string &suffix); // case-insensitive; suffix in lower case
std::string fileName(const std::string &path);                  // "a/b/Crash.cue" -> "Crash.cue"
std::string dirName(const std::string &path);                   // "a/b/Crash.cue" -> "a/b"
std::string stem(const std::string &path);                      // "a/b/Crash.cue" -> "Crash"
std::string extension(const std::string &path);                 // "a/b/Crash.CUE" -> ".cue" (lower case)
std::string withoutSlash(std::string path);                     // "a/b/" -> "a/b"
std::string join(const std::string &dir, const std::string &name);

//******************
// the disk
//******************
struct Entry {
    std::string name;
    bool isDir;
};

std::vector<Entry> listDir(const std::string &dir); // one level, "." and ".." left out, unsorted
bool exists(const std::string &path);
bool isDir(const std::string &path);
bool isFile(const std::string &path);
uint64_t fileSize(const std::string &path);  // 0 when it cannot be told
uint64_t freeSpace(const std::string &path); // bytes free where `path` is; UINT64_MAX when it cannot be told
std::string tempDir(); // the OS temp directory, no trailing slash; a location, not a promise it exists
bool makeDirs(const std::string &path);      // with every missing parent
bool removeFile(const std::string &path);
bool removeDir(const std::string &path);                         // an empty one
bool removeDirRecursive(const std::string &path);                // everything inside it too
bool renameFile(const std::string &from, const std::string &to); // never over an existing file
FILE *openFile(const std::string &path, const char *mode);

// `size` bytes of `data` to `path`, through `path.part` renamed when complete; never over an existing file.
// False with `why` set on any failure (the .part is removed again).
bool saveFile(const std::string &path, const uint8_t *data, size_t size, std::string &why);

// Fills `buf` (room for `capacity` bytes) with the next piece of the file and sets `filled`; 0 = the end.
// Returns false to stop with a failure.
using FillFn = std::function<bool(uint8_t *buf, size_t capacity, size_t &filled)>;

// The same as above for a file too big for one buffer: `fill` is called until it gives 0 bytes, each piece
// written as it comes, through one `chunkSize` buffer. When `fill` returns false the .part is removed and
// saveFile returns false; `why` is what `fill` put there (capture it), or a general reason.
bool saveFile(const std::string &path, const FillFn &fill, std::string &why, size_t chunkSize = 1024 * 1024);

// `from` copied to `to` through `to.part`, say()ing the percent of `from`'s size; never over an existing file.
bool copyWithProgress(const std::string &from, const std::string &to, std::string &why);

// Every file under `dir` whose name ends in one of `extensions` (lower case, with the dot: {".pkg", ".cue"};
// empty = every file), as full paths, sorted. Folders and files starting with '.' or '!' (!SaveStates,
// !MemCards) are skipped. `recursive` false: `dir` itself only.
std::vector<std::string> findFiles(const std::string &dir, const std::vector<std::string> &extensions,
                                   bool recursive = true);

} // namespace fsutil
