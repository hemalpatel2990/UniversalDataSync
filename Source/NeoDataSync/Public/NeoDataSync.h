// Copyright 2025-2026 NeoNexus Studios. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FNeoDataSyncModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
