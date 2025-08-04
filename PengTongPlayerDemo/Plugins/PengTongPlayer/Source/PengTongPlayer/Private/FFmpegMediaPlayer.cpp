#include "FFmpegMediaPlayer.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWaveProcedural.h"
#include "Async/Async.h" // For AsyncTask to run on GameThread
#include "HAL/Platform.h" // For FPlatformProcess::Sleep
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"
#include "RHICommandList.h" 
#include "RenderingThread.h"
#include "AudioDeviceHandle.h"
#include "AudioDevice.h"

// --- Constructor & Destructor ---

UFFmpegMediaPlayer::UFFmpegMediaPlayer()
{
    m_PlayerState = EPlayerState::Stopped;
    m_bStopThreads = false;
    m_AudioComponent = nullptr;
}

UFFmpegMediaPlayer::~UFFmpegMediaPlayer()
{

}

void UFFmpegMediaPlayer::BeginDestroy()
{
    Super::BeginDestroy();

    Stop();
}
// --- Public API Implementation ---

bool UFFmpegMediaPlayer::SetMedia(const FString& FilePath)
{
    Stop();

    m_bStopThreads = false;

    if (m_PlayerState != EPlayerState::Stopped)
    {
        UE_LOG(LogTemp, Warning, TEXT("FFmpegMediaPlayer: Player is already open. Please Stop() it first."));
        return false;
    }
    FString PackagePath;
    if (!FPackageName::TryConvertLongPackageNameToFilename(FilePath, PackagePath))
    {
        UE_LOG(LogTemp, Error, TEXT("FFmpegMediaPlayer: Could not open file %s"), *FilePath);
        return false;
    }
    PackagePath += TEXT(".mp4"); // Ensure the file extension is correct
    // Convert FString to const char* for FFmpeg
    const std::string FilePathStd = TCHAR_TO_UTF8(*PackagePath);

    // 1. Open Input File
    if (avformat_open_input(&m_FormatCtx, FilePathStd.c_str(), nullptr, nullptr) != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FFmpegMediaPlayer: Could not open file %s"), *FilePath);
        return false;
    }

    // 2. Find Stream Info
    if (avformat_find_stream_info(m_FormatCtx, nullptr) < 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FFmpegMediaPlayer: Could not find stream information."));
        Cleanup();
        return false;
    }

    // 3. Find Video and Audio Streams and Decoders
    for (unsigned int i = 0; i < m_FormatCtx->nb_streams; ++i)
    {
        AVStream* stream = m_FormatCtx->streams[i];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) continue;

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_VideoStreamIndex < 0)
        {
            m_VideoStreamIndex = i;
            m_VideoStream = stream;
            m_VideoCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_VideoCodecCtx, stream->codecpar);
            avcodec_open2(m_VideoCodecCtx, codec, nullptr);
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_AudioStreamIndex < 0)
        {
            m_AudioStreamIndex = i;
            m_AudioStream = stream;
            m_AudioCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_AudioCodecCtx, stream->codecpar);
            avcodec_open2(m_AudioCodecCtx, codec, nullptr);
        }
    }

    if (m_VideoStreamIndex == -1 || m_AudioStreamIndex == -1)
    {
        UE_LOG(LogTemp, Error, TEXT("FFmpegMediaPlayer: Could not find both video and audio streams."));
        Cleanup();
        return false;
    }

    m_VideoDimensions = FIntPoint(m_VideoCodecCtx->width, m_VideoCodecCtx->height);

    // 4. Setup Video Texture and SwsContext
    m_VideoTexture = UTexture2D::CreateTransient(m_VideoCodecCtx->width, m_VideoCodecCtx->height, PF_B8G8R8A8);
    m_VideoTexture->UpdateResource();
    m_SwsCtx = sws_getContext(m_VideoCodecCtx->width, m_VideoCodecCtx->height, m_VideoCodecCtx->pix_fmt,
        m_VideoCodecCtx->width, m_VideoCodecCtx->height, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    // 5. Setup Audio SoundWave and SwrContext
    m_AudioSoundWave = NewObject<USoundWaveProcedural>();
    m_AudioSoundWave->SetSampleRate(m_AudioCodecCtx->sample_rate);
    m_AudioSoundWave->NumChannels = m_AudioCodecCtx->ch_layout.nb_channels;
    m_AudioSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
    m_AudioSoundWave->SoundGroup = SOUNDGROUP_Music;
    m_AudioSoundWave->bLooping = false;

    if (UWorld* World = GetWorld())
    {
        // *** CORRECCIÓN: Crear y almacenar el componente de audio en lugar de llamar a PlaySound2D directamente ***
        m_AudioComponent = UGameplayStatics::CreateSound2D(World, m_AudioSoundWave, 1.0f, 1.0f, 0.0f, nullptr, true);
    }

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&m_SwrCtx, &out_ch_layout, AV_SAMPLE_FMT_S16, m_AudioCodecCtx->sample_rate,
        &m_AudioCodecCtx->ch_layout, m_AudioCodecCtx->sample_fmt, m_AudioCodecCtx->sample_rate, 0, nullptr);
    swr_init(m_SwrCtx);
    m_AudioSoundWave->NumChannels = out_ch_layout.nb_channels; // Update channels based on output layout
    AudioNumChannels = m_AudioSoundWave->NumChannels;

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_VideoCodecCtx->width, m_VideoCodecCtx->height, 32);
    m_FrameData.SetNumUninitialized(num_bytes);
    FrameDataCopy.SetNumUninitialized(m_FrameData.Num());

   // UGameplayStatics::PlaySound2D(GetWorld(), m_AudioSoundWave, 5.0f);

    // 6. Start threads
    m_bStopThreads = false;
//     m_DemuxerThread = std::thread(&UFFmpegMediaPlayer::DemuxerThread, this);
//     m_VideoDecodeThread = std::thread(&UFFmpegMediaPlayer::VideoDecodeThread, this);
//     m_AudioDecodeThread = std::thread(&UFFmpegMediaPlayer::AudioDecodeThread, this);


    

    UE_LOG(LogTemp, Log, TEXT("FFmpegMediaPlayer: Opened %s successfully."), *FilePath);

    return true;
}

void UFFmpegMediaPlayer::Play()
{
    if (m_PlayerState == EPlayerState::Paused)
    {
        UE_LOG(LogTemp, Log, TEXT("Player RESUMED"));
    }
    else
    {
        Stop();
    }
    m_PlayerState = EPlayerState::Playing;

    if (m_AudioComponent)
    {
        m_AudioComponent->Play();
    }


    // --- 使用 FRunnable 启动线程 ---
    m_DemuxerRunnable = MakeUnique<FFFmpegRunnable>(TEXT("FFmpegDemuxerThread"), [this]() { this->DemuxerThread(); });
    m_AudioRunnable = MakeUnique<FFFmpegRunnable>(TEXT("FFmpegAudioThread"), [this]() { this->AudioDecodeThread(); });
    m_VideoRunnable = MakeUnique<FFFmpegRunnable>(TEXT("FFmpegVideoThread"), [this]() { this->VideoDecodeThread(); });
    
    
}

void UFFmpegMediaPlayer::Pause()
{
    if (m_PlayerState == EPlayerState::Playing)
    {
        m_PlayerState = EPlayerState::Paused;
        UE_LOG(LogTemp, Log, TEXT("Player PAUSED"));
    }
    if (IsValid(m_AudioSoundWave))
    {
        if (m_AudioComponent && m_AudioComponent->IsPlaying())
        {
            m_AudioComponent->SetPaused(true);
        }

    }
}


void UFFmpegMediaPlayer::Resume()
{
    if (m_PlayerState == EPlayerState::Paused)
    {
        m_PlayerState = EPlayerState::Playing;
        UE_LOG(LogTemp, Log, TEXT("Player Playing"));
    }

    if (m_AudioComponent)
    {
        m_AudioComponent->SetPaused(false);
    }
//     if (!GEngine || !GEngine->UseSound())
//     {
//         return;
//     }
// 
//     UWorld* ThisWorld = GetWorld();
// 
//     if (ThisWorld && ThisWorld->GetAudioDevice())
//     {
//         // Stop the sound wave from playing
//         ThisWorld->GetAudioDevice()->PauseActiveSound(m_AudioSoundWave);
//     }

}

void UFFmpegMediaPlayer::Stop()
{
    if (m_PlayerState == EPlayerState::Stopped)
    {
        return;
    }

    m_bStopThreads = true;
    m_PlayerState = EPlayerState::Stopped;

    m_VideoPacketQueue.Stop();
    m_AudioPacketQueue.Stop();



    if (m_AudioSoundWave && IsValid(m_AudioSoundWave))
    {
        if (!GEngine || !GEngine->UseSound())
        {
            return;
        }

        UWorld* ThisWorld = GetWorld();
        
        if (ThisWorld && ThisWorld->GetAudioDevice())
        {
            // Stop the sound wave from playing
            ThisWorld->GetAudioDevice()->StopSoundsUsingResource(m_AudioSoundWave);
        }
       
        m_AudioSoundWave->ResetAudio();
        m_AudioSoundWave->ConditionalBeginDestroy();
        m_AudioSoundWave = nullptr;
    }

//     auto SafeJoinThread = [](std::thread& Thread) {
//         if (Thread.joinable()) {
//             if (Thread.get_id() != std::this_thread::get_id()) {
//                 Thread.join();
//             }
//             else {
//                 Thread.detach();
//             }
//         }
//         };
// 
//     SafeJoinThread(m_DemuxerThread);
//     SafeJoinThread(m_VideoDecodeThread);
//     SafeJoinThread(m_AudioDecodeThread);


    m_DemuxerRunnable.Reset();
    m_VideoRunnable.Reset();
    m_AudioRunnable.Reset();
    // 
    //     if (m_DemuxerThread.joinable()) m_DemuxerThread.join();
    //     if (m_VideoDecodeThread.joinable()) m_VideoDecodeThread.join();
    //     if (m_AudioDecodeThread.joinable()) m_AudioDecodeThread.join();

    Cleanup();
    UE_LOG(LogTemp, Log, TEXT("FFmpegMediaPlayer: Player Closed."));
}

EPlayerState UFFmpegMediaPlayer::GetPlayerState() const
{
    return m_PlayerState;
}

UTexture2D* UFFmpegMediaPlayer::GetVideoTexture() const
{
    return m_VideoTexture;
}

USoundWaveProcedural* UFFmpegMediaPlayer::GetAudioSoundWave() const
{
    return m_AudioSoundWave;
}

FIntPoint UFFmpegMediaPlayer::GetVideoDimensions() const
{
    return m_VideoDimensions;
}


// --- Private Worker Threads & Helpers ---

void UFFmpegMediaPlayer::DemuxerThread()
{
    while (!m_bStopThreads.load(std::memory_order_acquire))
    {
        if (m_PlayerState == EPlayerState::Paused)
        {
            FPlatformProcess::Sleep(0.1f);
            continue;
        }

        if (m_PlayerState == EPlayerState::Stopped)
        {
            break;
        }

        if (m_VideoPacketQueue.Size() > 50 || m_AudioPacketQueue.Size() > 50)
        {
            FPlatformProcess::Sleep(0.01f);
            continue;
        }

        AVPacket* packet = av_packet_alloc();
        if (av_read_frame(m_FormatCtx, packet) < 0)
        {
            av_packet_free(&packet);
            m_PlayerState = EPlayerState::Finished;
            break;
        }

        if (packet->stream_index == m_VideoStreamIndex)
        {
            m_VideoPacketQueue.Push(packet);
        }
        else if (packet->stream_index == m_AudioStreamIndex)
        {
            m_AudioPacketQueue.Push(packet);
        }
        else
        {
            av_packet_free(&packet);
        }
    }
    UE_LOG(LogTemp, Log, TEXT("Demuxer thread finished."));
}

// Forward declaration of the helper function
void UFFmpegMediaPlayer::InitSwrContext(const AVFrame* InFrame)
{
    // --- Define Your Target Audio Format Here ---
    AVChannelLayout TargetChannelLayout;
    av_channel_layout_default(&TargetChannelLayout, 2); // Create a standard stereo layout
    const int32    TargetSampleRate = 48000;
    const AVSampleFormat TargetSampleFormat = AV_SAMPLE_FMT_S16;
    // ---------------------------------------------

    // Get properties from the incoming frame using the NEW API
    const AVChannelLayout* InChannelLayout = &InFrame->ch_layout;
    const int32    InSampleRate = InFrame->sample_rate;
    const AVSampleFormat InSampleFormat = (AVSampleFormat)InFrame->format;

    // Check if the context needs to be re-initialized using the NEW comparison function
    if (m_SwrCtx != nullptr &&
        (av_channel_layout_compare(&m_LastAudioSpec.ChannelLayout, InChannelLayout) != 0 ||
            m_LastAudioSpec.SampleRate != InSampleRate ||
            m_LastAudioSpec.SampleFormat != InSampleFormat))
    {
        swr_free(&m_SwrCtx);
        m_SwrCtx = nullptr;
        UE_LOG(LogTemp, Log, TEXT("Audio stream format changed. Re-initializing SwrContext."));
    }

    // Allocate and configure the SwrContext if it's not valid using the NEW function
    if (m_SwrCtx == nullptr)
    {
        // Use swr_alloc_set_opts2 for the new AVChannelLayout API
        if (swr_alloc_set_opts2(&m_SwrCtx,
            &TargetChannelLayout, TargetSampleFormat, TargetSampleRate, // Output
            InChannelLayout, InSampleFormat, InSampleRate,     // Input
            0, nullptr) < 0)
        {
            m_SwrCtx = nullptr; // Ensure context is null on failure
        }

        if (m_SwrCtx)
        {
            if (swr_init(m_SwrCtx) < 0)
            {
                swr_free(&m_SwrCtx);
                m_SwrCtx = nullptr;
                UE_LOG(LogTemp, Error, TEXT("Failed to initialize SwrContext."));
            }
            else
            {
                // Store the format we used for initialization using the NEW copy function
                av_channel_layout_copy(&m_LastAudioSpec.ChannelLayout, InChannelLayout);
                m_LastAudioSpec.SampleRate = InSampleRate;
                m_LastAudioSpec.SampleFormat = InSampleFormat;
                UE_LOG(LogTemp, Log, TEXT("SwrContext initialized successfully."));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to allocate SwrContext."));
        }
    }
}


void UFFmpegMediaPlayer::AudioDecodeThread()
{
    AVFrame* frame = av_frame_alloc();
    TArray<uint8_t> ResampledBuffer;

    // Initialize the audio spec tracker. This should be done once when the thread starts.
    m_LastAudioSpec = FAudioSpec();

    double LastFrameTime = FPlatformTime::Seconds();
    FScopeLock Lock(&FFmpegResourceCriticalSectionSound);
    while (!m_bStopThreads.load(std::memory_order_acquire))
    {
        while (m_PlayerState == EPlayerState::Paused && !m_bStopThreads.load(std::memory_order_acquire))
        {
            FPlatformProcess::Sleep(0.1f);
        }

//         if (m_PlayerState == EPlayerState::Paused)
//         {
//             FPlatformProcess::Sleep(0.1f);
//             LastFrameTime = FPlatformTime::Seconds();
//             continue;
//         }

        if (m_PlayerState.load(std::memory_order_acquire) == EPlayerState::Stopped)
        {
            break;
        }

        AVPacket* packet = nullptr;
        if (!m_AudioPacketQueue.Pop(packet))
        {
            continue;
        }

        if (avcodec_send_packet(m_AudioCodecCtx, packet) == 0)
        {
            while (avcodec_receive_frame(m_AudioCodecCtx, frame) == 0)
            {
                if (m_PlayerState.load(std::memory_order_acquire) == EPlayerState::Stopped)
                {
                    break;
                }
                while (m_PlayerState == EPlayerState::Paused && !m_bStopThreads.load(std::memory_order_acquire))
                {
                    FPlatformProcess::Sleep(0.1f);
                }

                // Ensure the SwrContext is valid and configured for this frame's format
                InitSwrContext(frame);
                if (!m_SwrCtx)
                {
                    UE_LOG(LogTemp, Error, TEXT("Cannot process audio frame, SwrContext is not valid."));
                    continue; // Skip this frame if context is bad
                }

                if (frame->pts != AV_NOPTS_VALUE)
                {
                    m_AudioClock = frame->pts * av_q2d(m_AudioStream->time_base);
                }

                // --- CORRECTED BUFFER CALCULATION ---
                AVChannelLayout TargetChannelLayout;
                av_channel_layout_default(&TargetChannelLayout, 2); // Stereo
                const int32 TargetSampleRate = 48000;
                const int32 TargetNumChannels = TargetChannelLayout.nb_channels;

                // Calculate the maximum number of samples the output buffer might need
                const int max_out_nb_samples = (int)av_rescale_rnd(frame->nb_samples, TargetSampleRate, frame->sample_rate, AV_ROUND_UP);

                // Calculate the size of the buffer needed for that many samples in the target format
                const int out_buffer_size = av_samples_get_buffer_size(nullptr, TargetNumChannels, max_out_nb_samples, AV_SAMPLE_FMT_S16, 1);

                ResampledBuffer.SetNumUninitialized(out_buffer_size);
                uint8_t* pResampledBuffer = ResampledBuffer.GetData();

                // Perform the conversion
                int real_out_nb_samples = swr_convert(m_SwrCtx, &pResampledBuffer, max_out_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);

                if (real_out_nb_samples > 0)
                {
                    int real_out_buffer_size = av_samples_get_buffer_size(nullptr, TargetNumChannels, real_out_nb_samples, AV_SAMPLE_FMT_S16, 1);
                    if (!m_bStopThreads.load(std::memory_order_acquire))
                    {
                        TArray<uint8> AudioDataCopy(ResampledBuffer.GetData(), real_out_buffer_size);
                        AsyncTask(ENamedThreads::GameThread, [this, AudioDataCopy]()
                            {
                                if (m_PlayerState == EPlayerState::Paused)
                                {
                                    return;
                                }

                                if (IsValid(m_AudioSoundWave))
                                {
                                    m_AudioSoundWave->QueueAudio(AudioDataCopy.GetData(), AudioDataCopy.Num());
                                }
                            });
                    }
                }
                else if (real_out_nb_samples < 0)
                {
                    // Log the error if conversion fails
                    char err_buf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
                    av_strerror(real_out_nb_samples, err_buf, AV_ERROR_MAX_STRING_SIZE);
                    UE_LOG(LogTemp, Error, TEXT("swr_convert failed with error: %s"), UTF8_TO_TCHAR(err_buf));
                }
            }
        }
        av_packet_free(&packet);
    }
    av_frame_free(&frame);
    swr_free(&m_SwrCtx); // Clean up the context when the thread finishes
    av_channel_layout_uninit(&m_LastAudioSpec.ChannelLayout); // Clean up the layout struct
    UE_LOG(LogTemp, Log, TEXT("Audio thread finished."));
}


// void UFFmpegMediaPlayer::AudioDecodeThread()
// {
//     AVFrame* frame = av_frame_alloc();
//     TArray<uint8_t> ResampledBuffer;
// 
//     // �߾��ȼ�ʱ������¼ÿ��ѭ���Ŀ�ʼʱ��
//     double LastFrameTime = FPlatformTime::Seconds();
//     FScopeLock Lock(&FFmpegResourceCriticalSectionSound);
//     while (!m_bStopThreads.load(std::memory_order_acquire))
//     {
//         if (m_PlayerState == EPlayerState::Paused)
//         {
//             FPlatformProcess::Sleep(0.1f);
//             LastFrameTime = FPlatformTime::Seconds(); // ������ͣ��ļ�ʱ
//             continue;
//         }
// 
//         if (m_PlayerState.load(std::memory_order_acquire) == EPlayerState::Stopped)
//         {
//             break;
//         }
// 
//         AVPacket* packet = nullptr;
//         if (!m_AudioPacketQueue.Pop(packet))
//         {
//             continue; // Queue was stopped
//         }
// 
//         if (avcodec_send_packet(m_AudioCodecCtx, packet) == 0)
//         {
//             while (avcodec_receive_frame(m_AudioCodecCtx, frame) == 0)
//             {
//                 if (m_PlayerState.load(std::memory_order_acquire) == EPlayerState::Stopped)
//                 {
//                     break;
//                 }
// 
//                 if (frame->pts != AV_NOPTS_VALUE)
//                 {
//                     m_AudioClock = frame->pts * av_q2d(m_AudioStream->time_base);
//                 }
// 
//                 const int max_out_nb_samples = (int)av_rescale_rnd(frame->nb_samples, m_AudioCodecCtx->sample_rate, m_AudioCodecCtx->sample_rate, AV_ROUND_UP);
//                 int out_buffer_size = av_samples_get_buffer_size(nullptr, AudioNumChannels, max_out_nb_samples, AV_SAMPLE_FMT_S16, 1);
//                 ResampledBuffer.SetNumUninitialized(out_buffer_size);
//                 uint8_t* pResampledBuffer = ResampledBuffer.GetData();
//                 int real_out_nb_samples = swr_convert(m_SwrCtx, &pResampledBuffer, max_out_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
// 
//                 if (real_out_nb_samples > 0)
//                 {
//                     int real_out_buffer_size = av_samples_get_buffer_size(nullptr, AudioNumChannels, real_out_nb_samples, AV_SAMPLE_FMT_S16, 1);
//                     if (!m_bStopThreads.load(std::memory_order_acquire))
//                     {
//                         TArray<uint8> AudioDataCopy(ResampledBuffer.GetData(), real_out_buffer_size);
//                         AsyncTask(ENamedThreads::GameThread, [this, AudioDataCopy]()
//                             {
//                                 if (IsValid(m_AudioSoundWave))
//                                 {
//                                     m_AudioSoundWave->QueueAudio(AudioDataCopy.GetData(), AudioDataCopy.Num());
//                                 }
//                             });
//                     }
// 
//                     const double FrameDuration = (double)real_out_nb_samples / (double)m_AudioCodecCtx->sample_rate;
// 
//                     const double CurrentTime = FPlatformTime::Seconds();
//                     const double TimeTaken = CurrentTime - LastFrameTime;
// 
//                     const double SleepTime = FrameDuration - TimeTaken;
//                     LastFrameTime = FPlatformTime::Seconds();
//                 }
//             }
//         }
//         av_packet_free(&packet);
//     }
// 
//     av_frame_free(&frame);
//     UE_LOG(LogTemp, Log, TEXT("Audio thread finished."));
// }

void UFFmpegMediaPlayer::VideoDecodeThread()
{
    FScopeLock Lock(&FFmpegResourceCriticalSectionVedio);

    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();

    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, m_FrameData.GetData(), AV_PIX_FMT_BGRA, m_VideoCodecCtx->width, m_VideoCodecCtx->height, 32);

    double video_time_base = av_q2d(m_VideoStream->time_base);

    while (!m_bStopThreads.load(std::memory_order_acquire))
    {
        while (m_PlayerState == EPlayerState::Paused && !m_bStopThreads.load(std::memory_order_acquire))
        {
            FPlatformProcess::Sleep(0.1f);
        }

        if (m_PlayerState == EPlayerState::Stopped)
        {
            break;
        }

        AVPacket* packet = nullptr;
        if (!m_VideoPacketQueue.Pop(packet))
        {
            continue; // Queue was stopped
        }

        if (avcodec_send_packet(m_VideoCodecCtx, packet) == 0)
        {
            while (avcodec_receive_frame(m_VideoCodecCtx, frame) == 0)
            {
                if (m_PlayerState.load(std::memory_order_acquire) == EPlayerState::Stopped)
                {
                    break;
                }
                //double video_pts = (frame->best_effort_timestamp == AV_NOPTS_VALUE) ? 0 : frame->best_effort_timestamp * video_time_base;
                double video_pts = (frame->best_effort_timestamp == AV_NOPTS_VALUE) ? 0 : frame->pts * video_time_base;
                double current_audio_clock = GetAudioClock();

                // Simple A-V sync
                double delay = video_pts - current_audio_clock;
                if (delay > 0.01)
                {
                    FPlatformProcess::Sleep(delay);
                }
                current_audio_clock = GetAudioClock();
                // Skip frame if we are too late
                if (current_audio_clock > video_pts + 0.5)
                {
                    continue;
                }

                if (m_SwsCtx)
                {
                    sws_scale(m_SwsCtx, (const uint8_t* const*)frame->data, frame->linesize, 0, m_VideoCodecCtx->height, rgb_frame->data, rgb_frame->linesize);
                }


                // Update texture on the game thread
                UpdateTexture(rgb_frame);
            }
        }
        av_packet_free(&packet);
    }

    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    UE_LOG(LogTemp, Log, TEXT("Video thread finished."));
}

void UFFmpegMediaPlayer::Cleanup()
{
    {
        FScopeLock Lock(&FFmpegResourceCriticalSectionSound);
        // Free FFmpeg contexts
        if (m_SwrCtx) {
            swr_free(&m_SwrCtx);
            m_SwrCtx = nullptr;
        }

        if (m_AudioCodecCtx) {
            avcodec_free_context(&m_AudioCodecCtx);
        }

    }
  
    {
        FScopeLock Lock(&FFmpegResourceCriticalSectionVedio);
        if (m_SwsCtx) {
            sws_freeContext(m_SwsCtx);
            m_SwsCtx = nullptr;
        }


        if (m_VideoCodecCtx) {
            avcodec_free_context(&m_VideoCodecCtx);
        }
    }

   

    if (m_FormatCtx) {
        avformat_close_input(&m_FormatCtx); 
    }

    // Set pointers to null
    m_VideoCodecCtx = nullptr;
    m_AudioCodecCtx = nullptr;
    m_FormatCtx = nullptr;
    m_SwsCtx = nullptr;
    m_SwrCtx = nullptr;

    m_VideoStreamIndex = -1;
    m_AudioStreamIndex = -1;
    m_AudioClock = 0.0;
    m_VideoDimensions = FIntPoint::ZeroValue;

    // UE Objects will be garbage collected. Setting them to null is good practice.
    m_VideoTexture = nullptr;
    m_AudioSoundWave = nullptr;
    m_AudioComponent = nullptr;
}

double UFFmpegMediaPlayer::GetAudioClock()
{
    // A simple implementation of the audio clock. A more robust solution might
    // account for the buffer in USoundWaveProcedural.
   // return m_AudioClock;
//     if (!m_AudioStream || !m_AudioCodecCtx || !m_AudioSoundWave)
//     {
//         // Not ready yet, return 0 or the last known clock time
//         return m_AudioClock;
//     }
// 
//     // The 'high water mark' of our clock is the PTS of the last frame sent to the queue.
//     double pts = m_AudioClock;
// 
//     // Get the number of bytes currently sitting in the SoundWave's buffer, waiting to be played.
//     const int32 QueuedBytes = m_AudioSoundWave->GetAvailableAudioByteCount();
// 
//     // Calculate how many bytes are processed per second.
//     // SampleRate * NumChannels * BytesPerSample (2 for 16-bit audio)
//     const int32 BytesPerSec = m_AudioCodecCtx->sample_rate * m_AudioSoundWave->NumChannels * 2;
// 
//     if (BytesPerSec > 0)
//     {
//         // Subtract the duration of the buffered audio from our high water mark.
//         // This gives us an estimate of the PTS of the audio currently being heard.
//         pts -= (double)QueuedBytes / BytesPerSec;
//     }
// 
//     return pts;
    if (!m_AudioStream || !m_AudioSoundWave)
        return m_AudioClock;

    // ���㻺���д����ŵ���Ƶʱ��
    const int32 QueuedBytes = m_AudioSoundWave->GetAvailableAudioByteCount();
    const double BufferedTime = static_cast<double>(QueuedBytes) /
        (m_AudioCodecCtx->sample_rate *
            m_AudioSoundWave->NumChannels *
            sizeof(int16));

    return m_AudioClock - BufferedTime; // ��ǰ����λ�� = ���֡ʱ�� - ����ʱ��
}

void UFFmpegMediaPlayer::UpdateTexture(const AVFrame* RgbFrame)
{
    if (!m_VideoTexture || !RgbFrame /*|| !m_VideoTexture->GetResource()*/)
    {
        return;
    }

    // 1. ����Ƶ֡���ݿ�����һ��TArray�У��Ա㱻Lambda��ȫ����
    //    ��������Ǳ���ģ���ΪRgbFrameָ���ں����������ʧЧ��


    FMemory::Memcpy(FrameDataCopy.GetData(), RgbFrame->data[0], FrameDataCopy.Num());
    TArray<uint8> LocalCopyOfData = this->FrameDataCopy;

    AsyncTask(ENamedThreads::GameThread, [this, LocalCopyOfData]()
        {

            if (m_PlayerState.load(std::memory_order_acquire) == EPlayerState::Stopped)
            {
                return;
            }

            if (!m_VideoTexture || !IsValid(m_VideoTexture) || !m_VideoTexture->GetResource())
            {
                return;
            }
            // 2. ��ȡ�����ĳߴ��RHI��Դ
            const FIntPoint VideoDimensions = GetVideoDimensions();
            FTextureRHIRef TextureRHI = m_VideoTexture->GetResource()->GetTexture2DRHI();

            // 3. ENQUEUE_RENDER_COMMAND �Ὣһ��Lambda�����ɷ�����Ⱦ�߳�ȥִ��
            ENQUEUE_RENDER_COMMAND(UpdateVideoTexture)(
                [TextureRHI, VideoDimensions, this, LocalCopyOfData](FRHICommandListImmediate& RHICmdList)
                {
                    // --- �ⲿ�ִ�����������Ⱦ�߳���ִ�� ---
                    FTaskTagScope Scope(ETaskTag::EParallelRenderingThread);
                    // ����Ҫ���µ�������������������
                    FUpdateTextureRegion2D UpdateRegion(0, 0, 0, 0, VideoDimensions.X, VideoDimensions.Y);

                    // ����Դ���ݵ��о� (Stride/Pitch)
                    // BGRA��ʽ��ÿ������4���ֽ�
                    const uint32 Stride = VideoDimensions.X * 4;

                    // ����RHI����ֱ�Ӹ���GPU�ϵ���������
                    RHIUpdateTexture2D(
                        TextureRHI,          // Ҫ���µ�����RHI��Դ
                        0,                   // Mipmap�㼶��0����߼�
                        UpdateRegion,        // ��������
                        Stride,              // Դ�����о�
                        LocalCopyOfData.GetData() // Դ��������
                    );
                }
                );

        });

}