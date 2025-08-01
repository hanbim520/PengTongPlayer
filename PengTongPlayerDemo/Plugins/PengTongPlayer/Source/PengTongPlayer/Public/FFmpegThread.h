#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include <string>

// Forward declarations for FFmpeg structs to avoid including FFmpeg headers here.
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

// This struct holds the raw pixel data for a single video frame.
// It's designed to be passed between threads safely using a TSharedPtr.
struct FVideoFrame
{
    TArray<uint8> Data;
    int32 Width;
    int32 Height;
};

// Our main worker thread class inheriting from FRunnable.
class FFmpegThread : public FRunnable
{
public:
    // Constructor: Takes the path to the media file.
    FFmpegThread(FString InMediaPath);
    virtual ~FFmpegThread();

    // FRunnable interface methods that we must implement.
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
    virtual void Exit() override;
    // 线程控制
    void Pause();
    void Resume();
    bool IsRunning() const;

    void Cleanup();

    bool StartThread();

    // ---- Public methods for the Game Thread to get data ----

    // Tries to get the next decoded video frame from the queue. Returns true if successful.
    bool GetNextVideoFrame(TSharedPtr<FVideoFrame, ESPMode::ThreadSafe>& OutFrame);

    // Tries to get the next decoded audio chunk from the queue. Returns true if successful.
    bool GetNextAudioFrame(TArray<uint8>& OutData);

    // Public properties that the component might need to read.
    FString MediaPath;
    int32 VideoWidth = 0;
    int32 VideoHeight = 0;

public:
    bool ResolveMediaPath(const FString& InPath, std::string& OutFFmpegPath)
    {
        // 1. 转换为绝对路径
        FString AbsolutePath;
        if (InPath.StartsWith(TEXT("/Game"))) {
            if (!FPackageName::TryConvertLongPackageNameToFilename(InPath, AbsolutePath)) {
                return false;
            }
            OutFFmpegPath = TCHAR_TO_UTF8(*(AbsolutePath + TEXT(".mp4"))) ;
            return true;
           // AbsolutePath = FPaths::ChangeExtension(AbsolutePath, TEXT("mp4"));
        }
        else {
            AbsolutePath = FPaths::ConvertRelativePathToFull(InPath);
        }

        // 2. 路径规范化
        FPaths::NormalizeFilename(AbsolutePath);
        FPaths::RemoveDuplicateSlashes(AbsolutePath);

        // 3. 回退检查
        if (!FPaths::FileExists(AbsolutePath)) {
            FString Fallback = FPaths::ProjectContentDir() / TEXT("Movies") / FPaths::GetCleanFilename(InPath);
            if (FPaths::FileExists(Fallback)) {
                AbsolutePath = Fallback;
            }
            else {
                return false;
            }
        }

        // 4. 转换为FFmpeg格式
        OutFFmpegPath = TCHAR_TO_UTF8(*AbsolutePath);
        return true;
    }
private:
    // A thread-safe boolean to control the running state of the loop in Run().
    FThreadSafeBool bIsRunning = false;

    // --- FFmpeg-related pointers ---
    AVFormatContext* FormatCtx = nullptr;
    AVCodecContext* VideoCodecCtx = nullptr;
    AVCodecContext* AudioCodecCtx = nullptr;
    SwsContext* SwsCtx = nullptr;      // For video frame conversion
    SwrContext* SwrCtx = nullptr;      // For audio sample conversion
    int VideoStreamIndex = -1;
    int AudioStreamIndex = -1;

    // --- Thread-safe queues for communication with the Game Thread ---
    // The video queue stores smart pointers to frame data.
    TQueue<TSharedPtr<FVideoFrame, ESPMode::ThreadSafe>> VideoFrameQueue;
    // The audio queue stores raw bytes of resampled audio.
    TQueue<TArray<uint8>> AudioFrameQueue;

    // A private helper function to release all FFmpeg resources.
   


    FRunnableThread* ThreadHandle = nullptr;
    FThreadSafeBool bStopThread;
    FThreadSafeBool bPauseThread;
};