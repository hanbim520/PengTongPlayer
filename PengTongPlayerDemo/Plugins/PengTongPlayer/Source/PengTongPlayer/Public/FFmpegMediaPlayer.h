#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/Runnable.h"
#include <functional>

// Standard C++ libraries
#include <string>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "HAL/CriticalSection.h" 
#include "Runtime/Engine/Classes/Components/AudioComponent.h"
// Forward declarations for Unreal Engine types
class UTexture2D;
class USoundWaveProcedural;

// FFmpeg library headers
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "libavutil/channel_layout.h" 
}

#include "FFmpegMediaPlayer.generated.h"

class FFFmpegRunnable : public FRunnable
{
public:
    FFFmpegRunnable(const FString& ThreadName, TFunction<void()> InWork)
        : Work(InWork), Thread(nullptr) // This initializer list is now valid
    {
        Thread = FRunnableThread::Create(this, *ThreadName);
    }

    virtual ~FFFmpegRunnable() override
    {
        if (Thread)
        {
            Thread->Kill(true);
            delete Thread;
            Thread = nullptr;
        }
    }

    virtual uint32 Run() override
    {
        if (Work) { Work(); }
        return 0;
    }

private:
    // **FIX:** Member variables must be declared before use.
    TFunction<void()> Work;
    FRunnableThread* Thread;
};


struct FAudioSpec
{
    AVChannelLayout ChannelLayout; // Use the new struct type
    int32 SampleRate = 0;
    AVSampleFormat SampleFormat = AV_SAMPLE_FMT_NONE;

    // Default constructor to initialize the layout properly
    FAudioSpec()
    {
        av_channel_layout_default(&ChannelLayout, 0);
    }

    // Destructor to clean up the layout
    ~FAudioSpec()
    {
        av_channel_layout_uninit(&ChannelLayout);
    }
};

// Player State Enum
UENUM(BlueprintType)
enum class EPlayerState : uint8
{
    Stopped UMETA(DisplayName = "Stopped"),
    Playing UMETA(DisplayName = "Playing"),
    Paused  UMETA(DisplayName = "Paused"),
    Finished UMETA(DisplayName = "Finished")
};


UCLASS(BlueprintType)
class PENGTONGPLAYER_API UFFmpegMediaPlayer : public UObject
{
    GENERATED_BODY()

public:
    UFFmpegMediaPlayer();
    ~UFFmpegMediaPlayer();

    // --- Public Blueprint API ---

    /**
     * Opens a media file from the given path and prepares it for playback.
     * @param FilePath The absolute path to the media file.
     * @return True if the file was opened successfully, false otherwise.
     */
    UFUNCTION(BlueprintCallable, Category = "FFmpegMediaPlayer")
    bool SetMedia(const FString& FilePath);

    /** Starts or resumes playback. */
    UFUNCTION(BlueprintCallable, Category = "FFmpegMediaPlayer")
    void Play();

    /** Pauses playback. */
    UFUNCTION(BlueprintCallable, Category = "FFmpegMediaPlayer")
    void Pause();

    /** Pauses playback. */
    UFUNCTION(BlueprintCallable, Category = "FFmpegMediaPlayer")
    void Resume();

    /** Stops playback and releases resources. */
    UFUNCTION(BlueprintCallable, Category = "FFmpegMediaPlayer")
    void Stop();

    /** Returns the current state of the player. */
    UFUNCTION(BlueprintPure, Category = "FFmpegMediaPlayer")
    EPlayerState GetPlayerState() const;

    /** Returns the dynamic texture that displays the video frames. */
    UFUNCTION(BlueprintPure, Category = "FFmpegMediaPlayer")
    UTexture2D* GetVideoTexture() const;

    /** Returns the procedural sound wave for audio playback. */
    UFUNCTION(BlueprintPure, Category = "FFmpegMediaPlayer")
    USoundWaveProcedural* GetAudioSoundWave() const;

    UPROPERTY()
    TObjectPtr<UAudioComponent> m_AudioComponent;

    /** Gets the video dimensions. */
    UFUNCTION(BlueprintPure, Category = "FFmpegMediaPlayer")
    FIntPoint GetVideoDimensions() const;

    virtual void BeginDestroy() override;

private:
    // --- Thread-Safe Queue ---
    template <typename T>
    class TSafeQueue {
    public:
        void Push(T value) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(std::move(value));
            m_cond.notify_one();
        }

        bool Pop(T& value) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [&] { return !m_queue.empty() || bStopRequested; });
            if (bStopRequested && m_queue.empty()) return false;
            value = std::move(m_queue.front());
            m_queue.pop();
            return true;
        }

        void Stop() {
            std::lock_guard<std::mutex> lock(m_mutex);
            bStopRequested = true;
            m_cond.notify_all();
        }

        size_t Size() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

    private:
        std::queue<T> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cond;
        bool bStopRequested = false;
    };

    // --- Thread Functions ---
    void DemuxerThread();
    void VideoDecodeThread();
    void AudioDecodeThread();
    void InitSwrContext(const AVFrame* InFrame);

    // --- Helper Functions ---
    void Cleanup();
    double GetAudioClock();
    void UpdateTexture(const AVFrame* RgbFrame);

    // --- Player State & Control ---
    std::atomic<EPlayerState> m_PlayerState;
    std::atomic<bool> m_bStopThreads;

    // --- Threads ---
    std::thread m_DemuxerThread;
    std::thread m_VideoDecodeThread;
    std::thread m_AudioDecodeThread;



    // --- Threads ---
    TUniquePtr<FFFmpegRunnable> m_DemuxerRunnable;
    TUniquePtr<FFFmpegRunnable> m_VideoRunnable;
    TUniquePtr<FFFmpegRunnable> m_AudioRunnable;

    // --- FFmpeg Contexts & Streams ---
    AVFormatContext* m_FormatCtx = nullptr;
    AVCodecContext* m_VideoCodecCtx = nullptr;
    AVCodecContext* m_AudioCodecCtx = nullptr;
    AVStream* m_VideoStream = nullptr;
    AVStream* m_AudioStream = nullptr;
    SwsContext* m_SwsCtx = nullptr;
    SwrContext* m_SwrCtx = nullptr;
    int m_VideoStreamIndex = -1;
    int m_AudioStreamIndex = -1;
    double m_AudioClock = 0.0;

    int32 AudioNumChannels = 0;
    // --- Packet Queues ---
    TSafeQueue<AVPacket*> m_VideoPacketQueue;
    TSafeQueue<AVPacket*> m_AudioPacketQueue;

    // --- Unreal Engine Objects ---
    UPROPERTY(Transient)
    UTexture2D* m_VideoTexture = nullptr;

    UPROPERTY(Transient)
    USoundWaveProcedural* m_AudioSoundWave = nullptr;

    // --- Video Frame Data ---
    TArray<uint8> m_FrameData;
    TArray<uint8> FrameDataCopy;
    FIntPoint m_VideoDimensions;

    FAudioSpec m_LastAudioSpec;

    mutable FCriticalSection FFmpegResourceCriticalSectionSound;
    mutable FCriticalSection FFmpegResourceCriticalSectionVedio;

};