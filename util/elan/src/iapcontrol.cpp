#include "iapcontrol.h"

#define DEBUG_LOG			0

#define EP1_LENGTH			8
#define EP2_LENGTH			8
#define EP3_LENGTH			64
#define EP4_LENGTH			64

#define CMD_RUN_IAP			0x20
#define CMD_FINISHED_IAP		0x21
#define CMD_SOFTWARE_RESET		0x22
#define CMD_GET_FW_VERSION		0x41
#define CMD_ABORT_PROCESS		0x48
#define CMD_WRITE_ROM			0xA0
#define CMD_WRITE_ROM_FINISH		0xA1
#define CMD_WRITE_OPTION		0xA2
#define CMD_WRITE_OPTION_FINISH		0xA3
#define CMD_SET_ROM_ADDRESS		0xA4
#define CMD_SET_OPTION_ADDRESS		0xA5
#define CMD_READ_ROM			0xE0
#define CMD_READ_ROM_FINISH		0xE1
#define CMD_READ_OPTION			0xE2
#define CMD_READ_OPTION_FINISH		0xE3


#define ERR_STA_READY		0x00
#define ERR_STA_SUCCESS		0x01

int g_deviceCount = 1;
DEVINFO g_devInfo;



int runIap(uint8_t iapMode)
{
    const char funcName[] = "run iap";    
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_RUN_IAP;
    dataSend[4] = iapMode;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName); 

Exit:

    return result;
}

int finishIap(uint32_t checksum)
{
    const char funcName[] = "finish iap";   
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_FINISHED_IAP;
    *(uint32_t *)(dataSend + 4) = checksum;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);

Exit:

    return result;
}

int softwareReset()
{
    const char funcName[] = "software reset";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_SOFTWARE_RESET;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);

Exit:

    return result;
}

int getFwVersion(uint32_t *pfwVer, uint8_t *pbootType)
{
    const char funcName[] = "get fw ver";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_GET_FW_VERSION;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    *pfwVer = *(uint32_t*)(statRecv + 4); 
    
    *pbootType = statRecv[2];
    
    printf("[IAP] %s - fw version: 0x%08X, boot type: 0x%02X\n", funcName, *pfwVer, statRecv[2]);    

Exit:

    return result;
}

int abortProcess()
{
    const char funcName[] = "abort process";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_ABORT_PROCESS;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);  

Exit:

    return result;
}

int writeRom(uint32_t length)
{
    const char funcName[] = "write rom command";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_WRITE_ROM;
    *(uint32_t *)(dataSend + 4) = length;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);    

Exit:

    return result;
}

int writeRomFinish(uint32_t checksum)
{
    const char funcName[] = "write rom finish";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_WRITE_ROM_FINISH;
    *(uint32_t *)(dataSend + 4) = checksum;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X), checksum from fw: 0x%08X\n", funcName, statRecv[1], *(uint32_t*)(statRecv+4));
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);    

Exit:

    return result;
}

int setRomAddress(uint32_t address)
{
    const char funcName[] = "set rom address";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_SET_ROM_ADDRESS;
    *(uint32_t *)(dataSend + 4) = address;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);   

Exit:

    return result;
}

int setOptionAddress(uint32_t address)
{
    const char funcName[] = "set option address";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_SET_OPTION_ADDRESS;
    *(uint32_t *)(dataSend + 4) = address;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);   

Exit:

    return result;
}

int readRom(uint32_t length)
{
    const char funcName[] = "read rom command";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_READ_ROM;
    *(uint32_t *)(dataSend + 4) = length;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName); 

Exit:

    return result;
}

int readRomFinish(uint32_t checksum)
{
    const char funcName[] = "read rom finish";
    unsigned char dataSend[EP1_LENGTH];
    unsigned char statRecv[EP2_LENGTH];    
    int result = 0;  
    
    
        
    memset(dataSend, 0x00, sizeof(dataSend));
    memset(statRecv, 0x00, sizeof(statRecv));
    
 
    dataSend[0] = CMD_READ_ROM_FINISH;
    *(uint32_t *)(dataSend + 4) = checksum;
    
    
    result = iapSendCommand(dataSend, sizeof(dataSend));
    if(result)
    {
        printf("[IAP] %s - send command fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    result = iapRecvStatus(statRecv, sizeof(statRecv));
    if(result)
    {
        printf("[IAP] %s - receive status fail\n", funcName);
        result = -1;
        goto Exit; 
    }
    
    if(statRecv[0] != dataSend[0])
    {
        printf("[IAP] %s - return cmmmand code is not match (0x%02X)\n", funcName, statRecv[0]);
        result = -1;
        goto Exit; 
    
    }
    
    if(statRecv[1] != ERR_STA_SUCCESS)
    {
        printf("[IAP] %s - fail, error code (0x%02X)\n", funcName, statRecv[1]);
        result = -1;
        goto Exit; 
    
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - success\n", funcName);

Exit:

    return result;
}

int iapSendCommand(unsigned char *pBuff, int length)
{
    const char funcName[] = "send command";
    int errCode = 0;
    int result = 0;
    
    
    
    if(g_devInfo.handle_ep1 == NULL)
    {
        printf("[IAP] %s - handle is null\n", funcName);
        return -1;
    }

    if(pBuff == NULL)
    {
        printf("[IAP] %s - buffer is null\n", funcName);
        return -1;
    }
    
    errCode = hid_write(g_devInfo.handle_ep1, pBuff, length);
 
    if(errCode != length)
    {
        printf("[IAP] %s - fail, error code: %d, %ls\n", funcName, errCode, hid_error(g_devInfo.handle_ep1));
        result = -1;
    }
        
    return result;
}

int iapRecvStatus(unsigned char *pBuff, int length)
{
    const char funcName[] = "receive status";
    int errCode = 0;    
    int result = 0;



    if(g_devInfo.handle_ep2 == NULL)
    {
        printf("[IAP] %s - handle is null\n", funcName);
        return -1;
    }

    if(pBuff == NULL)
    {
        printf("[IAP] %s - buffer is null\n", funcName);
        return -1;
    }      
    
    errCode = hid_read(g_devInfo.handle_ep2, pBuff, length);
   
    if(errCode != length)
    {
        printf("[IAP] %s - fail, error code: %d, %ls\n", funcName, errCode, hid_error(g_devInfo.handle_ep2));
        result = -1;
    }
   
    return result;
}

int iapSendData(unsigned char *pBuff, int length)
{
    const char funcName[] = "send data";
    unsigned char tmpBuff[length + 1];
    int newLength = length + 1;
    int errCode = 0;    
    int result = 0;
    
    
    
    memset(tmpBuff, 0x00, newLength);
    memcpy(tmpBuff + 1, pBuff, length);   
    
    if(g_devInfo.handle_ep3 == NULL)
    {
        printf("[IAP] %s - handle is null\n", funcName);
        return -1;
    }

    if(pBuff == NULL)
    {
        printf("[IAP] %s - buffer is null\n", funcName);
        return -1;
    }      
    
    errCode = hid_write(g_devInfo.handle_ep3, tmpBuff, newLength);    
    
    if(errCode != newLength)
    {
        printf("[IAP] %s - fail, error code: %d, %ls\n", funcName, errCode, hid_error(g_devInfo.handle_ep3));
        result = -1;
    }
   
    return result;
}

int iapRecvData(unsigned char *pBuff, int length)
{
    const char funcName[] = "receive data";
    unsigned char tmpBuff[length + 1];
    int newLength = length + 1;
    int errCode = 0;    
    int result = 0;
    
    
        
    memset(tmpBuff, 0x00, newLength);    
    
    if(g_devInfo.handle_ep4 == NULL)
    {
        printf("[IAP] %s - handle is null\n", funcName);
        return -1;
    }

    if(pBuff == NULL)
    {
        printf("[IAP] %s - buffer is null\n", funcName);
        return -1;
    }      
    
    errCode = hid_read(g_devInfo.handle_ep4, pBuff, length);
   
    if(errCode != length)
    {
        printf("[IAP] %s - fail, error code: %d, %ls\n", funcName, errCode, hid_error(g_devInfo.handle_ep4));
        result = -1;
    }
    
    //memcpy(pBuff, tmpBuff + 1, length);
   
    return result;
}


int connectDevice(unsigned short vid, unsigned short pid)
{  
    const char funcName[] = "connect device";
    int count = 0;
    
    
    //printf("[Connect Device] Start\n");
    
    count = getDeviceCount(vid, pid);
    
    if(DEBUG_LOG)
        printf("[IAP] %s - device count: %d\n", funcName, count);
    
    if(count > 0)
    {  
        if(g_deviceCount)
        {       
        
            return DEVSTA_EXISTED;
        }        
        
        g_deviceCount = count;
        
        return DEVSTA_INSERT; 
    }
    
    if(g_deviceCount)
    {    	
        releaseResource();
            
        g_deviceCount = 0;    
    
        return DEVSTA_REMOVE;
    }

    printf("[IAP] %s - exit\n", funcName);
    
    return DEVSTA_NON;   
}

int getDeviceCount(unsigned short vid, unsigned short pid)
{ 
    const char funcName[] = "get device count";
    int count = 0;
    int enumCnt = 0;
    int res = 0;
    hid_device_info *info = NULL;
    hid_device_info *info_next = NULL;    
    
    
    
    res = hid_init();
    
    if(res == -1)  
    {
    	printf("[IAP] %s - hid initial fail\n", funcName);
        count = 0;
        goto Exit;   
    }
    
    info = hid_enumerate(vid, pid);
    
    if(!info)
    {
        printf("[IAP] %s - unable to get device information (VID: 0x%04X, PID: 0x%04X)\n", funcName, vid, pid);
        count = 0;
        goto Exit;            
    }
    
    if(DEBUG_LOG)
        printf("[IAP] %s - device vid: 0x%04X, device pid: 0x%04X\n", funcName, vid, pid);
    
    info_next = info;    
    
    while(1)
    {    	
    	//printf("[IAP] =====================[ %d ]=====================\n", enumCnt);    	
    	//printf("[IAP] path: %s, serial number: %ls\n", info_next->path, info_next->serial_number);    	
    	//printf("[IAP] interface number: %d\n", info_next->interface_number);
    	//printf("[IAP] Get Device Count - manufacturer: %ls\n", info_next->manufacturer_string);
    	//printf("[IAP] Get Device Count - product: %ls\n", info_next->product_string);
    	//printf("\n");
    	
    	if(info_next->interface_number == 0)
    	{
    	    g_devInfo.handle_ep1 = hid_open_path(info_next->path);
    	    if(!g_devInfo.handle_ep1)
    	    {
    		printf("[IAP] %s - unable to open device (interface number: %d)\n", funcName, info_next->interface_number);
        	count = 0;
        	goto Exit;
    	    }
    	    count++;
    	}
    	else if(info_next->interface_number == 1)
    	{
    	    g_devInfo.handle_ep2 = hid_open_path(info_next->path);
    	    if(!g_devInfo.handle_ep2)
    	    {
    		printf("[IAP] %s - unable to open device (interface number: %d)\n", funcName, info_next->interface_number);
        	count = 0;
        	goto Exit;
    	    }
    	    count++;   	
    	}
    	else if(info_next->interface_number == 2)
    	{
    	    g_devInfo.handle_ep3 = hid_open_path(info_next->path); 
    	    if(!g_devInfo.handle_ep3)
    	    {
    		printf("[IAP] %s - unable to open device (interface number: %d)\n", funcName, info_next->interface_number);
        	count = 0;
        	goto Exit;
    	    }
    	    count++;  	
    	}
    	else if(info_next->interface_number == 3)
    	{
    	    g_devInfo.handle_ep4 = hid_open_path(info_next->path); 
    	    if(!g_devInfo.handle_ep4)
    	    {
    		printf("[IAP] %s unable to open device (interface number: %d)\n", funcName, info_next->interface_number);
        	count = 0;
        	goto Exit;
    	    }
    	    count++;    	
    	}
 
    	if(info_next->next == NULL)
    	    break;
    	    
    	info_next = info_next->next;   
    	enumCnt++;
    }

Exit:
 
    hid_free_enumeration(info); 
 
    return count;
}

void releaseResource()
{
    const char funcName[] = "release resource";
    
    
        
    if(DEBUG_LOG)
        printf("[IAP] %s - start\n", funcName);    

    if(g_devInfo.handle_ep1)
    {
        hid_close(g_devInfo.handle_ep1);
        g_devInfo.handle_ep1 = NULL;
    }
    
    if(g_devInfo.handle_ep2)
    {
        hid_close(g_devInfo.handle_ep2);
        g_devInfo.handle_ep2 = NULL;
    }
    
    if(g_devInfo.handle_ep3)
    {
        hid_close(g_devInfo.handle_ep3);
        g_devInfo.handle_ep3 = NULL;
    }
    
    if(g_devInfo.handle_ep4)
    {
        hid_close(g_devInfo.handle_ep4);
        g_devInfo.handle_ep4 = NULL;
    }

    if(DEBUG_LOG)
        printf("[IAP] %s - exit\n", funcName);
}

void initialIapControl()
{
    releaseResource();
}

void releaseIapcontrol()
{
    releaseResource();
    
    hid_exit();
}
