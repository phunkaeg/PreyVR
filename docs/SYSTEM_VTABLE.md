# `CSystem` vtable and `gEnv` layout
A derived reference, not a promoted registry. It maps every `ISystem` virtual to its address in the
installed Steam `PreyDll.dll` (`7D6E322F…`, image base `0x180000000`). Registry entries R-040 to
R-044 carry the load-bearing subset; everything here is regenerable from the DLL plus
`ISystem.h`.

**How it was built.** `ISystem::AutoDetectSpec` is the one function in this cluster the Chairloader
PDB headers name *and* that could be identified in the Steam image independently, by its log
strings. Its slot index computed from the header, subtracted from its address, gives the vtable
base `0x1D9B9C8`.

**Counting rule.** Count only virtuals declared *directly* in `struct ISystem`. Two belong to the
nested `ILoadingProgressListener` and four sit inside `#if 0` blocks; including them shifts every
slot by two and silently produces wrong addresses.

## Why this table is trusted

Alignment is not assumed from the header — it is measured, three ways:

1. **Four slots predicted, then read.** `GetGlobalEnvironment` `+0x018`, `SetViewCamera` `+0x380`,
   `GetViewCamera` `+0x388`, `AutoDetectSpec` `+0x480`. Three were predicted before being read.
2. **Shape asymmetry.** Slots whose header name begins `Get`/`Is` land on a trivial accessor
   **74 times out of 99**; slots that do not, **2 times out of 94**. A misaligned table would show
   the same rate in both groups. The two exceptions are not counterexamples: they are
   `NeedDoWorkDuringOcclusionChecks` and `WasInDevMode`, predicate getters whose names simply do
   not begin with `Get` or `Is`. Every one of the 76 trivial accessors in the table is
   semantically a getter, with no counterexample anywhere.
3. **The decisive one — 24 independent name predictions.** Twenty-four accessors compile to
   `MOV RAX,[this+0x28]; MOV RAX,[RAX+d]; RET`, relaying through the `gEnv` pointer at
   `CSystem+0x28`. For each, `d` was checked against the member the header gives that name.
   **All 24 land on the correct member.** The two that look like misses are CryEngine's own
   aliases — `GetIAnimationSystem()` returns `ICharacterManager*` and `GetIPak()` returns
   `ICryPak*` — so they are correct too. This simultaneously confirms the vtable alignment and the
   entire `SSystemGlobalEnvironment` pointer layout.

Slots marked **[v]** were byte-verified individually. The rest are derived from an alignment
confirmed at 28 independent points; treat an unmarked address as high-confidence but re-check the
bytes before hooking one.

## `gEnv` — `SSystemGlobalEnvironment` at `PreyDll.dll+0x224D980`

Reached from `CSystem+0x28`. Pointer members, in declaration order:

| Offset | Member | Type |
| ---: | --- | --- |
| `+0x000` | `pDialogSystem` | `IDialogSystem*` |
| `+0x008` | `p3DEngine` | `I3DEngine*` |
| `+0x010` | `pNetwork` | `INetwork*` |
| `+0x018` | `pOnline` | `IOnline*` |
| `+0x020` | `pLobby` | `ICryLobby*` |
| `+0x028` | `pArkRewardSystem` | `IArkRewardSystem*` |
| `+0x030` | `pArkEntitlementSystem` | `IArkEntitlementSystem*` |
| `+0x038` | `pArkDlcSystem` | `IArkDlcSystem*` |
| `+0x040` | `pScriptSystem` | `IScriptSystem*` |
| `+0x048` | `pPhysicalWorld` | `IPhysicalWorld*` |
| `+0x050` | `pFlowSystem` | `IFlowSystem*` |
| `+0x058` | `pInput` | `IInput*` |
| `+0x060` | `pStatoscope` | `IStatoscope*` |
| `+0x068` | `pCryPak` | `ICryPak*` |
| `+0x070` | `pFileChangeMonitor` | `IFileChangeMonitor*` |
| `+0x078` | `pProfileLogSystem` | `IProfileLogSystem*` |
| `+0x080` | `pParticleManager` | `IParticleManager*` |
| `+0x088` | `pOpticsManager` | `IOpticsManager*` |
| `+0x090` | `pFrameProfileSystem` | `IFrameProfileSystem*` |
| `+0x098` | `pTimer` | `ITimer*` |
| `+0x0A0` | `pCryFont` | `ICryFont*` |
| `+0x0A8` | `pGame` | `IGame*` |
| `+0x0B0` | `pLocalMemoryUsage` | `ILocalMemoryUsage*` |
| `+0x0B8` | `pEntitySystem` | `IEntitySystem*` |
| `+0x0C0` | `pConsole` | `IConsole*` |
| `+0x0C8` | `pTelemetrySystem` | `Telemetry::ITelemetrySystem*` |
| `+0x0D0` | `pAudioSystem` | `IAudioSystem*` |
| `+0x0D8` | `pArkRoomVolumeManager` | `IArkRoomVolumeManager*` |
| `+0x0E0` | `pSystem` | `ISystem*` |
| `+0x0E8` | `pCharacterManager` | `ICharacterManager*` |
| `+0x0F0` | `pAISystem` | `IAISystem*` |
| `+0x0F8` | `pLog` | `ILog*` |
| `+0x100` | `pCodeCheckpointMgr` | `ICodeCheckpointMgr*` |
| `+0x108` | `pMovieSystem` | `IMovieSystem*` |
| `+0x110` | `pNameTable` | `INameTable*` |
| `+0x118` | `pVisualLog` | `IVisualLog*` |
| `+0x120` | `pRenderer` | `IRenderer*` |
| `+0x128` | `pAuxGeomRenderer` | `IRenderAuxGeom*` |
| `+0x130` | `pHardwareMouse` | `IHardwareMouse*` |
| `+0x138` | `pMaterialEffects` | `IMaterialEffects*` |
| `+0x140` | `pJobManager` | `JobManager::IJobManager*` |
| `+0x148` | `pOverloadSceneManager` | `IOverloadSceneManager*` |
| `+0x150` | `pFlashUI` | `IFlashUI*` |
| `+0x158` | `pServiceNetwork` | `IServiceNetwork*` |
| `+0x160` | `pRemoteCommandManager` | `IRemoteCommandManager*` |
| `+0x168` | `pSystemScheduler` | `ISystemScheduler*` |

## `ISystem` vtable at `PreyDll.dll+0x1D9B9C8` — 193 slots

| # | Slot | Symbol | RVA | Body / behaviour |
| ---: | ---: | --- | ---: | --- |
| 0 | `+0x000` | `~ISystem` | `0xDF0490` | `48895c240857...` |
| 1 | `+0x008` | `Release` | `0x12BC380` | `4885c9740b48...` |
| 2 | `+0x010` | `GetCVarsWhiteListConfigSink` **[v]** | `0xDF1430` | `MOV RAX,[this+0xF78]; RET` |
| 3 | `+0x018` | `GetGlobalEnvironment` **[v]** | `0x903CA0` | `MOV RAX,[this+0x28]; RET` |
| 4 | `+0x020` | `GetRootFolder` **[v]** | `0xDF2A60` | `MOV RAX,[this+0xD60]; RET` |
| 5 | `+0x028` | `Update` | `0xDF5640` | `488bc4488958...` |
| 6 | `+0x030` | `UpdateLoadtime` | `0xDF6B30` | `48895c240848...` |
| 7 | `+0x038` | `DoWorkDuringOcclusionChecks` | `0xDF0B00` | `40534883ec20...` |
| 8 | `+0x040` | `NeedDoWorkDuringOcclusionChecks` | `0xDF31D0` | `MOVZX EAX,byte[this+0x2DF1]; RET` |
| 9 | `+0x048` | `Render` | `0xE0BA30` | `41574881ecd0...` |
| 10 | `+0x050` | `RenderBegin` | `0xE0BD80` | `40534883ec20...` |
| 11 | `+0x058` | `RenderEnd` | `0xE0BE40` | `48896c241856...` |
| 12 | `+0x060` | `SynchronousLoadingTick` | `0xE0C080` | `40534883ec60...` |
| 13 | `+0x068` | `RenderStatistics` | `0xE0BFF0` | `40534883ec30...` |
| 14 | `+0x070` | `RenderPhysicsStatistics` | `0x1706520` | `c20000cccccc...` |
| 15 | `+0x078` | `GetProfileData` | `0xE0BA20` | `LEA RAX,[this+0xF90]; RET` |
| 16 | `+0x080` | `ClearProfileData` | `0xE0B540` | `488b81900f00...` |
| 17 | `+0x088` | `GetUsedMemory` | `0xDF2BA0` | `e95b6d0300cc...` |
| 18 | `+0x090` | `GetUserName` | `0xE0FCB0` | `40534881ec20...` |
| 19 | `+0x098` | `GetCPUFlags` | `0xDF1410` | `488b81000a00...` |
| 20 | `+0x0A0` | `GetLogicalCPUCount` | `0xDF2560` | `488b81000a00...` |
| 21 | `+0x0A8` | `DumpMemoryUsageStatistics` | `0xE0EE20` | `48895c241048...` |
| 22 | `+0x0B0` | `Quit` | `0xDF3C10` | `40534883ec20...` |
| 23 | `+0x0B8` | `Relaunch` | `0xDF3E60` | `48895c240857...` |
| 24 | `+0x0C0` | `IsQuitting` | `0xDF2D40` | `MOVZX EAX,byte[this+0x9C8]; RET` |
| 25 | `+0x0C8` | `IsShaderCacheGenMode` | `0xDF2D70` | `MOVZX EAX,byte[this+0x9C9]; RET` |
| 26 | `+0x0D0` | `SerializingFile` | `0xDF42A0` | `8991cc090000...` |
| 27 | `+0x0D8` | `IsSerializingFile` | `0xDF2D60` | `MOV EAX,[this+0x9CC]; RET` |
| 28 | `+0x0E0` | `IsRelaunch` | `0xDF2D50` | `MOVZX EAX,byte[this+0x9CA]; RET` |
| 29 | `+0x0E8` | `DisplayErrorMessage` | `0xE0B8B0` | `48895c240848...` |
| 30 | `+0x0F0` | `FatalError` | `0xE0F530` | `48895424104c...` |
| 31 | `+0x0F8` | `ReportBug` | `0xE106A0` | `48895424104c...` |
| 32 | `+0x100` | `OpenArkBugReporter` | `0x1706520` | `c20000cccccc...` |
| 33 | `+0x108` | `SetLastSaveFile` | `0xDF43C0` | `4881c1402e00...` |
| 34 | `+0x110` | `GetLastSaveFile` | `0xDF18D0` | `MOV RAX,[this+0x2E40]; RET` |
| 35 | `+0x118` | `WarningV` | `0xDF6C10` | `44894c242044...` |
| 36 | `+0x120` | `Warning` | `0xDF6BE0` | `4c8bdc4883ec...` |
| 37 | `+0x128` | `ShowMessage` | `0xDF4570` | `488b89680f00...` |
| 38 | `+0x130` | `CheckLogVerbosity` | `0xDF06D0` | `40534883ec20...` |
| 39 | `+0x138` | `IsUIFrameworkMode` | `0xDF2D90` | `MOVZX EAX,byte[this+0x9D5]; RET` |
| 40 | `+0x140` | `GetIZLibCompressor` | `0xDF18A0` | `MOV RAX,[this+0xD38]; RET` |
| 41 | `+0x148` | `GetIZLibDecompressor` | `0xDF18B0` | `MOV RAX,[this+0xD40]; RET` |
| 42 | `+0x150` | `GetLZ4Decompressor` | `0xDF18C0` | `MOV RAX,[this+0xD48]; RET` |
| 43 | `+0x158` | `GetPerfHUD` | `0xDF2A40` | `MOV RAX,[this+0xFD8]; RET` |
| 44 | `+0x160` | `GetPlatformOS` | `0xDF2A50` | `MOV RAX,[this+0xFD0]; RET` |
| 45 | `+0x168` | `GetINotificationNetwork` | `0xDF16E0` | `MOV RAX,[this+0x2DA8]; RET` |
| 46 | `+0x170` | `GetIHardwareMouse` | `0xDF1670` | relays `gEnv->pHardwareMouse` (gEnv+0x130) |
| 47 | `+0x178` | `GetIDialogSystem` | `0xDF1610` | relays `gEnv->pDialogSystem` (gEnv+0x0) |
| 48 | `+0x180` | `GetIFlowSystem` | `0xDF1650` | relays `gEnv->pFlowSystem` (gEnv+0x50) |
| 49 | `+0x188` | `GetIBudgetingSystem` | `0xDF15C0` | `MOV RAX,[this+0xD30]; RET` |
| 50 | `+0x190` | `GetINameTable` | `0xDF16C0` | relays `gEnv->pNameTable` (gEnv+0x110) |
| 51 | `+0x198` | `GetIDiskProfiler` | `0xDF1620` | `MOV RAX,[this+0xFC8]; RET` |
| 52 | `+0x1A0` | `GetIProfileSystem` | `0xDF1750` | `LEA RAX,[this+0xFB8]; RET` |
| 53 | `+0x1A8` | `GetIValidator` | `0xDF1880` | `MOV RAX,[this+0x9D8]; RET` |
| 54 | `+0x1B0` | `GetIPhysicsDebugRenderer` | `0xDF1730` | `MOV RAX,[this+0xAC0]; RET` |
| 55 | `+0x1B8` | `GetIPhysRenderer` | `0xDF1700` | `488b91c00a00...` |
| 56 | `+0x1C0` | `GetIAnimationSystem` | `0xDF1580` | relays `gEnv->pCharacterManager` (gEnv+0xE8) |
| 57 | `+0x1C8` | `GetStreamEngine` | `0xDF2A70` | `MOV RAX,[this+0xAA8]; RET` |
| 58 | `+0x1D0` | `GetICmdLine` | `0xDF15D0` | `MOV RAX,[this+0x2D80]; RET` |
| 59 | `+0x1D8` | `GetILog` | `0xDF1690` | relays `gEnv->pLog` (gEnv+0xF8) |
| 60 | `+0x1E0` | `GetIPak` | `0xDF16F0` | relays `gEnv->pCryPak` (gEnv+0x68) |
| 61 | `+0x1E8` | `GetICryFont` | `0xDF15F0` | relays `gEnv->pCryFont` (gEnv+0xA0) |
| 62 | `+0x1F0` | `GetIEntitySystem` | `0xDF1630` | relays `gEnv->pEntitySystem` (gEnv+0xB8) |
| 63 | `+0x1F8` | `GetIMemoryManager` | `0xDF16A0` | `MOV RAX,[this+0xAB8]; RET` |
| 64 | `+0x200` | `GetAISystem` | `0xDF13F0` | relays `gEnv->pAISystem` (gEnv+0xF0) |
| 65 | `+0x208` | `GetIMovieSystem` | `0xDF16B0` | relays `gEnv->pMovieSystem` (gEnv+0x108) |
| 66 | `+0x210` | `GetIPhysicalWorld` | `0xDF1720` | relays `gEnv->pPhysicalWorld` (gEnv+0x48) |
| 67 | `+0x218` | `GetIAudioSystem` | `0xDF15B0` | relays `gEnv->pAudioSystem` (gEnv+0xD0) |
| 68 | `+0x220` | `GetIArkRoomVolumeManager` | `0xDF15A0` | relays `gEnv->pArkRoomVolumeManager` (gEnv+0xD8) |
| 69 | `+0x228` | `GetI3DEngine` | `0xDF1570` | relays `gEnv->p3DEngine` (gEnv+0x8) |
| 70 | `+0x230` | `GetIScriptSystem` | `0xDF1810` | relays `gEnv->pScriptSystem` (gEnv+0x40) |
| 71 | `+0x238` | `GetIConsole` | `0xDF15E0` | relays `gEnv->pConsole` (gEnv+0xC0) |
| 72 | `+0x240` | `GetIRemoteConsole` | `0xDF1770` | `40534883ec20...` |
| 73 | `+0x248` | `GetIArkBethesdaNetManager` | `0xDF1590` | `MOV RAX,[this+0xFE8]; RET` |
| 74 | `+0x250` | `GetIResourceManager` | `0xDF1800` | `MOV RAX,[this+0x2D98]; RET` |
| 75 | `+0x258` | `GetIThreadTaskManager` | `0xDF1860` | `MOV RAX,[this+0x2D90]; RET` |
| 76 | `+0x260` | `GetIProfilingSystem` | `0xDF1760` | `LEA RAX,[this+0x1130]; RET` |
| 77 | `+0x268` | `GetISystemEventDispatcher` | `0xDF1820` | `MOV RAX,[this+0xD20]; RET` |
| 78 | `+0x270` | `GetIVisualLog` | `0xDF1890` | relays `gEnv->pVisualLog` (gEnv+0x118) |
| 79 | `+0x278` | `GetIFileChangeMonitor` | `0xDF1640` | relays `gEnv->pFileChangeMonitor` (gEnv+0x70) |
| 80 | `+0x280` | `GetHWND` | `0xDF1560` | `MOV RAX,[this+0xF80]; RET` |
| 81 | `+0x288` | `GetIGame` | `0xDF1660` | relays `gEnv->pGame` (gEnv+0xA8) |
| 82 | `+0x290` | `GetINetwork` | `0xDF16D0` | relays `gEnv->pNetwork` (gEnv+0x10) |
| 83 | `+0x298` | `GetIRenderer` | `0xDF17F0` | relays `gEnv->pRenderer` (gEnv+0x120) |
| 84 | `+0x2A0` | `GetIInput` | `0xDF1680` | relays `gEnv->pInput` (gEnv+0x58) |
| 85 | `+0x2A8` | `GetITimer` | `0xDF1870` | relays `gEnv->pTimer` (gEnv+0x98) |
| 86 | `+0x2B0` | `SetLoadingProgressListener` | `0xDF43F0` | `488991782d00...` |
| 87 | `+0x2B8` | `GetLoadingProgressListener` | `0xDF18E0` | `MOV RAX,[this+0x2D78]; RET` |
| 88 | `+0x2C0` | `SetIGame` | `0xDF4360` | `488b41284889...` |
| 89 | `+0x2C8` | `SetIFlowSystem` | `0xDF4350` | `488b41284889...` |
| 90 | `+0x2D0` | `SetIDialogSystem` | `0xDF4320` | `488b41284889...` |
| 91 | `+0x2D8` | `SetIMaterialEffects` | `0xDF4370` | `488b41284889...` |
| 92 | `+0x2E0` | `SetIParticleManager` | `0xDF4390` | `488b41284889...` |
| 93 | `+0x2E8` | `SetIOpticsManager` | `0xDF4380` | `488b41284889...` |
| 94 | `+0x2F0` | `SetIArkRoomVolumeManager` | `0xDF4310` | `488b41284889...` |
| 95 | `+0x2F8` | `SetIFileChangeMonitor` | `0xDF4330` | `488b41284889...` |
| 96 | `+0x300` | `SetIVisualLog` | `0xDF43B0` | `488b41284889...` |
| 97 | `+0x308` | `SetIFlashUI` | `0xDF4340` | `488b41284889...` |
| 98 | `+0x310` | `ChangeUserPath` | `0xE0D0A0` | `488bc455488d...` |
| 99 | `+0x318` | `DebugStats` | `0xE0E660` | `48895c241055...` |
| 100 | `+0x320` | `DumpWinHeaps` | `0xE0EF80` | `40554154488d...` |
| 101 | `+0x328` | `DumpMMStats` | `0xE0EDB0` | `40574881ec20...` |
| 102 | `+0x330` | `SetForceNonDevMode` | `0xDF42F0` | `8891e0090000...` |
| 103 | `+0x338` | `GetForceNonDevMode` | `0xDF1550` | `MOVZX EAX,byte[this+0x9E0]; RET` |
| 104 | `+0x340` | `WasInDevMode` | `0xDF72C0` | `MOVZX EAX,byte[this+0x9E1]; RET` |
| 105 | `+0x348` | `IsDevMode` | `0xDF2BE0` | `4883ec2880b9...` |
| 106 | `+0x350` | `IsMODValid` | `0xDF2C10` | `405341564883...` |
| 107 | `+0x358` | `CreateXmlNode` | `0xDF0AA0` | `48895c240848...` |
| 108 | `+0x360` | `LoadXmlFromBuffer` | `0xDF3100` | `40534883ec30...` |
| 109 | `+0x368` | `LoadXmlFromFile` | `0xDF3130` | `40534883ec30...` |
| 110 | `+0x370` | `GetXmlUtils` | `0xDF2BC0` | `MOV RAX,[this+0xD50]; RET` |
| 111 | `+0x378` | `GetArchiveHost` | `0xDF1400` | `MOV RAX,[this+0xD58]; RET` |
| 112 | `+0x380` | `SetViewCamera` **[v]** | `0xDF4560` | `4881c1880700...` |
| 113 | `+0x388` | `GetViewCamera` **[v]** | `0xDF2BB0` | `LEA RAX,[this+0x788]; RET` |
| 114 | `+0x390` | `IgnoreUpdates` | `0xDF2BD0` | `8891d7090000...` |
| 115 | `+0x398` | `SetIProcess` | `0xDF43A0` | `488991b00a00...` |
| 116 | `+0x3A0` | `GetIProcess` | `0xDF1740` | `MOV RAX,[this+0xAB0]; RET` |
| 117 | `+0x3A8` | `IsTestMode` | `0xDF2D80` | `MOVZX EAX,byte[this+0x9D0]; RET` |
| 118 | `+0x3B0` | `SetFrameProfiler` | `0x1706520` | `c20000cccccc...` |
| 119 | `+0x3B8` | `StartLoadingSectionProfiling` | `0x597420` | `33c0c3cccccc...` |
| 120 | `+0x3C0` | `EndLoadingSectionProfiling` | `0x1706520` | `c20000cccccc...` |
| 121 | `+0x3C8` | `StartBootSectionProfiler` | `0x597420` | `33c0c3cccccc...` |
| 122 | `+0x3D0` | `StopBootSectionProfiler` | `0x1706520` | `c20000cccccc...` |
| 123 | `+0x3D8` | `StartBootProfilerSession` | `0x1706520` | `c20000cccccc...` |
| 124 | `+0x3E0` | `StopBootProfilerSession` | `0x1706520` | `c20000cccccc...` |
| 125 | `+0x3E8` | `OutputLoadingTimeStats` | `0x1706520` | `c20000cccccc...` |
| 126 | `+0x3F0` | `GetLoadingProfilerCallstack` | `0x597420` | `33c0c3cccccc...` |
| 127 | `+0x3F8` | `GetFileVersion` | `0xDF8230` | `LEA RAX,[this+0xFF0]; RET` |
| 128 | `+0x400` | `GetProductVersion` | `0xDF8240` | `LEA RAX,[this+0x1000]; RET` |
| 129 | `+0x408` | `GetBuildVersion` | `0xDF8220` | `LEA RAX,[this+0x1010]; RET` |
| 130 | `+0x410` | `GetBuildInfo` | `0xDF8210` | `LEA RAX,[this+0x1020]; RET` |
| 131 | `+0x418` | `AddRuntimeBuildInfo` | `0xDF81C0` | `48895c240848...` |
| 132 | `+0x420` | `WriteCompressedFile` | `0xD89EB0` | `48895c240844...` |
| 133 | `+0x428` | `ReadCompressedFile` | `0xD89E30` | `48895c240857...` |
| 134 | `+0x430` | `GetCompressedFileSizeA` | `0xD89C30` | `405355574881...` |
| 135 | `+0x438` | `CompressDataBlock` | `0xD89BB0` | `40534883ec30...` |
| 136 | `+0x440` | `DecompressDataBlock` | `0xD89BF0` | `40534883ec20...` |
| 137 | `+0x448` | `GetIDataProbe` | `0xDF1600` | `MOV RAX,[this+0x10B0]; RET` |
| 138 | `+0x450` | `GetMappedPathLocation` | `0xDF2570` | `48895c241048...` |
| 139 | `+0x458` | `SaveConfiguration` | `0x1706520` | `c20000cccccc...` |
| 140 | `+0x460` | `LoadConfiguration` | `0xDF8250` | `4885d20f8434...` |
| 141 | `+0x468` | `GetConfigSpec` | `0xDF1440` | `84d27419488b...` |
| 142 | `+0x470` | `GetMaxConfigSpec` | `0xDF2780` | `MOV EAX,[this+0x1124]; RET` |
| 143 | `+0x478` | `SetConfigSpec` | `0xDF42B0` | `4584c0741348...` |
| 144 | `+0x480` | `AutoDetectSpec` **[v]** | `0xD870C0` | `885424104889...` |
| 145 | `+0x488` | `SetThreadState` | `0xDF4530` | `83fa02751a48...` |
| 146 | `+0x490` | `CreateSizer` | `0xDF0A70` | `4883ec28b948...` |
| 147 | `+0x498` | `IsPaused` | `0xDF2D30` | `MOVZX EAX,byte[this+0x2D3C]; RET` |
| 148 | `+0x4A0` | `GetLocalizationManager` | `0xDF18F0` | `MOV RAX,[this+0x10B8]; RET` |
| 149 | `+0x4A8` | `CreateFlashPlayerInstance` | `0xE88F10` | `4883ec28b990...` |
| 150 | `+0x4B0` | `CreateFlashPlayerBootStrapper` | `0xE88E40` | `40534883ec20...` |
| 151 | `+0x4B8` | `SetFlashLoadMovieHandler` | `0xE8D650` | `48891511bdc8...` |
| 152 | `+0x4C0` | `GetFlashProfileResults` | `0xE8A450` | `c702000080bf...` |
| 153 | `+0x4C8` | `GetFlashMemoryUsage` | `0xE8A430` | `40534883ec20...` |
| 154 | `+0x4D0` | `ResetFlashMeshCache` | `0xE8C8D0` | `4883ec28e8f7...` |
| 155 | `+0x4D8` | `GFxAmpEnable` | `0x1706520` | `c20000cccccc...` |
| 156 | `+0x4E0` | `GFxAmpAdvanceFrame` | `0x1706520` | `c20000cccccc...` |
| 157 | `+0x4E8` | `ResetFlashDirtyState` | `0xE8C8B0` | `4883ec28e887...` |
| 158 | `+0x4F0` | `CreateAVIReader` | `0xD889E0` | `40534883ec20...` |
| 159 | `+0x4F8` | `ReleaseAVIReader` | `0xD88EB0` | `4c8bc24885d2...` |
| 160 | `+0x500` | `GetITextModeConsole` | `0xDF1840` | `80b9d6090000...` |
| 161 | `+0x508` | `GetNoiseGen` | `0xDF2800` | `40534883ec70...` |
| 162 | `+0x510` | `GetUpdateCounter` | `0xDF2A90` | `MOV RAX,[this+0x2D40]; RET` |
| 163 | `+0x518` | `GetCryFactoryRegistry` | `0xE9A6B0` | `488d05f9d43d...` |
| 164 | `+0x520` | `RegisterErrorObserver` | `0xE09010` | `488954241048...` |
| 165 | `+0x528` | `UnregisterErrorObserver` | `0xE0AFF0` | `40534883ec20...` |
| 166 | `+0x530` | `OnAssert` | `0xE05600` | `405541564157...` |
| 167 | `+0x538` | `OnScriptWarning` | `0xE05D30` | `48895c240848...` |
| 168 | `+0x540` | `IsAssertDialogVisible` | `0xE048E0` | `MOVZX EAX,byte[this+0x2DF8]; RET` |
| 169 | `+0x548` | `SetAssertVisible` | `0xE0A030` | `8891f82d0000...` |
| 170 | `+0x550` | `GetApplicationInstance` | `0xE0F860` | `41564883ec40...` |
| 171 | `+0x558` | `GetCurrentUpdateTimeStats` | `0xDF1470` | `8b81382d0000...` |
| 172 | `+0x560` | `GetUpdateTimeStats` | `0xDF2B80` | `8b81382d0000...` |
| 173 | `+0x568` | `ClearErrorMessages` | `0xDF0700` | `4881c1e02d00...` |
| 174 | `+0x570` | `GetIDebugCallstack` | `0xE0FCA0` | `e96b8ef9ffcc...` |
| 175 | `+0x578` | `debug_LogCallStack` | `0xE10D90` | `48895c240848...` |
| 176 | `+0x580` | `GetITestSystem` | `0xDF1830` | `MOV RAX,[this+0x2D88]; RET` |
| 177 | `+0x588` | `ExecuteCommandLine` | `0xDF1110` | `405541544156...` |
| 178 | `+0x590` | `GetUpdateStats` | `0xDF2AA0` | `4883ec18488b...` |
| 179 | `+0x598` | `DumpMemoryCoverage` | `0xDF1100` | `4881c1d82d00...` |
| 180 | `+0x5A0` | `GetSystemGlobalState` | `0xDF2A80` | `MOV EAX,[this+0x2D70]; RET` |
| 181 | `+0x5A8` | `SetSystemGlobalState` | `0xDF4400` | `48895c241057...` |
| 182 | `+0x5B0` | `AddPlatformOSCreateFlag` | `0xDF0500` | `08913d2d0000...` |
| 183 | `+0x5B8` | `AsyncMemcpy` | `0xDF0510` | `4053488b4424...` |
| 184 | `+0x5C0` | `GetLevelEncrypter` | `0x597420` | `33c0c3cccccc...` |
| 185 | `+0x5C8` | `GetEvaluationManager` | `0x597420` | `33c0c3cccccc...` |
| 186 | `+0x5D0` | `GetDeveloperName` | `0x597420` | `33c0c3cccccc...` |
| 187 | `+0x5D8` | `GetCVarsWhiteList` | `0x5E63E0` | `MOV RAX,[this+0xF70]; RET` |
| 188 | `+0x5E0` | `OnPLMEvent` | `0xDF3640` | `40534883ec20...` |
| 189 | `+0x5E8` | `SteamInit` | `0xDF5320` | `40534883ec20...` |
| 190 | `+0x5F0` | `InitializeEngineModule` | `0xE04480` | `488bc44c8940...` |
| 191 | `+0x5F8` | `UnloadEngineModule` | `0xE0A690` | `48895c240848...` |
| 192 | `+0x600` | `OverridePathMappings` | `0xDF3830` | `405553565741...` |
