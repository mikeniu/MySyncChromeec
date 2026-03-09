#include <iostream>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <unistd.h>

#include "iapcontrol.h"
#include "filecontrol.h"

using namespace std;


typedef struct _CMDLINE
{
    unsigned short Vid;
    unsigned short Pid;
    string FilePath;
}CMDLINE;




//Parse Command Line
int ParseCommandLine(char **pArgv, int Argc, CMDLINE *pCmdLine);

int sendData(unsigned char *pBuff, int length);
int readData(unsigned char *pBuff, int length);

//Iap Process
int iapWriteRomCode(unsigned char *pRom, uint32_t romLength, unsigned char *pOpt, uint32_t optLength);
int getChecksum(unsigned char *pBuff, int length, uint32_t *pChecksum);

//Set Progress Bar
void setProgressBar(int progress, int totalSize);

//Initial / Release Resource
void appInitial();
void appExit();
