#ifdef WINDOZE
/*
  ANSI.c - ANSI escape sequence console driver.

  Jason Hood, 21 & 22 October, 2005.

  Derived from ANSI.xs by Jean-Louis Morel, from his Perl package
  Win32::Console::ANSI.  I removed the codepage conversion ("\e(") and added
  WriteConsole hooking.

  v1.01, 11 & 12 March, 2006:
    disable when console has disabled processed output;
    \e[5m (blink) is the same as \e[4m (underline);
    do not conceal control characters (0 to 31);
    \e[m will restore original color.

  v1.10, 22 February, 2009:
    fix MyWriteConsoleW for strings longer than the buffer;
    initialise attributes to current;
    hook into child processes.

  v1.11, 28 February, 2009:
    fix hooking into child processes (only do console executables).

  v1.12, 9 March, 2009:
    really fix hooking (I didn't realise MinGW didn't generate relocations).

  v1.13, 21 & 27 March, 2009:
    alternate injection method, to work with DEP;
    use Unicode and the current output code page (not OEMCP).

  v1.14, 3 April, 2009:
    fix test for empty import section.

  v1.15, 17 May, 2009:
    properly update lpNumberOfCharsWritten in MyWriteConsoleA.

  v1.20, 26 & 29 May, 17 to 21 June, 2009:
    create an ANSICON environment variable;
    hook GetEnvironmentVariable to create ANSICON dynamically;
    use another injection method.

  v1.22, 5 October, 2009:
    hook LoadLibrary to intercept the newly loaded functions.

  v1.23, 11 November, 2009:
    unload gracefully;
    conceal characters by making foreground same as background;
    reverse the bold/underline attributes, too.

  v1.25, 15, 20 & 21 July, 2010:
    hook LoadLibraryEx (now cscript works);
    Win7 support.

  v1.30, 3 August to 7 September, 2010:
    x64 support.

  v1.31, 13 & 19 November, 2010:
    fix multibyte conversion problems.

  v1.32, 4 to 22 December, 2010:
    test for lpNumberOfCharsWritten/lpNumberOfBytesWritten being NULL;
    recognise DSR and xterm window title;
    ignore sequences starting with \e[? & \e[>;
    close the handles opened by CreateProcess.

  v1.40, 25 & 26 February, 1 March, 2011:
    hook GetProcAddress, addresses issues with .NET (work with PowerShell);
    implement SO & SI to use the DEC Special Graphics Character Set (enables
     line drawing via ASCII); ignore \e(X & \e)X (where X is any character);
    add \e[?25h & \e[?25l to show/hide the cursor (DECTCEM).

  v1.50, 7 to 14 December, 2011:
    added dynamic environment variable ANSICON_VER to return version;
    read ANSICON_EXC environment variable to exclude selected modules;
    read ANSICON_GUI environment variable to hook selected GUI programs;
    read ANSICON_DEF environment variable to set the default GR;
    transfer current GR to child, read it on exit.

  v1.51, 15 January, 5, 22 & 24 February, 2012:
    added log mask 16 to log all the imported modules of imported modules;
    ignore the version within the core API DLL names;
    fix 32-bit process trying to identify 64-bit process;
    hook _lwrite & _hwrite.

  v1.52, 10 April, 1 & 2 June, 2012:
    use ansicon.exe to enable 32-bit to inject into 64-bit;
    implement \e[39m & \e[49m (only setting color, nothing else);
    added the character/line equivalents (keaj`) of the cursor movement
     sequences (ABCDG), as well as vertical absolute (d) and erase characters
     (X).

  v1.53, 12 June, 2012:
    fixed Update_GRM when running multiple processes (e.g. "cl /MP").

  v1.60, 22 to 24 November, 2012:
    alternative method to obtain LLW for 64->32 injection;
    support for VC6 (remove section pragma, rename isdigit to is_digit).

  v1.61, 14 February, 2013:
    go back to using ANSI-LLW.exe for 64->32 injection.

  v1.62, 17 & 18 July, 2013:
    another method to obtain LLW for 64->32 injection.

  v1.64, 2 August, 2013:
    better method of determining a console handle (see IsConsoleHandle).

  v1.65, 28 August, 2013:
    fix \e[K (was using window, not buffer).
*/
//#define WIN32_LEAN_AND_MEAN
//#include <winsock2.h>
#include <windows.h>

#include "ansicon.h"
#include "version.h"
#include <tlhelp32.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef WIN10
#define wcstok wcstok_s
wchar_t* next_token1 = NULL;
#define WCONTEXT ,&next_token1
#else
#define WCONTEXT
#endif

BOOL ParseAndPrintString( HANDLE hDev, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten );
BOOL search_env( LPCTSTR var, LPCTSTR val );
void set_ansicon( PCONSOLE_SCREEN_BUFFER_INFO pcsbi );

#ifndef bool
    #define bool int
    #define false ((bool)0)
    #define true  ((bool)1)
#endif


#define is_digit(c) ('0' <= (c) && (c) <= '9')

#ifdef __GNUC__
#define SHARED __attribute__((shared, section(".shared")))
#else
#pragma data_seg(".shared", "read,write,shared")
#pragma data_seg()
#define SHARED __declspec(allocate(".shared"))
#endif


// ========== Global variables and constants

HANDLE	  hConOut;		// handle to CONOUT$

#define ESC	'\x1B'          // ESCape character
#define BEL	'\x07'
#define SO	'\x0E'          // Shift Out
#define SI	'\x0F'          // Shift In

#define MAX_ARG 16		// max number of args in an escape sequence
int   state;			// automata state
TCHAR prefix;			// escape sequence prefix ( '[', ']' or '(' );
TCHAR prefix2;			// secondary prefix ( '?' or '>' );
TCHAR suffix;			// escape sequence suffix
int   es_argc;			// escape sequence args count
int   es_argv[MAX_ARG]; 	// escape sequence args
TCHAR Pt_arg[MAX_PATH*2];	// text parameter for Operating System Command
int   Pt_len;
BOOL  shifted;

/*
int main(int argc, char **argv) {
#define BUFFERSIZE 50000
	char Buffer[BUFFERSIZE];
	int i;
	int NumberOfBytesWritten = 0;
	BOOL bTemp;
	HANDLE hDev;
	HANDLE hStdout, hStdin, hStderr; 
	HANDLE hConsoleBuffer;
//	CONSOLE_SCREEN_BUFFER_INFO csbiInfo; 
//	WORD wOldColorAttrs; 
	for(i=0;i<BUFFERSIZE;i++)Buffer[i]='\000';

	FreeConsole();

	if (!AllocConsole())
    {
        printf("AllocConsole failed\n"); 
    }

	hConsoleBuffer = CreateConsoleScreenBuffer(
		GENERIC_READ | GENERIC_WRITE,  // access flag
		0,      // buffer share mode   can be zero | FILE_SHARE_READ | FILE_SHARE_WRITE
		NULL , // pointer to security attributes  LPSECURITY_ATTRIBUTES *lpSecurityAttributes
		CONSOLE_TEXTMODE_BUFFER,          // type of buffer to create   The only currently supported screen buffer type is CONSOLE_TEXTMODE_BUFFER.
		NULL   // reserved   LPVOID lpScreenBufferData
	);

    // Get handles to STDIN and STDOUT. 

    hStdin = GetStdHandle(STD_INPUT_HANDLE); 
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE || 
        hStdout == INVALID_HANDLE_VALUE || hStderr == INVALID_HANDLE_VALUE) 
    {
        printf("GetStdHandle failed\n");
        return 1;
    }
	hDev = hStderr;

	strcpy(Buffer,"My \33[31;1mtest\33[0m string..\n");

	bTemp = ParseAndPrintString( hDev, Buffer, strlen(Buffer), &NumberOfBytesWritten );
	printf("number of bytes written = %i\n", NumberOfBytesWritten);
	return 1;
} */


// DEC Special Graphics Character Set from
// http://vt100.net/docs/vt220-rm/table2-4.html
// Some of these may not look right, depending on the font and code page (in
// particular, the Control Pictures probably won't work at all).
const WCHAR G1[] =
{
  ' ',          // _ - blank
  L'\x2666',    // ` - Black Diamond Suit
  L'\x2592',    // a - Medium Shade
  L'\x2409',    // b - HT
  L'\x240c',    // c - FF
  L'\x240d',    // d - CR
  L'\x240a',    // e - LF
  L'\x00b0',    // f - Degree Sign
  L'\x00b1',    // g - Plus-Minus Sign
  L'\x2424',    // h - NL
  L'\x240b',    // i - VT
  L'\x2518',    // j - Box Drawings Light Up And Left
  L'\x2510',    // k - Box Drawings Light Down And Left
  L'\x250c',    // l - Box Drawings Light Down And Right
  L'\x2514',    // m - Box Drawings Light Up And Right
  L'\x253c',    // n - Box Drawings Light Vertical And Horizontal
  L'\x00af',    // o - SCAN 1 - Macron
  L'\x25ac',    // p - SCAN 3 - Black Rectangle
  L'\x2500',    // q - SCAN 5 - Box Drawings Light Horizontal
  L'_',         // r - SCAN 7 - Low Line
  L'_',         // s - SCAN 9 - Low Line
  L'\x251c',    // t - Box Drawings Light Vertical And Right
  L'\x2524',    // u - Box Drawings Light Vertical And Left
  L'\x2534',    // v - Box Drawings Light Up And Horizontal
  L'\x252c',    // w - Box Drawings Light Down And Horizontal
  L'\x2502',    // x - Box Drawings Light Vertical
  L'\x2264',    // y - Less-Than Or Equal To
  L'\x2265',    // z - Greater-Than Or Equal To
  L'\x03c0',    // { - Greek Small Letter Pi
  L'\x2260',    // | - Not Equal To
  L'\x00a3',    // } - Pound Sign
  L'\x00b7',    // ~ - Middle Dot
};

#define FIRST_G1 '_'
#define LAST_G1  '~'


// color constants

#define FOREGROUND_BLACK 0
#define FOREGROUND_WHITE FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE

#define BACKGROUND_BLACK 0
#define BACKGROUND_WHITE BACKGROUND_RED|BACKGROUND_GREEN|BACKGROUND_BLUE

const BYTE foregroundcolor[8] =
{
  FOREGROUND_BLACK,			// black foreground
  FOREGROUND_RED,			// red foreground
  FOREGROUND_GREEN,			// green foreground
  FOREGROUND_RED | FOREGROUND_GREEN,	// yellow foreground
  FOREGROUND_BLUE,			// blue foreground
  FOREGROUND_BLUE | FOREGROUND_RED,	// magenta foreground
  FOREGROUND_BLUE | FOREGROUND_GREEN,	// cyan foreground
  FOREGROUND_WHITE			// white foreground
};

const BYTE backgroundcolor[8] =
{
  BACKGROUND_BLACK,			// black background
  BACKGROUND_RED,			// red background
  BACKGROUND_GREEN,			// green background
  BACKGROUND_RED | BACKGROUND_GREEN,	// yellow background
  BACKGROUND_BLUE,			// blue background
  BACKGROUND_BLUE | BACKGROUND_RED,	// magenta background
  BACKGROUND_BLUE | BACKGROUND_GREEN,	// cyan background
  BACKGROUND_WHITE,			// white background
};

const BYTE attr2ansi[8] =		// map console attribute to ANSI number
{
  0,					// black
  4,					// blue
  2,					// green
  6,					// cyan
  1,					// red
  5,					// magenta
  3,					// yellow
  7					// white
};

GRM grm;

// saved cursor position
COORD SavePos;

// Variables to enable copying attributes between processes.
SHARED DWORD s_pid;
SHARED GRM   s_grm;
SHARED DWORD s_flag;
#define GRM_INIT 1
#define GRM_EXIT 2

#ifdef _WIN64
SHARED DWORD LLW32;
#endif


// Wait for the child process to finish, then update our GRM to the child's.
DWORD WINAPI UpdateGRM( LPVOID child_pi )
{
  DWORD  pid  = ((LPPROCESS_INFORMATION)child_pi)->dwProcessId;
  HANDLE proc = ((LPPROCESS_INFORMATION)child_pi)->hProcess;
  free( child_pi );

  WaitForSingleObject( proc, INFINITE );
  CloseHandle( proc );

  if (s_flag == GRM_EXIT && s_pid == pid)
  {
    s_flag = 0;
    grm = s_grm;
  }

  return 0;
}




// ========== Print Buffer functions

#define BUFFER_SIZE 2048

int   nCharInBuffer;
CHAR ChBuffer[BUFFER_SIZE];

//-----------------------------------------------------------------------------
//   FlushBuffer()
// Writes the buffer to the console and empties it.
//-----------------------------------------------------------------------------

void FlushBuffer( void )
{
  DWORD nWritten;
  if (nCharInBuffer <= 0) return;
  WriteConsole( hConOut, ChBuffer, nCharInBuffer, &nWritten, NULL );
  nCharInBuffer = 0;
}

//-----------------------------------------------------------------------------
//   PushBuffer( WCHAR c )
// Adds a character in the buffer.
//-----------------------------------------------------------------------------

void PushBuffer( WCHAR c )
{
  if (shifted && c >= FIRST_G1 && c <= LAST_G1)
    c = G1[c-FIRST_G1];
  ChBuffer[nCharInBuffer] = (char)c;
  if (++nCharInBuffer == BUFFER_SIZE)
    FlushBuffer();
}

//-----------------------------------------------------------------------------
//   SendSequence( LPTSTR seq )
// Send the string to the input buffer.
//-----------------------------------------------------------------------------

void SendSequence( LPTSTR seq )
{
  DWORD out;
  INPUT_RECORD in;
  HANDLE hStdIn = GetStdHandle( STD_INPUT_HANDLE );

  in.EventType = KEY_EVENT;
  in.Event.KeyEvent.bKeyDown = TRUE;
  in.Event.KeyEvent.wRepeatCount = 1;
  in.Event.KeyEvent.wVirtualKeyCode = 0;
  in.Event.KeyEvent.wVirtualScanCode = 0;
  in.Event.KeyEvent.dwControlKeyState = 0;
  for (; *seq; ++seq)
  {
    in.Event.KeyEvent.uChar.UnicodeChar = *seq;
    WriteConsoleInput( hStdIn, &in, 1, &out );
  }
}

// ========== Print functions

//-----------------------------------------------------------------------------
//   InterpretEscSeq()
// Interprets the last escape sequence scanned by ParseAndPrintString
//   prefix             escape sequence prefix
//   es_argc            escape sequence args count
//   es_argv[]          escape sequence args array
//   suffix             escape sequence suffix
//
// for instance, with \e[33;45;1m we have
// prefix = '[',
// es_argc = 3, es_argv[0] = 33, es_argv[1] = 45, es_argv[2] = 1
// suffix = 'm'
//-----------------------------------------------------------------------------

void InterpretEscSeq( void )
{
  int  i;
  WORD attribut;
  CONSOLE_SCREEN_BUFFER_INFO Info;
  CONSOLE_CURSOR_INFO CursInfo;
  DWORD len, NumberOfCharsWritten;
  COORD Pos;
  SMALL_RECT Rect;
  CHAR_INFO  CharInfo;

  if (prefix == '[')
  {
    if (prefix2 == '?' && (suffix == 'h' || suffix == 'l'))
    {
      if (es_argc == 1 && es_argv[0] == 25)
      {
	GetConsoleCursorInfo( hConOut, &CursInfo );
	CursInfo.bVisible = (suffix == 'h');
	SetConsoleCursorInfo( hConOut, &CursInfo );
	return;
      }
    }
    // Ignore any other \e[? or \e[> sequences.
    if (prefix2 != 0)
      return;

    GetConsoleScreenBufferInfo( hConOut, &Info );
    switch (suffix)
    {
      case 'm':
	if (es_argc == 0) es_argv[es_argc++] = 0;
	for (i = 0; i < es_argc; i++)
	{
	  if (30 <= es_argv[i] && es_argv[i] <= 37)
	    grm.foreground = es_argv[i] - 30;
	  else if (40 <= es_argv[i] && es_argv[i] <= 47)
	    grm.background = es_argv[i] - 40;
	  else switch (es_argv[i])
	  {
	    case 0:
	    case 39:
	    case 49:
	    {
	      TCHAR def[4];
	      int   a;
	      *def = '7'; def[1] = '\0';
	      GetEnvironmentVariable( (const char *)L"ANSICON_DEF", def, lenof(def) );
	      a = wcstol( (const unsigned short *)def, NULL, 16 );
	      grm.reverse = FALSE;
	      if (a < 0)
	      {
		grm.reverse = TRUE;
		a = -a;
	      }
	      if (es_argv[i] != 49)
		grm.foreground = attr2ansi[a & 7];
	      if (es_argv[i] != 39)
		grm.background = attr2ansi[(a >> 4) & 7];
	      if (es_argv[i] == 0)
	      {
		if (es_argc == 1)
		{
		  grm.bold	= a & FOREGROUND_INTENSITY;
		  grm.underline = a & BACKGROUND_INTENSITY;
		}
		else
		{
		  grm.bold	= 0;
		  grm.underline = 0;
		}
		grm.rvideo    = 0;
		grm.concealed = 0;
	      }
	    }
	    break;

	    case  1: grm.bold	   = FOREGROUND_INTENSITY; break;
	    case  5: // blink
	    case  4: grm.underline = BACKGROUND_INTENSITY; break;
	    case  7: grm.rvideo    = 1; break;
	    case  8: grm.concealed = 1; break;
	    case 21: // oops, this actually turns on double underline
	    case 22: grm.bold	   = 0; break;
	    case 25:
	    case 24: grm.underline = 0; break;
	    case 27: grm.rvideo    = 0; break;
	    case 28: grm.concealed = 0; break;
	  }
	}
	if (grm.concealed)
	{
	  if (grm.rvideo)
	  {
	    attribut = foregroundcolor[grm.foreground]
		     | backgroundcolor[grm.foreground];
	    if (grm.bold)
	      attribut |= FOREGROUND_INTENSITY | BACKGROUND_INTENSITY;
	  }
	  else
	  {
	    attribut = foregroundcolor[grm.background]
		     | backgroundcolor[grm.background];
	    if (grm.underline)
	      attribut |= FOREGROUND_INTENSITY | BACKGROUND_INTENSITY;
	  }
	}
	else if (grm.rvideo)
	{
	  attribut = foregroundcolor[grm.background]
		   | backgroundcolor[grm.foreground];
	  if (grm.bold)
	    attribut |= BACKGROUND_INTENSITY;
	  if (grm.underline)
	    attribut |= FOREGROUND_INTENSITY;
	}
	else
	  attribut = foregroundcolor[grm.foreground] | grm.bold
		   | backgroundcolor[grm.background] | grm.underline;
	if (grm.reverse)
	  attribut = ((attribut >> 4) & 15) | ((attribut & 15) << 4);
	SetConsoleTextAttribute( hConOut, attribut );
      return;

      case 'J':
	if (es_argc == 0) es_argv[es_argc++] = 0; // ESC[J == ESC[0J
	if (es_argc != 1) return;
	switch (es_argv[0])
	{
	  case 0:		// ESC[0J erase from cursor to end of display
	    len = (Info.dwSize.Y - Info.dwCursorPosition.Y - 1) * Info.dwSize.X
		  + Info.dwSize.X - Info.dwCursorPosition.X - 1;
	    FillConsoleOutputCharacter( hConOut, ' ', len,
					Info.dwCursorPosition,
					&NumberOfCharsWritten );
	    FillConsoleOutputAttribute( hConOut, Info.wAttributes, len,
					Info.dwCursorPosition,
					&NumberOfCharsWritten );
	  return;

	  case 1:		// ESC[1J erase from start to cursor.
	    Pos.X = 0;
	    Pos.Y = 0;
	    len   = Info.dwCursorPosition.Y * Info.dwSize.X
		    + Info.dwCursorPosition.X + 1;
	    FillConsoleOutputCharacter( hConOut, ' ', len, Pos,
					&NumberOfCharsWritten );
	    FillConsoleOutputAttribute( hConOut, Info.wAttributes, len, Pos,
					&NumberOfCharsWritten );
	    return;

	  case 2:		// ESC[2J Clear screen and home cursor
	    Pos.X = 0;
	    Pos.Y = 0;
	    len   = Info.dwSize.X * Info.dwSize.Y;
	    FillConsoleOutputCharacter( hConOut, ' ', len, Pos,
					&NumberOfCharsWritten );
	    FillConsoleOutputAttribute( hConOut, Info.wAttributes, len, Pos,
					&NumberOfCharsWritten );
	    SetConsoleCursorPosition( hConOut, Pos );
	  return;

	  default:
	  return;
	}

      case 'K':
	if (es_argc == 0) es_argv[es_argc++] = 0; // ESC[K == ESC[0K
	if (es_argc != 1) return;
	switch (es_argv[0])
	{
	  case 0:		// ESC[0K Clear to end of line
	    len = Info.dwSize.X - Info.dwCursorPosition.X + 1;
	    FillConsoleOutputCharacter( hConOut, ' ', len,
					Info.dwCursorPosition,
					&NumberOfCharsWritten );
	    FillConsoleOutputAttribute( hConOut, Info.wAttributes, len,
					Info.dwCursorPosition,
					&NumberOfCharsWritten );
	  return;

	  case 1:		// ESC[1K Clear from start of line to cursor
	    Pos.X = 0;
	    Pos.Y = Info.dwCursorPosition.Y;
	    FillConsoleOutputCharacter( hConOut, ' ',
					Info.dwCursorPosition.X + 1, Pos,
					&NumberOfCharsWritten );
	    FillConsoleOutputAttribute( hConOut, Info.wAttributes,
					Info.dwCursorPosition.X + 1, Pos,
					&NumberOfCharsWritten );
	  return;

	  case 2:		// ESC[2K Clear whole line.
	    Pos.X = 0;
	    Pos.Y = Info.dwCursorPosition.Y;
	    FillConsoleOutputCharacter( hConOut, ' ', Info.dwSize.X, Pos,
					&NumberOfCharsWritten );
	    FillConsoleOutputAttribute( hConOut, Info.wAttributes,
					Info.dwSize.X, Pos,
					&NumberOfCharsWritten );
	  return;

	  default:
	  return;
	}

      case 'X':                 // ESC[#X Erase # characters.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[X == ESC[1X
	if (es_argc != 1) return;
	FillConsoleOutputCharacter( hConOut, ' ', es_argv[0],
				    Info.dwCursorPosition,
				    &NumberOfCharsWritten );
	FillConsoleOutputAttribute( hConOut, Info.wAttributes, es_argv[0],
				    Info.dwCursorPosition,
				    &NumberOfCharsWritten );
      return;

      case 'L':                 // ESC[#L Insert # blank lines.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[L == ESC[1L
	if (es_argc != 1) return;
	Rect.Left   = 0;
	Rect.Top    = Info.dwCursorPosition.Y;
	Rect.Right  = Info.dwSize.X - 1;
	Rect.Bottom = Info.dwSize.Y - 1;
	Pos.X = 0;
	Pos.Y = Info.dwCursorPosition.Y + es_argv[0];
	CharInfo.Char.UnicodeChar = ' ';
	CharInfo.Attributes = Info.wAttributes;
	ScrollConsoleScreenBuffer( hConOut, &Rect, NULL, Pos, &CharInfo );
      return;

      case 'M':                 // ESC[#M Delete # lines.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[M == ESC[1M
	if (es_argc != 1) return;
	if (es_argv[0] > Info.dwSize.Y - Info.dwCursorPosition.Y)
	  es_argv[0] = Info.dwSize.Y - Info.dwCursorPosition.Y;
	Rect.Left   = 0;
	Rect.Top    = Info.dwCursorPosition.Y + es_argv[0];
	Rect.Right  = Info.dwSize.X - 1;
	Rect.Bottom = Info.dwSize.Y - 1;
	Pos.X = 0;
	Pos.Y = Info.dwCursorPosition.Y;
	CharInfo.Char.UnicodeChar = ' ';
	CharInfo.Attributes = Info.wAttributes;
	ScrollConsoleScreenBuffer( hConOut, &Rect, NULL, Pos, &CharInfo );
      return;

      case 'P':                 // ESC[#P Delete # characters.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[P == ESC[1P
	if (es_argc != 1) return;
	if (Info.dwCursorPosition.X + es_argv[0] > Info.dwSize.X - 1)
	  es_argv[0] = Info.dwSize.X - Info.dwCursorPosition.X;
	Rect.Left   = Info.dwCursorPosition.X + es_argv[0];
	Rect.Top    = Info.dwCursorPosition.Y;
	Rect.Right  = Info.dwSize.X - 1;
	Rect.Bottom = Info.dwCursorPosition.Y;
	CharInfo.Char.UnicodeChar = ' ';
	CharInfo.Attributes = Info.wAttributes;
	ScrollConsoleScreenBuffer( hConOut, &Rect, NULL, Info.dwCursorPosition,
				   &CharInfo );
      return;

      case '@':                 // ESC[#@ Insert # blank characters.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[@ == ESC[1@
	if (es_argc != 1) return;
	if (Info.dwCursorPosition.X + es_argv[0] > Info.dwSize.X - 1)
	  es_argv[0] = Info.dwSize.X - Info.dwCursorPosition.X;
	Rect.Left   = Info.dwCursorPosition.X;
	Rect.Top    = Info.dwCursorPosition.Y;
	Rect.Right  = Info.dwSize.X - 1 - es_argv[0];
	Rect.Bottom = Info.dwCursorPosition.Y;
	Pos.X = Info.dwCursorPosition.X + es_argv[0];
	Pos.Y = Info.dwCursorPosition.Y;
	CharInfo.Char.UnicodeChar = ' ';
	CharInfo.Attributes = Info.wAttributes;
	ScrollConsoleScreenBuffer( hConOut, &Rect, NULL, Pos, &CharInfo );
      return;

      case 'k':                 // ESC[#k
      case 'A':                 // ESC[#A Moves cursor up # lines
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[A == ESC[1A
	if (es_argc != 1) return;
	Pos.Y = Info.dwCursorPosition.Y - es_argv[0];
	if (Pos.Y < 0) Pos.Y = 0;
	Pos.X = Info.dwCursorPosition.X;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'e':                 // ESC[#e
      case 'B':                 // ESC[#B Moves cursor down # lines
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[B == ESC[1B
	if (es_argc != 1) return;
	Pos.Y = Info.dwCursorPosition.Y + es_argv[0];
	if (Pos.Y >= Info.dwSize.Y) Pos.Y = Info.dwSize.Y - 1;
	Pos.X = Info.dwCursorPosition.X;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'a':                 // ESC[#a
      case 'C':                 // ESC[#C Moves cursor forward # spaces
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[C == ESC[1C
	if (es_argc != 1) return;
	Pos.X = Info.dwCursorPosition.X + es_argv[0];
	if (Pos.X >= Info.dwSize.X) Pos.X = Info.dwSize.X - 1;
	Pos.Y = Info.dwCursorPosition.Y;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'j':                 // ESC[#j
      case 'D':                 // ESC[#D Moves cursor back # spaces
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[D == ESC[1D
	if (es_argc != 1) return;
	Pos.X = Info.dwCursorPosition.X - es_argv[0];
	if (Pos.X < 0) Pos.X = 0;
	Pos.Y = Info.dwCursorPosition.Y;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'E':                 // ESC[#E Moves cursor down # lines, column 1.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[E == ESC[1E
	if (es_argc != 1) return;
	Pos.Y = Info.dwCursorPosition.Y + es_argv[0];
	if (Pos.Y >= Info.dwSize.Y) Pos.Y = Info.dwSize.Y - 1;
	Pos.X = 0;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'F':                 // ESC[#F Moves cursor up # lines, column 1.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[F == ESC[1F
	if (es_argc != 1) return;
	Pos.Y = Info.dwCursorPosition.Y - es_argv[0];
	if (Pos.Y < 0) Pos.Y = 0;
	Pos.X = 0;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case '`':                 // ESC[#`
      case 'G':                 // ESC[#G Moves cursor column # in current row.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[G == ESC[1G
	if (es_argc != 1) return;
	Pos.X = es_argv[0] - 1;
	if (Pos.X >= Info.dwSize.X) Pos.X = Info.dwSize.X - 1;
	if (Pos.X < 0) Pos.X = 0;
	Pos.Y = Info.dwCursorPosition.Y;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'd':                 // ESC[#d Moves cursor row #, current column.
	if (es_argc == 0) es_argv[es_argc++] = 1; // ESC[d == ESC[1d
	if (es_argc != 1) return;
	Pos.Y = es_argv[0] - 1;
	if (Pos.Y < 0) Pos.Y = 0;
	if (Pos.Y >= Info.dwSize.Y) Pos.Y = Info.dwSize.Y - 1;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 'f':                 // ESC[#;#f
      case 'H':                 // ESC[#;#H Moves cursor to line #, column #
	if (es_argc == 0)
	  es_argv[es_argc++] = 1; // ESC[H == ESC[1;1H
	if (es_argc == 1)
	  es_argv[es_argc++] = 1; // ESC[#H == ESC[#;1H
	if (es_argc > 2) return;
	Pos.X = es_argv[1] - 1;
	if (Pos.X < 0) Pos.X = 0;
	if (Pos.X >= Info.dwSize.X) Pos.X = Info.dwSize.X - 1;
	Pos.Y = es_argv[0] - 1;
	if (Pos.Y < 0) Pos.Y = 0;
	if (Pos.Y >= Info.dwSize.Y) Pos.Y = Info.dwSize.Y - 1;
	SetConsoleCursorPosition( hConOut, Pos );
      return;

      case 's':                 // ESC[s Saves cursor position for recall later
	if (es_argc != 0) return;
	SavePos = Info.dwCursorPosition;
      return;

      case 'u':                 // ESC[u Return to saved cursor position
	if (es_argc != 0) return;
	SetConsoleCursorPosition( hConOut, SavePos );
      return;

      case 'n':                 // ESC[#n Device status report
	if (es_argc != 1) return; // ESC[n == ESC[0n -> ignored
	switch (es_argv[0])
	{
	  case 5:		// ESC[5n Report status
	    SendSequence( (char *)L"\33[0n" ); // "OK"
	  return;

	  case 6:		// ESC[6n Report cursor position
	  {
	    TCHAR buf[32];
	    wsprintf( buf, (const char *)L"\33[%d;%dR", Info.dwCursorPosition.Y + 1,
					  Info.dwCursorPosition.X + 1 );
	    SendSequence( buf );
	  }
	  return;

	  default:
	  return;
	}

      case 't':                 // ESC[#t Window manipulation
	if (es_argc != 1) return;
	if (es_argv[0] == 21)	// ESC[21t Report xterm window's title
	{
	  TCHAR buf[MAX_PATH*2];
	  DWORD len = GetConsoleTitle( buf+3, lenof(buf)-3-2 );
	  // Too bad if it's too big or fails.
	  buf[0] = ESC;
	  buf[1] = ']';
	  buf[2] = 'l';
	  buf[3+len] = ESC;
	  buf[3+len+1] = '\\';
	  buf[3+len+2] = '\0';
	  SendSequence( buf );
	}
      return;

      default:
      return;
    }
  }
  else // (prefix == ']')
  {
    // Ignore any \e]? or \e]> sequences.
    if (prefix2 != 0)
      return;

    if (es_argc == 1 && es_argv[0] == 0) // ESC]0;titleST
    {
      SetConsoleTitle( Pt_arg );
    }
  }
}

//-----------------------------------------------------------------------------
//   ParseAndPrintString(hDev, lpBuffer, nNumberOfBytesToWrite)
// Parses the string lpBuffer, interprets the escapes sequences and prints the
// characters in the device hDev (console).
// The lexer is a three states automata.
// If the number of arguments es_argc > MAX_ARG, only the MAX_ARG-1 firsts and
// the last arguments are processed (no es_argv[] overflow).
//-----------------------------------------------------------------------------

BOOL
ParseAndPrintString( HANDLE hDev,
		     LPCVOID lpBuffer,
		     DWORD nNumberOfBytesToWrite,
		     LPDWORD lpNumberOfBytesWritten
		     )
{
  DWORD   i;
  LPCTSTR s;

  if (hDev != hConOut)	// reinit if device has changed
  {
    hConOut = hDev;
    state = 1;
    shifted = FALSE;
  }
  for (i = nNumberOfBytesToWrite, s = (LPCTSTR)lpBuffer; i > 0; i--, s++)
  {
    if (state == 1)
    {
      if (*s == ESC) state = 2;
      else if (*s == SO) shifted = TRUE;
      else if (*s == SI) shifted = FALSE;
      else PushBuffer( *s );
    }
    else if (state == 2)
    {
      if (*s == ESC) ;	// \e\e...\e == \e
      else if ((*s == '[') || (*s == ']'))
      {
	FlushBuffer();
	prefix = *s;
	prefix2 = 0;
	state = 3;
	Pt_len = 0;
	*Pt_arg = '\0';
      }
      else if (*s == ')' || *s == '(') state = 6;
      else state = 1;
    }
    else if (state == 3)
    {
      if (is_digit( *s ))
      {
        es_argc = 0;
	es_argv[0] = *s - '0';
        state = 4;
      }
      else if (*s == ';')
      {
        es_argc = 1;
        es_argv[0] = 0;
	es_argv[1] = 0;
        state = 4;
      }
      else if (*s == '?' || *s == '>')
      {
	prefix2 = *s;
      }
      else
      {
        es_argc = 0;
        suffix = *s;
        InterpretEscSeq();
        state = 1;
      }
    }
    else if (state == 4)
    {
      if (is_digit( *s ))
      {
	es_argv[es_argc] = 10 * es_argv[es_argc] + (*s - '0');
      }
      else if (*s == ';')
      {
        if (es_argc < MAX_ARG-1) es_argc++;
	es_argv[es_argc] = 0;
	if (prefix == ']')
	  state = 5;
      }
      else
      {
	es_argc++;
        suffix = *s;
        InterpretEscSeq();
        state = 1;
      }
    }
    else if (state == 5)
    {
      if (*s == BEL)
      {
	Pt_arg[Pt_len] = '\0';
        InterpretEscSeq();
        state = 1;
      }
      else if (*s == '\\' && Pt_len > 0 && Pt_arg[Pt_len-1] == ESC)
      {
	Pt_arg[--Pt_len] = '\0';
        InterpretEscSeq();
        state = 1;
      }
      else if (Pt_len < lenof(Pt_arg)-1)
	Pt_arg[Pt_len++] = *s;
    }
    else if (state == 6)
    {
      // Ignore it (ESC ) 0 is implicit; nothing else is supported).
      state = 1;
    }
  }
  FlushBuffer();
  if (lpNumberOfBytesWritten != NULL)
    *lpNumberOfBytesWritten = nNumberOfBytesToWrite - i;
  return (i == 0);
}



//-----------------------------------------------------------------------------
//   IsConsoleHandle
// Determine if the handle is writing to the console, with processed output.
//-----------------------------------------------------------------------------
BOOL IsConsoleHandle( HANDLE h )
{
  DWORD mode;

  if (!GetConsoleMode( h, &mode ))
  {
    // This fails if the handle isn't opened for reading.  Fortunately, it
    // seems WriteConsole tests the handle before it tests the length.
    return WriteConsole( h, NULL, 0, &mode, NULL );
  }

  return (mode & ENABLE_PROCESSED_OUTPUT);
}


//-----------------------------------------------------------------------------
//   MyWrite...
// The new functions that must replace the original Write... functions.  These
// functions have exactly the same signature as the original ones.  This
// module is not hooked, so we can still call the original functions ourselves.
//-----------------------------------------------------------------------------

BOOL
WINAPI MyWriteConsoleA( HANDLE hCon, LPCVOID lpBuffer,
			DWORD nNumberOfCharsToWrite,
			LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved )
{
  LPWSTR buf;
  DWORD  len;
  BOOL	 rc = TRUE;

  if (IsConsoleHandle( hCon ))
  {
    UINT cp = GetConsoleOutputCP();
    DEBUGSTR( 4, (char *)L"\33WriteConsoleA: %lu \"%.*S\"",
	      nNumberOfCharsToWrite, nNumberOfCharsToWrite, lpBuffer );
    len = MultiByteToWideChar( cp, 0, lpBuffer, nNumberOfCharsToWrite, NULL,0 );
    buf = malloc( TSIZE(len) );
    if (buf == NULL)
    {
      if (lpNumberOfCharsWritten != NULL)
	*lpNumberOfCharsWritten = 0;
      return (nNumberOfCharsToWrite == 0);
    }
    MultiByteToWideChar( cp, 0, lpBuffer, nNumberOfCharsToWrite, buf, len );
    rc = ParseAndPrintString( hCon, buf, len, lpNumberOfCharsWritten );
    free( buf );
    if (rc && lpNumberOfCharsWritten != NULL &&
	      *lpNumberOfCharsWritten != nNumberOfCharsToWrite)
    {
      // Converting a multibyte character to Unicode results in a different
      // "character" count.  This causes some programs to think not everything
      // was written, so the difference is sent again.	Fudge the (presumably)
      // correct count.
      if (search_env( (const char *)L"ANSICON_API", prog ))
	*lpNumberOfCharsWritten = nNumberOfCharsToWrite;
    }
    return rc;
  }

  return WriteConsoleA( hCon, lpBuffer, nNumberOfCharsToWrite,
			lpNumberOfCharsWritten, lpReserved );

}

BOOL
WINAPI MyWriteConsoleW( HANDLE hCon, LPCVOID lpBuffer,
			DWORD nNumberOfCharsToWrite,
			LPDWORD lpNumberOfCharsWritten, LPVOID lpReserved )
{
  if (IsConsoleHandle( hCon ))
  {
    DEBUGSTR( 4, (char *)L"\33WriteConsoleW: %lu \"%.*s\"",
	      nNumberOfCharsToWrite, nNumberOfCharsToWrite, lpBuffer );
    return ParseAndPrintString( hCon, lpBuffer,
				nNumberOfCharsToWrite,
				lpNumberOfCharsWritten );
  }

  return WriteConsoleW( hCon, lpBuffer, nNumberOfCharsToWrite,
			lpNumberOfCharsWritten, lpReserved );
}

BOOL
WINAPI MyWriteFile( HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
		    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped )
{
  if (IsConsoleHandle( hFile ))
  {
    DEBUGSTR( 4, (char *)L"WriteFile->" );
    return MyWriteConsoleA( hFile, lpBuffer,
			    nNumberOfBytesToWrite,
			    lpNumberOfBytesWritten,
			    lpOverlapped );
  }

  return WriteFile( hFile, lpBuffer, nNumberOfBytesToWrite,
		    lpNumberOfBytesWritten, lpOverlapped );
}



UINT
WINAPI My_lwrite( HFILE hFile, LPCSTR lpBuffer, UINT uBytes )
{
//  if (IsConsoleHandle( HHFILE hFile ))
  if (IsConsoleHandle( &hFile ))
  {
    DWORD written;
    DEBUGSTR( 4, (char *)L"_lwrite->" );
//    MyWriteConsoleA( HHFILE hFile, lpBuffer, uBytes, &written, NULL );
    MyWriteConsoleA( &hFile, lpBuffer, uBytes, &written, NULL );
    return written;
  }

  return _lwrite( hFile, lpBuffer, uBytes );
}

long
WINAPI My_hwrite( HFILE hFile, LPCSTR lpBuffer, long lBytes )
{
//  if (IsConsoleHandle( HHFILE hFile ))
  if (IsConsoleHandle( &hFile ))
  {
    DWORD written;
    DEBUGSTR( 4, (char *)L"_hwrite->" );
//    MyWriteConsoleA( HHFILE hFile, lpBuffer, lBytes, &written, NULL );
    MyWriteConsoleA( &hFile, lpBuffer, lBytes, &written, NULL );
    return written;
  }

  return _hwrite( hFile, lpBuffer, lBytes );
}


//-----------------------------------------------------------------------------
//   OriginalAttr()
// Determine the original attributes for use by \e[m.
//-----------------------------------------------------------------------------
void OriginalAttr( void )
{
  HANDLE hConOut;
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  hConOut = CreateFile( (char *)L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
				    FILE_SHARE_READ | FILE_SHARE_WRITE,
				    NULL, OPEN_EXISTING, 0, 0 );
  if (!GetConsoleScreenBufferInfo( hConOut, &csbi ))
    csbi.wAttributes = 7;
  CloseHandle( hConOut );

  if (s_flag == GRM_INIT && s_pid == GetCurrentProcessId())
  {
    s_flag = 0;
    grm = s_grm;
  }
  else
  {
    if (GetEnvironmentVariable( (char *)L"ANSICON_REVERSE", NULL, 0 ))
    {
      SetEnvironmentVariable( (char *)L"ANSICON_REVERSE", NULL );
      grm.reverse = TRUE;
      grm.foreground = attr2ansi[(csbi.wAttributes >> 4) & 7];
      grm.background = attr2ansi[csbi.wAttributes & 7];
      grm.bold	     = (csbi.wAttributes & BACKGROUND_INTENSITY) >> 4;
      grm.underline  = (csbi.wAttributes & FOREGROUND_INTENSITY) << 4;
    }
    else
    {
      grm.foreground = attr2ansi[csbi.wAttributes & 7];
      grm.background = attr2ansi[(csbi.wAttributes >> 4) & 7];
      grm.bold	     = csbi.wAttributes & FOREGROUND_INTENSITY;
      grm.underline  = csbi.wAttributes & BACKGROUND_INTENSITY;
    }
  }
  if (!GetEnvironmentVariable( (char *)L"ANSICON_DEF", NULL, 0 ))
  {
    TCHAR  def[4];
    LPTSTR a = def;
    if (grm.reverse)
    {
      *a++ = '-';
      csbi.wAttributes = ((csbi.wAttributes >> 4) & 15)
		       | ((csbi.wAttributes & 15) << 4);
    }
    wsprintf( a, (char *)L"%X", csbi.wAttributes & 255 );
    SetEnvironmentVariable( (char *)L"ANSICON_DEF", def );
  }
  set_ansicon( &csbi );
}

void set_ansicon( PCONSOLE_SCREEN_BUFFER_INFO pcsbi )
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  TCHAR buf[64];

  if (pcsbi == NULL)
  {
    HANDLE hConOut;
    hConOut = CreateFile( (const char *)L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
				      FILE_SHARE_READ | FILE_SHARE_WRITE,
				      NULL, OPEN_EXISTING, 0, 0 );
    GetConsoleScreenBufferInfo( hConOut, &csbi );
    CloseHandle( hConOut );
    pcsbi = &csbi;
  }

  wsprintf( buf, (const char *)L"%dx%d (%dx%d)",
	    pcsbi->dwSize.X, pcsbi->dwSize.Y,
	    pcsbi->srWindow.Right - pcsbi->srWindow.Left + 1,
	    pcsbi->srWindow.Bottom - pcsbi->srWindow.Top + 1 );
  SetEnvironmentVariable( (const char *)L"ANSICON", buf );
}

// Search an environment variable for a string.
BOOL search_env( LPCTSTR var, LPCTSTR val )
{
  static LPTSTR env;
  static DWORD	env_len;
  DWORD len;
  BOOL	not;

  len = GetEnvironmentVariable( var, env, env_len );
  if (len == 0)
    return FALSE;

  if (len > env_len)
  {
    LPTSTR tmp = realloc( env, TSIZE(len) );
    if (tmp == NULL)
      return FALSE;
    env = tmp;
    env_len = len;
    GetEnvironmentVariable( var, env, env_len );
  }

  not = (*env == '!');
  if (not && env[1] == '\0')
    return TRUE;

  for (var = (const char *)wcstok( (unsigned short *)(env + not), (const unsigned short *)L";" WCONTEXT); var; var = (const char *)wcstok( NULL, (const unsigned short *)L";" WCONTEXT))
    if (_wcsicmp( (const unsigned short *)val, (const unsigned short *)var ) == 0)
      return !not;

  return not;
}
#endif	// WINDOZE
