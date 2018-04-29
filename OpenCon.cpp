#ifdef WINDOZE
#include <windows.h> 
//#include "passwin.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


void NewLine(void); 
void ScrollScreenBuffer(HANDLE, INT); 
 
HANDLE hStdin, hStderr; 
HANDLE hConsoleBuffer;
CONSOLE_SCREEN_BUFFER_INFO csbiInfo; 
WORD wOldColorAttrs; 
extern "C" void MsgBox(char *buffer);
extern "C" int OpenCon(HANDLE *hStdout);
extern "C" int SetScreenColor( int fcolorcode, int fintensity, int bcolorcode, int bintensity );
extern "C" int ResetScreenColor( void );
 
int OpenCon(HANDLE *hStdout) 
{ 
    LPSTR lpszPrompt1 = "Debug console opened for passwin";
    LPSTR lpszPrompt2 = "(keyboard input to this window is ignored)\n";
//    CHAR chBuffer[256]; 
//    DWORD cRead; 
    DWORD cWritten, fdwMode, fdwOldMode; 

	FreeConsole();

	if (!AllocConsole())
    {
        MsgBox("AllocConsole failed"); 
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
    *hStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
	hStderr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE || 
        hStdout == INVALID_HANDLE_VALUE || hStderr == INVALID_HANDLE_VALUE) 
    {
        MsgBox("GetStdHandle failed");
        return 1;
    }
    // Save the current text colors. 

    if (! GetConsoleScreenBufferInfo(hConsoleBuffer, &csbiInfo)) 
    {
        MsgBox("GetConsoleScreenBufferInfo failed in SetUpDebugConsole"); 
        return 1;
    }

    wOldColorAttrs = csbiInfo.wAttributes; 

    // Set the text attributes to draw red text on black background. 

    if (! SetConsoleTextAttribute(hStderr, FOREGROUND_RED | 
            FOREGROUND_INTENSITY))
    {
        MsgBox("SetConsoleTextAttribute failed in SetUpDebugConsole"); 
        return 1;
    }

    // Set the screen buffer size to 150 columns by 250 lines. 

	COORD dwScreenBufferSize;
	dwScreenBufferSize.X = 150;
	dwScreenBufferSize.Y = 250;

    if (! SetConsoleScreenBufferSize(hStderr, dwScreenBufferSize) )
    {
        MsgBox("SetConsoleScreenBufferSize failed in SetUpDebugConsole"); 
        return 1;
    }

    // Write to STDERR by using the default 
    // modes. Input is echoed automatically, and ReadFile 
    // does not return until a carriage return is typed. 
    // 
    // The default input modes are line, processed, and echo. 
    // The default output modes are processed and wrap at EOL. 

        if (! WriteFile( 
            hStderr,               // output handle 
            lpszPrompt1,           // prompt string 
            lstrlenA(lpszPrompt1), // string length 
            &cWritten,             // bytes written 
            NULL) )                // not overlapped 
        {
            MsgBox("WriteFile failed in SetUpDebugConsole"); 
            return 1;
        }


    // Turn off the line input and echo input modes 

    if (! GetConsoleMode(hConsoleBuffer, &fdwOldMode)) 
    {
       MsgBox("GetConsoleMode failed in SetUpDebugConsole");
       return 1;
    }

    fdwMode = fdwOldMode & 
        ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT); 
    if (! SetConsoleMode(hConsoleBuffer, fdwMode)) 
    {
       MsgBox("SetConsoleMode failed in SetUpDebugConsole");
       return 1;
    }

    // ReadFile returns when any input is available.  
    // WriteFile is used to echo input. 

    NewLine();

        if (! WriteFile( 
            hStderr,               // output handle 
            lpszPrompt2,           // prompt string 
            lstrlenA(lpszPrompt2), // string length 
            &cWritten,             // bytes written 
            NULL) )                // not overlapped 
        {
            MsgBox("second WriteFile failed in SetUpDebugConsole");
            return 1;
        }
/*
        if (! ReadFile(hStdin, chBuffer, 1, &cRead, NULL)) 
            break; 
        if (chBuffer[0] == '\r')
            NewLine();
        else if (! WriteFile(hStdout, chBuffer, cRead, 
            &cWritten, NULL)) break;
        else
            NewLine();
        if (chBuffer[0] == 'q') break; 
*/
    // Restore the original console mode. 

    SetConsoleMode(hConsoleBuffer, fdwOldMode);

    // Restore the original text colors. 

    SetConsoleTextAttribute(hStderr, wOldColorAttrs);

    return 0;
}

// The NewLine function handles carriage returns when the processed 
// input mode is disabled. It gets the current cursor position 
// and resets it to the first cell of the next row. 
 
void NewLine(void) 
{ 
    if (! GetConsoleScreenBufferInfo(hConsoleBuffer, &csbiInfo)) 
    {
        MsgBox("GetConsoleScreenBufferInfo failed in NewLine");
        return;
    }

    csbiInfo.dwCursorPosition.X = 0; 

    // If it is the last line in the screen buffer, scroll 
    // the buffer up. 

    if ((csbiInfo.dwSize.Y-1) == csbiInfo.dwCursorPosition.Y) 
    { 
        ScrollScreenBuffer(hConsoleBuffer, 1); 
    } 

    // Otherwise, advance the cursor to the next line. 

    else csbiInfo.dwCursorPosition.Y += 1; 
 
    if (! SetConsoleCursorPosition(hStderr, 
        csbiInfo.dwCursorPosition)) 
    {
        MsgBox("SetConsoleCursorPosition failed in NewLine");
        return;
    }
	return;
} 

void ScrollScreenBuffer(HANDLE h, INT x)
{
    SMALL_RECT srctScrollRect, srctClipRect;
    CHAR_INFO chiFill;
    COORD coordDest;

    srctScrollRect.Left = 0;
    srctScrollRect.Top = 1;
    srctScrollRect.Right = csbiInfo.dwSize.X - (SHORT)x; 
    srctScrollRect.Bottom = csbiInfo.dwSize.Y - (SHORT)x; 
 
    // The destination for the scroll rectangle is one row up. 
 
    coordDest.X = 0; 
    coordDest.Y = 0; 
 
    // The clipping rectangle is the same as the scrolling rectangle. 
    // The destination row is left unchanged. 
 
    srctClipRect = srctScrollRect; 
 
    // Set the fill character and attributes. 
 
    chiFill.Attributes = FOREGROUND_RED|FOREGROUND_INTENSITY; 
    chiFill.Char.AsciiChar = (char)' '; 
 
    // Scroll up one line. 
 
    ScrollConsoleScreenBuffer( 
        h,               // screen buffer handle 
        &srctScrollRect, // scrolling rectangle 
        &srctClipRect,   // clipping rectangle 
        coordDest,       // top left destination cell 
        &chiFill);       // fill character and color 

	return;
}

int SetScreenColor( int fcolorcode, int fintensity, int bcolorcode, int bintensity )
{
	WORD wColorAttrs = 0;
/*
#define FOREGROUND_BLUE      0x0001 // text color contains blue.
#define FOREGROUND_GREEN     0x0002 // text color contains green.
#define FOREGROUND_RED       0x0004 // text color contains red.
#define FOREGROUND_INTENSITY 0x0008 // text color is intensified.
#define BACKGROUND_BLUE      0x0010 // background color contains blue.
#define BACKGROUND_GREEN     0x0020 // background color contains green.
#define BACKGROUND_RED       0x0040 // background color contains red.
#define BACKGROUND_INTENSITY 0x0080 // background color is intensified.
#define COMMON_LVB_LEADING_BYTE    0x0100 // Leading Byte of DBCS
#define COMMON_LVB_TRAILING_BYTE   0x0200 // Trailing Byte of DBCS
#define COMMON_LVB_GRID_HORIZONTAL 0x0400 // DBCS: Grid attribute: top horizontal.
#define COMMON_LVB_GRID_LVERTICAL  0x0800 // DBCS: Grid attribute: left vertical.
#define COMMON_LVB_GRID_RVERTICAL  0x1000 // DBCS: Grid attribute: right vertical.
#define COMMON_LVB_REVERSE_VIDEO   0x4000 // DBCS: Reverse fore/back ground attribute.
#define COMMON_LVB_UNDERSCORE      0x8000 // DBCS: Underscore.

#define COMMON_LVB_SBCSDBCS        0x0300 // SBCS or DBCS flag.
*/
/*	int i,j,k,l;
		for (j=0;j<2;j++) {
				for (l=0;l<2;l++) {
	for (i=0;i<8;i++) {
			for (k=0;k<8;k++) {
					fcolorcode = i;
					bcolorcode = k;
					fintensity = j;
					bintensity = l;*/
	int fccode;
	fccode = ((unsigned int)fcolorcode) % 8;
	int bccode;
	bccode = ((unsigned int)bcolorcode) % 8;
	int ficode;
	ficode = ((unsigned int)fintensity) % 2;
	int bicode;
	bicode = ((unsigned int)bintensity) % 2;
	wColorAttrs = fccode | (ficode<<3) | (bccode<<4) | (bicode<<7); 
	SetConsoleTextAttribute(hStderr, wColorAttrs);
/*					printf("i=%i,j=%i,k=%i,l=%i,%04x",i,j,k,l,wColorAttrs);
				}
			printf("\n");
			}
		}
	}*/
		
	return (1);
}

int ResetScreenColor( void )
{
	SetConsoleTextAttribute(hStderr, wOldColorAttrs);
	return (1);
}
#endif	// WINDOZE
