


#include <suppress.h>
#include <stdio.h>
#include <stdlib.h>

#include <ntifs.h>
//#include "filespy.h"
#include "fspyKern.h"
#include "log.h"
#include "UsbSecure.h"
#include "PfpCreate.h"

LUID FatSecurityPrivilege = { SE_SECURITY_PRIVILEGE, 0 };

NTSTATUS 
PfpCommonCreate(__in PIRP_CONTEXT	IrpContextParam	,
                __in PDEVICE_OBJECT DeviceObject,
                __in PIRP			Irp)
{

    //KIRQL						oldIrql;
    NTSTATUS					ntstatus ;
    IO_STATUS_BLOCK 			iostatus;
    PFILESPY_DEVICE_EXTENSION	pExt				= NULL;
    PDEVICE_OBJECT				pAttachedDevice		= NULL;
    PIO_STACK_LOCATION			pstack				= IoGetCurrentIrpStackLocation(Irp);

    PERESOURCE					pDeviceResouce		= NULL;
    PERESOURCE					pVirtualFileResouce		= NULL;

    WCHAR					*	FullPathName		= NULL;	 
    ULONG						lFullPathLenInBytes = 0;
    UNICODE_STRING				FileFullPath;	
    PPROCESSINFO				ProcessInfo			= NULL;
    PDISKFILEOBJECT				pDiskFileObject		= NULL;		
    BOOLEAN						bFirstOpen			= FALSE;


    FILESTATE					AcsType;	
    PIRP_CONTEXT				IrpContext			= IrpContextParam;
    TOP_LEVEL_CONTEXT			TopLevelContext;
    PTOP_LEVEL_CONTEXT			ThreadTopLevelContext = NULL;

    ULONG						exLenght			= 0;
    WCHAR						szExt[50]			= {0};


    PFILE_OBJECT				pFileObject	= NULL;
    BOOLEAN						bHasFileExt = TRUE;
    BOOLEAN						bFileExtInProcessNotSelected = FALSE;

    ULONG						Options           ;
    WCHAR						DeviceLetter [3]={0};
    PROTECTTYPE					ProtectTypeForFolder;
    BOOLEAN						bEncryptForFolder = FALSE;
    BOOLEAN						bBackupForFolder = FALSE;
    BOOLEAN						bFolderUnderProtect = FALSE;
    BOOLEAN						bFolderLocked = FALSE;
    ULONG						bEncryptFileTypeForFolder = ENCRYPT_NONE;
    LONGLONG				    FilesizeForExistFile = 0;
    BOOLEAN						bFileOurCreated = FALSE;
    BOOLEAN						bOpenFileStream = FALSE;
    PDISKDIROBEJECT				pParentRootDir	= NULL;
    PDISKDIROBEJECT				pParentDir		= NULL;
    BOOLEAN						bPareseCompleted= FALSE;
    PWCHAR						pRemainer       = NULL;
    PVIRTUALDISKFILE			pVirtualDiskFile = NULL;
    BOOLEAN						bIrpPost		 = FALSE;
    pFileObject = pstack->FileObject;
    /*
    This fcb may be the first created ,or just get from the memroy struct
    1: file has been open in memory ,in this case ,just get from the memory structure;
    2: file not exist or file not opened yet.
    */


    pExt  = DeviceObject->DeviceExtension;
    FsRtlEnterFileSystem();

    ASSERT(IS_FILESPY_DEVICE_OBJECT( DeviceObject ) );

    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    if(pExt->bShadow)
    {
        if(pFileObject->FileName.Buffer== NULL||pFileObject->FileName.Length==0)
        {
            Irp->IoStatus.Status = STATUS_ACCESS_DENIED;

            PfpCompleteRequest( (IrpContext?&IrpContext:NULL), &Irp, STATUS_ACCESS_DENIED);			 
            FsRtlExitFileSystem();
            return STATUS_ACCESS_DENIED;
        }

        pAttachedDevice  = ((PFILESPY_DEVICE_EXTENSION)pExt->pRealDevice->DeviceExtension)->NLExtHeader.AttachedToDeviceObject;
        goto PASSTHROUGH;
    }
    //////////////////////////////////////////////////////////////////////////	//////////////////////////////////////////////////////////////////////////


    pAttachedDevice  = pExt->NLExtHeader.AttachedToDeviceObject;

    if(g_ourProcessHandle== PsGetCurrentProcessId())
        goto PASSTHROUGH;

    if(!PfpGetDeviceLetter(DeviceObject,DeviceLetter))
        goto PASSTHROUGH;

    //������ں˴������ļ���irp �ǾͲ�������
    /*if(Irp->RequestorMode != UserMode)
    { 
    ntstatus = SpyPassThrough( DeviceObject, Irp );
    FsRtlExitFileSystem();
    return ntstatus ;	
    }*/
    //Ƕ�׵��� ������
    if(IoGetTopLevelIrp()!= NULL)
    {
        goto PASSTHROUGH;
    }
    if(pstack->Flags & SL_OPEN_PAGING_FILE)
    {
        goto PASSTHROUGH;
    }
    if(g_ExcludeID!= INVALID_HANDLE_VALUE && g_ExcludeID== PsGetCurrentProcessId())
    {
        goto PASSTHROUGH;
    }



    if((pParentRootDir = PfpGetVirtualRootDirFromSpyDevice(DeviceObject)) ==  NULL)
    {		
        goto PASSTHROUGH;
    }



    //////////////////////////////////////////////////////////////////////////
    //�ļ�û�к�׺ ������
    bHasFileExt	  =	PfpGetFileExtFromFileObject(pFileObject,szExt,&exLenght);
    //��������ļ���׺,�����ļ������������ִ���ļ�
    // 
    // 	if(!PfpIsFileNameValid(pFileObject->FileName.Buffer,pFileObject->FileName.Length) && 
    // 		(pFileObject->RelatedFileObject== NULL || !PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject)))
    // 	{
    // 		iostatus.Status			= STATUS_OBJECT_NAME_INVALID;
    // 		iostatus.Information	= 0;
    // 		goto EXIT;
    // 
    // 	}
    // 	if(bHasFileExt && exLenght== 3*sizeof(WCHAR))
    // 	{
    // 		//if(IsFileTypeBelongExeType(szExt)) goto PASSTHROUGH;
    // 		
    // 	}

    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    //���� ���Ǵ�������ʵĿ¼���� ������������ļ����ļ��ж���ֻ���ġ�
    //1: �Ƿ�򿪵���һ�����Ǵ�������ʵ��Ŀ¼
    //2���Ƿ��Ǵ򿪵� ��ʵĿ¼������ļ���
    //3:�Ƿ�򿪵��� ��ʵĿ¼���������Ŀ¼��
    //4���Ƿ��Ǵ򿪵�����Ŀ¼�����Ŀ¼��
    //5:�Ƿ��Ǵ�����Ŀ¼������ļ���
    //�õ� Ҫ���ʵ��ļ���ȫ·��
    if(!NT_SUCCESS(ntstatus = PfpGetFullPathPreCreate(Irp,&FullPathName,&lFullPathLenInBytes,DeviceObject)))
    {
        if(!PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject))
        {
            goto PASSTHROUGH;
        }
        else
        {
            iostatus.Status			= ntstatus;
            iostatus.Information	= 0;
            goto EXIT;
        }
    }
    if(wcsstr(FullPathName,L"WINDOWS\\System32\\Msimtf.dll")!=NULL ||wcsstr(FullPathName,L"WINDOWS\\System32\\msimtf.dll")!=NULL )
    {
        KdPrint (("\r\n"));
    }
    bOpenFileStream = PfpIsStreamPath(FullPathName,lFullPathLenInBytes);

    FileFullPath.Buffer = FullPathName;
    FileFullPath.Length = (USHORT)lFullPathLenInBytes;
    FileFullPath.MaximumLength = (FileFullPath.Length+2);

    //�ļ�������Ϊ�� ���� ������ʵĶ����Ƿ�����Ŀ¼����ôֱ��Pass ���²������ִ��
    if(FileFullPath.Length ==0 || (FileFullPath.Length==2 && FileFullPath.Buffer[0]==L'\\'))
    {
        goto PASSTHROUGH;
    }

    //!!!!!!!!!!!!!!!!!!
    //���ﴦ�����usb��ȫ�̼���//�������ʱ��ϵͳ�Ѿ���¼������
    if(pExt->bUsbDevice)
    {
        //����USB�ϵ��ļ���ֻ���ڼ���ϵͳ��¼���Ҽ��ܼ���ϵͳ��Ҫ����ܵģ����Ҵ򿪵����ļ���������ļ��� DATA�����������ǲŽ��м��ܴ���
        if(FileFullPath.Buffer[(FileFullPath.Length>>1)-1]==L'\\')
        {
            goto PASSTHROUGH;
        }

        if(IsOpenDirectory(pstack->Flags)||IsDirectory(pstack->Parameters.Create.Options))
        {
            goto PASSTHROUGH;
        }
        if(ExeHasLoggon!=0 && IsUsbDeviceNeedEncryption(DeviceObject) && !bOpenFileStream)
        {
            bFolderUnderProtect = TRUE;
            bEncryptForFolder   = TRUE;
            bEncryptFileTypeForFolder = ENCRYPT_ALL;
            goto FOLDERANDNOFOLDER;
        }		 
        goto PASSTHROUGH;//���ڼ���USB �ϵ� ��DATA���ļ��������ǲ������κδ���

    }
    if(bHasFileExt && exLenght== 3*sizeof(WCHAR) &&_wcsnicmp(szExt,L"TXT",3)==0)
    {
        iostatus.Status			= STATUS_SUCCESS;
        iostatus.Information	= 0;
    }

    //!!!!!!!!!!!!!!!!!
    //���ﴦ����Ǹ���fileobject�����ǵģ�ֱ�����¼�������

    if(PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject))
    {
        if(bOpenFileStream)
        {
            pFileObject->RelatedFileObject = NULL;
            if(pFileObject->FileName.Buffer)
            {
                ExFreePool(pFileObject->FileName.Buffer);				
            }
            pFileObject->FileName.Buffer = ExAllocatePool(PagedPool,2+lFullPathLenInBytes);
            pFileObject->FileName.Length = (USHORT)lFullPathLenInBytes;
            pFileObject->FileName.MaximumLength = ( (USHORT)lFullPathLenInBytes +2);

            memcpy(pFileObject->FileName.Buffer,FullPathName, lFullPathLenInBytes );
            pFileObject->FileName.Buffer[lFullPathLenInBytes >>1] =L'\0';
            goto PASSTHROUGH;
        }else
        {
            pVirtualDiskFile = ((PPfpFCB)pFileObject->RelatedFileObject->FsContext)->pDiskFileObject->pVirtualDiskFile;
            bPareseCompleted    = TRUE;
            bFolderUnderProtect = TRUE;
            ExAcquireResourceExclusiveLite(pParentRootDir->AccssLocker,TRUE);
            ExAcquireResourceExclusiveLite(pVirtualDiskFile->pVirtualDiskLocker,TRUE );
            pDeviceResouce = pParentRootDir->AccssLocker;
            pVirtualFileResouce  = pVirtualDiskFile->pVirtualDiskLocker;
            //KdPrint(("Create function accquire file resource %Xh\r\n",pVirtualFileResouce));
            pDiskFileObject = ((PPfpFCB)pFileObject->RelatedFileObject->FsContext)->pDiskFileObject;
            goto FOLDERANDNOFOLDER;
        }

    }



    ////VirtualizerStart();
    bFolderUnderProtect =GetFolderProtectProperty(DeviceLetter ,
        FullPathName,
        FileFullPath.Length>>1,
        &ProtectTypeForFolder,
        &bEncryptForFolder,
        &bBackupForFolder,
        &bFolderLocked,
        &bEncryptFileTypeForFolder);
    ////VirtualizerEnd();
    //�����ϵͳû������ ��ô�Ͳ����������� ��ǰ���ʵ�Ŀ¼�����ļ� ���ٰ�ȫ�ļ�����
    if( !ExeHasLoggon && !bFolderUnderProtect)
    {
        goto PASSTHROUGH;
    }

    if(bFolderUnderProtect)//˵�� ����ļ����ڸ��˰�ȫ�ļ��е� ����
    {
        //1: ���˰�ȫ�ļ���
        if(bFolderLocked||(ExeHasLoggon==0) )//�ļ��еĹ��� ������״̬ �κη��ʶ���ܾ�
        {
            //1:�ļ������� ���� �û�û�е�¼
            iostatus.Status = STATUS_ACCESS_DENIED;
            iostatus.Information = 0;
            goto EXIT;
        }else 
        {//2: �ļ��н��������û���¼

            if(IsOpenDirectory(pstack->Flags)||IsDirectory(pstack->Parameters.Create.Options))
            {
                goto PASSTHROUGH;
            }
            if((FileFullPath.Buffer[(FileFullPath.Length>>1)-1]==L'\\') || bEncryptFileTypeForFolder== ENCRYPT_NONE )//������˰�ȫ�ļ����ڽ��� ״̬�����Ҳ���Ҫ����ܵ���ôֱ��pass���������������ļ���child �����κβ���
            {
                goto PASSTHROUGH;//ֱ��pass��
            }
            if(bOpenFileStream)
            {
                goto PASSTHROUGH;	
            }

            //������Ҫ��Create����Param��Option������ٵĴ���
            {
                //FILE_OPEN_REPARSE_POINT �Ǵ���Ӧ�ó����Լ���������ݣ�������������û��Ҫ������������ݽ��м���
                if(pstack->Parameters.Create.Options&FILE_OPEN_REPARSE_POINT)
                {
                    goto PASSTHROUGH;
                }

            }//end of  //������Ҫ��Create����Param��Option������ٵĴ���
            goto FOLDERANDNOFOLDER; //ʣ�µľ��ǽ����µ�Ҫ��ӽ��ܵ� ���ʸ��˰�ȫ�ļ����µ� child
            //����״̬�£����� û�б�Ҫ�õ���ǰ�Ľ��̰�
        }
    }else //2�����Ǹ��˰�ȫ�ļ���
    {

        if(bHasFileExt && exLenght== 3*sizeof(WCHAR) &&_wcsnicmp(szExt,L"TXT",3)==0)
        {
            iostatus.Status			= STATUS_SUCCESS;
            iostatus.Information	= 0;
        }
        if(!ExeHasLoggon)//û�е�¼
        {
            goto PASSTHROUGH;
        }
        //�����¼�����
        if(IsOpenDirectory(pstack->Flags)||IsDirectory(pstack->Parameters.Create.Options))
        {
            goto PASSTHROUGH;
        }
        if((FileFullPath.Buffer[(FileFullPath.Length>>1)-1]==L'\\') /*||PfpFindExcludProcess(PsGetProcessId(IoGetCurrentProcess() ))*/)
        {
            goto PASSTHROUGH;
        }
        if(bOpenFileStream)
        {
            goto PASSTHROUGH;	
        }
    }



    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////

    //KdPrint(((procdess id )));
    // 	if(	PfpFindExcludProcess(PsGetProcessId(IoGetCurrentProcess() )	))
    // 	{	//��������Ǳ��ų��Ľ��̣�������ǰ����
    // 		goto PASSTHROUGH;
    // 	}

    pDeviceResouce = pParentRootDir->AccssLocker;
    ExAcquireResourceExclusiveLite(pDeviceResouce,TRUE );
    pParentDir  = PfpPareseToDirObject(pParentRootDir,FullPathName,&pRemainer,&bPareseCompleted);

    //ASSERT((pParentDir  == pParentRootDir)?bPareseCompleted:TRUE);
    if(!bPareseCompleted)
    {
        ExReleaseResourceLite(pDeviceResouce);
        pDeviceResouce = NULL;
        pParentDir = NULL;
        pRemainer = NULL;
    }else
    {
        //�ҵ���Ŀ¼
        UNICODE_STRING TempString;

        ////VirtualizerStart();
        TempString.Buffer = pRemainer;
        TempString.Length =  (USHORT)(lFullPathLenInBytes-(ULONG)((PUCHAR)pRemainer-(PUCHAR)FullPathName));
        TempString.MaximumLength  = TempString.Length +2;
        pVirtualDiskFile = PfpFindVirtualDiskFileObjectInParent(pParentDir,&TempString);
        if(pVirtualDiskFile)
        {
            pVirtualFileResouce		= pVirtualDiskFile->pVirtualDiskLocker;
            ExAcquireResourceExclusiveLite(pVirtualFileResouce,TRUE);
            //KdPrint(("Create function accquire file resource %Xh\r\n",pVirtualFileResouce));
        }
        ////VirtualizerEnd();
    }
    if(bHasFileExt && exLenght== 3*sizeof(WCHAR) &&_wcsnicmp(szExt,L"TXT",3)==0)
    {
        iostatus.Status			= STATUS_SUCCESS;
        iostatus.Information	= 0;
    }
    if(IsFileTypeBelongExeType(szExt)||!PfpIsThereValidProcessInfo()||(ProcessInfo= (IrpContext?IrpContext->pProcessInfo:PfpGetProcessInfoForCurProc()))	== NULL|| !ProcessInfo->bEnableEncrypt)//û���ҵ������Ӧ�ĳ��� ������ �������ǰ�ǽ�ֹ���ܵ�
    {		
        //����3��������Ե�ǰ���ļ�������
        //1����exe���͵��ļ���ϵͳ�������
        //2: ��ǰ�Ľ��̲��Ǽ��ܽ��̣�
        //3: ��ǰ�Ľ��̱���ֹ�����ˣ�
        //�������ǣ� �ر����Ƕ�����ļ��Ĵ򿪵ļ�¼����ϵͳ��������������ļ�������

        //2: ��DiskFileObject �Ķ�������رղ���ɾ�� ϵͳ�ӳٹرյ��ļ���MM��ʱ��Դ򿪵��ļ���ȡ�ӳٹرյ��㷨��
        if(pVirtualDiskFile)
        {
            PDISKFILEOBJECT pDiskFile ;
            pDiskFile = PpfGetDiskFileObjectFromVirtualDisk(pVirtualDiskFile );
            if(pDiskFile )
            {
                PfpCloseFileHasGoThroughCleanupAndNotUsed(pDiskFile);//!!!!!!!!!!!!!!!�������û�д��ڵı�Ҫ���ѵ��� exe�ļ� ���Ǵ����˲���Ҫ�رգ�
            }
        }
        if(bHasFileExt && 
            _wcsicmp(szExt,L"EXE")==0 && 
            ((ProcessInfo= (IrpContext?IrpContext->pProcessInfo:PfpGetProcessInfoForCurProc()))!= NULL) && 
            ProcessInfo->bBowser && 
            !ProcessInfo->bAllCreateExeFile)
        {
            AcsType	= PfpGetFileAttriForRequestEx(DeviceObject,FullPathName,lFullPathLenInBytes,&FilesizeForExistFile);
            if(AcsType	== ACCESSING_FILE_NONEXIST )
            {
                Options = pstack->Parameters.Create.Options;
                Options = ((Options>> 24) & 0x000000FF);
                if(Options ==FILE_OPEN_IF || Options ==FILE_CREATE||FILE_OVERWRITE==Options)
                {
                    iostatus.Status			= STATUS_ACCESS_DENIED;
                    iostatus.Information	= 0;
                    goto EXIT;
                }				
            }
        }

        goto PASSTHROUGH;
    }
    if(bHasFileExt && exLenght== 3*sizeof(WCHAR) &&_wcsnicmp(szExt,L"TXT",3)==0)
    {
        iostatus.Status			= STATUS_SUCCESS;
        iostatus.Information	= 0;
    }
    if(!ProcessInfo->bBowser)//��������������
    {
        bFileExtInProcessNotSelected = (bHasFileExt && PfpFileExtentionExistInProcInfoNotSelete(ProcessInfo,szExt));
    }else//����������������ô��û�к�׺�Ļ�����û�а������ļ����͵��ļ�һ�ɲ���
    {
        if(!bHasFileExt ||  !PfpFileExtentionExistInProcInfo(ProcessInfo,szExt))// û���ļ���׺����û�����ù������͵��ļ�
        {
            goto PASSTHROUGH;// PASS ���� ������
        }
    }


FOLDERANDNOFOLDER: //����ľ��Ǹ��˰�ȫ�ļ��кͿ��Ž��̷��ʵ�ʱ��Ҫ����
    //������Ǵ��� û�к�׺�Ļ�����Ҫ����ܵĻ����� û��ָ���ļ������͵��ļ�
    //��Щ��Ҫ��ȫ������

    //����Ŀ¼ ������
    // ��directory �Ĳ���

    if(pDeviceResouce== NULL)
    {
        ////VirtualizerStart()	;
        pDeviceResouce =  pParentRootDir->AccssLocker;
        ExAcquireResourceExclusiveLite(pDeviceResouce,TRUE );
        pParentDir  = PfpPareseToDirObject(pParentRootDir,FullPathName,&pRemainer,&bPareseCompleted);
        ////VirtualizerEnd();

        if(!bPareseCompleted)
        {				
            PDISKDIROBEJECT pTempParentDir = NULL;

            pTempParentDir =PfpMakeVirtualChildDirForFile(pParentDir,&pRemainer);

            if(pTempParentDir == NULL)
            {
                iostatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                iostatus.Information = 0;
                goto EXIT;
            }

            ASSERT(pTempParentDir != pParentDir);
            pParentDir = pTempParentDir ;
        } 

    }

    if(pVirtualDiskFile== NULL)
    {
        UNICODE_STRING TempString;
        ////VirtualizerStart();
        TempString.Buffer = pRemainer;
        TempString.Length =  (USHORT) (lFullPathLenInBytes-(ULONG)((PUCHAR)pRemainer-(PUCHAR)FullPathName));
        TempString.MaximumLength  = TempString.Length +2;
        pVirtualDiskFile= PfpFindVirtualDiskFileObjectInParent(pParentDir,&TempString);		
        ////VirtualizerEnd();
        if(pVirtualDiskFile == NULL )
        {
            pVirtualDiskFile =CreateVirDiskFileAndInsertIntoParentVirtual(pParentDir,pRemainer);
            if(pVirtualDiskFile== NULL)
            {
                iostatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                iostatus.Information = 0;
                goto EXIT;
            }
        }
        pVirtualFileResouce		=pVirtualDiskFile->pVirtualDiskLocker;
        ExAcquireResourceExclusiveLite(pVirtualFileResouce,TRUE);
        //KdPrint(("Create function accquire file resource %Xh\r\n",pVirtualFileResouce));

        ASSERT(pDiskFileObject== NULL);

    }
    else	
    {
        ASSERT(pVirtualFileResouce!= NULL);

    }

    if(bHasFileExt && exLenght== 3*sizeof(WCHAR) &&_wcsnicmp(szExt,L"TXT",3)==0)
    {
        iostatus.Status			= STATUS_SUCCESS;
        iostatus.Information	= 0;
    }
    ////VirtualizerStart();
    if(pDiskFileObject=PpfGetDiskFileObjectFromVirtualDisk(pVirtualDiskFile))
    {
        //ExConvertExclusiveToSharedLite(pDeviceResouce);
    }
    ////VirtualizerEnd();

    if(IsFileTypeBelongExeType(szExt))//!!!!!�������Ϊ������ȴ���һ����ʱ�ļ�����ʵ��һ����ִ�У�exe ���͵��ļ����Ǻ�׺���ǣ�����Ȼ����ReName ��Ҫ���ɵ��ļ�
    {
        if(pDiskFileObject )
        {		
            PfpCloseFileHasGoThroughCleanupAndNotUsed(pDiskFileObject);
            pDiskFileObject  = PpfGetDiskFileObjectFromVirtualDisk(pVirtualDiskFile);
        }
        if(pDiskFileObject  == NULL)
            goto PASSTHROUGH;
    }

    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    //��� ����򿪵��ļ��Ѿ��� delayclose�� �������� ��ô˵������ļ��Ǵ����ڴ����ϵĲ�����û�б��򿪹���
    // 	if(pDiskFileObject== NULL && (pDiskFileObject = PfpFindDiskFileObjectFromDelayClose(DeviceLetter,&FileFullPath))!= NULL)
    // 	{					
    // 		//KdPrint (("in create fileobject Opened From DelayClose queue %wZ\n",&pFileObject->FileName));
    // 		bFirstOpen  = TRUE;	
    // 		pDiskFileObject ->bProcessOpened	= (ProcessInfo!= NULL);
    // 		pDiskFileObject ->bUnderSecurFolder = bFolderUnderProtect;
    // 		
    // 		AcsType = ACCESSING_FILE_EXIST;
    // 		goto PassFromDelayClose;
    // 	}

    //ͨ��Ҫ���ʵ��ļ���ȫ·���������ǲ����Ѿ��д򿪹���DiskFileObject
    if(pDiskFileObject!= NULL)
    {				

        if((pDiskFileObject ->pFCB!= NULL) &&FlagOn( ((PPfpFCB)pDiskFileObject->pFCB)->FcbState,FCB_STATE_FILE_DELETED))
        {
            iostatus.Status			= STATUS_DELETE_PENDING;
            pDiskFileObject			= NULL;
            goto EXIT;	
        }
        if(PfpIsAllFileObjectThroughCleanup(pDiskFileObject))
        {
            pDiskFileObject->bOpeningAfterAllGothroughCleanup = TRUE;
        }
        AcsType = ACCESSING_FILE_EXIST;
        goto PassFromDelayClose;

    }else
    {
        bFirstOpen  = TRUE;	
    }


    if(ProcessInfo && bFileExtInProcessNotSelected)//�ҵ����̣�����������͵��ļ� ��������̵������б�����
    {

        ExAcquireFastMutex(&ProcessInfo->HandleMutex);
        bFileOurCreated=PfpIsFileInProcessCreated(ProcessInfo,(IrpContext?IrpContext->hProcessOrignal:PsGetProcessId(IoGetCurrentProcess())),DeviceLetter,FullPathName);
        ExReleaseFastMutex(&ProcessInfo->HandleMutex);

        if(!bFileOurCreated)
            goto PASSTHROUGH;	
    }

    //��� ���ʵ�Ŀ�����ļ������ļ��У�������ļ��Ƿ����
    // 
    // 	if(wcschr(FullPathName,L':'))
    // 	{
    // 		KdPrint((" \r\n"));
    // 	}


    AcsType	= PfpGetFileAttriForRequestEx(DeviceObject,FullPathName,lFullPathLenInBytes,&FilesizeForExistFile);
    ////VirtualizerEnd();
    //�ļ���  ��ǰ����� �����ļ��� ������ �ļ������ڣ��ļ����ڻ��ߴ��ڵ��ļ���ֻ����
    if(AcsType !=ACCESSING_FILE_NONEXIST &&  AcsType!=ACCESSING_FILE_EXIST &&  AcsType!= ACCESSING_FILE_EXIST_READONLY)
    {		
        goto PASSTHROUGH;
    }
    // ���� �����ļ��Ĳ�������Ҫ��������ļ��� �������ǲ��ǺϷ���
    // �ļ������ڵ�ʱ�� �ǲ����� �� �򿪻��߸��ǵķ�ʽ�� �����ļ��ġ�
    Options = pstack->Parameters.Create.Options;
    Options = ((Options>> 24) & 0x000000FF);
    if( AcsType ==ACCESSING_FILE_NONEXIST && ( Options == FILE_OPEN ||Options == FILE_OVERWRITE)&& !PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject) )
    {
        if(!PfpIsFileNameValid(pFileObject->FileName.Buffer,pFileObject->FileName.Length))
        {
            iostatus.Status			= STATUS_OBJECT_NAME_INVALID;
        }
        else
        {
            iostatus.Status			= STATUS_OBJECT_NAME_NOT_FOUND;
        }
        iostatus.Information	= 0;
        goto EXIT;	
    }

    //����ļ��Ѿ����� ��ȡ�ļ������ݿ����ǲ��Ǽ��ܵ��ļ�

    pDiskFileObject = NULL;
    bFirstOpen		= TRUE;

    if( AcsType != ACCESSING_FILE_NONEXIST )//�ļ�����
    {
        if( /*!PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject) &&*/!bFolderUnderProtect )
        {
            if(FilesizeForExistFile==0 && AcsType==ACCESSING_FILE_EXIST_READONLY )
                goto PASSTHROUGH;

            if(!PfpIsFileEncryptedAccordtoFileSize(FilesizeForExistFile) && !bFileOurCreated)
            {
                //KdPrint (("in create fileobject FileSize not correct for Encrypted File %wZ\n",&pFileObject->FileName));
                goto PASSTHROUGH;
            }
            if(bFileExtInProcessNotSelected)
            {	
                goto PASSTHROUGH;			

            }
            if(/*bFirstOpen && */!PfpIsFileEncrypted(&FileFullPath,DeviceObject) && !bFileOurCreated )
            {		
                goto PASSTHROUGH;
            }
        }

        //�������ļ�û�д򿪹�����������ļ����Ǽ��ܵ��ļ�����ô ��˵���Ǵ��ڵ�һ�������ļ���������

    }else //����ļ�������
    {		
        if(ProcessInfo && ProcessInfo->bBowser && !ProcessInfo->bAllCreateExeFile && bHasFileExt && IsFileTypeBelongExeType(szExt) && (Options&(FILE_SUPERSEDE|FILE_CREATE|FILE_OPEN_IF|FILE_OVERWRITE_IF)))//!!!�������ָ���˲��ô�����ִ�����͵��ļ�
        {
            iostatus.Status			= STATUS_ACCESS_DENIED;
            iostatus.Information	= 0;
            goto EXIT;
        }
        if(bFileExtInProcessNotSelected  )		//�ļ������ڲ���
        {
            goto PASSTHROUGH;
        }

    }

    //����Ĵ�������жϵ�ǰ��������ǲ��� ������ļ��ķ���Ҫ���Ǽ��ܵķ�ʽ��

    //�жϵ�ǰ��Ӧ�ó��򴴽����ǲ��ǿ��ŵ�

    if(!ProcessInfo  && !bFolderUnderProtect && !PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject))//��ǰ���̲����ţ���������ļ��Ѿ����ڱ����ŵĽ��̱༭
    {
        //���� �����ŵĽ��̡�
        if(PfpIsRequestWriteAccess(Irp)&& !bFirstOpen)//����ǿ��ŵĽ���Ҫд��Ȩ�ޣ��������ʱ���ļ��Ѿ������ŵĽ������ڱ༭��
        {
            if(pDiskFileObject->pFCB)
            {
                if(((PPfpFCB)pDiskFileObject->pFCB)->UncleanCount ==0)
                {
                    ExAcquireFastMutex(((PPfpFCB)pDiskFileObject->pFCB)->Other_Mutex);

                    ((PPfpFCB)pDiskFileObject->pFCB)->bModifiedByOther = TRUE;
                    ExReleaseFastMutex(((PPfpFCB)pDiskFileObject->pFCB)->Other_Mutex);

                }else
                {//˵�� ���ʱ���������ڱ��༭�У����ڷǿ��ŵĽ��̷��� �ܾ����ʡ�

                    iostatus.Status			= STATUS_ACCESS_DENIED;
                    iostatus.Information	= 0;
                    goto EXIT;
                }				
            }
            //���������ĵ�����reference �ǲ����Ѿ���0
        }
        goto PASSTHROUGH;
    }


    if(bFirstOpen)//!!!��һ�δ�����ļ���ʱ�� ����һ��DISKFILEOBJECT�Ķ���
    {
        if( (pDiskFileObject = PfpCreateDiskFileObject(&FileFullPath,DeviceObject)) == NULL)
        {
            iostatus.Status			= STATUS_INSUFFICIENT_RESOURCES;
            iostatus.Information	= 0;
            goto EXIT;	
        }
        pDiskFileObject->pParentDirResource = pDeviceResouce;
        pDiskFileObject ->bProcessOpened = (ProcessInfo!= NULL);
        pDiskFileObject ->bUnderSecurFolder = bFolderUnderProtect;
    }

PassFromDelayClose:

    //////////////////////////////////////////////////////////////////////////
    //								do backup wrok !!1:�ж��ǲ���Ҫ���ݣ�2:�Ա��ݵ�Ŀ���ļ��ж��Ƿ���ڣ������ھ���ͬ��һ���ļ�����
    /*
    if(  //��һ�δ򿪻��ߴ�������ļ���
    FALSE ||
    (pDiskFileObject->bNeedBackUp = (bFolderUnderProtect?
    (bBackupForFolder?
    (bHasFileExt?
    IsFileTypeEncryptForFolder(DeviceLetter,
    FullPathName,
    (FileFullPath.Length>>1),
    szExt)
    :
    FALSE
    )
    :
    FALSE
    )
    :
    (ProcessInfo?
    PfpIsFileTypeNeedBackup(ProcessInfo,szExt)
    :
    FALSE
    )
    ) 
    )//) //��������ΪҪ�󱸷�
    &&(bFirstOpen||!PfpIsBackupFileObjectStillValid(pDiskFileObject)) 
    && !BooleanFlagOn( pstack->Parameters.Create.Options,FILE_DELETE_ON_CLOSE)	//������ʱ�ļ�
    && g_szBackupDir!= NULL														//���Ҫ�󱸷ݣ���ô���ж��ǲ��ǵ�һ�δ�����ļ��� �����˱����ļ���
    )																			// ��鵱ǰ���ļ� �ǲ�������򿪵ĳ���Ҫ�󱸷ݵ�                       
    //ֻ���ڵ�һ�δ򿪵�ʱ���������Ҫ��������
    {
    //�������ǲ��Ǵ򿪵ı����ļ����е��ļ����������ôĬ���ǲ��Դ��ļ� ���ݵ�

    if(!IsFileUnderBackupDir(DeviceLetter,&FileFullPath))
    {
    if(STATUS_INSUFFICIENT_RESOURCES == PfpDoBackUpWorkAboutCreate(pDiskFileObject,DeviceObject,ProcessInfo, FullPathName))
    {
    iostatus.Status			= STATUS_INSUFFICIENT_RESOURCES;
    iostatus.Information	= 0;
    goto EXIT;
    }	
    }	

    }
    */
    //end of backup 
    // do backup wrok
    //////////////////////////////////////////////////////////////////////////

    ////VirtualizerStart();
    ThreadTopLevelContext =  PfpSetTopLevelIrp(&TopLevelContext,FALSE,FALSE);	

    __try 
    {			
        BOOLEAN  doLog= FALSE;
        if(IrpContext== NULL)
        {
            IrpContext = PfpCreateIrpContext( Irp, TRUE );
            IrpContext->hProcessOrignal=PsGetProcessId(IoGetCurrentProcess());
            IrpContext->pProcessInfo   =ProcessInfo;
        }
        PfpUpdateIrpContextWithTopLevel( IrpContext, ThreadTopLevelContext );
        IrpContext->pNextDevice = pAttachedDevice;
        IrpContext->RealDevice  = DeviceObject;
        IrpContext->OriginatingIrp = Irp;

        iostatus = PfpEncapCreateFile(IrpContext,Irp,AcsType,bFirstOpen,&FileFullPath,&pDiskFileObject,&bIrpPost);

        if(bIrpPost  ||!NT_SUCCESS(iostatus.Status))
        {
            goto Try_exit;
        }
        if( ProcessInfo )//д����־ ��¼ÿ������ʵ���򿪵��ļ�
        {
            HandleOfExe *				pHandleInfo = NULL;
            PCCBRECORD					pCcbRecord = NULL;
            PPROCESSCREATEDFILEWithCCBs pProcessCreatedFileWithCCB= NULL;
            pCcbRecord = ExAllocatePoolWithTag(PagedPool,sizeof(CCBRECORD),'00CC');
            if(pCcbRecord == NULL)
            {
                goto Process_File;
            }

            ExAcquireFastMutex(&ProcessInfo->HandleMutex);

            pHandleInfo = PfpGetHandleInfoUsingHanlde(ProcessInfo,IrpContext->hProcessOrignal);
            if(pHandleInfo == NULL)
            {
                pHandleInfo = PfpAddHanldeIntoProcessInfo(IrpContext->hProcessOrignal,ProcessInfo);
            }
            if(pHandleInfo== NULL)
            {
                ExReleaseFastMutex(&ProcessInfo->HandleMutex);
                goto Process_File;
            }

            pProcessCreatedFileWithCCB = PfpGetCreatedFileWithCCBFromHandleOfexe(pHandleInfo,DeviceLetter,FullPathName);
            if(pProcessCreatedFileWithCCB == NULL)
            {
                pProcessCreatedFileWithCCB = PfpCreateProcessCreatedFileWithCCB(DeviceLetter,FullPathName);
                if(pProcessCreatedFileWithCCB == NULL)
                {
                    ExReleaseFastMutex(&ProcessInfo->HandleMutex);
                    goto Process_File;
                }
                PfpAddCreateFilesWithCCBsIntoHandleOfExe(pHandleInfo,pProcessCreatedFileWithCCB);
            }
            PfpAddCCBIntoProcessCreatedFilesWithCCBs(pProcessCreatedFileWithCCB,pCcbRecord);
            pCcbRecord = NULL;
            ExReleaseFastMutex(&ProcessInfo->HandleMutex);

Process_File:


            if(pCcbRecord )
            {
                ExFreePool(pCcbRecord);
            }

        }
        if(!bFirstOpen)
        {
            goto Try_exit;
        }

        if(ProcessInfo )
        {
            if(!ProcessInfo ->bForceEncryption )
            {
                if(((PPfpFCB)(pDiskFileObject->pFCB))->Header.FileSize.QuadPart==0 && ( !bHasFileExt ||!PfpFileExtentionExistInProcInfo(ProcessInfo,szExt)))
                {
                    //�����̲���ǿ�Ƽ��ܵ�ʱ�򣬶Բ�Ҫ����ܵ��ļ�������ȷ�Ĳ���
                    ((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt = FALSE;
                    ((PPfpFCB)(pDiskFileObject->pFCB))->bWriteHead   = FALSE;
                    pDiskFileObject->bFileNOTEncypted				 = TRUE;
                }
            } 
            if(((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt == FALSE)//~!!!!�����ܵ��ļ�Ҫ������ļ��ŵ�ָ���Ľ����б���ȥ��ǰ��Ҫ���ñȽ�
            {
                ExAcquireFastMutex(&ProcessInfo->HandleMutex);
                PfpAddCreatedFileIntoProcess(ProcessInfo,IrpContext->hProcessOrignal,DeviceLetter,FullPathName);
                ExReleaseFastMutex(&ProcessInfo->HandleMutex);
            }
            if(g_bLog && g_LogEvent)
            {
                DoLog(DeviceLetter,&FileFullPath,&ProcessInfo->ProcessName,TRUE,((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt);

            }
        }

        if(bFolderUnderProtect/* && bEncryptForFolder */&& bEncryptFileTypeForFolder== ENCRYPT_TYPES )//!!!!�Ը��˰�ȫ�ļ���������ļ����ָ���˶�ָ�����͵��ļ����м��ܣ���ǰ����ļ��Ĵ�С��0��
            //�ļ�û�����ͣ����߲���Ҫ����ܵ��ļ����ͣ���ô ������Ϊ������
        {
            if(((PPfpFCB)(pDiskFileObject->pFCB))->Header.FileSize.QuadPart==0 && 
                ( !bHasFileExt ||!IsFileTypeEncryptForFolder(DeviceLetter ,FullPathName,FileFullPath.Length/sizeof(WCHAR),szExt)))
            {
                ((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt = FALSE;
                ((PPfpFCB)(pDiskFileObject->pFCB))->bWriteHead   = FALSE;
                pDiskFileObject->bFileNOTEncypted				 = TRUE;
            }
        }
        if(pExt->bUsbDevice)
        {
            if(((PPfpFCB)(pDiskFileObject->pFCB))->Header.FileSize.QuadPart==0 && ( !IsFileNeedEncryptionForUsb(DeviceObject,bHasFileExt?szExt:NULL)))
            {
                ((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt = FALSE;
                ((PPfpFCB)(pDiskFileObject->pFCB))->bWriteHead   = FALSE;
                pDiskFileObject->bFileNOTEncypted				 = TRUE;
            }
        }

Try_exit:
        ;
    }
	__except(PfpExceptionFilter( IrpContext, GetExceptionInformation() ))
    {

        KdPrint (("Voilation Happend in create fileobject %wZ\n",&pFileObject->FileName));
        //
        //  We had some trouble trying to perform the requested
        //  operation, so we'll abort the I/O request with
        //  the error status that we get back from the
        //  exception code
        //

        ntstatus	= PfpProcessException( IrpContext, NULL, GetExceptionCode() );
        IrpContext	= NULL;
        if(NT_SUCCESS(ntstatus))
        {
            iostatus.Status =STATUS_ACCESS_DENIED;
            iostatus.Information = 0;
        }
    }
    ////VirtualizerEnd();

    //����ɹ��ˣ������ǵ�һ�δ�����ļ�����ôpDiskFileObject ���Ǹմ����ģ���ô��Ҫ�ӵ�ÿ��spydeviceȫ�ֵ��������档
    if(pDiskFileObject && NT_SUCCESS(iostatus.Status) && !bIrpPost)
    {		
        if(bFirstOpen)
            PfpAddDiskFileObjectIntoItsVirtualDiskFile(pVirtualDiskFile,pDiskFileObject);
        else 
        {
            pDiskFileObject->bOpeningAfterAllGothroughCleanup = FALSE;
        }
    }

    if(NT_SUCCESS(iostatus.Status) && pDiskFileObject && !bFolderUnderProtect)
        PfpIncreFileOpen();

    if (ThreadTopLevelContext == &TopLevelContext) 
    {
        PfpRestoreTopLevelIrp( ThreadTopLevelContext );
    }
EXIT:
    if(pVirtualFileResouce)
    {
        //KdPrint(("Create function release file resource %Xh\r\n",pVirtualFileResouce));
        ExReleaseResourceLite(pVirtualFileResouce);
    }

    if(pDeviceResouce)
    {
        ExReleaseResourceLite(pDeviceResouce);
    }

    if(ProcessInfo && !bIrpPost)
    {
        InterlockedDecrement(&ProcessInfo->nRef);
    }

    if((!NT_SUCCESS(iostatus.Status)||bIrpPost) && bFirstOpen && pDiskFileObject)
    {
        if(pDiskFileObject->pFCB)
        {
            PfpDeleteFCB(&((PPfpFCB)pDiskFileObject->pFCB));					
        }


        if(pDiskFileObject->bNeedBackUp && pDiskFileObject->hBackUpFileHandle!= INVALID_HANDLE_VALUE
            && pDiskFileObject->hBackUpFileObject!= NULL)	
        {
            PfpCloseRealDiskFile(&pDiskFileObject->hBackUpFileHandle,&pDiskFileObject->hBackUpFileObject);
        }
        PfpDeleteDiskFileObject(&pDiskFileObject);
    }
    if(FullPathName)
    {
        ExFreePool(FullPathName);
    }


    if(!bIrpPost )
    {   
        Irp->IoStatus = iostatus;
        PfpCompleteRequest( (IrpContext?&IrpContext:NULL), &Irp, iostatus.Status );
    }

    FsRtlExitFileSystem();	 
    return iostatus.Status;

PASSTHROUGH:


    if(ProcessInfo )
    {
        InterlockedDecrement(&ProcessInfo->nRef);
    }

    if(FullPathName)
    {
        ExFreePool(FullPathName);
    }
    if(pVirtualFileResouce)
    {
        //KdPrint(("Create function release file resource %Xh\r\n",pVirtualFileResouce));
        ExReleaseResourceLite(pVirtualFileResouce);
    }
    if(pDeviceResouce)
    {
        ExReleaseResourceLite(pDeviceResouce);
    }
    PfpCompleteRequest( (IrpContext?&IrpContext:NULL),NULL,0);

    FsRtlExitFileSystem();
    IoSkipCurrentIrpStackLocation(Irp);
    ntstatus = IoCallDriver(pAttachedDevice,Irp);

    return ntstatus;
}
NTSTATUS
PfpCreate (
           __in PDEVICE_OBJECT DeviceObject,
           __in PIRP Irp
           )
           /*++

           Routine Description:

           This is the routine that is associated with IRP_MJ_CREATE IRP.  If the
           DeviceObject is the ControlDevice, we do the creation work for the
           ControlDevice and complete the IRP.  Otherwise, we pass through
           this IRP for another device to complete.

           Note: Some of the code in this function duplicates the functions
           SpyDispatch and SpyPassThrough, but a design decision was made that
           it was worth the code duplication to break out the IRP handlers
           that can be pageable code.

           Arguments:

           DeviceObject - Pointer to device object Filespy attached to the file system
           filter stack for the volume receiving this I/O request.

           Irp - Pointer to the request packet representing the I/O request.

           Return Value:

           If DeviceObject == gControlDeviceObject, then this function will
           complete the Irp and return the status of that completion.  Otherwise,
           this function returns the result of calling SpyPassThrough.

           --*/
{

    KIRQL						oldIrql;
    NTSTATUS					ntstatus ;

    if (DeviceObject == gControlDeviceObject) 
    {
        FsRtlEnterFileSystem();
        KeAcquireSpinLock( &gControlDeviceStateLock, &oldIrql );

        if (gControlDeviceState != CLOSED) 
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = FILE_OPENED;	
        } else 
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = FILE_OPENED;
            gControlDeviceState = OPENED;
        }

        KeReleaseSpinLock( &gControlDeviceStateLock, oldIrql );
        ntstatus = Irp->IoStatus.Status;

        IoCompleteRequest( Irp, IO_DISK_INCREMENT );
        FsRtlExitFileSystem();
        return ntstatus;
    }

    ASSERT(IS_FILESPY_DEVICE_OBJECT( DeviceObject ) );

    return PfpCommonCreate(NULL,DeviceObject,Irp);
}

BOOLEAN
PfpIsRequestWriteAccess(PIRP pIrp)
{
    PIO_STACK_LOCATION piSp = NULL;

    piSp = IoGetCurrentIrpStackLocation(pIrp);

    return (BOOLEAN)FlagOn(piSp ->Parameters.Create.SecurityContext->DesiredAccess,FILE_WRITE_DATA);

}


IO_STATUS_BLOCK
PfpOpenExistingFcb (
                    IN PIRP_CONTEXT IrpContext,
                    IN PFILE_OBJECT FileObject,
                    IN PPfpFCB	*	ppFcb,
                    IN PDISKFILEOBJECT *pDiskFileObject,
                    IN PACCESS_MASK DesiredAccess,
                    IN USHORT		ShareAccess,
                    IN LARGE_INTEGER		AllocationSize,				
                    IN UCHAR		FileAttributes,
                    IN ULONG		CreateDisposition,
                    IN BOOLEAN		DeleteOnClose,
                    OUT PBOOLEAN	OplockPostIrp
                    )

                    /*++

                    Routine Description:

                    This routine opens the specified existing fcb

                    Arguments:

                    FileObject - Supplies the File object

                    Vcb - Supplies the Vcb denoting the volume containing the Fcb

                    Fcb - Supplies the already existing fcb

                    DesiredAccess - Supplies the desired access of the caller

                    ShareAccess - Supplies the share access of the caller

                    AllocationSize - Supplies the initial allocation if the file is being
                    superseded or overwritten

                    EaBuffer - Supplies the Ea set if the file is being superseded or
                    overwritten

                    EaLength - Supplies the size, in byte, of the EaBuffer

                    FileAttributes - Supplies file attributes to use if the file is being
                    superseded or overwritten

                    CreateDisposition - Supplies the create disposition for this operation

                    NoEaKnowledge - This opener doesn't understand Ea's and we fail this
                    open if the file has NeedEa's.

                    DeleteOnClose - The caller wants the file gone when the handle is closed

                    FileNameOpenedDos - The caller hit the short side of the name pair finding
                    this file

                    OplockPostIrp - Address to store boolean indicating if the Irp needs to
                    be posted to the Fsp.

                    Return Value:

                    IO_STATUS_BLOCK - Returns the completion status for the operation

                    --*/

{
    IO_STATUS_BLOCK		Iosb;

    ACCESS_MASK			AddedAccess = 0;
    PPfpFCB				Fcb = *ppFcb;
    //
    //  The following variables are for abnormal termination
    //

    BOOLEAN				UnwindShareAccess		= FALSE;	
    BOOLEAN				DecrementFcbOpenCount	= FALSE;
    PUSERFILEOBJECT		pUserFileobject			= NULL;
    LARGE_INTEGER		orignalFileSize			={0};
    UNREFERENCED_PARAMETER(pDiskFileObject);
    //DebugTrace(+1, Dbg, "FatOpenExistingFcb...\n", 0);

    //
    //  Get the Fcb exlcusive.  This is important as cleanup does not
    //  acquire the Vcb.

    //

    //(VOID)PfpAcquireExclusiveFcb( IrpContext, Fcb );

    orignalFileSize.QuadPart = (Fcb)->Header.FileSize.QuadPart;
    __try 
    {

        *OplockPostIrp = FALSE;

        //
        //  Take special action if there is a current batch oplock or
        //  batch oplock break in process on the Fcb.
        //

        if (FsRtlCurrentBatchOplock( &Fcb->Oplock )) 
        {

            //
            //  We remember if a batch oplock break is underway for the
            //  case where the sharing check fails.
            //

            Iosb.Information = FILE_OPBATCH_BREAK_UNDERWAY;

            Iosb.Status = FsRtlCheckOplock( &Fcb->Oplock,
                IrpContext->OriginatingIrp,
                IrpContext,
                PfpOplockComplete,
                PfpPrePostIrp );

            if (Iosb.Status != STATUS_SUCCESS
                && Iosb.Status != STATUS_OPLOCK_BREAK_IN_PROGRESS) 
            {

                *OplockPostIrp = TRUE;
                try_return( NOTHING );
            }
        }

        //
        //  Check if the user wanted to create the file, also special case
        //  the supersede and overwrite options.  Those add additional,
        //  possibly only implied, desired accesses to the caller, which
        //  we must be careful to pull back off if the caller did not actually
        //  request them.
        //
        //  In other words, check against the implied access, but do not modify
        //  share access as a result.
        //

        if (CreateDisposition == FILE_CREATE)
        {

            Iosb.Status = STATUS_OBJECT_NAME_COLLISION;
            try_return( Iosb );

        } else if (CreateDisposition == FILE_SUPERSEDE) 
        {

            SetFlag( AddedAccess,DELETE & ~(*DesiredAccess) );

            *DesiredAccess |= DELETE;

        } else if ((CreateDisposition == FILE_OVERWRITE) ||
            (CreateDisposition == FILE_OVERWRITE_IF))
        {

            SetFlag( AddedAccess,(FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES) & ~(*DesiredAccess) );

            *DesiredAccess |= FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
        }

        //
        //  Check the desired access
        //

        // 		if (!PfpCheckFileAccess( (UCHAR)Fcb->Attribute,
        // 								 DesiredAccess )) 
        // 		{
        // 
        // 				Iosb.Status = STATUS_ACCESS_DENIED;
        // 				try_return( Iosb );
        // 		}

        //
        //  Check for trying to delete a read only file.
        //

        if (DeleteOnClose &&
            FlagOn( Fcb->Attribute, FAT_DIRENT_ATTR_READ_ONLY )) 
        {

            Iosb.Status = STATUS_CANNOT_DELETE;
            try_return( Iosb );
        }

        //!!!!!������Ҫ����ǰҪ����һ��userfileobject����ֹ������� purgecache�ĺ������õ�ʱ�� ����close����Ϊ��close�ĺ�������Ҫ���Userfileobjects�Ƿ�Ϊ�㣬���һ�ɾ��
        //����ļ���Ӧ��DiskFileObject �������������򿪵Ĺ����м�Ҫ�ȼ���һ��������userfileobject����close������Ϊuserfileobjects ����Ϊ�㣬�Ӷ���ֹɾ��DISKFILEOBJECT ����
        pUserFileobject = PfpCreateUserFileObject(	FileObject,(*pDiskFileObject)->pDiskFileObjectWriteThrough,(*pDiskFileObject)->hFileWriteThrough);
        if( pUserFileobject == NULL )
        {
            Iosb.Status= STATUS_INSUFFICIENT_RESOURCES ;
            try_return( Iosb );
        }

        PfpAddUserFileObjectIntoDiskFileObject(*pDiskFileObject,pUserFileobject);

        //
        //  If we are asked to do an overwrite or supersede operation then
        //  deny access for files where the file attributes for system and
        //  hidden do not match
        //


        //
        //  Check if the Fcb has the proper share access
        //

        // 		if (!NT_SUCCESS(Iosb.Status = IoCheckShareAccess( *DesiredAccess,
        // 															ShareAccess,
        // 															FileObject,
        // 															&Fcb->ShareAccess,
        // 															FALSE ))) 
        // 		{
        // 
        // 				try_return( Iosb );
        // 		}

        //
        //  Now check that we can continue based on the oplock state of the
        //  file.
        //
        //  It is important that we modified the DesiredAccess in place so
        //  that the Oplock check proceeds against any added access we had
        //  to give the caller.
        //

        /*	Iosb.Status = FsRtlCheckOplock( &Fcb->Oplock,
        IrpContext->OriginatingIrp,
        IrpContext,
        PfpOplockComplete,
        PfpPrePostIrp );

        if (Iosb.Status != STATUS_SUCCESS
        && Iosb.Status != STATUS_OPLOCK_BREAK_IN_PROGRESS)
        {

        *OplockPostIrp = TRUE;
        try_return( NOTHING );
        }
        */
        //
        //  Set the flag indicating if Fast I/O is possible
        //

        Fcb->Header.IsFastIoPossible = PfpIsFastIoPossible( Fcb );

        //
        //  If the user wants write access access to the file make sure there
        //  is not a process mapping this file as an image.  Any attempt to
        //  delete the file will be stopped in fileinfo.c
        //
        //  If the user wants to delete on close, we must check at this
        //  point though.
        //

        if (FlagOn(*DesiredAccess, FILE_WRITE_DATA) || DeleteOnClose) 
        {

            Fcb->UncleanCount += 1;
            DecrementFcbOpenCount = TRUE;

            if (!MmFlushImageSection( &Fcb->SegmentObject,MmFlushForWrite )) 
            {
                Iosb.Status = DeleteOnClose ? STATUS_CANNOT_DELETE :STATUS_SHARING_VIOLATION;
                try_return( Iosb );
            }
        }

        //PfpRemoveUserFileObejctFromDiskFileObject(&pDiskFileObj->UserFileObjList,pUserFileObjects );
        //
        //  If this is a non-cached open on a non-paging file, and there
        //  are no open cached handles, but there is a still a data
        //  section, attempt a flush and purge operation to avoid cache
        //  coherency overhead later.  We ignore any I/O errors from
        //  the flush.
        //
        //  We set the CREATE_IN_PROGRESS flag to prevent the Fcb from
        //  going away out from underneath us.
        //

        if (FlagOn( FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING ) &&
            (Fcb->UncleanCount == Fcb->NonCachedUnCleanupCount) &&
            (Fcb->SegmentObject.DataSectionObject != NULL) )
        {

            CcFlushCache( &Fcb->SegmentObject, NULL, 0, NULL );

            //
            //  Grab and release PagingIo to serialize ourselves with the lazy writer.
            //  This will work to ensure that all IO has completed on the cached
            //  data and we will succesfully tear away the cache section.
            //

            ExAcquireResourceExclusiveLite( Fcb->Header.Resource, TRUE);
            ExReleaseResourceLite( Fcb->Header.Resource );

            CcPurgeCacheSection( &Fcb->SegmentObject,
                NULL,
                0,
                FALSE );

        }

        //
        //  Check if the user only wanted to open the file
        //

        if ((CreateDisposition == FILE_OPEN) ||
            (CreateDisposition == FILE_OPEN_IF)) 
        {

            //DebugTrace(0, Dbg, "Doing open operation\n", 0);

            //
            //  If the caller has no Ea knowledge, we immediately check for
            //  Need Ea's on the file.
            //
            // !!!!!!!!!!this line we should add the ea to the real disk file		

            FileObject->FsContext2	= PfpCreateCCB();
            FileObject->FsContext	= Fcb;
            FileObject->SectionObjectPointer = &Fcb->SegmentObject;

            //
            //  Fill in the information field, the status field is already
            //  set.
            //
            Iosb.Status      = STATUS_SUCCESS;
            Iosb.Information = FILE_OPENED;

            try_return( Iosb );
        }

        //
        //  Check if we are to supersede/overwrite the file, we can wait for
        //  any I/O at this point
        //

        if ((CreateDisposition == FILE_SUPERSEDE) ||
            (CreateDisposition == FILE_OVERWRITE) ||
            (CreateDisposition == FILE_OVERWRITE_IF)) 
        {			

            PACCESS_STATE AccessState;
            PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( IrpContext->OriginatingIrp );

            //DebugTrace(0, Dbg, "Doing supersede/overwrite operation\n", 0);

            //
            //  We check if the caller wants ACCESS_SYSTEM_SECURITY access on this
            //  directory and fail the request if he does.
            //

            AccessState = IrpSp->Parameters.Create.SecurityContext->AccessState;

            //
            //  Check if the remaining privilege includes ACCESS_SYSTEM_SECURITY.
            //

            if (FlagOn( AccessState->RemainingDesiredAccess, ACCESS_SYSTEM_SECURITY )) 
            {
                if (!SeSinglePrivilegeCheck( FatSecurityPrivilege,UserMode )) 
                {
                    Iosb.Status = STATUS_ACCESS_DENIED;
                    try_return( Iosb );
                }

                //
                //  Move this privilege from the Remaining access to Granted access.
                //

                ClearFlag( AccessState->RemainingDesiredAccess, ACCESS_SYSTEM_SECURITY );
                SetFlag( AccessState->PreviouslyGrantedAccess, ACCESS_SYSTEM_SECURITY );
            }
            //
            //  And overwrite the file.  We remember the previous status
            //  code because it may contain information about
            //  the oplock status.

            //OldStatus = Iosb.Status;
            Fcb->UncleanCount++;


            Iosb = PfpSupersedeOrOverwriteFile( IrpContext,
                FileObject,
                Fcb,
                AllocationSize,				
                FileAttributes,
                CreateDisposition
                );


            if(NT_SUCCESS(Iosb.Status) && orignalFileSize.QuadPart != 0 )
            {
                FILE_END_OF_FILE_INFORMATION FileSize = {0};
                PfpSetFileNotEncryptSize((*pDiskFileObject)->pDiskFileObjectWriteThrough,FileSize.EndOfFile,IrpContext->pNextDevice );
                ((PPfpFCB)( Fcb))->bNeedEncrypt	= TRUE;
                ((PPfpFCB)( Fcb))->bWriteHead		= TRUE;
                (*pDiskFileObject)->bFileNOTEncypted= FALSE;
            }
            Fcb->UncleanCount--;

            try_return( Iosb );
        }

        //
        //  If we ever get here then the I/O system gave us some bad input
        //

        //	FatBugCheck( CreateDisposition, 0, 0 );

try_exit: NOTHING;

        //
        //  Update the share access and counts if successful
        //

        if ((Iosb.Status != STATUS_PENDING) && NT_SUCCESS(Iosb.Status)) 
        {

            //
            //  Now, we may have added some access bits above to indicate the access
            //  this caller would conflict with (as opposed to what they get) in order
            //  to perform the overwrite/supersede.  We need to make a call to that will
            //  recalculate the bits in the fileobject to reflect the real access they
            //  will get.
            //

            if (AddedAccess) 
            {

                NTSTATUS Status;

                ClearFlag( *DesiredAccess, AddedAccess );
                Status = IoCheckShareAccess( *DesiredAccess,
                    ShareAccess,
                    FileObject,
                    &Fcb->ShareAccess,
                    TRUE );

                //
                //  It must be the case that we are really asking for less access, so
                //  any conflict must have been detected before this point.
                //

                ASSERT( Status == STATUS_SUCCESS );

            } else
            {

                IoUpdateShareAccess( FileObject, &Fcb->ShareAccess );
            }

            UnwindShareAccess = TRUE;

            //
            //  In case this was set, clear it now.
            //

            //ClearFlag(Fcb->FcbState, FCB_STATE_DELAY_CLOSE);

            Fcb->UncleanCount += 1;
            Fcb->OpenCount += 1;
            if (FlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING))
            {
                Fcb->NonCachedUnCleanupCount += 1;
            }

            {
                PPfpCCB Ccb;
                Ccb = (PPfpCCB)FileObject->FsContext2;

                //
                //  Mark the DeleteOnClose bit if the operation was successful.
                //

                if ( DeleteOnClose ) 
                {

                    SetFlag( Fcb->FcbState, FCB_STATE_DELETE_ON_CLOSE );
                }				
            }
        }

    }
	__finally 
    {
        //
        //  If this is an abnormal termination then undo our work
        //

        if (AbnormalTermination() ||Iosb.Status!=STATUS_SUCCESS) 
        {
            if(AbnormalTermination())
            {
                KdPrint(("OpenExsitingFcb function exception \r\n"));
            }
            else
            {
                KdPrint(("OpenExsitingFcb function %Xh",Iosb.Status));
            }

            if (UnwindShareAccess) { IoRemoveShareAccess( FileObject, &Fcb->ShareAccess ); }

            if(pUserFileobject != NULL)	
            {
                PfpRemoveUserFileObejctFromDiskFileObject(&(*pDiskFileObject)->UserFileObjList,pUserFileobject );
                PfpDeleteUserFileObject(&pUserFileobject );
            }

        }

        if (DecrementFcbOpenCount) 
        {
            Fcb->UncleanCount -= 1;			
        }
    }

    return Iosb;
}


IO_STATUS_BLOCK
PfpOpenExistingFile (
                     IN PIRP_CONTEXT	IrpContext,
                     IN PFILE_OBJECT	FileObject,	
                     IN OUT PPfpFCB	*	Fcb,
                     IN PDISKFILEOBJECT* pDiskFileObject,
                     IN PACCESS_MASK	DesiredAccess,					 
                     IN USHORT			ShareAccess,
                     IN LARGE_INTEGER	AllocationSize,	
                     IN UCHAR			FileAttributes	,
                     IN ULONG			CreateDisposition,					
                     IN BOOLEAN			NoEaKnowledge,
                     IN BOOLEAN			DeleteOnClose,
                     PIO_SECURITY_CONTEXT SecurityContext,
                     FILESTATE			AcsType
                     )

                     /*++

                     Routine Description:

                     This routine opens the specified file.  The file has not previously
                     been opened.

                     Arguments:

                     FileObject - Supplies the File object

                     Vcb - Supplies the Vcb denoting the volume containing the file

                     ParentFcb - Supplies the parent directory containing the file to be
                     opened

                     Dirent - Supplies the dirent for the file being opened

                     LfnByteOffset - Tells where the Lfn begins.  If there is no Lfn
                     this field is the same as DirentByteOffset.

                     DirentByteOffset - Supplies the Vbo of the dirent within its parent
                     directory

                     Lfn - May supply a long name for the file.

                     DesiredAccess - Supplies the desired access of the caller

                     ShareAccess - Supplies the share access of the caller

                     AllocationSize - Supplies the initial allocation if the file is being
                     superseded, overwritten, or created.

                     EaBuffer - Supplies the Ea set if the file is being superseded,
                     overwritten, or created.

                     EaLength - Supplies the size, in byte, of the EaBuffer

                     FileAttributes - Supplies file attributes to use if the file is being
                     superseded, overwritten, or created

                     CreateDisposition - Supplies the create disposition for this operation

                     IsPagingFile - Indicates if this is the paging file being opened.

                     NoEaKnowledge - This opener doesn't understand Ea's and we fail this
                     open if the file has NeedEa's.

                     DeleteOnClose - The caller wants the file gone when the handle is closed

                     FileNameOpenedDos - The caller opened this file by hitting the 8.3 side
                     of the Lfn/8.3 pair

                     Return Value:

                     IO_STATUS_BLOCK - Returns the completion status for the operation

                     --*/

{
    IO_STATUS_BLOCK		Iosb;

    NTSTATUS			ntStatus;
    LARGE_INTEGER		orignalFileSize ;
    ACCESS_MASK			AddedAccess = 0;
    PUSERFILEOBJECT		pUserFileobject;
    BOOLEAN				bAlreadyOpened = FALSE;
    UNREFERENCED_PARAMETER(NoEaKnowledge);

    orignalFileSize.QuadPart =0;

    __try {

        //
        //  Check if the user wanted to create the file or if access is
        //  denied
        //

        if (CreateDisposition == FILE_CREATE) 
        {
            Iosb.Status = STATUS_OBJECT_NAME_COLLISION;
            try_return( Iosb );

        } else if ((CreateDisposition == FILE_SUPERSEDE)  ) 
        {

            SetFlag( AddedAccess,DELETE & ~(*DesiredAccess) );

            *DesiredAccess |= DELETE;

        } else if (((CreateDisposition == FILE_OVERWRITE) ||
            (CreateDisposition == FILE_OVERWRITE_IF))  )
        {

            SetFlag( AddedAccess,
                (FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES) & ~(*DesiredAccess) );

            *DesiredAccess |= FILE_WRITE_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
        }

        // 		if (!PfpCheckFileAccess( 			FileAttributes,
        // 			DesiredAccess)) 
        // 		{
        // 
        // 				Iosb.Status = STATUS_ACCESS_DENIED;
        // 				try_return( Iosb );
        // 		}

        //
        //  Check for trying to delete a read only file.
        //

        if (DeleteOnClose &&
            FlagOn( FileAttributes, FAT_DIRENT_ATTR_READ_ONLY ))
        {

            Iosb.Status = STATUS_CANNOT_DELETE;
            try_return( Iosb );
        }
        //
        //  Create a new Fcb for the file, and set the file size in
        //  the fcb.
        //
        if((*pDiskFileObject)->pFCB== NULL)
        {
            (*pDiskFileObject)->pFCB = (PPfpFCB)PfpCreateFCB();// Create fcb 

            *Fcb = (*pDiskFileObject)->pFCB;

            if(*Fcb == NULL)
            {			
                Iosb.Status = STATUS_INSUFFICIENT_RESOURCES;
                try_return(Iosb.Status);
            }
            (*Fcb)->pDiskFileObject  =  *pDiskFileObject;
        }else
        {
            bAlreadyOpened = TRUE;
            *Fcb = (*pDiskFileObject)->pFCB;
            (*Fcb)->FcbState = 0;
        }


        // 		(*pDiskFileObject)->pFCB = *Fcb;// set the fcb into diskfileobject
        // 		(*Fcb)->pDiskFileObject  =  *pDiskFileObject;
        // so below function can initlize the fcb's filed using values read from disk file.

        if(!bAlreadyOpened)
        {
            ntStatus = PfpCreateRealDiskFile(*pDiskFileObject,
                &Iosb,
                CreateDisposition,
                AllocationSize,
                DeleteOnClose,
                NULL,
                0,
                FileAttributes,
                SecurityContext,
                DesiredAccess,
                ShareAccess,
                AcsType
                );

            if(!NT_SUCCESS(ntStatus))
            {
                Iosb.Status = ntStatus;
                try_return(Iosb.Status);
            }
        }
        orignalFileSize.QuadPart = ((PPfpFCB)(*pDiskFileObject)->pFCB)->Header.FileSize.QuadPart;
        //
        //  Now case on whether we are to simply open, supersede, or
        //  overwrite the file.
        //

        switch (CreateDisposition) 
        {

        case FILE_OPEN:
        case FILE_OPEN_IF:

            //DebugTrace(0, Dbg, "Doing only an open operation\n", 0);

            //
            //  If the caller has no Ea knowledge, we immediately check for
            //  Need Ea's on the file.
            //

            //
            //  Setup the context and section object pointers.
            //

            FileObject->FsContext			= *Fcb;
            FileObject->SectionObjectPointer= &(*Fcb)->SegmentObject;
            FileObject->FsContext2			= PfpCreateCCB();
            Iosb.Status						= STATUS_SUCCESS;
            Iosb.Information				= FILE_OPENED;
            break;

        case FILE_SUPERSEDE:
        case FILE_OVERWRITE:
        case FILE_OVERWRITE_IF:

            //DebugTrace(0, Dbg, "Doing supersede/overwrite operation\n", 0);

            //
            //  Determine the granted access for this operation now.
            //
            {
                PACCESS_STATE AccessState;
                PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( IrpContext->OriginatingIrp );

                //DebugTrace(0, Dbg, "Doing supersede/overwrite operation\n", 0);

                //
                //  We check if the caller wants ACCESS_SYSTEM_SECURITY access on this
                //  directory and fail the request if he does.
                //

                AccessState = IrpSp->Parameters.Create.SecurityContext->AccessState;

                //
                //  Check if the remaining privilege includes ACCESS_SYSTEM_SECURITY.
                //

                if (FlagOn( AccessState->RemainingDesiredAccess, ACCESS_SYSTEM_SECURITY )) 
                {

                    if (!SeSinglePrivilegeCheck( FatSecurityPrivilege,
                        UserMode )) 
                    {

                        Iosb.Status = STATUS_ACCESS_DENIED;
                        try_return( Iosb );
                    }

                    //
                    //  Move this privilege from the Remaining access to Granted access.
                    //

                    ClearFlag( AccessState->RemainingDesiredAccess, ACCESS_SYSTEM_SECURITY );
                    SetFlag( AccessState->PreviouslyGrantedAccess, ACCESS_SYSTEM_SECURITY );
                }
            }


            Iosb = PfpSupersedeOrOverwriteFile( IrpContext,
                FileObject,
                *Fcb,
                AllocationSize,												
                FileAttributes,
                CreateDisposition
                );

            if(NT_SUCCESS(Iosb.Status) && orignalFileSize.QuadPart != 0 )
            {
                FILE_END_OF_FILE_INFORMATION FileSize = {0};
                /*IO_STATUS_BLOCK iostatus;
                ZwSetInformationFile((*pDiskFileObject)->hFileWriteThrough,&iostatus,&FileSize,sizeof(FILE_END_OF_FILE_INFORMATION),
                FileEndOfFileInformation);*/
                PfpSetFileNotEncryptSize((*pDiskFileObject)->pDiskFileObjectWriteThrough,FileSize.EndOfFile,IrpContext->pNextDevice );
                ((PPfpFCB)(* Fcb))->bNeedEncrypt	= TRUE;
                ((PPfpFCB)(*Fcb))->bWriteHead		= TRUE;
                (*pDiskFileObject)->bFileNOTEncypted= FALSE;
            }

            break;

        default:

            //DebugTrace(0, Dbg, "Illegal Create Disposition\n", 0);

            ASSERT(0);
            break;
        }

try_exit: NOTHING;

        //
        //  Setup our share access and counts if things were successful.
        //

        if ((Iosb.Status != STATUS_PENDING) && NT_SUCCESS(Iosb.Status)) 
        {



            if ( DeleteOnClose ) 
            {
                SetFlag( ((PPfpFCB)(*Fcb))->FcbState, FCB_STATE_DELETE_ON_CLOSE);
            }

            //
            //  Remove any virtual access the caller needed to check against, but will
            //  not really receive.  Overwrite/supersede is a bit of a special case.
            //

            ClearFlag( *DesiredAccess, AddedAccess );

            IoSetShareAccess( *DesiredAccess,
                ShareAccess,
                FileObject,
                & (*Fcb)->ShareAccess );

            (*Fcb)->UncleanCount += 1;
            (*Fcb)->OpenCount += 1;
            if (FlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING)) 
            {
                (*Fcb)->NonCachedUnCleanupCount += 1;
            }		


            pUserFileobject = PfpCreateUserFileObject(	FileObject,
                (*pDiskFileObject)->pDiskFileObjectWriteThrough,
                (*pDiskFileObject)->hFileWriteThrough);
            if( pUserFileobject == NULL )
            {				
                ntStatus = Iosb.Status= STATUS_INSUFFICIENT_RESOURCES ;
            }else
            {//Open existing file, so there should not be any Diskfleobject for this File.
                PfpAddUserFileObjectIntoDiskFileObject(*pDiskFileObject,pUserFileobject);
            }
        }

    }
	__finally 
    {

        //DebugUnwind( FatOpenExistingFile );

        //
        //  If this is an abnormal termination then undo our work
        //

        if (AbnormalTermination()||!NT_SUCCESS(Iosb.Status)) 
        {

            if((*pDiskFileObject)->pDiskFileObjectWriteThrough)
            {
                ObDereferenceObject((*pDiskFileObject)->pDiskFileObjectWriteThrough);
                (*pDiskFileObject)->pDiskFileObjectWriteThrough = NULL;
            }

            FileObject->FsContext = NULL;

            if(FileObject->FsContext2 )
            {
                ExFreePool(FileObject->FsContext2 );
                FileObject->FsContext2 = NULL;
            }

            if((*pDiskFileObject)->pFCB)
            {
                PfpDeleteFCB(&((PPfpFCB)(*pDiskFileObject)->pFCB));
                (*pDiskFileObject)->pFCB = NULL;
            }

            *Fcb = NULL;
        }

        //DebugTrace(-1, Dbg, "FatOpenExistingFile -> Iosb.Status = %08lx\n", Iosb.Status);
    }
    //Iosb.Status = ntStatus;
    return Iosb;
}


IO_STATUS_BLOCK
PfpSupersedeOrOverwriteFile (
                             IN PIRP_CONTEXT IrpContext,
                             IN PFILE_OBJECT FileObject,
                             IN PPfpFCB		 Fcb,
                             IN LARGE_INTEGER		 AllocationSize,							
                             IN UCHAR		 FileAttributes,
                             IN ULONG		 CreateDisposition

                             )

                             /*++

                             Routine Description:

                             This routine performs a file supersede or overwrite operation.

                             Arguments:

                             FileObject - Supplies a pointer to the file object

                             Fcb - Supplies a pointer to the Fcb

                             AllocationSize - Supplies an initial allocation size

                             EaBuffer - Supplies the Ea set for the superseded/overwritten file

                             EaLength - Supplies the length, in bytes, of EaBuffer

                             FileAttributes - Supplies the supersede/overwrite file attributes

                             CreateDisposition - Supplies the create disposition for the file
                             It must be either supersede, overwrite, or overwrite if.

                             NoEaKnowledge - This opener doesn't understand Ea's and we fail this
                             open if the file has NeedEa's.

                             Return Value:

                             IO_STATUS_BLOCK - Returns the completion status for the operation

                             --*/

{
    IO_STATUS_BLOCK Iosb;
    BOOLEAN ReleasePaging = FALSE;
    UNREFERENCED_PARAMETER(FileAttributes);
    UNREFERENCED_PARAMETER(IrpContext);

    //
    //  The following variables are for abnormal termination
    //
    //DebugTrace(+1, Dbg, "FatSupersedeOrOverwriteFile...\n", 0);

    //
    //  We fail this operation if the caller doesn't understand Ea's.
    //

    /*if (NoEaKnowledge
    && EaLength > 0) 
    {

    Iosb.Status = STATUS_ACCESS_DENIED;

    DebugTrace(-1, Dbg, "FatSupersedeOrOverwriteFile -> Iosb.Status = %08lx\n", Iosb.Status);
    return Iosb;
    }
    */
    __try 
    {

        //
        //  Before we actually truncate, check to see if the purge
        //  is going to fail.
        //
        LARGE_INTEGER NewFileSize={0};
        if (!MmCanFileBeTruncated( &Fcb->SegmentObject,&NewFileSize)) 
        {
            try_return( Iosb.Status = STATUS_USER_MAPPED_FILE );
        }

        //
        //  Setup the context and section object pointers, and update
        //  our reference counts
        //
        FileObject->FsContext = Fcb;
        FileObject->SectionObjectPointer = &Fcb->SegmentObject;
        FileObject->FsContext2= PfpCreateCCB();
        //
        //  Since this is an supersede/overwrite, purge the section so
        //  that mappers will see zeros.  We set the CREATE_IN_PROGRESS flag
        //  to prevent the Fcb from going away out from underneath us.
        //

        CcPurgeCacheSection( &Fcb->SegmentObject, NULL, 0, FALSE );

        //
        //  Now set the new allocation size, we do that by first
        //  zeroing out the current file size.  Then we truncate and
        //  allocate up to the new allocation size
        //

        (VOID)ExAcquireResourceExclusiveLite( Fcb->Header.Resource,TRUE );
        ReleasePaging = TRUE;

        Fcb->Header.FileSize.LowPart		=
            Fcb->Header.ValidDataLength.LowPart = 0;
        Fcb->Header.AllocationSize			= AllocationSize;		

        //
        //  Tell the cache manager the size went to zero
        //  This call is unconditional, because MM always wants to know.
        //

        CcSetFileSizes( FileObject,(PCC_FILE_SIZES)&Fcb->Header.AllocationSize );


        ExReleaseResourceLite( Fcb->Header.Resource );
        ReleasePaging = FALSE;

        //
        //  And set our status to success
        //

        Iosb.Status = STATUS_SUCCESS;

        if (CreateDisposition == FILE_SUPERSEDE) 
        {

            Iosb.Information = FILE_SUPERSEDED;

        } else 
        {

            Iosb.Information = FILE_OVERWRITTEN;
        }

try_exit: NOTHING;
    }
	__finally 
    {
        if (ReleasePaging)  
        {  
            ExReleaseResourceLite( Fcb->Header.Resource );  
        }
    }

    return Iosb;
}



BOOLEAN
PfpCheckFileAccess (					
                    IN UCHAR DirentAttributes,
                    IN PACCESS_MASK DesiredAccess
                    )

                    /*++

                    Routine Description:

                    This routine checks if a desired access is allowed to a file represented
                    by the specified DirentAttriubutes.

                    Arguments:

                    DirentAttributes - Supplies the Dirent attributes to check access for

                    DesiredAccess - Supplies the desired access mask that we are checking for

                    Return Value:

                    BOOLEAN - TRUE if access is allowed and FALSE otherwise

                    --*/

{
    BOOLEAN Result;

    //
    //  This procedures is programmed like a string of filters each
    //  filter checks to see if some access is allowed,  if it is not allowed
    //  the filter return FALSE to the user without further checks otherwise
    //  it moves on to the next filter.  The filter check is to check for
    //  desired access flags that are not allowed for a particular dirent
    //

    Result = TRUE;

    __try {

        //
        //  Check for Volume ID or Device Dirents, these are not allowed user
        //  access at all
        //


        //
        //  Check the desired access for the object - we only blackball that
        //  we do not understand.  The model of filesystems using ACLs is that
        //  they do not type the ACL to the object the ACL is on.  Permissions
        //  are not checked for consistency vs. the object type - dir/file.
        //

        if (FlagOn(*DesiredAccess, ~(DELETE |
            READ_CONTROL |
            WRITE_OWNER |
            WRITE_DAC |
            SYNCHRONIZE |
            ACCESS_SYSTEM_SECURITY |
            FILE_WRITE_DATA |
            FILE_READ_EA |
            FILE_WRITE_EA |
            FILE_READ_ATTRIBUTES |
            FILE_WRITE_ATTRIBUTES |
            FILE_LIST_DIRECTORY |
            FILE_TRAVERSE |
            FILE_DELETE_CHILD |
            FILE_APPEND_DATA))) 
        {

            //DebugTrace(0, Dbg, "Cannot open object\n", 0);

            try_return( Result = FALSE );
        }

        //
        //  Check for a read-only Dirent
        //

        if (FlagOn(DirentAttributes, FAT_DIRENT_ATTR_READ_ONLY)) 
        {

            //
            //  Check the desired access for a read-only dirent, we blackball
            //  WRITE, FILE_APPEND_DATA, FILE_ADD_FILE,
            //  FILE_ADD_SUBDIRECTORY, and FILE_DELETE_CHILD
            //

            if (FlagOn(*DesiredAccess, ~(DELETE |
                READ_CONTROL |
                WRITE_OWNER |
                WRITE_DAC |
                SYNCHRONIZE |
                ACCESS_SYSTEM_SECURITY |
                FILE_READ_DATA |
                FILE_READ_EA |
                FILE_WRITE_EA |
                FILE_READ_ATTRIBUTES |
                FILE_WRITE_ATTRIBUTES |
                FILE_EXECUTE |
                FILE_LIST_DIRECTORY |
                FILE_TRAVERSE))) 
            {

                //DebugTrace(0, Dbg, "Cannot open readonly\n", 0);

                try_return( Result = FALSE );
            }
        }

try_exit: NOTHING;
    }
	__finally 
    {	

        //DebugTrace(-1, Dbg, "FatCheckFileAccess -> %08lx\n", Result);
    }

    return Result;
}

IO_STATUS_BLOCK
PfpCreateNewFile (
                  IN PIRP_CONTEXT		IrpContext,
                  IN PFILE_OBJECT		FileObject,	
                  IN OUT PPfpFCB	*	Fcb,
                  IN PDISKFILEOBJECT*	pDiskFileObject,
                  IN PACCESS_MASK		DesiredAccess,
                  IN USHORT				ShareAccess,
                  IN LARGE_INTEGER				AllocationSize,
                  IN PFILE_FULL_EA_INFORMATION EaBuffer,
                  IN ULONG				EaLength,
                  IN UCHAR				FileAttributes,					 
                  IN BOOLEAN			NoEaKnowledge,
                  IN BOOLEAN			DeleteOnClose,
                  IN BOOLEAN			TemporaryFile,
                  PIO_SECURITY_CONTEXT SecurityContext,
                  ULONG CreateDisposition
                  )

                  /*++

                  Routine Description:

                  This routine creates a new file.  The file has already been verified
                  not to exist yet.

                  Arguments:

                  FileObject - Supplies the file object for the newly created file

                  Vcb - Supplies the Vcb denote the volume to contain the new file

                  ParentDcb - Supplies the parent directory containg the newly created
                  File

                  OemName - Supplies the Oem name for the newly created file.  It may
                  or maynot be 8.3 complient, but will be upcased.

                  UnicodeName - Supplies the Unicode name for the newly created file.
                  It may or maynot be 8.3 complient.  This name contains the original
                  case information.

                  DesiredAccess - Supplies the desired access of the caller

                  ShareAccess - Supplies the shared access of the caller

                  AllocationSize - Supplies the initial allocation size for the file

                  EaBuffer - Supplies the Ea set for the newly created file

                  EaLength - Supplies the length, in bytes, of EaBuffer

                  FileAttributes - Supplies the file attributes for the newly created
                  file

                  LfnBuffer - A MAX_LFN sized buffer for directory searching

                  IsPagingFile - Indicates if this is the paging file being created

                  NoEaKnowledge - This opener doesn't understand Ea's and we fail this
                  open if the file has NeedEa's.

                  DeleteOnClose - The caller wants the file gone when the handle is closed

                  TemporaryFile - Signals the lazywriter to not write dirty data unless
                  absolutely has to.


                  Return Value:

                  IO_STATUS_BLOCK - Returns the completion status for the operation

                  --*/

{
    IO_STATUS_BLOCK Iosb;
    NTSTATUS		ntStatus;
    BOOLEAN			LocalAbnormalTermination;
    PUSERFILEOBJECT pUserFileobject;

    UNREFERENCED_PARAMETER(IrpContext);
    //
    //  We fail this operation if the caller doesn't understand Ea's.
    //

    if (NoEaKnowledge
        && EaLength > 0)
    {

        Iosb.Status = STATUS_ACCESS_DENIED;
        return Iosb;
    }

    //
    //  DeleteOnClose and ReadOnly are not compatible.
    //

    if (DeleteOnClose && FlagOn(FileAttributes, FAT_DIRENT_ATTR_READ_ONLY))
    {
        Iosb.Status = STATUS_CANNOT_DELETE;
        return Iosb;
    }


    if ((CreateDisposition == FILE_OPEN) ||
        (CreateDisposition == FILE_OVERWRITE)) 
    {

        Iosb.Status = STATUS_OBJECT_NAME_NOT_FOUND;				
        return Iosb;

    }
    //
    //  Look in the tunnel cache for names and timestamps to restore
    //


    __try 
    {

        *Fcb = (PPfpFCB)PfpCreateFCB();// Create fcb 


        //
        //  If this is a temporary file, note it in the FcbState
        //

        if (TemporaryFile) 
        {

            //SetFlag( Fcb->FcbState, FCB_STATE_TEMPORARY );
        }

        (*pDiskFileObject)->pFCB = *Fcb;
        (*Fcb)->pDiskFileObject  = *pDiskFileObject;
        ntStatus = PfpCreateRealDiskFile(	*pDiskFileObject,
            &Iosb,
            FILE_CREATE,
            AllocationSize,
            DeleteOnClose,
            EaBuffer,
            EaLength,
            FileAttributes,
            SecurityContext,
            DesiredAccess,
            ShareAccess,
            ACCESSING_FILE_NONEXIST);


        if(!NT_SUCCESS(ntStatus))
            try_return(ntStatus);		

        //
        //  Setup the context and section object pointers, and update
        //  our reference counts
        //


        FileObject->FsContext			 = (PVOID)*Fcb;
        FileObject->FsContext2			 = (PVOID)PfpCreateCCB();
        FileObject->SectionObjectPointer = &(*Fcb)->SegmentObject;

        //
        //  Setup our share access
        //

        IoSetShareAccess( *DesiredAccess,
            ShareAccess,
            FileObject,
            &(*Fcb)->ShareAccess );

        (*Fcb)->UncleanCount += 1;
        (*Fcb)->OpenCount += 1;
        if (FlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING)) 
        {
            (*Fcb)->NonCachedUnCleanupCount += 1;
        }	

        //
        //  And set our return status
        //

        Iosb.Status		 = STATUS_SUCCESS;
        Iosb.Information = FILE_CREATED;

        if ( NT_SUCCESS(Iosb.Status) ) 
        {

            //
            //  Mark the DeleteOnClose bit if the operation was successful.
            //

            if ( DeleteOnClose ) 
            {

                SetFlag( ((PPfpFCB)(*Fcb))->FcbState, FCB_STATE_DELETE_ON_CLOSE);
            }


            pUserFileobject = PfpCreateUserFileObject(	FileObject,
                (*pDiskFileObject)->pDiskFileObjectWriteThrough,
                (*pDiskFileObject)->hFileWriteThrough);
            if( pUserFileobject == NULL )
            {
                Iosb.Status= STATUS_INSUFFICIENT_RESOURCES ;

            }else
            {
                PfpAddUserFileObjectIntoDiskFileObject(*pDiskFileObject,pUserFileobject);
            }

        }

    }
	__finally 
    {

        //
        //  If this is an abnormal termination then undo our work.
        //
        //  The extra exception handling here is so nasty.  We've got
        //  two places here where an exception can be thrown again.
        //

        LocalAbnormalTermination = (AbnormalTermination()?TRUE:FALSE);

        if (LocalAbnormalTermination) 
        {	

            //
            //  Mark the dirents deleted.  The code is complex because of
            //  dealing with an LFN than crosses a page boundry.
            //		

        }

    }

try_exit:
    return Iosb;
}



IO_STATUS_BLOCK 
PfpEncapCreateFile(IN PIRP_CONTEXT				IrpContext,
                   IN PIRP						pIrp,
                   IN FILESTATE					Type,

                   IN BOOLEAN					bFirstOPEN,
                   IN UNICODE_STRING *			pFullPathName,
                   IN OUT PDISKFILEOBJECT *		pDiskFileObject,
                   PBOOLEAN				OplockPostIrp
                   )
{
    PIO_STACK_LOCATION	IrpSp		;
    NTSTATUS			Status		;
    IO_STATUS_BLOCK     ioStatus	;
    PFILE_OBJECT		FileObject	;
    UNICODE_STRING		FileName;
    PFILE_OBJECT		RelatedFileObject;
    PACCESS_MASK		DesiredAccess;
    ULONG				Options;
    UCHAR				FileAttributes;
    USHORT				ShareAccess;
    BOOLEAN				DirectoryFile;
    BOOLEAN				NonDirectoryFile;

    BOOLEAN				NoIntermediateBuffering;
    BOOLEAN				TemporaryFile;
    BOOLEAN				IsPagingFile;
    BOOLEAN				OpenTargetDirectory;
    BOOLEAN				NoEaKnowledge;
    ULONG				CreateDisposition;
    BOOLEAN				DeleteOnClose;
    PPfpFCB				ppFcbCreated ;
    LARGE_INTEGER		AllocationSize;

    PFILE_FULL_EA_INFORMATION EaBuffer;
    ULONG				EaLength;

    PIO_SECURITY_CONTEXT SecurityContext;

    UNREFERENCED_PARAMETER(pFullPathName);

    *OplockPostIrp	= FALSE;
    Status			= STATUS_SUCCESS;
    ppFcbCreated	= NULL;
    ioStatus.Status = STATUS_ACCESS_DENIED;

    ASSERT(pIrp);

    IrpSp = IoGetCurrentIrpStackLocation(pIrp);
    ASSERT(IrpSp->MajorFunction == IRP_MJ_CREATE);

    if ((IrpSp->FileObject->FileName.Length > sizeof(WCHAR)) &&
        (IrpSp->FileObject->FileName.Buffer[1] == L'\\') &&
        (IrpSp->FileObject->FileName.Buffer[0] == L'\\'))
    {

        IrpSp->FileObject->FileName.Length -= sizeof(WCHAR);

        RtlMoveMemory( &IrpSp->FileObject->FileName.Buffer[0],
            &IrpSp->FileObject->FileName.Buffer[1],
            IrpSp->FileObject->FileName.Length );

        //
        //  If there are still two beginning backslashes, the name is bogus.
        //

        if ((IrpSp->FileObject->FileName.Length > sizeof(WCHAR)) &&
            (IrpSp->FileObject->FileName.Buffer[1] == L'\\') &&
            (IrpSp->FileObject->FileName.Buffer[0] == L'\\')) 
        {
            ioStatus.Status= STATUS_OBJECT_NAME_INVALID;
            goto PASSTHROUGH;
        }
    }

    ASSERT( IrpSp->Parameters.Create.SecurityContext != NULL );

    SecurityContext   = IrpSp->Parameters.Create.SecurityContext;
    FileObject        = IrpSp->FileObject;
    FileName          = FileObject->FileName;
    AllocationSize    = pIrp->Overlay.AllocationSize;
    EaBuffer          = pIrp->AssociatedIrp.SystemBuffer;
    RelatedFileObject = FileObject->RelatedFileObject;	
    DesiredAccess     = &IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    Options           = IrpSp->Parameters.Create.Options;
    FileAttributes    = (UCHAR)(IrpSp->Parameters.Create.FileAttributes & ~FILE_ATTRIBUTE_NORMAL);
    ShareAccess       = IrpSp->Parameters.Create.ShareAccess;
    EaLength          = IrpSp->Parameters.Create.EaLength;

    if ( RelatedFileObject != NULL )
    {
        FileObject->Vpb = RelatedFileObject->Vpb;
    }
    else
    {
        FileObject->Vpb= FileObject->DeviceObject->Vpb;
    }

    FileAttributes   &= (FILE_ATTRIBUTE_READONLY |
        FILE_ATTRIBUTE_HIDDEN   |
        FILE_ATTRIBUTE_SYSTEM   |
        FILE_ATTRIBUTE_ARCHIVE );

    //
    //  Locate the volume device object and Vcb that we are trying to access
    //

    //
    //  If this is an open by fileid operation, just fail it explicitly.  FAT's
    //  source of fileids is not reversible for open operations.
    //

    /*if (BooleanFlagOn( Options, FILE_OPEN_BY_FILE_ID ))
    {

    FatCompleteRequest( IrpContext, Irp, STATUS_NOT_IMPLEMENTED );
    return STATUS_NOT_IMPLEMENTED;
    }*/

    DirectoryFile           = BooleanFlagOn( Options, FILE_DIRECTORY_FILE );
    NonDirectoryFile        = BooleanFlagOn( Options, FILE_NON_DIRECTORY_FILE );

    NoIntermediateBuffering = BooleanFlagOn( Options, FILE_NO_INTERMEDIATE_BUFFERING );
    NoEaKnowledge           = BooleanFlagOn( Options, FILE_NO_EA_KNOWLEDGE );
    DeleteOnClose           = BooleanFlagOn( Options, FILE_DELETE_ON_CLOSE );

    TemporaryFile		= BooleanFlagOn( IrpSp->Parameters.Create.FileAttributes,FILE_ATTRIBUTE_TEMPORARY );

    CreateDisposition	= (Options >> 24) & 0x000000FF;

    IsPagingFile		= BooleanFlagOn( IrpSp->Flags, SL_OPEN_PAGING_FILE );

    OpenTargetDirectory = BooleanFlagOn( IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY );

    if(DirectoryFile || IsPagingFile/*|| OpenTargetDirectory*/)
    {
        ioStatus.Status = STATUS_INVALID_PARAMETER;

        goto  PASSTHROUGH;
    }

    if (DeleteOnClose && FlagOn(FileAttributes, FAT_DIRENT_ATTR_READ_ONLY)) 
    {

        ioStatus.Status = STATUS_INVALID_PARAMETER;
        goto  PASSTHROUGH;

    }
    if (pIrp->Overlay.AllocationSize.HighPart != 0 ) 
    {
        ioStatus.Status = STATUS_INVALID_PARAMETER;
        goto  PASSTHROUGH;

    }
    ExAcquireResourceExclusiveLite(&(*pDiskFileObject)->UserObjectResource,TRUE);
    __try
    {
        switch(Type)
        {
        case ACCESSING_FILE_EXIST:
        case ACCESSING_FILE_EXIST_READONLY:

            {
                if(!bFirstOPEN  )
                {
                    ppFcbCreated =(PPfpFCB) (*pDiskFileObject)->pFCB;

                    ioStatus = PfpOpenExistingFcb(	IrpContext,
                        FileObject,
                        &ppFcbCreated,
                        pDiskFileObject,
                        DesiredAccess,
                        ShareAccess,
                        AllocationSize,
                        FileAttributes,
                        CreateDisposition,
                        DeleteOnClose,
                        OplockPostIrp);//passin the fcb , set the create attribut on the fileobject

                    if(!NT_SUCCESS(ioStatus.Status ))
                    {
                        KdPrint(("-------------------------------------PfpOpenExistingFcb failed error code :%Xh-------------------------------------\r\n",ioStatus.Status));
                    }
                }
                else
                { 
                    ioStatus = PfpOpenExistingFile(IrpContext,
                        FileObject,
                        &ppFcbCreated,
                        pDiskFileObject,
                        DesiredAccess,
                        ShareAccess,
                        AllocationSize,
                        FileAttributes,
                        CreateDisposition,
                        NoEaKnowledge,
                        DeleteOnClose,								
                        SecurityContext,
                        Type);//pass out   and open the file,set the create attribut on the fileobject


                    if(NT_SUCCESS(ioStatus.Status) )
                    {
                        if((ioStatus.Information==FILE_OVERWRITTEN||ioStatus.Information==FILE_SUPERSEDED))
                        {
                            FILE_BASIC_INFORMATION* basic= NULL;
                            basic= (FILE_BASIC_INFORMATION*)ExAllocatePoolWithTag(NonPagedPool,sizeof(FILE_BASIC_INFORMATION),'N211');	

                            if( basic && NT_SUCCESS(PfpQueryFileInforByIrp((*pDiskFileObject)->pDiskFileObjectWriteThrough,
                                (PUCHAR)basic,sizeof(FILE_BASIC_INFORMATION),
                                FileBasicInformation,IrpContext->pNextDevice)))

                            {
                                ppFcbCreated->CreationTime=basic->CreationTime.QuadPart;
                                ppFcbCreated->CurrentLastAccess=basic->LastAccessTime.QuadPart;
                                ppFcbCreated->LastChangeTime = basic->ChangeTime.QuadPart;
                                ppFcbCreated->LastModificationTime  = basic->LastWriteTime.QuadPart;
                            }
                            else
                            {
                                ASSERT(0);
                            }
                            ExFreePool_A(basic);
                        }else
                        {
                            KeQuerySystemTime((PLARGE_INTEGER)&ppFcbCreated->CreationTime);

                            ppFcbCreated->CurrentLastAccess=ppFcbCreated->LastChangeTime=ppFcbCreated->LastModificationTime =ppFcbCreated->CreationTime;
                        }
                    }else
                    {
                        KdPrint(("-------------------------------------PfpOpenExistingFile failed error code :%Xh-------------------------------------\r\n",ioStatus.Status));
                    }
                }
            }
            break;

        case ACCESSING_FILE_NONEXIST :

            {
                ioStatus = PfpCreateNewFile(	IrpContext,
                    FileObject,
                    &ppFcbCreated,
                    pDiskFileObject,
                    DesiredAccess,
                    ShareAccess,
                    AllocationSize,
                    EaBuffer,
                    EaLength,
                    FileAttributes,
                    NoEaKnowledge,
                    DeleteOnClose,
                    TemporaryFile,						
                    SecurityContext,
                    CreateDisposition);//pass out  create the file,set the create attribut on the fileobject

                if(NT_SUCCESS(ioStatus.Status))
                {

                    KeQuerySystemTime((PLARGE_INTEGER)&ppFcbCreated->CreationTime);
                    ppFcbCreated->CurrentLastAccess=ppFcbCreated->LastChangeTime=ppFcbCreated->LastModificationTime =ppFcbCreated->CreationTime;

                }else
                {
                    KdPrint(("-------------------------------------PfpCreateNewFile failed error code :%Xh-------------------------------------\r\n",ioStatus.Status));
                }
            }
            break;
        default:
            break;
        }
    }
	__except(EXCEPTION_EXECUTE_HANDLER)
    {
        ioStatus.Status  = STATUS_ACCESS_VIOLATION;
        KdPrint(("-------------------------------------PfpEncapCreateFile exception-------------------------------------\r\n"));
    }

    ExReleaseResourceLite(&(*pDiskFileObject)->UserObjectResource);

PASSTHROUGH:

    if(NT_SUCCESS(ioStatus.Status))
    {
        if(!NoIntermediateBuffering)
        {
            SetFlag(FileObject->Flags,FO_CACHE_SUPPORTED);
        }
        else
        {
            SetFlag( FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING );
        }
        if(TemporaryFile)
        {
            SetFlag( FileObject->Flags, FO_TEMPORARY_FILE );
        }

        if(ShareAccess&FILE_SHARE_READ)
        {
            FileObject->SharedRead = TRUE;
        }
        if(ShareAccess &FILE_SHARE_WRITE)
        {
            FileObject->SharedWrite = TRUE;
        }

        if(ShareAccess &FILE_SHARE_DELETE)
        {
            FileObject->SharedDelete = TRUE;
        }

        if(*DesiredAccess &FILE_GENERIC_READ)
        {
            FileObject->ReadAccess = TRUE;
        }
        if(*DesiredAccess &FILE_GENERIC_WRITE)
        {
            FileObject->WriteAccess = TRUE;
        }
        FileObject->Flags|=FO_HANDLE_CREATED;
    }
    return ioStatus;

}

PPROCESSINFO PfpGetProcessInfoForCurProc()
{
    PPROCESSINFO	ProcessInfo;
    UNICODE_STRING	ProcessName;
    HANDLE			hProcess;
    WCHAR			*FullPathName= NULL;	 
    UCHAR			*pszHashValue= NULL;
    pszHashValue = ExAllocatePoolWithTag(NonPagedPool,PROCESSHASHVALULENGTH,'1102');

    if(pszHashValue== NULL)
    {
        return NULL;
    }


    hProcess = PsGetProcessId(IoGetCurrentProcess() );
    if(PfpFindExcludProcess(hProcess))	
    {
        if(pszHashValue)
        {
            ExFreePool(pszHashValue);
        }
        return NULL;
    }

    RtlInitUnicodeString(&ProcessName,NULL);
    ExAcquireResourceSharedLite(&g_ProcessInfoResource,TRUE);
    ProcessInfo = PfpGetProcessInfoUsingProcessId(hProcess);

    if(!ProcessInfo)
    {
        //1:get Hash value : HashValue
        if(!NT_SUCCESS(GetProcessImageName(ZwCurrentProcess(),&ProcessName )))
        {
            KdPrint(("Failed to get Image Name\r\n"));
            goto PASSTHROUGH;
        } 


        FullPathName = ExAllocatePoolWithTag(PagedPool,ProcessName.Length+sizeof(WCHAR),'2005');
        if(FullPathName == NULL)
        {
            goto PASSTHROUGH;
        }


        RtlCopyMemory(FullPathName,ProcessName.Buffer,ProcessName.Length);
        FullPathName[ProcessName.Length>>1] = L'\0';

        if(!PfpGetHashValueForEXE(FullPathName,ProcessName.Length,pszHashValue ,PROCESSHASHVALULENGTH))
        {	

            ProcessInfo = NULL;
            goto PASSTHROUGH;
        }			

        ProcessInfo = PfpGetProcessInfoUsingHashValue(pszHashValue,PROCESSHASHVALULENGTH,NULL);

        if(ProcessInfo== NULL)
        {
            PfpAddExcludProcess(hProcess);
            goto PASSTHROUGH ;;
        }

        ExAcquireFastMutex(&ProcessInfo->HandleMutex);
        PfpAddHanldeIntoProcessInfo(hProcess,ProcessInfo);
        ExReleaseFastMutex(&ProcessInfo->HandleMutex);	
    }
    ExReleaseResourceLite(&g_ProcessInfoResource);
    if(FullPathName!= NULL)
    {
        ExFreePool(FullPathName);
    }
    if(ProcessName.Buffer)
    {
        RtlFreeUnicodeString (&ProcessName);
    }
    if(pszHashValue)
    {
        ExFreePool(pszHashValue);
    }
    return ProcessInfo;

PASSTHROUGH:
    ExReleaseResourceLite(&g_ProcessInfoResource);
    if(FullPathName!= NULL)
    {
        ExFreePool(FullPathName);
    }
    if(ProcessName.Buffer)
    {
        RtlFreeUnicodeString (&ProcessName);
    }
    if(pszHashValue)
    {
        ExFreePool(pszHashValue);
    }
    return NULL;
}


/*
Currently we don't care  accessing file  by fileid;
*/
BOOLEAN	PfpFileIsNotSelectedInProcess(PFILE_OBJECT pFileObject,PPROCESSINFO pProcInfo) //this function should be called on create .
/*
PFILE_OBJECT pFileObject :passed by create routine, according to user's request creating or opning file.
PPROCESSINFO pProcInfo   :Process Info record in internal list for current process.
*/
{
    WCHAR szExt[50]={0};
    LONG  exLenght;
    exLenght = 0;

    if(pFileObject == NULL||pProcInfo == NULL)
        return FALSE;
    if(pFileObject->RelatedFileObject)
    {
        if(((PPfpFCB)pFileObject->RelatedFileObject->FsContext)->Header.NodeTypeCode == -32768 && pFileObject->FileName.Buffer[0]==L':')
        {
            PVOID pBuffer	  = NULL;
            USHORT  bufferLen   = 0;
            USHORT  bufferLenMax= 0;
            pBuffer = ExAllocatePoolWithTag(PagedPool,
                (bufferLenMax=(pFileObject->FileName.MaximumLength+
                pFileObject->RelatedFileObject->FileName.MaximumLength)),'3005');
            ASSERT(pBuffer );
            bufferLen =(pFileObject->FileName.Length+pFileObject->RelatedFileObject->FileName.Length);

            RtlCopyMemory(pBuffer, pFileObject->RelatedFileObject->FileName.Buffer,pFileObject->RelatedFileObject->FileName.Length);
            RtlCopyMemory(((PUCHAR)pBuffer)+pFileObject->RelatedFileObject->FileName.Length,pFileObject->FileName.Buffer,pFileObject->FileName.Length);

            ExFreePool(pFileObject->FileName.Buffer);
            pFileObject->FileName.Buffer =pBuffer;
            pFileObject->FileName.Length = bufferLen;
            pFileObject->FileName.MaximumLength =  bufferLenMax;
            return TRUE;
        }
    }
    return (PfpGetFileExtFromFileObject(pFileObject,szExt,&exLenght) && PfpFileExtentionExistInProcInfoNotSelete(pProcInfo,szExt));

}


FILESTATE 
PfpGetFileAttriForRequestEx(IN PDEVICE_OBJECT pDeviceObject,
                            IN PWCHAR szFullPathWithOutDevice,
                            IN ULONG	lLenInBytes,
                            IN OUT LONGLONG *		FileSize)
{
    FILESTATE       AccessType  = INVALID_ACCESS;
    WCHAR			szRoot[] =L"\\";
    PWCHAR			pszParentPath = NULL;
    NTSTATUS		ntstatus;
    HANDLE			pParent = INVALID_HANDLE_VALUE;
    BOOLEAN			FileAccessedReadonly = FALSE;
    BOOLEAN			bDir = FALSE;

    LONG nIndex = (lLenInBytes>>1)-1;

    while(nIndex >=0 && szFullPathWithOutDevice[nIndex]!= L'\\')nIndex--;

    if(nIndex <0) return INVALID_ACCESS;

    if(nIndex==0)
    {
        pszParentPath = szRoot;
    }else
    {
        pszParentPath = szFullPathWithOutDevice;
        szFullPathWithOutDevice[nIndex]=L'\0';
    }

    ntstatus = PfpOpenDirByShadowDevice(pszParentPath,&pParent,pDeviceObject);
    if(!NT_SUCCESS(ntstatus)||pParent== INVALID_HANDLE_VALUE)
    {
        AccessType = INVALID_ACCESS;
        goto PASSTHROUGH;
    }

    FileAccessedReadonly = FALSE;
    AccessType = (PfpFileExistInDir(((PFILESPY_DEVICE_EXTENSION)pDeviceObject->DeviceExtension)->NLExtHeader.AttachedToDeviceObject,
        pParent,&szFullPathWithOutDevice[(nIndex+1)],
        &FileAccessedReadonly,
        &bDir  ,
        FileSize)?ACCESSING_FILE_EXIST:ACCESSING_FILE_NONEXIST);
    if(bDir)
    {
        AccessType = ACCESSING_DIR_EXIST;
    }else
    {
        if(AccessType ==ACCESSING_FILE_EXIST &&  FileAccessedReadonly )
        {
            AccessType =ACCESSING_FILE_EXIST_READONLY;
        }
    }
    if(pParent != INVALID_HANDLE_VALUE )
    {
        ZwClose(pParent);
    }
PASSTHROUGH:

    if(pszParentPath == szFullPathWithOutDevice)
    {
        szFullPathWithOutDevice[nIndex]=L'\\';
    }
    return AccessType;

}
FILESTATE PfpGetFileAttriForRequest(PIRP pIrp, PDEVICE_OBJECT pDeviceObject,
                                    IN OUT LONGLONG *		FileSize)
{
    PFILE_OBJECT				pFileObject;
    PFILESPY_DEVICE_EXTENSION   pShadowExt;
    PDEVICE_OBJECT				pNextDriver;
    HANDLE						pParent				= INVALID_HANDLE_VALUE;	
    WCHAR						szExt[50]			={0};

    WCHAR						*szParentPathTemp	=NULL;
    WCHAR*						pszTemp				= NULL;
    LONG						szLength;

    NTSTATUS					ntstatus;
    FILESTATE					AccessType;
    BOOLEAN						FileAccessedReadonly;
    BOOLEAN						bDir = FALSE;
    WCHAR *						szFullPathofParent  = NULL;
    ASSERT(pIrp);
    if(!pIrp)
        return INVALID_ACCESS;

    pFileObject = IoGetCurrentIrpStackLocation(pIrp)->FileObject;


    pNextDriver			= ((PFILESPY_DEVICE_EXTENSION)pDeviceObject->DeviceExtension)->pShadowDevice;
    pShadowExt		    = (PFILESPY_DEVICE_EXTENSION)pNextDriver->DeviceExtension;

    ASSERT(pShadowExt);

    pszTemp = ExAllocatePool_A(  PagedPool,szLength=(pFileObject->FileName.Length+sizeof(WCHAR)));
    if(pszTemp  == NULL)
    {
        AccessType=INVALID_ACCESS;
        goto PASSTHROUGH;
    }
    if(pFileObject->FileName.Buffer[pFileObject->FileName.Length/sizeof(WCHAR)-1]==L'\\')
    {
        AccessType = ACCESSING_DIR_EXIST;
        goto PASSTHROUGH;
    }

    memcpy(pszTemp,pFileObject->FileName.Buffer,pFileObject->FileName.Length);
    pszTemp[pFileObject->FileName.Length/sizeof(WCHAR)]=0;

    if(pFileObject->RelatedFileObject)
    {		
        if(PfpFileObjectHasOurFCB(pFileObject->RelatedFileObject))
        {
            PPfpFCB pFcb = (PPfpFCB )pFileObject->RelatedFileObject->FsContext;
            ntstatus = ObOpenObjectByPointer(pFcb->pDiskFileObject->pDiskFileObjectWriteThrough,
                OBJ_KERNEL_HANDLE ,
                NULL,
                0,
                *IoFileObjectType,
                KernelMode,
                &pParent);
            if(!NT_SUCCESS(ntstatus))
            {
                AccessType = INVALID_ACCESS;
                goto PASSTHROUGH;
            }


        }
        else if(pFileObject->RelatedFileObject->Flags&FO_HANDLE_CREATED )
        {
            LONG   lParentLen		   = 0;
            WCHAR  szDriverLetter   [3]= {0};
            WCHAR*	pszSpe			   = NULL;
            WCHAR*	pTempBuffer			   = NULL;

            if(!NT_SUCCESS(ntstatus=PfpGetFullPathForFileObject(pFileObject->RelatedFileObject,&szFullPathofParent,&lParentLen,((PFILESPY_DEVICE_EXTENSION)pDeviceObject->DeviceExtension)->NLExtHeader.AttachedToDeviceObject)))
            {
                AccessType = INVALID_ACCESS;
                goto PASSTHROUGH;
            }
            //ZwClose(pParent);
            //pParent= INVALID_HANDLE_VALUE;
            pszSpe= wcschr(pszTemp,L'\\');
            if(pszSpe!= NULL)
            {
                pTempBuffer			   = ExAllocatePoolWithTag(PagedPool,lParentLen+sizeof(WCHAR)*wcslen((pszTemp)),'4005');
                if(!pTempBuffer)
                {
                    AccessType = INVALID_ACCESS;
                    goto PASSTHROUGH;
                }
                *pszSpe= 0;
                wcscpy(pTempBuffer,szFullPathofParent);
                wcscat(pTempBuffer,L"\\");
                wcscat(pTempBuffer,pszTemp);
                pszSpe++;
                wcscpy(pszTemp,pszSpe);

                ExFreePool_A(szFullPathofParent);
                szFullPathofParent = pTempBuffer;

            }
            ntstatus = PfpOpenDirByShadowDevice(szFullPathofParent,&pParent,pDeviceObject);
            if(!NT_SUCCESS(ntstatus))
            {
                AccessType = INVALID_ACCESS;
                goto PASSTHROUGH;
            }

        }else
        {
            AccessType = INVALID_ACCESS;
            goto PASSTHROUGH;
        }
    }
    else
    {			
        WCHAR	*szParentPath		=NULL;		
        ULONG   nSize1				= 0;

        if(!PfpGetParentPath(pszTemp,pFileObject->FileName.Length,&szParentPath,&nSize1))		
        {
            AccessType = INVALID_ACCESS;
            goto PASSTHROUGH;

        }
        ntstatus = PfpOpenDirByShadowDevice(szParentPath,&pParent,pDeviceObject);

        ExFreePool_A(szParentPath);
        szParentPath = NULL;

        if(!NT_SUCCESS(ntstatus))
        {
            AccessType = INVALID_ACCESS;
            goto PASSTHROUGH;
        }

        memcpy(pszTemp,pszTemp+(nSize1>>1),szLength-sizeof(WCHAR)*(1+(nSize1>>1)));
        pszTemp[(szLength-sizeof(WCHAR)*(1+(nSize1>>1)))/sizeof(WCHAR)]=0;
    }	

    ASSERT(pParent!= INVALID_HANDLE_VALUE);

    FileAccessedReadonly = FALSE;
    AccessType = (PfpFileExistInDir(((PFILESPY_DEVICE_EXTENSION)pDeviceObject->DeviceExtension)->NLExtHeader.AttachedToDeviceObject,
        pParent,
        pszTemp,
        &FileAccessedReadonly,
        &bDir  ,FileSize)?ACCESSING_FILE_EXIST:ACCESSING_FILE_NONEXIST);
    if(bDir)
    {
        AccessType = ACCESSING_DIR_EXIST;
    }else
    {
        if(AccessType ==ACCESSING_FILE_EXIST &&  FileAccessedReadonly )
        {
            AccessType =ACCESSING_FILE_EXIST_READONLY;
        }
    }
    if(pParent != INVALID_HANDLE_VALUE )
    {
        ZwClose(pParent);
    }

PASSTHROUGH:
    if(szFullPathofParent)
    {
        ExFreePool(szFullPathofParent);
    }
    if(pszTemp)
    {
        ExFreePool(pszTemp);
    }

    return AccessType;
}
//���������ȱ��
BOOLEAN	PfpGetParentPath(WCHAR *pszFilePath,ULONG nPathLenInbytes,WCHAR** pDirPath,LONG* nSize)
{
    LONG nIndex  = ((nPathLenInbytes>>1)-1);
    if(nIndex  <0) 
    {
        return FALSE;
    }

    if(pszFilePath==  NULL ||nPathLenInbytes==0 || pDirPath== NULL||nSize== NULL) return FALSE;

    *pDirPath = NULL;
    *nSize	  = 0;

    if(nIndex  == 0 && pszFilePath[nIndex]==L'\\')
    {
        *pDirPath = ExAllocatePool_A(PagedPool,4);
        *pDirPath [0] = L'\\';
        *pDirPath [1] =L'\0';
        *nSize	= 2;
        return TRUE;
    }

    while(nIndex>=0 && pszFilePath[nIndex]!=L'\\' )
    {
        nIndex--;
    }
    if(nIndex<0)
    {
        return FALSE;
    }

    *nSize = (nIndex+1)*sizeof(WCHAR);

    *pDirPath = ExAllocatePoolWithTag(PagedPool,*nSize+2,'5005');
    if(*pDirPath == NULL) 
    {
        return FALSE;
    }

    memcpy(*pDirPath,pszFilePath,*nSize);

    *pDirPath[nIndex+1]=L'\0';

    return TRUE;

}	


NTSTATUS PfpOpenDirByShadowDevice(
                                  WCHAR * pDirPath,
                                  HANDLE *pHandleReturned,
                                  PDEVICE_OBJECT pDevice)
{
    PDEVICE_OBJECT  pShadowDevice;
    PWCHAR			pDirWithDeviceName;
    PFILESPY_DEVICE_EXTENSION	pExt;
    LONG			lDirPathLen;
    LONG			lShadowNameLen;
    BOOLEAN			bNetWorkDevice;
    PUCHAR 			pTemp;
    UNICODE_STRING	ObjectAttr;
    OBJECT_ATTRIBUTES Obj;
    NTSTATUS		ntStatus;
    IO_STATUS_BLOCK iostatus; 

    pExt = (PFILESPY_DEVICE_EXTENSION)pDevice->DeviceExtension;

    ASSERT(!pExt ->bShadow);
    ASSERT(pDirPath!= NULL &&pDirPath[0]!=0);

    pShadowDevice = pExt ->pShadowDevice;

    ASSERT(pShadowDevice);

    bNetWorkDevice = (pDevice->DeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM);

    lDirPathLen = wcslen(pDirPath)*sizeof(WCHAR);

    pExt = (PFILESPY_DEVICE_EXTENSION)pShadowDevice ->DeviceExtension;

    lShadowNameLen = bNetWorkDevice?pExt ->UserNames.Length:wcslen(pExt->DeviceNames)*sizeof(WCHAR);

    pDirWithDeviceName = ExAllocatePoolWithTag(PagedPool,lShadowNameLen+lDirPathLen+2*sizeof(WCHAR),'6005');

    if(!pDirWithDeviceName)
        return STATUS_INSUFFICIENT_RESOURCES;

    pTemp = (PUCHAR)pDirWithDeviceName;

    memcpy(pTemp ,bNetWorkDevice?pExt->UserNames.Buffer:pExt->DeviceNames,lShadowNameLen);

    pTemp += lShadowNameLen;	

    memcpy(pTemp ,pDirPath,lDirPathLen);

    if(pDirPath[lDirPathLen/sizeof(WCHAR)-1]!=L'\\')
    {
        pDirWithDeviceName[(lDirPathLen+lShadowNameLen)/sizeof(WCHAR)]=L'\\';
        lShadowNameLen+=2;
    }

    pDirWithDeviceName[(lDirPathLen+lShadowNameLen)/sizeof(WCHAR)] = 0;

    ObjectAttr.Buffer = pDirWithDeviceName;
    ObjectAttr.Length = (USHORT)(lDirPathLen+lShadowNameLen);
    ObjectAttr.MaximumLength = ObjectAttr.Length+4;

    InitializeObjectAttributes(&Obj,&ObjectAttr,OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE,NULL,NULL);

    ntStatus = ZwCreateFile(pHandleReturned,
        FILE_LIST_DIRECTORY|FILE_TRAVERSE|SYNCHRONIZE,
        &Obj,
        &iostatus,									
        NULL,
        FILE_ATTRIBUTE_DIRECTORY ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT|FILE_DIRECTORY_FILE,
        NULL,
        0);


    ExFreePool(pDirWithDeviceName);
    pDirWithDeviceName = NULL;

    return ntStatus;

}

NTSTATUS PfpOpenFileByShadowDevice(
                                   WCHAR * pDirPath,
                                   HANDLE *pHandleReturned,
                                   PDEVICE_OBJECT pDevice)
{
    PDEVICE_OBJECT  pShadowDevice;
    PWCHAR			pDirWithDeviceName;
    PFILESPY_DEVICE_EXTENSION	pExt;
    LONG			lDirPathLen;
    LONG			lShadowNameLen;
    BOOLEAN			bNetWorkDevice;
    PUCHAR 			pTemp;
    UNICODE_STRING	ObjectAttr;
    OBJECT_ATTRIBUTES Obj;
    NTSTATUS		ntStatus;
    IO_STATUS_BLOCK iostatus; 

    pExt = (PFILESPY_DEVICE_EXTENSION)pDevice->DeviceExtension;

    ASSERT(!pExt ->bShadow);
    ASSERT(pDirPath!= NULL &&pDirPath[0]!=0);

    pShadowDevice = pExt ->pShadowDevice;

    ASSERT(pShadowDevice);

    bNetWorkDevice = (pDevice->DeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM);

    lDirPathLen = wcslen(pDirPath)*sizeof(WCHAR);

    pExt = (PFILESPY_DEVICE_EXTENSION)pShadowDevice ->DeviceExtension;

    lShadowNameLen = bNetWorkDevice?pExt ->UserNames.Length:wcslen(pExt->DeviceNames)*sizeof(WCHAR);

    pDirWithDeviceName = ExAllocatePoolWithTag(PagedPool,lShadowNameLen+lDirPathLen+2*sizeof(WCHAR),'6005');

    if(!pDirWithDeviceName)
        return STATUS_INSUFFICIENT_RESOURCES;

    pTemp = (PUCHAR)pDirWithDeviceName;

    memcpy(pTemp ,bNetWorkDevice?pExt->UserNames.Buffer:pExt->DeviceNames,lShadowNameLen);

    pTemp += lShadowNameLen;	

    memcpy(pTemp ,pDirPath,lDirPathLen);

    /*if(pDirPath[lDirPathLen/sizeof(WCHAR)-1]!=L'\\')
    {
    pDirWithDeviceName[(lDirPathLen+lShadowNameLen)/sizeof(WCHAR)]=L'\\';
    lShadowNameLen+=2;
    }*/

    pDirWithDeviceName[(lDirPathLen+lShadowNameLen)/sizeof(WCHAR)] = 0;

    ObjectAttr.Buffer = pDirWithDeviceName;
    ObjectAttr.Length = (USHORT)(lDirPathLen+lShadowNameLen);
    ObjectAttr.MaximumLength = ObjectAttr.Length+4;

    InitializeObjectAttributes(&Obj,&ObjectAttr,OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE,NULL,NULL);
    ntStatus  = ZwCreateFile(	pHandleReturned,
        FILE_READ_DATA| SYNCHRONIZE|FILE_WRITE_DATA ,
        &Obj,
        &iostatus,									
        NULL,
        FILE_ATTRIBUTE_NORMAL ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING|FILE_SYNCHRONOUS_IO_NONALERT|FILE_COMPLETE_IF_OPLOCKED,
        NULL,
        0);

    ExFreePool(pDirWithDeviceName);
    pDirWithDeviceName = NULL;

    return ntStatus;

}
BOOLEAN PfpFileExistInDir(PDEVICE_OBJECT pNextDevice,
                          IN  HANDLE		hDir,
                          IN  WCHAR *		pFileName,
                          OUT BOOLEAN *	bReadonly,
                          IN OUT BOOLEAN*	bDir,
                          OUT LONGLONG	*	FileSize)
{
    IO_STATUS_BLOCK   iostatus;
    PVOID			  pBuffer = NULL;
    ULONG			  lBufferLen;
    UNICODE_STRING	  FileName;
    NTSTATUS		  ntstatus;
    ULONG			  nIndex =0;
    PFILE_OBJECT		pDirObject = NULL;
    //KEVENT			  event;
    FILE_DIRECTORY_INFORMATION* FileDirectoryEntry;

    *FileSize = 0;
    ASSERT(bReadonly && pFileName);
    *bDir = FALSE;
    while(*(pFileName+nIndex) == L'\\' )nIndex ++;

    if(*(pFileName+nIndex) == L'\0')
        return FALSE;

    RtlInitUnicodeString(&FileName,(pFileName+nIndex) );

    lBufferLen = (sizeof(FILE_DIRECTORY_INFORMATION)+sizeof(WCHAR)*512+7)&~(ULONG)7;
    FileDirectoryEntry = ExAllocatePoolWithTag(PagedPool,lBufferLen,'7005');

    if(FileDirectoryEntry == NULL)
        return FALSE;

    RtlZeroMemory(FileDirectoryEntry,lBufferLen);
    //	KeInitializeEvent(&event,NotificationEvent,FALSE);

    ntstatus=ObReferenceObjectByHandle(hDir,
        FILE_LIST_DIRECTORY|FILE_TRAVERSE,
        *IoFileObjectType,
        KernelMode,
        &pDirObject,
        NULL);
    if(!NT_SUCCESS(ntstatus))
    {
        if(FileDirectoryEntry)
            ExFreePool(FileDirectoryEntry);
        return FALSE;
    }
    ntstatus =PfpQueryDirectoryByIrp(pNextDevice,pDirObject,FileDirectoryInformation,FileDirectoryEntry,lBufferLen,&FileName,&iostatus);
    // 	ntstatus = ZwQueryDirectoryFile(hDir,
    // 									NULL,
    // 									NULL,
    // 									NULL,
    // 									&iostatus,
    // 									FileDirectoryEntry,
    // 									lBufferLen,
    // 									FileDirectoryInformation,
    // 									TRUE,
    // 									&FileName,
    // 									TRUE);
    // 	if(ntstatus == STATUS_PENDING)
    // 	{
    // 		//KeWaitForSingleObject(&event,Executive,KernelMode,FALSE,NULL);
    // 		ntstatus = iostatus.Status;
    // 	}
    if(!NT_SUCCESS(ntstatus) && STATUS_BUFFER_OVERFLOW!= ntstatus)
    {
        if(FileDirectoryEntry)
            ExFreePool(FileDirectoryEntry);
        ObDereferenceObject(pDirObject);
        return FALSE;
    }
    if(ntstatus== STATUS_BUFFER_OVERFLOW)
    {
        ExFreePool_A(FileDirectoryEntry);
        lBufferLen = (sizeof(FILE_DIRECTORY_INFORMATION)+iostatus.Information+7)&~(ULONG)7;

        FileDirectoryEntry = ExAllocatePoolWithTag(PagedPool,lBufferLen,'8005');

        if(FileDirectoryEntry == NULL)
        {
            ObDereferenceObject(pDirObject);
            return FALSE;
        }

        ntstatus=PfpQueryDirectoryByIrp(	pNextDevice,
            pDirObject,
            FileDirectoryInformation,
            FileDirectoryEntry,
            lBufferLen,
            &FileName,
            &iostatus);
        // 		ntstatus = ZwQueryDirectoryFile(hDir,
        // 			NULL,
        // 			NULL,
        // 			NULL,
        // 			&iostatus,
        // 			FileDirectoryEntry,
        // 			lBufferLen,
        // 			FileDirectoryInformation,
        // 			TRUE,
        // 			&FileName,
        // 			TRUE);
        // 		if(ntstatus == STATUS_PENDING)
        // 		{
        // 			//KeWaitForSingleObject(&event,Executive,KernelMode,FALSE,NULL);
        // 			ntstatus = iostatus.Status;
        // 		}
        if(!NT_SUCCESS(ntstatus) )
        {
            if(FileDirectoryEntry)
                ExFreePool(FileDirectoryEntry);
            ObDereferenceObject(pDirObject);
            return FALSE;
        }
    }

    if((FileDirectoryEntry->FileAttributes&FILE_ATTRIBUTE_DIRECTORY) !=FILE_ATTRIBUTE_DIRECTORY )
    {
        *bReadonly = (FileDirectoryEntry->FileAttributes&FILE_ATTRIBUTE_READONLY)?TRUE:FALSE	;

        *FileSize = FileDirectoryEntry->EndOfFile.QuadPart; 
    }else
    {
        *bDir = TRUE;	 
    }

    if(FileDirectoryEntry)
        ExFreePool(FileDirectoryEntry);
    ObDereferenceObject(pDirObject);
    return (ntstatus== STATUS_NO_MORE_FILES)?FALSE:(NT_SUCCESS(ntstatus)?TRUE:FALSE);
}

NTSTATUS 
PfpGetFullPathForFileObject(IN PFILE_OBJECT hFile,
                            IN OUT WCHAR**	pFullPath,
                            IN OUT LONG *	nLen,
                            IN PDEVICE_OBJECT pNextDevice)
{
    PVOID		pBuffer ;

    NTSTATUS	ntstatus;

    LONG		BufLen =0;

    pBuffer  = ExAllocatePoolWithTag(PagedPool,BufLen=(sizeof(WCHAR)*(MAX_PATH+1)+sizeof( FILE_NAME_INFORMATION)),'9005');		
    if(pBuffer== NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ntstatus = PfpQueryFileInforByIrp(hFile,(PUCHAR)pBuffer,BufLen,FileNameInformation,pNextDevice);

    if(!NT_SUCCESS(ntstatus) && ntstatus!=STATUS_BUFFER_OVERFLOW )
    {
        if(pBuffer)
        {
            ExFreePool(pBuffer);
        }
        return ntstatus;
    }

    if((BufLen-sizeof( FILE_NAME_INFORMATION)) < ((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength )
    {
        ExFreePool(pBuffer);

        BufLen = ((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength+sizeof(FILE_NAME_INFORMATION);
        pBuffer = ExAllocatePoolWithTag(PagedPool,BufLen,'0105');

        ntstatus = PfpQueryFileInforByIrp(hFile,(PUCHAR)pBuffer,BufLen,FileNameInformation,pNextDevice);		

        if(!NT_SUCCESS(ntstatus))
        {
            ExFreePool(pBuffer);			 
            return ntstatus;
        }
    }
    BufLen  = ((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength;
    RtlMoveMemory(pBuffer,((PFILE_NAME_INFORMATION)pBuffer)->FileName,BufLen);
    ((PWCHAR)pBuffer)[BufLen/sizeof(WCHAR)]=0;

    *pFullPath = pBuffer;
    *nLen = BufLen;

    return STATUS_SUCCESS;
}
/*
Thus the file C:\dir1\dir2\filename.ext will appear as \dir1\dir2\filename.ext, 
while the file \\server\share\dir1\dir2\filename.ext will appear as \server\share\dir1\dir2\filename.ext. 

*/
NTSTATUS PfpGetFullPathPreCreate(
                                 PIRP pIrp,
                                 WCHAR** pszFullPathWithOutDeviceName,
                                 ULONG*	szLenReturnedInBytes,
                                 PDEVICE_OBJECT pDevice)
{

    PFILE_OBJECT pFileObject ;
    PFILE_OBJECT pParentObject;
    PDEVICE_OBJECT pNextDevice= NULL;
    PVOID		 pBuffer;
    ULONG		 BufLen;
    ULONG		 FullParentNameLen;
    PIO_STACK_LOCATION  pStack;
    PWCHAR		 pFileName = NULL;
    ULONG		 nNameLen  = 0; //in bytes
    HANDLE		 pParent;
    NTSTATUS	 ntstatus;
    WCHAR		szNameSpace[]    =L"\\??\\";
    WCHAR		szNameSpace1[]   =L"\\DOSDEVICES\\";
    ULONG		szLen = 0;
    PWCHAR		pszWithDiskHeadChar = NULL;
    BOOLEAN		bParentBeDir = TRUE;
    PWCHAR      pDataStreamPoint = NULL;
    PWCHAR		pShortNameLabel = NULL;
    //UNREFERENCED_PARAMETER(pDevice);
    pParent = INVALID_HANDLE_VALUE;

    pBuffer		= NULL;
    pStack		= IoGetCurrentIrpStackLocation(pIrp);
    pFileObject = pStack->FileObject;
    FullParentNameLen = 0;
    pNextDevice = ((PFILESPY_DEVICE_EXTENSION)pDevice->DeviceExtension)->NLExtHeader.AttachedToDeviceObject;
    // no need to redirect to a shadow device;
    pParentObject = pFileObject->RelatedFileObject;

    if(pFileObject ->FileName.Buffer== NULL ||pFileObject->FileName.Length==0)
        return STATUS_INSUFFICIENT_RESOURCES;


    pFileName= pFileObject->FileName.Buffer;
    nNameLen = pFileObject->FileName.Length; 

    if( pParentObject )
    {
        if(PfpFileObjectHasOurFCB(pParentObject))
        {
            FullParentNameLen = ((PPfpFCB)(pParentObject->FsContext))->pDiskFileObject->FullFilePath.Length;
            bParentBeDir = FALSE;
        }else
        {
            FILE_BASIC_INFORMATION *pbasicInfo = NULL;
            pBuffer  = ExAllocatePoolWithTag(PagedPool,BufLen=(sizeof(WCHAR)*(MAX_PATH+1)+sizeof( FILE_NAME_INFORMATION)),'1105');		
            if(pBuffer== NULL)
            {
                return STATUS_INSUFFICIENT_RESOURCES; 
            }

            ntstatus = PfpQueryFileInforByIrp(pParentObject,(PUCHAR)pBuffer,BufLen,FileNameInformation,pNextDevice);

            if(!NT_SUCCESS(ntstatus) && ntstatus!=STATUS_BUFFER_OVERFLOW )
            {
                if(pBuffer)
                {
                    ExFreePool(pBuffer);
                }
                return ntstatus;
            }

            if((BufLen-sizeof( FILE_NAME_INFORMATION)) < ((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength )
            {
                BufLen = ((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength+sizeof(FILE_NAME_INFORMATION);

                ExFreePool(pBuffer);

                pBuffer = ExAllocatePoolWithTag(PagedPool,BufLen,'2105');

                ntstatus = PfpQueryFileInforByIrp(pParentObject,(PUCHAR)pBuffer,BufLen,FileNameInformation,pNextDevice);

                if(!NT_SUCCESS(ntstatus))
                {
                    ExFreePool(pBuffer); 
                    return ntstatus;
                }
            }
            pbasicInfo = (FILE_BASIC_INFORMATION*)ExAllocatePoolWithTag(NonPagedPool,sizeof(FILE_BASIC_INFORMATION),'N311');
            if(pbasicInfo!=NULL)
            {	
                ntstatus = PfpQueryFileInforByIrp(pParentObject,(PUCHAR)pbasicInfo,sizeof(FILE_BASIC_INFORMATION),FileBasicInformation,pNextDevice);
                if(NT_SUCCESS(ntstatus ))
                {
                    bParentBeDir= ((pbasicInfo->FileAttributes&FILE_ATTRIBUTE_DIRECTORY)?TRUE:FALSE);
                }else
                {
                    bParentBeDir = TRUE;
                }
                ExFreePool_A(pbasicInfo);
            }
            FullParentNameLen=((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength;
        }

        pFileName= pFileObject->FileName.Buffer;
        nNameLen = pFileObject->FileName.Length;

    } else
    {
        bParentBeDir = FALSE;
    }

    *pszFullPathWithOutDeviceName = (PWCHAR)ExAllocatePoolWithTag(PagedPool,FullParentNameLen+nNameLen+8,'3105');
    if(*pszFullPathWithOutDeviceName  == NULL)
    {
        if(pBuffer)
        {
            ExFreePool(pBuffer);
        } 
        return STATUS_INSUFFICIENT_RESOURCES;
    }


    szLen = 0;

    if(pBuffer)
    {
        BufLen = ((PFILE_NAME_INFORMATION)pBuffer)->FileNameLength;

        RtlCopyMemory(*pszFullPathWithOutDeviceName,((PFILE_NAME_INFORMATION)pBuffer)->FileName,BufLen);
        szLen = BufLen;
        ExFreePool(pBuffer);


    }else
    {

        if(pParentObject && PfpFileObjectHasOurFCB(pParentObject))
        {
            BufLen =((PPfpFCB)(pParentObject->FsContext))->pDiskFileObject->FullFilePath.Length;
            RtlCopyMemory(*pszFullPathWithOutDeviceName,((PPfpFCB)(pParentObject->FsContext))->pDiskFileObject->FullFilePath.Buffer,BufLen);
            szLen = BufLen;
        }

    }
    if(pFileName!= NULL && nNameLen!=0)
    {	
        if(bParentBeDir && pFileName[0]!=L':' && pFileName[0]!=L'\\')
        {
            if((*pszFullPathWithOutDeviceName)[szLen/sizeof(WCHAR)-1] !=L'\\')
            {
                (*pszFullPathWithOutDeviceName)[szLen/sizeof(WCHAR)]=L'\\';
                szLen +=sizeof(WCHAR);
            }
        }
        RtlCopyMemory(&(*pszFullPathWithOutDeviceName)[szLen/sizeof(WCHAR)],pFileName,nNameLen);
        szLen += nNameLen;
    }
    (*pszFullPathWithOutDeviceName)[szLen/sizeof(WCHAR)]=L'\0';


    if((pDataStreamPoint =wcsstr(*pszFullPathWithOutDeviceName,L"::$DATA")) && pDataStreamPoint[7]==L'\0' )
    {
        szLen =(ULONG)((PUCHAR) pDataStreamPoint-(PUCHAR)*pszFullPathWithOutDeviceName);
        *pDataStreamPoint = L'\0';
    }

    pszWithDiskHeadChar = *pszFullPathWithOutDeviceName;
    if( (pShortNameLabel =wcschr(pszWithDiskHeadChar,L'~'))== NULL) //�õ���·�����治���С� ���ļ����Ĵ�����ĸ��ֱ�ӷ���
    {
        *szLenReturnedInBytes = szLen;	 
        return STATUS_SUCCESS;
    }else if(pShortNameLabel != pszWithDiskHeadChar)
    {
        pShortNameLabel--;
        if(*pShortNameLabel== L'\\')
        {
            *szLenReturnedInBytes = szLen;	 
            return STATUS_SUCCESS;
        }
    }

    {
        PWCHAR pszPre = NULL;

        if((pszPre  =wcsstr(*pszFullPathWithOutDeviceName,szNameSpace))!= NULL)
        {
            pszPre+=wcslen(szNameSpace);
            if(*pszPre!=0)
            {
                while(*pszPre != 0 && *pszPre!= L'\\')pszPre++;
                if(*pszPre != 0)
                    pszWithDiskHeadChar  = pszPre ;
            }
        }else if((pszPre  =wcsstr(*pszFullPathWithOutDeviceName,szNameSpace1))!= NULL)
        {
            pszPre+=wcslen(szNameSpace1);
            if(*pszPre!=0)
            {
                while(*pszPre != 0 && *pszPre!= L'\\')pszPre++;
                if(*pszPre != 0)
                    pszWithDiskHeadChar  = pszPre ;
            }
        }
    }
    {
        NTSTATUS nstatus1;
        HANDLE hDir   = INVALID_HANDLE_VALUE;

        PWCHAR szSeprateor = NULL;
        PWCHAR pszDir = ExAllocatePoolWithTag(PagedPool,1024*sizeof(WCHAR),'4105');
        PFILE_OBJECT pDirObject = NULL;
        IO_STATUS_BLOCK  ioquery;
        PVOID pQueryDir =  NULL;
        UNICODE_STRING szDirtoryName;
        NTSTATUS ntStatus1 ;
        PWCHAR pHead = pszWithDiskHeadChar;

        if(pszDir == NULL)
        {
            ExFreePool(*pszFullPathWithOutDeviceName);
            *pszFullPathWithOutDeviceName = NULL;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        pQueryDir = ExAllocatePoolWithTag(PagedPool,(1024+sizeof(FILE_BOTH_DIR_INFORMATION))*sizeof(WCHAR),'5106');

        pszDir[0]=L'\\';pszDir[1]=0;
        while(*pHead!=0 && *pHead==L'\\')pHead++;

        while( *pHead != 0 &&(szSeprateor =wcschr(pHead,L'\\'))!= NULL && szSeprateor[1]!=0  )
        {

            *szSeprateor = 0;
            if(wcslen(pHead)==8 && wcschr(pHead,L'~')!= NULL)
            {
                nstatus1=PfpOpenDirByShadowDevice(pszDir,&hDir,pDevice);
                if(hDir== INVALID_HANDLE_VALUE)
                {
                    if(pszDir[wcslen(pszDir)-1]!=L'\\')
                    {
                        wcscat(pszDir,L"\\");
                    }
                    wcscat(pszDir,pHead);
                    *pHead = 0;

                    break;
                }
                RtlInitUnicodeString(&szDirtoryName,pHead);
                ntStatus1=ObReferenceObjectByHandle(hDir,
                    FILE_LIST_DIRECTORY|FILE_TRAVERSE,
                    *IoFileObjectType,
                    KernelMode,
                    &pDirObject,
                    NULL);
                if(NT_SUCCESS(ntStatus1))
                {
                    ntStatus1=  PfpQueryDirectoryByIrp(((PFILESPY_DEVICE_EXTENSION)pDevice->DeviceExtension)->NLExtHeader.AttachedToDeviceObject,
                        pDirObject,
                        FileBothDirectoryInformation,
                        pQueryDir,
                        (1024+sizeof(FILE_BOTH_DIR_INFORMATION))*sizeof(WCHAR),
                        &szDirtoryName,
                        &ioquery);
                    ObDereferenceObject(pDirObject);
                    pDirObject = NULL;
                }


                // 				ntStatus1  =ZwQueryDirectoryFile(hDir,NULL,NULL,NULL,&ioquery,pQueryDir,(1024+sizeof(FILE_BOTH_DIR_INFORMATION))*sizeof(WCHAR),
                // 					FileBothDirectoryInformation,
                // 					TRUE,&szDirtoryName,TRUE);
                ZwClose(hDir);

                hDir= INVALID_HANDLE_VALUE;
                if(pszDir[wcslen(pszDir)-1]!=L'\\')
                {
                    wcscat(pszDir,L"\\");
                }
                if(NT_SUCCESS(ntStatus1))
                {
                    wcsncat(pszDir,((PFILE_BOTH_DIR_INFORMATION) pQueryDir)->FileName,((PFILE_BOTH_DIR_INFORMATION) pQueryDir)->FileNameLength/sizeof(WCHAR));
                    pHead = szSeprateor;
                    pHead++;
                }else
                {
                    *szSeprateor = L'\\';	
                    wcscat(pszDir,pHead);
                    *pHead = 0;
                    break;
                }

            }else
            {
                if(pszDir[wcslen(pszDir)-1]!=L'\\')
                {
                    wcscat(pszDir,L"\\");
                }
                wcscat(pszDir,pHead);
                pHead = szSeprateor;
                pHead++;

            }


        };
        if(*pHead != 0)
        {
            if(szSeprateor!= NULL)*szSeprateor = 0;

            if(wcslen(pHead)==8 && wcschr(pHead,L'~')!= NULL)
            {

                nstatus1=PfpOpenDirByShadowDevice(pszDir,&hDir,pDevice);

                if(hDir== INVALID_HANDLE_VALUE)
                {
                    if(pszDir[wcslen(pszDir)-1]!=L'\\')
                    {
                        wcscat(pszDir,L"\\");
                    }
                    wcscat(pszDir,pHead);				 
                }
                else
                {

                    RtlInitUnicodeString(&szDirtoryName,pHead);
                    ntStatus1=ObReferenceObjectByHandle(hDir,
                        FILE_LIST_DIRECTORY|FILE_TRAVERSE,
                        *IoFileObjectType,
                        KernelMode,
                        &pDirObject,
                        NULL);
                    if(NT_SUCCESS(ntStatus1))
                    {
                        ntStatus1=  PfpQueryDirectoryByIrp(((PFILESPY_DEVICE_EXTENSION)pDevice->DeviceExtension)->NLExtHeader.AttachedToDeviceObject,
                            pDirObject,
                            FileBothDirectoryInformation,
                            pQueryDir,
                            (1024+sizeof(FILE_BOTH_DIR_INFORMATION))*sizeof(WCHAR),
                            &szDirtoryName,
                            &ioquery);
                        ObDereferenceObject(pDirObject);
                        pDirObject = NULL;
                    }
                    // 					ntStatus1  =ZwQueryDirectoryFile(hDir,NULL,NULL,NULL,&ioquery,pQueryDir,(1024+sizeof(FILE_BOTH_DIR_INFORMATION))*sizeof(WCHAR),
                    // 						FileBothDirectoryInformation,
                    // 						TRUE,&szDirtoryName,TRUE);
                    ZwClose(hDir);				 

                    if(pszDir[wcslen(pszDir)-1]!=L'\\')
                    {
                        wcscat(pszDir,L"\\");
                    }

                    if(NT_SUCCESS(ntStatus1))
                    {
                        wcsncat(pszDir,((PFILE_BOTH_DIR_INFORMATION) pQueryDir)->FileName,((PFILE_BOTH_DIR_INFORMATION) pQueryDir)->FileNameLength/sizeof(WCHAR));

                    }else
                    {
                        wcscat(pszDir,pHead);	
                    }

                }

            }else
            {
                if(pszDir[wcslen(pszDir)-1]!=L'\\')
                {
                    wcscat(pszDir,L"\\");
                } 
                wcscat(pszDir,pHead); 
            }
            if(szSeprateor)
            {
                wcscat(pszDir,L"\\");
            }
        } 

        if(pQueryDir)
        {
            ExFreePool_A(pQueryDir);
        }
        ExFreePool_A(*pszFullPathWithOutDeviceName);
        *pszFullPathWithOutDeviceName = pszDir;
        *szLenReturnedInBytes = (wcslen(pszDir)<<1);	 

    }
    return STATUS_SUCCESS;
}


BOOLEAN 
PfpIsStreamPath(WCHAR*pszFullPathWithOutDeviceName,
                IN ULONG szLenInBytes)
{
    LONG nIndex =(szLenInBytes>>1)-1;
    BOOLEAN  bFoundSteamLable = FALSE;
    ASSERT(pszFullPathWithOutDeviceName!= NULL && szLenInBytes!= 0);

    while(nIndex >=0 && pszFullPathWithOutDeviceName[nIndex]!= L'\\') 
    {
        if(pszFullPathWithOutDeviceName[nIndex]==L':') bFoundSteamLable= TRUE;
        nIndex--;
    };
    return (nIndex>=0 && bFoundSteamLable);
}

NTSTATUS 
PfpDoBackUpWorkAboutCreate(PDISKFILEOBJECT pDiskFileObject,PDEVICE_OBJECT pDevice,PPROCESSINFO pProcessInfo,PWCHAR FullPathName)
{
    PBackUpInfo		pBackUpInfo = NULL;
    PWCHAR			pszExeName  = NULL;
    PWCHAR			szFullFilePathWithDeviceName = NULL;

    WCHAR			szDeviceName[20]={0};
    LONG				nIndex = 0;
    LONG				nIndex1 =0;
    PDEVICE_OBJECT		pSpyDevice = NULL;
    ULONG				nLength = 0;

    WCHAR			*szBackupFullPath = NULL;
    WCHAR			*szBackupDirOnShadowDevice= NULL;
    NTSTATUS		ntstatus;

    pDiskFileObject->hBackUpFileHandle = INVALID_HANDLE_VALUE;
    pDiskFileObject->hBackUpFileObject = NULL;
    if(!g_szBackupDir)
    {
        return STATUS_SUCCESS;
    }
    while(g_szBackupDir[nIndex]==L'\\' && g_szBackupDir[nIndex]!= L'\0' )nIndex++;
    if(g_szBackupDir[nIndex]== L'\0' ) return STATUS_INVALID_PARAMETER;

    while(g_szBackupDir[nIndex]!=L'\\'&& g_szBackupDir[nIndex]!= L'\0')
    {
        szDeviceName[nIndex1] =g_szBackupDir[nIndex];
        nIndex1++;
        nIndex++;
    };
    if( g_szBackupDir[nIndex]== L'\0')
        return STATUS_BUFFER_OVERFLOW;

    pSpyDevice = PfpGetSpyDeviceFromName(szDeviceName);

    if(pSpyDevice  == NULL)
        return STATUS_BUFFER_OVERFLOW;;

    nIndex++;//Խ�� //�������

    if(!PfpGenerateFullFilePathWithShadowDeviceName(pSpyDevice,&g_szBackupDir[nIndex],&szBackupDirOnShadowDevice))
    {
        ASSERT(FALSE);
    };

    nIndex = wcslen(FullPathName)-1;
    if(nIndex <=0) return STATUS_BUFFER_OVERFLOW;



    if(!PfpGenerateFullFilePathWithShadowDeviceName(pDevice,FullPathName,&szFullFilePathWithDeviceName))
    {
        ASSERT(FALSE);
    };

    while( nIndex>=0 && FullPathName[nIndex]!=L'\\')
    {	
        nIndex--;
    };
    nLength  = wcslen (FullPathName)*sizeof(WCHAR);
    if(nIndex >=0 )
    {
        nIndex++;	
        nLength -= nIndex*sizeof(WCHAR); 
    }

    nLength += (wcslen(szBackupDirOnShadowDevice)+2)*sizeof(WCHAR);
    szBackupFullPath = ExAllocatePoolWithTag(PagedPool,nLength,'6105');
    if(szBackupFullPath== NULL)
    {
        if(szBackupDirOnShadowDevice)
            ExFreePool(szBackupDirOnShadowDevice);
        if(szFullFilePathWithDeviceName)
            ExFreePool(szFullFilePathWithDeviceName);

        return STATUS_INSUFFICIENT_RESOURCES;
    }
    wcscpy(szBackupFullPath,szBackupDirOnShadowDevice);

    if(szBackupFullPath[wcslen(szBackupFullPath)-1]!= L'\\')
    {
        wcscat(szBackupFullPath,L"\\");
    }

    if(nIndex<0 )
    {			
        wcscat(szBackupFullPath,FullPathName);
    }
    else
    {			
        wcscat(szBackupFullPath,&FullPathName[nIndex]);
    }



    ntstatus = PfpCreateBackUpFile_Real(&pDiskFileObject->hBackUpFileObject,&pDiskFileObject->hBackUpFileHandle,szBackupFullPath);

    if(NT_SUCCESS(ntstatus) && ntstatus==FILE_CREATED)//��������ļ����´�����
    {
        if(pDiskFileObject->hFileWriteThrough!= INVALID_HANDLE_VALUE && pDiskFileObject->hFileWriteThrough!= NULL)		
        {
            PfpCopyFile(pDiskFileObject->hBackUpFileHandle,pDiskFileObject->hFileWriteThrough);
        }else
        {	
            HANDLE hFileOrignal= INVALID_HANDLE_VALUE;
            IO_STATUS_BLOCK    iostatus;
            ntstatus = PfpOpenOriganlFileForBackup(szFullFilePathWithDeviceName,&hFileOrignal,&iostatus);
            if(iostatus.Information == FILE_OPENED)
            {
                PfpCopyFile(pDiskFileObject->hBackUpFileHandle,hFileOrignal);
            }
            if(hFileOrignal!= INVALID_HANDLE_VALUE)ZwClose(hFileOrignal);
        }
    }

    if(szBackupFullPath)
    {
        ExFreePool(szBackupFullPath);

    }
    if(szBackupDirOnShadowDevice)
    {
        ExFreePool(szBackupDirOnShadowDevice);
    }
    if(szFullFilePathWithDeviceName)
    {
        ExFreePool(szFullFilePathWithDeviceName);
    }
    return STATUS_SUCCESS;
}



NTSTATUS PfpCreateRealDiskFile(PDISKFILEOBJECT	pDiskFileObject,					  
                               IO_STATUS_BLOCK* iostatus,
                               ULONG			CreateDisposition,
                               LARGE_INTEGER	AllocationSize,//when operatype!=OPEN_FILE_EXIST, this parameter is valid 
                               BOOLEAN			DeleteOnClose,
                               PVOID			EaBuffer,
                               ULONG			EALength,
                               ULONG			FileAttributes,
                               PIO_SECURITY_CONTEXT SecurityContext,
                               PACCESS_MASK		DesiredAccess,
                               IN USHORT		ShareAccess,
                               FILESTATE			AcsType)
{
    NTSTATUS					ntstatus ;
    OBJECT_ATTRIBUTES			Objattri;
    PFILESPY_DEVICE_EXTENSION	pExt			= (PFILESPY_DEVICE_EXTENSION )pDiskFileObject->pOurSpyDevice->DeviceExtension;
    PFILESPY_DEVICE_EXTENSION	pShadowExt		= NULL;
    PDEVICE_OBJECT				pShadowDevice	= NULL;
    WCHAR						*szFullpath		= NULL;
    WCHAR*						szCurPos		= NULL;

    PFILE_OBJECT				pUserFileobeject= NULL;
    PCREATECONTEXT				pCreateContext = NULL;

    UNICODE_STRING				ObjectAttri_U;
    LARGE_INTEGER				AllocationFile;	
    ULONG						CreateOptions;
    PVOID						EncryptHead;
    IO_STATUS_BLOCK				iostatusread;
    HANDLE						hCreatethread = INVALID_HANDLE_VALUE;
    //OBJECT_ATTRIBUTES			ObjectAttributes;
    UNREFERENCED_PARAMETER(SecurityContext);
    UNREFERENCED_PARAMETER(ShareAccess);


    EncryptHead =NULL;


    RtlZeroMemory(&AllocationFile,sizeof(AllocationFile));	

    ASSERT(!pExt->bShadow) ;

    pShadowDevice  = pExt->pShadowDevice;

    ASSERT(pShadowDevice);

    pShadowExt  = (PFILESPY_DEVICE_EXTENSION)pShadowDevice->DeviceExtension;

    szFullpath = ExAllocatePoolWithTag(PagedPool,(wcslen(pShadowExt->DeviceNames)+(pDiskFileObject->FullFilePath.Length>>1)+1)<<1,'7105');

    if(szFullpath== NULL )
    {
        //ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    szCurPos  = szFullpath;	
    wcscpy(szCurPos,pShadowExt->DeviceNames);
    szCurPos=szCurPos+wcslen(pShadowExt->DeviceNames);


    RtlCopyMemory(szCurPos ,pDiskFileObject->FullFilePath.Buffer,pDiskFileObject->FullFilePath.Length);
    szCurPos [pDiskFileObject->FullFilePath.Length/sizeof(WCHAR)] =0;

    RtlInitUnicodeString(&ObjectAttri_U,szFullpath);

    InitializeObjectAttributes(&Objattri,
        &ObjectAttri_U,
        OBJ_CASE_INSENSITIVE |OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );
    if(AllocationSize.QuadPart != 0)
    {
        AllocationFile.QuadPart = AllocationSize.QuadPart +ENCRYPTIONHEADLENGTH; 
    }

    CreateOptions = FILE_WRITE_THROUGH|FILE_RANDOM_ACCESS|(DeleteOnClose?FILE_DELETE_ON_CLOSE:0);

    ntstatus  = ZwCreateFile(	&pDiskFileObject->hFileWriteThrough,
        ((AcsType== ACCESSING_FILE_EXIST_READONLY)?FILE_GENERIC_READ:(FILE_GENERIC_READ|FILE_GENERIC_WRITE))  ,//*DesiredAccess| ((AcsType== ACCESSING_FILE_EXIST_READONLY)?0:FILE_WRITE_DATA)| SYNCHRONIZE  ,
        &Objattri,
        iostatus,									
        &AllocationFile,
        FileAttributes ,
        FILE_SHARE_READ,//|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        CreateDisposition,
        CreateOptions|FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING|FILE_SYNCHRONOUS_IO_NONALERT|FILE_COMPLETE_IF_OPLOCKED,
        EaBuffer ,
        EALength);

    /*pCreateContext = ExAllocateFromNPagedLookasideList(&PfpCreateContextLookasideList);
    if(pCreateContext == NULL)
    {
    ntstatus = STATUS_INSUFFICIENT_RESOURCES;
    goto EXIT;		
    }


    pCreateContext->EaBuffer = NULL;
    pCreateContext->EaLength  = 0;;
    if(EaBuffer&& EALength!= 0)
    {
    pCreateContext->EaBuffer = ExAllocatePool_A(NonPagedPool,EALength);
    if(pCreateContext->EaBuffer )
    {
    memcpy(pCreateContext->EaBuffer ,EaBuffer,EALength);
    pCreateContext->EaLength  = EALength;
    }
    }
    pCreateContext->phFileCreated = INVALID_HANDLE_VALUE;
    pCreateContext->DesiredAccess = ((AcsType== ACCESSING_FILE_EXIST_READONLY)?FILE_GENERIC_READ:(FILE_GENERIC_READ|FILE_GENERIC_WRITE))  ;
    pCreateContext->pFilePath = szFullpath;
    pCreateContext->AllocationSize.QuadPart =AllocationFile.QuadPart;
    pCreateContext->FileAttributes = FileAttributes;
    pCreateContext->ShareAccess   = FILE_SHARE_READ;//|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
    pCreateContext->CreateDisposition = CreateDisposition;
    pCreateContext->CreateOptions = CreateOptions|FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING|FILE_SYNCHRONOUS_IO_NONALERT;
    KeInitializeEvent(&pCreateContext->hEvent,NotificationEvent, FALSE);
    pCreateContext->ntstatus = STATUS_SUCCESS;

    InsertCreateContextIntoLists(pCreateContext);

    KeWaitForSingleObject(&pCreateContext->hEvent,Executive,KernelMode,FALSE,(PLARGE_INTEGER)NULL)	;


    ntstatus= pCreateContext->ntstatus;
    iostatus->Information = pCreateContext->IoStatusBlock.Information;
    iostatus->Status= pCreateContext->IoStatusBlock.Status;
    pDiskFileObject->hFileWriteThrough= pCreateContext->phFileCreated;

    if(pCreateContext->EaBuffer != NULL)
    {
    ExFreePool_A(pCreateContext->EaBuffer);
    }
    ExFreeToNPagedLookasideList(&PfpCreateContextLookasideList,pCreateContext);
    */
    if(!NT_SUCCESS(ntstatus))
    {
        goto EXIT;
    }

    ntstatus = ObReferenceObjectByHandle(pDiskFileObject->hFileWriteThrough,
        0,
        *IoFileObjectType,
        KernelMode,
        &pDiskFileObject->pDiskFileObjectWriteThrough,
        NULL);
    if(!NT_SUCCESS(ntstatus))
    {	
        goto EXIT;
    }



    if(iostatus->Information == FILE_OPENED)
    {
        //1:get the filesize
        //if >512 the 
        LARGE_INTEGER	ByteOffset	= {0};
        ULONG			Length		= ENCRYPTIONHEADLENGTH;			
        NTSTATUS		status;

        EncryptHead = ExAllocatePoolWithTag(NonPagedPool ,ENCRYPTIONHEADLENGTH,'N101');
        if(EncryptHead == NULL)
        {
            ntstatus = STATUS_INSUFFICIENT_RESOURCES;
            goto EXIT;
        }

        status = PfpReadHeadForEncryption(	EncryptHead,
            Length,
            pDiskFileObject->pDiskFileObjectWriteThrough,
            pExt->NLExtHeader.AttachedToDeviceObject,
            &iostatusread
            );

        if(!NT_SUCCESS(status)|| iostatusread.Information != ENCRYPTIONHEADLENGTH )
        {
            pDiskFileObject->bFileNOTEncypted = TRUE;	

            if( !PfpInitFCBFromFileOnDISK(	pDiskFileObject->pDiskFileObjectWriteThrough,
                pDiskFileObject->pFCB,
                FALSE,
                pExt->NLExtHeader.AttachedToDeviceObject) )
            {
                ntstatus = STATUS_ACCESS_DENIED;
                goto EXIT;
            }
        }else
        {
            pDiskFileObject->bFileNOTEncypted = !PfpCheckEncryptInfo(EncryptHead,ENCRYPTIONHEADLENGTH);
            if(!pDiskFileObject->bFileNOTEncypted)
            {
                if(!PfpInitFCBFromEncryptBuffer(EncryptHead,ENCRYPTIONHEADLENGTH,pDiskFileObject->pFCB))
                {
                    ntstatus = STATUS_FILE_CORRUPT_ERROR;
                    goto EXIT;
                }else
                {
                    ((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt = TRUE;//�մ򿪵��ļ�����ⷢ�������ģ�����fcbΪҪ��Ҫ���ܡ�
                }
            }else
            {
                if( !PfpInitFCBFromFileOnDISK(	pDiskFileObject->pDiskFileObjectWriteThrough,
                    pDiskFileObject->pFCB,
                    FALSE,
                    pExt->NLExtHeader.AttachedToDeviceObject) )
                {
                    ntstatus = STATUS_ACCESS_DENIED;
                    goto EXIT;
                }
            } 
        }

    }else 
    {
        pDiskFileObject->bFileNOTEncypted		= FALSE;//��ʼ��Ϊ���ܵģ�����д�ļ���ʱ�򣬾Ͳ����ǲ����´������ļ���.
        ((PPfpFCB)(pDiskFileObject->pFCB))->Header.ValidDataLength.QuadPart = ((PPfpFCB)(pDiskFileObject->pFCB))->Header.FileSize.QuadPart =0;
        ((PPfpFCB)(pDiskFileObject->pFCB))->Header.AllocationSize.QuadPart  =0;
        ((PPfpFCB)(pDiskFileObject->pFCB))->bNeedEncrypt = TRUE;//�մ������ļ���������������fcbΪҪ��Ҫ���ܡ�

        if((iostatus->Information== FILE_OVERWRITTEN||iostatus->Information== FILE_CREATED) && (AllocationFile.QuadPart!=0))		
        {
            ASSERT(AllocationFile.QuadPart>ENCRYPTIONHEADLENGTH);
            ((PPfpFCB)(pDiskFileObject->pFCB))->Header.AllocationSize.QuadPart = (AllocationFile.QuadPart-ENCRYPTIONHEADLENGTH);
        }



        // 		
        // 		if( !PfpInitFCBFromFileOnDISK(	pDiskFileObject->pDiskFileObjectWriteThrough,
        // 										pDiskFileObject->pFCB,
        // 										TRUE,
        // 										pExt->NLExtHeader.AttachedToDeviceObject) )
        // 		{
        // 			ntstatus = STATUS_ACCESS_DENIED;
        // 			goto EXIT;
        // 		}


    }

EXIT:
    if(szFullpath)
    {
        ExFreePool(szFullpath);
    }
    if(EncryptHead)
    {
        ExFreePool(EncryptHead);
    }
    EncryptHead= NULL;
    if(!NT_SUCCESS(ntstatus))
    {
        iostatus->Status  = ntstatus;
        if(pDiskFileObject->pDiskFileObjectWriteThrough)
        {
            ObDereferenceObject(pDiskFileObject->pDiskFileObjectWriteThrough);		
            pDiskFileObject->pDiskFileObjectWriteThrough= NULL;

        }	
        if(pDiskFileObject->hFileWriteThrough!= INVALID_HANDLE_VALUE)
        {
            ZwClose(pDiskFileObject->hFileWriteThrough);
            pDiskFileObject->hFileWriteThrough = INVALID_HANDLE_VALUE;
        }
    }

    return ntstatus;
}



BOOLEAN			
PfpInitFCBFromEncryptBuffer(
                            IN PVOID Buffer ,
                            IN ULONG Len,
                            IN PPfpFCB pFcb)
{
    ASSERT(Buffer);
    ASSERT(Len!=0);
    ASSERT(pFcb != NULL);	
    UNREFERENCED_PARAMETER(Len);
    (PUCHAR)Buffer+=sizeof(LONGLONG);
    pFcb->Header.FileSize.QuadPart = *(LONGLONG*)Buffer;
    (PUCHAR)Buffer += sizeof(LONGLONG);
    pFcb->Header.ValidDataLength.QuadPart = *(LONGLONG*)Buffer;
    (PUCHAR)Buffer += sizeof(LONGLONG);
    pFcb->Header.AllocationSize.QuadPart  = *(LONGLONG*)Buffer;
    pFcb->bWriteHead = TRUE;
    return TRUE;
}

BOOLEAN			
PfpInitFCBFromFileOnDISK(
                         IN PFILE_OBJECT	hFileObject	,
                         IN PPfpFCB			pFcb,
                         BOOLEAN			bNewCreated,
                         PDEVICE_OBJECT		pNextDevice)
{	
    FILE_STANDARD_INFORMATION* StardInfo= NULL;
    NTSTATUS ntstatus;

    ULONG	 Length;

    ASSERT(pFcb);

    StardInfo = (FILE_STANDARD_INFORMATION*)ExAllocatePoolWithTag(PagedPool,sizeof(FILE_STANDARD_INFORMATION),'8105');
    if(StardInfo ==  NULL)
        return FALSE;

    RtlZeroMemory(StardInfo,sizeof(StardInfo));
    Length = sizeof(FILE_STANDARD_INFORMATION);	

    ntstatus = PfpQueryFileInforByIrp(hFileObject,(PUCHAR)StardInfo,Length,FileStandardInformation,pNextDevice);	 

    if(NT_SUCCESS(ntstatus ))
    {
        //�����´������ļ�����ôҪ��ȥ ����ͷ�ĳ��ȣ�����allocationsize��
        //����filesize ���� ���´������ļ�������£����ʱ���ļ��Ĵ�С��0
        //���ڲ����´������ļ�������Ҳ���Ǽ��ܵ��ļ�����ô���ǵ�fcb�����FileSize����ԭ���ļ���FileSize

        if(bNewCreated)
        {
            if(StardInfo->AllocationSize.QuadPart!=0)
            {
                pFcb->Header.AllocationSize.QuadPart  = (StardInfo->AllocationSize.QuadPart-ENCRYPTIONHEADLENGTH);
            }else
            {
                pFcb->Header.AllocationSize.QuadPart  = 0;
            }

        }else
        {
            pFcb->Header.AllocationSize.QuadPart  = StardInfo->AllocationSize.QuadPart;
        }

        pFcb->Header.ValidDataLength = pFcb->Header.FileSize	= StardInfo->EndOfFile;
        if(pFcb->Header.FileSize.QuadPart ==0)
        {
            pFcb->bWriteHead   = FALSE;
            pFcb->bNeedEncrypt = TRUE;
        }

    }
    if(StardInfo)
    {
        ExFreePool_A(StardInfo);
    }
    return NT_SUCCESS(ntstatus );
}


BOOLEAN	
PfpFileObjectHasOurFCB(
                       IN PFILE_OBJECT pFileObject
                       )
{

    BOOLEAN b = FALSE; 

    b = ( pFileObject && (pFileObject->FsContext) && ((PPfpFCB)pFileObject->FsContext)->Header.NodeTypeCode ==-32768);

    return b;

}



NTSTATUS 
PfpReadHeadForEncryption(
                         PVOID pEncryptHead,
                         ULONG Len,
                         IN PFILE_OBJECT pDiskFile,
                         PDEVICE_OBJECT  pNextDevice,
                         PIO_STATUS_BLOCK pIostatus
                         )
{
    LARGE_INTEGER	Offset;	
    NTSTATUS	status;
    Offset.QuadPart =0;
    ////VirtualizerStart();
    status=PfpReadFileByAllocatedIrp(pEncryptHead,Len,Offset,pDiskFile,pNextDevice,pIostatus);
    ////VirtualizerEnd();
    return status;

}

NTSTATUS 
PfpReadFileByAllocatedIrp(
                          PVOID				pBuffer,
                          ULONG				Len,
                          LARGE_INTEGER		Offset,
                          IN PFILE_OBJECT	pDiskFile,
                          PDEVICE_OBJECT		pNextDevice,
                          PIO_STATUS_BLOCK	pIostatus
                          )
{
    KEVENT					SyncEvent;
    PIRP					pIrp ;
    NTSTATUS				ntStatus;
    PIO_STACK_LOCATION		pIoStack;


    KeInitializeEvent(&SyncEvent,NotificationEvent,FALSE);

    pIrp = IoAllocateIrp(pNextDevice->StackSize,FALSE);
    ////VirtualizerEnd();
    if( pIrp  == NULL )
    {
        ntStatus= STATUS_INSUFFICIENT_RESOURCES;
        goto EXIT1;
    }


    //map the user buffer into our new allocated irp,
    //Attention!!  the pPreIrp maybe has buffer in user space


    pIrp->MdlAddress = IoAllocateMdl(pBuffer, Len, FALSE, TRUE, NULL);
    ////VirtualizerEnd();
    if (!pIrp->MdlAddress)
    {
        ntStatus= STATUS_INSUFFICIENT_RESOURCES;
        goto EXIT1;
    }


    MmBuildMdlForNonPagedPool(pIrp->MdlAddress);

    pIrp->UserBuffer						= MmGetMdlVirtualAddress(pIrp->MdlAddress);
    //pIrp->AssociatedIrp.SystemBuffer		= pEncryptHead;

    pIrp->Flags								= IRP_NOCACHE|IRP_INPUT_OPERATION;
    pIrp->UserEvent							= NULL;
    pIrp->RequestorMode						= KernelMode;
    pIrp->Tail.Overlay.Thread				= (PETHREAD) PsGetCurrentThread();
    pIrp->Tail.Overlay.OriginalFileObject	= pDiskFile;


    pIoStack  = IoGetNextIrpStackLocation(pIrp);

    pIoStack->MajorFunction = IRP_MJ_READ;
    pIoStack->MinorFunction = IRP_MN_NORMAL;
    pIoStack->DeviceObject  = pNextDevice;
    pIoStack->FileObject	= pDiskFile;
    pIoStack->Parameters.Read.ByteOffset			= Offset;
    pIoStack->Parameters.Read.Length				= Len;

    ntStatus = IoSetCompletionRoutineEx(pNextDevice,
        pIrp,
        PfpNonCachedReadByIrpCompete,
        &SyncEvent,
        TRUE,
        TRUE,
        TRUE);

    if(!NT_SUCCESS(ntStatus))
    {
        IoFreeMdl(pIrp->MdlAddress);
        IoFreeIrp(pIrp);
        pIrp = NULL;		
    }else
    {	
        ntStatus = IoCallDriver(pNextDevice,pIrp);

        if( ntStatus  == STATUS_PENDING )
        {

            KeWaitForSingleObject( &SyncEvent,
                Executive,
                KernelMode,
                FALSE,
                NULL);

        }
        *pIostatus  =  pIrp->IoStatus; 
        ntStatus	= pIrp->IoStatus.Status;
        IoFreeMdl(pIrp->MdlAddress);

        IoFreeIrp(pIrp);
    }

EXIT1:	
    return ntStatus;


}

NTSTATUS
PfpNonCachedReadByIrpCompete(
                             IN PDEVICE_OBJECT  DeviceObject,
                             IN PIRP  Irp,
                             IN PVOID  Context
                             )
{
    PKEVENT    pEvent;
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(DeviceObject);

    pEvent = (PKEVENT)Context;	

    KeSetEvent(pEvent,0,FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

BOOLEAN
PfpIsFileEncryptedAccordtoFileSize(LONGLONG Filesize)
{
    return ((Filesize==0)||((Filesize>ENCRYPTIONHEADLENGTH) && !(Filesize&((LONGLONG) (g_SectorSize-1)))));

}
BOOLEAN PfpIsFileEncrypted(UNICODE_STRING * pFileFullPath,PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS					ntstatus ;
    OBJECT_ATTRIBUTES			Objattri;
    PFILESPY_DEVICE_EXTENSION	pExt			= (PFILESPY_DEVICE_EXTENSION )DeviceObject->DeviceExtension;
    PFILESPY_DEVICE_EXTENSION	pShadowExt		= NULL;
    PDEVICE_OBJECT				pShadowDevice	= NULL;
    WCHAR						*szFullpath		= NULL;
    WCHAR*						szCurPos		= NULL;

    PFILE_OBJECT				pUserFileobeject= NULL;
    PFILE_OBJECT				pFileObject		= NULL;
    UNICODE_STRING				ObjectAttri_U;	

    PVOID						EncryptHead;
    IO_STATUS_BLOCK				iostatusread;
    HANDLE						hFileWriteThrough = INVALID_HANDLE_VALUE;
    ULONG						ShadowDeviceNameLen = 0;
    BOOLEAN						bEnCrypted = FALSE;
    PCREATECONTEXT				pCreateContext = NULL;
    HANDLE						hCreatethread = INVALID_HANDLE_VALUE;
    //OBJECT_ATTRIBUTES			ObjectAttributes;
    EncryptHead =NULL;
    ASSERT(pFileFullPath);
    ASSERT(pFileFullPath->Buffer && pFileFullPath->Length!=0);
    ASSERT(!pExt->bShadow) ;

    pShadowDevice  = pExt->pShadowDevice;

    ASSERT(pShadowDevice);

    pShadowExt  = (PFILESPY_DEVICE_EXTENSION)pShadowDevice->DeviceExtension;
    ShadowDeviceNameLen =wcslen(pShadowExt->DeviceNames );

    szFullpath = ExAllocatePoolWithTag(PagedPool,sizeof(WCHAR)*(ShadowDeviceNameLen+1)+pFileFullPath->Length,'9105');
    if(szFullpath== NULL )
    {
        return FALSE;
    }
    szCurPos  = szFullpath;	

    wcscpy(szCurPos,pShadowExt->DeviceNames);
    szCurPos= &szCurPos[ShadowDeviceNameLen];


    RtlCopyMemory(szCurPos ,pFileFullPath->Buffer,pFileFullPath->Length);
    szCurPos [pFileFullPath->Length>>1] =0;

    RtlInitUnicodeString(&ObjectAttri_U,szFullpath);

    InitializeObjectAttributes(&Objattri,
        &ObjectAttri_U,
        OBJ_CASE_INSENSITIVE |OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );

    ntstatus  = ZwCreateFile(	&hFileWriteThrough,
        FILE_READ_DATA| SYNCHRONIZE ,
        &Objattri,
        &iostatusread,									
        NULL,
        FILE_ATTRIBUTE_NORMAL ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING|FILE_SYNCHRONOUS_IO_NONALERT|FILE_COMPLETE_IF_OPLOCKED,
        NULL,
        0);

    /*
    pCreateContext = ExAllocateFromNPagedLookasideList(&PfpCreateContextLookasideList);
    if(pCreateContext == NULL)
    {
    ntstatus = STATUS_INSUFFICIENT_RESOURCES;
    goto EXIT;		
    }


    pCreateContext->EaBuffer = NULL;
    pCreateContext->EaLength  = 0;

    pCreateContext->phFileCreated = INVALID_HANDLE_VALUE;
    pCreateContext->DesiredAccess =(FILE_READ_DATA| SYNCHRONIZE );
    pCreateContext->pFilePath = szFullpath;
    pCreateContext->AllocationSize.QuadPart =-1;
    pCreateContext->FileAttributes = FILE_ATTRIBUTE_NORMAL;
    pCreateContext->ShareAccess    = FILE_SHARE_READ;//|FILE_SHARE_WRITE;
    pCreateContext->CreateDisposition = FILE_OPEN;
    pCreateContext->CreateOptions = FILE_NON_DIRECTORY_FILE|FILE_NO_INTERMEDIATE_BUFFERING|FILE_SYNCHRONOUS_IO_NONALERT;
    KeInitializeEvent(&pCreateContext->hEvent,NotificationEvent, FALSE);
    pCreateContext->ntstatus = STATUS_SUCCESS;

    InsertCreateContextIntoLists(pCreateContext);

    KeWaitForSingleObject(&pCreateContext->hEvent,Executive,KernelMode,FALSE,(PLARGE_INTEGER)NULL)	;


    ntstatus= pCreateContext->ntstatus;

    iostatusread.Information = pCreateContext->IoStatusBlock.Information;
    iostatusread.Status= pCreateContext->IoStatusBlock.Status;
    hFileWriteThrough = pCreateContext->phFileCreated;

    if(pCreateContext->EaBuffer != NULL)
    {
    ExFreePool_A(pCreateContext->EaBuffer);
    }
    ExFreeToNPagedLookasideList(&PfpCreateContextLookasideList,pCreateContext);


    */
    if(!NT_SUCCESS(ntstatus))
    {
        goto EXIT;
    }



    if(iostatusread.Information == FILE_OPENED)
    {
        LARGE_INTEGER	ByteOffset	= {0};

        ntstatus = ObReferenceObjectByHandle(hFileWriteThrough,
            0,
            *IoFileObjectType,
            KernelMode,
            &pFileObject,
            NULL);
        if(!NT_SUCCESS(ntstatus))
        {
            goto EXIT;
        }

        EncryptHead = ExAllocatePoolWithTag(NonPagedPool ,ENCRYPTIONHEADLENGTH,'N201');
        if(EncryptHead == NULL)
        {
            ntstatus = STATUS_INSUFFICIENT_RESOURCES;
            goto EXIT;
        }

        ntstatus = PfpReadFileByAllocatedIrp(EncryptHead,ENCRYPTIONHEADLENGTH,ByteOffset,pFileObject,pExt->NLExtHeader.AttachedToDeviceObject,&iostatusread);

        if(NT_SUCCESS(ntstatus)&&  iostatusread.Information == ENCRYPTIONHEADLENGTH )
        {
            bEnCrypted=PfpCheckEncryptInfo(EncryptHead,ENCRYPTIONHEADLENGTH);				 
        }
        else if(ntstatus == STATUS_END_OF_FILE) //?????????����ط�Ҫ����һ��
        {	
            bEnCrypted = TRUE;
        }
    }

EXIT:
    if(szFullpath)
    {
        ExFreePool(szFullpath);
    }
    if(EncryptHead)
    {
        ExFreePool(EncryptHead);
    }
    if(pFileObject!= NULL)
    {
        ObDereferenceObject(pFileObject);
    }
    if(hFileWriteThrough!= INVALID_HANDLE_VALUE)
    {
        ZwClose(hFileWriteThrough);		
    }
    return bEnCrypted;
}


HANDLE PfpGetHandleFromObject(PFILE_OBJECT pFileobject)

{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    if(pFileobject== NULL)
    {
        return INVALID_HANDLE_VALUE;
    }
    ObOpenObjectByPointer(pFileobject,
        OBJ_KERNEL_HANDLE ,
        NULL,
        DELETE|GENERIC_READ|GENERIC_WRITE,
        *IoFileObjectType,
        KernelMode,
        &hFile);
    return hFile;
}

VOID
PfpCloseFileHasGoThroughCleanupAndNotUsed(PDISKFILEOBJECT pDiskFileObject)
{

    if( pDiskFileObject->pFCB && 
        PfpIsAllFileObjectThroughCleanup(pDiskFileObject) &&
        !pDiskFileObject->bOpeningAfterAllGothroughCleanup)
    {					

        PfpCloseRealDiskFile(&(pDiskFileObject->hFileWriteThrough),&(pDiskFileObject->pDiskFileObjectWriteThrough));

        SetFlag(((PPfpFCB)pDiskFileObject->pFCB)->FcbState, FCB_STATE_FILE_DELETED);

        if(pDiskFileObject->bNeedBackUp)
        {//��������ϵ��ļ�Ҳ�ر��ˣ�����ô������Ϣ���Ǹ����ݵ�Thread��ȥ�رձ��ݵ��ļ���

            PfpCloseRealDiskFile(&pDiskFileObject->hBackUpFileHandle,&pDiskFileObject->hBackUpFileObject);
        }
    }
}
BOOLEAN	PfpIsAllFileObjectThroughCleanup(PDISKFILEOBJECT pDiskFileobject)
{
    PLIST_ENTRY		pList			= NULL;
    PUSERFILEOBJECT pUserFileobject = NULL;
    if(pDiskFileobject)
    {
        for(pList = pDiskFileobject->UserFileObjList.Blink;pList!= &pDiskFileobject->UserFileObjList;pList = pList->Blink)
        {
            pUserFileobject  = CONTAINING_RECORD(pList ,USERFILEOBJECT,list)	;
            if(!FlagOn(pUserFileobject ->UserFileObj->Flags,FO_CLEANUP_COMPLETE))
            {
                return FALSE;
            }
        }
    }
    return TRUE;
}

ULONG	PfpGetUncleanupCount(PDISKFILEOBJECT pDiskFileobject)
{
    PLIST_ENTRY		pList			= NULL;
    PUSERFILEOBJECT pUserFileobject = NULL;
    ULONG			nUnCleanup		= 0;
    if(pDiskFileobject)
    {
        for(pList = pDiskFileobject->UserFileObjList.Blink;pList!= &pDiskFileobject->UserFileObjList;pList = pList->Blink)
        {
            pUserFileobject  = CONTAINING_RECORD(pList ,USERFILEOBJECT,list)	;
            if(!FlagOn(pUserFileobject ->UserFileObj->Flags,FO_CLEANUP_COMPLETE))
            {
                nUnCleanup++;
            }
        }
    }
    return nUnCleanup;
}


BOOLEAN PfpIsBackupFileObjectStillValid(PDISKFILEOBJECT pDiskFileobject)
{
    return ( (pDiskFileobject->hBackUpFileHandle!= INVALID_HANDLE_VALUE) && (pDiskFileobject->hBackUpFileObject!= NULL));
}


BOOLEAN IsFileUnderBackupDir(WCHAR *szDevice, UNICODE_STRING* pFilePath)
{
    if(_wcsnicmp(szDevice,g_szBackupDir,2)==0)
    {
        if(pFilePath->Length> (USHORT)((wcslen(g_szBackupDir)-2)<<1 ) )
        {
            if(_wcsnicmp(pFilePath->Buffer,&g_szBackupDir[2],wcslen(g_szBackupDir)-2)==0)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOLEAN  IsDirectory(ULONG Action)
{
    return (BOOLEAN)(FlagOn( Action, FILE_DIRECTORY_FILE ));	
}

BOOLEAN  IsOpenDirectory(UCHAR Action)
{
    return (BOOLEAN)(BooleanFlagOn(Action, SL_OPEN_TARGET_DIRECTORY));
}

BOOL	 IsFileTypeBelongExeType(WCHAR* pExt)
{
    WCHAR			p2[]=L"DLL";
    WCHAR			p3[]=L"OCX";
    WCHAR			p4[]=L"EXE";
    WCHAR			p5[]=L"SYS";
    WCHAR			p6[]=L"COM";
    WCHAR			p7[]=L"BAT";
    return (_wcsicmp(pExt,p2)==0 ||_wcsicmp(pExt,p3)==0||_wcsicmp(pExt,p4)==0 ||_wcsicmp(pExt,p5)==0||_wcsicmp(pExt,p6)==0 ||_wcsicmp(pExt,p7)==0);	
}

VOID	 DoLog(WCHAR* szDevice, UNICODE_STRING *pFilePath,UNICODE_STRING * pProcessImage,BOOLEAN bCreate,BOOLEAN bEncrypted)
{
    WCHAR *szProcName	= NULL;
    WCHAR *pszChar		= NULL;
    ULONG nCount1		= 0;
    if(pProcessImage && pFilePath && szDevice )
    {
        szProcName = ExAllocatePoolWithTag(PagedPool,pProcessImage->Length+2,'0205');
        if(szProcName !=  NULL)
        {	
            memcpy(szProcName,pProcessImage->Buffer,pProcessImage->Length);//???????����ط�Ҫ�Ż�һ�£���Ҫ�����ڴ���
            szProcName[pProcessImage->Length/sizeof(WCHAR)]=L'\0';
            pszChar  = wcsrchr(szProcName,L'\\');
            if(pszChar== NULL)
            {
                pszChar  = szProcName;
                nCount1  = pProcessImage->Length/sizeof(WCHAR);
            }else
            {
                pszChar++;
                nCount1  = pProcessImage->Length/sizeof(WCHAR)-(pszChar-szProcName);
            }
            AddIntoLogQeuue(pFilePath->Buffer,pFilePath->Length/sizeof(WCHAR),szDevice,2,pszChar,nCount1, bCreate,   bEncrypted,(ULONG)(LONGLONG)PsGetCurrentProcessId());
            if(szProcName)
            {
                ExFreePool(szProcName);
            }
        }

    }

}


BOOLEAN
PfpDoesPathInvalid(PWCHAR szFullPath)
{
    return (wcschr(szFullPath, L'\\')!= NULL ||
        wcschr(szFullPath, L'/')!= NULL ||
        wcschr(szFullPath, L'?')!= NULL  ||
        wcschr(szFullPath, L'*')!= NULL  ||
        wcschr(szFullPath, L'|')!= NULL||
        wcschr(szFullPath, L':')!= NULL  ||
        wcschr(szFullPath, L'"')!= NULL  ||
        wcschr(szFullPath,   L'>')!= NULL ||
        wcschr(szFullPath,   L'<')!= NULL );

}

BOOLEAN 
PfpIsFileNameValid(PWCHAR szFileName,ULONG len)
{
    ULONG nSize = len/sizeof(WCHAR);
    ULONG nIndex = 0;
    while(nIndex <nSize)
    {
        if(/*szFileName[nIndex]==L'\\'||*/
            szFileName[nIndex]==L'/'||
            szFileName[nIndex]==L'?'||
            szFileName[nIndex]==L'*'||
            szFileName[nIndex]==L'|'||
            //szFileName[nIndex]==L':'||
            szFileName[nIndex]==L'"'||
            szFileName[nIndex]==L'>'||
            szFileName[nIndex]==L'<')
        {
            return FALSE;
        }
        nIndex++;
    }
    return TRUE;
}

VOID
PfpCreateFileWorker (PVOID  Context)
{
    OBJECT_ATTRIBUTES			Objattri;
    UNICODE_STRING				ObjectAttri_U;
    PCREATECONTEXT pCreateContext = 	NULL;
    PLIST_ENTRY				    pListItem = NULL;
    KIRQL oldIrql;	
    while(1)
    {
        if(NT_SUCCESS(KeWaitForSingleObject(&g_EventCreateThread,Executive,KernelMode,FALSE,(PLARGE_INTEGER)NULL)))

        {
            KeClearEvent(&g_EventCreateThread);
            while(1)
            {
                KeAcquireSpinLock( &gCreateContextLock, &oldIrql );
                pListItem = RemoveHeadList (&g_CreateContext);
                KeReleaseSpinLock( &gCreateContextLock, oldIrql );
                if(pListItem == &g_CreateContext)
                {
                    break;
                }

                pCreateContext = CONTAINING_RECORD(pListItem,CREATECONTEXT,list);
                RtlInitUnicodeString(&ObjectAttri_U,pCreateContext->pFilePath);

                InitializeObjectAttributes(&Objattri,
                    &ObjectAttri_U,
                    OBJ_CASE_INSENSITIVE |OBJ_KERNEL_HANDLE,
                    NULL,
                    NULL
                    );

                pCreateContext->ntstatus  = ZwCreateFile(	&pCreateContext->phFileCreated,
                    pCreateContext->DesiredAccess,
                    &Objattri,
                    &pCreateContext->IoStatusBlock,									
                    ((pCreateContext->AllocationSize.QuadPart==-1)?NULL:&pCreateContext->AllocationSize),
                    pCreateContext->FileAttributes,
                    pCreateContext->ShareAccess,
                    pCreateContext->CreateDisposition,
                    pCreateContext->CreateOptions|FILE_COMPLETE_IF_OPLOCKED ,
                    pCreateContext->EaBuffer ,
                    pCreateContext->EaLength);
                KeSetEvent(&pCreateContext->hEvent,IO_NO_INCREMENT, FALSE);

            };
        }
    }
}

VOID
PfpCreateFileWorker1 (PVOID  Context)
{
    OBJECT_ATTRIBUTES			Objattri;
    UNICODE_STRING				ObjectAttri_U;
    PCREATECONTEXT pCreateContext = 	NULL;
    PLIST_ENTRY				    pListItem = NULL;
    KIRQL oldIrql;
    while(1)
    {
        if(NT_SUCCESS(KeWaitForSingleObject(&g_EventCreateThread1,Executive,KernelMode,FALSE,(PLARGE_INTEGER)NULL)))
        {
            KeClearEvent(&g_EventCreateThread1);
            while(1)
            {
                KeAcquireSpinLock( &gCreateContextLock1, &oldIrql );
                pListItem = RemoveHeadList (&g_CreateContext1);
                KeReleaseSpinLock( &gCreateContextLock1, oldIrql );

                if(pListItem == &g_CreateContext1)
                {
                    break;
                }

                pCreateContext = CONTAINING_RECORD(pListItem,CREATECONTEXT,list);

                RtlInitUnicodeString(&ObjectAttri_U,pCreateContext->pFilePath);

                InitializeObjectAttributes(&Objattri,
                    &ObjectAttri_U,
                    OBJ_CASE_INSENSITIVE |OBJ_KERNEL_HANDLE,
                    NULL,
                    NULL
                    );

                pCreateContext->ntstatus  = ZwCreateFile(	&pCreateContext->phFileCreated,
                    pCreateContext->DesiredAccess,
                    &Objattri,
                    &pCreateContext->IoStatusBlock,									
                    ((pCreateContext->AllocationSize.QuadPart==-1)?NULL:&pCreateContext->AllocationSize),
                    pCreateContext->FileAttributes,
                    pCreateContext->ShareAccess,
                    pCreateContext->CreateDisposition,
                    pCreateContext->CreateOptions|FILE_COMPLETE_IF_OPLOCKED ,
                    pCreateContext->EaBuffer ,
                    pCreateContext->EaLength);
                KeSetEvent(&pCreateContext->hEvent,IO_NO_INCREMENT, FALSE);

            };

        }
    };
}
VOID		InsertCreateContextIntoLists(PCREATECONTEXT pCreateContext)
{
    KIRQL oldIrql;
    if(g_CreateNum.QuadPart&0x1)
    {
        KeAcquireSpinLock( &gCreateContextLock, &oldIrql );		 
        InsertTailList(&g_CreateContext,&pCreateContext->list);
        KeReleaseSpinLock( &gCreateContextLock, oldIrql );
        KeSetEvent(&g_EventCreateThread,IO_NO_INCREMENT, FALSE);
    }else
    {
        KeAcquireSpinLock( &gCreateContextLock1, &oldIrql );
        InsertTailList(&g_CreateContext1,&pCreateContext->list);
        KeReleaseSpinLock( &gCreateContextLock1, oldIrql );
        KeSetEvent(&g_EventCreateThread1,IO_NO_INCREMENT, FALSE);
    }
    g_CreateNum.QuadPart++;
}




NTSTATUS 
PfpQueryDirectoryByIrp(	IN PDEVICE_OBJECT pNextDevice,
                       IN PFILE_OBJECT	  pDirFileObject,
                       IN FILE_INFORMATION_CLASS FileInformationClass,
                       IN PVOID			pBuffer, //�������buffer
                       IN ULONG			Len,//userbuffer��ʣ��� �ֽ�
                       IN PUNICODE_STRING pFilterUnicode,
                       PIO_STATUS_BLOCK  pIostatus)
{
    PIRP				pnewIrp;
    PIO_STACK_LOCATION  pIostack;
    //PIO_STACK_LOCATION  pPreIoStack;	
    KEVENT				waitEvent;
    NTSTATUS			ntstatus;
    pnewIrp = IoAllocateIrp(pNextDevice->StackSize,TRUE);

    if(pnewIrp == NULL)
    {
        pIostatus->Information =0;
        pIostatus->Status = STATUS_INSUFFICIENT_RESOURCES;
        return pIostatus->Status ;

    }	

    pIostack	= IoGetNextIrpStackLocation(pnewIrp);

    pIostack->FileObject	= pDirFileObject;

    pIostack->Flags			= SL_RETURN_SINGLE_ENTRY|SL_RESTART_SCAN;
    pIostack->MajorFunction = IRP_MJ_DIRECTORY_CONTROL;
    pIostack->MinorFunction = IRP_MN_QUERY_DIRECTORY;
    pIostack->Parameters.QueryDirectory.FileName = pFilterUnicode;
    pIostack->Parameters.QueryDirectory.FileInformationClass= FileInformationClass  ;
    pIostack->Parameters.QueryDirectory.Length = Len;
    pnewIrp->MdlAddress = NULL;
    pnewIrp->UserBuffer = pBuffer;
    pnewIrp->Tail.Overlay.Thread = PsGetCurrentThread();
    pnewIrp->UserEvent = NULL;
    pnewIrp->UserIosb		= pIostatus;
    pnewIrp->Flags			= IRP_SYNCHRONOUS_API;
    pnewIrp->RequestorMode	= KernelMode;

    pnewIrp->Tail.Overlay.Thread = PsGetCurrentThread();
    pnewIrp->UserEvent		= NULL;



    KeInitializeEvent(&waitEvent,NotificationEvent ,FALSE);
    IoSetCompletionRoutine(	pnewIrp,
        DirControlCompletion,		//CompletionRoutine
        &waitEvent,					//context parameter
        TRUE,
        TRUE,
        TRUE
        );

    ntstatus =  IoCallDriver(pNextDevice,pnewIrp);

    if (STATUS_PENDING == ntstatus)
    {

        KeWaitForSingleObject( &waitEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL );
    }

    //
    //  Verify the completion has actually been run
    //

    ASSERT(KeReadStateEvent(&waitEvent) || !NT_SUCCESS(pIostatus->Status));

    return pIostatus->Status;
}