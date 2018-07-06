 /*++

Copyright (c) 1989-1999  Microsoft Corporation

Module Name:

    namelookupdef.h

Abstract:

    Header file containing the name lookup definitions needed by both user
    and kernel mode.  No kernel-specific data types are used here.


Environment:

    User and kernel.

--*/
#ifndef __USBSECURE_H__
#define __USBSECURE_H__


FAST_MUTEX g_UsbMutex;
LIST_ENTRY g_UsbSecureListHead;

KEVENT * g_UsbDeviceSignal;

typedef ULONG USBControlSTATUS  ; //1: �ܱ��� ������2:�ܱ�����ͣ��3:û������

typedef struct _TagFileTypeForUsb
{
	LIST_ENTRY list;
	WCHAR szFileType[50];
}FILETYPEFORUSB,*PFILETYPEFORUSB;

typedef struct _TagUsbSecure
{
	LIST_ENTRY List;
	BOOLEAN bEncryptAll;//TRUE: force to encrypt all FILES�� FALSE: encrypte special files		
	CHAR*  pszDeviceID;
	ULONG  nLen;
	ULONG  VolumeID;
	FAST_MUTEX  FileTypesLock;
	LIST_ENTRY FileTypeListHead;
	USBControlSTATUS  nControlStatu;	//1: �ܱ��� ������2:�ܱ�����ͣ��3:û������
	
	PDEVICE_OBJECT  pUsbDevice;//when connected, this field is not NULL;
	WCHAR  DriverLetter[10];//can change
	WCHAR  DriverDescription[40];//can change
	PDEVICE_OBJECT pUsbVolumeDevice;
}USBSECURE,*PUSBSECURE;

VOID 
PfpInitUsbSecureS(
				  IN PVOID pBuffer,
				  IN ULONG llen);

VOID 
PfpWriteUsbSecurIntoBuffer(
						   IN OUT PVOID pBuffer,
						   IN OUT ULONG * pLLen);//pLLen �ڷ��ص�ʱ�� ��ŵ���Buffer ʣ�µĴ�С

ULONG 
PfpGetUsbSecurLenForSave();

typedef struct _tagUsbQueryIDs
{
	CHAR DeviceID[200];
	ULONG VolumeID;
}USBQUERYIDS,*PUSBQUERYIDS;
typedef struct _tagUsbControlStatus
{
	ULONG nEncryptAll;
	ULONG nControlStatus;
}USBCONTROLSTATUS,*PUSBCONTROLSTATUS;


typedef struct _tagUsbControlStatusSet
{
	USBQUERYIDS		 usbIds;
	USBCONTROLSTATUS ControlStatus;
}USBCONTROLSTATUSSET,*PUSBCONTROLSTATUSSET;

typedef struct _tagUsbFileTypesSet
{
	USBQUERYIDS		usbIds;
	ULONG			nFiletypeLen;
	WCHAR			FileTypes[1];
}USBFILETYPESSET,*PUSBFILETYPESSET;


//����ĺ����Ǹ� USRMode�� ������ʵ�ʱ�� ʹ�õ�
VOID	
PfpDeleteUsbSecure(
				   IN ULONG VolumeID,
				   IN CHAR* pszDeviceID,
				   IN ULONG idLen);

BOOLEAN 
PfpQueryUsbControlStatus(
						 IN ULONG VolumeID,
						 IN CHAR* pszDeviceID,
						 IN ULONG idLen,
						 BOOLEAN* pbEncryptALL,
						 ULONG * pControlStatus);

BOOLEAN 
PfpQueryUsbFileTypesLen(
						IN ULONG VolumeID,
						IN CHAR* pszDeviceID,
						IN ULONG idLen,
						ULONG *nLen);//nLen ���ص�ʱ�� �Ǽ�¼ Ҫʹ�ö���

BOOLEAN 
PfpQueryUsbFileTypes(
					 IN ULONG VolumeID,
					 IN CHAR* pszDeviceID,
					 IN ULONG idLen,
					 IN WCHAR *pszBuffer,
					 ULONG *nLen);

BOOLEAN 
PfpSetUsbFileEncryptType(	
						 IN ULONG VolumeID,
						 IN CHAR* pszDeviceID,
						 IN ULONG idLen,
						 BOOLEAN bEncryptForce);

BOOLEAN 
PfpQueryUsbFileEncryptType(	
						 IN ULONG VolumeID,
						 IN CHAR* pszDeviceID,
						 IN ULONG idLen,
						 ULONG*   bEncryptForce);

BOOLEAN 
PfpSetUsbControlSTATUS(
					   IN ULONG VolumeID,
					   IN CHAR* pszDeviceID,
					   IN ULONG idLen,
					   USBControlSTATUS controlStatus);//3 û�����õ����ֵ Ӧ�ò��ܳ�������������У���Ϊ3��ʾһ��״̬ û�����ù���״̬ 
																													//���Ե�
BOOLEAN 
PfpSetUsbEncryptionFileTypes(
							 IN ULONG VolumeID,
							 IN CHAR* pszDeviceID,
							 IN ULONG idLen,
							 WCHAR *szFileTypes,
							 ULONG nLen);

ULONG   
PfpQueryUsbConfigNum();

BOOLEAN 
PfpQueryAllUsbIDs(
				  IN OUT PVOID pBuf,
				  IN ULONG*  pLeft);

//����ĺ������ڲ�ʹ�õ�
PUSBSECURE 
PfpGetUsbSecure(
				IN ULONG VolumeID,
				IN CHAR* pszDeviceID,
				IN ULONG idLen);

BOOLEAN    
PfpIsUsbConnectd(
				 PUSBSECURE  pUsbSecure);

BOOLEAN 
PfpCopyUsbFileTypesIntoBuffer(
							  PUSBSECURE  pUsbSecure,
							  PVOID pVOID,
							  ULONG * nSize);

ULONG 
PfpGetFileTypeLenForOneSecure(
							  PUSBSECURE  pUsbSecure);

VOID 
PfpDeleteUsbFileTypeForOneSecure(
								 PUSBSECURE  pUsbSecure);

VOID 
PfpAddUsbFileTypesIntoOneSecure(
								PUSBSECURE  pUsbSecure,
								PWCHAR pszFileTypes,
								ULONG nLen);

ULONG 
PfpPutOneSecureIntoBuffer(
						  PVOID pBuf,
						  ULONG nLen,
						  PUSBSECURE  pUsbSecure);

PUSBSECURE 
PfpCreateOneSecureFromBuffer(
							 PVOID pBuf,
							 ULONG nBufLen);

ULONG 
PfpCalcOneUsbSecureSpaceForSaving(PUSBSECURE pSecureItem);



VOID 
PfpInitUsbDeviceWithSecure(
						   PDEVICE_OBJECT pOurDevice);


typedef struct _USB_DEVICE_INITIALIZE_WORKITEM
{

	WORK_QUEUE_ITEM WorkItem;

	//
	//  The DeviceObject whose name is being retrieved.  We need this in addition
	//  to the other fields so we can make sure it doesn't disappear while
	//  we're retrieving the name.
	//

	PDEVICE_OBJECT DeviceObject;

	//
	//  The name library device extension header of the device object to get
	//  the DOS name of.
	//

	PNL_DEVICE_EXTENSION_HEADER NLExtHeader;

} _USB_DEVICE_INITIALIZE_WORKITEM, *PUSB_DEVICE_INITIALIZE_WORKITEM;


VOID
PfpUsbInitSecureWorker (
						PVOID  Context/*__in PUSB_DEVICE_INITIALIZE_WORKITEM Context*/
						);
						   




//////////////////////////////////////////////////////////////////////////
//�����������
BOOLEAN  IsUsbDeviceNeedEncryption(PDEVICE_OBJECT pUsbDevice);
BOOLEAN  IsFileNeedEncryptionForUsb(PDEVICE_OBJECT pDevice,WCHAR* pszFileType);

VOID PfpDeleteUsbSecureMemory(IN PUSBSECURE pUsbSecure);

#endif

