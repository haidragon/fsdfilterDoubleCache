
#include <ntifs.h>
#include <stdlib.h>
#include <suppress.h>
#include "filespy.h"
#include "fspyKern.h"
#include "log.h"

VOID
PfpThreadCreationNotification (
							   IN HANDLE  ProcessId,
							   IN HANDLE  ThreadId,
							   IN BOOLEAN  Create
								  )
{
	return ;
}
VOID
PfpImageLoadNotification (
						  IN PUNICODE_STRING  FullImageName,
						  IN HANDLE  ProcessId, // where image is mapped
						  IN PIMAGE_INFO  ImageInfo
						  )
{
	return ;
}
VOID
pfpCreateProcessNotify(
					   IN HANDLE  ParentId,
					   IN HANDLE  ProcessId,
					   IN BOOLEAN  Create
					   )
{
	
	PPROCESSINFO	pProcessInfo = NULL;
	
	FsRtlEnterFileSystem();
	if(!Create)
	{
		ExAcquireResourceSharedLite(&g_ProcessInfoResource,TRUE);
		pProcessInfo = PfpGetProcessInfoUsingProcessId(ProcessId);
		ExReleaseResourceLite(&g_ProcessInfoResource);
		if(pProcessInfo )
		{
			ExAcquireFastMutex(&pProcessInfo->HandleMutex);
			PfpDeleteHandle(pProcessInfo,ProcessId);
			ExReleaseFastMutex(&pProcessInfo->HandleMutex);

			AddIntoLogQeuue(NULL,0,NULL,0,NULL,0,2,0,(ULONG)(LONGLONG)ProcessId);
			InterlockedDecrement(&pProcessInfo->nRef);
			//pProcessInfo= NULL;
		}
		
		if(pProcessInfo== NULL)
		{
			PfpDelExcludProcess(ProcessId); 
		}
		pProcessInfo = NULL;

	}else
	{

		//1:�鿴parentid �ǲ��ǿ��ŵģ�����ǿ��ŵģ���ô�鿴�����ӽ����ǲ���Ҫ��Ϊ���ŵġ�
		//���Ҫ���ǿ��ŵģ���ô���������һ���µ�processinfo ������Ӧ����Ϣ1
		UNICODE_STRING	ExeFullPathParent;
		WCHAR*			pParentFullPath = NULL;
		UCHAR			szHashValueParent[PROCESSHASHVALULENGTH];
		HANDLE			ProcessHandle = INVALID_HANDLE_VALUE;
		
		if(KeGetCurrentIrql()> DISPATCH_LEVEL)
		{
			goto PARENTDONE;
		}
		RtlInitUnicodeString(&ExeFullPathParent,NULL);


		ExAcquireResourceSharedLite(&g_ProcessInfoResource,TRUE);

		pProcessInfo = PfpGetProcessInfoUsingProcessId(ParentId);
		
		ExReleaseResourceLite(&g_ProcessInfoResource);

		//��Ϊ����������������ˣ�����û���߹�һ��create�������
		//��ΪPROCESSID ��ͨ��Create ����¼��ProcessInfo ������ݽṹ����ġ�
		//����Ҫ�ٴ�ͨ��Exe��hashValue���õ����Exe ��ProcessInfo ���ݽṹ������֤�ж�����������ǲ��ǿ��ŵġ�
		if(pProcessInfo == NULL)
		{
			PfpGetProcessHandleFromID(ParentId,&ProcessHandle);

			if(ProcessHandle== INVALID_HANDLE_VALUE)
				goto PARENTDONE;

			if(!NT_SUCCESS(GetProcessImageName(ProcessHandle,&ExeFullPathParent)))
			{
				ZwClose(ProcessHandle);			
				goto PARENTDONE;
			}
			ZwClose(ProcessHandle);
			
			if(ExeFullPathParent.Buffer== NULL)
				goto PARENTDONE;

			pParentFullPath  = ExAllocatePoolWithTag(PagedPool,ExeFullPathParent.Length+sizeof(WCHAR),'7009');
			if(!pParentFullPath  )
				goto PARENTDONE;
			

			memcpy(pParentFullPath,ExeFullPathParent.Buffer,ExeFullPathParent.Length);
			pParentFullPath  [ExeFullPathParent.Length/sizeof(WCHAR)]=L'\0';

			ExAcquireResourceSharedLite(&g_ProcessInfoResource,TRUE);
			pProcessInfo = PfpGetProcessInfoUsingFullPath(pParentFullPath);
			ExReleaseResourceLite(&g_ProcessInfoResource);
			
			if(pProcessInfo== NULL)
				goto PARENTDONE ;

			if(!PfpGetHashValueForEXE(pParentFullPath,ExeFullPathParent.Length,szHashValueParent,PROCESSHASHVALULENGTH))
			{					
				goto PARENTDONE;
			}
			
			if(!memcmp(pProcessInfo->ProcessHashValue,szHashValueParent,PROCESSHASHVALULENGTH)==0)
			{
				goto PARENTDONE;
			}
		}

		if(pProcessInfo && pProcessInfo->bAllowInherent )
		{
			UNICODE_STRING ExeFullPath;
			PPROCESSINFO	pProcessInfoChild = NULL;
			
			PWCHAR		   pExePath		= NULL;
			ULONG		   nLenOfFileTypes;
			WCHAR*		   pTypes = NULL;
			UCHAR		   szHashValue [PROCESSHASHVALULENGTH];
			HANDLE		   ProcessHandle = INVALID_HANDLE_VALUE;
			__try
			{
				RtlInitUnicodeString(&ExeFullPath,NULL);

				PfpGetProcessHandleFromID(ProcessId,&ProcessHandle);
				if(ProcessHandle== INVALID_HANDLE_VALUE)
					goto PARENTDONE;

				if(!NT_SUCCESS(GetProcessImageName(ProcessHandle,&ExeFullPath)))
				{
					ZwClose(ProcessHandle)	;		
					ProcessHandle = INVALID_HANDLE_VALUE;
					goto DONE;
				}
				ZwClose(ProcessHandle)	;	
				ProcessHandle = INVALID_HANDLE_VALUE;
				pExePath  = ExAllocatePoolWithTag(PagedPool,ExeFullPath.Length+sizeof(WCHAR),'8009');
				if(!pExePath  )
					goto DONE;


				memcpy(pExePath,ExeFullPath.Buffer,ExeFullPath.Length);
				pExePath  [ExeFullPath.Length/sizeof(WCHAR)]='\0';

				if(!PfpGetHashValueForEXE(pExePath,ExeFullPath.Length,szHashValue,PROCESSHASHVALULENGTH))
					goto DONE;

				nLenOfFileTypes = PfpCalcFileTypesLen(pProcessInfo);
				if(nLenOfFileTypes == 0)
					goto DONE;
				nLenOfFileTypes =  (nLenOfFileTypes+7)&~7;

				pTypes = ExAllocatePoolWithTag(PagedPool,nLenOfFileTypes,'9009')	;
				if(pTypes==  NULL )
					goto DONE;
				
				PfpCopyFileTypesIntoBuffer(pTypes,pProcessInfo);
				pProcessInfoChild = PfpCreateAndInitProcessInfo(ExeFullPath,
																szHashValue,
																PROCESSHASHVALULENGTH,
																ProcessId,
																TRUE,
																pTypes,
																pProcessInfo->bNeedBackUp,
																TRUE,
																pProcessInfo->bForceEncryption,
																pProcessInfo->bAlone,
																pProcessInfo->bBowser,
																pProcessInfo->bAllCreateExeFile,
																pProcessInfo->nEncryptTypes);

				if(pProcessInfo == NULL)
					goto DONE;

				ExAcquireResourceExclusiveLite(&g_ProcessInfoResource,TRUE);
				PfpAddProcessIntoGlobal(pProcessInfoChild );
				ExReleaseResourceLite(&g_ProcessInfoResource);

DONE:
				;
			}
			__finally
			{
				if(ExeFullPath.Buffer )
				{
					RtlFreeUnicodeString(&ExeFullPath);
				}
				if(pExePath)
				{
					ExFreePool(pExePath);
				}
				if(pTypes)
				{
					ExFreePool(pTypes);
				}
				if(ProcessHandle!= INVALID_HANDLE_VALUE)
					ZwClose(ProcessHandle);
			}
PARENTDONE:
			if(pParentFullPath)
			{
				ExFreePool(pParentFullPath);
			}
			if(ExeFullPathParent.Buffer)
			{
				RtlFreeUnicodeString(&ExeFullPathParent);
			}
		
			//Get hash value for this new process 
			//Get filename for this process
			//copy filetypes from praent's process info into new created processinfo.
			//copy bAllowInherent into new created processinfo.
			//add processinfo into global processinfos.

		}
		if(pProcessInfo)
		{
			InterlockedDecrement(&pProcessInfo->nRef);
		}
	}
	FsRtlExitFileSystem();
}