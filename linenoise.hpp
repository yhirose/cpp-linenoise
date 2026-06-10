/*
 *  linenoise.hpp -- Multi-platform C++ header-only linenoise library.
 *
 *  A C++17 implementation derived from antirez/linenoise. The editing
 *  engine is a function-by-function port that tracks upstream (last
 *  synced with commit a473823, 2026-05-02); original to this project are:
 *   - Full Unicode extended grapheme cluster support (UAX #29) and
 *     display width handling (UAX #11 East Asian Width + emoji),
 *     replacing upstream's heuristic UTF-8 handling.
 *   - Native Windows support via the Windows console virtual terminal
 *     mode (Windows 10+ / Windows Terminal). No ANSI emulation layer.
 *   - A thin C++ API on top of the original linenoise editing engine.
 *
 *  All credits and commendations have to go to the authors of the
 *  original library: https://github.com/antirez/linenoise
 *
 * ------------------------------------------------------------------------
 *
 *  Copyright (c) 2015-2026 yhirose
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice, this
 *     list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LINENOISE_HPP
#define LINENOISE_HPP

#ifndef _WIN32
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#endif /* _WIN32 */

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace linenoise {

/* Editing limits. Can be overridden before including this header. */
#ifndef LINENOISE_DEFAULT_HISTORY_MAX_LEN
#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#endif
#ifndef LINENOISE_MAX_LINE
#define LINENOISE_MAX_LINE (1024 * 1024)
#endif
#ifndef LINENOISE_PASTE_FOLD_THRESHOLD
#define LINENOISE_PASTE_FOLD_THRESHOLD 200 /* Min bytes to fold a one-line paste. */
#endif

/* ========================== Unicode support =============================== *
 * Extended grapheme cluster segmentation (UAX #29) and display width
 * (wcwidth-style, based on East Asian Width and emoji properties), driven
 * by compact range tables generated from the Unicode Character Database
 * by scripts/gen_unicode_tables.py.
 */
namespace unicode {

/* Grapheme_Cluster_Break classes (bits 0-3 of CharProp::val). */
enum class GCB : uint8_t {
    Other = 0,
    CR = 1,
    LF = 2,
    Control = 3,
    Extend = 4,
    ZWJ = 5,
    RegionalIndicator = 6,
    Prepend = 7,
    SpacingMark = 8,
    L = 9,
    V = 10,
    T = 11,
    LV = 12,
    LVT = 13,
};

/* Indic_Conjunct_Break classes (bits 5-6 of CharProp::val). */
enum class InCB : uint8_t { None = 0, Consonant = 1, Extend = 2, Linker = 3 };

/* Width classes (bits 7-8 of CharProp::val). */
enum class WidthClass : uint8_t { Default = 0, Zero = 1, Wide = 2 };

inline constexpr uint16_t ExtPictFlag = 1 << 4;

struct CharProp {
    uint32_t lo;
    uint32_t hi;
    uint16_t val;
};

// {{{ unicode-tables (generated by scripts/gen_unicode_tables.py, Unicode 17.0.0)
// Unicode 17.0.0. 987 ranges.
inline const CharProp char_props[] = {
    {0x0,0x9,0x3}, {0xA,0xA,0x2}, {0xB,0xC,0x3}, {0xD,0xD,0x1},
    {0xE,0x1F,0x3}, {0x7F,0x9F,0x3}, {0xA9,0xA9,0x10}, {0xAD,0xAD,0x3},
    {0xAE,0xAE,0x10}, {0x300,0x36F,0xC4}, {0x483,0x489,0xC4}, {0x591,0x5BD,0xC4},
    {0x5BF,0x5BF,0xC4}, {0x5C1,0x5C2,0xC4}, {0x5C4,0x5C5,0xC4}, {0x5C7,0x5C7,0xC4},
    {0x600,0x605,0x87}, {0x610,0x61A,0xC4}, {0x61C,0x61C,0x83}, {0x64B,0x65F,0xC4},
    {0x670,0x670,0xC4}, {0x6D6,0x6DC,0xC4}, {0x6DD,0x6DD,0x87}, {0x6DF,0x6E4,0xC4},
    {0x6E7,0x6E8,0xC4}, {0x6EA,0x6ED,0xC4}, {0x70F,0x70F,0x87}, {0x711,0x711,0xC4},
    {0x730,0x74A,0xC4}, {0x7A6,0x7B0,0xC4}, {0x7EB,0x7F3,0xC4}, {0x7FD,0x7FD,0xC4},
    {0x816,0x819,0xC4}, {0x81B,0x823,0xC4}, {0x825,0x827,0xC4}, {0x829,0x82D,0xC4},
    {0x859,0x85B,0xC4}, {0x890,0x891,0x87}, {0x897,0x89F,0xC4}, {0x8CA,0x8E1,0xC4},
    {0x8E2,0x8E2,0x87}, {0x8E3,0x902,0xC4}, {0x903,0x903,0x8}, {0x915,0x939,0x20},
    {0x93A,0x93A,0xC4}, {0x93B,0x93B,0x8}, {0x93C,0x93C,0xC4}, {0x93E,0x940,0x8},
    {0x941,0x948,0xC4}, {0x949,0x94C,0x8}, {0x94D,0x94D,0xE4}, {0x94E,0x94F,0x8},
    {0x951,0x957,0xC4}, {0x958,0x95F,0x20}, {0x962,0x963,0xC4}, {0x978,0x97F,0x20},
    {0x981,0x981,0xC4}, {0x982,0x983,0x8}, {0x995,0x9A8,0x20}, {0x9AA,0x9B0,0x20},
    {0x9B2,0x9B2,0x20}, {0x9B6,0x9B9,0x20}, {0x9BC,0x9BC,0xC4}, {0x9BE,0x9BE,0x44},
    {0x9BF,0x9C0,0x8}, {0x9C1,0x9C4,0xC4}, {0x9C7,0x9C8,0x8}, {0x9CB,0x9CC,0x8},
    {0x9CD,0x9CD,0xE4}, {0x9D7,0x9D7,0x44}, {0x9DC,0x9DD,0x20}, {0x9DF,0x9DF,0x20},
    {0x9E2,0x9E3,0xC4}, {0x9F0,0x9F1,0x20}, {0x9FE,0x9FE,0xC4}, {0xA01,0xA02,0xC4},
    {0xA03,0xA03,0x8}, {0xA3C,0xA3C,0xC4}, {0xA3E,0xA40,0x8}, {0xA41,0xA42,0xC4},
    {0xA47,0xA48,0xC4}, {0xA4B,0xA4D,0xC4}, {0xA51,0xA51,0xC4}, {0xA70,0xA71,0xC4},
    {0xA75,0xA75,0xC4}, {0xA81,0xA82,0xC4}, {0xA83,0xA83,0x8}, {0xA95,0xAA8,0x20},
    {0xAAA,0xAB0,0x20}, {0xAB2,0xAB3,0x20}, {0xAB5,0xAB9,0x20}, {0xABC,0xABC,0xC4},
    {0xABE,0xAC0,0x8}, {0xAC1,0xAC5,0xC4}, {0xAC7,0xAC8,0xC4}, {0xAC9,0xAC9,0x8},
    {0xACB,0xACC,0x8}, {0xACD,0xACD,0xE4}, {0xAE2,0xAE3,0xC4}, {0xAF9,0xAF9,0x20},
    {0xAFA,0xAFF,0xC4}, {0xB01,0xB01,0xC4}, {0xB02,0xB03,0x8}, {0xB15,0xB28,0x20},
    {0xB2A,0xB30,0x20}, {0xB32,0xB33,0x20}, {0xB35,0xB39,0x20}, {0xB3C,0xB3C,0xC4},
    {0xB3E,0xB3E,0x44}, {0xB3F,0xB3F,0xC4}, {0xB40,0xB40,0x8}, {0xB41,0xB44,0xC4},
    {0xB47,0xB48,0x8}, {0xB4B,0xB4C,0x8}, {0xB4D,0xB4D,0xE4}, {0xB55,0xB56,0xC4},
    {0xB57,0xB57,0x44}, {0xB5C,0xB5D,0x20}, {0xB5F,0xB5F,0x20}, {0xB62,0xB63,0xC4},
    {0xB71,0xB71,0x20}, {0xB82,0xB82,0xC4}, {0xBBE,0xBBE,0x44}, {0xBBF,0xBBF,0x8},
    {0xBC0,0xBC0,0xC4}, {0xBC1,0xBC2,0x8}, {0xBC6,0xBC8,0x8}, {0xBCA,0xBCC,0x8},
    {0xBCD,0xBCD,0xC4}, {0xBD7,0xBD7,0x44}, {0xC00,0xC00,0xC4}, {0xC01,0xC03,0x8},
    {0xC04,0xC04,0xC4}, {0xC15,0xC28,0x20}, {0xC2A,0xC39,0x20}, {0xC3C,0xC3C,0xC4},
    {0xC3E,0xC40,0xC4}, {0xC41,0xC44,0x8}, {0xC46,0xC48,0xC4}, {0xC4A,0xC4C,0xC4},
    {0xC4D,0xC4D,0xE4}, {0xC55,0xC56,0xC4}, {0xC58,0xC5A,0x20}, {0xC62,0xC63,0xC4},
    {0xC81,0xC81,0xC4}, {0xC82,0xC83,0x8}, {0xCBC,0xCBC,0xC4}, {0xCBE,0xCBE,0x8},
    {0xCBF,0xCBF,0xC4}, {0xCC0,0xCC0,0x44}, {0xCC1,0xCC1,0x8}, {0xCC2,0xCC2,0x44},
    {0xCC3,0xCC4,0x8}, {0xCC6,0xCC6,0xC4}, {0xCC7,0xCC8,0x44}, {0xCCA,0xCCB,0x44},
    {0xCCC,0xCCD,0xC4}, {0xCD5,0xCD6,0x44}, {0xCE2,0xCE3,0xC4}, {0xCF3,0xCF3,0x8},
    {0xD00,0xD01,0xC4}, {0xD02,0xD03,0x8}, {0xD15,0xD3A,0x20}, {0xD3B,0xD3C,0xC4},
    {0xD3E,0xD3E,0x44}, {0xD3F,0xD40,0x8}, {0xD41,0xD44,0xC4}, {0xD46,0xD48,0x8},
    {0xD4A,0xD4C,0x8}, {0xD4D,0xD4D,0xE4}, {0xD4E,0xD4E,0x7}, {0xD57,0xD57,0x44},
    {0xD62,0xD63,0xC4}, {0xD81,0xD81,0xC4}, {0xD82,0xD83,0x8}, {0xDCA,0xDCA,0xC4},
    {0xDCF,0xDCF,0x44}, {0xDD0,0xDD1,0x8}, {0xDD2,0xDD4,0xC4}, {0xDD6,0xDD6,0xC4},
    {0xDD8,0xDDE,0x8}, {0xDDF,0xDDF,0x44}, {0xDF2,0xDF3,0x8}, {0xE31,0xE31,0xC4},
    {0xE33,0xE33,0x8}, {0xE34,0xE3A,0xC4}, {0xE47,0xE4E,0xC4}, {0xEB1,0xEB1,0xC4},
    {0xEB3,0xEB3,0x8}, {0xEB4,0xEBC,0xC4}, {0xEC8,0xECE,0xC4}, {0xF18,0xF19,0xC4},
    {0xF35,0xF35,0xC4}, {0xF37,0xF37,0xC4}, {0xF39,0xF39,0xC4}, {0xF3E,0xF3F,0x8},
    {0xF71,0xF7E,0xC4}, {0xF7F,0xF7F,0x8}, {0xF80,0xF84,0xC4}, {0xF86,0xF87,0xC4},
    {0xF8D,0xF97,0xC4}, {0xF99,0xFBC,0xC4}, {0xFC6,0xFC6,0xC4}, {0x1000,0x102A,0x20},
    {0x102D,0x1030,0xC4}, {0x1031,0x1031,0x8}, {0x1032,0x1037,0xC4}, {0x1039,0x1039,0xE4},
    {0x103A,0x103A,0xC4}, {0x103B,0x103C,0x8}, {0x103D,0x103E,0xC4}, {0x103F,0x103F,0x20},
    {0x1050,0x1055,0x20}, {0x1056,0x1057,0x8}, {0x1058,0x1059,0xC4}, {0x105A,0x105D,0x20},
    {0x105E,0x1060,0xC4}, {0x1061,0x1061,0x20}, {0x1065,0x1066,0x20}, {0x106E,0x1070,0x20},
    {0x1071,0x1074,0xC4}, {0x1075,0x1081,0x20}, {0x1082,0x1082,0xC4}, {0x1084,0x1084,0x8},
    {0x1085,0x1086,0xC4}, {0x108D,0x108D,0xC4}, {0x108E,0x108E,0x20}, {0x109D,0x109D,0xC4},
    {0x1100,0x115E,0x109}, {0x115F,0x115F,0x89}, {0x1160,0x11A7,0x8A}, {0x11A8,0x11FF,0x8B},
    {0x135D,0x135F,0xC4}, {0x1712,0x1714,0xC4}, {0x1715,0x1715,0x44}, {0x1732,0x1733,0xC4},
    {0x1734,0x1734,0x44}, {0x1752,0x1753,0xC4}, {0x1772,0x1773,0xC4}, {0x1780,0x17B3,0x20},
    {0x17B4,0x17B5,0xC4}, {0x17B6,0x17B6,0x8}, {0x17B7,0x17BD,0xC4}, {0x17BE,0x17C5,0x8},
    {0x17C6,0x17C6,0xC4}, {0x17C7,0x17C8,0x8}, {0x17C9,0x17D1,0xC4}, {0x17D2,0x17D2,0xE4},
    {0x17D3,0x17D3,0xC4}, {0x17DD,0x17DD,0xC4}, {0x180B,0x180D,0xC4}, {0x180E,0x180E,0x83},
    {0x180F,0x180F,0xC4}, {0x1885,0x1886,0xC4}, {0x18A9,0x18A9,0xC4}, {0x1920,0x1922,0xC4},
    {0x1923,0x1926,0x8}, {0x1927,0x1928,0xC4}, {0x1929,0x192B,0x8}, {0x1930,0x1931,0x8},
    {0x1932,0x1932,0xC4}, {0x1933,0x1938,0x8}, {0x1939,0x193B,0xC4}, {0x1A17,0x1A18,0xC4},
    {0x1A19,0x1A1A,0x8}, {0x1A1B,0x1A1B,0xC4}, {0x1A20,0x1A54,0x20}, {0x1A55,0x1A55,0x8},
    {0x1A56,0x1A56,0xC4}, {0x1A57,0x1A57,0x8}, {0x1A58,0x1A5E,0xC4}, {0x1A60,0x1A60,0xE4},
    {0x1A62,0x1A62,0xC4}, {0x1A65,0x1A6C,0xC4}, {0x1A6D,0x1A72,0x8}, {0x1A73,0x1A7C,0xC4},
    {0x1A7F,0x1A7F,0xC4}, {0x1AB0,0x1ADD,0xC4}, {0x1AE0,0x1AEB,0xC4}, {0x1B00,0x1B03,0xC4},
    {0x1B04,0x1B04,0x8}, {0x1B0B,0x1B0C,0x20}, {0x1B13,0x1B33,0x20}, {0x1B34,0x1B34,0xC4},
    {0x1B35,0x1B35,0x44}, {0x1B36,0x1B3A,0xC4}, {0x1B3B,0x1B3B,0x44}, {0x1B3C,0x1B3C,0xC4},
    {0x1B3D,0x1B3D,0x44}, {0x1B3E,0x1B41,0x8}, {0x1B42,0x1B42,0xC4}, {0x1B43,0x1B43,0x44},
    {0x1B44,0x1B44,0x64}, {0x1B45,0x1B4C,0x20}, {0x1B6B,0x1B73,0xC4}, {0x1B80,0x1B81,0xC4},
    {0x1B82,0x1B82,0x8}, {0x1B83,0x1BA0,0x20}, {0x1BA1,0x1BA1,0x8}, {0x1BA2,0x1BA5,0xC4},
    {0x1BA6,0x1BA7,0x8}, {0x1BA8,0x1BA9,0xC4}, {0x1BAA,0x1BAA,0x44}, {0x1BAB,0x1BAB,0xE4},
    {0x1BAC,0x1BAD,0xC4}, {0x1BAE,0x1BAF,0x20}, {0x1BBB,0x1BBD,0x20}, {0x1BE6,0x1BE6,0xC4},
    {0x1BE7,0x1BE7,0x8}, {0x1BE8,0x1BE9,0xC4}, {0x1BEA,0x1BEC,0x8}, {0x1BED,0x1BED,0xC4},
    {0x1BEE,0x1BEE,0x8}, {0x1BEF,0x1BF1,0xC4}, {0x1BF2,0x1BF3,0x44}, {0x1C24,0x1C2B,0x8},
    {0x1C2C,0x1C33,0xC4}, {0x1C34,0x1C35,0x8}, {0x1C36,0x1C37,0xC4}, {0x1CD0,0x1CD2,0xC4},
    {0x1CD4,0x1CE0,0xC4}, {0x1CE1,0x1CE1,0x8}, {0x1CE2,0x1CE8,0xC4}, {0x1CED,0x1CED,0xC4},
    {0x1CF4,0x1CF4,0xC4}, {0x1CF7,0x1CF7,0x8}, {0x1CF8,0x1CF9,0xC4}, {0x1DC0,0x1DFF,0xC4},
    {0x200B,0x200B,0x83}, {0x200C,0x200C,0x84}, {0x200D,0x200D,0xC5}, {0x200E,0x200F,0x83},
    {0x2028,0x2029,0x3}, {0x202A,0x202E,0x83}, {0x203C,0x203C,0x10}, {0x2049,0x2049,0x10},
    {0x2060,0x206F,0x83}, {0x20D0,0x20F0,0xC4}, {0x2122,0x2122,0x10}, {0x2139,0x2139,0x10},
    {0x2194,0x2199,0x10}, {0x21A9,0x21AA,0x10}, {0x231A,0x231B,0x110}, {0x2328,0x2328,0x10},
    {0x2329,0x232A,0x100}, {0x23CF,0x23CF,0x10}, {0x23E9,0x23EC,0x110}, {0x23ED,0x23EF,0x10},
    {0x23F0,0x23F0,0x110}, {0x23F1,0x23F2,0x10}, {0x23F3,0x23F3,0x110}, {0x23F8,0x23FA,0x10},
    {0x24C2,0x24C2,0x10}, {0x25AA,0x25AB,0x10}, {0x25B6,0x25B6,0x10}, {0x25C0,0x25C0,0x10},
    {0x25FB,0x25FC,0x10}, {0x25FD,0x25FE,0x110}, {0x2600,0x2604,0x10}, {0x260E,0x260E,0x10},
    {0x2611,0x2611,0x10}, {0x2614,0x2615,0x110}, {0x2618,0x2618,0x10}, {0x261D,0x261D,0x10},
    {0x2620,0x2620,0x10}, {0x2622,0x2623,0x10}, {0x2626,0x2626,0x10}, {0x262A,0x262A,0x10},
    {0x262E,0x262F,0x10}, {0x2630,0x2637,0x100}, {0x2638,0x263A,0x10}, {0x2640,0x2640,0x10},
    {0x2642,0x2642,0x10}, {0x2648,0x2653,0x110}, {0x265F,0x2660,0x10}, {0x2663,0x2663,0x10},
    {0x2665,0x2666,0x10}, {0x2668,0x2668,0x10}, {0x267B,0x267B,0x10}, {0x267E,0x267E,0x10},
    {0x267F,0x267F,0x110}, {0x268A,0x268F,0x100}, {0x2692,0x2692,0x10}, {0x2693,0x2693,0x110},
    {0x2694,0x2697,0x10}, {0x2699,0x2699,0x10}, {0x269B,0x269C,0x10}, {0x26A0,0x26A0,0x10},
    {0x26A1,0x26A1,0x110}, {0x26A7,0x26A7,0x10}, {0x26AA,0x26AB,0x110}, {0x26B0,0x26B1,0x10},
    {0x26BD,0x26BE,0x110}, {0x26C4,0x26C5,0x110}, {0x26C8,0x26C8,0x10}, {0x26CE,0x26CE,0x110},
    {0x26CF,0x26CF,0x10}, {0x26D1,0x26D1,0x10}, {0x26D3,0x26D3,0x10}, {0x26D4,0x26D4,0x110},
    {0x26E9,0x26E9,0x10}, {0x26EA,0x26EA,0x110}, {0x26F0,0x26F1,0x10}, {0x26F2,0x26F3,0x110},
    {0x26F4,0x26F4,0x10}, {0x26F5,0x26F5,0x110}, {0x26F7,0x26F9,0x10}, {0x26FA,0x26FA,0x110},
    {0x26FD,0x26FD,0x110}, {0x2702,0x2702,0x10}, {0x2705,0x2705,0x110}, {0x2708,0x2709,0x10},
    {0x270A,0x270B,0x110}, {0x270C,0x270D,0x10}, {0x270F,0x270F,0x10}, {0x2712,0x2712,0x10},
    {0x2714,0x2714,0x10}, {0x2716,0x2716,0x10}, {0x271D,0x271D,0x10}, {0x2721,0x2721,0x10},
    {0x2728,0x2728,0x110}, {0x2733,0x2734,0x10}, {0x2744,0x2744,0x10}, {0x2747,0x2747,0x10},
    {0x274C,0x274C,0x110}, {0x274E,0x274E,0x110}, {0x2753,0x2755,0x110}, {0x2757,0x2757,0x110},
    {0x2763,0x2764,0x10}, {0x2795,0x2797,0x110}, {0x27A1,0x27A1,0x10}, {0x27B0,0x27B0,0x110},
    {0x27BF,0x27BF,0x110}, {0x2934,0x2935,0x10}, {0x2B05,0x2B07,0x10}, {0x2B1B,0x2B1C,0x110},
    {0x2B50,0x2B50,0x110}, {0x2B55,0x2B55,0x110}, {0x2CEF,0x2CF1,0xC4}, {0x2D7F,0x2D7F,0xC4},
    {0x2DE0,0x2DFF,0xC4}, {0x2E80,0x2E99,0x100}, {0x2E9B,0x2EF3,0x100}, {0x2F00,0x2FD5,0x100},
    {0x2FF0,0x3029,0x100}, {0x302A,0x302D,0xC4}, {0x302E,0x302F,0x144}, {0x3030,0x3030,0x110},
    {0x3031,0x303C,0x100}, {0x303D,0x303D,0x110}, {0x303E,0x303E,0x100}, {0x3041,0x3096,0x100},
    {0x3099,0x309A,0xC4}, {0x309B,0x30FF,0x100}, {0x3105,0x312F,0x100}, {0x3131,0x3163,0x100},
    {0x3164,0x3164,0x80}, {0x3165,0x318E,0x100}, {0x3190,0x31E5,0x100}, {0x31EF,0x321E,0x100},
    {0x3220,0x3247,0x100}, {0x3250,0x3296,0x100}, {0x3297,0x3297,0x110}, {0x3298,0x3298,0x100},
    {0x3299,0x3299,0x110}, {0x329A,0xA48C,0x100}, {0xA490,0xA4C6,0x100}, {0xA66F,0xA672,0xC4},
    {0xA674,0xA67D,0xC4}, {0xA69E,0xA69F,0xC4}, {0xA6F0,0xA6F1,0xC4}, {0xA802,0xA802,0xC4},
    {0xA806,0xA806,0xC4}, {0xA80B,0xA80B,0xC4}, {0xA823,0xA824,0x8}, {0xA825,0xA826,0xC4},
    {0xA827,0xA827,0x8}, {0xA82C,0xA82C,0xC4}, {0xA880,0xA881,0x8}, {0xA8B4,0xA8C3,0x8},
    {0xA8C4,0xA8C5,0xC4}, {0xA8E0,0xA8F1,0xC4}, {0xA8FF,0xA8FF,0xC4}, {0xA926,0xA92D,0xC4},
    {0xA947,0xA951,0xC4}, {0xA952,0xA952,0x8}, {0xA953,0xA953,0x44}, {0xA960,0xA97C,0x109},
    {0xA980,0xA982,0xC4}, {0xA983,0xA983,0x8}, {0xA989,0xA98B,0x20}, {0xA98F,0xA9B2,0x20},
    {0xA9B3,0xA9B3,0xC4}, {0xA9B4,0xA9B5,0x8}, {0xA9B6,0xA9B9,0xC4}, {0xA9BA,0xA9BB,0x8},
    {0xA9BC,0xA9BD,0xC4}, {0xA9BE,0xA9BF,0x8}, {0xA9C0,0xA9C0,0x64}, {0xA9E0,0xA9E4,0x20},
    {0xA9E5,0xA9E5,0xC4}, {0xA9E7,0xA9EF,0x20}, {0xA9FA,0xA9FE,0x20}, {0xAA29,0xAA2E,0xC4},
    {0xAA2F,0xAA30,0x8}, {0xAA31,0xAA32,0xC4}, {0xAA33,0xAA34,0x8}, {0xAA35,0xAA36,0xC4},
    {0xAA43,0xAA43,0xC4}, {0xAA4C,0xAA4C,0xC4}, {0xAA4D,0xAA4D,0x8}, {0xAA60,0xAA6F,0x20},
    {0xAA71,0xAA73,0x20}, {0xAA7A,0xAA7A,0x20}, {0xAA7C,0xAA7C,0xC4}, {0xAA7E,0xAA7F,0x20},
    {0xAAB0,0xAAB0,0xC4}, {0xAAB2,0xAAB4,0xC4}, {0xAAB7,0xAAB8,0xC4}, {0xAABE,0xAABF,0xC4},
    {0xAAC1,0xAAC1,0xC4}, {0xAAE0,0xAAEA,0x20}, {0xAAEB,0xAAEB,0x8}, {0xAAEC,0xAAED,0xC4},
    {0xAAEE,0xAAEF,0x8}, {0xAAF5,0xAAF5,0x8}, {0xAAF6,0xAAF6,0xE4}, {0xABC0,0xABDA,0x20},
    {0xABE3,0xABE4,0x8}, {0xABE5,0xABE5,0xC4}, {0xABE6,0xABE7,0x8}, {0xABE8,0xABE8,0xC4},
    {0xABE9,0xABEA,0x8}, {0xABEC,0xABEC,0x8}, {0xABED,0xABED,0xC4}, {0xD7B0,0xD7C6,0x8A},
    {0xD7CB,0xD7FB,0x8B}, {0xF900,0xFAFF,0x100}, {0xFB1E,0xFB1E,0xC4}, {0xFE00,0xFE0F,0xC4},
    {0xFE10,0xFE19,0x100}, {0xFE20,0xFE2F,0xC4}, {0xFE30,0xFE52,0x100}, {0xFE54,0xFE66,0x100},
    {0xFE68,0xFE6B,0x100}, {0xFEFF,0xFEFF,0x83}, {0xFF01,0xFF60,0x100}, {0xFF9E,0xFF9F,0x44},
    {0xFFA0,0xFFA0,0x80}, {0xFFE0,0xFFE6,0x100}, {0xFFF0,0xFFFB,0x83}, {0x101FD,0x101FD,0xC4},
    {0x102E0,0x102E0,0xC4}, {0x10376,0x1037A,0xC4}, {0x10A00,0x10A00,0x20}, {0x10A01,0x10A03,0xC4},
    {0x10A05,0x10A06,0xC4}, {0x10A0C,0x10A0F,0xC4}, {0x10A10,0x10A13,0x20}, {0x10A15,0x10A17,0x20},
    {0x10A19,0x10A35,0x20}, {0x10A38,0x10A3A,0xC4}, {0x10A3F,0x10A3F,0xE4}, {0x10AE5,0x10AE6,0xC4},
    {0x10D24,0x10D27,0xC4}, {0x10D69,0x10D6D,0xC4}, {0x10EAB,0x10EAC,0xC4}, {0x10EFA,0x10EFF,0xC4},
    {0x10F46,0x10F50,0xC4}, {0x10F82,0x10F85,0xC4}, {0x11000,0x11000,0x8}, {0x11001,0x11001,0xC4},
    {0x11002,0x11002,0x8}, {0x11038,0x11046,0xC4}, {0x11070,0x11070,0xC4}, {0x11073,0x11074,0xC4},
    {0x1107F,0x11081,0xC4}, {0x11082,0x11082,0x8}, {0x110B0,0x110B2,0x8}, {0x110B3,0x110B6,0xC4},
    {0x110B7,0x110B8,0x8}, {0x110B9,0x110BA,0xC4}, {0x110BD,0x110BD,0x87}, {0x110C2,0x110C2,0xC4},
    {0x110CD,0x110CD,0x87}, {0x11100,0x11102,0xC4}, {0x11103,0x11126,0x20}, {0x11127,0x1112B,0xC4},
    {0x1112C,0x1112C,0x8}, {0x1112D,0x11132,0xC4}, {0x11133,0x11133,0xE4}, {0x11134,0x11134,0xC4},
    {0x11144,0x11144,0x20}, {0x11145,0x11146,0x8}, {0x11147,0x11147,0x20}, {0x11173,0x11173,0xC4},
    {0x11180,0x11181,0xC4}, {0x11182,0x11182,0x8}, {0x111B3,0x111B5,0x8}, {0x111B6,0x111BE,0xC4},
    {0x111BF,0x111BF,0x8}, {0x111C0,0x111C0,0x44}, {0x111C2,0x111C3,0x7}, {0x111C9,0x111CC,0xC4},
    {0x111CE,0x111CE,0x8}, {0x111CF,0x111CF,0xC4}, {0x1122C,0x1122E,0x8}, {0x1122F,0x11231,0xC4},
    {0x11232,0x11233,0x8}, {0x11234,0x11234,0xC4}, {0x11235,0x11235,0x44}, {0x11236,0x11237,0xC4},
    {0x1123E,0x1123E,0xC4}, {0x11241,0x11241,0xC4}, {0x112DF,0x112DF,0xC4}, {0x112E0,0x112E2,0x8},
    {0x112E3,0x112EA,0xC4}, {0x11300,0x11301,0xC4}, {0x11302,0x11303,0x8}, {0x1133B,0x1133C,0xC4},
    {0x1133E,0x1133E,0x44}, {0x1133F,0x1133F,0x8}, {0x11340,0x11340,0xC4}, {0x11341,0x11344,0x8},
    {0x11347,0x11348,0x8}, {0x1134B,0x1134C,0x8}, {0x1134D,0x1134D,0x44}, {0x11357,0x11357,0x44},
    {0x11362,0x11363,0x8}, {0x11366,0x1136C,0xC4}, {0x11370,0x11374,0xC4}, {0x11380,0x11389,0x20},
    {0x1138B,0x1138B,0x20}, {0x1138E,0x1138E,0x20}, {0x11390,0x113B5,0x20}, {0x113B8,0x113B8,0x44},
    {0x113B9,0x113BA,0x8}, {0x113BB,0x113C0,0xC4}, {0x113C2,0x113C2,0x44}, {0x113C5,0x113C5,0x44},
    {0x113C7,0x113C9,0x44}, {0x113CA,0x113CA,0x8}, {0x113CC,0x113CD,0x8}, {0x113CE,0x113CE,0xC4},
    {0x113CF,0x113CF,0x44}, {0x113D0,0x113D0,0xE4}, {0x113D1,0x113D1,0x7}, {0x113D2,0x113D2,0xC4},
    {0x113E1,0x113E2,0xC4}, {0x11435,0x11437,0x8}, {0x11438,0x1143F,0xC4}, {0x11440,0x11441,0x8},
    {0x11442,0x11444,0xC4}, {0x11445,0x11445,0x8}, {0x11446,0x11446,0xC4}, {0x1145E,0x1145E,0xC4},
    {0x114B0,0x114B0,0x44}, {0x114B1,0x114B2,0x8}, {0x114B3,0x114B8,0xC4}, {0x114B9,0x114B9,0x8},
    {0x114BA,0x114BA,0xC4}, {0x114BB,0x114BC,0x8}, {0x114BD,0x114BD,0x44}, {0x114BE,0x114BE,0x8},
    {0x114BF,0x114C0,0xC4}, {0x114C1,0x114C1,0x8}, {0x114C2,0x114C3,0xC4}, {0x115AF,0x115AF,0x44},
    {0x115B0,0x115B1,0x8}, {0x115B2,0x115B5,0xC4}, {0x115B8,0x115BB,0x8}, {0x115BC,0x115BD,0xC4},
    {0x115BE,0x115BE,0x8}, {0x115BF,0x115C0,0xC4}, {0x115DC,0x115DD,0xC4}, {0x11630,0x11632,0x8},
    {0x11633,0x1163A,0xC4}, {0x1163B,0x1163C,0x8}, {0x1163D,0x1163D,0xC4}, {0x1163E,0x1163E,0x8},
    {0x1163F,0x11640,0xC4}, {0x116AB,0x116AB,0xC4}, {0x116AC,0x116AC,0x8}, {0x116AD,0x116AD,0xC4},
    {0x116AE,0x116AF,0x8}, {0x116B0,0x116B5,0xC4}, {0x116B6,0x116B6,0x44}, {0x116B7,0x116B7,0xC4},
    {0x1171D,0x1171D,0xC4}, {0x1171E,0x1171E,0x8}, {0x1171F,0x1171F,0xC4}, {0x11722,0x11725,0xC4},
    {0x11726,0x11726,0x8}, {0x11727,0x1172B,0xC4}, {0x1182C,0x1182E,0x8}, {0x1182F,0x11837,0xC4},
    {0x11838,0x11838,0x8}, {0x11839,0x1183A,0xC4}, {0x11900,0x11906,0x20}, {0x11909,0x11909,0x20},
    {0x1190C,0x11913,0x20}, {0x11915,0x11916,0x20}, {0x11918,0x1192F,0x20}, {0x11930,0x11930,0x44},
    {0x11931,0x11935,0x8}, {0x11937,0x11938,0x8}, {0x1193B,0x1193C,0xC4}, {0x1193D,0x1193D,0x44},
    {0x1193E,0x1193E,0xE4}, {0x1193F,0x1193F,0x7}, {0x11940,0x11940,0x8}, {0x11941,0x11941,0x7},
    {0x11942,0x11942,0x8}, {0x11943,0x11943,0xC4}, {0x119D1,0x119D3,0x8}, {0x119D4,0x119D7,0xC4},
    {0x119DA,0x119DB,0xC4}, {0x119DC,0x119DF,0x8}, {0x119E0,0x119E0,0xC4}, {0x119E4,0x119E4,0x8},
    {0x11A00,0x11A00,0x20}, {0x11A01,0x11A0A,0xC4}, {0x11A0B,0x11A32,0x20}, {0x11A33,0x11A38,0xC4},
    {0x11A39,0x11A39,0x8}, {0x11A3B,0x11A3E,0xC4}, {0x11A47,0x11A47,0xE4}, {0x11A50,0x11A50,0x20},
    {0x11A51,0x11A56,0xC4}, {0x11A57,0x11A58,0x8}, {0x11A59,0x11A5B,0xC4}, {0x11A5C,0x11A83,0x20},
    {0x11A84,0x11A89,0x7}, {0x11A8A,0x11A96,0xC4}, {0x11A97,0x11A97,0x8}, {0x11A98,0x11A98,0xC4},
    {0x11A99,0x11A99,0xE4}, {0x11B60,0x11B60,0xC4}, {0x11B61,0x11B61,0x8}, {0x11B62,0x11B64,0xC4},
    {0x11B65,0x11B65,0x8}, {0x11B66,0x11B66,0xC4}, {0x11B67,0x11B67,0x8}, {0x11C2F,0x11C2F,0x8},
    {0x11C30,0x11C36,0xC4}, {0x11C38,0x11C3D,0xC4}, {0x11C3E,0x11C3E,0x8}, {0x11C3F,0x11C3F,0xC4},
    {0x11C92,0x11CA7,0xC4}, {0x11CA9,0x11CA9,0x8}, {0x11CAA,0x11CB0,0xC4}, {0x11CB1,0x11CB1,0x8},
    {0x11CB2,0x11CB3,0xC4}, {0x11CB4,0x11CB4,0x8}, {0x11CB5,0x11CB6,0xC4}, {0x11D31,0x11D36,0xC4},
    {0x11D3A,0x11D3A,0xC4}, {0x11D3C,0x11D3D,0xC4}, {0x11D3F,0x11D45,0xC4}, {0x11D46,0x11D46,0x7},
    {0x11D47,0x11D47,0xC4}, {0x11D8A,0x11D8E,0x8}, {0x11D90,0x11D91,0xC4}, {0x11D93,0x11D94,0x8},
    {0x11D95,0x11D95,0xC4}, {0x11D96,0x11D96,0x8}, {0x11D97,0x11D97,0xC4}, {0x11EF3,0x11EF4,0xC4},
    {0x11EF5,0x11EF6,0x8}, {0x11F00,0x11F01,0xC4}, {0x11F02,0x11F02,0x7}, {0x11F03,0x11F03,0x8},
    {0x11F04,0x11F10,0x20}, {0x11F12,0x11F33,0x20}, {0x11F34,0x11F35,0x8}, {0x11F36,0x11F3A,0xC4},
    {0x11F3E,0x11F3F,0x8}, {0x11F40,0x11F40,0xC4}, {0x11F41,0x11F41,0x44}, {0x11F42,0x11F42,0xE4},
    {0x11F5A,0x11F5A,0xC4}, {0x13430,0x1343F,0x83}, {0x13440,0x13440,0xC4}, {0x13447,0x13455,0xC4},
    {0x1611E,0x16129,0xC4}, {0x1612A,0x1612C,0x8}, {0x1612D,0x1612F,0xC4}, {0x16AF0,0x16AF4,0xC4},
    {0x16B30,0x16B36,0xC4}, {0x16D63,0x16D63,0x8A}, {0x16D67,0x16D6A,0x8A}, {0x16F4F,0x16F4F,0xC4},
    {0x16F51,0x16F87,0x8}, {0x16F8F,0x16F92,0xC4}, {0x16FE0,0x16FE3,0x100}, {0x16FE4,0x16FE4,0xC4},
    {0x16FF0,0x16FF1,0x144}, {0x16FF2,0x16FF6,0x100}, {0x17000,0x18CD5,0x100}, {0x18CFF,0x18D1E,0x100},
    {0x18D80,0x18DF2,0x100}, {0x1AFF0,0x1AFF3,0x100}, {0x1AFF5,0x1AFFB,0x100}, {0x1AFFD,0x1AFFE,0x100},
    {0x1B000,0x1B122,0x100}, {0x1B132,0x1B132,0x100}, {0x1B150,0x1B152,0x100}, {0x1B155,0x1B155,0x100},
    {0x1B164,0x1B167,0x100}, {0x1B170,0x1B2FB,0x100}, {0x1BC9D,0x1BC9E,0xC4}, {0x1BCA0,0x1BCA3,0x83},
    {0x1CF00,0x1CF2D,0xC4}, {0x1CF30,0x1CF46,0xC4}, {0x1D165,0x1D166,0x44}, {0x1D167,0x1D169,0xC4},
    {0x1D16D,0x1D172,0x44}, {0x1D173,0x1D17A,0x83}, {0x1D17B,0x1D182,0xC4}, {0x1D185,0x1D18B,0xC4},
    {0x1D1AA,0x1D1AD,0xC4}, {0x1D242,0x1D244,0xC4}, {0x1D300,0x1D356,0x100}, {0x1D360,0x1D376,0x100},
    {0x1DA00,0x1DA36,0xC4}, {0x1DA3B,0x1DA6C,0xC4}, {0x1DA75,0x1DA75,0xC4}, {0x1DA84,0x1DA84,0xC4},
    {0x1DA9B,0x1DA9F,0xC4}, {0x1DAA1,0x1DAAF,0xC4}, {0x1E000,0x1E006,0xC4}, {0x1E008,0x1E018,0xC4},
    {0x1E01B,0x1E021,0xC4}, {0x1E023,0x1E024,0xC4}, {0x1E026,0x1E02A,0xC4}, {0x1E08F,0x1E08F,0xC4},
    {0x1E130,0x1E136,0xC4}, {0x1E2AE,0x1E2AE,0xC4}, {0x1E2EC,0x1E2EF,0xC4}, {0x1E4EC,0x1E4EF,0xC4},
    {0x1E5EE,0x1E5EF,0xC4}, {0x1E6E3,0x1E6E3,0xC4}, {0x1E6E6,0x1E6E6,0xC4}, {0x1E6EE,0x1E6EF,0xC4},
    {0x1E6F5,0x1E6F5,0xC4}, {0x1E8D0,0x1E8D6,0xC4}, {0x1E944,0x1E94A,0xC4}, {0x1F004,0x1F004,0x110},
    {0x1F02C,0x1F02F,0x10}, {0x1F094,0x1F09F,0x10}, {0x1F0AF,0x1F0B0,0x10}, {0x1F0C0,0x1F0C0,0x10},
    {0x1F0CF,0x1F0CF,0x110}, {0x1F0D0,0x1F0D0,0x10}, {0x1F0F6,0x1F0FF,0x10}, {0x1F170,0x1F171,0x10},
    {0x1F17E,0x1F17F,0x10}, {0x1F18E,0x1F18E,0x110}, {0x1F191,0x1F19A,0x110}, {0x1F1AE,0x1F1E5,0x10},
    {0x1F1E6,0x1F1FF,0x6}, {0x1F200,0x1F200,0x100}, {0x1F201,0x1F202,0x110}, {0x1F203,0x1F20F,0x10},
    {0x1F210,0x1F219,0x100}, {0x1F21A,0x1F21A,0x110}, {0x1F21B,0x1F22E,0x100}, {0x1F22F,0x1F22F,0x110},
    {0x1F230,0x1F231,0x100}, {0x1F232,0x1F23A,0x110}, {0x1F23B,0x1F23B,0x100}, {0x1F23C,0x1F23F,0x10},
    {0x1F240,0x1F248,0x100}, {0x1F249,0x1F24F,0x10}, {0x1F250,0x1F251,0x110}, {0x1F252,0x1F25F,0x10},
    {0x1F260,0x1F265,0x100}, {0x1F266,0x1F2FF,0x10}, {0x1F300,0x1F320,0x110}, {0x1F321,0x1F321,0x10},
    {0x1F324,0x1F32C,0x10}, {0x1F32D,0x1F335,0x110}, {0x1F336,0x1F336,0x10}, {0x1F337,0x1F37C,0x110},
    {0x1F37D,0x1F37D,0x10}, {0x1F37E,0x1F393,0x110}, {0x1F396,0x1F397,0x10}, {0x1F399,0x1F39B,0x10},
    {0x1F39E,0x1F39F,0x10}, {0x1F3A0,0x1F3CA,0x110}, {0x1F3CB,0x1F3CE,0x10}, {0x1F3CF,0x1F3D3,0x110},
    {0x1F3D4,0x1F3DF,0x10}, {0x1F3E0,0x1F3F0,0x110}, {0x1F3F3,0x1F3F3,0x10}, {0x1F3F4,0x1F3F4,0x110},
    {0x1F3F5,0x1F3F5,0x10}, {0x1F3F7,0x1F3F7,0x10}, {0x1F3F8,0x1F3FA,0x110}, {0x1F3FB,0x1F3FF,0x144},
    {0x1F400,0x1F43E,0x110}, {0x1F43F,0x1F43F,0x10}, {0x1F440,0x1F440,0x110}, {0x1F441,0x1F441,0x10},
    {0x1F442,0x1F4FC,0x110}, {0x1F4FD,0x1F4FD,0x10}, {0x1F4FF,0x1F53D,0x110}, {0x1F549,0x1F54A,0x10},
    {0x1F54B,0x1F54E,0x110}, {0x1F550,0x1F567,0x110}, {0x1F56F,0x1F570,0x10}, {0x1F573,0x1F579,0x10},
    {0x1F57A,0x1F57A,0x110}, {0x1F587,0x1F587,0x10}, {0x1F58A,0x1F58D,0x10}, {0x1F590,0x1F590,0x10},
    {0x1F595,0x1F596,0x110}, {0x1F5A4,0x1F5A4,0x110}, {0x1F5A5,0x1F5A5,0x10}, {0x1F5A8,0x1F5A8,0x10},
    {0x1F5B1,0x1F5B2,0x10}, {0x1F5BC,0x1F5BC,0x10}, {0x1F5C2,0x1F5C4,0x10}, {0x1F5D1,0x1F5D3,0x10},
    {0x1F5DC,0x1F5DE,0x10}, {0x1F5E1,0x1F5E1,0x10}, {0x1F5E3,0x1F5E3,0x10}, {0x1F5E8,0x1F5E8,0x10},
    {0x1F5EF,0x1F5EF,0x10}, {0x1F5F3,0x1F5F3,0x10}, {0x1F5FA,0x1F5FA,0x10}, {0x1F5FB,0x1F64F,0x110},
    {0x1F680,0x1F6C5,0x110}, {0x1F6CB,0x1F6CB,0x10}, {0x1F6CC,0x1F6CC,0x110}, {0x1F6CD,0x1F6CF,0x10},
    {0x1F6D0,0x1F6D2,0x110}, {0x1F6D5,0x1F6D8,0x110}, {0x1F6D9,0x1F6DB,0x10}, {0x1F6DC,0x1F6DF,0x110},
    {0x1F6E0,0x1F6E5,0x10}, {0x1F6E9,0x1F6E9,0x10}, {0x1F6EB,0x1F6EC,0x110}, {0x1F6ED,0x1F6F0,0x10},
    {0x1F6F3,0x1F6F3,0x10}, {0x1F6F4,0x1F6FC,0x110}, {0x1F6FD,0x1F6FF,0x10}, {0x1F7DA,0x1F7DF,0x10},
    {0x1F7E0,0x1F7EB,0x110}, {0x1F7EC,0x1F7EF,0x10}, {0x1F7F0,0x1F7F0,0x110}, {0x1F7F1,0x1F7FF,0x10},
    {0x1F80C,0x1F80F,0x10}, {0x1F848,0x1F84F,0x10}, {0x1F85A,0x1F85F,0x10}, {0x1F888,0x1F88F,0x10},
    {0x1F8AE,0x1F8AF,0x10}, {0x1F8BC,0x1F8BF,0x10}, {0x1F8C2,0x1F8CF,0x10}, {0x1F8D9,0x1F8FF,0x10},
    {0x1F90C,0x1F93A,0x110}, {0x1F93C,0x1F945,0x110}, {0x1F947,0x1F9FF,0x110}, {0x1FA58,0x1FA5F,0x10},
    {0x1FA6E,0x1FA6F,0x10}, {0x1FA70,0x1FA7C,0x110}, {0x1FA7D,0x1FA7F,0x10}, {0x1FA80,0x1FA8A,0x110},
    {0x1FA8B,0x1FA8D,0x10}, {0x1FA8E,0x1FAC6,0x110}, {0x1FAC7,0x1FAC7,0x10}, {0x1FAC8,0x1FAC8,0x110},
    {0x1FAC9,0x1FACC,0x10}, {0x1FACD,0x1FADC,0x110}, {0x1FADD,0x1FADE,0x10}, {0x1FADF,0x1FAEA,0x110},
    {0x1FAEB,0x1FAEE,0x10}, {0x1FAEF,0x1FAF8,0x110}, {0x1FAF9,0x1FAFF,0x10}, {0x1FC00,0x1FFFD,0x10},
    {0x20000,0x2FFFD,0x100}, {0x30000,0x3FFFD,0x100}, {0xE0000,0xE001F,0x83}, {0xE0020,0xE007F,0xC4},
    {0xE0080,0xE00FF,0x83}, {0xE0100,0xE01EF,0xC4}, {0xE01F0,0xE0FFF,0x83},
};
inline constexpr size_t char_props_size = 987;
// }}} unicode-tables

/* Look up the merged properties of a codepoint. Hangul syllables are
 * handled algorithmically to keep the table small. */
inline uint16_t char_props_of(uint32_t cp) {
    if (cp >= 0xAC00 && cp <= 0xD7A3) {
        /* Hangul syllable: LV when the syllable index is a multiple of 28
         * (no trailing consonant), LVT otherwise. Always 2 columns wide. */
        auto gcb = ((cp - 0xAC00) % 28 == 0) ? GCB::LV : GCB::LVT;
        return static_cast<uint16_t>(static_cast<uint16_t>(gcb) |
                                     (static_cast<uint16_t>(WidthClass::Wide) << 7));
    }
    size_t lo = 0, hi = char_props_size;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (char_props[mid].hi < cp) {
            lo = mid + 1;
        } else if (char_props[mid].lo > cp) {
            hi = mid;
        } else {
            return char_props[mid].val;
        }
    }
    return 0;
}

inline GCB gcb_of(uint16_t props) { return static_cast<GCB>(props & 0xF); }
inline bool is_ext_pict(uint16_t props) { return (props & ExtPictFlag) != 0; }
inline InCB incb_of(uint16_t props) { return static_cast<InCB>((props >> 5) & 0x3); }
inline WidthClass width_class_of(uint16_t props) {
    return static_cast<WidthClass>((props >> 7) & 0x3);
}

/* Decode the UTF-8 sequence starting at s (avail bytes available) into a
 * codepoint, storing the consumed byte length in *len. Invalid or truncated
 * sequences are consumed one byte at a time and returned verbatim, so the
 * editing code never gets stuck. */
inline uint32_t decode_utf8(const char *s, size_t avail, size_t *len) {
    const auto *p = reinterpret_cast<const unsigned char *>(s);
    *len = 1;
    if (avail == 0) return 0;
    if ((p[0] & 0x80) == 0) return p[0];
    if ((p[0] & 0xE0) == 0xC0 && avail >= 2 && (p[1] & 0xC0) == 0x80) {
        *len = 2;
        return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    }
    if ((p[0] & 0xF0) == 0xE0 && avail >= 3 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80) {
        *len = 3;
        return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    }
    if ((p[0] & 0xF8) == 0xF0 && avail >= 4 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *len = 4;
        return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
               ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    }
    return p[0];
}

/* Return the byte length of the codepoint ending at position 'pos'
 * (exclusive) in buf. */
inline size_t prev_cp_len(const char *buf, size_t pos) {
    if (pos == 0) return 0;
    size_t i = pos;
    do {
        i--;
    } while (i > 0 && (pos - i) < 4 &&
             (static_cast<unsigned char>(buf[i]) & 0xC0) == 0x80);
    return pos - i;
}

/* Encode a codepoint as UTF-8 and append it to out. */
inline void encode_utf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/* Incremental UAX #29 grapheme cluster boundary detector. Feed codepoints
 * left to right; is_boundary_before() reports whether a cluster boundary
 * precedes the codepoint about to be fed. */
struct GraphemeBreaker {
    bool at_start = true;
    GCB prev = GCB::Other;
    int ri_run = 0;            /* Consecutive Regional Indicators seen. */
    bool emoji_zwj = false;    /* \p{ExtPict} Extend* ZWJ just seen (GB11). */
    bool emoji_seq = false;    /* In \p{ExtPict} Extend* prefix of GB11. */
    bool incb_active = false;  /* InCB Consonant [Extend Linker]* seen (GB9c). */
    bool incb_linker = false;  /* ... with at least one Linker. */

    bool is_boundary_before(uint32_t cp) const {
        uint16_t p = char_props_of(cp);
        GCB next = gcb_of(p);
        if (at_start) return true;                          /* GB1 */
        if (prev == GCB::CR && next == GCB::LF) return false; /* GB3 */
        if (prev == GCB::Control || prev == GCB::CR || prev == GCB::LF)
            return true;                                    /* GB4 */
        if (next == GCB::Control || next == GCB::CR || next == GCB::LF)
            return true;                                    /* GB5 */
        if (prev == GCB::L &&
            (next == GCB::L || next == GCB::V || next == GCB::LV ||
             next == GCB::LVT))
            return false;                                   /* GB6 */
        if ((prev == GCB::LV || prev == GCB::V) &&
            (next == GCB::V || next == GCB::T))
            return false;                                   /* GB7 */
        if ((prev == GCB::LVT || prev == GCB::T) && next == GCB::T)
            return false;                                   /* GB8 */
        if (next == GCB::Extend || next == GCB::ZWJ) return false; /* GB9 */
        if (next == GCB::SpacingMark) return false;         /* GB9a */
        if (prev == GCB::Prepend) return false;             /* GB9b */
        if (incb_active && incb_linker && incb_of(p) == InCB::Consonant)
            return false;                                   /* GB9c */
        if (emoji_zwj && is_ext_pict(p)) return false;      /* GB11 */
        if (prev == GCB::RegionalIndicator &&
            next == GCB::RegionalIndicator && (ri_run % 2) == 1)
            return false;                                   /* GB12/GB13 */
        return true;                                        /* GB999 */
    }

    void feed(uint32_t cp) {
        uint16_t p = char_props_of(cp);
        GCB g = gcb_of(p);

        ri_run = (g == GCB::RegionalIndicator) ? ri_run + 1 : 0;

        /* GB11 state machine: ExtPict Extend* ZWJ × ExtPict. */
        if (is_ext_pict(p)) {
            emoji_seq = true;
            emoji_zwj = false;
        } else if (emoji_seq && g == GCB::Extend) {
            emoji_zwj = false;
        } else if (emoji_seq && g == GCB::ZWJ) {
            emoji_zwj = true;
            emoji_seq = false;
        } else {
            emoji_seq = false;
            emoji_zwj = false;
        }

        /* GB9c state machine: InCB=Consonant [InCB=Extend InCB=Linker]*
         * InCB=Linker [InCB=Extend InCB=Linker]* × InCB=Consonant. */
        InCB ic = incb_of(p);
        if (ic == InCB::Consonant) {
            incb_active = true;
            incb_linker = false;
        } else if (incb_active && (ic == InCB::Extend || ic == InCB::Linker)) {
            if (ic == InCB::Linker) incb_linker = true;
        } else {
            incb_active = false;
            incb_linker = false;
        }

        prev = g;
        at_start = false;
    }
};

/* Return the byte length of the extended grapheme cluster starting at
 * position 'pos' in s (which holds 'len' bytes). */
inline size_t next_grapheme_len(const char *s, size_t pos, size_t len) {
    if (pos >= len) return 0;
    GraphemeBreaker gb;
    size_t i = pos;
    while (i < len) {
        size_t cplen;
        uint32_t cp = decode_utf8(s + i, len - i, &cplen);
        if (i > pos && gb.is_boundary_before(cp)) break;
        gb.feed(cp);
        i += cplen;
    }
    return i - pos;
}

/* Return the byte length of the extended grapheme cluster ending at
 * position 'pos' (exclusive). 'buf' points at the start of the whole
 * string, so the function can look as far back as needed. The position is
 * expected to be a cluster boundary (callers only move by clusters).
 *
 * The function scans back a bounded window of codepoints (extending
 * through Regional Indicator runs so flag parity stays exact), then
 * segments forward. Pathological clusters longer than the window are
 * approximated. */
inline size_t prev_grapheme_len(const char *buf, size_t pos) {
    if (pos == 0) return 0;
    const int max_back = 64;
    size_t start = pos;
    int back = 0;
    while (start > 0 && back < max_back) {
        size_t l = prev_cp_len(buf, start);
        if (l == 0) break;
        size_t cplen;
        uint32_t cp = decode_utf8(buf + (start - l), l, &cplen);
        uint16_t p = char_props_of(cp);
        GCB g = gcb_of(p);
        start -= l;
        back++;
        /* A control/CR/LF always starts its own cluster (except LF after
         * CR), and an ASCII character can only be glued to what precedes
         * it by a Prepend (which is never ASCII): both make safe re-sync
         * anchors. */
        if (g == GCB::Control || g == GCB::CR) break;
        if (g == GCB::LF) {
            size_t pl = prev_cp_len(buf, start);
            if (pl > 0 && buf[start - pl] == '\r') start -= pl; /* CRLF */
            break;
        }
        if (cp < 0x80) {
            size_t pl = prev_cp_len(buf, start);
            if (pl == 0) break;
            size_t dummy;
            uint32_t pcp = decode_utf8(buf + (start - pl), pl, &dummy);
            if (gcb_of(char_props_of(pcp)) != GCB::Prepend) break;
        }
    }
    /* Make sure we did not stop inside a Regional Indicator run: parity
     * matters for GB12/GB13. */
    while (start > 0) {
        size_t dummy;
        uint32_t cp = decode_utf8(buf + start, 4, &dummy);
        if (gcb_of(char_props_of(cp)) != GCB::RegionalIndicator) break;
        size_t l = prev_cp_len(buf, start);
        if (l == 0) break;
        uint32_t pcp = decode_utf8(buf + (start - l), l, &dummy);
        if (gcb_of(char_props_of(pcp)) != GCB::RegionalIndicator) break;
        start -= l;
    }
    /* Forward-segment the window; the last boundary before pos starts the
     * cluster we are looking for. */
    size_t prev = start, i = start;
    while (i < pos) {
        size_t g = next_grapheme_len(buf, i, pos);
        if (g == 0) break;
        prev = i;
        i += g;
    }
    return pos - prev;
}

/* Display width of a single codepoint, terminal-style. */
inline int cp_width(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0; /* control chars */
    switch (width_class_of(char_props_of(cp))) {
    case WidthClass::Zero: return 0;
    case WidthClass::Wide: return 2;
    default: return 1;
    }
}

/* Display width of one extended grapheme cluster (len bytes at s).
 * Extending characters and ZWJ-joined continuations contribute no width;
 * an emoji variation selector (U+FE0F) forces emoji presentation (2
 * columns); a Regional Indicator pair (flag) is 2 columns. */
inline int grapheme_width(const char *s, size_t len) {
    int width = 0;
    int ri_count = 0;
    bool has_vs16 = false;
    bool after_zwj = false;
    size_t i = 0;
    while (i < len) {
        size_t cplen;
        uint32_t cp = decode_utf8(s + i, len - i, &cplen);
        uint16_t p = char_props_of(cp);
        GCB g = gcb_of(p);
        if (cp == 0xFE0F) has_vs16 = true;
        if (g == GCB::RegionalIndicator) ri_count++;
        if (!after_zwj && g != GCB::Extend && g != GCB::ZWJ)
            width += cp_width(cp);
        after_zwj = (g == GCB::ZWJ);
        i += cplen;
    }
    if (ri_count >= 2) return 2; /* flag emoji */
    if (has_vs16 && width < 2) return 2;
    return width;
}

/* If s[] points at an ANSI CSI escape sequence (e.g. a color change like
 * ESC [ 1 ; 32 m), return its length in bytes. Otherwise return 0.
 * The sequence layout follows ECMA-48: ESC '[', parameter bytes
 * (0x30-0x3f), intermediate bytes (0x20-0x2f), and a final byte
 * (0x40-0x7e). */
inline size_t ansi_escape_len(const char *s, size_t len) {
    if (len < 2 || s[0] != '\x1b' || s[1] != '[') return 0;
    size_t i = 2;
    while (i < len && static_cast<unsigned char>(s[i]) >= 0x30 &&
           static_cast<unsigned char>(s[i]) <= 0x3f)
        i++;
    while (i < len && static_cast<unsigned char>(s[i]) >= 0x20 &&
           static_cast<unsigned char>(s[i]) <= 0x2f)
        i++;
    if (i >= len || static_cast<unsigned char>(s[i]) < 0x40 ||
        static_cast<unsigned char>(s[i]) > 0x7e)
        return 0;
    return i + 1;
}

/* Display width of a UTF-8 string of 'len' bytes, summing grapheme cluster
 * widths. ANSI CSI escape sequences (e.g. colors in the prompt) count as
 * zero width. */
inline size_t str_width(const char *s, size_t len) {
    size_t width = 0, i = 0;
    while (i < len) {
        if (s[i] == '\x1b') {
            size_t skip = ansi_escape_len(s + i, len - i);
            if (skip > 0) {
                i += skip;
                continue;
            }
        }
        size_t g = next_grapheme_len(s, i, len);
        if (g == 0) break;
        width += grapheme_width(s + i, g);
        i += g;
    }
    return width;
}

/* Display width of the single grapheme cluster at s. */
inline int single_grapheme_width(const char *s, size_t len) {
    return len ? grapheme_width(s, len) : 0;
}

} // namespace unicode

/* ====================== Editing engine (detail) =========================== *
 * A faithful C++ port of the antirez/linenoise editing engine: raw mode,
 * single/multi line refresh, history, completion, hints, mask mode and
 * bracketed paste folding. Differences from upstream are noted inline.
 */
namespace detail {

inline constexpr size_t kHistoryDefaultMaxLen = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
inline constexpr size_t kMaxLine = LINENOISE_MAX_LINE;
inline constexpr size_t kPasteFoldThreshold = LINENOISE_PASTE_FOLD_THRESHOLD;
inline constexpr size_t kPasteFoldContext = 8; /* Context chars kept around folds. */
inline constexpr size_t kPasteMaxBytes = LINENOISE_MAX_LINE;
inline constexpr int kMaxFolds = 16;

using CompletionCallbackFn =
    std::function<void(const char *, std::vector<std::string> &)>;
using HintsCallbackFn = std::function<std::string(const char *, int &, bool &)>;

/* Mutable library state (C++17 inline variables: one instance program-wide,
 * unlike the per-translation-unit statics of older cpp-linenoise). */
inline CompletionCallbackFn completion_callback;
inline HintsCallbackFn hints_callback;
inline bool mask_mode = false;
inline bool ml_mode = false;
inline bool raw_mode = false;
inline int raw_mode_ifd = STDIN_FILENO;
inline int raw_mode_ofd = STDOUT_FILENO;
inline bool atexit_registered = false;
inline size_t history_max_len = kHistoryDefaultMaxLen;
inline std::vector<std::string> history;

enum KEY_ACTION {
    KEY_NULL = 0,
    CTRL_A = 1,
    CTRL_B = 2,
    CTRL_C = 3,
    CTRL_D = 4,
    CTRL_E = 5,
    CTRL_F = 6,
    CTRL_H = 8,
    TAB = 9,
    CTRL_K = 11,
    CTRL_L = 12,
    ENTER = 13,
    CTRL_N = 14,
    CTRL_P = 16,
    CTRL_T = 20,
    CTRL_U = 21,
    CTRL_W = 23,
    ESC = 27,
    BACKSPACE = 127
};

/* The State structure represents the state during line editing. */
struct State {
    bool in_completion = false; /* TAB pressed, navigating completions. */
    size_t completion_idx = 0;  /* Index of next completion to propose. */
    int ifd = STDIN_FILENO;     /* Terminal stdin file descriptor. */
    int ofd = STDOUT_FILENO;    /* Terminal stdout file descriptor. */
    std::string buf;            /* Edited line buffer. */
    std::string prompt;         /* Prompt to display. */
    size_t pos = 0;             /* Current cursor position (bytes). */
    size_t oldpos = 0;          /* Previous refresh cursor position. */
    size_t cols = 80;           /* Number of columns in terminal. */
    size_t oldrows = 0;         /* Rows used by last refreshed line. */
    int oldrpos = 1;            /* Cursor row from last refresh. */
    int history_index = 0;      /* History index we are editing. */
    bool history_entry_active = false; /* Temp history entry not yet removed. */
    /* Folded byte ranges of buf (display-only), sorted by start offset. */
    std::vector<std::pair<size_t, size_t>> folds;
};

inline State *active_state = nullptr; /* For Hide()/Show(). */

/* ===================== Low level terminal handling ======================== */

inline bool assume_tty() { return std::getenv("LINENOISE_ASSUME_TTY") != nullptr; }

#ifdef _WIN32

inline DWORD orig_console_in_mode = 0;
inline DWORD orig_console_out_mode = 0;
inline bool console_out_mode_saved = false;
inline std::string console_in_queue;
inline size_t console_in_queue_pos = 0;
inline wchar_t console_pending_high_surrogate = 0;

/* True if fd refers to a real console; stores the console handle. */
inline bool console_handle(int fd, HANDLE *out = nullptr) {
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return false;
    if (out) *out = h;
    return true;
}

inline bool is_tty(int fd) { return console_handle(fd); }

/* Read one byte of UTF-8 input. Console input is read with ReadConsoleW
 * (UTF-16, surrogate pairs handled) and transcoded to UTF-8 so the editing
 * engine sees the same byte stream as on Unix. With virtual terminal input
 * enabled, special keys arrive as VT escape sequences in this stream. */
inline int read_byte(int fd, char *c) {
    HANDLE h;
    if (!console_handle(fd, &h))
        return static_cast<int>(_read(fd, c, 1));
    while (console_in_queue_pos >= console_in_queue.size()) {
        console_in_queue.clear();
        console_in_queue_pos = 0;
        wchar_t wc;
        DWORD nread = 0;
        if (!ReadConsoleW(h, &wc, 1, &nread, nullptr)) {
            errno = EIO;
            return -1;
        }
        if (nread == 0) return 0;
        uint32_t cp = wc;
        if (wc >= 0xD800 && wc <= 0xDBFF) {
            console_pending_high_surrogate = wc;
            continue;
        }
        if (wc >= 0xDC00 && wc <= 0xDFFF) {
            if (!console_pending_high_surrogate) continue;
            cp = 0x10000 +
                 ((static_cast<uint32_t>(console_pending_high_surrogate) - 0xD800)
                  << 10) +
                 (static_cast<uint32_t>(wc) - 0xDC00);
            console_pending_high_surrogate = 0;
        } else {
            console_pending_high_surrogate = 0;
        }
        unicode::encode_utf8(console_in_queue, cp);
    }
    *c = console_in_queue[console_in_queue_pos++];
    return 1;
}

/* Write UTF-8 bytes. Console output goes through WriteConsoleW so it
 * renders correctly regardless of the console code page; the engine always
 * writes complete escape sequences and UTF-8 characters per call. */
inline int write_bytes(int fd, const char *p, size_t n) {
    HANDLE h;
    if (!console_handle(fd, &h)) {
        size_t off = 0;
        while (off < n) {
            int w = _write(fd, p + off, static_cast<unsigned>(n - off));
            if (w <= 0) return -1;
            off += static_cast<size_t>(w);
        }
        return static_cast<int>(n);
    }
    if (n == 0) return 0;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, p, static_cast<int>(n), nullptr, 0);
    if (wlen <= 0) return -1;
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p, static_cast<int>(n), &w[0], wlen);
    DWORD written = 0;
    if (!WriteConsoleW(h, w.data(), static_cast<DWORD>(wlen), &written, nullptr))
        return -1;
    return static_cast<int>(n);
}

#else /* !_WIN32 */

inline struct termios orig_termios; /* In order to restore at exit. */

inline bool is_tty(int fd) { return ::isatty(fd) != 0; }

inline int read_byte(int fd, char *c) {
    return static_cast<int>(::read(fd, c, 1));
}

inline int write_bytes(int fd, const char *p, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(fd, p + off, n - off);
        if (w <= 0) {
            if (w == -1 && errno == EINTR) continue;
            return -1;
        }
        off += static_cast<size_t>(w);
    }
    return static_cast<int>(n);
}

#endif /* _WIN32 */

inline int write_str(int fd, const char *s) {
    return write_bytes(fd, s, std::strlen(s));
}

inline void at_exit_handler();

/* Return true if the terminal is known to be unable to understand basic
 * escape sequences. On Windows this means the console refused virtual
 * terminal mode (pre-Windows 10 consoles). */
inline bool is_unsupported_term() {
#ifdef _WIN32
    HANDLE h;
    DWORD mode;
    if (!console_handle(STDOUT_FILENO, &h)) return false;
    if (!GetConsoleMode(h, &mode)) return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return false;
    if (!SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return true;
    SetConsoleMode(h, mode);
    return false;
#else
    static const char *unsupported[] = {"dumb", "cons25", "emacs", nullptr};
    const char *term = std::getenv("TERM");
    if (term == nullptr) return false;
    for (int j = 0; unsupported[j]; j++) {
        if (!strcasecmp(term, unsupported[j])) return true;
    }
    return false;
#endif
}

/* Put the terminal in raw mode. Returns 0 on success, -1 (errno=ENOTTY)
 * on failure. */
inline int enable_raw_mode(int fd) {
    /* Test mode: when LINENOISE_ASSUME_TTY is set, skip terminal setup.
     * This allows testing via pipes without a real terminal. */
    if (assume_tty()) {
        raw_mode = true;
        return 0;
    }

#ifdef _WIN32
    HANDLE hin;
    if (!console_handle(fd, &hin)) goto fatal;
    if (!atexit_registered) {
        std::atexit(at_exit_handler);
        atexit_registered = true;
    }
    {
        if (!GetConsoleMode(hin, &orig_console_in_mode)) goto fatal;
        DWORD in_mode = orig_console_in_mode;
        in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        if (!SetConsoleMode(hin, in_mode)) goto fatal;

        HANDLE hout;
        if (console_handle(raw_mode_ofd, &hout)) {
            if (GetConsoleMode(hout, &orig_console_out_mode)) {
                console_out_mode_saved = true;
                DWORD out_mode = orig_console_out_mode |
                                 ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                 ENABLE_PROCESSED_OUTPUT;
                if (!SetConsoleMode(hout, out_mode)) {
                    SetConsoleMode(hin, orig_console_in_mode);
                    goto fatal;
                }
            }
        }
    }
    raw_mode = true;
    raw_mode_ifd = fd;
    /* Ask the terminal to wrap paste input between ESC[200~ and ESC[201~. */
    write_str(raw_mode_ofd, "\x1b[?2004h");
    return 0;
#else
    struct termios raw;

    if (!is_tty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        std::atexit(at_exit_handler);
        atexit_registered = true;
    }
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;

    raw = orig_termios; /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - echoing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    raw_mode = true;
    raw_mode_ifd = fd;
    /* Ask the terminal to wrap paste input between ESC[200~ and ESC[201~. */
    write_str(raw_mode_ofd, "\x1b[?2004h");
    return 0;
#endif

fatal:
    errno = ENOTTY;
    return -1;
}

inline void disable_raw_mode(int fd) {
    /* Test mode: nothing to restore. */
    if (assume_tty()) {
        raw_mode = false;
        return;
    }
#ifdef _WIN32
    HANDLE hin;
    if (raw_mode && console_handle(fd, &hin)) {
        write_str(raw_mode_ofd, "\x1b[?2004l");
        SetConsoleMode(hin, orig_console_in_mode);
        HANDLE hout;
        if (console_out_mode_saved && console_handle(raw_mode_ofd, &hout))
            SetConsoleMode(hout, orig_console_out_mode);
        raw_mode = false;
    }
#else
    /* Don't even check the return value as it's too late. */
    if (raw_mode && tcsetattr(fd, TCSAFLUSH, &orig_termios) != -1) {
        /* Leave bracketed paste mode when leaving raw mode. */
        write_str(raw_mode_ofd, "\x1b[?2004l");
        raw_mode = false;
    }
#endif
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
inline int get_cursor_position(int ifd, int ofd) {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;

    /* Report cursor location */
    if (write_bytes(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf) - 1) {
        if (read_byte(ifd, buf + i) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) return -1;
    return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
inline size_t get_columns([[maybe_unused]] int ifd, int ofd) {
    /* Test mode: use LINENOISE_COLS env var for fixed width. */
    if (const char *cols_env = std::getenv("LINENOISE_COLS")) {
        int v = std::atoi(cols_env);
        if (v > 0) return static_cast<size_t>(v);
    }

#ifdef _WIN32
    HANDLE hout;
    if (console_handle(ofd, &hout)) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(hout, &info))
            return static_cast<size_t>(info.srWindow.Right - info.srWindow.Left + 1);
    }
    return 80;
#else
    struct winsize ws;

    if (ioctl(ofd, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int start, cols;

        /* Get the initial position so we can restore it later. */
        start = get_cursor_position(ifd, ofd);
        if (start == -1) return 80;

        /* Go to right margin and get position. */
        if (write_bytes(ofd, "\x1b[999C", 6) != 6) return 80;
        cols = get_cursor_position(ifd, ofd);
        if (cols == -1) return 80;

        /* Restore position. */
        if (cols > start) {
            char seq[32];
            snprintf(seq, 32, "\x1b[%dD", cols - start);
            write_str(ofd, seq);
        }
        return static_cast<size_t>(cols);
    }
    return ws.ws_col;
#endif
}

/* Clear the screen. Used to handle ctrl+l. */
inline void clear_screen() {
    write_bytes(STDOUT_FILENO, "\x1b[H\x1b[2J", 7);
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
inline void beep() {
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== History =================================== */

/* Add a new entry to the history. Returns true if the line was added. */
inline bool history_add(const char *line) {
    if (history_max_len == 0) return false;

    /* Don't add duplicated lines. */
    if (!history.empty() && history.back() == line) return false;

    /* If we reached the max length, remove the older line. */
    if (history.size() == history_max_len) history.erase(history.begin());
    history.emplace_back(line);
    return true;
}

inline bool history_set_max_len(size_t len) {
    if (len < 1) return false;
    if (history.size() > len)
        history.erase(history.begin(), history.end() - len);
    history_max_len = len;
    return true;
}

/* Save the history in the specified file. Returns true on success. */
inline bool history_save(const char *filename) {
#ifndef _WIN32
    mode_t old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
#endif
    std::ofstream f(filename, std::ios::binary);
#ifndef _WIN32
    umask(old_umask);
    if (f) ::chmod(filename, S_IRUSR | S_IWUSR);
#endif
    if (!f) return false;
    for (const auto &entry : history) {
        /* Keep the history file newline-separated: embedded newlines in an
         * entry are stored as CR and converted back by history_load(). */
        std::string s = entry;
        std::replace(s.begin(), s.end(), '\n', '\r');
        f << s << '\n';
    }
    return f.good();
}

/* Load the history from the specified file. Returns true on success. */
inline bool history_load(const char *filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        /* Rebuild embedded newlines that were saved as CR. */
        std::replace(line.begin(), line.end(), '\r', '\n');
        history_add(line.c_str());
    }
    return true;
}

/* At exit we'll try to fix the terminal to the initial conditions. */
inline void at_exit_handler() { disable_raw_mode(raw_mode_ifd); }

/* =========================== Refresh / folds ============================== */

inline constexpr int kRefreshClean = 1 << 0; /* Clean the old prompt. */
inline constexpr int kRefreshWrite = 1 << 1; /* Rewrite the prompt. */
inline constexpr int kRefreshAll = kRefreshClean | kRefreshWrite;

inline void refresh_line(State &l);
inline void refresh_line_with_flags(State &l, int flags);

/* A fold is a display-only replacement for a byte range in l.buf. The
 * edited buffer always keeps the real bytes; refresh code asks
 * render_buffer() for a temporary printable version plus the cursor
 * position inside it. */
struct RenderFold {
    size_t start;
    size_t end;
    std::string display;
};

/* Return the number of logical lines in the range. */
inline size_t fold_count_lines(const char *buf, size_t len) {
    size_t lines = 1;
    for (size_t j = 0; j < len; j++)
        if (buf[j] == '\n') lines++;
    return lines;
}

/* Return true if the text should be folded: if it contains newlines or is
 * at least kPasteFoldThreshold bytes long. */
inline bool should_fold_text(const char *buf, size_t len) {
    return memchr(buf, '\n', len) != nullptr || len >= kPasteFoldThreshold;
}

/* Fill f.display with the text shown instead of the folded range. */
inline void fold_set_rendered_text(RenderFold &f, const std::string &buf) {
    size_t hidden = f.end - f.start;
    size_t lines = fold_count_lines(buf.data() + f.start, hidden);
    char display[64];
    if (lines > 1)
        snprintf(display, sizeof(display), "[... %zu pasted lines ...]", lines);
    else
        snprintf(display, sizeof(display), "[... %zu pasted chars ...]", hidden);
    f.display = display;
}

/* Populate f with one fold reconstructed from a history entry. History
 * stores the real text, but not the original paste boundaries, so we
 * reconstruct an approximation: fold if it is long or contains newlines,
 * keeping a few characters of context at both ends. */
inline bool build_history_fold(State &l, RenderFold &f) {
    f.start = f.end = 0;
    f.display.clear();
    if (l.buf.empty() || mask_mode) return false;
    if (!should_fold_text(l.buf.data(), l.buf.size())) return false;

    f.start = 0;
    f.end = l.buf.size();
    if (l.buf.size() > kPasteFoldContext * 2) {
        size_t pos = 0, chars = 0;
        bool nl = false;

        /* We leave (if possible) a few chars on the start before the fold,
         * to give context. */
        while (pos < l.buf.size() && chars < kPasteFoldContext) {
            size_t step = unicode::next_grapheme_len(l.buf.data(), pos, l.buf.size());
            if (step == 0 || pos + step > l.buf.size()) break;
            if (l.buf[pos] == '\n') nl = true;
            pos += step;
            chars++;
        }
        f.start = nl ? 0 : pos;

        /* And also on the end side. */
        pos = l.buf.size();
        chars = 0;
        nl = false;
        while (pos > 0 && chars < kPasteFoldContext) {
            size_t step = unicode::prev_grapheme_len(l.buf.data(), pos);
            if (step == 0 || step > pos) break;
            pos -= step;
            if (l.buf[pos] == '\n') nl = true;
            chars++;
        }
        f.end = nl ? l.buf.size() : pos;
        if (f.start >= f.end) {
            f.start = 0;
            f.end = l.buf.size();
        }
    }
    fold_set_rendered_text(f, l.buf);
    return true;
}

/* Populate fs with the folds to render for the current buffer. Return true
 * if folding should be used, false if the buffer renders as-is. */
inline bool get_render_folds(State &l, std::vector<RenderFold> &fs) {
    fs.clear();
    if (l.buf.empty() || mask_mode) return false;

    for (const auto &range : l.folds) {
        if (range.first >= range.second || range.second > l.buf.size()) continue;
        RenderFold f;
        f.start = range.first;
        f.end = range.second;
        fold_set_rendered_text(f, l.buf);
        fs.push_back(std::move(f));
    }
    return !fs.empty();
}

/* Build the string actually displayed in the user prompt: the edited line,
 * or a version where pasted/multiline ranges are replaced by their folded
 * "[...]" markers. outpos is l.pos translated into the rendered buffer. */
inline void render_buffer(State &l, std::string &out, size_t &outpos) {
    std::vector<RenderFold> fs;
    if (!get_render_folds(l, fs)) {
        out = l.buf;
        outpos = l.pos;
        return;
    }

    /* Gaps are copied as-is, folded ranges are replaced by their markers.
     * The bytes inside each [start,end) range stay in l.buf but are not
     * emitted to the terminal. */
    out.clear();
    size_t src = 0;
    bool pos_set = false;
    outpos = 0;
    for (const auto &f : fs) {
        if (!pos_set && l.pos <= f.start) {
            outpos = out.size() + (l.pos - src);
            pos_set = true;
        }
        out.append(l.buf, src, f.start - src);

        if (!pos_set && l.pos < f.end) {
            outpos = out.size() + f.display.size();
            pos_set = true;
        }
        out += f.display;
        if (!pos_set && l.pos == f.end) {
            outpos = out.size();
            pos_set = true;
        }
        src = f.end;
    }
    if (!pos_set) outpos = out.size() + (l.pos - src);
    out.append(l.buf, src, l.buf.size() - src);
}

/* Return the number of bytes to move right from pos. If pos is at the
 * start of a folded range, the whole hidden range is skipped by one cursor
 * movement. */
inline size_t edit_next_len(State &l, size_t pos) {
    std::vector<RenderFold> fs;
    if (get_render_folds(l, fs)) {
        for (const auto &f : fs)
            if (pos == f.start) return f.end - f.start;
    }
    return unicode::next_grapheme_len(l.buf.data(), pos, l.buf.size());
}

/* Return the number of bytes to move left from pos. If pos is at the end
 * of a folded range, the whole hidden range is skipped by one cursor
 * movement. */
inline size_t edit_prev_len(State &l, size_t pos) {
    std::vector<RenderFold> fs;
    if (get_render_folds(l, fs)) {
        for (const auto &f : fs)
            if (pos == f.end) return f.end - f.start;
    }
    return unicode::prev_grapheme_len(l.buf.data(), pos);
}

/* Add a fold range, keeping the array sorted by start offset. */
inline void fold_add(State &l, size_t start, size_t end) {
    if (start >= end || l.folds.size() == kMaxFolds) return;
    auto it = std::upper_bound(
        l.folds.begin(), l.folds.end(), start,
        [](size_t v, const std::pair<size_t, size_t> &f) { return v < f.first; });
    l.folds.insert(it, {start, end});
}

/* Clear all remembered fold ranges. */
inline void fold_clear(State &l) { l.folds.clear(); }

/* Return true if [pos,pos+len) overlaps any folded range. */
inline bool range_overlaps_fold(State &l, size_t pos, size_t len) {
    size_t end = pos + len;
    for (const auto &f : l.folds)
        if (end > f.first && pos < f.second) return true;
    return false;
}

/* Adjust fold ranges after an insertion. If insertion lands inside a fold,
 * remove that fold because it no longer maps to an unchanged range. */
inline void adjust_folds_after_insert(State &l, size_t pos, size_t len) {
    for (auto it = l.folds.begin(); it != l.folds.end();) {
        if (pos <= it->first) {
            it->first += len;
            it->second += len;
            ++it;
        } else if (pos < it->second) {
            it = l.folds.erase(it);
        } else {
            ++it;
        }
    }
}

/* Adjust fold ranges after a deletion. If deletion overlaps a fold, remove
 * that fold because it no longer maps to an unchanged range. */
inline void adjust_folds_after_delete(State &l, size_t pos, size_t len) {
    size_t end = pos + len;
    for (auto it = l.folds.begin(); it != l.folds.end();) {
        if (end <= it->first) {
            it->first -= len;
            it->second -= len;
            ++it;
        } else if (pos >= it->second) {
            ++it;
        } else {
            it = l.folds.erase(it);
        }
    }
}

/* Helper of refresh_single_line() and refresh_multi_line() to show hints
 * to the right of the prompt. */
inline void refresh_show_hints(std::string &ab, State &l, size_t pwidth,
                               size_t bufwidth) {
    if (!hints_callback) return;
    if (pwidth + bufwidth >= l.cols) return;
    int color = -1;
    bool bold = false;
    std::string hint = hints_callback(l.buf.c_str(), color, bold);
    if (hint.empty()) return;

    size_t hintlen = hint.size();
    size_t hintwidth = unicode::str_width(hint.data(), hintlen);
    size_t hintmaxwidth = l.cols - (pwidth + bufwidth);
    /* Truncate hint to fit, respecting grapheme cluster boundaries. */
    if (hintwidth > hintmaxwidth) {
        size_t i = 0, w = 0;
        while (i < hintlen) {
            size_t clen = unicode::next_grapheme_len(hint.data(), i, hintlen);
            if (clen == 0) break;
            int cwidth = unicode::single_grapheme_width(hint.data() + i, clen);
            if (w + cwidth > hintmaxwidth) break;
            w += cwidth;
            i += clen;
        }
        hintlen = i;
    }
    if (bold && color == -1) color = 37;
    char seq[64];
    if (color != -1 || bold)
        snprintf(seq, 64, "\033[%d;%d;49m", bold ? 1 : 0, color);
    else
        seq[0] = '\0';
    ab += seq;
    ab.append(hint, 0, hintlen);
    if (color != -1 || bold) ab += "\033[0m";
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. Uses display
 * widths (not byte counts) for cursor positioning and horizontal
 * scrolling. */
inline void refresh_single_line(State &l, int flags) {
    size_t pwidth = unicode::str_width(l.prompt.data(), l.prompt.size());
    int fd = l.ofd;
    std::string render;
    size_t pos;
    render_buffer(l, render, pos);
    const char *buf = render.data();
    size_t len = render.size();

    /* Calculate the display width up to cursor and total display width. */
    size_t poscol = unicode::str_width(buf, pos);
    size_t lencol = unicode::str_width(buf, len);
    size_t fullwidth = lencol;

    /* Scroll the buffer horizontally if cursor is past the right edge:
     * trim whole grapheme clusters from the left until the cursor fits. */
    while (len > 0 && pwidth + poscol >= l.cols) {
        size_t clen = unicode::next_grapheme_len(buf, 0, len);
        if (clen == 0 || clen > pos) break;
        int cwidth = unicode::single_grapheme_width(buf, clen);
        buf += clen;
        len -= clen;
        pos -= clen;
        poscol -= cwidth;
        lencol -= cwidth;
    }

    /* Trim from the right if the line still doesn't fit. */
    while (len > 0 && pwidth + lencol > l.cols) {
        size_t clen = unicode::prev_grapheme_len(buf, len);
        if (clen == 0) break;
        int cwidth = unicode::single_grapheme_width(buf + len - clen, clen);
        len -= clen;
        lencol -= cwidth;
    }

    std::string ab;
    /* Cursor to left edge */
    ab += '\r';

    if (flags & kRefreshWrite) {
        /* Write the prompt and the current buffer content */
        ab += l.prompt;
        if (mask_mode) {
            /* In mask mode, output one '*' per grapheme cluster. */
            size_t i = 0;
            while (i < len) {
                ab += '*';
                size_t step = unicode::next_grapheme_len(buf, i, len);
                if (step == 0) break;
                i += step;
            }
        } else {
            ab.append(buf, len);
        }
        /* Show hints if any. */
        refresh_show_hints(ab, l, pwidth, fullwidth);
    }

    /* Erase to right */
    ab += "\x1b[0K";

    if (flags & kRefreshWrite) {
        /* Move cursor to original position (display column, not byte). */
        char seq[64];
        snprintf(seq, sizeof(seq), "\r\x1b[%dC", static_cast<int>(poscol + pwidth));
        ab += seq;
    }

    write_bytes(fd, ab.data(), ab.size()); /* Can't recover from write error. */
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. Uses display
 * widths for positioning. */
inline void refresh_multi_line(State &l, int flags) {
    char seq[64];
    size_t pwidth = unicode::str_width(l.prompt.data(), l.prompt.size());
    std::string render;
    size_t render_pos;
    render_buffer(l, render, render_pos);
    size_t bufwidth = unicode::str_width(render.data(), render.size());
    size_t poswidth = unicode::str_width(render.data(), render_pos);
    int rows = static_cast<int>((pwidth + bufwidth + l.cols - 1) / l.cols);
    int rpos = l.oldrpos; /* Cursor relative row from previous refresh. */
    int rpos2;            /* rpos after refresh. */
    int col;              /* Column position, zero-based. */
    int old_rows = static_cast<int>(l.oldrows);
    int fd = l.ofd;
    l.oldrows = static_cast<size_t>(rows);

    /* First step: clear all the lines used before, starting from the
     * last row. */
    std::string ab;

    if (flags & kRefreshClean) {
        if (old_rows - rpos > 0) {
            snprintf(seq, 64, "\x1b[%dB", old_rows - rpos);
            ab += seq;
        }

        /* Now for every row clear it, go up. */
        for (int j = 0; j < old_rows - 1; j++) ab += "\r\x1b[0K\x1b[1A";
    }

    if (flags & kRefreshAll) {
        /* Clean the top line. */
        ab += "\r\x1b[0K";
    }

    if (flags & kRefreshWrite) {
        /* Write the prompt and the current buffer content */
        ab += l.prompt;
        if (mask_mode) {
            /* In mask mode, output one '*' per grapheme cluster. */
            size_t i = 0;
            while (i < render.size()) {
                ab += '*';
                size_t step =
                    unicode::next_grapheme_len(render.data(), i, render.size());
                if (step == 0) break;
                i += step;
            }
        } else {
            ab += render;
        }

        /* Show hints if any. */
        refresh_show_hints(ab, l, pwidth, bufwidth);

        /* If we are at the very end of the screen with our prompt, we need
         * to emit a newline and move the prompt to the first column. */
        if (l.pos && render_pos == render.size() &&
            (poswidth + pwidth) % l.cols == 0) {
            ab += '\n';
            ab += '\r';
            rows++;
            if (rows > static_cast<int>(l.oldrows))
                l.oldrows = static_cast<size_t>(rows);
        }

        /* Move cursor to right position. */
        rpos2 = static_cast<int>((pwidth + poswidth + l.cols) / l.cols);

        /* Go up till we reach the expected position. */
        if (rows - rpos2 > 0) {
            snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
            ab += seq;
        }

        /* Set column. */
        col = static_cast<int>((pwidth + poswidth) % l.cols);
        if (col) {
            snprintf(seq, 64, "\r\x1b[%dC", col);
            ab += seq;
        } else {
            ab += '\r';
        }

        l.oldrpos = rpos2;
    }

    l.oldpos = l.pos;

    write_bytes(fd, ab.data(), ab.size()); /* Can't recover from write error. */
}

/* Calls the two low level functions refresh_single_line() or
 * refresh_multi_line() according to the selected mode. */
inline void refresh_line_with_flags(State &l, int flags) {
    if (ml_mode)
        refresh_multi_line(l, flags);
    else
        refresh_single_line(l, flags);
}

/* Utility function to avoid specifying kRefreshAll all the times. */
inline void refresh_line(State &l) { refresh_line_with_flags(l, kRefreshAll); }

/* ============================== Completion ================================ */

/* Called by complete_line() and Show() to render the current edited line
 * with the proposed completion. If the current completion table is already
 * available, it is passed as second argument, otherwise the callback is
 * used to obtain it. */
inline void refresh_line_with_completion(State &ls,
                                         const std::vector<std::string> *lc,
                                         int flags) {
    /* Obtain the table of completions if the caller didn't provide one. */
    std::vector<std::string> ctable;
    if (lc == nullptr) {
        completion_callback(ls.buf.c_str(), ctable);
        lc = &ctable;
    }

    /* Show the edited line with completion if possible, or just refresh. */
    if (ls.completion_idx < lc->size()) {
        std::string saved_buf = std::move(ls.buf);
        size_t saved_pos = ls.pos;
        auto saved_folds = std::move(ls.folds);
        ls.buf = (*lc)[ls.completion_idx];
        ls.pos = ls.buf.size();
        ls.folds.clear();
        refresh_line_with_flags(ls, flags);
        ls.buf = std::move(saved_buf);
        ls.pos = saved_pos;
        ls.folds = std::move(saved_folds);
    } else {
        refresh_line_with_flags(ls, flags);
    }
}

/* This is a helper function for edit_feed() and is called when the user
 * types the <tab> key in order to complete the string currently in the
 * input.
 *
 * If the function returns non-zero, the caller should handle the returned
 * value as a byte read from the standard input, and process it as usual:
 * the function may return a byte read from the terminal but not processed.
 * Otherwise, if zero is returned, the input was consumed by the
 * complete_line() function to navigate the possible completions, and the
 * caller should read the next character from stdin. */
inline int complete_line(State &ls, int keypressed) {
    std::vector<std::string> lc;
    int c = keypressed;

    completion_callback(ls.buf.c_str(), lc);
    if (lc.empty()) {
        beep();
        ls.in_completion = false;
        c = 0;
    } else {
        switch (c) {
        case TAB: /* tab */
            if (!ls.in_completion) {
                ls.in_completion = true;
                ls.completion_idx = 0;
            } else {
                ls.completion_idx = (ls.completion_idx + 1) % (lc.size() + 1);
                if (ls.completion_idx == lc.size()) beep();
            }
            c = 0;
            break;
        case ESC: /* escape */
            /* Re-show original buffer */
            if (ls.completion_idx < lc.size()) refresh_line(ls);
            ls.in_completion = false;
            c = 0;
            break;
        default:
            /* Update buffer and return */
            if (ls.completion_idx < lc.size()) {
                ls.buf = lc[ls.completion_idx];
                if (ls.buf.size() > kMaxLine) ls.buf.resize(kMaxLine);
                ls.pos = ls.buf.size();
                fold_clear(ls);
            }
            ls.in_completion = false;
            break;
        }

        /* Show completion or original buffer */
        if (ls.in_completion && ls.completion_idx < lc.size()) {
            refresh_line_with_completion(ls, &lc, kRefreshAll);
        } else {
            refresh_line(ls);
        }
    }

    return c; /* Return last read character */
}

/* ============================= Line editing =============================== */

/* Insert bytes into l.buf without repainting the prompt. The paste path
 * uses this to first store the real pasted bytes, then mark their range as
 * folded, and only then refresh, so raw pasted newlines are never printed
 * directly. Returns false when the buffer would exceed kMaxLine. */
inline bool edit_insert_no_refresh(State &l, const char *c, size_t clen) {
    if (clen > kMaxLine || l.buf.size() > kMaxLine - clen) return false;
    size_t insert_pos = l.pos;
    l.buf.insert(l.pos, c, clen);
    l.pos += clen;
    adjust_folds_after_insert(l, insert_pos, clen);
    return true;
}

/* Insert the character(s) 'c' of length 'clen' at cursor current position.
 * Handles both single-byte ASCII and multi-byte UTF-8 sequences. */
inline void edit_insert(State &l, const char *c, size_t clen) {
    if (l.buf.size() == l.pos) {
        bool needs_refresh = memchr(c, '\n', clen) != nullptr ||
                             memchr(c, '\r', clen) != nullptr;

        if (!edit_insert_no_refresh(l, c, clen)) return;
        if (!needs_refresh && !ml_mode && !hints_callback &&
            (mask_mode || l.folds.empty())) {
            size_t bufwidth = unicode::str_width(l.buf.data(), l.buf.size());
            if (unicode::str_width(l.prompt.data(), l.prompt.size()) + bufwidth <
                l.cols) {
                /* Avoid a full update of the line in the trivial case:
                 * appending in a line that fits the terminal width. */
                if (mask_mode) {
                    write_bytes(l.ofd, "*", 1);
                } else {
                    write_bytes(l.ofd, c, clen);
                }
                return;
            }
        }
        refresh_line(l);
    } else {
        if (!edit_insert_no_refresh(l, c, clen)) return;
        refresh_line(l);
    }
}

/* Move cursor on the left, by one grapheme cluster. */
inline void edit_move_left(State &l) {
    if (l.pos > 0) {
        l.pos -= edit_prev_len(l, l.pos);
        refresh_line(l);
    }
}

/* Move cursor on the right, by one grapheme cluster. */
inline void edit_move_right(State &l) {
    if (l.pos != l.buf.size()) {
        l.pos += edit_next_len(l, l.pos);
        refresh_line(l);
    }
}

/* Move cursor to the start of the line. */
inline void edit_move_home(State &l) {
    if (l.pos != 0) {
        l.pos = 0;
        refresh_line(l);
    }
}

/* Move cursor to the end of the line. */
inline void edit_move_end(State &l) {
    if (l.pos != l.buf.size()) {
        l.pos = l.buf.size();
        refresh_line(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
inline constexpr int kHistoryNext = 0;
inline constexpr int kHistoryPrev = 1;
inline void edit_history_next(State &l, int dir) {
    if (history.size() > 1) {
        /* Update the current history entry before overwriting it with the
         * next one. */
        history[history.size() - 1 - l.history_index] = l.buf;
        /* Show the new entry */
        l.history_index += (dir == kHistoryPrev) ? 1 : -1;
        if (l.history_index < 0) {
            l.history_index = 0;
            return;
        } else if (l.history_index >= static_cast<int>(history.size())) {
            l.history_index = static_cast<int>(history.size()) - 1;
            return;
        }

        /* Copy the selected history entry into the edit buffer. */
        l.buf = history[history.size() - 1 - l.history_index];
        if (l.buf.size() > kMaxLine) l.buf.resize(kMaxLine);
        l.pos = l.buf.size();
        fold_clear(l);

        /* History stores the real text, but not the original paste ranges.
         * If the recalled entry needs folding, create one display fold now
         * so text typed after recall remains outside the folded range. */
        RenderFold f;
        if (build_history_fold(l, f)) fold_add(l, f.start, f.end);
        refresh_line(l);
    }
}

/* Delete the character at the right of the cursor without altering the
 * cursor position, as the "Delete" keyboard key does. */
inline void edit_delete(State &l) {
    if (!l.buf.empty() && l.pos < l.buf.size()) {
        size_t clen = edit_next_len(l, l.pos);
        adjust_folds_after_delete(l, l.pos, clen);
        l.buf.erase(l.pos, clen);
        refresh_line(l);
    }
}

/* Backspace implementation. Deletes the grapheme cluster before the
 * cursor. */
inline void edit_backspace(State &l) {
    if (l.pos > 0 && !l.buf.empty()) {
        size_t clen = edit_prev_len(l, l.pos);
        adjust_folds_after_delete(l, l.pos - clen, clen);
        l.buf.erase(l.pos - clen, clen);
        l.pos -= clen;
        refresh_line(l);
    }
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
inline void edit_delete_prev_word(State &l) {
    size_t old_pos = l.pos;

    /* Skip spaces before the word (move backwards by clusters). */
    while (l.pos > 0 && l.buf[l.pos - 1] == ' ')
        l.pos -= edit_prev_len(l, l.pos);
    /* Skip non-space characters (move backwards by clusters). */
    while (l.pos > 0 && l.buf[l.pos - 1] != ' ')
        l.pos -= edit_prev_len(l, l.pos);
    size_t diff = old_pos - l.pos;
    adjust_folds_after_delete(l, l.pos, diff);
    l.buf.erase(l.pos, diff);
    refresh_line(l);
}

/* ========================= Non-blocking edit API ========================== */

/* Result of one edit_feed() step. */
enum class EditResult {
    More,   /* Still editing: feed more input. */
    Done,   /* Line completed (ENTER): result holds the line. */
    Eof,    /* Ctrl-D on an empty line, or end of input. */
    Cancel, /* Ctrl-C. */
    Error,  /* I/O error. */
};

/* Remove the temporary "current line" history entry, if active. */
inline void edit_drop_history_entry(State &l) {
    if (l.history_entry_active && !history.empty()) {
        history.pop_back();
        l.history_entry_active = false;
    }
}

/* Start a line editing session: put the terminal in raw mode and show the
 * prompt. After this, feed input with edit_feed() until it returns
 * something different from EditResult::More, then call edit_stop().
 * Returns false (errno=ENOTTY) when the terminal cannot be set up. */
inline bool edit_start(State &l, int stdin_fd, int stdout_fd, const char *prompt) {
    l.in_completion = false;
    l.ifd = stdin_fd != -1 ? stdin_fd : STDIN_FILENO;
    l.ofd = stdout_fd != -1 ? stdout_fd : STDOUT_FILENO;
    l.buf.clear();
    l.prompt = prompt ? prompt : "";
    l.oldpos = l.pos = 0;
    fold_clear(l);

    /* Enter raw mode. */
    raw_mode_ofd = l.ofd;
    if (enable_raw_mode(l.ifd) == -1) return false;

    l.cols = get_columns(l.ifd, l.ofd);
    if (l.cols == 0) l.cols = 80;
    l.oldrows = 0;
    l.oldrpos = 1; /* Cursor starts on row 1. */
    l.history_index = 0;
    active_state = &l;

    /* If stdin is not a tty, stop here with the initialization. We will
     * actually just read a line from standard input in blocking mode
     * later, in edit_feed(). */
    if (!is_tty(l.ifd) && !assume_tty()) return true;

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    history_add("");
    l.history_entry_active = !history.empty();

    if (write_bytes(l.ofd, l.prompt.data(), l.prompt.size()) == -1) return false;
    return true;
}

/* Read a line from stdin with no editing and no length limits, used when
 * the standard input is not a tty (pipe, file redirection...). */
inline EditResult read_no_tty(std::string &out) {
    out.clear();
    if (!std::getline(std::cin, out)) return EditResult::Eof;
    return EditResult::Done;
}

/* Make room and append bytes to the temporary paste buffer. Returns false
 * if the paste would exceed maxlen. */
inline bool paste_buffer_append(std::string &buf, const char *s, size_t slen,
                                size_t maxlen) {
    if (buf.size() > maxlen || slen > maxlen - buf.size()) return false;
    buf.append(s, slen);
    return true;
}

/* Read a bracketed paste until ESC[201~ and insert the real bytes. If
 * folding is needed, remember the inserted range so only rendering is
 * shortened. */
inline void edit_paste(State &l) {
    static const char END[] = "\x1b[201~";
    const size_t ENDLEN = sizeof(END) - 1;
    std::string buf;
    size_t match = 0;
    size_t maxlen = kMaxLine > l.buf.size() ? kMaxLine - l.buf.size() : 0;
    bool overflowed = false;

    if (maxlen > kPasteMaxBytes) maxlen = kPasteMaxBytes;
    /* Once all fold slots are used, consume later pastes without storing
     * them. */
    if (l.folds.size() == kMaxFolds) maxlen = 0;

    while (true) {
        char c;
        if (read_byte(l.ifd, &c) != 1) break;

        /* Track a possible ESC[201~ terminator without copying it into the
         * paste. If it turns out to be ordinary input, flush the partial
         * match below. */
        if (c == END[match]) {
            match++;
            if (match == ENDLEN) break;
            continue;
        }

        if (match > 0) {
            if (!overflowed && !paste_buffer_append(buf, END, match, maxlen))
                overflowed = true;
            match = 0;
            if (c == END[0]) {
                match = 1;
                continue;
            }
        }

        if (!overflowed && !paste_buffer_append(buf, &c, 1, maxlen))
            overflowed = true;
    }

    if (overflowed) {
        beep();
        return;
    }
    if (buf.empty()) return;

    {
        /* Normalize pasted CR and CRLF to LF, so the edit buffer uses one
         * internal newline representation. */
        size_t r = 0, w = 0;
        while (r < buf.size()) {
            if (buf[r] == '\r') {
                buf[w++] = '\n';
                r += (r + 1 < buf.size() && buf[r + 1] == '\n') ? 2 : 1;
            } else {
                buf[w++] = buf[r++];
            }
        }
        buf.resize(w);
    }

    if (!mask_mode && should_fold_text(buf.data(), buf.size())) {
        size_t start = l.pos;
        if (!edit_insert_no_refresh(l, buf.data(), buf.size())) {
            beep();
            return;
        }
        fold_add(l, start, start + buf.size());
        refresh_line(l);
    } else {
        edit_insert(l, buf.data(), buf.size());
    }
}

/* Return the number of bytes of the UTF-8 sequence introduced by byte c. */
inline int utf8_seq_len(char c) {
    auto uc = static_cast<unsigned char>(c);
    if ((uc & 0x80) == 0) return 1;
    if ((uc & 0xE0) == 0xC0) return 2;
    if ((uc & 0xF0) == 0xE0) return 3;
    if ((uc & 0xF8) == 0xF0) return 4;
    return 1; /* Invalid encoding, treat as a single byte. */
}

/* Process one unit of input. Returns EditResult::More while the user is
 * still editing; on EditResult::Done the completed line is stored in
 * 'result'. */
inline EditResult edit_feed(State &l, std::string &result) {
    /* Not a TTY: pass control to line reading without character count
     * limits. */
    if (!is_tty(l.ifd) && !assume_tty()) return read_no_tty(result);

    char c;
    int nread;
    char seq[3];

    nread = read_byte(l.ifd, &c);
    if (nread < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? EditResult::More
                                                         : EditResult::Error;
    } else if (nread == 0) {
        return EditResult::Eof;
    }

    /* Only autocomplete when the callback is set. complete_line() returns
     * the character to be handled next, or zero when the key was consumed
     * to navigate completions. */
    if ((l.in_completion || c == TAB) && completion_callback) {
        int retval = complete_line(l, c);
        /* Read next character when 0 */
        if (retval == 0) return EditResult::More;
        c = static_cast<char>(retval);
    }

    switch (c) {
    case ENTER: /* enter */
        edit_drop_history_entry(l);
        if (ml_mode) edit_move_end(l);
        if (hints_callback) {
            /* Force a refresh without hints to leave the previous line as
             * the user typed it after a newline. */
            HintsCallbackFn hc = std::move(hints_callback);
            hints_callback = nullptr;
            refresh_line(l);
            hints_callback = std::move(hc);
        }
        result = l.buf;
        return EditResult::Done;
    case CTRL_C: /* ctrl-c */
        /* Unlike upstream, drop the temporary history entry here too, so a
         * canceled line never lingers in the history. */
        edit_drop_history_entry(l);
        return EditResult::Cancel;
    case BACKSPACE: /* backspace */
    case CTRL_H:    /* ctrl-h */
        edit_backspace(l);
        break;
    case CTRL_D: /* ctrl-d, remove char at right of cursor, or if the line
                    is empty, act as end-of-file. */
        if (!l.buf.empty()) {
            edit_delete(l);
        } else {
            edit_drop_history_entry(l);
            return EditResult::Eof;
        }
        break;
    case CTRL_T: /* ctrl-t, swaps current character with previous. */
        if (l.pos > 0 && l.pos < l.buf.size()) {
            size_t prevlen = edit_prev_len(l, l.pos);
            size_t currlen = edit_next_len(l, l.pos);
            size_t prevstart = l.pos - prevlen;
            if (range_overlaps_fold(l, prevstart, prevlen + currlen)) {
                beep();
                break;
            }
            /* Swap the two grapheme clusters around the cursor. */
            std::string tmp = l.buf.substr(l.pos, currlen);
            l.buf.erase(l.pos, currlen);
            l.buf.insert(prevstart, tmp);
            if (l.pos + currlen <= l.buf.size()) l.pos += currlen;
            refresh_line(l);
        }
        break;
    case CTRL_B: /* ctrl-b */
        edit_move_left(l);
        break;
    case CTRL_F: /* ctrl-f */
        edit_move_right(l);
        break;
    case CTRL_P: /* ctrl-p */
        edit_history_next(l, kHistoryPrev);
        break;
    case CTRL_N: /* ctrl-n */
        edit_history_next(l, kHistoryNext);
        break;
    case ESC: /* escape sequence */
        /* Read the next two bytes representing the escape sequence. Use two
         * calls to handle slow terminals returning the two chars at
         * different times. */
        if (read_byte(l.ifd, seq) != 1) break;
        if (read_byte(l.ifd, seq + 1) != 1) break;

        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                char param[8];
                size_t plen = 1;
                char final_byte = 0;

                param[0] = seq[1];
                while (plen < sizeof(param)) {
                    char p;
                    if (read_byte(l.ifd, &p) != 1) break;
                    if (p >= '0' && p <= '9') {
                        param[plen++] = p;
                    } else {
                        final_byte = p;
                        break;
                    }
                }
                if (final_byte == '~') {
                    if (plen == 1 && param[0] == '3') {
                        edit_delete(l); /* Delete key. */
                    } else if (plen == 3 && memcmp(param, "200", 3) == 0) {
                        edit_paste(l);
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': /* Up */
                    edit_history_next(l, kHistoryPrev);
                    break;
                case 'B': /* Down */
                    edit_history_next(l, kHistoryNext);
                    break;
                case 'C': /* Right */
                    edit_move_right(l);
                    break;
                case 'D': /* Left */
                    edit_move_left(l);
                    break;
                case 'H': /* Home */
                    edit_move_home(l);
                    break;
                case 'F': /* End */
                    edit_move_end(l);
                    break;
                }
            }
        }

        /* ESC O sequences. */
        else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': /* Home */
                edit_move_home(l);
                break;
            case 'F': /* End */
                edit_move_end(l);
                break;
            }
        }
        break;
    default: {
        /* Handle UTF-8 multi-byte sequences: when we receive the first byte
         * of a multi-byte character, read the remaining bytes before
         * inserting. */
        char utf8[4];
        int utf8len = utf8_seq_len(c);
        utf8[0] = c;
        for (int i = 1; i < utf8len; i++) {
            if (read_byte(l.ifd, utf8 + i) != 1) {
                utf8len = i;
                break;
            }
        }
        edit_insert(l, utf8, static_cast<size_t>(utf8len));
        break;
    }
    case CTRL_U: /* Ctrl+u, delete the whole line. */
        l.buf.clear();
        l.pos = 0;
        fold_clear(l);
        refresh_line(l);
        break;
    case CTRL_K: /* Ctrl+k, delete from current to end of line. */
        adjust_folds_after_delete(l, l.pos, l.buf.size() - l.pos);
        l.buf.erase(l.pos);
        refresh_line(l);
        break;
    case CTRL_A: /* Ctrl+a, go to the start of the line */
        edit_move_home(l);
        break;
    case CTRL_E: /* ctrl+e, go to the end of the line */
        edit_move_end(l);
        break;
    case CTRL_L: /* ctrl+l, clear screen */
        clear_screen();
        refresh_line(l);
        break;
    case CTRL_W: /* ctrl+w, delete previous word */
        edit_delete_prev_word(l);
        break;
    }
    return EditResult::More;
}

/* End an editing session: restore the terminal to normal mode. */
inline void edit_stop(State &l) {
    edit_drop_history_entry(l);
    active_state = nullptr;
    if (!is_tty(l.ifd) && !assume_tty()) return;
    disable_raw_mode(l.ifd);
    write_bytes(l.ofd, "\n", 1);
}

/* Hide the current line. Call Show() to redisplay it. Useful to print
 * asynchronous output without mixing it with the edited line. */
inline void hide(State &l) {
    if (ml_mode)
        refresh_multi_line(l, kRefreshClean);
    else
        refresh_single_line(l, kRefreshClean);
}

/* Show the current line again after hide(). */
inline void show(State &l) {
    if (l.in_completion) {
        refresh_line_with_completion(l, nullptr, kRefreshWrite);
    } else {
        refresh_line_with_flags(l, kRefreshWrite);
    }
}

/* A blocking loop over edit_start()/edit_feed()/edit_stop(). */
inline EditResult blocking_edit(int stdin_fd, int stdout_fd, const char *prompt,
                                std::string &line) {
    State l;
    if (!edit_start(l, stdin_fd, stdout_fd, prompt)) return EditResult::Error;
    EditResult res;
    while ((res = edit_feed(l, line)) == EditResult::More) {
    }
    edit_stop(l);
    return res;
}

/* Cooked-mode fallback for terminals that cannot handle escape sequences:
 * print the prompt and read a plain line. */
inline EditResult cooked_edit(const char *prompt, std::string &line) {
    fputs(prompt, stdout);
    fflush(stdout);
    EditResult res = read_no_tty(line);
    while (!line.empty() && line.back() == '\r') line.pop_back();
    return res;
}

/* The high level line editing entry point: picks raw editing, plain pipe
 * reading or the cooked fallback depending on the terminal. */
inline EditResult edit_line(const char *prompt, std::string &line) {
    line.clear();
    if (!is_tty(STDIN_FILENO) && !assume_tty()) {
        /* Not a tty: read from file / pipe with no line length limits. */
        return read_no_tty(line);
    }
    if (is_unsupported_term()) return cooked_edit(prompt, line);
    return blocking_edit(STDIN_FILENO, STDOUT_FILENO, prompt, line);
}

} // namespace detail

/* ============================= Public API ================================= */

/* Completion callback: receives the current edit buffer and fills the
 * vector with completion candidates. */
using CompletionCallback = detail::CompletionCallbackFn;

/* Hints callback: receives the current edit buffer and returns the hint to
 * display at the right of the cursor (empty string for no hint). 'color'
 * is an ANSI color code (e.g. 35 for magenta, -1 for default), 'bold'
 * selects bold text. */
using HintsCallback = detail::HintsCallbackFn;

/* Register a callback function to be called for tab-completion. */
inline void SetCompletionCallback(CompletionCallback fn) {
    detail::completion_callback = std::move(fn);
}

/* Register a callback function to show hints at the right of the prompt. */
inline void SetHintsCallback(HintsCallback fn) {
    detail::hints_callback = std::move(fn);
}

/* Enable or disable the multi line mode. */
inline void SetMultiLine(bool multi_line_mode) {
    detail::ml_mode = multi_line_mode;
}

/* Enable "mask mode": the terminal displays '*' for every typed character,
 * useful for passwords and other secrets. */
inline void EnableMaskMode() { detail::mask_mode = true; }
inline void DisableMaskMode() { detail::mask_mode = false; }

/* Read a line with full editing. Returns true when the user wants to quit
 * (Ctrl-C, Ctrl-D on an empty line, or end of input). */
inline bool Readline(const char *prompt, std::string &line) {
    auto res = detail::edit_line(prompt, line);
    return res != detail::EditResult::Done;
}

inline std::string Readline(const char *prompt, bool &quit) {
    std::string line;
    quit = Readline(prompt, line);
    return line;
}

inline std::string Readline(const char *prompt) {
    bool quit; /* ignored */
    return Readline(prompt, quit);
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, and will make sure to retain just the
 * latest 'len' elements if the new history length is smaller than the
 * number of items already stored. */
inline bool SetHistoryMaxLen(size_t len) {
    return detail::history_set_max_len(len);
}

/* Add a new entry to the history. */
inline bool AddHistory(const char *line) { return detail::history_add(line); }

/* Save the history in the specified file. */
inline bool SaveHistory(const char *path) { return detail::history_save(path); }

/* Load the history from the specified file. */
inline bool LoadHistory(const char *path) { return detail::history_load(path); }

inline const std::vector<std::string> &GetHistory() { return detail::history; }

/* Clear the screen. */
inline void ClearScreen() { detail::clear_screen(); }

/* Temporarily hide the line being edited (to print asynchronous output),
 * then show it again. Only meaningful while a Readline() call is active. */
inline void HideLine() {
    if (detail::active_state) detail::hide(*detail::active_state);
}

inline void ShowLine() {
    if (detail::active_state) detail::show(*detail::active_state);
}

/* Debugging helper: print scan codes on screen for keyboard debugging. */
inline void PrintKeyCodes() {
    char quit[4];

    printf("Linenoise key codes debugging mode.\n"
           "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    detail::raw_mode_ofd = STDOUT_FILENO;
    if (detail::enable_raw_mode(STDIN_FILENO) == -1) return;
    memset(quit, ' ', 4);
    while (true) {
        char c;
        int nread = detail::read_byte(STDIN_FILENO, &c);
        if (nread <= 0) continue;
        memmove(quit, quit + 1, sizeof(quit) - 1); /* shift string to left. */
        quit[sizeof(quit) - 1] = c; /* Insert current char on the right. */
        if (memcmp(quit, "quit", sizeof(quit)) == 0) break;

        printf("'%c' %02x (%d) (type quit to exit)\n", isprint(c) ? c : '?',
               static_cast<int>(c), static_cast<int>(c));
        printf("\r"); /* Go to left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    detail::disable_raw_mode(STDIN_FILENO);
}

} // namespace linenoise

#endif /* LINENOISE_HPP */
