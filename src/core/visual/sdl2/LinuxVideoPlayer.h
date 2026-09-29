/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

/* Linux software video player.
 *
 * The other platforms delegate movie playback to a native overlay (DirectShow
 * on Windows, AVPlayer on macOS/iOS/OHOS, MediaPlayer on Android). Linux has no
 * such overlay, so this player decodes with FFmpeg into an RGBA buffer that the
 * SDL renderer draws as a full-window texture. Movie.tjs already selects this
 * backend for every platform that is not Windows/macOS/Android - it calls it
 * "SDL ffmpeg overlay" - so only the implementation was missing.
 */

#ifndef KRKRSDL2_LINUX_VIDEO_PLAYER_H
#define KRKRSDL2_LINUX_VIDEO_PLAYER_H

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)

#include <SDL.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class TVPLinuxVideoPlayer
{
public:
	TVPLinuxVideoPlayer();
	~TVPLinuxVideoPlayer();

	/* Opens the file and decodes the first frame so the caller can query the
	 * video size right away. Returns false when the movie cannot be played. */
	bool Open(const char *filename);
	void Close();

	bool Play();
	void Pause();
	void Stop();
	void Rewind();

	void SetVolume(float volume); /* 0.0 - 1.0 */
	float Volume() const { return volume_.load(); }
	bool HasAudio() const { return audio_stream_ >= 0; }

	int Width() const { return width_; }
	int Height() const { return height_; }
	double FPS() const { return fps_; }
	int FrameCount() const { return frame_count_; }
	double Duration() const { return duration_; }
	double Position() const;

	/* Render thread side: returns the current frame as tightly packed BGRA rows
	 * (matching SDL_PIXELFORMAT_ARGB8888 on little-endian hosts). The frame
	 * stays available until the next one is decoded, so a render loop running
	 * faster than the decoder keeps showing the same picture instead of
	 * falling back to something else in between. `is_new` (optional) is set
	 * when this frame has not been presented yet. Returns false only before
	 * the first frame or after ResetPlaybackState(). */
	bool AcquireFrame(const uint8_t **pixels, int *pitch, bool *is_new = nullptr);
	void ReleaseFrame();

	bool IsFinished() const { return finished_.load(); }

private:
	void DecodeLoop();
	void PublishFrame(AVFrame *frame, double &first_pts);
	void CloseInternal();
	void ResetPlaybackState();

	bool OpenAudio(int sample_rate, int channels);
	void CloseAudio();
	void PushAudio(const uint8_t *data, int bytes);
	void ClearAudio();
	static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int length);

	AVFormatContext *format_;
	AVCodecContext *video_ctx_;
	AVCodecContext *audio_ctx_;
	SwsContext *sws_;
	SwrContext *swr_;
	AVPacket *packet_;
	AVFrame *video_frame_;
	AVFrame *audio_frame_;
	int video_stream_;
	int audio_stream_;
	AVRational video_time_base_;

	int width_;
	int height_;
	double fps_;
	int frame_count_;
	double duration_;

	std::thread decoder_;
	std::atomic<bool> quit_;
	std::atomic<bool> paused_;
	std::atomic<bool> running_;
	std::atomic<bool> finished_;
	std::atomic<bool> rewind_requested_;
	std::atomic<double> clock_base_;   /* seconds, set when playback starts */
	std::atomic<double> position_;     /* last decoded frame position */

	std::mutex frame_mutex_;
	std::vector<uint8_t> frame_buffer_[2];
	int front_buffer_;
	/* frame_valid_ survives ReleaseFrame(); frame_ready_ marks "not presented
	 * yet" and is what keeps the renderer from re-blitting an unchanged frame. */
	bool frame_valid_;
	bool frame_ready_;

	SDL_AudioDeviceID audio_device_;
	std::mutex audio_mutex_;
	std::condition_variable audio_space_;
	std::vector<uint8_t> audio_queue_;
	/* Written by the TJS thread, read by the SDL audio callback thread. */
	std::atomic<float> volume_;
	int audio_sample_rate_;
	int audio_channels_;
};

/* ---------------------------------------------------------------------------
 * Bridge used by the SDL render loop.
 *
 * The player itself is owned by tTJSNI_VideoOverlay (VideoOvlImpl.cpp), which
 * also owns the "playback finished" notification, so the loop only needs these
 * four entry points. They are no-ops when no movie is playing.
 * ------------------------------------------------------------------------- */
bool TVPLinuxVideoIsActive();
/* `is_new` reports whether this frame has not been presented yet; the loop
 * uses it to skip re-uploading an unchanged picture. */
bool TVPLinuxVideoAcquireFrame(const uint8_t **pixels, int *pitch, int *width, int *height, bool *is_new);
void TVPLinuxVideoReleaseFrame();
/* Returns true once per playback when the movie reached its end; the overlay
 * has already been switched to the "stop" status at that point, which is what
 * Movie.tjs waits for. Must be called from the main thread. */
bool TVPLinuxVideoConsumeFinished();

#endif /* __linux__ && !__ANDROID__ && !__OHOS__ */

#endif /* KRKRSDL2_LINUX_VIDEO_PLAYER_H */
