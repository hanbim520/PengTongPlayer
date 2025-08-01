// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class PengTongPlayer : ModuleRules
{
    private string UProjectPath
    {
        get { return Directory.GetParent(ModuleDirectory).Parent.FullName; }
    }

    public PengTongPlayer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
            new string[] {
				// ... add public include paths required here ...
			}
            );


        PrivateIncludePaths.AddRange(
            new string[] {
				// ... add other private include paths required here ...
			}
            );


        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "UMG",
                "AudioMixer",
				// ... add other public dependencies that you statically link with here ...
			}
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "RenderCore",
                "RHI",
				// ... add private dependencies that you statically link with here ...	
			}
            );
    
    // --- FFmpeg Integration ---
    // Get the base path for the ThirdParty directory
    string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/FFmpeg"));

        // Platform-specific library logic
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // 1. Add Include Paths
            // All FFmpeg headers are in subdirectories under this main include path.
            // The headers themselves use relative paths (e.g., #include "libavutil/frame.h"),
            // so we only need to add the root include directory.
            string IncludePath = Path.Combine(ThirdPartyPath, "include");
            PublicIncludePaths.Add(IncludePath);
            System.Console.WriteLine("FFmpeg Include Path: " + IncludePath);

            // 2. Add Library Path and Link Static Libraries (.lib)
            string LibraryPath = Path.Combine(ThirdPartyPath, "lib", "Win64");
            PublicAdditionalLibraries.AddRange(new string[]
            {
                Path.Combine(LibraryPath, "avformat.lib"),
                Path.Combine(LibraryPath, "avcodec.lib"),
                Path.Combine(LibraryPath, "avutil.lib"),
                Path.Combine(LibraryPath, "swscale.lib"),
                Path.Combine(LibraryPath, "swresample.lib"),
                Path.Combine(LibraryPath, "avdevice.lib"),
                Path.Combine(LibraryPath, "avfilter.lib"),
               // Path.Combine(LibraryPath, "SDL2.lib") // You were linking this, so it's kept.
			});

            // 3. Handle Runtime Dependencies (.dll)
            // These DLLs must be copied to the output binaries folder (e.g., Binaries/Win64)
            // so they can be found when the application runs.
            string BinariesPath = Path.Combine(LibraryPath, "bin");
            System.Console.WriteLine("Searching for DLLs in: " + BinariesPath);


            if (Directory.Exists(BinariesPath))
            {
                // Find all .dll files in the binaries directory
                foreach (string DllPath in Directory.GetFiles(BinariesPath, "*.dll"))
                {
                    string DllName = Path.GetFileName(DllPath);
                    // Add the DLL to be delay-loaded by the engine.
                    PublicDelayLoadDLLs.Add(DllName);
                    // **FIX:** Use a more explicit overload for RuntimeDependencies.
                    // This tells UBT to copy the source file (DllPath) to the output directory.
                    // This is more reliable for ensuring the DLL is where it needs to be.
                    //                     RuntimeDependencies.Add(DllPath);
                    //                     System.Console.WriteLine($"Staging runtime dependency for packaged build: {DllPath}");
                    string ProjectBinariesPath = Path.Combine(Target.ProjectFile.Directory.FullName, "Binaries", Target.Platform.ToString());
                    //string Destination = Path.Combine(ProjectBinariesPath, "ThirdParty/FFmpeg", DllName);
                    string Destination = Path.Combine(ProjectBinariesPath,  DllName);

                    RuntimeDependencies.Add(Destination, DllPath, StagedFileType.NonUFS);

                    System.Console.WriteLine($"Staging runtime dependency: {DllPath} -> {Destination}");
                }
            }
            else
            {
                // Add a warning to the build log if the directory doesn't exist.
                System.Console.WriteLine("Warning: FFmpeg binaries directory not found: " + BinariesPath);
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            // For Android, you need to specify the path to the shared libraries (.so)
            // and use an Android Plugin Language (APL) file to package them.
            string LibraryPath = Path.Combine(ThirdPartyPath, "lib", "Android", "arm64-v8a"); // Example for arm64
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libavformat.so"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libavcodec.so"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libavutil.so"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libswscale.so"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libswresample.so"));

            // The APL file tells the build system how to package the .so files.
            // Place FFmpeg_APL.xml in the 'Private' directory.
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(ModuleDirectory, "Private", "FFmpeg_APL.xml"));
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            // For iOS, you link against static libraries (.a)
            string LibraryPath = Path.Combine(ThirdPartyPath, "lib", "IOS");
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libavformat.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libavcodec.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libavutil.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libswscale.a"));
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libswresample.a"));

            // iOS also requires linking against system frameworks used by FFmpeg
            PublicFrameworks.AddRange(new string[] { "VideoToolbox", "AudioToolbox", "CoreMedia" });
            PublicSystemLibraries.AddRange(new string[] { "bz2", "z", "iconv" });
        }


        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
