#include "filecontrol.h"


int getBinary(string filePath, unsigned char *pBinary, uint32_t *pLength)
{ 
    const char funcName[] = "get binary";
    fstream binFile;
    uint32_t size = 0;
    int result = 0; 
       
    
    
    //printf("[IAP] %s - file Path (%s)\n", funcName, filePath.c_str());	
     
    binFile.open(filePath.c_str(), ios::in | ios::binary);
    
    if(!binFile)
    {
        printf("[IAP] %s - open file fail (%s)\n", funcName, filePath.c_str());	
	return -1;    
    }    
    
    binFile.seekg(0, ios::end);
    
    size = binFile.tellg();
    
    //printf("[IAP] %s - size: %d\n", funcName, size);
    
    if(pBinary == NULL)
    {
        *pLength = size;        
        goto Exit;
    }
    
    if(size != *pLength)
    {
        printf("[IAP] %s - file size is not match, request: %d, recv: %d\n", funcName, size, *pLength);
    } 
    
    binFile.seekg(0, ios::beg);  
    
    binFile.read((char*)pBinary, size);
 
 Exit:   
    
    binFile.close();

    return result;
}
