#include "Win32DiskDriver.h"
#include <stdio.h>

BOOL sd_read_block(unsigned char *p, unsigned long SectorStart, unsigned long SectorCount)
{
	unsigned long BytesPerSector = 512;
	unsigned long nBytes;
	//char Drive[] = "\\\\.\\USBSTOR\\DISK&VEN_MASS&PROD_STORAGE_DEVICE&REV__\125D20140310&0";
	BOOL result = FALSE;
	HANDLE hDeviceHandle = CreateFileW(TEXT("\\\\.\\PhysicalDrive2"), 
									  GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
									  NULL, OPEN_EXISTING, 0, NULL);
	if(hDeviceHandle != INVALID_HANDLE_VALUE)
	{
		long long pointer;
		long phigh;
		pointer = SectorStart * BytesPerSector; // use positive offset
		phigh = (long)(pointer >> 32);
		SetFilePointer(hDeviceHandle, pointer, NULL, FILE_BEGIN);
		if(ReadFile(hDeviceHandle, p, SectorCount * BytesPerSector, &nBytes, NULL))
			result = TRUE;
	}
	CloseHandle(hDeviceHandle);
	return result;
}

BOOL sd_write_block(unsigned char *p, unsigned long SectorStart, unsigned long SectorCount)
{
	unsigned long BytesPerSector = 512;
	unsigned long nBytes;
	//char Drive[] = "\\\\.\\USBSTOR\\DISK&VEN_MASS&PROD_STORAGE_DEVICE&REV__\125D20140310&0";
	BOOL result = FALSE;
	HANDLE hDeviceHandle = CreateFile(TEXT("\\\\.\\G:"),
									  GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
									  NULL, OPEN_EXISTING, 0, NULL);
	DWORD dwByteReturned;
	BOOL bLOCK, bUNLOCK, bb;
	/*
	bLOCK = DeviceIoControl(
		hDeviceHandle,
		FSCTL_LOCK_VOLUME,
		NULL,
		0,
		NULL,
		0,
		&dwByteReturned,
		NULL
	);
	*/
	bUNLOCK = DeviceIoControl(
		hDeviceHandle,
		FSCTL_DISMOUNT_VOLUME,
		NULL,
		0,
		NULL,
		0,
		&dwByteReturned,
		NULL
	);
	
	if(hDeviceHandle != INVALID_HANDLE_VALUE && bUNLOCK /* && bLOCK */)
	{
		long long pointer;
		long phigh;
		puts("here");
		pointer = SectorStart * BytesPerSector; // use positive offset
		phigh = (long)(pointer >> 32);
		SetFilePointer(hDeviceHandle, pointer, NULL, FILE_BEGIN);
		result = WriteFile(hDeviceHandle, p, SectorCount * BytesPerSector, &nBytes, NULL);
	}
	CloseHandle(hDeviceHandle);
	return result;
}