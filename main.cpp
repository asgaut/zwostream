#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h> // for isatty
#include <stdarg.h>

#define DISABLE_OPENCV_24_COMPATIBILITY
#include <opencv2/imgproc/imgproc.hpp>

#include "ASICamera2.h"

// Format: https://en.cppreference.com/w/c/io/fprintf
void imgPrintf(cv::InputOutputArray img, const char* format, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	cv::putText(img, buffer, cv::Point(5,20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200,200,200), 1);
}

// Macros for operating on timeval structures are described in
// http://linux.die.net/man/3/timeradd
unsigned int get_highres_time()
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) == -1) {
		fprintf(stderr, "clock_gettime: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	return t.tv_sec*1000 + t.tv_nsec/1000000;
}

bool exit_mainloop;

static void sigint_handler(int sig, siginfo_t *si, void *unused)
{
	struct sigaction sa;

	fprintf(stderr, "Caught %s.\n", sig == SIGINT ? "SIGINT" : "SIGTERM");
	// Remove the handler
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = 0;
	sa.sa_flags = SA_RESETHAND;
	if (sigaction(sig, &sa, NULL) == -1) {
		fprintf(stderr, "sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	// Break the main loop
	exit_mainloop = true;
}

static void install_sigint_handler(void)
{
	struct sigaction sa;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigint_handler;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		fprintf(stderr, "sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		fprintf(stderr, "sigaction: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

int  main()
{
	int i;
	bool bresult;
	int CamIndex=0;

	install_sigint_handler();

	int numDevices = ASIGetNumOfConnectedCameras();
	if (numDevices <= 0)
	{
		fprintf(stderr, "no camera connected\n");
		exit(EXIT_FAILURE);
	}

	ASI_CAMERA_INFO CamInfo;
	fprintf(stderr, "Attached cameras:\n");
	for (i = 0; i < numDevices; i++)
	{
		ASIGetCameraProperty(&CamInfo, i);
		fprintf(stderr, "\t%d: %s\n", i, CamInfo.Name);
	}
	CamIndex = 0;

	ASIGetCameraProperty(&CamInfo, CamIndex);
	bresult = ASIOpenCamera(CamInfo.CameraID);
	bresult += ASIInitCamera(CamInfo.CameraID);
	if (bresult)
	{
		fprintf(stderr, "OpenCamera error, are you root?\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "%s information\n",CamInfo.Name);
	fprintf(stderr, "\tResolution: %ldx%ld\n", CamInfo.MaxWidth, CamInfo.MaxHeight);
	if (CamInfo.IsColorCam) {
		const char* bayer[] = { "RG", "BG", "GR", "GB" };
		fprintf(stderr, "\tColor Camera: bayer pattern: %s\n", bayer[CamInfo.BayerPattern]);
	}
	else
		fprintf(stderr, "\tMono camera\n");

	int ctrlnum;
	ASIGetNumOfControls(CamInfo.CameraID, &ctrlnum);
	ASI_CONTROL_CAPS ctrlcap;
	for (i = 0; i < ctrlnum; i++) {
		ASIGetControlCaps(CamInfo.CameraID, i, &ctrlcap);
		fprintf(stderr, "\t%s '%s' [%ld,%ld] %s\n", ctrlcap.Name, ctrlcap.Description,
			ctrlcap.MinValue, ctrlcap.MaxValue,
			ctrlcap.IsAutoSupported?"(Auto supported)":"(Manual only)");
	}

	int fDropCount;
	unsigned long fpsCount = 0, fCount = 0;
	long exp = 250*1000;
	long gain = 50;
	long sensorTemp;

	ASISetControlValue(CamInfo.CameraID, ASI_EXPOSURE, exp, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, ASI_TRUE);
	ASISetControlValue(CamInfo.CameraID, ASI_AUTO_MAX_GAIN, 80, ASI_TRUE);
	//ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 60, ASI_FALSE); // transfer speed percentage
	//ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 0, ASI_FALSE);
	//ASISetControlValue(CamInfo.CameraID, ASI_WB_B, 90, ASI_FALSE);
	//ASISetControlValue(CamInfo.CameraID, ASI_WB_R, 48, ASI_TRUE);

	if (CamInfo.IsTriggerCam)
	{
		ASI_CAMERA_MODE mode;
		// Multi mode camera, need to select the camera mode
		ASISetCameraMode(CamInfo.CameraID, ASI_MODE_NORMAL);
		ASIGetCameraMode(CamInfo.CameraID, &mode);
		if (mode != ASI_MODE_NORMAL)
			fprintf(stderr, "Set mode failed!\n");
	}

	if (isatty(fileno(stdout))) {
		fprintf(stderr, "stdout is a tty, will not dump video data\n");
		exit_mainloop = true;
	}

	// RAW16 does not seem to work with USB2 on RPi3 (causes too long transfer times?)
	// ASI_IMG_RAW8: use ffmpeg -pixel_format gray8
	// ASI_IMG_RAW16: use ffmpeg -pixel_format gray12le
	// Common (pipe from stdin): ffmpeg -f rawvideo -vcodec rawvideo -video_size 1280x960 -i pipe:0
	ASI_IMG_TYPE Image_type = ASI_IMG_RAW8;
	ASISetROIFormat(CamInfo.CameraID, CamInfo.MaxWidth, CamInfo.MaxHeight, 1, Image_type);
	cv::Mat img(CamInfo.MaxHeight, CamInfo.MaxWidth, CV_8UC1);

	ASIStartVideoCapture(CamInfo.CameraID);
	while (!exit_mainloop)
	{
		ASI_ERROR_CODE code;
		ASI_BOOL bVal;

		code = ASIGetVideoData(CamInfo.CameraID, img.data, img.elemSize()*img.size().area(), 2000);
		if (code != ASI_SUCCESS) {
			fprintf(stderr, "ASIGetVideoData() error: %d\n", code);
			exit(EXIT_FAILURE);
		}
		fCount++;
		fpsCount++;
		ASIGetControlValue(CamInfo.CameraID, ASI_GAIN, &gain, &bVal);
		ASIGetControlValue(CamInfo.CameraID, ASI_EXPOSURE, &exp, &bVal);
		ASIGetControlValue(CamInfo.CameraID, ASI_TEMPERATURE, &sensorTemp, &bVal);
		ASIGetDroppedFrames(CamInfo.CameraID, &fDropCount);

		char timestamp[20] = {0};
		struct tm *tmp;
		time_t t = time(NULL);
		tmp = gmtime(&t);
		strftime(timestamp, sizeof(timestamp), "%Y%m%d %H%M%SZ", tmp);
		imgPrintf(img, "%s Gain:%ld Exp:%ldms Frame:%lu Dropped:%u Temp:%.0fC",
			timestamp, gain, exp/1000, fCount, fDropCount, sensorTemp/10.0);
		fwrite(img.data, img.elemSize(), img.size().area(), stdout);
		fflush(stdout);
	}
	ASIStopVideoCapture(CamInfo.CameraID);

	fprintf(stderr, "Frames written: %lu\n", fCount);
	ASICloseCamera(CamInfo.CameraID);
	return 0;
}
