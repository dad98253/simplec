#ifdef WINDOZE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void MsgBox(char *buffer)
{
	fprintf(stderr,"%s\n");
	return;
}
#endif	// WINDOZE
