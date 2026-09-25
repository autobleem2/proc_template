// fsutil: the portable filesystem helpers declared in fsutil.h. Every path in and out is UTF-8, '/' separated.
//
// A typical processor run, put together:
//
//     vector<string> args = utf8Args(argc, argv);                  // "Zażółć/ゲーム.pkg" arrives intact
//     for (const string &pkg : findFiles(gamesDir, {".pkg"})) {    // every .pkg under Games/, sorted
//         string dest = join(dirName(pkg), stem(pkg));             // "Games/Crash.pkg" -> "Games/Crash"
//         if (freeSpace(gamesDir) < fileSize(pkg)) { ... }         // not enough room on the stick
//         makeDirs(dest);
//         string out = join(dest, "Crash.bin");
//         FILE *f = openFile(out + ".part", "wb");                 // write to <name>.part ...
//         ...
//         fclose(f);
//         renameFile(out + ".part", out);                          // ... and rename it when it is complete
//         removeFile(pkg);                                         // the source goes last
//     }

#include "fsutil.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

using namespace std;

namespace fsutil {

//******************
// the protocol's output
//******************
// Every line is flushed at once: the launcher shows progress as it comes, and a pipe is block-buffered.
//
//     say("#Processing " + fileName(pkg));                          // a stage
//     say(to_string(i + 1) + "/" + to_string(files.size()));        // a counter
//     say("#WARN - " + why);                                        // a warning, the run goes on
void say(const string &line) {
    fputs(line.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// The percent of a long job, from bytes: give it the total once, then the bytes done after every piece. It
// prints only when the whole number moves, so a 4 GB file in 1 MB pieces is 101 lines, not 4000.
//
//     Percent percent{item->size};                        // 100% = the item's size
//     for (uint64_t done = 0; done < item->size; ) {
//         size_t n = package.read(*item, done, buf.get(), chunk);
//         if (n == 0) break;                              // a read error
//         ...
//         done += n;
//         percent(done);                                  // "0" .. "100"
//     }
void Percent::operator()(uint64_t done) {
    int percent = total == 0 ? 100 : static_cast<int>(done * 100 / total);
    if (percent != last) {
        last = percent;
        say(to_string(percent));
    }
}

static double monotonicSeconds() {
    return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
}

Heartbeat::Heartbeat(double seconds, const string &line) : intervalSeconds(seconds), last(monotonicSeconds()) {
    (void)line; // nothing said yet - the constructor just starts the clock, matching one already-said line
}

// Says `line` only once `intervalSeconds` have passed since the last time this actually said something
// (never on the very first call right after construction - that already counts as the first "said"
// moment). Cheap to call every loop iteration; it is just a clock check until the interval is up.
void Heartbeat::ping(const string &line) {
    double now = monotonicSeconds();
    if (now - last < intervalSeconds)
        return;
    last = now;
    say(line);
}

// A processor protocol environment variable, UTF-8 - empty when unset:
//
//     string platform = env("AB_PLATFORM");          // "psc", "rpi", "pcusb", "windows", ...
string env(const string &name) {
#ifdef _WIN32
    wstring wname = wide(name);
    DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (n == 0)
        return string();
    wstring w(n, L'\0');
    n = GetEnvironmentVariableW(wname.c_str(), &w[0], n);
    return narrow(wstring(w.data(), n));
#else
    const char *v = getenv(name.c_str());
    return v ? string(v) : string();
#endif
}

//******************
// UTF-8
//******************
#ifdef _WIN32
// UTF-8 -> UTF-16, for the Windows *W API calls. Only needed when you call Windows yourself:
//
//     SetFileAttributesW(wide(path).c_str(), FILE_ATTRIBUTE_HIDDEN);
wstring wide(const string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

// UTF-16 -> UTF-8, for what a Windows *W API call gives back:
//
//     WIN32_FIND_DATAW data; ...
//     string name = narrow(data.cFileName);
string narrow(const wstring &w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

// The command line as UTF-8; [0] is the program itself. Use it instead of argv in main():
//
//     vector<string> args = utf8Args(argc, argv);
//     args.erase(args.begin());                  // just the arguments
//     // psnext.exe --start --games "E:/Games/ゲーム"  ->  {"--start", "--games", "E:/Games/ゲーム"}
vector<string> utf8Args(int argc, char **argv) {
    vector<string> out;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wargv) {
        for (int i = 0; i < n; ++i)
            out.push_back(narrow(wargv[i]));
        LocalFree(wargv);
        return out;
    }
#endif
    out.assign(argv, argv + argc);
    return out;
}

//******************
// memory
//******************
// A byte buffer that frees itself - no delete[], also on an early return. Its contents start undefined (not
// zeroed, so a big one costs nothing up front). nullptr when the memory is not there, so check it: the console
// has little RAM and a package can be gigabytes.
//
//     Buffer buf = makeBuffer(1024 * 1024);
//     if (!buf) { say("#ERROR - out of memory"); return 1; }
//     size_t n = fread(buf.get(), 1, 1024 * 1024, in);   // buf.get() is the uint8_t*
//     buf[0] = 0x7f;                                      // or index it directly
//     ...                                                 // freed here, at the end of the scope
//
//     vector<Buffer> blocks;                              // it can be moved into containers or returned
//     blocks.push_back(makeBuffer(65536));
Buffer makeBuffer(size_t size) {
    return Buffer(new (nothrow) uint8_t[size]);
}

//******************
// names
//******************
// ASCII letters to lower case; UTF-8 bytes are left as they are:
//
//     lower("Crash (USA).PKG")  ->  "crash (usa).pkg"
//     lower("ŻÓŁW.PKG")         ->  "ŻÓŁw.pkg"
string lower(string s) {
    for (char &c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// Case-insensitive; give the suffix in lower case:
//
//     endsWith("Crash.PKG", ".pkg")  ->  true
//     endsWith("Crash.pkg", ".PKG")  ->  false  (the suffix must be lower case)
bool endsWith(const string &s, const string &suffix) {
    return s.size() >= suffix.size() && lower(s.substr(s.size() - suffix.size())) == suffix;
}

// The last part of a path ('/' or '\'):
//
//     fileName("Games/Crash/Crash.pkg")  ->  "Crash.pkg"
//     fileName("Crash.pkg")              ->  "Crash.pkg"
string fileName(const string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? path : path.substr(slash + 1);
}

// Everything before the last part:
//
//     dirName("Games/Crash/Crash.pkg")  ->  "Games/Crash"
//     dirName("Crash.pkg")              ->  "."
string dirName(const string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == string::npos ? "." : path.substr(0, slash);
}

// The file name without its last extension:
//
//     stem("Games/Crash (USA).pkg")  ->  "Crash (USA)"
//     stem("Games/game.tar.gz")      ->  "game.tar"
//     stem("Games/.hidden")          ->  ".hidden"
string stem(const string &path) {
    string name = fileName(path);
    size_t dot = name.find_last_of('.');
    return dot == string::npos || dot == 0 ? name : name.substr(0, dot);
}

// The last extension, with its dot, in lower case; "" when there is none:
//
//     extension("Games/Crash.PKG")  ->  ".pkg"
//     extension("Games/README")     ->  ""
//     extension("Games/.hidden")    ->  ""
string extension(const string &path) {
    string name = fileName(path);
    size_t dot = name.find_last_of('.');
    return dot == string::npos || dot == 0 ? "" : lower(name.substr(dot));
}

// Trailing slashes off (a lone "/" is kept):
//
//     withoutSlash("E:/Games/")  ->  "E:/Games"
//     withoutSlash("/")          ->  "/"
string withoutSlash(string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
        path.pop_back();
    return path;
}

// A folder and a name, with exactly one '/' between them:
//
//     join("E:/Games/", "Crash")  ->  "E:/Games/Crash"
//     join("", "Crash")           ->  "Crash"
string join(const string &dir, const string &name) {
    if (dir.empty())
        return name;
    return withoutSlash(dir) + "/" + name;
}

//******************
// the disk
//******************
// One level of a folder, unsorted, without "." and "..":
//
//     for (const Entry &e : listDir(gamesDir))
//         if (e.isDir)
//             say("#Game folder " + e.name);
vector<Entry> listDir(const string &dir) {
    vector<Entry> out;
#ifdef _WIN32
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW(wide(join(dir, "*")).c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        string name = narrow(data.cFileName);
        if (name != "." && name != "..")
            out.push_back({name, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileW(h, &data));
    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());
    if (!d)
        return out;
    while (dirent *e = readdir(d)) {
        string name = e->d_name;
        if (name == "." || name == "..")
            continue;
        out.push_back({name, isDir(join(dir, name))});
    }
    closedir(d);
#endif
    return out;
}

// Is anything there, a file or a folder:
//
//     if (exists(target))
//         say("#WARN - " + fileName(target) + " is already there");
bool exists(const string &path) {
#ifdef _WIN32
    return GetFileAttributesW(wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

// Is it a folder:
//
//     if (!isDir(gamesDir)) { say("#ERROR - no Games folder"); return 1; }
bool isDir(const string &path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(wide(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// Is it a file (it exists and it is not a folder):
//
//     bool unpacked = isFile(join(dest, stem(pkg) + ".cue"));
bool isFile(const string &path) {
    return exists(path) && !isDir(path);
}

// The size in bytes, >4 GB included; 0 when the file is not there:
//
//     uint64_t total = 0;
//     for (const string &f : files)
//         total += fileSize(f);                  // for a "0".."100" percent over all of them
uint64_t fileSize(const string &path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wide(path).c_str(), GetFileExInfoStandard, &data))
        return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
#endif
}

// Bytes free on the drive / filesystem the path is on; UINT64_MAX when it cannot be told, so check that first:
//
//     uint64_t free = freeSpace(gamesDir);
//     if (free != UINT64_MAX && needed > free)
//         say("#WARN - not enough space: " + to_string(needed / (1024 * 1024)) + " MB needed");
uint64_t freeSpace(const string &path) {
#ifdef _WIN32
    ULARGE_INTEGER avail;
    if (GetDiskFreeSpaceExW(wide(path).c_str(), &avail, nullptr, nullptr))
        return avail.QuadPart;
#else
    struct statvfs fs;
    if (statvfs(path.c_str(), &fs) == 0)
        return static_cast<uint64_t>(fs.f_bavail) * fs.f_frsize;
#endif
    return UINT64_MAX;
}

// The OS temp directory, '/' separated and without a trailing slash - a location, not a promise it exists:
//
//     string scratch = join(tempDir(), "psnext_" + package.titleId());
//     makeDirs(scratch);
string tempDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, buf);
    string path = len > 0 && len < MAX_PATH ? narrow(wstring(buf, len)) : string(".");
    for (char &c : path)
        if (c == '\\')
            c = '/';
    return withoutSlash(path);
#else
    const char *dir = getenv("TMPDIR");
    if (!dir) dir = getenv("TMP");
    if (!dir) dir = getenv("TEMP");
    if (!dir) dir = "/tmp";
    return withoutSlash(dir);
#endif
}

// A folder with every missing parent; true when it is there afterwards (already being there is fine):
//
//     makeDirs(join(gamesDir, "Crash (USA)/extra"));   // creates "Crash (USA)" and "extra"
bool makeDirs(const string &path) {
    if (path.empty() || isDir(path))
        return true;
    size_t slash = path.find_last_of("/\\");
    if (slash != string::npos && slash > 0 && !makeDirs(path.substr(0, slash)))
        return false;
#ifdef _WIN32
    return CreateDirectoryW(wide(path).c_str(), nullptr) || isDir(path);
#else
    return mkdir(path.c_str(), 0777) == 0 || isDir(path);
#endif
}

// Deletes one file (not a folder). On Windows a file that is still open cannot be deleted - fclose it first:
//
//     if (!removeFile(pkg))
//         say("#WARN - " + fileName(pkg) + " was processed but could not be deleted");
bool removeFile(const string &path) {
#ifdef _WIN32
    return DeleteFileW(wide(path).c_str()) != 0;
#else
    return unlink(path.c_str()) == 0;
#endif
}

// Deletes one folder, only when it is empty:
//
//     removeFile(join(dest, "tmp.bin"));
//     removeDir(dest);                           // false if anything is still inside
bool removeDir(const string &path) {
#ifdef _WIN32
    return RemoveDirectoryW(wide(path).c_str()) != 0;
#else
    return rmdir(path.c_str()) == 0;
#endif
}

// Deletes a folder and everything inside it. True when it is gone afterwards (already gone is fine);
// false if anything under it could not be removed (e.g. a file still open elsewhere):
//
//     removeDirRecursive(scratch); // clean up a temp working folder when done, however it filled up
bool removeDirRecursive(const string &path) {
    if (!isDir(path))
        return !exists(path);
    bool ok = true;
    for (const Entry &e : listDir(path)) {
        string child = join(path, e.name);
        ok &= e.isDir ? removeDirRecursive(child) : removeFile(child);
    }
    return removeDir(path) && ok;
}

// Moves / renames a file; false when the target is already there (never overwrites):
//
//     renameFile(out + ".part", out);            // the finished file appears in one step
bool renameFile(const string &from, const string &to) {
    if (exists(to)) // rename() replaces a file on Linux, MoveFile does not: the same answer everywhere
        return false;
#ifdef _WIN32
    return MoveFileW(wide(from).c_str(), wide(to).c_str()) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

// fopen() with a UTF-8 path, the same modes ("rb", "wb", "ab", ...); close it with fclose():
//
//     FILE *in = openFile(pkg, "rb");
//     if (!in) { say("#WARN - cannot read " + fileName(pkg)); continue; }
//     unsigned char buf[65536];
//     size_t n;
//     while ((n = fread(buf, 1, sizeof(buf), in)) > 0) { ... }
//     fclose(in);
FILE *openFile(const string &path, const char *mode) {
#ifdef _WIN32
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

// A whole buffer to a file, the safe way: written to <path>.part, closed, then renamed to <path>. A run stopped
// halfway leaves only a .part (removed on the next try), never a half file under the real name. An existing
// <path> is not overwritten. On failure the .part is removed and `why` says what went wrong:
//
//     Buffer buf = makeBuffer(size);
//     ...                                                 // fill it
//     string why;
//     if (!saveFile(join(dest, "game.bin"), buf.get(), size, why))
//         say("#WARN - " + why);
//
// A file too big for one buffer: the same steps by hand - openFile(part, "wb"), fwrite chunk by chunk,
// check fclose's result, renameFile(part, path).
bool saveFile(const string &path, const uint8_t *data, size_t size, string &why) {
    if (exists(path)) { // never overwrite (renameFile would refuse anyway)
        why = fileName(path) + " is already there";
        return false;
    }
    string part = path + ".part";
    removeFile(part); // left by a run that was stopped: ours, start again

    FILE *out = openFile(part, "wb");
    if (!out) {
        why = "could not create " + fileName(part);
        return false;
    }
    bool ok = fwrite(data, 1, size, out) == size;
    ok = fclose(out) == 0 && ok; // always closed; a full disk can show up only here
    if (!ok) {
        removeFile(part);
        why = "could not write " + fileName(part) + " (is the stick full?)";
        return false;
    }
    if (!renameFile(part, path)) {
        removeFile(part);
        why = "could not rename " + fileName(part);
        return false;
    }
    return true;
}

// A file of any size to disk, piece by piece: `fill` is asked for the next piece until it gives 0 bytes. Only
// one `chunkSize` buffer is in memory, however big the file. Written through <path>.part and renamed at the
// end, exactly like the buffer saveFile above; a stopped run leaves only the .part. The callback is where the
// real work happens (read + decrypt the package), and the natural place for the percent line.
//
//     FILE *in = openFile(pkg, "rb");
//     uint64_t total = fileSize(pkg), done = 0;
//     int lastPercent = -1;
//     string why;
//     bool ok = saveFile(join(dest, "game.bin"), [&](uint8_t *buf, size_t capacity, size_t &filled) {
//         filled = fread(buf, 1, capacity, in);           // 0 at the end of the input: the file is complete
//         if (filled == 0 && ferror(in)) {
//             why = "could not read " + fileName(pkg);   // what saveFile reports
//             return false;                               // stop: the .part is removed
//         }
//         // ... decrypt buf[0..filled) in place ...
//         done += filled;
//         int percent = total ? static_cast<int>(done * 100 / total) : 100;
//         if (percent != lastPercent) {                   // only when it moves
//             lastPercent = percent;
//             say(to_string(percent));
//         }
//         return true;
//     }, why);
//     fclose(in);
//     if (!ok)
//         say("#WARN - " + why);
bool saveFile(const string &path, const FillFn &fill, string &why, size_t chunkSize) {
    if (exists(path)) { // never overwrite (renameFile would refuse anyway)
        why = fileName(path) + " is already there";
        return false;
    }
    Buffer buf = makeBuffer(chunkSize);
    if (!buf) {
        why = "out of memory for a " + to_string(chunkSize / 1024) + " KB buffer";
        return false;
    }
    string part = path + ".part";
    removeFile(part); // left by a run that was stopped: ours, start again

    FILE *out = openFile(part, "wb");
    if (!out) {
        why = "could not create " + fileName(part);
        return false;
    }
    why.clear();
    bool ok = true, filledOk = true;
    for (;;) {
        size_t filled = 0;
        if (!fill(buf.get(), chunkSize, filled) || filled > chunkSize) {
            filledOk = false;
            break;
        }
        if (filled == 0)
            break; // the end
        if (fwrite(buf.get(), 1, filled, out) != filled) {
            ok = false;
            break;
        }
    }
    ok = fclose(out) == 0 && ok; // always closed; a full disk can show up only here
    if (!filledOk || !ok) {
        removeFile(part);
        if (why.empty())
            why = filledOk ? "could not write " + fileName(part) + " (is the stick full?)"
                           : "could not produce the data for " + fileName(path);
        return false;
    }
    if (!renameFile(part, path)) {
        removeFile(part);
        why = "could not rename " + fileName(part);
        return false;
    }
    return true;
}

// A file copied through the streaming saveFile, with the percent of its size said on the way ("0".."100").
// The source is closed before this returns, so the caller can delete it right after:
//
//     string why;
//     if (copyWithProgress(pkg, join(dest, stem(pkg) + ".pkg"), why))
//         removeFile(pkg);
//     else
//         say("#WARN - " + fileName(pkg) + ": " + why);
//
// The same shape with a Package item instead of a file (read() decrypts; 100% = the item's size):
//
//     uint64_t done = 0;
//     Percent percent{item->size};
//     bool ok = saveFile(target, [&](uint8_t *buf, size_t capacity, size_t &filled) {
//         uint64_t left = item->size - done;
//         filled = package.read(*item, done, buf, left < capacity ? static_cast<size_t>(left) : capacity);
//         if (filled == 0 && left > 0) { why = "could not read the package"; return false; }
//         done += filled;
//         percent(done);
//         return true;
//     }, why);
bool copyWithProgress(const string &from, const string &to, string &why) {
    FILE *in = openFile(from, "rb");
    if (!in) {
        why = "could not open " + fileName(from);
        return false;
    }
    uint64_t done = 0;
    Percent percent{fileSize(from)};
    bool ok = saveFile(
        to,
        [&](uint8_t *buf, size_t capacity, size_t &filled) {
            filled = fread(buf, 1, capacity, in);
            if (filled == 0 && ferror(in)) {
                why = "could not read " + fileName(from);
                return false;
            }
            done += filled;
            percent(done);
            return true;
        },
        why);
    fclose(in);
    return ok;
}

//******************
// findFiles
//******************
static void findInto(const string &dir, const vector<string> &extensions, bool recursive, vector<string> &out) {
    for (const Entry &e : listDir(dir)) {
        if (e.name.empty() || e.name[0] == '.' || e.name[0] == '!')
            continue;
        string path = join(dir, e.name);
        if (e.isDir) {
            if (recursive)
                findInto(path, extensions, recursive, out);
            continue;
        }
        bool wanted = extensions.empty();
        for (const string &ext : extensions) {
            if (endsWith(e.name, ext)) {
                wanted = true;
                break;
            }
        }
        if (wanted)
            out.push_back(path);
    }
}

// Every file under a folder with one of the extensions (lower case, with the dot), full paths, sorted.
// Names starting with '.' or '!' (!SaveStates, !MemCards) are skipped, files and folders alike:
//
//     findFiles("E:/Games", {".pkg"})              // every .pkg (and .PKG) in the whole tree
//     findFiles("E:/Games", {".cue", ".bin"})      // either extension
//     findFiles("E:/Games", {".pkg"}, false)       // Games/ itself only, not its subfolders
//     findFiles("E:/Games/Crash", {})              // every file in the game's folder
vector<string> findFiles(const string &dir, const vector<string> &extensions, bool recursive) {
    vector<string> out;
    findInto(withoutSlash(dir), extensions, recursive, out);
    sort(out.begin(), out.end());
    return out;
}

} // namespace fsutil
