#include "pch.h"
#include "custom_video_capturer.h"
#include <fstream>

namespace webrtc
{
	class CustomVideoCapturer::InsertFrameTask : public rtc::QueuedTask {
	public:
		// Repeats in |repeat_interval_ms|. One-time if |repeat_interval_ms| == 0.
		InsertFrameTask(
			CustomVideoCapturer* frame_generator_capturer,
			uint32_t repeat_interval_ms)
			: frame_generator_capturer_(frame_generator_capturer),
			repeat_interval_ms_(repeat_interval_ms),
			intended_run_time_ms_(-1) {}

	private:
		bool Run() override {
			bool task_completed = true;
			if (repeat_interval_ms_ > 0) {
				// This is not a one-off frame. Check if the frame interval for this
				// task queue is the same same as the current configured frame rate.
				uint32_t current_interval_ms =
					1000 / frame_generator_capturer_->GetCurrentConfiguredFramerate();
				if (repeat_interval_ms_ != current_interval_ms) {
					// Frame rate has changed since task was started, create a new instance.
					rtc::TaskQueue::Current()->PostDelayedTask(
						std::unique_ptr<rtc::QueuedTask>(new InsertFrameTask(
							frame_generator_capturer_, current_interval_ms)),
						current_interval_ms);
				}
				else {
					// Schedule the next frame capture event to happen at approximately the
					// correct absolute time point.
					int64_t delay_ms;
					int64_t time_now_ms = rtc::TimeMillis();
					if (intended_run_time_ms_ > 0) {
						delay_ms = time_now_ms - intended_run_time_ms_;
					}
					else {
						delay_ms = 0;
						intended_run_time_ms_ = time_now_ms;
					}
					intended_run_time_ms_ += repeat_interval_ms_;
					if (delay_ms < repeat_interval_ms_) {
						rtc::TaskQueue::Current()->PostDelayedTask(
							std::unique_ptr<rtc::QueuedTask>(this),
							repeat_interval_ms_ - delay_ms);
					}
					else {
						rtc::TaskQueue::Current()->PostDelayedTask(
							std::unique_ptr<rtc::QueuedTask>(this), 0);
					}
					// Repost of this instance, make sure it is not deleted.
					task_completed = false;
				}
			}
			frame_generator_capturer_->InsertFrame();
			// Task should be deleted only if it's not repeating.
			return task_completed;
		}

		CustomVideoCapturer* const frame_generator_capturer_;
		const uint32_t repeat_interval_ms_;
		int64_t intended_run_time_ms_;
	};

	CustomVideoCapturer::CustomVideoCapturer(webrtc::Clock* clock,
		int target_fps) : 
		clock_(clock),
		sending_(false),
		sink_(nullptr),
		use_software_encoder_(false),
		sink_wants_observer_(nullptr),
		target_fps_(target_fps),
		first_frame_capture_time_(-1),
		task_queue_("FrameGenCapQ",
			rtc::TaskQueue::Priority::HIGH)
	{
		SetCaptureFormat(NULL);
		set_enable_video_adapter(false);

		// Default supported formats. Use ResetSupportedFormats to over write.
		std::vector<cricket::VideoFormat> formats;
		formats.push_back(cricket::VideoFormat(640, 480, cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_H264));
		formats.push_back(cricket::VideoFormat(1280, 720, cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_H264));
		formats.push_back(cricket::VideoFormat(640, 480, cricket::VideoFormat::FpsToInterval(60), cricket::FOURCC_H264));
		formats.push_back(cricket::VideoFormat(1280, 720, cricket::VideoFormat::FpsToInterval(60), cricket::FOURCC_H264));
		SetSupportedFormats(formats);
	}

	int CustomVideoCapturer::GetCurrentConfiguredFramerate() {
		rtc::CritScope cs(&lock_);
		if (wanted_fps_ && *wanted_fps_ < target_fps_)
			return *wanted_fps_;
		return target_fps_;
	}

	bool CustomVideoCapturer::Init() {
		return true;
	}

	cricket::CaptureState CustomVideoCapturer::Start(const cricket::VideoFormat& format) {
		SetCaptureFormat(&format);
		Init();
		running_ = true;
		sending_ = true;
		SetCaptureState(cricket::CS_RUNNING);
		return cricket::CS_RUNNING;
	}

	void CustomVideoCapturer::InsertFrame() {
		rtc::CritScope cs(&lock_);
	}

	void CustomVideoCapturer::Stop() {
		rtc::CritScope cs(&lock_);
		sending_ = false;
	}

	void CustomVideoCapturer::SetSinkWantsObserver(SinkWantsObserver* observer) {
		rtc::CritScope cs(&lock_);
		RTC_DCHECK(!sink_wants_observer_);
		sink_wants_observer_ = observer;
	}

	void CustomVideoCapturer::AddOrUpdateSink(
		rtc::VideoSinkInterface<VideoFrame>* sink,
		const rtc::VideoSinkWants& wants) {
		rtc::CritScope cs(&lock_);
		//RTC_CHECK(!sink_ || sink_ == sink);
		sink_ = sink;
		if (sink_wants_observer_)
			sink_wants_observer_->OnSinkWantsChanged(sink, wants);
	}

	bool CustomVideoCapturer::IsRunning() { return running_; }
	bool CustomVideoCapturer::IsScreencast() const { return false; }

	bool CustomVideoCapturer::GetPreferredFourccs(std::vector<uint32_t>* fourccs) {
		fourccs->push_back(cricket::FOURCC_H264);
		return true;
	}

	void CustomVideoCapturer::SetFakeRotation(VideoRotation rotation) {
		rtc::CritScope cs(&lock_);
		fake_rotation_ = rotation;
	}

	void CustomVideoCapturer::ChangeResolution(size_t width, size_t height) {
		//TODO - change nvencode settings
	}

	void CustomVideoCapturer::RemoveSink(
		rtc::VideoSinkInterface<VideoFrame>* sink) {
		rtc::CritScope cs(&lock_);
		//RTC_CHECK(sink_ == sink);
		sink_ = nullptr;
	}

	void CustomVideoCapturer::ForceFrame() {
		// One-time non-repeating task,
		// therefore repeat_interval_ms is 0 in InsertFrameTask()
		task_queue_.PostTask(
			std::unique_ptr<rtc::QueuedTask>(new InsertFrameTask(this, 0)));
	}
};