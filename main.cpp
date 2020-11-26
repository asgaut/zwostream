#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h> // for isatty
#include <stdarg.h>
#include <stdint.h>

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
int64_t get_highres_time()
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) == -1) {
		fprintf(stderr, "clock_gettime: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	return (int64_t)(t.tv_sec)*1000 + (int64_t)(t.tv_nsec)/1000000;
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

struct options {
	bool verbose;
	int64_t duration;
	long exposure_ms;
	bool gain_auto;
	long gain;
	ASI_IMG_TYPE asi_image_type;
	int cv_array_type;
};

void parse_command_line(int argc, char **argv, options *dest)
{
	int optc;

	// Set defaults
	dest->duration = -1;
	dest->exposure_ms = 500;
	dest->gain_auto = false;
	dest->gain = 50;
	dest->asi_image_type = ASI_IMG_RAW8;
	dest->cv_array_type = CV_8UC1;
	dest->verbose = false;

	while ((optc = getopt(argc, argv, "hd:e:Gg:p:v")) != -1)
	{
		switch (optc) {
		case 'd':
			dest->duration = atoi(optarg) * 1000;
			switch (optarg[strlen(optarg) - 1]) {
			case 's':
				break;
			case 'm':
				dest->duration *= 60;
				break;
			case 'h':
				dest->duration *= 3600;
				break;
			default:
				fprintf(stderr, "invalid suffix for -d option: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'e':
			dest->exposure_ms = atoi(optarg);
			break;
		case 'G':
			dest->gain_auto = true;
			break;
		case 'g':
			dest->gain = atoi(optarg);
			break;
		case 'p':
			if (strcasecmp(optarg, "RAW8") == 0) {
				dest->asi_image_type = ASI_IMG_RAW8;
				dest->cv_array_type = CV_8UC1;
			}
			else if (strcasecmp(optarg, "RAW16") == 0) {
				dest->asi_image_type = ASI_IMG_RAW16;
				dest->cv_array_type = CV_16UC1;
			}
			else {
				fprintf(stderr, "invalid argument for -p option: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'v':
			dest->verbose = true;
			break;
		case 'h':
		case '?':
			printf("Usage: %s\n" \
			"  -d {duration} Stop streaming after this time (examples 3600s, 60m, 1h) (default infinite)\n"
			"  -e {exposure time} Set exposure time in ms (default 500 ms)\n"
			"  -G Enable auto gain (default off)\n"
			"  -g {gain (0-100)} Set gain or the initial gain if auto gain is enabled (default 50)\n"
			"  -p {RAW8 | RAW16} Set pixel format for the camera and generated output (default RAW8)\n"
			"  -h Print this help\n", argv[0]
			);
			exit(EXIT_SUCCESS);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv)
{
	int i;
	bool bresult;
	int CamIndex=0;
	options opt;

	parse_command_line(argc, argv, &opt);
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

	int fDropCount = 0;
	unsigned long fpsCount = 0, fCount = 0;
	long exp = opt.exposure_ms * 1000;
	long gain = opt.gain;
	long sensorTemp;

	ASISetControlValue(CamInfo.CameraID, ASI_EXPOSURE, exp, ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID, ASI_GAIN, gain, opt.gain_auto ? ASI_TRUE : ASI_FALSE);
	ASISetControlValue(CamInfo.CameraID, ASI_AUTO_MAX_GAIN, 100, ASI_TRUE);
	//ASISetControlValue(CamInfo.CameraID, ASI_BANDWIDTHOVERLOAD, 60, ASI_FALSE); // transfer speed percentage
	//ASISetControlValue(CamInfo.CameraID, ASI_HIGH_SPEED_MODE, 0, ASI_FALSE);

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

	// RAW8: use ffmpeg -pixel_format gray8
	// RAW16: use ffmpeg -pixel_format gray12le
	// Example usage with ffmpeg:
	//   ./zwostream -p RAW8 | ffmpeg -f rawvideo -pixel_format gray8 -vcodec rawvideo -video_size 1280x960 -i pipe:0
	ASISetROIFormat(CamInfo.CameraID, CamInfo.MaxWidth, CamInfo.MaxHeight, 1, opt.asi_image_type);
	cv::Mat img(CamInfo.MaxHeight, CamInfo.MaxWidth, opt.cv_array_type);

	ASIStartVideoCapture(CamInfo.CameraID);
	int64_t end_time = opt.duration != -1 ? get_highres_time() + opt.duration : -1;
	while (!exit_mainloop && ((end_time == -1) || (end_time - get_highres_time() > 0)))
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

	fprintf(stderr, "Frames written: %lu Dropped: %d\n", fCount, fDropCount);
	ASICloseCamera(CamInfo.CameraID);
	return 0;
}
