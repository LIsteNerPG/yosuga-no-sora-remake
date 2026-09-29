/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "LinuxVideoPlayer.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)

#include <chrono>
#include <cstring>

namespace {

const int kAudioQueueLimit = 192000; /* ~1 s of 48 kHz stereo S16 */
const int kAudioChunk = 4096;

double NowSeconds()
{
	return (double)SDL_GetTicks() / 1000.0;
}

} // namespace

TVPLinuxVideoPlayer::TVPLinuxVideoPlayer()
	: format_(nullptr)
	, video_ctx_(nullptr)
	, audio_ctx_(nullptr)
	, sws_(nullptr)
	, swr_(nullptr)
	, packet_(nullptr)
	, video_frame_(nullptr)
	, audio_frame_(nullptr)
	, video_stream_(-1)
	, audio_stream_(-1)
	, width_(0)
	, height_(0)
	, fps_(0.0)
	, frame_count_(0)
	, duration_(0.0)
	, quit_(false)
	, paused_(false)
	, running_(false)
	, finished_(false)
	, rewind_requested_(false)
	, clock_base_(0.0)
	, position_(0.0)
	, front_buffer_(0)
	, frame_ready_(false)
	, audio_device_(0)
	, volume_(1.0f)
	, audio_sample_rate_(0)
	, audio_channels_(0)
{
	video_time_base_.num = 1;
	video_time_base_.den = 1000;
}

TVPLinuxVideoPlayer::~TVPLinuxVideoPlayer()
{
	Close();
}

void TVPLinuxVideoPlayer::CloseInternal()
{
	if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
	if (swr_) { swr_free(&swr_); swr_ = nullptr; }
	if (video_frame_) { av_frame_free(&video_frame_); video_frame_ = nullptr; }
	if (audio_frame_) { av_frame_free(&audio_frame_); audio_frame_ = nullptr; }
	if (packet_) { av_packet_free(&packet_); packet_ = nullptr; }
	if (video_ctx_) { avcodec_free_context(&video_ctx_); video_ctx_ = nullptr; }
	if (audio_ctx_) { avcodec_free_context(&audio_ctx_); audio_ctx_ = nullptr; }
	if (format_) { avformat_close_input(&format_); format_ = nullptr; }
	video_stream_ = -1;
	audio_stream_ = -1;
}

void TVPLinuxVideoPlayer::ResetPlaybackState()
{
	std::lock_guard<std::mutex> lock(frame_mutex_);
	front_buffer_ = 0;
	frame_ready_ = false;
	position_.store(0.0);
}

bool TVPLinuxVideoPlayer::Open(const char *filename)
{
	Close();

	if (avformat_open_input(&format_, filename, nullptr, nullptr) < 0)
	{
		return false;
	}
	if (avformat_find_stream_info(format_, nullptr) < 0)
	{
		Close();
		return false;
	}

	video_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	audio_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (video_stream_ < 0)
	{
		Close();
		return false;
	}

	AVCodecParameters *video_params = format_->streams[video_stream_]->codecpar;
	const AVCodec *video_codec = avcodec_find_decoder(video_params->codec_id);
	if (!video_codec)
	{
		Close();
		return false;
	}
	video_ctx_ = avcodec_alloc_context3(video_codec);
	if (!video_ctx_ || avcodec_parameters_to_context(video_ctx_, video_params) < 0 ||
		avcodec_open2(video_ctx_, video_codec, nullptr) < 0)
	{
		Close();
		return false;
	}

	video_time_base_ = format_->streams[video_stream_]->time_base;
	width_ = video_ctx_->width;
	height_ = video_ctx_->height;
	fps_ = av_q2d(format_->streams[video_stream_]->avg_frame_rate);
	if (!(fps_ > 0.0))
	{
		fps_ = 25.0;
	}
	if (format_->duration > 0)
	{
		duration_ = (double)format_->duration / (double)AV_TIME_BASE;
	}
	else if (format_->streams[video_stream_]->duration > 0)
	{
		duration_ = (double)format_->streams[video_stream_]->duration *
			av_q2d(format_->streams[video_stream_]->time_base);
	}
	frame_count_ = (int)(duration_ * fps_ + 0.5);
	if (width_ <= 0 || height_ <= 0)
	{
		Close();
		return false;
	}

	if (audio_stream_ >= 0)
	{
		AVCodecParameters *audio_params = format_->streams[audio_stream_]->codecpar;
		const AVCodec *audio_codec = avcodec_find_decoder(audio_params->codec_id);
		if (audio_codec)
		{
			audio_ctx_ = avcodec_alloc_context3(audio_codec);
			if (!audio_ctx_ || avcodec_parameters_to_context(audio_ctx_, audio_params) < 0 ||
				avcodec_open2(audio_ctx_, audio_codec, nullptr) < 0)
			{
				if (audio_ctx_) { avcodec_free_context(&audio_ctx_); }
				audio_ctx_ = nullptr;
				audio_stream_ = -1;
			}
		}
		else
		{
			audio_stream_ = -1;
		}
	}

	sws_ = sws_getContext(width_, height_, video_ctx_->pix_fmt,
		width_, height_, AV_PIX_FMT_BGRA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!sws_)
	{
		Close();
		return false;
	}

	packet_ = av_packet_alloc();
	video_frame_ = av_frame_alloc();
	audio_frame_ = av_frame_alloc();
	if (!packet_ || !video_frame_ || !audio_frame_)
	{
		Close();
		return false;
	}

	frame_buffer_[0].assign((size_t)width_ * (size_t)height_ * 4u, 0);
	frame_buffer_[1].assign((size_t)width_ * (size_t)height_ * 4u, 0);
	ResetPlaybackState();

	if (audio_stream_ >= 0)
	{
		OpenAudio(audio_ctx_->sample_rate > 0 ? audio_ctx_->sample_rate : 48000,
			audio_ctx_->ch_layout.nb_channels > 0 ? audio_ctx_->ch_layout.nb_channels : 2);
	}
	return true;
}

void TVPLinuxVideoPlayer::Close()
{
	quit_.store(true);
	running_.store(false);
	paused_.store(false);
	audio_space_.notify_all();

	if (decoder_.joinable())
	{
		decoder_.join();
	}
	CloseAudio();
	ClearAudio();
	CloseInternal();

	width_ = 0;
	height_ = 0;
	fps_ = 0.0;
	frame_count_ = 0;
	duration_ = 0.0;
	frame_buffer_[0].clear();
	frame_buffer_[1].clear();
	ResetPlaybackState();
	quit_.store(false);
	finished_.store(false);
}

bool TVPLinuxVideoPlayer::Play()
{
	if (!format_)
	{
		return false;
	}
	if (!decoder_.joinable())
	{
		quit_.store(false);
		finished_.store(false);
		paused_.store(false);
		clock_base_.store(NowSeconds());
		decoder_ = std::thread(&TVPLinuxVideoPlayer::DecodeLoop, this);
	}
	else
	{
		/* Resuming after a pause keeps the frame clock continuous. */
		clock_base_.store(NowSeconds() - position_.load());
	}
	running_.store(true);
	paused_.store(false);
	if (audio_device_)
	{
		SDL_PauseAudioDevice(audio_device_, 0);
	}
	return true;
}

void TVPLinuxVideoPlayer::Pause()
{
	paused_.store(true);
	if (audio_device_)
	{
		SDL_PauseAudioDevice(audio_device_, 1);
	}
}

void TVPLinuxVideoPlayer::Stop()
{
	quit_.store(true);
	running_.store(false);
	audio_space_.notify_all();
	if (decoder_.joinable())
	{
		decoder_.join();
	}
	ClearAudio();
	CloseAudio();
	ResetPlaybackState();
	finished_.store(false);
	quit_.store(false);
}

void TVPLinuxVideoPlayer::Rewind()
{
	rewind_requested_.store(true);
	audio_space_.notify_all();
}

void TVPLinuxVideoPlayer::SetVolume(float volume)
{
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	if (volume > 1.0f)
	{
		volume = 1.0f;
	}
	volume_.store(volume);
}

double TVPLinuxVideoPlayer::Position() const
{
	return position_.load();
}

bool TVPLinuxVideoPlayer::AcquireFrame(const uint8_t **pixels, int *pitch)
{
	std::lock_guard<std::mutex> lock(frame_mutex_);
	if (!frame_ready_)
	{
		return false;
	}
	const std::vector<uint8_t> &buffer = frame_buffer_[front_buffer_];
	if (buffer.empty())
	{
		return false;
	}
	*pixels = buffer.data();
	*pitch = width_ * 4;
	return true;
}

void TVPLinuxVideoPlayer::ReleaseFrame()
{
	std::lock_guard<std::mutex> lock(frame_mutex_);
	frame_ready_ = false;
}

bool TVPLinuxVideoPlayer::OpenAudio(int sample_rate, int channels)
{
	if (channels > 2)
	{
		channels = 2;
	}
	if (channels < 1)
	{
		channels = 2;
	}

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = sample_rate;
	want.format = AUDIO_S16SYS;
	want.channels = (Uint8)channels;
	want.samples = 1024;
	want.callback = &TVPLinuxVideoPlayer::AudioCallback;
	want.userdata = this;

	SDL_AudioSpec have;
	audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (audio_device_ == 0)
	{
		return false;
	}
	audio_sample_rate_ = have.freq;
	audio_channels_ = have.channels;

	swr_ = swr_alloc();
	if (!swr_)
	{
		SDL_PauseAudioDevice(audio_device_, 1);
		return true; /* video only */
	}
	AVChannelLayout output_layout;
	av_channel_layout_default(&output_layout, audio_channels_);
	int err = swr_alloc_set_opts2(&swr_, &output_layout, AV_SAMPLE_FMT_S16, audio_sample_rate_,
		&audio_ctx_->ch_layout, audio_ctx_->sample_fmt, audio_ctx_->sample_rate,
		0, nullptr);
	av_channel_layout_uninit(&output_layout);
	if (err < 0 || swr_init(swr_) < 0)
	{
		swr_free(&swr_);
		swr_ = nullptr;
	}

	SDL_PauseAudioDevice(audio_device_, 1);
	return true;
}

void TVPLinuxVideoPlayer::CloseAudio()
{
	if (audio_device_)
	{
		SDL_CloseAudioDevice(audio_device_);
		audio_device_ = 0;
	}
}

void TVPLinuxVideoPlayer::ClearAudio()
{
	std::lock_guard<std::mutex> lock(audio_mutex_);
	audio_queue_.clear();
}

void TVPLinuxVideoPlayer::PushAudio(const uint8_t *data, int bytes)
{
	if (!audio_device_ || bytes <= 0)
	{
		return;
	}
	std::unique_lock<std::mutex> lock(audio_mutex_);
	while (!quit_.load() && (int)audio_queue_.size() > kAudioQueueLimit)
	{
		audio_space_.wait_for(lock, std::chrono::milliseconds(20));
	}
	audio_queue_.insert(audio_queue_.end(), data, data + bytes);
}

void SDLCALL TVPLinuxVideoPlayer::AudioCallback(void *userdata, Uint8 *stream, int length)
{
	TVPLinuxVideoPlayer *self = static_cast<TVPLinuxVideoPlayer *>(userdata);
	if (!self)
	{
		SDL_memset(stream, 0, length);
		return;
	}
	std::lock_guard<std::mutex> lock(self->audio_mutex_);
	int available = (int)self->audio_queue_.size();
	int take = available < length ? available : length;
	if (take > 0)
	{
		SDL_memcpy(stream, self->audio_queue_.data(), (size_t)take);
		self->audio_queue_.erase(self->audio_queue_.begin(), self->audio_queue_.begin() + take);
	}
	if (take < length)
	{
		SDL_memset(stream + take, 0, (size_t)(length - take));
	}
	float volume = self->volume_.load();
	if (volume < 1.0f)
	{
		Sint16 *samples = (Sint16 *)stream;
		int count = length / 2;
		for (int i = 0; i < count; ++i)
		{
			samples[i] = (Sint16)(samples[i] * volume);
		}
	}
}

void TVPLinuxVideoPlayer::DecodeLoop()
{
	double first_pts = -1.0;

	while (!quit_.load())
	{
		if (rewind_requested_.exchange(false))
		{
			av_seek_frame(format_, -1, 0, AVSEEK_FLAG_BACKWARD);
			if (video_ctx_) { avcodec_flush_buffers(video_ctx_); }
			if (audio_ctx_) { avcodec_flush_buffers(audio_ctx_); }
			ClearAudio();
			ResetPlaybackState();
			clock_base_.store(NowSeconds());
			first_pts = -1.0;
			finished_.store(false);
			continue;
		}

		if (paused_.load() || !running_.load())
		{
			SDL_Delay(10);
			continue;
		}

		int ret = av_read_frame(format_, packet_);
		if (ret < 0)
		{
			/* Flush the decoders so the last frames are not lost, then report
			 * completion. TickBeat polls IsFinished() and turns it into
			 * onStatusChanged("stop"), which is what lets Movie.tjs's phase
			 * machine leave the "running" state - the same contract the other
			 * platforms satisfy through their native players. */
			if (video_ctx_)
			{
				avcodec_send_packet(video_ctx_, nullptr);
				while (avcodec_receive_frame(video_ctx_, video_frame_) == 0)
				{
					PublishFrame(video_frame_, first_pts);
					if (first_pts >= 0.0)
					{
						first_pts = first_pts;
					}
				}
			}
			finished_.store(true);
			break;
		}

		if (packet_->stream_index == video_stream_)
		{
			if (avcodec_send_packet(video_ctx_, packet_) >= 0)
			{
				while (avcodec_receive_frame(video_ctx_, video_frame_) == 0)
				{
					PublishFrame(video_frame_, first_pts);
				}
			}
		}
		else if (packet_->stream_index == audio_stream_ && swr_)
		{
			if (avcodec_send_packet(audio_ctx_, packet_) >= 0)
			{
				while (avcodec_receive_frame(audio_ctx_, audio_frame_) == 0)
				{
					uint8_t converted[kAudioChunk * 4];
					uint8_t *out[1] = { converted };
					int samples = swr_convert(swr_, out, kAudioChunk,
						(const uint8_t **)audio_frame_->data, audio_frame_->nb_samples);
					if (samples > 0)
					{
						PushAudio(converted, samples * audio_channels_ * 2);
					}
				}
			}
		}
		av_packet_unref(packet_);
	}
}

void TVPLinuxVideoPlayer::PublishFrame(AVFrame *frame, double &first_pts)
{
	double pts = 0.0;
	if (frame->pts != AV_NOPTS_VALUE)
	{
		pts = (double)frame->pts * av_q2d(video_time_base_);
	}
	if (first_pts < 0.0)
	{
		first_pts = pts;
		clock_base_.store(NowSeconds());
	}
	double relative = pts - first_pts;
	if (relative < 0.0)
	{
		relative = 0.0;
	}

	/* Pace the video against the wall clock: the audio device consumes its own
	 * queue at the correct rate, so this keeps the picture close to the sound
	 * without a full audio-clock implementation. */
	double target = clock_base_.load() + relative;
	while (!quit_.load() && !paused_.load())
	{
		double now = NowSeconds();
		if (now >= target)
		{
			break;
		}
		double wait = target - now;
		SDL_Delay((Uint32)(wait > 0.01 ? 5 : 1));
	}

	int back = 1 - front_buffer_;
	uint8_t *destination[4] = { frame_buffer_[back].data(), nullptr, nullptr, nullptr };
	int destination_linesize[4] = { width_ * 4, 0, 0, 0 };
	sws_scale(sws_, frame->data, frame->linesize, 0, height_, destination, destination_linesize);

	{
		std::lock_guard<std::mutex> lock(frame_mutex_);
		front_buffer_ = back;
		frame_ready_ = true;
	}
	position_.store(relative);
}

#endif /* __linux__ && !__ANDROID__ && !__OHOS__ */
