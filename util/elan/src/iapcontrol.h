#include <iostream>
#include <fstream>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace std;


#include "globalData.h"


int runIap(uint8_t iapMode);
int finishIap(uint32_t checksum);
int softwareReset();
int getFwVersion(uint32_t *pfwVer, uint8_t *pbootType);
int abortProcess();
int writeRom(uint32_t length);
int writeRomFinish(uint32_t checksum);
int setRomAddress(uint32_t address);
int setOptionAddress(uint32_t address);
int readRom(uint32_t length);
int readRomFinish(uint32_t checksum);

int iapSendCommand(unsigned char *pBuff, int length);
int iapRecvStatus(unsigned char *pBuff, int length);
int iapSendData(unsigned char *pBuff, int length);
int iapRecvData(unsigned char *pBuff, int length);


//Device Enumeration
int connectDevice(unsigned short vid, unsigned short pid);
int getDeviceCount(unsigned short vid, unsigned short pid);


void releaseResource();
void initialIapControl();
void releaseIapcontrol();
