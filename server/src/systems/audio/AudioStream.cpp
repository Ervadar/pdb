#include "AudioStream.h"
#include <boost/log/trivial.hpp>

namespace Pdb
{

AudioStream::AudioStream(float& masterVolume) : paused_(false), masterVolume_(masterVolume)
{
    rtAudio_ = std::make_unique<RtAudio>();
    int nDevices = rtAudio_->getDeviceCount();
    if (nDevices < 1) {
        BOOST_LOG_TRIVIAL(error) << "No audio devices found!";
        exit(0);
    }
    parameters_.deviceId = rtAudio_->getDefaultOutputDevice();
}

void AudioStream::pauseToggle()
{
    if(paused_)
    {
        rtAudio_->startStream();
        paused_ = false;
    }
    else
    {
        paused_ = true;
        rtAudio_->stopStream();
    }
}

int playCb(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
        double streamTime, RtAudioStreamStatus status, void *userData)
{
    return ((AudioStream*)userData)->playCallback(outputBuffer, inputBuffer, nBufferFrames, streamTime, status);
}

}