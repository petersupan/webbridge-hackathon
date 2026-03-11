#include "MyObject.h"
#include <portable-file-dialogs.h>
#include "webbridge/impl/send_frame.h"
#include <chrono>
#include <cstring>
#include <math.h>

static int frameNr = 0;

void MyObject::foo(const std::string& val)
{
	strProp = val;
	aEvent.emit(42, false);
}

bool MyObject::bar()
{
	Pod newPod;
	newPod.a = 42;
	newPod.b = 123456789ULL;
	pod = newPod;

	return !aBool();
}

void MyObject::file()
{
	auto selection = pfd::open_file("Choose file").result();
}

void MyObject::testVectors()
{
	status = Status::Running;
	std::vector<int> fibonacci = { 1, 1, 2, 3, 5, 8, 13, 21 };
	numbers = fibonacci;
	strProp = "Vector mit " + std::to_string(fibonacci.size()) + " Fibonacci-Zahlen";
	counter = static_cast<int>(fibonacci.size());
	status = Status::Completed;
}



void MyObject::transferSingleFrame()
{
	frameNr++;
	std::vector<uint8_t> imgdat;
	imgdat.resize(1024*1024*4);
	for(int i = 0; i < 1024*1024; i++) {
		imgdat[i*4] = rand() % 256;
		imgdat[i*4+1] = (i+frameNr*10)%255;
		imgdat[i*4+2] = rand() % 256;
		imgdat[i*4+3] = 255;
	}
	SharedFrameSender sender(get_webview());
	sender.send(imgdat.data(), 1024*1024*4, 1024, 1024);
}

void MyObject::startVideo()
{
	stopVideo();
	videoRunning_ = true;

	// Create sender & pre-allocate shared buffer on the main thread (COM requirement)
	auto sender = std::make_shared<SharedFrameSender>(get_webview());
	constexpr size_t dataSize = 1024 * 1024 * 4;
	sender->ensureBuffer(dataSize);

	dispatchPending_ = false;

	videoThread_ = std::thread([this, sender]() {
		frameNr = 0;

		// Generate a 1024 x 4096 source image filled with nonsense data
		constexpr int srcWidth = 1024;
		constexpr int srcHeight = 4096;
		std::vector<BYTE> srcImage(srcWidth * srcHeight * 4);
		for (int i = 0; i < srcWidth * srcHeight; i++) {
			srcImage[i * 4]     = static_cast<BYTE>(sin((i %1024) / 100.0) * 127.0 + 128.0);
			srcImage[i * 4 + 1] = (i /1024) % 255;
			srcImage[i * 4 + 2] = static_cast<BYTE>((sin((i %1024) / 100.0) * cos(i/65536.0) ) * 127.0 + 128.0);
			srcImage[i * 4 + 3] = 255;
		}

		while (videoRunning_) {

			BYTE* buf = sender->bufferPtr();

			// Pick the starting row based on frameNr, wrapping so we stay in bounds
			int startLine = frameNr % (srcHeight - srcWidth);  // 4096 - 1024 = 3072 usable offsets
			size_t srcOffset = static_cast<size_t>(startLine) * srcWidth * 4;

			// Copy 1024 * 1024 pixels from the source image into buf
			std::memcpy(buf, srcImage.data() + srcOffset, srcWidth * srcWidth * 4);

			// Only dispatch if the previous post has been consumed.
			// If still pending, skip this frame (latest data is already in the buffer).
			// Only dispatch if the previous post has been consumed.
			bool expected = false;
			int currentFrame = frameNr;
			if (dispatchPending_.compare_exchange_strong(expected, true)) {
				get_webview().dispatch([this, sender, currentFrame]() {
					sender->post(1024, 1024, "RGBA", currentFrame);
					dispatchPending_.store(false, std::memory_order_release);
				});
			}
			frameNr++;

			std::this_thread::sleep_for(std::chrono::milliseconds(15));
		}

	});
}

void MyObject::stopVideo()
{
	videoRunning_ = false;
	if (videoThread_.joinable()) {
		videoThread_.join();
	}
}

void MyObject::throwError()
{
	throw std::runtime_error("Test Exception");
}

std::string MyObject::multiParamTest(
	int intValue,
	bool boolValue,
	const std::string& strValue,
	const std::vector<int>& vecValue,
	Status statusValue,
	const Pod& podValue)
{
	counter = intValue;
	aBool = boolValue;
	strProp = strValue;
	numbers = vecValue;
	status = statusValue;
	pod = podValue;

	std::string result = "multiParamTest called with: ";
	result += "int=" + std::to_string(intValue);
	result += ", bool=" + std::string(boolValue ? "true" : "false");
	result += ", str='" + strValue + "'";
	result += ", vec.size=" + std::to_string(vecValue.size());
	result += ", status=" + std::to_string(static_cast<int>(statusValue));
	result += ", pod.a=" + std::to_string(podValue.a);
	result += ", pod.b=" + std::to_string(podValue.b);

	return result;
}
