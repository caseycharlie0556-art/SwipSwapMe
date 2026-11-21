#include <wups.h>
#include <mic/mic.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/memdefaultheap.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

WUPS_PLUGIN_NAME("GamePad Mic Streamer");
WUPS_PLUGIN_DESCRIPTION("Streams GamePad mic PCM over UDP");
WUPS_PLUGIN_VERSION("v0.1");
WUPS_PLUGIN_AUTHOR("Custom");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

#define CONFIG_PATH "sd:/wiiu/mic_ip.txt"
#define PC_PORT     5555

static OSThread gThread;
static uint8_t *gStack = nullptr;
static bool gRunning = false;

static bool ReadTargetIP(char *outIp, size_t maxLen) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return false;
    if (!fgets(outIp, maxLen, f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    outIp[strcspn(outIp, "\r\n \t")] = '\0';
    return strlen(outIp) > 0;
}

int MicThreadEntry(int argc, const char **argv) {
    char targetIp[64] = {0};
    if (!ReadTargetIP(targetIp, sizeof(targetIp))) {
        return 0;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return 0;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PC_PORT);
    if (inet_pton(AF_INET, targetIp, &dest.sin_addr) != 1) {
        close(sock);
        return 0;
    }

    MICWorkMemory workMem{};
    MICHandle handle = MICInit(MIC_INSTANCE_0, 0, &workMem, nullptr);
    if (handle < 0 || MICOpen(handle) != MIC_ERROR_OK) {
        close(sock);
        return 0;
    }

    MICSetState(handle, 0, 1);

    MICStatus status{};
    while (gRunning) {
        if (MICGetStatus(handle, &status) == MIC_ERROR_OK && status.availableData > 0) {
            uint32_t samples = status.availableData;
            if (samples > 512) samples = 512;
            sendto(sock, status.buffer, samples * sizeof(int16_t), 0,
                   (sockaddr *)&dest, sizeof(dest));
            MICSetDataConsumed(handle, samples);
        }
        OSSleepTicks(OSMillisecondsToTicks(5));
    }

    MICSetState(handle, 0, 0);
    MICClose(handle);
    MICUninit(handle);
    close(sock);
    return 0;
}

INITIALIZE_PLUGIN() {
    gRunning = true;
    gStack = (uint8_t *)MEMAllocFromDefaultHeapEx(0x8000, 16);
    if (gStack && OSCreateThread(&gThread, MicThreadEntry, 0, nullptr,
                                 gStack + 0x8000, 0x8000, 16, OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        OSResumeThread(&gThread);
    }
}

DEINITIALIZE_PLUGIN() {
    gRunning = false;
    if (gStack) {
        OSJoinThread(&gThread, nullptr);
        MEMFreeToDefaultHeap(gStack);
        gStack = nullptr;
    }
}
