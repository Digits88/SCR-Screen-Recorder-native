
#include "capture.h"

using namespace android;

void setupInput() {
    if (useFb) {
        setupFb();
    } else {
        setupScreenshot();
    }

    if (allowVerticalFrames && inputWidth < inputHeight && (rotation == 0 || rotation == 180)) {
        swapPadding();
        videoWidth = inputWidth + 2 * paddingWidth;
        videoHeight = inputHeight + 2 * paddingHeight;
        rotateView = false;
    } else if (allowVerticalFrames && inputWidth > inputHeight && (rotation == 90 || rotation == 270)) {
        swapPadding();
        videoWidth = inputHeight + 2 * paddingWidth;
        videoHeight = inputWidth + 2 * paddingHeight;
        rotateView = true;
    } else {
        if (inputWidth > inputHeight) {
            videoWidth = inputWidth + 2 * paddingWidth;
            videoHeight = inputHeight + 2 * paddingHeight;
            rotateView = false;
        } else {
            videoWidth = inputHeight + 2 * paddingWidth;
            videoHeight = inputWidth + 2 * paddingHeight;
            rotateView = true;
        }
    }
}

void setupFb() {
    ALOGV("Setting up FB mmap");
    const char* fbpath = "/dev/graphics/fb0";
    fbFd = open(fbpath, O_RDONLY);

    if (fbFd < 0) {
        stop(202, "Error opening FB device");
    }

    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &fbInfo) != 0) {
        stop(203, "FB ioctl failed");
    }

    if (ioctl(fbFd, FBIOGET_FSCREENINFO, &fbFixInfo) != 0) {
        stop(203, "FB ioctl fix failed");
    }

    int bytespp = fbInfo.bits_per_pixel / 8;
    size_t offset = (fbInfo.xoffset + fbInfo.yoffset * fbInfo.xres) * bytespp;
    inputWidth = fbInfo.xres;
    inputHeight = fbInfo.yres;
    inputStride = fbFixInfo.line_length / bytespp;
    ALOGV("FB stride: %d width: %d hieght: %d bytespp: %d", inputStride, inputWidth, inputHeight, bytespp);

    fbMapBase = mmap(0, fbFixInfo.smem_len, PROT_READ, MAP_SHARED, fbFd, 0);
    if (fbMapBase == MAP_FAILED) {
        ALOGE("mmap failed (size: %d) : %s", fbFixInfo.smem_len, strerror(errno));
        stop(204, "mmap failed");
    }
    inputBase = (void const *)((char const *)fbMapBase + offset);
}

void setupScreenshot() {
    screenshot = new ScreenshotClient();
#if SCR_SDK_VERSION >= 21
    display = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    if (display == NULL) {
        stop(205, "Can't access display");
    }
    if (screenshot->update(display, Rect(0, 0), false) != NO_ERROR) {
        stop(217, "screenshot->update() failed");
    }
    #elif SCR_SDK_VERSION >= 17
    display = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    if (display == NULL) {
        stop(205, "Can't access display");
    }
    if (screenshot->update(display) != NO_ERROR) {
        stop(217, "screenshot->update() failed");
    }
    #else
    if (screenshot->update() != NO_ERROR) {
        stop(217, "screenshot->update() failed");
    }
#endif // SCR_SDK_VERSION

    if ((screenshot->getWidth() < screenshot->getHeight()) != (reqWidth < reqHeight)) {
        ALOGI("swapping dimensions");
        int tmp = reqWidth;
        reqWidth = reqHeight;
        reqHeight = tmp;
    }
    screenshotUpdate(reqWidth, reqHeight);
    inputWidth = screenshot->getWidth();
    inputHeight = screenshot->getHeight();
    inputStride = screenshot->getStride();
    if (useOes) {
        screenshot->release();
    }
    ALOGV("Screenshot width: %d, height: %d, stride: %d, format %d, size: %d", inputWidth, inputHeight, inputStride, screenshot->getFormat(), screenshot->getSize());
}

void swapPadding() {
    int tmp = paddingWidth;
    paddingWidth = paddingHeight;
    paddingHeight = tmp;
}

void adjustRotation() {
    if (rotateView) {
        rotation = (rotation + 90) % 360;
    }
}

void updateInput() {
    if (stopping)
        return;

    if (useFb) {
        updateFb();
    } else if (useOes) {
        updateOes();
    } else {
        updateScreenshot();
    }
}

void updateFb() {
    // it's still flickering, maybe ioctl(fd, FBIO_WAITFORVSYNC, &crt); would help
    if (ioctl(fbFd, FBIOGET_VSCREENINFO, &fbInfo) != 0) {
        stop(223, "FB ioctl failed");
    }
    int bytespp = fbInfo.bits_per_pixel / 8;
    size_t offset = (fbInfo.xoffset + fbInfo.yoffset * fbInfo.xres) * bytespp;
    inputBase = (void const *)((char const *)fbMapBase + offset);
}

void updateOes() {
    #if SCR_SDK_VERSION >= 21
    if (glConsumer.get() == NULL) {
        BufferQueue::createBufferQueue(&producer, &consumer);
        glConsumer = new GLConsumer(consumer, 1, GLConsumer::TEXTURE_EXTERNAL, true, false);
        ALOGV("Creating GLConsumer");
    }
    if (ScreenshotClient::capture(display, producer, Rect(0, 0), reqWidth, reqHeight, 0, -1, false) != NO_ERROR) {
        stop(217, "capture failed");
    }
    #elif SCR_SDK_VERSION == 19
    if (glConsumer.get() == NULL) {
        bufferQueue = new BufferQueue();
        glConsumer = new GLConsumer(bufferQueue, 1);
        ALOGV("Creating GLConsumer");
    }
    if (ScreenshotClient::capture(display, bufferQueue, reqWidth, reqHeight, 0, -1) != NO_ERROR) {
        stop(217, "capture failed");
    }
    #elif SCR_SDK_VERSION >= 18
    if (glConsumer.get() != NULL) {
        glConsumer.clear();
    }
    glConsumer = new GLConsumer(1);
    glConsumer->setName(String8("scr_consumer"));
         if (ScreenshotClient::capture(display, glConsumer->getBufferQueue(),
            reqWidth, reqHeight, 0, -1) != NO_ERROR) {
        stop(217, "capture failed");
    }
    #endif // SCR_SDK_VERSION
}

void updateScreenshot() {
    inputBase = NULL;
    if (screenshotUpdate(reqWidth, reqHeight) == NO_ERROR) {
        inputBase = screenshot->getPixels();
    }
}

status_t screenshotUpdate(int reqWidth, int reqHeight) {
    status_t err = NO_ERROR;

    #if SCR_SDK_VERSION >= 18
        screenshot->release();
    #endif

    #if SCR_SDK_VERSION >= 21
    err = screenshot->update(display, Rect(0, 0), reqWidth, reqHeight, false);
    #elif SCR_SDK_VERSION >= 17
    err = screenshot->update(display, reqWidth, reqHeight);
    #else
    err = screenshot->update(reqWidth, reqHeight);
    #endif // SCR_SDK_VERSION

    if (err != NO_ERROR) {
        updateErrors++;
        ALOGW("update error %d", updateErrors);
        if (frameCount < 2 || updateErrors > MAX_UPDATE_ERRORS) {
            stop(217, "update failed");
        }
    } else {
        updateErrors = 0;
    }
    return err;
}

void closeInput() {
    ALOGV("Closing input");

    if (useFb) {
        if (fbFd >= 0) {
            close(fbFd);
            fbFd = -1;
        }
        ALOGV("Input closed.");
        return;
    }

    delete screenshot;
    #if SCR_SDK_VERSION >= 17
    if (display.get() != NULL) {
        display.clear();
    }
    #endif // SCR_SDK_VERSION 17
    #if SCR_SDK_VERSION >= 18
    if (glConsumer.get() != NULL) {
        glConsumer.clear();
    }
    #endif // SCR_SDK_VERSION 18
    #if SCR_SDK_VERSION >= 21
    if (consumer.get() != NULL) {
        consumer.clear();
    }
    if (producer.get() != NULL) {
        producer.clear();
    }
    #endif // SCR_SDK_VERSION 21
    #if SCR_SDK_VERSION == 19
    if (bufferQueue.get() != NULL) {
        bufferQueue.clear();
    }
    #endif // SCR_SDK_VERSION 19
    ALOGV("Input closed.");
}


void updateTexImage() {
    #if SCR_SDK_VERSION >= 18
    if (useOes) {
        if (glConsumer->updateTexImage() != NO_ERROR) {
            if (!stopping) {
                stop(226, "texture update failed");
            }
        }
    }
    #endif
}
