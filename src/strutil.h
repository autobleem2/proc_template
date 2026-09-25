// strutil: small string helpers that come up in every processor and are not about the filesystem (that's
// fsutil.h - lower()/endsWith()/join() live there since path names are strings too, but are file-shaped).
// Everything here is byte-oriented and UTF-8 safe by construction: it never inspects anything above 0x7F,
// so multi-byte sequences pass through untouched.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace strutil {

// ASCII whitespace (space, tab, \r, \n) trimmed from both ends; nothing else is touched:
//
//     trim("  Crash Bandicoot \r\n")  ->  "Crash Bandicoot"
std::string trim(const std::string &s);

// `s` cut at every occurrence of `delim` (a single character, not a set); empty pieces are kept, so
// splitting always returns pieces.size() == count(delim) + 1:
//
//     split("EP0102-NPEF00199_00-GRESIDENTE000ITA", '-')  ->  {"EP0102", "NPEF00199_00", "GRESIDENTE000ITA"}
//     split("", ',')                                       ->  {""}
std::vector<std::string> split(const std::string &s, char delim);

// The pieces put back together with `delim` between them (not before the first or after the last) - the
// inverse of split() when nothing was trimmed out of the pieces:
//
//     join({"Crash", "Spyro"}, ", ")  ->  "Crash, Spyro"
//     join({}, ", ")                  ->  ""
std::string join(const std::vector<std::string> &parts, const std::string &delim);

// Case-insensitive prefix check, the counterpart to fsutil::endsWith(); `prefix` in lower case:
//
//     startsWith(system, "arcade - ")     // "Arcade - MAME 2003-Plus" -> true
bool startsWith(const std::string &s, const std::string &prefix);

// Every occurrence of `from` replaced by `to`, left to right, non-overlapping (an empty `from` returns `s`
// unchanged rather than looping forever):
//
//     replaceAll(cueText, "\r\n", "\n")           // normalize a .cue's line endings
std::string replaceAll(std::string s, const std::string &from, const std::string &to);

// A byte count as a person reads it, base-1024, one decimal place from MB up ("512 bytes", "3.4 KB",
// "1.2 GB"); for a "#WARN - not enough space" line, not for anything parsed back:
//
//     say("#WARN - not enough space: " + humanSize(needed) + " needed, " + humanSize(free) + " free");
std::string humanSize(uint64_t bytes);

// A base-10 unsigned integer from the whole string (not just a prefix of it) - false, `out` untouched, on
// anything else: empty, a sign, a decimal point, trailing junk, or a value that overflows uint64_t. Safer
// than atoi/strtoul for a command-line argument or an .ini value, which should be rejected outright rather
// than silently read as 0 or truncated:
//
//     uint64_t timeout;
//     if (!tryParseUInt(value, timeout)) { say("#ERROR - bad Timeout= value: " + value); return 1; }
bool tryParseUInt(const std::string &s, uint64_t &out);

} // namespace strutil
