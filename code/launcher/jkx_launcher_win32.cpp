/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The two things about the launcher that only exist on Windows: the records
// Steam and GOG keep in the registry, and a window with a button per game.
//
// Neither can be tested on the machine this was written on, which is why
// neither contains a decision. Where a game might be is jkx_install_find.cpp,
// against a machine invented by a test; what a directory holds is
// jkx_install_scan.cpp, against a filesystem invented by a test. What is left
// here is calling the operating system and drawing buttons, and that has to be
// looked at rather than asserted.

#ifdef _WIN32

#include "jkx_launcher.h"
#include "jkx_install_find.h"

#include <windows.h>
#include <shlobj.h>

#include <stdio.h>
#include <string.h>

/*
===============================================================================

Asking Windows

===============================================================================
*/

static HKEY RootKey( installRegRoot_t root )
{
	return ( root == JKX_REG_CURRENT_USER ) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}


// Both views of the registry, always.
//
// A 64-bit program reading SOFTWARE\... gets the 64-bit view, and Steam and GOG
// are 32-bit programs whose keys live under WOW6432Node. The explicit
// WOW6432Node paths in jkx_install_find.cpp cover that from the other side;
// asking for both views as well costs one failed open and means neither half
// has to be right about which Windows put it where.
static int OpenEitherView( installRegRoot_t root, const char *key, REGSAM access, HKEY *out )
{
	if ( RegOpenKeyExA( RootKey( root ), key, 0, access | KEY_WOW64_32KEY, out ) == ERROR_SUCCESS ) {
		return 1;
	}
	if ( RegOpenKeyExA( RootKey( root ), key, 0, access | KEY_WOW64_64KEY, out ) == ERROR_SUCCESS ) {
		return 1;
	}
	return 0;
}


static int WinReadRegistry( void *user, installRegRoot_t root, const char *key,
							const char *value, char *out, int outSize )
{
	HKEY	handle;
	DWORD	type = 0;
	DWORD	size;

	(void)user;

	if ( !OpenEitherView( root, key, KEY_QUERY_VALUE, &handle ) ) {
		return 0;
	}

	size = (DWORD)outSize;
	const LONG result = RegQueryValueExA( handle, value, NULL, &type, (LPBYTE)out, &size );
	RegCloseKey( handle );

	if ( result != ERROR_SUCCESS || ( type != REG_SZ && type != REG_EXPAND_SZ ) ) {
		return 0;
	}

	// RegQueryValueEx does not promise a terminator, and a value written
	// without one is the shape that turns a path into whatever follows it in
	// this buffer.
	if ( size >= (DWORD)outSize ) {
		size = (DWORD)outSize - 1;
	}
	out[size] = '\0';
	return ( out[0] != '\0' );
}


static int WinListSubKeys( void *user, installRegRoot_t root, const char *key,
						   char *out, int outStride, int max )
{
	HKEY	handle;
	int		count = 0;

	(void)user;

	if ( !OpenEitherView( root, key, KEY_ENUMERATE_SUB_KEYS, &handle ) ) {
		return 0;
	}

	for ( DWORD i = 0; count < max; i++ ) {
		char	name[256];
		DWORD	size = (DWORD)sizeof( name );

		if ( RegEnumKeyExA( handle, i, name, &size, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS ) {
			break;
		}
		snprintf( out + (size_t)count * outStride, (size_t)outStride, "%s", name );
		count++;
	}

	RegCloseKey( handle );
	return count;
}


static int WinListDirs( void *user, const char *path, char *out, int outStride, int max )
{
	char				pattern[JKX_MAX_PATH];
	WIN32_FIND_DATAA	data;
	HANDLE				find;
	int					count = 0;

	(void)user;

	snprintf( pattern, sizeof( pattern ), "%s\\*", path );

	find = FindFirstFileA( pattern, &data );
	if ( find == INVALID_HANDLE_VALUE ) {
		return 0;
	}

	do {
		if ( !( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
			continue;
		}
		if ( strcmp( data.cFileName, "." ) == 0 || strcmp( data.cFileName, ".." ) == 0 ) {
			continue;
		}
		snprintf( out + (size_t)count * outStride, (size_t)outStride, "%s", data.cFileName );
		count++;
	} while ( count < max && FindNextFileA( find, &data ) );

	FindClose( find );
	return count;
}


static int WinReadText( void *user, const char *path, char *out, int outSize )
{
	FILE	*f;
	size_t	read;

	(void)user;

	f = fopen( path, "rb" );
	if ( !f ) {
		return 0;
	}

	read = fread( out, 1, (size_t)outSize - 1, f );
	fclose( f );

	out[read] = '\0';
	return (int)read;
}


int Launcher_PlatformCandidates( char *out, int outStride, int max )
{
	installSearch_t	search;

	search.readRegistry	= WinReadRegistry;
	search.listSubKeys	= WinListSubKeys;
	search.listDirs		= WinListDirs;
	search.readText		= WinReadText;
	search.user			= NULL;

	return InstallFind_Candidates( &search, out, outStride, max );
}


/*
===============================================================================

The window

===============================================================================
*/

#define JKX_UI_WIDTH		540
#define JKX_UI_MARGIN		16
#define JKX_UI_BUTTON_H		52
#define JKX_UI_GAP			8

#define JKX_ID_FIRST_GAME	1000
#define JKX_ID_BROWSE		2000

typedef struct {
	launcherFound_t	*found;
	int				*count;
	int				max;
	int				chosen;
	HFONT			font;
	HFONT			fontSmall;
	HWND			status;
} launcherWindow_t;


// The shell's own font at the shell's own size, rather than the one a window
// gets by default - which is a bitmap face from 1995 and is the difference
// between a program that looks like part of the system and one that looks
// abandoned.
static void MakeFonts( launcherWindow_t *ui )
{
	NONCLIENTMETRICSA	metrics;

	memset( &metrics, 0, sizeof( metrics ) );
	metrics.cbSize = sizeof( metrics );

	if ( SystemParametersInfoA( SPI_GETNONCLIENTMETRICS, sizeof( metrics ), &metrics, 0 ) ) {
		ui->font = CreateFontIndirectA( &metrics.lfMessageFont );

		metrics.lfMessageFont.lfHeight = (LONG)( metrics.lfMessageFont.lfHeight * 9 / 10 );
		ui->fontSmall = CreateFontIndirectA( &metrics.lfMessageFont );
	} else {
		ui->font = (HFONT)GetStockObject( DEFAULT_GUI_FONT );
		ui->fontSmall = ui->font;
	}
}


static void SetFont( HWND control, HFONT font )
{
	SendMessageA( control, WM_SETFONT, (WPARAM)font, TRUE );
}


// SHBrowseForFolder rather than the newer dialog: this needs one folder, works
// the same on every Windows this game runs on, and does not require COM to have
// been initialised in a particular apartment.
static int BrowseForFolder( HWND parent, char *out, int outSize )
{
	BROWSEINFOA		info;
	LPITEMIDLIST	list;
	char			path[MAX_PATH];

	memset( &info, 0, sizeof( info ) );
	info.hwndOwner	= parent;
	info.lpszTitle	= "Select the folder that contains base - for Steam or GOG that is GameData";
	info.ulFlags	= BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

	list = SHBrowseForFolderA( &info );
	if ( list == NULL ) {
		return 0;
	}

	const BOOL got = SHGetPathFromIDListA( list, path );
	CoTaskMemFree( list );

	if ( !got ) {
		return 0;
	}

	snprintf( out, (size_t)outSize, "%s", path );
	return 1;
}


static void BuildControls( HWND window, launcherWindow_t *ui )
{
	int	y = JKX_UI_MARGIN;

	for ( int i = 0; i < *ui->count; i++ ) {
		char	label[JKX_MAX_PATH + 64];
		HWND	button;
		HWND	path;

		snprintf( label, sizeof( label ), "%s", Launcher_GameName( ui->found[i].kind ) );

		button = CreateWindowExA( 0, "BUTTON", label,
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			JKX_UI_MARGIN, y, JKX_UI_WIDTH - JKX_UI_MARGIN * 2, JKX_UI_BUTTON_H,
			window, (HMENU)(INT_PTR)( JKX_ID_FIRST_GAME + i ), NULL, NULL );
		SetFont( button, ui->font );
		y += JKX_UI_BUTTON_H + 2;

		// The path under the button rather than inside it. Which of two
		// installations of the same game this is can only be told from the
		// path, and a button caption that is a full path is a button nobody can
		// read the first word of.
		path = CreateWindowExA( 0, "STATIC", ui->found[i].root,
			WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
			JKX_UI_MARGIN + 4, y, JKX_UI_WIDTH - JKX_UI_MARGIN * 2 - 8, 18,
			window, NULL, NULL, NULL );
		SetFont( path, ui->fontSmall );
		y += 18 + JKX_UI_GAP;
	}

	if ( *ui->count == 0 ) {
		HWND	nothing = CreateWindowExA( 0, "STATIC",
			"No installation of Jedi Academy or Jedi Outcast was found.\r\n"
			"If one is somewhere unusual, point this at it once and it is remembered.",
			WS_CHILD | WS_VISIBLE,
			JKX_UI_MARGIN, y, JKX_UI_WIDTH - JKX_UI_MARGIN * 2, 40,
			window, NULL, NULL, NULL );
		SetFont( nothing, ui->font );
		y += 40 + JKX_UI_GAP;
	}

	{
		HWND	browse = CreateWindowExA( 0, "BUTTON", "Choose a folder...",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			JKX_UI_MARGIN, y, 160, 30,
			window, (HMENU)(INT_PTR)JKX_ID_BROWSE, NULL, NULL );
		SetFont( browse, ui->font );
		y += 30 + JKX_UI_GAP;
	}

	ui->status = CreateWindowExA( 0, "STATIC", "",
		WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
		JKX_UI_MARGIN, y, JKX_UI_WIDTH - JKX_UI_MARGIN * 2, 18,
		window, NULL, NULL, NULL );
	SetFont( ui->status, ui->fontSmall );
	y += 18 + JKX_UI_MARGIN;

	// The window is sized around what went into it rather than the other way
	// round, so one game and three do not both get a window with a hole in it.
	{
		RECT	wanted = { 0, 0, JKX_UI_WIDTH, y };

		AdjustWindowRect( &wanted, (DWORD)GetWindowLongPtrA( window, GWL_STYLE ), FALSE );
		SetWindowPos( window, NULL, 0, 0,
			wanted.right - wanted.left, wanted.bottom - wanted.top,
			SWP_NOMOVE | SWP_NOZORDER );
	}
}


static LRESULT CALLBACK LauncherProc( HWND window, UINT message, WPARAM wparam, LPARAM lparam )
{
	launcherWindow_t	*ui = (launcherWindow_t *)GetWindowLongPtrA( window, GWLP_USERDATA );

	switch ( message ) {
	case WM_CREATE:
		{
			CREATESTRUCTA	*create = (CREATESTRUCTA *)lparam;

			ui = (launcherWindow_t *)create->lpCreateParams;
			SetWindowLongPtrA( window, GWLP_USERDATA, (LONG_PTR)ui );
			BuildControls( window, ui );
		}
		return 0;

	case WM_CTLCOLORSTATIC:
		// Static text over the dialog background rather than over white, which
		// is what it gets by default on a plain window class.
		SetBkMode( (HDC)wparam, TRANSPARENT );
		return (LRESULT)GetSysColorBrush( COLOR_BTNFACE );

	case WM_COMMAND:
		if ( ui == NULL ) {
			break;
		}
		{
			const int	id = LOWORD( wparam );

			if ( id >= JKX_ID_FIRST_GAME && id < JKX_ID_FIRST_GAME + *ui->count ) {
				ui->chosen = id - JKX_ID_FIRST_GAME;
				DestroyWindow( window );
				return 0;
			}

			if ( id == JKX_ID_BROWSE ) {
				char	picked[JKX_MAX_PATH];

				if ( !BrowseForFolder( window, picked, sizeof( picked ) ) ) {
					return 0;
				}

				{
					launcherFound_t	one;

					if ( !Launcher_Identify( picked, &one ) ) {
						SetWindowTextA( ui->status, one.reason );
						return 0;
					}

					// Straight in rather than added to the list and waited for:
					// the user just answered the only question this window
					// asks.
					if ( *ui->count < ui->max ) {
						ui->found[*ui->count] = one;
						ui->chosen = *ui->count;
						( *ui->count )++;
					} else {
						ui->found[ui->max - 1] = one;
						ui->chosen = ui->max - 1;
					}
					DestroyWindow( window );
					return 0;
				}
			}
		}
		break;

	case WM_DESTROY:
		PostQuitMessage( 0 );
		return 0;

	default:
		break;
	}

	return DefWindowProcA( window, message, wparam, lparam );
}


int Launcher_ChooseInWindow( launcherFound_t *found, int *count, int max )
{
	launcherWindow_t	ui;
	WNDCLASSEXA			cls;
	HWND				window;
	MSG					message;

	// One game and nothing to ask about: showing a window whose only content is
	// a button the user must press to get what they already asked for is worse
	// than starting the game.
	if ( *count == 1 ) {
		return 0;
	}

	memset( &ui, 0, sizeof( ui ) );
	ui.found	= found;
	ui.count	= count;
	ui.max		= max;
	ui.chosen	= JKX_UI_CLOSED;
	MakeFonts( &ui );

	memset( &cls, 0, sizeof( cls ) );
	cls.cbSize			= sizeof( cls );
	cls.lpfnWndProc		= LauncherProc;
	cls.hInstance		= GetModuleHandleA( NULL );
	cls.hCursor			= LoadCursor( NULL, IDC_ARROW );
	cls.hbrBackground	= (HBRUSH)( COLOR_BTNFACE + 1 );
	cls.lpszClassName	= "JKXLauncher";
	cls.hIcon			= LoadIcon( cls.hInstance, MAKEINTRESOURCE( 1 ) );

	if ( !RegisterClassExA( &cls ) ) {
		return JKX_UI_UNAVAILABLE;
	}

	window = CreateWindowExA( 0, cls.lpszClassName, "JKX",
		( WS_OVERLAPPEDWINDOW & ~( WS_THICKFRAME | WS_MAXIMIZEBOX ) ),
		CW_USEDEFAULT, CW_USEDEFAULT, JKX_UI_WIDTH, 200,
		NULL, NULL, cls.hInstance, &ui );

	if ( window == NULL ) {
		// No window is not a reason to fail: the console path below can do
		// everything this can, less comfortably.
		return JKX_UI_UNAVAILABLE;
	}

	ShowWindow( window, SW_SHOW );
	UpdateWindow( window );

	while ( GetMessageA( &message, NULL, 0, 0 ) > 0 ) {
		if ( IsDialogMessageA( window, &message ) ) {
			continue;
		}
		TranslateMessage( &message );
		DispatchMessageA( &message );
	}

	if ( ui.font ) {
		DeleteObject( ui.font );
	}
	if ( ui.fontSmall && ui.fontSmall != ui.font ) {
		DeleteObject( ui.fontSmall );
	}

	return ui.chosen;
}

#endif	// _WIN32
