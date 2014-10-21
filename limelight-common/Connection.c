#include "Limelight-internal.h"
#include "Platform.h"

static int stage = STAGE_NONE;
static CONNECTION_LISTENER_CALLBACKS listenerCallbacks;
static CONNECTION_LISTENER_CALLBACKS originalCallbacks;

static int alreadyTerminated;

static const char* stageNames[STAGE_MAX] = {
	"none",
	"platform initialization",
	"RTSP handshake",
	"control stream initialization",
	"video stream initialization",
	"audio stream initialization",
	"input stream initialization",
	"control stream establishment",
	"video stream establishment",
	"audio stream establishment",
	"input stream establishment"
};

const char* LiGetStageName(int stage) {
	return stageNames[stage];
}

void LiStopConnection(void) {
	if (stage == STAGE_INPUT_STREAM_START) {
		Limelog("Stopping input stream...");
		stopInputStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_AUDIO_STREAM_START) {
		Limelog("Stopping audio stream...");
		stopAudioStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_VIDEO_STREAM_START) {
		Limelog("Stopping video stream...");
		stopVideoStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_CONTROL_STREAM_START) {
		Limelog("Stopping control stream...");
		stopControlStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_INPUT_STREAM_INIT) {
		Limelog("Cleaning up input stream...");
		destroyInputStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_AUDIO_STREAM_INIT) {
		Limelog("Cleaning up audio stream...");
		destroyAudioStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_VIDEO_STREAM_INIT) {
		Limelog("Cleaning up video stream...");
		destroyVideoStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_CONTROL_STREAM_INIT) {
		Limelog("Cleaning up control stream...");
		destroyControlStream();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_RTSP_HANDSHAKE) {
		Limelog("Terminating RTSP handshake...");
		terminateRtspHandshake();
		stage--;
		Limelog("done\n");
	}
	if (stage == STAGE_PLATFORM_INIT) {
		Limelog("Cleaning up platform...");
		cleanupPlatformSockets();
		cleanupPlatformThreads();
		stage--;
		Limelog("done\n");
	}
	LC_ASSERT(stage == STAGE_NONE);
}

static void ClStageStarting(int stage)
{
    originalCallbacks.stageStarting(stage);
}

static void ClStageComplete(int stage)
{
    originalCallbacks.stageComplete(stage);
}

static void ClStageFailed(int stage, long errorCode)
{
    originalCallbacks.stageFailed(stage, errorCode);
}

static void ClConnectionStarted(void)
{
    originalCallbacks.connectionStarted();
}

static void ClConnectionTerminated(long errorCode)
{
    // Avoid recursion and issuing multiple callbacks
    if (alreadyTerminated) {
        return;
    }
    
    alreadyTerminated = 1;
    originalCallbacks.connectionTerminated(errorCode);
}

void ClDisplayMessage(char* message)
{
    originalCallbacks.displayMessage(message);
}

void ClDisplayTransientMessage(char* message)
{
    originalCallbacks.displayTransientMessage(message);
}

int LiStartConnection(IP_ADDRESS host, PSTREAM_CONFIGURATION streamConfig, PCONNECTION_LISTENER_CALLBACKS clCallbacks,
	PDECODER_RENDERER_CALLBACKS drCallbacks, PAUDIO_RENDERER_CALLBACKS arCallbacks, void* renderContext, int drFlags) {
	int err;

	memcpy(&originalCallbacks, clCallbacks, sizeof(originalCallbacks));
    
    listenerCallbacks.stageStarting = ClStageStarting;
    listenerCallbacks.stageComplete = ClStageComplete;
    listenerCallbacks.stageFailed = ClStageFailed;
    listenerCallbacks.connectionStarted = ClConnectionStarted;
    listenerCallbacks.connectionTerminated = ClConnectionTerminated;
    listenerCallbacks.displayMessage = ClDisplayMessage;
    listenerCallbacks.displayTransientMessage = ClDisplayTransientMessage;
    
    alreadyTerminated = 0;

	Limelog("Initializing platform...");
	listenerCallbacks.stageStarting(STAGE_PLATFORM_INIT);
	err = initializePlatformSockets();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_PLATFORM_INIT, err);
		goto Cleanup;
	}
	err = initializePlatformThreads();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_PLATFORM_INIT, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_PLATFORM_INIT);
	listenerCallbacks.stageComplete(STAGE_PLATFORM_INIT);
	Limelog("done\n");

	Limelog("Starting RTSP handshake...");
	listenerCallbacks.stageStarting(STAGE_RTSP_HANDSHAKE);
	err = performRtspHandshake(host, streamConfig);
	if (err != 0) {
		Limelog("failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_RTSP_HANDSHAKE, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_RTSP_HANDSHAKE);
	listenerCallbacks.stageComplete(STAGE_RTSP_HANDSHAKE);
	Limelog("done\n");

	Limelog("Initializing control stream...");
	listenerCallbacks.stageStarting(STAGE_CONTROL_STREAM_INIT);
	err = initializeControlStream(host, streamConfig, &listenerCallbacks);
	if (err != 0) {
		Limelog("failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_CONTROL_STREAM_INIT, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_CONTROL_STREAM_INIT);
	listenerCallbacks.stageComplete(STAGE_CONTROL_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing video stream...");
	listenerCallbacks.stageStarting(STAGE_VIDEO_STREAM_INIT);
	initializeVideoStream(host, streamConfig, drCallbacks, &listenerCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_VIDEO_STREAM_INIT);
	listenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing audio stream...");
	listenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_INIT);
	initializeAudioStream(host, arCallbacks, &listenerCallbacks);
	stage++;
	LC_ASSERT(stage == STAGE_AUDIO_STREAM_INIT);
	listenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_INIT);
	Limelog("done\n");

	Limelog("Initializing input stream...");
	listenerCallbacks.stageStarting(STAGE_INPUT_STREAM_INIT);
	initializeInputStream(host, &listenerCallbacks,
		streamConfig->remoteInputAesKey, sizeof(streamConfig->remoteInputAesKey),
		streamConfig->remoteInputAesIv, sizeof(streamConfig->remoteInputAesIv));
	stage++;
	LC_ASSERT(stage == STAGE_INPUT_STREAM_INIT);
	listenerCallbacks.stageComplete(STAGE_INPUT_STREAM_INIT);
	Limelog("done\n");

	Limelog("Starting control stream...");
	listenerCallbacks.stageStarting(STAGE_CONTROL_STREAM_START);
	err = startControlStream();
	if (err != 0) {
		Limelog("failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_CONTROL_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_CONTROL_STREAM_START);
	listenerCallbacks.stageComplete(STAGE_CONTROL_STREAM_START);
	Limelog("done\n");

	Limelog("Starting video stream...");
	listenerCallbacks.stageStarting(STAGE_VIDEO_STREAM_START);
	err = startVideoStream(renderContext, drFlags);
	if (err != 0) {
		Limelog("Video stream start failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_VIDEO_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_VIDEO_STREAM_START);
	listenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_START);
	Limelog("done\n");

	Limelog("Starting audio stream...");
	listenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_START);
	err = startAudioStream();
	if (err != 0) {
		Limelog("Audio stream start failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_AUDIO_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_AUDIO_STREAM_START);
	listenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_START);
	Limelog("done\n");

	Limelog("Starting input stream...");
	listenerCallbacks.stageStarting(STAGE_INPUT_STREAM_START);
	err = startInputStream();
	if (err != 0) {
		Limelog("Input stream start failed: %d\n", err);
		listenerCallbacks.stageFailed(STAGE_INPUT_STREAM_START, err);
		goto Cleanup;
	}
	stage++;
	LC_ASSERT(stage == STAGE_INPUT_STREAM_START);
	listenerCallbacks.stageComplete(STAGE_INPUT_STREAM_START);
	Limelog("done\n");

	listenerCallbacks.connectionStarted();

Cleanup:
	return err;
}