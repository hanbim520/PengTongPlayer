// This file needs to include the FFmpeg headers.
// We wrap them to manage compiler warnings and ensure proper linkage.
#pragma warning(push)
#pragma warning(disable: 4996) // Disable deprecation warnings that are common in FFmpeg

#include "FFmpegThread.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
}

#pragma warning(pop)


FFmpegThread::FFmpegThread(FString InMediaPath)
    : MediaPath(InMediaPath),
    bStopThread(false),
    bPauseThread(false)
{
    // The constructor simply stores the path. Initialization happens in Init().
}

FFmpegThread::~FFmpegThread()
{
    // Ensure cleanup is called if the object is destroyed unexpectedly.
    Cleanup();
}

bool FFmpegThread::Init()
{
    // This is called by FRunnableThread::Create() on the Game Thread before Run() is called.

    // Open the media file using FFmpeg.

    std::string FFMpegPath = "";
    ResolveMediaPath(MediaPath, FFMpegPath);
    if (avformat_open_input(&FormatCtx, FFMpegPath.c_str(), nullptr, nullptr) != 0) {
        UE_LOG(LogTemp, Error, TEXT("FFmpeg: Could not open file %s"), *MediaPath);
        return false;
    }
    if (avformat_find_stream_info(FormatCtx, nullptr) < 0) {
        UE_LOG(LogTemp, Error, TEXT("FFmpeg: Could not find stream info"));
        return false;
    }

    // Find the first video and audio streams.
    for (uint32 i = 0; i < FormatCtx->nb_streams; ++i) {
        AVStream* stream = FormatCtx->streams[i];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) continue;

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && VideoStreamIndex < 0) {
            VideoStreamIndex = i;
            VideoCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(VideoCodecCtx, stream->codecpar);
            avcodec_open2(VideoCodecCtx, codec, nullptr);
            VideoWidth = VideoCodecCtx->width;
            VideoHeight = VideoCodecCtx->height;
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && AudioStreamIndex < 0) {
            AudioStreamIndex = i;
            AudioCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(AudioCodecCtx, stream->codecpar);
            avcodec_open2(AudioCodecCtx, codec, nullptr);
        }
    }

    if (VideoStreamIndex == -1 || AudioStreamIndex == -1) {
        UE_LOG(LogTemp, Error, TEXT("FFmpeg: Could not find video stream"));
        return false;
    }

    // Setup the video scaling context to convert frames to RGBA (for UTexture2D).
//     SwsCtx = sws_getContext(VideoCodecCtx->width, VideoCodecCtx->height, VideoCodecCtx->pix_fmt,
//         VideoCodecCtx->width, VideoCodecCtx->height, AV_PIX_FMT_RGBA,
//         SWS_BILINEAR, nullptr, nullptr, nullptr);

    SwrCtx = nullptr;
    swr_alloc_set_opts2(&SwrCtx, &AudioCodecCtx->ch_layout, AV_SAMPLE_FMT_S16, AudioCodecCtx->sample_rate, &AudioCodecCtx->ch_layout, AudioCodecCtx->sample_fmt, AudioCodecCtx->sample_rate, 0, nullptr);
    swr_init(SwrCtx);
    SwsCtx = sws_getContext(VideoCodecCtx->width, VideoCodecCtx->height, VideoCodecCtx->pix_fmt, VideoCodecCtx->width, VideoCodecCtx->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);


    // Setup the audio resampling context to convert audio to 16-bit stereo (for USoundWaveProcedural).
//     if (AudioCodecCtx)
//     {
//         AVChannelLayout out_ch_layout;
//         av_channel_layout_default(&out_ch_layout, 2); // Stereo
//         // Target format for Unreal: 44100 Hz, 16-bit signed integer, stereo
//         swr_alloc_set_opts2(&SwrCtx, &out_ch_layout, AV_SAMPLE_FMT_S16, 44100,
//             &AudioCodecCtx->ch_layout, AudioCodecCtx->sample_fmt, AudioCodecCtx->sample_rate,
//             0, nullptr);
//         swr_init(SwrCtx);
//     }

    // If we get here, initialization was successful.
    return true;
}

uint32 FFmpegThread::Run()
{
    // This is the main loop of the thread. It runs until bIsRunning is false.
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (!bStopThread) {
        // To prevent using too much memory, we pause decoding if the queues get too full.
//         if (VideoFrameQueue.GetSize() > 30 || AudioFrameQueue.GetSize() > 30) {
//             FPlatformProcess::Sleep(0.01f); // Sleep for 10ms
//             continue;
//         }
        if (bPauseThread)
        {
            FPlatformProcess::Sleep(0.1f);
            continue;
        }
        // Read one packet from the media file.
        if (av_read_frame(FormatCtx, packet) < 0) {
            break; // End of file, exit the loop.
        }

        // --- Process Video Packet ---
        if (packet->stream_index == VideoStreamIndex) {
            if (avcodec_send_packet(VideoCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(VideoCodecCtx, frame) == 0) {
                    // Create a new shared pointer for our frame data struct.
                    auto VideoFrame = MakeShared<FVideoFrame, ESPMode::ThreadSafe>();
                    VideoFrame->Width = VideoCodecCtx->width;
                    VideoFrame->Height = VideoCodecCtx->height;
                    // Allocate memory for the RGBA pixel data.
                    VideoFrame->Data.SetNumUninitialized(VideoFrame->Width * VideoFrame->Height * 4);

                    uint8_t* Dest[] = { VideoFrame->Data.GetData() };
                    int DestStride[] = { VideoFrame->Width * 4 };

                    // Convert the decoded frame (e.g., YUV420) to RGBA.
                    sws_scale(SwsCtx, frame->data, frame->linesize, 0, VideoCodecCtx->height, Dest, DestStride);

                    // Push the finished frame onto the queue for the Game Thread to pick up.
                    VideoFrameQueue.Enqueue(VideoFrame);
                }
            }
        }
        // --- Process Audio Packet ---
        else if (packet->stream_index == AudioStreamIndex && SwrCtx)
        {
            if (avcodec_send_packet(AudioCodecCtx, packet) == 0) {
                while (avcodec_receive_frame(AudioCodecCtx, frame) == 0) {
                    uint8_t* resampled_buffer = nullptr;
                    // Allocate a buffer for the resampled audio.
                    av_samples_alloc(&resampled_buffer, nullptr, 2, frame->nb_samples, AV_SAMPLE_FMT_S16, 0);

                    // Convert the decoded audio to our target format (S16 stereo).
                    int out_samples = swr_convert(SwrCtx, &resampled_buffer, frame->nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
                    int out_buffer_size = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 0);

                    // Copy the resampled audio data into a TArray.
                    TArray<uint8> AudioData;
                    AudioData.Append(resampled_buffer, out_buffer_size);
                    // Push the audio data onto the queue.
                    AudioFrameQueue.Enqueue(AudioData);

                    av_freep(&resampled_buffer);
                }
            }
        }

        // Release the packet reference.
        av_packet_unref(packet);
    }

    // Free the allocated frame and packet.
    av_frame_free(&frame);
    av_packet_free(&packet);

    // Signal that the thread has finished its work.
    bIsRunning = false;
    return 0;
}

void FFmpegThread::Stop()
{
    // This is called from the Game Thread to signal that the thread should exit its loop.
    bStopThread = true;
    if (ThreadHandle)
    {
        ThreadHandle->WaitForCompletion();
        delete ThreadHandle;
        ThreadHandle = nullptr;
    }
}

void FFmpegThread::Exit()
{
    // This is called on the Game Thread after the Run() method has finished.
    // We do final cleanup here.
    Cleanup();
}

void FFmpegThread::Cleanup()
{
    // Free all FFmpeg contexts and memory in reverse order of creation.
    if (SwsCtx) { sws_freeContext(SwsCtx); SwsCtx = nullptr; }
    if (SwrCtx) { swr_free(&SwrCtx); SwrCtx = nullptr; }
    if (VideoCodecCtx) { avcodec_free_context(&VideoCodecCtx); VideoCodecCtx = nullptr; }
    if (AudioCodecCtx) { avcodec_free_context(&AudioCodecCtx); AudioCodecCtx = nullptr; }
    if (FormatCtx) { avformat_close_input(&FormatCtx); FormatCtx = nullptr; }
}

bool FFmpegThread::GetNextVideoFrame(TSharedPtr<FVideoFrame, ESPMode::ThreadSafe>& OutFrame)
{
    // Dequeue is a thread-safe operation.
    return VideoFrameQueue.Dequeue(OutFrame);
}

bool FFmpegThread::GetNextAudioFrame(TArray<uint8>& OutData)
{
    return AudioFrameQueue.Dequeue(OutData);
}

bool FFmpegThread::StartThread()
{
    if (!ThreadHandle)
    {
        ThreadHandle = FRunnableThread::Create(
            this,
            TEXT("FFmpegDecodeThread"),
            128 * 1024, // 128KB栈大小
            TPri_AboveNormal // 高于默认优先级
        );
        return ThreadHandle != nullptr;
    }
    return false;
}

void FFmpegThread::Pause() { bPauseThread = true; }
void FFmpegThread::Resume() { bPauseThread = false; }
bool FFmpegThread::IsRunning() const { return !bStopThread && !bPauseThread; }