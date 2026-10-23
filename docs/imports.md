# Imports do Storm 2

Importados (thunks no XEX): **229**. Estado estatico no ReXGlue: STUB=18, impl=211.

Execucao medida: **115** imports chamados; **0** deles sao STUB/STUB_RET/AUSENTE (bloqueios).

Colunas do briefing: identificado = todos; necessario = chamado na execucao medida; implementado = impl (nao implica correto); testado = sem teste dedicado ainda; bloqueado = chamado e STUB/AUSENTE.

| import | thunk | estado | fonte | chamadas | 1o quadro | bloqueado |
|---|---|---|---|---:|---:|---|
| `__C_specific_handler` | `0x827DCB2C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `_snprintf` | `0x827DD33C` | impl | kernel/xboxkrnl/xboxkrnl_strings.cpp | 0 |  |  |
| `_vsnprintf` | `0x827DD1DC` | impl | kernel/xboxkrnl/xboxkrnl_strings.cpp | 0 |  |  |
| `DbgPrint` | `0x827DCB1C` | impl | kernel/xboxkrnl/xboxkrnl_strings.cpp | 0 |  |  |
| `ExAllocatePool` | `0x827DD3CC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `ExAllocatePoolTypeWithTag` | `0x827DD47C` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `ExCreateThread` | `0x827DD28C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 92 | 0 |  |
| `ExFreePool` | `0x827DD3BC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `ExGetXConfigSetting` | `0x827DCB0C` | impl | kernel/xboxkrnl/xboxkrnl_xconfig.cpp | 1658 | 0 |  |
| `ExRegisterTitleTerminateNotification` | `0x827DD0EC` | impl | kernel/xboxkrnl/xboxkrnl_modules.cpp | 3 | 0 |  |
| `ExTerminateThread` | `0x827DD26C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 74 | 3 |  |
| `HalReturnToFirmware` | `0x827DCC8C` | impl | kernel/xboxkrnl/xboxkrnl_hal.cpp | 0 |  |  |
| `IoCheckShareAccess` | `0x827DD4CC` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoCompleteRequest` | `0x827DD49C` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoCreateDevice` | `0x827DD45C` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoDeleteDevice` | `0x827DD46C` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoDismountVolume` | `0x827DD35C` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoDismountVolumeByFileHandle` | `0x827DD4FC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoInvalidDeviceRequest` | `0x827DD43C` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoRemoveShareAccess` | `0x827DD4EC` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `IoSetShareAccess` | `0x827DD4DC` | STUB | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `KeAcquireSpinLockAtRaisedIrql` | `0x827DD54C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 21064 | 1 |  |
| `KeBugCheck` | `0x827DD14C` | impl | kernel/xboxkrnl/xboxkrnl_debug.cpp | 0 |  |  |
| `KeBugCheckEx` | `0x827DCCBC` | impl | kernel/xboxkrnl/xboxkrnl_debug.cpp | 0 |  |  |
| `KeDelayExecutionThread` | `0x827DD42C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 343147 | 2 |  |
| `KeEnterCriticalRegion` | `0x827DD0FC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 6901 | 0 |  |
| `KeGetCurrentProcessType` | `0x827DCACC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 101327 | 0 |  |
| `KeInitializeDpc` | `0x827DD6DC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 2 | 0 |  |
| `KeLeaveCriticalRegion` | `0x827DD0DC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 6901 | 0 |  |
| `KeLockL2` | `0x827DD6BC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `KeQueryBasePriorityThread` | `0x827DCC1C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 200 | 0 |  |
| `KeQueryPerformanceFrequency` | `0x827DD07C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 7566 | 0 |  |
| `KeQuerySystemTime` | `0x827DD3EC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 209 | 0 |  |
| `KeReleaseSpinLockFromRaisedIrql` | `0x827DD53C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 21064 | 1 |  |
| `KeResetEvent` | `0x827DD38C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 12 | 585 |  |
| `KeSetAffinityThread` | `0x827DCC4C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 91 | 0 |  |
| `KeSetBasePriorityThread` | `0x827DCBFC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 471 | 0 |  |
| `KeSetEvent` | `0x827DD08C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 87305 | 7 |  |
| `KeTlsAlloc` | `0x827DD18C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 1 | 0 |  |
| `KeTlsFree` | `0x827DD17C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 0 |  |  |
| `KeTlsGetValue` | `0x827DD15C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 6577 | 0 |  |
| `KeTlsSetValue` | `0x827DD16C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 77 | 0 |  |
| `KeUnlockL2` | `0x827DD6CC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `KeWaitForMultipleObjects` | `0x827DD0AC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 86651 | 7 |  |
| `KeWaitForSingleObject` | `0x827DD09C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 7884 | 0 |  |
| `KfAcquireSpinLock` | `0x827DD13C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 20833 | 0 |  |
| `KfReleaseSpinLock` | `0x827DD12C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 20833 | 0 |  |
| `KiApcNormalRoutineNop` | `0x827DD55C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 1 | 0 |  |
| `MmAllocatePhysicalMemoryEx` | `0x827DD2AC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 65 | 0 |  |
| `MmFreePhysicalMemory` | `0x827DD2CC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 49 | 1 |  |
| `MmGetPhysicalAddress` | `0x827DD03C` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 1587 | 0 |  |
| `MmLockAndMapSegmentArray` | `0x827DD51C` | STUB | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `MmMapIoSpace` | `0x827DD04C` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 199 | 135 |  |
| `MmQueryAddressProtect` | `0x827DD1CC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 3768 | 3 |  |
| `MmQueryAllocationSize` | `0x827DD2BC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 93 | 0 |  |
| `MmQueryStatistics` | `0x827DD3DC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 3 | 0 |  |
| `MmUnlockAndUnmapSegmentArray` | `0x827DD52C` | STUB | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `NetDll___WSAFDIsSet` | `0x827DCE2C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_accept` | `0x827DCDBC` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_bind` | `0x827DCD8C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_closesocket` | `0x827DCD4C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_connect` | `0x827DCD9C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_ioctlsocket` | `0x827DCD6C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_listen` | `0x827DCDAC` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_recv` | `0x827DCDDC` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_recvfrom` | `0x827DCDEC` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_select` | `0x827DCDCC` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_send` | `0x827DCDFC` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_sendto` | `0x827DCE0C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_setsockopt` | `0x827DCD7C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_shutdown` | `0x827DCD5C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_socket` | `0x827DCD3C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_WSACleanup` | `0x827DCD2C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_WSAGetLastError` | `0x827DCE1C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_WSAStartup` | `0x827DCD1C` | impl | kernel/xam/xam_net.cpp | 1 | 0 |  |
| `NetDll_XNetInAddrToXnAddr` | `0x827DCE4C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_XNetQosListen` | `0x827DCE5C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_XNetQosLookup` | `0x827DCE6C` | STUB | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_XNetQosRelease` | `0x827DCE8C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_XNetQosServiceLookup` | `0x827DCE7C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_XNetXnAddrToInAddr` | `0x827DCE3C` | impl | kernel/xam/xam_net.cpp | 0 |  |  |
| `NetDll_XnpLogonGetStatus` | `0x827DCEEC` | STUB | kernel/xam/xam_net.cpp | 0 |  |  |
| `NtAllocateVirtualMemory` | `0x827DCC9C` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 577 | 0 |  |
| `NtCancelTimer` | `0x827DD24C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 0 |  |  |
| `NtClearEvent` | `0x827DD1BC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 2879 | 3 |  |
| `NtClose` | `0x827DCAFC` | impl | kernel/xboxkrnl/xboxkrnl_ob.cpp | 2417 | 0 |  |
| `NtCreateEvent` | `0x827DCA7C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 334 | 0 |  |
| `NtCreateFile` | `0x827DCABC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 101 | 3 |  |
| `NtCreateMutant` | `0x827DD2DC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 2 | 0 |  |
| `NtCreateSemaphore` | `0x827DD1FC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 1857 | 0 |  |
| `NtCreateTimer` | `0x827DD27C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 0 |  |  |
| `NtDeviceIoControlFile` | `0x827DD50C` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `NtDuplicateObject` | `0x827DCBDC` | impl | kernel/xboxkrnl/xboxkrnl_ob.cpp | 0 |  |  |
| `NtFlushBuffersFile` | `0x827DD34C` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `NtFreeVirtualMemory` | `0x827DCCCC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 550 | 2 |  |
| `NtOpenFile` | `0x827DCA9C` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 79 | 3 |  |
| `NtQueryDirectoryFile` | `0x827DCBAC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `NtQueryFullAttributesFile` | `0x827DCB6C` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `NtQueryInformationFile` | `0x827DCB8C` | impl | kernel/xboxkrnl/xboxkrnl_io_info.cpp | 294 | 3 |  |
| `NtQueryVirtualMemory` | `0x827DCCFC` | impl | kernel/xboxkrnl/xboxkrnl_memory.cpp | 0 |  |  |
| `NtQueryVolumeInformationFile` | `0x827DCB9C` | impl | kernel/xboxkrnl/xboxkrnl_io_info.cpp | 0 |  |  |
| `NtQueueApcThread` | `0x827DCC3C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 0 |  |  |
| `NtReadFile` | `0x827DCBCC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 1013 | 3 |  |
| `NtReadFileScatter` | `0x827DCBBC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `NtReleaseMutant` | `0x827DD29C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 62209 | 0 |  |
| `NtReleaseSemaphore` | `0x827DD20C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 83908 | 0 |  |
| `NtResumeThread` | `0x827DD1EC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 112 | 0 |  |
| `NtSetEvent` | `0x827DD23C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 195142128 | 0 |  |
| `NtSetInformationFile` | `0x827DCB7C` | impl | kernel/xboxkrnl/xboxkrnl_io_info.cpp | 617 | 7 |  |
| `NtSetTimerEx` | `0x827DD25C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 0 |  |  |
| `NtSignalAndWaitForSingleObjectEx` | `0x827DD21C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 195035604 | 0 |  |
| `NtSuspendThread` | `0x827DCC2C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 9 | 585 |  |
| `NtWaitForMultipleObjectsEx` | `0x827DD22C` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 1 | 7 |  |
| `NtWaitForSingleObjectEx` | `0x827DCADC` | impl | kernel/xboxkrnl/xboxkrnl_threading.cpp | 195202114 | 0 |  |
| `NtWriteFile` | `0x827DCAEC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 2 | 1339 |  |
| `ObCreateSymbolicLink` | `0x827DD36C` | impl | kernel/xboxkrnl/xboxkrnl_ob.cpp | 0 |  |  |
| `ObDeleteSymbolicLink` | `0x827DD37C` | impl | kernel/xboxkrnl/xboxkrnl_ob.cpp | 0 |  |  |
| `ObDereferenceObject` | `0x827DCBEC` | impl | kernel/xboxkrnl/xboxkrnl_ob.cpp | 824 | 0 |  |
| `ObIsTitleObject` | `0x827DD4BC` | STUB | kernel/xboxkrnl/xboxkrnl_ob.cpp | 0 |  |  |
| `ObReferenceObject` | `0x827DD44C` | STUB | kernel/xboxkrnl/xboxkrnl_ob.cpp | 0 |  |  |
| `ObReferenceObjectByHandle` | `0x827DCC0C` | impl | kernel/xboxkrnl/xboxkrnl_ob.cpp | 824 | 0 |  |
| `RtlCompareMemoryUlong` | `0x827DCCDC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlCompareStringN` | `0x827DD48C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlEnterCriticalSection` | `0x827DCC6C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 5329317 | 0 |  |
| `RtlFillMemoryUlong` | `0x827DCCAC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 1 | 1 |  |
| `RtlFreeAnsiString` | `0x827DD3FC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlImageXexHeaderField` | `0x827DCC7C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 1 | 0 |  |
| `RtlInitAnsiString` | `0x827DCAAC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 148 | 3 |  |
| `RtlInitializeCriticalSection` | `0x827DCCEC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 467 | 0 |  |
| `RtlInitializeCriticalSectionAndSpinCount` | `0x827DD1AC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 14 | 0 |  |
| `RtlInitUnicodeString` | `0x827DD41C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlLeaveCriticalSection` | `0x827DCC5C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 32987976 | 0 |  |
| `RtlMultiByteToUnicodeN` | `0x827DCA6C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlNtStatusToDosError` | `0x827DCA5C` | impl | kernel/xboxkrnl/xboxkrnl_error.cpp | 42 | 583 |  |
| `RtlRaiseException` | `0x827DCD0C` | impl | kernel/xboxkrnl/xboxkrnl_debug.cpp | 0 |  |  |
| `RtlTimeFieldsToTime` | `0x827DD3AC` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlTimeToTimeFields` | `0x827DD39C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 207 | 3 |  |
| `RtlTryEnterCriticalSection` | `0x827DD01C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 27839527 | 0 |  |
| `RtlUnicodeStringToAnsiString` | `0x827DD40C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlUnicodeToMultiByteN` | `0x827DCA8C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlUnwind` | `0x827DD19C` | impl | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `RtlUpcaseUnicodeChar` | `0x827DD4AC` | STUB | kernel/xboxkrnl/xboxkrnl_rtl.cpp | 0 |  |  |
| `sprintf` | `0x827DD5CC` | impl | kernel/xboxkrnl/xboxkrnl_strings.cpp | 0 |  |  |
| `StfsControlDevice` | `0x827DD2EC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `StfsCreateDevice` | `0x827DD2FC` | impl | kernel/xboxkrnl/xboxkrnl_io.cpp | 0 |  |  |
| `VdCallGraphicsNotificationRoutines` | `0x827DD67C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdEnableDisableClockGating` | `0x827DD5EC` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 0 |  |  |
| `VdEnableRingBufferRPtrWriteBack` | `0x827DD56C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdGetCurrentDisplayGamma` | `0x827DD5DC` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 2 | 0 |  |
| `VdGetCurrentDisplayInformation` | `0x827DD61C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 1 |  |
| `VdGetSystemCommandBuffer` | `0x827DD5BC` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 6900 | 0 |  |
| `VdInitializeEngines` | `0x827DD64C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdInitializeRingBuffer` | `0x827DD57C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdInitializeScalerCommandBuffer` | `0x827DD68C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 1 |  |
| `VdIsHSIOTrainingSucceeded` | `0x827DD65C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdPersistDisplay` | `0x827DD59C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 1 |  |
| `VdQueryVideoFlags` | `0x827DD66C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 2 | 0 |  |
| `VdQueryVideoMode` | `0x827DD60C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 3 | 0 |  |
| `VdRetrainEDRAM` | `0x827DD69C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 6900 | 0 |  |
| `VdRetrainEDRAMWorker` | `0x827DD6AC` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdSetDisplayMode` | `0x827DD62C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 1 |  |
| `VdSetGraphicsInterruptCallback` | `0x827DD63C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 1 | 0 |  |
| `VdSetSystemCommandBufferGpuIdentifierAddress` | `0x827DD58C` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 2 | 0 |  |
| `VdShutdownEngines` | `0x827DD5FC` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 0 |  |  |
| `VdSwap` | `0x827DD5AC` | impl | kernel/xboxkrnl/xboxkrnl_video.cpp | 6900 | 0 |  |
| `XamAlloc` | `0x827DCEBC` | impl | kernel/xam/xam_info.cpp | 0 |  |  |
| `XamContentClose` | `0x827DC9CC` | impl | kernel/xam/xam_content.cpp | 4 | 1127 |  |
| `XamContentCreateEnumerator` | `0x827DC9DC` | impl | kernel/xam/xam_content.cpp | 0 |  |  |
| `XamContentCreateEx` | `0x827DC9BC` | impl | kernel/xam/xam_content.cpp | 4 | 1127 |  |
| `XamContentGetDeviceData` | `0x827DC9EC` | impl | kernel/xam/xam_content_device.cpp | 1 | 1337 |  |
| `XamEnumerate` | `0x827DC99C` | impl | kernel/xam/xam_enum.cpp | 1 | 6792 |  |
| `XamFree` | `0x827DCE9C` | impl | kernel/xam/xam_info.cpp | 0 |  |  |
| `XamGetExecutionId` | `0x827DCA3C` | impl | kernel/xam/xam_info.cpp | 0 |  |  |
| `XamGetSystemVersion` | `0x827DC90C` | impl | kernel/xam/xam_info.cpp | 10 | 0 |  |
| `XamInputGetCapabilities` | `0x827DCFBC` | impl | kernel/xam/xam_input.cpp | 20677 | 8 |  |
| `XamInputGetKeystrokeEx` | `0x827DCFEC` | impl | kernel/xam/xam_input.cpp | 27568 | 8 |  |
| `XamInputGetState` | `0x827DCFCC` | impl | kernel/xam/xam_input.cpp | 6892 | 8 |  |
| `XamInputSetState` | `0x827DCFDC` | impl | kernel/xam/xam_input.cpp | 5780 | 1120 |  |
| `XamLoaderTerminateTitle` | `0x827DCA2C` | impl | kernel/xam/xam_info.cpp | 0 |  |  |
| `XamNotifyCreateListener` | `0x827DCF0C` | impl | kernel/xam/xam_notify.cpp | 1 | 0 |  |
| `XamSessionCreateHandle` | `0x827DCEDC` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamSessionRefObjByHandle` | `0x827DCECC` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamShowDeviceSelectorUI` | `0x827DC97C` | impl | kernel/xam/xam_ui.cpp | 1 | 1122 |  |
| `XamShowFriendsUI` | `0x827DC95C` | STUB | kernel/xam/xam_ui.cpp | 0 |  |  |
| `XamShowGamerCardUIForXUID` | `0x827DC96C` | STUB | kernel/xam/xam_ui.cpp | 0 |  |  |
| `XamShowMessageBoxUI` | `0x827DC98C` | impl | kernel/xam/xam_ui.cpp | 2 | 1243 |  |
| `XamShowMessageBoxUIEx` | `0x827DC9FC` | impl | kernel/xam/xam_ui.cpp | 0 |  |  |
| `XamShowSigninUI` | `0x827DC94C` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamTaskCloseHandle` | `0x827DCF9C` | impl | kernel/xam/xam_task.cpp | 0 |  |  |
| `XamTaskSchedule` | `0x827DCFAC` | impl | kernel/xam/xam_task.cpp | 0 |  |  |
| `XamTaskShouldExit` | `0x827DCF8C` | impl | kernel/xam/xam_task.cpp | 0 |  |  |
| `XamUserAreUsersFriends` | `0x827DC8EC` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamUserCheckPrivilege` | `0x827DC8FC` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamUserCreateAchievementEnumerator` | `0x827DC92C` | impl | kernel/xam/xam_user.cpp | 1 | 6792 |  |
| `XamUserCreateStatsEnumerator` | `0x827DC91C` | STUB | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamUserGetDeviceContext` | `0x827DCF1C` | impl | kernel/xam/xam_input.cpp | 0 |  |  |
| `XamUserGetName` | `0x827DC8CC` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamUserGetSigninInfo` | `0x827DCFFC` | impl | kernel/xam/xam_user.cpp | 8 | 9 |  |
| `XamUserGetSigninState` | `0x827DC8DC` | impl | kernel/xam/xam_user.cpp | 33 | 0 |  |
| `XamUserGetXUID` | `0x827DC93C` | impl | kernel/xam/xam_user.cpp | 0 |  |  |
| `XamUserReadProfileSettings` | `0x827DCF2C` | impl | kernel/xam/xam_user.cpp | 6 | 0 |  |
| `XamVoiceClose` | `0x827DCF4C` | impl | kernel/xam/xam_voice.cpp | 0 |  |  |
| `XamVoiceCreate` | `0x827DCF6C` | impl | kernel/xam/xam_voice.cpp | 0 |  |  |
| `XamVoiceHeadsetPresent` | `0x827DCF3C` | impl | kernel/xam/xam_voice.cpp | 0 |  |  |
| `XamVoiceSubmitPacket` | `0x827DCF5C` | STUB | kernel/xam/xam_voice.cpp | 0 |  |  |
| `XAudioGetSpeakerConfig` | `0x827DD11C` | impl | kernel/xboxkrnl/xboxkrnl_audio.cpp | 1 | 7 |  |
| `XAudioGetVoiceCategoryVolume` | `0x827DD02C` | impl | kernel/xboxkrnl/xboxkrnl_audio.cpp | 43325 | 7 |  |
| `XAudioRegisterRenderDriverClient` | `0x827DD10C` | impl | kernel/xboxkrnl/xboxkrnl_audio.cpp | 1 | 7 |  |
| `XAudioSubmitRenderDriverFrame` | `0x827DD0BC` | impl | kernel/xboxkrnl/xboxkrnl_audio.cpp | 43325 | 9 |  |
| `XAudioUnregisterRenderDriverClient` | `0x827DD0CC` | impl | kernel/xboxkrnl/xboxkrnl_audio.cpp | 0 |  |  |
| `XeCryptSha` | `0x827DD31C` | impl | kernel/xboxkrnl/xboxkrnl_crypt.cpp | 0 |  |  |
| `XeKeysConsolePrivateKeySign` | `0x827DD32C` | impl | kernel/xboxkrnl/xboxkrnl_crypt.cpp | 0 |  |  |
| `XeKeysConsoleSignatureVerification` | `0x827DD30C` | impl | kernel/xboxkrnl/xboxkrnl_crypt.cpp | 0 |  |  |
| `XexCheckExecutablePrivilege` | `0x827DCA4C` | impl | kernel/xboxkrnl/xboxkrnl_modules.cpp | 1 | 0 |  |
| `XexGetModuleHandle` | `0x827DD00C` | impl | kernel/xboxkrnl/xboxkrnl_modules.cpp | 0 |  |  |
| `XexGetProcedureAddress` | `0x827DCB4C` | impl | kernel/xboxkrnl/xboxkrnl_modules.cpp | 6 | 0 |  |
| `XexLoadImage` | `0x827DCB5C` | impl | kernel/xboxkrnl/xboxkrnl_modules.cpp | 1 | 0 |  |
| `XexUnloadImage` | `0x827DCB3C` | impl | kernel/xboxkrnl/xboxkrnl_modules.cpp | 0 |  |  |
| `XGetAVPack` | `0x827DCA1C` | impl | kernel/xam/xam_info.cpp | 1 | 0 |  |
| `XGetGameRegion` | `0x827DC89C` | impl | kernel/xam/xam_info.cpp | 0 |  |  |
| `XGetLanguage` | `0x827DCA0C` | impl | kernel/xam/xam_info.cpp | 0 |  |  |
| `XGetVideoMode` | `0x827DCF7C` | impl | kernel/xam/xam_video.cpp | 6 | 0 |  |
| `XMACreateContext` | `0x827DD05C` | impl | kernel/xboxkrnl/xboxkrnl_audio_xma.cpp | 199 | 135 |  |
| `XMAReleaseContext` | `0x827DD06C` | impl | kernel/xboxkrnl/xboxkrnl_audio_xma.cpp | 199 | 201 |  |
| `XMsgCancelIORequest` | `0x827DC9AC` | impl | kernel/xam/xam_msg.cpp | 0 |  |  |
| `XMsgInProcessCall` | `0x827DCEAC` | impl | kernel/xam/xam_msg.cpp | 0 |  |  |
| `XMsgStartIORequest` | `0x827DC8BC` | impl | kernel/xam/xam_msg.cpp | 21 | 9 |  |
| `XNotifyGetNext` | `0x827DCEFC` | impl | kernel/xam/xam_notify.cpp | 6904 | 9 |  |
| `XNotifyPositionUI` | `0x827DC8AC` | impl | kernel/xam/xam_notify.cpp | 6890 | 9 |  |
