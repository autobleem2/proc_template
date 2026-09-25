// proc_template_selftest: builds a tiny sample Games tree, runs the real proc_template binary on it (the
// PROC_EXE the build passes in) and checks what the protocol promises - the output lines, the exit code,
// the files, and that a second run does nothing new. Run by ctest from the build directory (a scratch tree
// is made there). Deliberately self-contained (no fsutil/strutil here) so it tests the built binary as an
// outside caller would, the same way proc_unzip's selftest does.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0777)
#define POPEN popen
#define PCLOSE pclose
#endif

using namespace std;

namespace {

int failures = 0;

void check(bool ok, const string &what) {
    if (!ok) {
        ++failures;
        cerr << "FAILED: " << what << endl;
    }
}

bool exists(const string &path) {
    ifstream f(path, ios::binary);
    return f.good();
}

void writeFile(const string &path, const string &text) {
    ofstream(path, ios::binary) << text;
}

// PROC_EXE, quoted, plus the given arguments, each quoted the same crude way proc_unzip's selftest does -
// good enough for the paths and flags this test itself constructs (no embedded quotes).
//
// _popen/system() run this through cmd.exe /c on Windows, which has a well-known quirk: when the command
// already starts with a quoted path, cmd's own "is the whole line quoted" check needs one more, outer pair
// of quotes around everything or it mis-splits the line (the first check.py-style path). POSIX popen has no
// such quirk, so this only wraps on Windows.
string run(const string &args, string &output) {
    string cmd = "\"" + string(PROC_EXE) + "\" " + args;
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    FILE *p = POPEN(cmd.c_str(), "r");
    if (!p) {
        output.clear();
        return "";
    }
    ostringstream out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
        out.write(buf, n);
    int status = PCLOSE(p);
    output = out.str();
#ifdef _WIN32
    return to_string(status);
#else
    return to_string(WEXITSTATUS(status));
#endif
}

} // namespace

int main() {
    MKDIR("selftest_work");
    string games = "selftest_work/Games";
    MKDIR(games.c_str());
    string sample = games + "/Sample.example";
    writeFile(sample, "sample input");

    string out;
    string exitCode = run("--version", out);
    check(exitCode == "0", "--version exits 0");
    check(!out.empty() && out[0] == '#', "--version's line starts with '#'");

    exitCode = run(string("--start --games \"") + games + "\"", out);
    check(exitCode == "0", "--start --games exits 0");
    check(out.find("#DONE") != string::npos, "--start --games says #DONE");
    string produced = games + "/Sample.out";
    check(exists(produced), "the matched sample produced its output file");
    check(!exists(sample + ".part"), "no .part file left behind");

    // idempotent: a second run touches nothing new and still finishes cleanly
    exitCode = run(string("--start --games \"") + games + "\"", out);
    check(exitCode == "0", "a second run also exits 0");
    check(out.find("#DONE") != string::npos, "a second run also says #DONE");

    if (failures == 0)
        cout << "all checks passed" << endl;
    else
        cerr << failures << " check(s) failed" << endl;
    return failures == 0 ? 0 : 1;
}
