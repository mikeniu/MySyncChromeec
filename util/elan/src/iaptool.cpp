#include "iaptool.h"

#define CMDLINE_FILE_PATH	"-F"
#define CMDLINE_VID		"-V"
#define CMDLINE_PID		"-P"

#define TIME_OUT		1000

#define STR_LENGTH		255


#define BOOT_VID		0x04F3
#define BOOT_PID		0x0910


CMDLINE g_CmdLine;


int g_DispNext = 0;


int main(int argc, char *argv[])
{    
    unsigned char *pBinary = NULL;    
    uint32_t binSize = 0;
    int devSta = 0;    
    
    
 
   
    
    printf("[IAP] main - start\n");   
    
    appInitial();   
    
    ParseCommandLine(argv, argc, &g_CmdLine);   
   
    //Connect Device
    devSta = connectDevice(BOOT_VID, BOOT_PID);
   
    printf("[IAP] main - device status: %d\n", devSta);   
    
    if(devSta == DEVSTA_NON || devSta == DEVSTA_REMOVE)
    {
        printf("[IAP] main - device can't be found\n");
        goto Exit;
    }    
    
    //Get File Size and Content
    if(getBinary(g_CmdLine.FilePath, NULL, &binSize))
    {
        printf("[IAP] main - get binary file size fail\n");
        goto Exit;   
    }
    
    printf("[IAP] main - binary file size %ld bytes\n", (long int)binSize);
    
    pBinary = new unsigned char [binSize];
    memset(pBinary, 0x00, binSize);
    
    if(getBinary(g_CmdLine.FilePath, pBinary, &binSize))
    {
        printf("[IAP] main - get binary content fail\n");
        goto Exit;  
    }   
    
    //Write Rom    
    if(iapWriteRomCode(pBinary, binSize, NULL, 0))
    {
        printf("[IAP] main - iap write rom fail\n");
        goto Exit;
    }
    
Exit:    
    
    
    if(pBinary)
    {
    	delete[] pBinary;
    	pBinary = NULL; 
    }   
   
    printf("[IAP] main - exit\n");
    
    //App Exit
    appExit();
   
    return 0;
}

int ParseCommandLine(char **pArgv, int Argc, CMDLINE *pCmdLine)
{

    if(pArgv == NULL || Argc == 0)
        return -1;
    
    //Index 0 is application name
    for(int i = 1; i < Argc; i += 2)
    {	
        //printf("[IAP] Parase Command Line (%s) - %s\n", pArgv[i], pArgv[i + 1]);    
          
    	if(strcmp(pArgv[i], CMDLINE_FILE_PATH) == 0)
   	{
   	    pCmdLine->FilePath = pArgv[i + 1];	    
	    
	    printf("[IAP] Parase Command Line - File Path: %s\n", pCmdLine->FilePath.c_str());	    
    	}
    	else if(strcmp(pArgv[i], CMDLINE_VID) == 0)
    	{
    	    pCmdLine->Vid = strtol(pArgv[i + 1], NULL, 16);
    	    
    	    printf("[IAP] Parase Command Line - Vid: 0x%04X\n", pCmdLine->Vid);
    	}
    	else if(strcmp(pArgv[i], CMDLINE_PID) == 0)
    	{
    	    pCmdLine->Pid = strtol(pArgv[i + 1], NULL, 16);
    	    
    	    printf("[IAP] Parase Command Line - Pid: 0x%04X\n", pCmdLine->Pid);
    	}    	 	
    }


    return 0;
}

int sendData(unsigned char *pBuff, int length)
{
    const char funcName[] = "send data";
    int index = 0;
    int pageSize = 64;
    int result = 0;
    
    
    
    if(pBuff == NULL)
	return-1;
	
    while(1)
    {   
    	if(iapSendData(pBuff + index, pageSize))
    	{
    	    printf("[IAP] %s - fail, count: %d\n", funcName, index);
    	    result = -1;
    	    break;
    	}
    	
    	//printf("[IAP] %s - count: %d, data: 0x%08X\n", funcName, index, *(uint32_t*)(pBuff + index));	
    	index += pageSize;    	
    	
    	setProgressBar(index, length);
    	
    	if(index >= length)
	    break;
    }    
    
    return result;
}

int readData(unsigned char *pBuff, int length)
{
    const char funcName[] = "read data";
    int index = 0;
    int pageSize = 64;
    int result = 0;
    
    
    
    if(pBuff == NULL)
	return-1;
    
    while(1)
    {   
    	if(iapRecvData(pBuff + index, pageSize))
    	{
    	    printf("[IAP] %s - fail, count: %d\n", funcName, index);
    	    result = -1;
    	    break;
    	}
    	
    	//printf("[IAP] %s - count: %d, data: 0x%08X\n", funcName, index, *(uint32_t*)(pBuff + index));	
    	index += pageSize;
    	
    	setProgressBar(index, length);
    	
    	if(index >= length)
	    break;
    }    
    
    return result;
}

int iapWriteRomCode(unsigned char *pRom, uint32_t romLength, unsigned char *pOpt, uint32_t optLength)
{   
    const char funcName[] = "iap process";
    unsigned char *pRomRead = NULL;    
    uint32_t romAddr = 0x0000;    
    uint32_t checksum = 0x0000;
    uint32_t fwVer = 0;
    uint8_t fwVer_Google = 0;
    uint8_t bootType = 0; 
    uint8_t iapMode = 0;    
    int result = 0;
    
    
    
    
    
    pRomRead = new unsigned char [romLength];
    memset(pRomRead, 0x00, romLength);
    
    
    //abort process
    if(abortProcess())
    {
        printf("[IAP] %s - abort process fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    //get fw version
    if(getFwVersion(&fwVer, &bootType))
    {
        printf("[IAP] %s - get fw version fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    //printf("[IAP] %s - fw ver: 0x%08X, boot type: 0x%02X\n", funcName, fwVer, bootType); 
    
    fwVer_Google = fwVer >> 8;    
    //printf("[IAP] %s - fw ver id: 0x%02X\n", funcName, fwVer_Google);
    //fw ver start from 0x10 is only for Google
    if(fwVer_Google != 0x10)
    {
     	printf("[IAP] %s - boot code is not support this project\n", funcName);
        result = -1;
        goto Exit;   
    }      
    
    //run iap    
    if(runIap(iapMode))
    {
        printf("[IAP] %s - run iap fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    //write rom
    printf("[IAP] %s - write rom data\n", funcName);
    if(setRomAddress(romAddr))
    {
        printf("[IAP] %s - set (write) rom address fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    if(writeRom(romLength))
    {
        printf("[IAP] %s - write rom command fail\n", funcName);
        result = -1;
        goto Exit;    
    }      
    
    if(sendData(pRom, romLength))
    {
        printf("[IAP] %s - write rom data fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    if(getChecksum(pRom, romLength, &checksum))
    {
        printf("[IAP] %s - calculate (write rom) checksum fail\n", funcName);
        result = -1;
        goto Exit;    
    }
    
    //printf("[IAP] %s - write rom checksum: 0x%08X\n", funcName, checksum);
    if(writeRomFinish(checksum))
    {
        printf("[IAP] %s - write rom finish fail\n", funcName);
        result = -1;
        goto Exit;      
    }
    
    //read rom
    printf("[IAP] %s - verify rom data\n", funcName);
    if(setRomAddress(romAddr))
    {
        printf("[IAP] %s - set (read) rom address fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    if(readRom(romLength))
    {
        printf("[IAP] %s - read rom command fail\n", funcName);
        result = -1;
        goto Exit;    
    }  
    
    if(readData(pRomRead, romLength))
    {
        printf("[IAP] %s - read rom data fail\n", funcName);
        result = -1;
        goto Exit;   
    } 
    
    if(getChecksum(pRomRead, romLength, &checksum))
    {
        printf("[IAP] %s - calculate (read rom) checksum fail\n", funcName);
        result = -1;
        goto Exit;    
    }
    
    if(readRomFinish(checksum))
    {
        printf("[IAP] %s - read rom finish fail\n", funcName);
        result = -1;
        goto Exit;      
    }
   
    //compare data
    if(memcmp(pRom, pRomRead, romLength))
    {
        printf("[IAP] %s - verify rom data fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    //finish iap
    if(getChecksum(pRom, romLength, &checksum))
    {
        printf("[IAP] %s - calculate (write rom) checksum fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    if(finishIap(checksum))
    {
        printf("[IAP] %s - finish iap fail\n", funcName);
        result = -1;
        goto Exit;     
    }
    
    printf("[IAP] %s - iap success\n", funcName);
    
    if(softwareReset())
    {
        printf("[IAP] %s - sw reset fail\n", funcName);
        result = -1;
        goto Exit;   
    }
    
    printf("[IAP] %s - sw reset\n", funcName);
    
Exit:
 
 
    if(pRomRead)
    {
        delete[] pRomRead;
        pRomRead = NULL;    
    }
    
    return result;	
}

int getChecksum(unsigned char *pBuff, int length, uint32_t *pChecksum)
{
    uint32_t checksum = 0;
    int result = 0;
    
    
    
    for(int i = 0; i < length; i+=4)
    {
        checksum += *(unsigned long*)(pBuff + i);
    }
    
    *pChecksum = checksum;    
    
    return result;
}

void setProgressBar(int progress, int totalSize)
{
    int step = 1;   
    int percent = 0;    
    
    percent = (100 * (progress)) / totalSize;   
    
    if(percent >= g_DispNext)
    {
        printf("\r[%s%s]", std::string(percent / 5, '#').c_str(), std::string(100 / 5 - percent / 5, ' ').c_str());
        printf(" %d %% [Data %d of %d]", percent, progress, totalSize);
        //std::cout.flush();
        fflush(stdout);
        
        g_DispNext += step;   
    }
    
    if(percent >= 100)
    {
        g_DispNext = 0;
        printf("\n");
    }
}

void appInitial()
{
    g_CmdLine.Vid = BOOT_VID;
    g_CmdLine.Pid = BOOT_PID;
    
    initialIapControl();
}

void appExit()
{
    releaseIapcontrol();
}    
