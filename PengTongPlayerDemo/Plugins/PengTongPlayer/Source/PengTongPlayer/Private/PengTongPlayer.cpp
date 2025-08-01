// Copyright Epic Games, Inc. All Rights Reserved.

#include "PengTongPlayer.h"

#define LOCTEXT_NAMESPACE "FPengTongPlayerModule"

void FPengTongPlayerModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
// #if PLATFORM_WINDOWS || WITH_EDITOR
//  // 1. Get the base directory of the project
//     FString ProjectBaseDir = FPaths::ProjectDir();
// 
//     // 2. Construct the path to our new DLL subdirectory
//     FString DllSubdirectory = FPaths::Combine(ProjectBaseDir, TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory(), TEXT("ThirdParty"), TEXT("FFmpeg"));
// 
//     // 3. Add our subdirectory to the OS's search path for DLLs.
//     // This is the crucial step that allows the engine to find the FFmpeg DLLs.
//     //FPlatformProcess::AddDllDirectory(*DllSubdirectory);
// 
//     TArray<FString> DllNames;
//     IFileManager::Get().FindFiles(DllNames, *DllSubdirectory, TEXT("*.dll"));
// 
// 
//     // 3. Manually load each found DLL and store its handle
//     UE_LOG(LogTemp, Log, TEXT("Attempting to load FFmpeg DLLs from: %s"), *DllSubdirectory);
//     for (const FString& DllName : DllNames)
//     {
//         const FString DllPath = FPaths::Combine(DllSubdirectory, DllName);
//         void* Handle = FPlatformProcess::GetDllHandle(*DllPath);
//         if (Handle != nullptr)
//         {
//             DllHandles.Add(Handle);
//             UE_LOG(LogTemp, Log, TEXT("Successfully loaded DLL: %s"), *DllPath);
//         }
//         else
//         {
//             // This log is crucial for debugging! It tells us exactly why a DLL failed to load.
//             uint32 ErrorCode = FPlatformMisc::GetLastError();
//             TCHAR ErrorMsg[1024];
//             FPlatformMisc::GetSystemErrorMessage(ErrorMsg, 1024, ErrorCode);
//             UE_LOG(LogTemp, Error, TEXT("Failed to load DLL: %s. Error Code: %d, Message: %s"), *DllPath, ErrorCode, ErrorMsg);
//         }
//     }
// #endif

}

void FPengTongPlayerModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
// #if PLATFORM_WINDOWS || WITH_EDITOR
// // Free the loaded DLLs in reverse order of loading
//     for (int32 i = DllHandles.Num() - 1; i >= 0; --i)
//     {
//         if (DllHandles[i] != nullptr)
//         {
//             FPlatformProcess::FreeDllHandle(DllHandles[i]);
//             DllHandles[i] = nullptr;
//         }
//     }
//     DllHandles.Empty();
// #endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPengTongPlayerModule, PengTongPlayer)