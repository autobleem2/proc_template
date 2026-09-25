// proc_template: a skeleton AutoBleem scanner processor. Copy this repository, rename it proc_<yours>, and
// fill in processOne() below - everything around it is the protocol plumbing every processor needs, already
// wired up and working. The launcher's docs/scanner-processors-plan.md is the full protocol spec; this file
// (and fsutil.h/strutil.h, the two headers it leans on) covers what most processors actually touch.
//
// The protocol, in short:
//
//     proc --version                      one line: "#<Name> V<version> - <description>", exit 0
//     proc --ismine --rom <file> --system "<name>"     exit 0 = mine, 1 = not mine (Kinds=rom only)
//     proc --start --games <dir>          the whole Games tree (Kinds=games-folder)
//     proc --start --rom <file> --system "<name>"      one ROM (Kinds=rom)
//
// While it works it prints, one flushed line at a time: "#<stage>" for each new stage, "0".."100" for a
// percent, "n/m" for a counter, "#WARN - <text>" for a recoverable problem (the run goes on), and at the
// end "#DONE" (exit 0) or "#ERROR - <text>" (exit 1, nothing usable came of the run). fsutil::say() is
// exactly that "one flushed line" - use it instead of printf/cout for anything the launcher should see.
//
// The rules every processor keeps (fsutil's saveFile/copyWithProgress already do the first three for you):
//   - atomic: write to "<name>.part", then rename - a run stopped halfway never leaves a half-written file
//     under the real name.
//   - never overwrite: an existing target file means skip it (with a #WARN), not clobber it.
//   - idempotent: a second run on your own output does nothing. Usually free if you just check the output
//     exists before doing the work.
//   - stay inside what you were given (the games dir, or the one file/system you were passed).
//   - say something at least every processor.ini Timeout= seconds - fsutil::Heartbeat below.
//
// Check yours before you publish it with the launcher's tools/proc_check.py (see ci/proc_check.sh here for
// how CI drives it, and README.md for the command by hand).

#include "fsutil.h"
#include "strutil.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace std;
using namespace fsutil;

namespace {

const char *Name = "Template";
const char *Version = "1.0.0";
const char *Description = "A skeleton scanner processor - copy me";

// The one real file extension this processor claims (must match package/processor.ini's Match=). A
// games-folder processor can list more than one; a rom processor checks --system in ismine() instead.
const vector<string> Extensions = {".example"};

//******************
// the actual work - this is what you replace
//******************
// Whatever this processor does to one matched file. `file` is a full path under the games dir; return true
// on success (even "nothing to do, already done" counts). On failure, put a short reason in `why` and
// return false - the caller turns that into a #WARN and keeps going with the next file.
//
// A typical shape: build the output path, bail out early if it is already there (idempotent), do the work
// into memory or piece by piece, write it with fsutil::saveFile/copyWithProgress (atomic, never overwrites),
// then remove the source if this processor is meant to consume it (processor.ini's Modifies=true).
bool processOne(const string &file, string &why) {
    string dest = join(dirName(file), stem(file) + ".out"); // example: Game.example -> Game.out
    if (exists(dest))
        return true; // already done - idempotent, not an error

    // ... real work goes here: read `file`, produce the output, report progress with fsutil::Percent for
    // anything large enough to take a moment ...

    Buffer data = makeBuffer(4);
    if (!data) {
        why = "out of memory";
        return false;
    }
    data[0] = 'T';
    data[1] = 'O';
    data[2] = 'D';
    data[3] = 'O';
    return saveFile(dest, data.get(), 4, why);
}

//******************
// --start --games <dir>
//******************
// Every matched file under the games dir, one at a time, in the shape every games-folder processor uses:
// say the stage and the counter, do the work, report a warning and move on if it fails, #DONE (or #ERROR
// when nothing usable came of the whole run) at the end.
int startGames(const string &gamesDir) {
    vector<string> files = findFiles(gamesDir, Extensions);
    Heartbeat beat{60}; // well under half of processor.ini's Timeout=, even if a single file is slow
    size_t failed = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        const string &file = files[i];
        say("#Processing " + fileName(file));
        say(to_string(i + 1) + "/" + to_string(files.size()));
        beat.ping();

        string why;
        if (!processOne(file, why)) {
            say("#WARN - " + fileName(file) + ": " + why);
            ++failed;
        }
    }
    if (failed > 0 && failed == files.size() && !files.empty()) {
        say("#ERROR - every file failed");
        return 1;
    }
    if (failed > 0)
        say("#WARN - " + to_string(failed) + " of " + to_string(files.size()) + " file(s) failed");
    say("#DONE");
    return 0;
}

//******************
// --ismine --rom <file> --system "<name>"     (Kinds=rom only - delete this and startGames's Extensions
// check above if this processor is games-folder only, like the default processor.ini here)
//******************
// Asked about one file the launcher is not sure belongs to this processor (a rom processor is asked this
// before --start --rom; a games-folder processor is never asked at all). Say yes only when you are sure -
// answering "mine" for a file another processor should have handled means that one never runs on it.
bool isMine(const string &file, const string &system) {
    (void)system; // most rom processors only care about the extension; keep this if you need to branch on it
    for (const string &ext : Extensions)
        if (endsWith(file, ext))
            return true;
    return false;
}

int usage() {
    fprintf(stderr, "usage: proc_template --version | --ismine --rom <file> --system <name> | "
                     "--start --games <dir> | --start --rom <file> --system <name>\n");
    return 2;
}

} // namespace

int main(int argc, char **argv) {
    vector<string> args = utf8Args(argc, argv);
    args.erase(args.begin()); // the program itself
    auto value = [&args](const string &flag) {
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == flag)
                return args[i + 1];
        return string();
    };
    auto has = [&args](const string &flag) {
        for (const string &a : args)
            if (a == flag)
                return true;
        return false;
    };

    const string title = string(Name) + " V" + Version;
    if (has("--version")) {
        say("#" + title + " - " + Description);
        return 0;
    }
    if (has("--ismine"))
        return isMine(value("--rom"), value("--system")) ? 0 : 1;
    if (has("--start")) {
        say("#Starting - " + title);
        if (has("--games"))
            return startGames(withoutSlash(value("--games")));
        if (has("--rom")) {
            string why;
            bool ok = processOne(value("--rom"), why);
            if (!ok)
                say("#ERROR - " + why);
            else
                say("#DONE");
            return ok ? 0 : 1;
        }
        say("#DONE"); // a kind this processor does not implement: nothing to do
        return 0;
    }
    return usage();
}
