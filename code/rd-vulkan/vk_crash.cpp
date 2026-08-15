// Symbolised crash reports, so a fault on the user's machine costs one run
// instead of a round of guessing.
//
// The engine dies with a bare 0xC0000005 and no indication of where. A process
// wide handler that walks the stack and resolves it against the shipped PDBs
// turns that into a function name and a line number, which is the difference
// between reading a diff and reading a report.
//
// This is not a debugger substitute and does not try to be one: no minidump, no
// heap inspection, no thread enumeration. It answers "which function" and stays
// small enough to be safe to run from inside an exception.

// Ahead of every include: this suppression is read when <stdio.h> declares
// fopen, and tr_local.h pulls that in. The handler runs after the process is
// already lost, so the checked variant buys nothing, and the warning would fail
// a build configured with /WX.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "tr_local.h"
#include "vk_local.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>

// A first-chance handler sees exceptions that are about to be handled normally
// as well as the fatal ones. Logging a bounded number of them keeps a benign
// early fault from hiding the real one, without letting a loop fill the disk.
#define JKX_CRASH_MAX_REPORTS 4
#define JKX_CRASH_MAX_FRAMES  64

static LONG  jkx_crash_reports = 0;
static LONG  jkx_crash_busy = 0;
static char  jkx_crash_path[MAX_PATH];
static bool  jkx_crash_symbols = false;

// Both of these have to be given back before this module can be unloaded: a
// handler the operating system still holds a pointer to, in memory that has
// been freed, is a jump into nothing on the next exception of any kind. That is
// a crash introduced by the crash reporter.
//
// The renderer in this tree is a static library linked into the engine
// (code/rd-vulkan/CMakeLists.txt, and CL_InitRef calls GetRefAPI directly), so
// it is never unloaded and this never has to happen. It used to be called from
// renderer shutdown anyway, on the reasoning that vid_restart unloads the
// module - which was true of the dynamic renderer this replaced and is not true
// here. The cost was exact: vid_restart crashed to the desktop and left no
// report, because the handler was taken down at the top of the teardown and put
// back at the bottom of the next initialisation, and the fault is in between.
// So this is kept, and not called.
static PVOID jkx_crash_vectored_handle = NULL;
static LPTOP_LEVEL_EXCEPTION_FILTER jkx_crash_previous_filter = NULL;

static const char *jkx_exception_name( DWORD code )
{
	switch ( code ) {
		case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
		case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
		case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
		case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
		case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
		default:                              return NULL;
	}
}

// C++ exceptions, debugger thread names and breakpoints all arrive here too and
// are none of our business.
static bool jkx_exception_is_fatal( DWORD code )
{
	return jkx_exception_name( code ) != NULL;
}

static void jkx_crash_write_frame( FILE *f, DWORD64 address, int index )
{
	// Fixed buffer: SYMBOL_INFO is a header followed by the name, so the
	// allocation has to cover both and MAX_SYM_NAME is the documented bound.
	char        buffer[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME];
	SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
	DWORD64     displacement = 0;
	IMAGEHLP_MODULE64 module;
	IMAGEHLP_LINE64   line;
	DWORD             lineDisplacement = 0;
	const HANDLE      process = GetCurrentProcess();

	memset( buffer, 0, sizeof( buffer ) );
	symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
	symbol->MaxNameLen = MAX_SYM_NAME;

	memset( &module, 0, sizeof( module ) );
	module.SizeOfStruct = sizeof( module );

	fprintf( f, "  %2d  0x%016llx", index, (unsigned long long)address );

	// The module and the offset inside it are printed unconditionally. Without
	// symbols they are still enough to resolve the frame by hand against the
	// PDB that CI packaged next to the binary.
	if ( jkx_crash_symbols && SymGetModuleInfo64( process, address, &module ) ) {
		fprintf( f, "  %s+0x%llx", module.ModuleName,
			(unsigned long long)( address - module.BaseOfImage ) );
	}

	if ( jkx_crash_symbols && SymFromAddr( process, address, &displacement, symbol ) ) {
		fprintf( f, "  %s+0x%llx", symbol->Name, (unsigned long long)displacement );

		memset( &line, 0, sizeof( line ) );
		line.SizeOfStruct = sizeof( line );
		if ( SymGetLineFromAddr64( process, address, &lineDisplacement, &line ) ) {
			fprintf( f, "  (%s:%lu)", line.FileName, (unsigned long)line.LineNumber );
		}
	}

	fprintf( f, "\n" );
}

static void jkx_crash_write_report( FILE *f, EXCEPTION_POINTERS *ep, const char *origin )
{
	const EXCEPTION_RECORD *record = ep->ExceptionRecord;
	CONTEXT       context = *ep->ContextRecord;
	STACKFRAME64  frame;
	const HANDLE  process = GetCurrentProcess();
	const HANDLE  thread = GetCurrentThread();
	int           i;

	fprintf( f, "\n=== JKX crash report (%s) ===\n", origin );
	fprintf( f, "exception: 0x%08lx %s\n", (unsigned long)record->ExceptionCode,
		jkx_exception_name( record->ExceptionCode ) );
	fprintf( f, "address:   0x%016llx\n", (unsigned long long)(DWORD64)record->ExceptionAddress );

	if ( record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 ) {
		static const char *access[] = { "read", "write", "execute" };
		const ULONG_PTR kind = record->ExceptionInformation[0];
		fprintf( f, "operation: %s of 0x%016llx\n",
			kind < 3 ? access[kind] : "?",
			(unsigned long long)record->ExceptionInformation[1] );
	}

	fprintf( f, "thread:    %lu\n", (unsigned long)GetCurrentThreadId() );
	fprintf( f, "stack:\n" );

	memset( &frame, 0, sizeof( frame ) );
#if defined( _M_X64 ) || defined( __x86_64__ )
	frame.AddrPC.Offset = context.Rip;
	frame.AddrFrame.Offset = context.Rbp;
	frame.AddrStack.Offset = context.Rsp;
	const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
	frame.AddrPC.Offset = context.Eip;
	frame.AddrFrame.Offset = context.Ebp;
	frame.AddrStack.Offset = context.Esp;
	const DWORD machine = IMAGE_FILE_MACHINE_I386;
#endif
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Mode = AddrModeFlat;

	for ( i = 0; i < JKX_CRASH_MAX_FRAMES; i++ ) {
		if ( !StackWalk64( machine, process, thread, &frame, &context, NULL,
				SymFunctionTableAccess64, SymGetModuleBase64, NULL ) ) {
			break;
		}
		if ( frame.AddrPC.Offset == 0 ) {
			break;
		}
		jkx_crash_write_frame( f, frame.AddrPC.Offset, i );
	}

	fprintf( f, "=== end of report ===\n" );
	fflush( f );
}

static LONG jkx_crash_handle( EXCEPTION_POINTERS *ep, const char *origin )
{
	FILE *f;

	if ( !jkx_exception_is_fatal( ep->ExceptionRecord->ExceptionCode ) ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	if ( InterlockedIncrement( &jkx_crash_reports ) > JKX_CRASH_MAX_REPORTS ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	// A fault inside the handler would otherwise recurse until the stack ends.
	if ( InterlockedExchange( &jkx_crash_busy, 1 ) != 0 ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	f = fopen( jkx_crash_path, "a" );
	if ( f != NULL ) {
		jkx_crash_write_report( f, ep, origin );
		fclose( f );
	}

	InterlockedExchange( &jkx_crash_busy, 0 );

	// Never swallow the exception: whatever would have happened without this
	// handler still happens, so behaviour under test is the shipped behaviour.
	return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK jkx_crash_vectored( EXCEPTION_POINTERS *ep )
{
	return jkx_crash_handle( ep, "first chance" );
}

static LONG WINAPI jkx_crash_unhandled( EXCEPTION_POINTERS *ep )
{
	jkx_crash_handle( ep, "unhandled" );

	// Whatever was there before this module loaded still gets its turn.
	if ( jkx_crash_previous_filter != NULL ) {
		return jkx_crash_previous_filter( ep );
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void vk_install_crash_handler( void )
{
	char *slash;

	if ( jkx_crash_vectored_handle != NULL ) {
		return; // already installed in this load of the module
	}

	// One place for logs, and it is the write directory.
	//
	// This used to land beside the executable while the console log and the
	// engine's own crash log went to the home path, so a report was in one of
	// two folders depending on which layer noticed the fault first. Asking
	// somebody to find a log is not the moment to explain that.
	//
	// fs_homepath rather than anything derived here, because it is what the
	// filesystem actually chose - including when the user overrode it with
	// +set fs_homepath, which is how the headless run keeps its output inside
	// its own directory. The executable's own folder is the fallback: on a
	// Steam install it may not be writable, but a path that exists beats none,
	// and it is where this wrote for its whole life until now.
	const char *home = Cvar_VariableString( "fs_homepath" );

	if ( home != NULL && home[0] != '\0' ) {
		Com_sprintf( jkx_crash_path, sizeof( jkx_crash_path ), "%s\\logs", home );
		CreateDirectoryA( jkx_crash_path, NULL );
		Com_sprintf( jkx_crash_path, sizeof( jkx_crash_path ),
			"%s\\logs\\jkx_crash.txt", home );
	} else {
		if ( GetModuleFileNameA( NULL, jkx_crash_path, sizeof( jkx_crash_path ) ) == 0 ) {
			return;
		}
		slash = strrchr( jkx_crash_path, '\\' );
		if ( slash == NULL ) {
			return;
		}
		Q_strncpyz( slash + 1, "jkx_crash.txt",
			(int)( sizeof( jkx_crash_path ) - ( slash + 1 - jkx_crash_path ) ) );
	}

	SymSetOptions( SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES );
	jkx_crash_symbols = SymInitialize( GetCurrentProcess(), NULL, TRUE ) != FALSE;

	// Last in the vectored chain, so a handler that legitimately deals with the
	// exception gets it first; plus the unhandled filter, because a vectored
	// handler can be unregistered by anything else in the process.
	jkx_crash_vectored_handle = AddVectoredExceptionHandler( 0, jkx_crash_vectored );
	jkx_crash_previous_filter = SetUnhandledExceptionFilter( jkx_crash_unhandled );

	CL_RefPrintf( PRINT_ALL, "JKX: crash reports go to %s%s\n", jkx_crash_path,
		jkx_crash_symbols ? "" : " (no symbols, addresses only)" );
}

void vk_remove_crash_handler( void )
{
	if ( jkx_crash_vectored_handle != NULL ) {
		RemoveVectoredExceptionHandler( jkx_crash_vectored_handle );
		jkx_crash_vectored_handle = NULL;
	}

	// Put back what we replaced - unless somebody installed a filter after we
	// did, in which case theirs is the one that belongs there and we have just
	// removed it by accident.
	{
		LPTOP_LEVEL_EXCEPTION_FILTER current =
			SetUnhandledExceptionFilter( jkx_crash_previous_filter );
		if ( current != jkx_crash_unhandled ) {
			SetUnhandledExceptionFilter( current );
		}
	}
	jkx_crash_previous_filter = NULL;

	if ( jkx_crash_symbols ) {
		SymCleanup( GetCurrentProcess() );
		jkx_crash_symbols = false;
	}

	// The next load of this module installs again, and appends to the same
	// file, so a report from before a vid_restart is not lost.
	jkx_crash_path[0] = '\0';
}

#else // _WIN32

// Linux gets a core file and a debugger, which is strictly better than anything
// this would produce.
void vk_install_crash_handler( void )
{
}

void vk_remove_crash_handler( void )
{
}

#endif // _WIN32
