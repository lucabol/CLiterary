/**
% Port of LLite in C
% Luca Bolognese
% 31/12/2012
**/

/**
Main ideas
==========

This is a port of [LLIte](https://github.com/lucabol/LLite/blob/master/Program.fs) in C. The reason for it is to
experiment with writing functional code in standard C and compare the experience with using a functional language
like F#
**/

/**
Cheating
--------

I will be using glib and an header of convenient macros/functions to help me. I don't think that is cheating.
A modern C praticoner has its bag of tricks.
**/

#ifdef PRINTMEM
#include <windows.h>
#include <psapi.h>
#endif

#include <string.h>
#include <stdbool.h>

#include <glib.h>
#include <glib/gprintf.h>
#include <arena.h>

#include "lutils.h"

/**
Lack of tuples
--------------

In the snippet below I overcomed such deficiency by declaring a struct. Using the new constructor syntax makes
initializing a static table simple.
**/

typedef struct LangSymbols { char language[40]; char start[10]; char end[10];} LangSymbols;

static
LangSymbols* s_lang_params_table[] = {
    &(LangSymbols) {.language = "fsharp", .start = "(*" "*", .end = "*" "*)"},
    &(LangSymbols) {.language = "c", .start = "/*" "*", .end = "*" "*/"},
    &(LangSymbols) {.language = "csharp", .start = "/*" "*", .end = "*" "*/"},
    &(LangSymbols) {.language = "java", .start = "/*" "*", .end = "*" "*/"},
    NULL
};

/**
Folding over arrays
-------------------

I also need to gather all the languages, aka perform a fold over the queue. You might have noticed my propensity
to add a `NULL` terminator marker to arrays (as for strings). This allows me to avoid passing a size to functions
and allows writing utility macros (as `foreach` below) more simply.

In the rest of this program, every time I end a function with `_z`, it is because I consider it generally
usable and I add a version of it without the `_z` to lutils.h.
**/

#define array_foreach_z(p) for(; *symbols != NULL; ++symbols)

static
char* summary(LangSymbols** symbols) {

    GString* langs = g_string_sized_new(20);
    array_foreach(symbols) g_string_append_printf(langs, "%s ", (*symbols)->language);

    g_string_truncate(langs, strlen(langs->str) - 1);

    GString* usage = g_string_sized_new(100);

    g_string_printf(usage,
        "You should specify:\n\t. either -l or -o and -p\n"
        "\t. either -indent or -P and -C\n"
        "\t. -l supports: %s"
        ,langs->str);

    return usage->str;
}

/**
Find an item in an array based on some expression. Returns NULL if not found. Again, this is a common task,
hence I'll abstract it out with a macro (that ends up being a cute use of gcc statment expressions).
**/

#define array_find_z(arr, ...)                          \
    ({                                                  \
        array_foreach(arr) if (__VA_ARGS__) break;      \
        *arr;                                           \
    })

static
LangSymbols* lang_find_symbols(LangSymbols** symbols, char* lang) {
    g_assert(symbols);
    g_assert(lang);

    return array_find(symbols, !strcmp((*symbols)->language, lang));
}

/**
Deallocating stuff
------------------

You might wonder why I don't seem overly worried about deallocating the memory that I allocate.
I haven't gone crazy(yet). You'll see.

Discriminated unions
--------------------

Here are the discriminated unions macros from a previous blog post of mine. I'll need a couple of these
and pre-declare two functions.

**/

union_decl(CodeSymbols, Indented, Surrounded)
    union_type(Indented,    int indentation;)
    union_type(Surrounded,  char* start_code; char* end_code;)
union_end(CodeSymbols);

typedef struct Options {
    char*           start_narrative;
    char*           end_narrative;
    CodeSymbols*    code_symbols;
} Options;

static
gchar* translate(Options*, gchar*);

union_decl(Block, Code, Narrative)
    union_type(Code,        char* code)
    union_type(Narrative,   char* narrative)
union_end(Block);

/**
Main data structure
-------------------

We want to use higher level abstractions that standard C arrays, hence we'll pick a convinient data structure
to use in the rest of the code. A queue allows you to insert at the front and back, with just a one pointer
overhead over a single linked list. Hence it is my data structure of choice for this program.
**/

static
GQueue* blockize(Options*, char*);

/**
There is already a function in glib to check if a string has a certain prefix (`g_str_has_prefix`). We need one
that returns the remaining string after the prefix. We also define a g_slow_assert that is executed just if
G_ENABLE_SLOW_ASSERT is defined
**/

static
char* str_after_prefix(char* src, char* prefix) {
    g_assert(src);
    g_assert(prefix);
    g_slow_assert(g_str_has_prefix(src, prefix));

    while(*prefix != '\0')
        if(*src == *prefix) ++src, ++prefix;
        else break;

    return src;
}

/**
Tokenizer
---------

The structure of the function is identical to the F# version. The big bread-winners are statement expressions ...
**/

#define NL "\n"

union_decl(Token, OpenComment, CloseComment, Text)
    union_type(OpenComment, int line)
    union_type(CloseComment,int line)
    union_type(Text,        char* text)
union_end(Token);

GQueue* tokenize(Options* options, char* source) {
    g_assert(options);
    g_assert(source);

    struct tuple { int line; GString* acc; char* rem;};

    bool is_opening(char* src)          { return g_str_has_prefix(src, options->start_narrative);}
    bool is_closing(char* src)          { return g_str_has_prefix(src, options->end_narrative);}
    char* remaining_open (char* src)    { return str_after_prefix(src, options->start_narrative);}
    char* remaining_close(char* src)    { return str_after_prefix(src, options->end_narrative);}

    struct tuple text(char* src, GString* acc, int line) {
        inline struct tuple stop_parse_text() { return (struct tuple) {.line = line, .acc = acc, .rem = src};}

        return  str_empty (src)? stop_parse_text() :
                is_opening(src)? stop_parse_text() :
                is_closing(src)? stop_parse_text() :
                                ({
                                  int line2         = g_str_has_prefix(src, NL) ? line + 1 : line;
                                  GString* newAcc   = g_string_append_c(acc, *src);
                                  char* rem         = src + 1;
                                  text(rem, newAcc, line2);
                                });
    }

    GQueue* tokenize_rec(char* src, GQueue* acc, int line) {
        return  str_empty(src)  ?   acc                     :
                is_opening(src) ?   tokenize_rec(remaining_open(src),
                                               g_queue_push_back(acc, union_new(Token, OpenComment, .line = line)),
                                               line)        :
                is_closing(src) ?   tokenize_rec(remaining_close(src),
                                               g_queue_push_back(acc, union_new(Token, CloseComment, .line = line)),
                                               line)        :
                                ({
                                    struct tuple t = text(src, g_string_sized_new(200), line);
                                    tokenize_rec(t.rem,
                                        g_queue_push_back(acc, union_new(Token, Text, .text = t.acc->str)), t.line);
                                 });
    }

    return tokenize_rec(source, g_queue_new(), 1);
}

/**
Parser
------

This has a similar structure as the F# version, just longer. It is very long because it contains 3 nested functions. Having all
of these close to each other makes the difference in concicesness between F# and C more apparent.

The creation of a `error` macro is unfortunate. I just don't know how to adapt `g_assert_e` so that it works for not pointer returning functions.

I also need a simple function `report_error` to exit gracefully giving a message to the user. I didn't found such thing in glib (?)
**/

#define report_error_z(...) G_STMT_START { g_print(__VA_ARGS__); exit(1); } G_STMT_END                                                            \

union_decl(Chunk, NarrativeChunk, CodeChunk)
    union_type(NarrativeChunk,  GQueue* tokens)
    union_type(CodeChunk,       GQueue* tokens)
union_end(Chunk);

static
GQueue* parse(Options* options, GQueue* tokens) {
    g_assert(options);
    g_assert(tokens);

    struct tuple { GQueue* acc; GQueue* rem;};

    #define error(...) ({ report_error(__VA_ARGS__); (struct tuple) {.acc = NULL, .rem = NULL}; })

    struct tuple parse_narrative(GQueue* acc, GQueue* rem) {

        bool isEmpty    = g_queue_is_empty(rem);
        Token* h        = g_queue_pop_head(rem);
        GQueue* t       = rem;

        return  isEmpty                 ? error("You haven't closed your last narrative comment")   :
                h->kind == OpenComment  ?
                    error("Don't open narrative comments inside narrative comments at line %i", h->OpenComment.line) :
                h->kind == CloseComment ? (struct tuple) {.acc = acc, .rem = t}                     :
                h->kind == Text         ? parse_narrative(g_queue_push_back(acc, h), t)             :
                                          error("Should never get here");
    };

    struct tuple parse_code(GQueue* acc, GQueue* rem) {

        bool isEmpty    = g_queue_is_empty(rem);
        Token* h    = g_queue_pop_head(rem);
        GQueue* t   = rem;

        return  isEmpty                 ? (struct tuple) {.acc = acc, .rem = t}                             :
                h->kind == OpenComment  ? (struct tuple) {.acc = acc, .rem = g_queue_push_front(rem, h)}    :
                h->kind == CloseComment ? parse_code(g_queue_push_back(acc, h), rem)                        :
                h->kind == Text         ? parse_code(g_queue_push_back(acc, h), rem)                        :
                                          error("Should never get here");
    };
    #undef error

    GQueue* parse_rec(GQueue* acc, GQueue* rem) {

        bool isEmpty    = g_queue_is_empty(rem);
        Token* h    = g_queue_pop_head(rem);
        GQueue* t   = rem;

        return  isEmpty                 ? acc                                                               :
                h->kind == OpenComment  ? ({
                                           GQueue* emp = g_queue_new();
                                           struct tuple tu = parse_narrative(emp, t);
                                           Chunk* ch = union_new(Chunk, NarrativeChunk, .tokens = tu.acc );
                                           GQueue* newQ = g_queue_push_back(acc, ch);
                                           parse_rec(newQ, tu.rem);
                                           })                                                              :
                h->kind == CloseComment ? report_error_e("Don't insert a close narrative comment at the start of your program at line %i",
                                                h->OpenComment.line)                                        :
                h->kind == Text         ?
                                        ({
                                           GQueue* emp = g_queue_new();
                                           struct tuple tu = parse_code(g_queue_push_front(emp, h), t);
                                           parse_rec(g_queue_push_back(acc, union_new(Chunk, CodeChunk, .tokens = tu.acc )),
                                                     tu.rem);
                                          })                                                               :
                                          g_assert_no_match;
    }

    return parse_rec(g_queue_new(), tokens);
}

/**
Flattener
---------

This follows the usual practice of representing fold as foreach statments (and maps to). Pheraps I shall build
abstractions for them. I also introduce a little macro to simplify writing of GFunc lambdas, given how pervasive
they are.

**/

#define g_func_z(type, name, ...) lambda(void, (void* private_it, G_GNUC_UNUSED void* private_no){       \
                                       type name = private_it;                                         \
                                       __VA_ARGS__                                                     \
                                })

static
GQueue* flatten(Options* options, GQueue* chunks) {
    GString* token_to_string_narrative(Token* tok) {
        return  tok->kind == OpenComment ||
                tok->kind == CloseComment   ?
                    report_error_e("Cannot nest narrative comments at line %i", tok->OpenComment.line)   :
                tok->kind == Text           ? g_string_new(tok->Text.text)                          :
                                              g_assert_no_match;
    }
    GString* token_to_string_code(Token* tok) {
        return  tok->kind == OpenComment    ?
                    report_error_e("Open narrative comment cannot be in code at line %i. Pheraps you have an open comment "
                              "in a code string before this comment tag?"
                              , tok->OpenComment.line)                                              :
                tok->kind == CloseComment   ? g_string_new(options->end_narrative)                  :
                tok->kind == Text           ? g_string_new(tok->Text.text)                          :
                                              g_assert_no_match;
    }
    Block* flatten_chunk(Chunk* ch) {
        return  ch->kind == NarrativeChunk  ? ({
                               GQueue* tokens = ch->NarrativeChunk.tokens;
                               GString* res = g_string_sized_new(256);
                               g_queue_foreach(tokens, g_func(Token*, tok,
                                                              g_string_append(res, token_to_string_narrative(tok)->str);
                                                              ), NULL);
                               union_new(Block, Narrative, .narrative = res->str);
                                               })   :
                ch->kind == CodeChunk       ? ({
                               GQueue* tokens = ch->CodeChunk.tokens;
                               GString* res = g_string_sized_new(256);
                               g_queue_foreach(tokens, g_func(Token*, tok,
                                                              g_string_append(res, token_to_string_code(tok)->str);
                                                              ), NULL);
                               union_new(Block, Code, .code = res->str);
                                               })   :
                               g_assert_no_match;
    }

    GQueue* res = g_queue_new();
    g_queue_foreach(chunks, g_func(Chunk*, ch,
                                Block* b = flatten_chunk(ch);
                                g_queue_push_tail(res, b);
                                ) ,NULL);
    return res;
}

/**
Now we can tie everything together to build blockize, which is our parse tree.
**/

static
GQueue* blockize(Options* options, char* source) {
    GQueue* tokens  = tokenize(options, source);
    GQueue* blocks  = parse(options, tokens);
    return flatten(options, blocks);
}

/**
Define the phases
-----------------

In C you can easily forward declare function, so you don't have to come up with some clever escabotage like we had to do in F#.
**/

static
GQueue* remove_empty_blocks(Options*, GQueue*);
static
GQueue* merge_blocks(Options*, GQueue*);
static
GQueue* add_code_tags(Options*, GQueue*);

static
GQueue* process_phases(Options* options, GQueue* blocks) {

    blocks          = remove_empty_blocks(options, blocks);
    blocks          = merge_blocks(options, blocks);
    blocks          = add_code_tags(options, blocks);
    return blocks;
}

static
char* extract(Block* b) {
    return  b->kind == Code         ? b->Code.code          :
            b->kind == Narrative    ? b->Narrative.narrative:
                                      g_assert_no_match;
}

/**
There must be a higher level way to write this ...
**/

static
bool is_str_all_spaces(const char* str) {
    g_assert(str);
    while(*str != '\0') {
        if(!g_ascii_isspace(*str))
            return false;
        str++;
    }
    return true;
}

static
GQueue* remove_empty_blocks(G_GNUC_UNUSED Options* options, GQueue* blocks) {

    g_queue_foreach(blocks, g_func(Block*, b,
        if(is_str_all_spaces(extract(b)))
            g_queue_remove(blocks, b);
                                   ), NULL);
    return blocks;
}

static
GQueue* merge_blocks(G_GNUC_UNUSED Options*options, GQueue* blocks) {
    return  g_queue_is_empty(blocks)            ? blocks            :
            g_queue_get_length(blocks) == 1     ? blocks            :
                ({
                 Block* h1 = g_queue_pop_head(blocks);
                 Block* h2 = g_queue_pop_head(blocks);
                 h1->kind == Code && h2->kind == Code ? ({
                     char* newCode = g_strjoin("", h1->Code.code, NL, h2->Code.code, NULL);
                     Block* b = union_new(Block, Code, .code = newCode);
                     merge_blocks(options, g_queue_push_front(blocks, b));
                                                         })         :
                 h1->kind == Narrative && h2->kind == Narrative ? ({
                     char* newNarr = g_strjoin("", h1->Narrative.narrative, NL, h2->Narrative.narrative, NULL);
                     Block* b = union_new(Block, Narrative, .narrative = newNarr);
                     merge_blocks(options, g_queue_push_front(blocks, b));
                                                         })         :
                                                         ({
                     GQueue* newBlocks = merge_blocks(options, g_queue_push_front(blocks, h2));
                     g_queue_push_front(newBlocks, h1);
                                                         });
                 });
}

/**
This really should be in glib ...
**/

inline static
gint g_asprintf_z(gchar** string, gchar const *format, ...) {
	va_list argp;
	va_start(argp, format);
	gint bytes = g_vasprintf(string, format, argp);
	va_end(argp);
    return bytes;
}

static
char* indent(int n, char* s) {
    g_assert(s);

    char* ind       = g_strnfill(n, ' ');
    char* tmp;
    g_asprintf(&tmp, "%s%s", ind, s);

    char* withNl;
    g_asprintf(&withNl, "\n%s", ind);

    return g_strjoinv(withNl, g_strsplit(tmp, NL, -1));
}

/**
And finally a definition for map. See if you like how the usage looks in the function below.
**/

#define g_queue_map_z(q, type, name, ...) ({                                                                     \
        GQueue* private_res = g_queue_new();                                                                   \
        g_queue_foreach(q, g_func(type, name,                                                                   \
            name = __VA_ARGS__;                                                                                 \
            g_queue_push_tail(private_res, name);                                                               \
            ), NULL);                                                                                           \
        private_res;                                                                                            \
                                      })

static
GQueue* add_code_tags(Options* options, GQueue* blocks) {

    GQueue* indent_blocks(GQueue* blocks) {
        return g_queue_map(blocks, Block*, b,
                b->kind == Narrative ? b                                                                                                    :
                b->kind == Code      ? union_new(Block, Code, .code = indent(options->code_symbols->Indented.indentation, b->Code.code))    :
                                       g_assert_no_match;);
    }

    GQueue* surround_blocks(GQueue* blocks) {
        return g_queue_map(blocks, Block*, b,
                b->kind == Narrative ?
                    union_new(Block, Narrative, .narrative = g_strjoin("", NL, g_strstrip(b->Narrative.narrative), NL, NULL))   :
                b->kind == Code      ?
                    union_new(Block, Code, .code = g_strjoin("",
                                                             NL,
                                                             options->code_symbols->Surrounded.start_code,
                                                             NL,
                                                             g_strstrip(b->Code.code),
                                                             NL,
                                                             options->code_symbols->Surrounded.end_code,
                                                             NL,
                                                             NULL))    :
                                       g_assert_no_match;);

    }

    return  options->code_symbols->kind == Indented     ?   indent_blocks(blocks)   :
            options->code_symbols->kind == Surrounded   ?   surround_blocks(blocks) :
                                                            g_assert_no_match;
}

char* stringify(GQueue* blocks) {
    GString* res = g_string_sized_new(2048);
    g_queue_foreach(blocks, g_func(Block*, b,
        g_string_append(res, extract(b));
    ), NULL);
    return g_strchug(res->str);
}

void deb(GQueue* q);

static
char* translate(Options* options, char* source) {
    g_assert(options);
    g_assert(source);

    GQueue* blocks  = blockize(options, source);
    blocks          = process_phases(options, blocks);
    return stringify(blocks);
}

/**
Parsing the command line
------------------------

In glib there is a command line parser that accept options in unix-like format and automatically produces professional
`--help` messages and such. We shoudl really have something like this in .NET.
**/

typedef struct CmdOptions { char* input_file; char* output_file; Options* options;} CmdOptions;

static
CmdOptions* parse_command_line(int argc, char* argv[]);

static char *no = NULL, *nc = NULL, *l = NULL, *co = NULL, *cc = NULL, *ou = NULL;
static char** in_file;

static int ind = 0;
static bool tests = false;

// this is a bug in gcc, fixed in 2.7.0 not to moan about the final NULL
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static GOptionEntry entries[] =
{
  { "language"          , 'l', 0, G_OPTION_ARG_STRING, &l , "Language used",                        "L" },
  { "output"            , 'o', 0, G_OPTION_ARG_FILENAME, &ou, "Defaults to the input file name with mkd extension", "FILE"},
  { "narrative-open"    , 'p', 0, G_OPTION_ARG_STRING, &no, "String opening a narrative comment",   "NO" },
  { "narrative-close"   , 'c', 0, G_OPTION_ARG_STRING, &nc, "String closing a narrative comment",   "NC" },
  { "code-open"         , 'P', 0, G_OPTION_ARG_STRING, &co, "String opening a code block",          "CO" },
  { "code-close"        , 'C', 0, G_OPTION_ARG_STRING, &cc, "String closing a code block",          "CC" },
  { "indent"            , 'i', 0, G_OPTION_ARG_INT,    &ind, "Indent the code by N whitespaces",    "N" },
  { "run-tests"         , 't', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE,   &tests, "Run all the testcases",               NULL },
  { G_OPTION_REMAINING  ,   0, 0, G_OPTION_ARG_FILENAME_ARRAY, &in_file, "Input file to process",      "FILE" },
  { NULL }
};
#pragma GCC diagnostic pop

/**
Brain damaged way to run tests with a `-t` hidden option. Not paying the code size price in release.
**/

#ifndef NDEBUG
#include "tests.c"
#endif

/**
Here is my big ass command parsing function. It could use a bit of refactoring ...
**/

static
CmdOptions* parse_command_line(int argc, char* argv[]) {

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new ("- translate source code with comemnts to an annotated file");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_set_summary(context, summary(s_lang_params_table));

    if (!g_option_context_parse (context, &argc, &argv, &error))
        report_error("option parsing failed: %s", error->message);

    CmdOptions* opt = g_new(CmdOptions, 1);
    opt->options = g_new(Options, 1);

    #ifndef NDEBUG
    if(tests) run_tests(argc, argv);
    #endif

    if(!in_file) report_error("No input file");
    opt->input_file = *in_file;

    // Uses input file without extension, adding extension .mkd (assume markdown)
    opt->output_file = ou ? ou :  ({
                                  char* output      = g_strdup(*in_file);
                                  char* extension   = g_strrstr(output, ".");
                                  extension ? ({
                                               *extension = '\0';
                                               g_strjoin("", output, ".mkd", NULL);
                                                }) :
                                               g_strjoin("", output, ".mkd", NULL);
                                  });

    if(l) { // user passed a language
        LangSymbols* lang = lang_find_symbols(s_lang_params_table, l);
        if(!lang) report_error("%s is not a supported language", l);

        opt->options->start_narrative  = lang->start;
        opt->options->end_narrative    = lang->end;

    } else {
        if(!no || !nc) report_error("You need to specify either -l, or both -p and -c");

        opt->options->start_narrative  = no;
        opt->options->end_narrative    = nc;
    }

    if(ind) { // user passed indent
        opt->options->code_symbols = union_new(CodeSymbols, Indented, .indentation = ind);
    } else {
        if(!co || !cc) report_error("You need to specify either -indent, or both -P and -C");
        opt->options->code_symbols = union_new(CodeSymbols, Surrounded, .start_code = co, .end_code = cc);
    }

    return opt;

}
/**
Some windows programs (i.e. notepad, VS, ...) add a 3 bytes prelude to their utf-8 files, C doesn't know
anything about it, so you need to strip it. On this topic, I suspect the program works on UTF-8 files
that contain non-ASCII chars, even if when I wrote it I didn't know anything about localization.

It should work because I'm just splitting the file when I see a certain ASCII string and in UTF-8 ASCII chars
cannot appear anywhere else than in their ASCII position.
**/

char* skip_utf8_bom(char* str) {
    unsigned char* b = (unsigned char*) str;
    return  b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF    ? (char*) &b[3]  : // UTF-8
                                                              (char*) b;
}

#ifdef PRINTMEM
void PrintMemoryInfo()
{
    typedef BOOL (__stdcall *foo)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    LPTSTR  ptcPSAPI_DLL = "C:\\WINDOWS\\system32\\psapi.dll";
    HMODULE   hPSAPI_DLL = LoadLibrary(ptcPSAPI_DLL);

    if( !hPSAPI_DLL ) g_error("ERROR: Failed to load ");

    foo GetProcessMemoryInfo=(foo)GetProcAddress(hPSAPI_DLL, "GetProcessMemoryInfo");

    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    hProcess = GetCurrentProcess();
    g_assert(hProcess);

    if ( GetProcessMemoryInfo( hProcess, &pmc, sizeof(pmc)) )
    {
        float bytesInMeg = 1048576;
        //printf( "\tPageFaultCount: %d\n", pmc.PageFaultCount / bytesInMeg );
        printf( "\tPeakWorkingSetSize: %f\n", pmc.PeakWorkingSetSize / bytesInMeg );
        printf( "\tWorkingSetSize: %f\n", pmc.WorkingSetSize / bytesInMeg);
        //printf( "\tQuotaPeakPagedPoolUsage: %d\n", pmc.QuotaPeakPagedPoolUsage );
        //printf( "\tQuotaPagedPoolUsage: %d\n", pmc.QuotaPagedPoolUsage );
        //printf( "\tQuotaPeakNonPagedPoolUsage: %d\n", pmc.QuotaPeakNonPagedPoolUsage );
        //printf( "\tQuotaNonPagedPoolUsage: %d\n", pmc.QuotaNonPagedPoolUsage );
        //printf( "\tPagefileUsage: %d\n", pmc.PagefileUsage );
        //printf( "\tPeakPagefileUsage: %d\n", pmc.PeakPagefileUsage );
    }

    CloseHandle( hProcess );
}
#endif

static Arena_T* the_arena;

static
gpointer arena_malloc(gsize n_bytes) {
    return Arena_alloc(*the_arena, n_bytes, __FILE__, __LINE__);
}

static
gpointer arena_calloc(gsize n_blocks, gsize    n_block_bytes) {
    return Arena_calloc(*the_arena, n_blocks, n_block_bytes, __FILE__, __LINE__);
}

/**
This is wrong in the case where mem points to a block of memory smaller than n_bytes and close to the end
of the arena. In such case, memmove reads from unaddressable memory and copies that garbage in n_bytes.
**/

static
gpointer arena_realloc(gpointer mem, gsize n_bytes) {
    gpointer newBlock = Arena_alloc(*the_arena, n_bytes, __FILE__, __LINE__);
    if(mem) memmove(newBlock, mem, n_bytes);
    return newBlock;
}

void arena_free(G_GNUC_UNUSED gpointer mem) {
    // NOP
}

void set_arena_allocator() {
    GMemVTable vt = (GMemVTable) { .malloc = arena_malloc, .calloc = arena_calloc, .realloc = arena_realloc, .free = arena_free};
    g_mem_set_vtable(&vt);

    the_arena = malloc(sizeof(Arena_T));
    *the_arena = Arena_new();
}

void destroy_arena_allocator() {
    Arena_free(*the_arena);
    Arena_dispose(the_arena);
}

int main(int argc, char* argv[])
{
    set_arena_allocator();

    CmdOptions* opt = parse_command_line(argc, argv);

    char* source    = NULL;
    GError* error   = NULL;

    if(!g_file_get_contents(opt->input_file, &source, NULL, &error))
        report_error(error->message);

    source = skip_utf8_bom(source);

    char* text              = translate(opt->options, source);

    if(!g_file_set_contents(opt->output_file, text, -1, &error))
        report_error(error->message);

    destroy_arena_allocator();

    //PrintMemoryInfo();
    return 0;
}
