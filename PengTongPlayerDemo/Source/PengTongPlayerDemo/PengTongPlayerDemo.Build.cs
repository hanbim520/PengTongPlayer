// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PengTongPlayerDemo : ModuleRules
{
	public PengTongPlayerDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"PengTongPlayerDemo",
			"PengTongPlayerDemo/Variant_Platforming",
			"PengTongPlayerDemo/Variant_Combat",
			"PengTongPlayerDemo/Variant_Combat/AI",
			"PengTongPlayerDemo/Variant_SideScrolling",
			"PengTongPlayerDemo/Variant_SideScrolling/Gameplay",
			"PengTongPlayerDemo/Variant_SideScrolling/AI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
