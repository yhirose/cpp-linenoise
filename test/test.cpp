/* Tests for linenoise.hpp.
 *
 * Covers the Unicode layer (grapheme cluster segmentation against the
 * official Unicode GraphemeBreakTest data, display widths), history
 * handling, and the editing engine driven in-process through pipes using
 * the LINENOISE_ASSUME_TTY / LINENOISE_COLS test hooks.
 */

#include "../linenoise.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define pipe_(fds) _pipe(fds, 65536, _O_BINARY)
#define close_ _close
#define write_ _write
#else
#include <unistd.h>
#define pipe_(fds) pipe(fds)
#define close_ close
#define write_ write
#endif

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(cond)) {                                                       \
            tests_failed++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        tests_run++;                                                         \
        auto va = (a);                                                       \
        decltype(va) vb = (b);                                               \
        if (!(va == vb)) {                                                   \
            tests_failed++;                                                  \
            printf("FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__,        \
                   __LINE__, #a, #b, (long long)va, (long long)vb);          \
        }                                                                    \
    } while (0)

#include "grapheme_break_test.inc"

using namespace linenoise;

/* Compute cluster boundaries (as codepoint indexes) with the library and
 * compare with the expectation from GraphemeBreakTest.txt. */
static void test_grapheme_break_data() {
    int failed_before = tests_failed;
    for (const auto &c : grapheme_break_cases) {
        /* Build the UTF-8 string and remember the byte offset of every
         * codepoint. */
        std::string s;
        std::vector<size_t> cp_offset; /* byte offset of each cp + end */
        for (auto cp : c.cps) {
            cp_offset.push_back(s.size());
            unicode::encode_utf8(s, cp);
        }
        cp_offset.push_back(s.size());

        /* Segment forward and collect cluster start offsets (in cp index). */
        std::vector<size_t> breaks;
        size_t i = 0;
        while (i < s.size()) {
            size_t cp_index =
                std::lower_bound(cp_offset.begin(), cp_offset.end(), i) -
                cp_offset.begin();
            breaks.push_back(cp_index);
            size_t g = unicode::next_grapheme_len(s.data(), i, s.size());
            if (g == 0) break;
            i += g;
        }
        breaks.push_back(c.cps.size());

        tests_run++;
        if (breaks != c.breaks) {
            tests_failed++;
            printf("FAIL grapheme break for cps:");
            for (auto cp : c.cps) printf(" %04X", cp);
            printf("\n  expected:");
            for (auto b : c.breaks) printf(" %zu", b);
            printf("\n  got:     ");
            for (auto b : breaks) printf(" %zu", b);
            printf("\n");
        }

        /* prev_grapheme_len must agree with forward segmentation at every
         * boundary. */
        std::vector<size_t> byte_breaks;
        i = 0;
        while (i < s.size()) {
            byte_breaks.push_back(i);
            i += unicode::next_grapheme_len(s.data(), i, s.size());
        }
        byte_breaks.push_back(s.size());
        for (size_t k = 1; k < byte_breaks.size(); k++) {
            size_t expect = byte_breaks[k] - byte_breaks[k - 1];
            size_t got = unicode::prev_grapheme_len(s.data(), byte_breaks[k]);
            tests_run++;
            if (got != expect) {
                tests_failed++;
                printf("FAIL prev_grapheme_len at %zu for cps:", byte_breaks[k]);
                for (auto cp : c.cps) printf(" %04X", cp);
                printf(" expected %zu got %zu\n", expect, got);
            }
        }
    }
    printf("grapheme break data: %s (%zu cases)\n",
           tests_failed == failed_before ? "ok" : "FAILED",
           grapheme_break_cases.size());
}

static size_t width(const char *s) {
    return unicode::str_width(s, strlen(s));
}

static void test_widths() {
    CHECK_EQ(width(""), 0u);
    CHECK_EQ(width("hello"), 5u);
    CHECK_EQ(width("こんにちは"), 10u);       /* CJK: 2 cols each */
    CHECK_EQ(width("ｱｲｳ"), 3u);               /* halfwidth katakana */
    CHECK_EQ(width("Ａ１"), 4u);              /* fullwidth forms */
    CHECK_EQ(width("한글"), 4u);              /* Hangul syllables */
    CHECK_EQ(width("각"), 2u); /* Hangul jamo L+V+T = 1 block */
    CHECK_EQ(width("é"), 1u);           /* e + combining acute */
    CHECK_EQ(width("à̖́"), 1u); /* multiple combining marks */
    CHECK_EQ(width("👍"), 2u);                /* emoji */
    CHECK_EQ(width("👍🏽"), 2u);                /* emoji + skin tone modifier */
    CHECK_EQ(width("👩‍👩‍👧‍👦"), 2u);                /* ZWJ family sequence */
    CHECK_EQ(width("🇯🇵"), 2u);                /* regional indicator pair */
    CHECK_EQ(width("🇯🇵🇺🇸"), 4u);              /* two flags */
    CHECK_EQ(width("☁️"), 2u);           /* VS16 forces emoji width */
    CHECK_EQ(width("☁"), 1u);                 /* text presentation cloud */
    CHECK_EQ(width("नमस्ते"), 4u); /* Devanagari with conjuncts/matras */
    CHECK_EQ(width("\x1b[32mhi\x1b[0m"), 2u); /* ANSI colors are zero width */
    CHECK_EQ(width("\t"), 0u);                /* control chars */

    /* Grapheme iteration sanity: family emoji is a single cluster. */
    const char *family = "👩‍👩‍👧‍👦x";
    size_t flen = strlen(family);
    CHECK_EQ(unicode::next_grapheme_len(family, 0, flen), flen - 1);
    CHECK_EQ(unicode::prev_grapheme_len(family, flen - 1), flen - 1);
}

static void test_history() {
    detail::history.clear();
    detail::history_set_max_len(3);
    CHECK(AddHistory("one"));
    CHECK(!AddHistory("one")); /* consecutive duplicates rejected */
    CHECK(AddHistory("two"));
    CHECK(AddHistory("three"));
    CHECK(AddHistory("four"));
    CHECK_EQ(GetHistory().size(), 3u);
    CHECK(GetHistory()[0] == "two");
    CHECK(GetHistory()[2] == "four");

    /* Round-trip through a file, including an entry with a newline. */
    CHECK(AddHistory("multi\nline"));
    const char *path = "test_history.tmp";
    CHECK(SaveHistory(path));
    detail::history.clear();
    detail::history_set_max_len(100);
    CHECK(LoadHistory(path));
    CHECK_EQ(GetHistory().size(), 3u);
    CHECK(GetHistory()[2] == "multi\nline");
    remove(path);
}

/* ====================== In-process editing tests ========================= *
 * Drive the editing engine through pipes. LINENOISE_ASSUME_TTY makes the
 * engine treat them as a terminal; LINENOISE_COLS fixes the width.
 */

struct EditHarness {
    int in_fds[2];
    int out_fds[2];
    detail::State l;

    explicit EditHarness(const char *prompt = "> ", const char *input = "") {
        pipe_(in_fds);
        pipe_(out_fds);
        if (*input) feed(input);
        detail::edit_start(l, in_fds[0], out_fds[1], prompt);
    }

    void feed(const std::string &s) { write_(in_fds[1], s.data(), (unsigned)s.size()); }

    /* Run the edit loop until it needs more input than available or
     * completes. Returns the result. */
    detail::EditResult run(std::string &line) {
        close_(in_fds[1]);
        in_fds[1] = -1;
        detail::EditResult r;
        while ((r = detail::edit_feed(l, line)) == detail::EditResult::More) {
        }
        detail::edit_stop(l);
        return r;
    }

    /* Read everything the engine wrote to the terminal. */
    std::string output() {
        close_(out_fds[1]);
        out_fds[1] = -1;
        std::string out;
        char buf[4096];
        for (;;) {
#ifdef _WIN32
            int n = _read(out_fds[0], buf, sizeof(buf));
#else
            ssize_t n = read(out_fds[0], buf, sizeof(buf));
#endif
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }

    ~EditHarness() {
        if (in_fds[1] != -1) close_(in_fds[1]);
        close_(in_fds[0]);
        if (out_fds[1] != -1) close_(out_fds[1]);
        close_(out_fds[0]);
    }
};

static void test_edit_basic() {
    detail::history.clear();
    EditHarness h("> ", "hello\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "hello");
    std::string out = h.output();
    CHECK(out.find("> ") != std::string::npos);
    CHECK(out.find("hello") != std::string::npos);
}

static void test_edit_utf8_backspace() {
    detail::history.clear();
    /* Type "あ👍🏽x", backspace twice: removes "x", then the whole emoji+
     * modifier cluster in ONE keypress. */
    EditHarness h("> ", "あ👍🏽x\x7f\x7f\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "あ");
}

static void test_edit_cursor_cluster_movement() {
    detail::history.clear();
    /* "🇯🇵x" + Left,Left then 'a': cursor must cross the flag pair as a
     * single unit, inserting before it. */
    EditHarness h("> ", "🇯🇵x\x1b[D\x1b[Da\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "a🇯🇵x");
}

static void test_edit_delete_and_word() {
    detail::history.clear();
    /* "foo bar" + Ctrl-W deletes the previous word. */
    EditHarness h("> ", "foo bar\x17\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "foo ");

    /* Home + Delete removes the first cluster. */
    EditHarness h2("> ", "héllo\x1b[H\x1b[3~\r");
    std::string line2;
    CHECK(h2.run(line2) == detail::EditResult::Done);
    CHECK(line2 == "éllo" || line2 == "h́llo"); /* depends on é form */
}

static void test_edit_ctrl_keys() {
    detail::history.clear();
    /* Ctrl-A then Ctrl-K wipes everything right of home. */
    EditHarness h("> ", "wipe me\x01\x0b\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "");

    /* Ctrl-C cancels. */
    EditHarness h2("> ", "abc\x03");
    std::string line2;
    CHECK(h2.run(line2) == detail::EditResult::Cancel);

    /* Ctrl-D on empty line is EOF. */
    EditHarness h3("> ", "\x04");
    std::string line3;
    CHECK(h3.run(line3) == detail::EditResult::Eof);

    /* Ctrl-T swaps clusters (works with wide chars). */
    EditHarness h4("> ", "あい\x1b[D\x14\r");
    std::string line4;
    CHECK(h4.run(line4) == detail::EditResult::Done);
    CHECK(line4 == "いあ");
}

static void test_edit_history_navigation() {
    detail::history.clear();
    AddHistory("first");
    AddHistory("second");
    /* Up,Up,Down recalls "first" then back to "second". */
    EditHarness h("> ", "\x1b[A\x1b[A\x1b[B\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "second");
    /* The temporary editing entry must be gone. */
    CHECK_EQ(detail::history.size(), 2u);
}

static void test_edit_completion() {
    detail::history.clear();
    SetCompletionCallback([](const char *buf, std::vector<std::string> &out) {
        if (buf[0] == 'h') {
            out.push_back("hello");
            out.push_back("hello there");
        }
    });
    /* 'h' + TAB selects "hello", ENTER accepts it. */
    EditHarness h("> ", "h\t\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "hello");

    /* TAB TAB selects the second candidate. */
    EditHarness h2("> ", "h\t\t\r");
    std::string line2;
    CHECK(h2.run(line2) == detail::EditResult::Done);
    CHECK(line2 == "hello there");
    SetCompletionCallback(nullptr);
}

static void test_edit_bracketed_paste_fold() {
    detail::history.clear();
    /* A multi-line bracketed paste is stored verbatim (CRLF normalized to
     * LF) and rendered folded. */
    EditHarness h("> ", "\x1b[200~line1\r\nline2\r\nline3\x1b[201~\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "line1\nline2\nline3");
    std::string out = h.output();
    CHECK(out.find("pasted lines") != std::string::npos);
    /* Raw newlines from the paste must never be echoed to the display. */
    CHECK(out.find("line1\nline2") == std::string::npos);
}

static void test_edit_mask_mode() {
    detail::history.clear();
    EnableMaskMode();
    EditHarness h("> ", "secret🔑\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    DisableMaskMode();
    CHECK(line == "secret🔑");
    std::string out = h.output();
    CHECK(out.find("secret") == std::string::npos);
    CHECK(out.find("*******") != std::string::npos); /* 7 clusters */
}

static void test_edit_hints() {
    detail::history.clear();
    SetHintsCallback([](const char *buf, int &color, bool &bold) -> std::string {
        color = 35;
        bold = false;
        if (strcmp(buf, "git") == 0) return " <command>";
        return "";
    });
    EditHarness h("> ", "git\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "git");
    std::string out = h.output();
    CHECK(out.find(" <command>") != std::string::npos);
    SetHintsCallback(nullptr);
}

static void test_edit_wide_line_scroll() {
    detail::history.clear();
    /* Single-line mode with 20 columns: typing past the edge must scroll
     * horizontally without breaking cluster boundaries (no crash, correct
     * result). */
    EditHarness h("> ", "あいうえおかきくけこさしすせそ\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    CHECK(line == "あいうえおかきくけこさしすせそ");
}

static void test_newline_conventions() {
    detail::history.clear();
    SetNewlineConventions(NEWLINE_ALT_ENTER | NEWLINE_BACKSLASH_ENTER |
                          NEWLINE_SPACE_ENTER | NEWLINE_LF);
    SetContinuationPrompt(". ");

    /* Space + Enter inserts a newline; final Enter submits everything. */
    {
        EditHarness h("> ", "ab \rcd\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "ab \ncd");
        /* The continuation prompt must be rendered. */
        std::string out = h.output();
        CHECK(out.find("\r\n. ") != std::string::npos);
    }

    /* Backslash + Enter: the backslash is removed. */
    {
        EditHarness h("> ", "ab\\\rcd\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "ab\ncd");
    }

    /* Alt+Enter (ESC CR). */
    {
        EditHarness h("> ", "ab\x1b\rcd\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "ab\ncd");
    }

    /* Raw LF (Ctrl-J / remapped Shift+Enter). */
    {
        EditHarness h("> ", "ab\ncd\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "ab\ncd");
    }

    /* With NEWLINE_LF disabled, a raw LF submits like Enter. */
    {
        SetNewlineConventions(0);
        EditHarness h("> ", "ab\n");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "ab");
    }

    /* Backspace across the newline merges the lines and cleans up the
     * extra row. */
    {
        SetNewlineConventions(NEWLINE_LF);
        EditHarness h("> ", "ab\ncd\x7f\x7f\x7f\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "ab");
    }

    SetContinuationPrompt("");
    SetNewlineConventions(NEWLINE_ALT_ENTER | NEWLINE_LF); /* defaults */
}

static void test_multiline_cursor_movement() {
    detail::history.clear();
    AddHistory("old entry");
    SetNewlineConventions(NEWLINE_LF);

    /* Compose two lines, press Up (into the first line), then type. The
     * cursor lands at the same display column on the first line. */
    {
        EditHarness h("> ", "abcd\nxy\x1b[AZ\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "abZcd\nxy");
    }

    /* Up on the first line still recalls history. */
    {
        EditHarness h("> ", "\x1b[A\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "old entry");
    }

    /* Down from the first line of a two-line buffer moves to the second
     * line; target column is clamped to the line length. */
    {
        EditHarness h("> ", "abcd\nx\x1b[A\x1b[BZ\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "abcd\nxZ");
    }

    /* Wide characters: column mapping counts display cells, so moving up
     * from after "x" (col 4) on line 2 lands after "あい" (col 4). */
    {
        EditHarness h("> ", "あいう\nFGHx\x1b[AZ\r");
        std::string line;
        CHECK(h.run(line) == detail::EditResult::Done);
        CHECK(line == "あいZう\nFGHx");
    }

    SetNewlineConventions(NEWLINE_ALT_ENTER | NEWLINE_LF); /* defaults */
}

static void test_multiline_mode() {
    detail::history.clear();
    SetMultiLine(true);
    EditHarness h("> ", "wrap around the twenty column terminal\r");
    std::string line;
    CHECK(h.run(line) == detail::EditResult::Done);
    SetMultiLine(false);
    CHECK(line == "wrap around the twenty column terminal");
}

int main() {
#ifdef _WIN32
    _putenv("LINENOISE_ASSUME_TTY=1");
    _putenv("LINENOISE_COLS=20");
#else
    setenv("LINENOISE_ASSUME_TTY", "1", 1);
    setenv("LINENOISE_COLS", "20", 1);
#endif

    test_grapheme_break_data();
    test_widths();
    test_history();
    test_edit_basic();
    test_edit_utf8_backspace();
    test_edit_cursor_cluster_movement();
    test_edit_delete_and_word();
    test_edit_ctrl_keys();
    test_edit_history_navigation();
    test_edit_completion();
    test_edit_bracketed_paste_fold();
    test_edit_mask_mode();
    test_edit_hints();
    test_edit_wide_line_scroll();
    test_newline_conventions();
    test_multiline_cursor_movement();
    test_multiline_mode();

    printf("%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
