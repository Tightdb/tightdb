#include <cstddef>
#include <cstring>
#include <string>
#include <iostream>

#include <tightdb/config.h>

#define TO_STR(x) TO_STR2(x)
#define TO_STR2(x) #x

using namespace std;


namespace {

enum Func {
    func_EmitFlags,
    func_ShowVersion,
    func_ShowPrefix,
    func_ShowExecPrefix,
    func_ShowIncludedir,
    func_ShowBindir,
    func_ShowLibdir
};

bool emit_cflags  = false;
bool emit_ldflags = false;

void clear_emit_flags()
{
    emit_cflags  = false;
    emit_ldflags = false;
}

bool dirty = false;

void emit_flags(const char*str)
{
    if (dirty)
        cout << ' ';
    cout << str;
    dirty = true;
}

void flush()
{
    if (dirty)
        cout << '\n';
}

void emit_flags()
{
    if (emit_cflags) {
#ifdef TIGHTDB_HAVE_CONFIG
        emit_flags("-DTIGHTDB_HAVE_CONFIG");
#endif

#ifdef TIGHTDB_ENABLE_REPLICATION
        emit_flags("-DTIGHTDB_ENABLE_REPLICATION");
#endif

        if (TIGHTDB_MAX_LIST_SIZE != TIGHTDB_DEFAULT_MAX_LIST_SIZE)
            emit_flags("-DTIGHTDB_MAX_LIST_SIZE=" TO_STR(TIGHTDB_MAX_LIST_SIZE));

#ifdef TIGHTDB_DEBUG
        emit_flags("-DTIGHTDB_DEBUG");
#endif
    }


    if (emit_ldflags) {
#ifdef TIGHTDB_CONFIG_IOS
#  ifdef TIGHTDB_DEBUG
        emit_flags("-ltightdb-ios-dbg");
#  else
        emit_flags("-ltightdb-ios");
#  endif
#else
#  ifdef TIGHTDB_DEBUG
        emit_flags("-ltightdb-dbg");
#  else
        emit_flags("-ltightdb");
#  endif
#endif
    }

    flush();
}

} // anonymous namespace



int main(int argc, char* argv[])
{
    Func func = func_EmitFlags;

    // Process command line
    {
        const char* prog = argv[0];
        --argc;
        ++argv;
        bool help  = false;
        bool error = false;
        int argc2 = 0;
        for (int i=0; i<argc; ++i) {
            char* arg = argv[i];
            size_t size = strlen(arg);
            if (size < 2 || strncmp(arg, "--", 2) != 0) {
                argv[argc2++] = arg;
                continue;
            }

            if (strcmp(arg, "--help") == 0) {
                help = true;
                continue;
            }
            if (strcmp(arg, "--cflags") == 0) {
                func = func_EmitFlags;
                emit_cflags = true;
                continue;
            }
            if (strcmp(arg, "--libs") == 0) {
                func = func_EmitFlags;
                emit_ldflags = true;
                continue;
            }
            if (strcmp(arg, "--version") == 0) {
                func = func_ShowVersion;
                clear_emit_flags();
                continue;
            }
            if (strcmp(arg, "--prefix") == 0) {
                func = func_ShowPrefix;
                clear_emit_flags();
                continue;
            }
            if (strcmp(arg, "--exec-prefix") == 0) {
                func = func_ShowExecPrefix;
                clear_emit_flags();
                continue;
            }
            if (strcmp(arg, "--includedir") == 0) {
                func = func_ShowIncludedir;
                clear_emit_flags();
                continue;
            }
            if (strcmp(arg, "--bindir") == 0) {
                func = func_ShowBindir;
                clear_emit_flags();
                continue;
            }
            if (strcmp(arg, "--libdir") == 0) {
                func = func_ShowLibdir;
                clear_emit_flags();
                continue;
            }
            error = true;
            break;
        }
        argc = argc2;

        if (argc != 0)
            error = true;

        if (error || help) {
            string msg =
                "Synopsis: "+string(prog)+"\n\n"
                "Options:\n"
                "  --version     Show the version of TightDB that this command was installed as part of\n"
                "  --cflags      Output all pre-processor and compiler flags\n"
                "  --libs        Output all linker flags\n"
                "  --prefix      Show the TightDB installation prefix\n"
                "  --exec-prefix Show the TightDB installation prefix for executables\n"
                "  --includedir  Show the directory containing the TightDB header files\n"
                "  --bindir      Show the directory containing the TightDB executables\n"
                "  --libdir      Show the directory containing the TightDB libraries\n";
            if (error) {
                cerr << "ERROR: Bad command line.\n\n" << msg;
                return 1;
            }
            cout << msg;
            return 0;
        }
    }


    switch (func) {
        case func_EmitFlags:
            emit_flags();
            break;
        case func_ShowVersion:
            cout << TIGHTDB_VERSION "\n";
            break;
        case func_ShowPrefix:
            cout << TIGHTDB_INSTALL_PREFIX "\n";
            break;
        case func_ShowExecPrefix:
            cout << TIGHTDB_INSTALL_EXEC_PREFIX "\n";
            break;
        case func_ShowIncludedir:
            cout << TIGHTDB_INSTALL_INCLUDEDIR "\n";
            break;
        case func_ShowBindir:
            cout << TIGHTDB_INSTALL_BINDIR "\n";
            break;
        case func_ShowLibdir:
            cout << TIGHTDB_INSTALL_LIBDIR "\n";
            break;
    }
}
