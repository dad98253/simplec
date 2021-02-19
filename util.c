#ifdef WINDOZE
/*
  util.c - Utility functions.
*/

#include "ansicon.h"
#include "version.h"

#ifdef WIN10
#define _snprintf _snprintf_s
#define BUFCNT ,_countof(tempfile)
#define _vsnwprintf _vsnwprintf_s
#define BUFCNT2 ,_countof(szBuffer)
#define _snwprintf _snwprintf_s
#define BUFCNT3 ,_countof(szEscape)
#else // WIN10
#define BUFCNT
#define BUFCNT2
#define BUFCNT3
#endif

TCHAR	prog_path[MAX_PATH];
LPTSTR	prog;
int	log_level;
char	tempfile[MAX_PATH];
DWORD	pid;


// Get just the name of the program: "C:\path\program.exe" -> "program".
// Returns a pointer within program; it is modified to remove the extension.
LPTSTR get_program_name( LPTSTR program )
{
  LPTSTR name, ext;

  if (program == NULL)
  {
    GetModuleFileName( NULL, prog_path, lenof(prog_path) );
    program = prog_path;
  }
  name = wcsrchr( program, '\\' );
  if (name != NULL)
    ++name;
  else
    name = program;
  ext = wcsrchr( name, '.' );
  if (ext != NULL && ext != name)
    *ext = '\0';

  return name;
}


void DEBUGSTR( int level, LPTSTR szFormat, ... )
{
  TCHAR szBuffer[1024], szEscape[1024];
  va_list pArgList;
  HANDLE mutex;
  DWORD wait;
  FILE* file;

  if ((log_level & 3) < level && !(level & 4 & log_level))
    return;

  if (*tempfile == '\0')
  {
#ifdef WIN10
	  char* pValue;
	  size_t len;
	  errno_t err = _dupenv_s(&pValue, &len, "TEMP");
	  if (err) {
		  perror(" The following error occurred in _dupenv_s when attempting to read the TEMP environemnt variable: ");
		  exit(EXIT_FAILURE);
	  }
	  _snprintf(tempfile BUFCNT, MAX_PATH, "%s\\ansicon.log", pValue);
	  free(pValue);
#else // WIN10
	_snprintf( tempfile BUFCNT, MAX_PATH, "%s\\ansicon.log", getenv( "TEMP" ) );
#endif // WIN10
    pid = GetCurrentProcessId();
  }
  if (szFormat == NULL)
  {
#ifdef WIN10
	  errno_t err;
	  err = fopen_s(&file, tempfile, (log_level & 8) ? "at" : "wt");
	  if (err) {
		  fprintf(stderr, "fopen_s() failed with error %d when attempting to open %s with mode = %s\n", err, tempfile, (log_level & 8) ? "at" : "wt");
		  file = NULL;
	  }
#else // WIN10
    file = fopen( tempfile, (log_level & 8) ? "at" : "wt" );
#endif // WIN10
    if (file != NULL)
    {
      SYSTEMTIME now;
      GetLocalTime( &now );
      fprintf( file, "ANSICON (" BITSA "-bit) v" PVERSA " log (%d) started "
		      "%d-%.2d-%.2d %d:%.2d:%.2d\n",
		     log_level,
		     now.wYear, now.wMonth, now.wDay,
		     now.wHour, now.wMinute, now.wSecond );
      fclose( file );
    }
    return;
  }

  va_start( pArgList, szFormat );
  _vsnwprintf( szBuffer BUFCNT2, lenof(szBuffer), szFormat, pArgList );
  va_end( pArgList );

  szFormat = szBuffer;
  if (*szFormat == '\33')
  {
    BOOL first = TRUE;
    LPTSTR pos = szEscape;
    while (*++szFormat != '\0' && pos < szEscape + lenof(szEscape) - 4)
    {
      if (*szFormat < 32)
      {
	*pos++ = '\\';
	switch (*szFormat)
	{
	  case '\a': *pos++ = 'a'; break;
	  case '\b': *pos++ = 'b'; break;
	  case '\t': *pos++ = 't'; break;
	  case '\r': *pos++ = 'r'; break;
	  case '\n': *pos++ = 'n'; break;
	  case	27 : *pos++ = 'e'; break;
	  default:
	    pos += _snwprintf( pos BUFCNT3, 32, L"%.*o",
			     (szFormat[1] >= '0' && szFormat[1] <= '7') ? 3 : 1,
			     *szFormat );
	}
      }
      else
      {
	if (*szFormat == '"')
	{
	  if (first)
	    first = FALSE;
	  else if (szFormat[1] != '\0')
	    *pos++ = '\\';
	}
	*pos++ = *szFormat;
      }
    }
    *pos = '\0';
    szFormat = szEscape;
  }

  mutex = CreateMutex( NULL, FALSE, L"ANSICON_debug_file" );
  wait	= WaitForSingleObject( mutex, 500 );
#ifdef WIN10
  errno_t err;
  err = fopen_s(&file, tempfile, "at");
  if (err) {
	  fprintf(stderr, "fopen_s() failed with error %d when attempting to open %s with mode = %s\n", err, tempfile, "at");
	  file = NULL;
  }
#else // WIN10
  file	= fopen( tempfile, "at" ); // _fmode might be binary
#endif // WIN10
  if (file != NULL)
  {
    fwprintf( file, L"%s (%lu): %s\n", prog, pid, szFormat );
    fclose( file );
  }
  if (wait == WAIT_OBJECT_0)
    ReleaseMutex( mutex );
  CloseHandle( mutex );
}
#endif	// WINDOZE
