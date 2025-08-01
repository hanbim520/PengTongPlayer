// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FPengTongPlayerModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
    // This will store the handles to the loaded DLLs.
// #if PLATFORM_WINDOWS || WITH_EDITOR
//     TArray<void*> DllHandles;
// #endif
};
