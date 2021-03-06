#include "VoiceManager.h"
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include <utility>

#include "Config.h"

namespace Pdb
{

VoiceManager::VoiceManager()
{
	BOOST_LOG_TRIVIAL(info) << "Initializing VoiceManager";
    Aws::InitAPI(options_);
}

VoiceManager::~VoiceManager()
{
    Aws::ShutdownAPI(options_);
}

void VoiceManager::synthesizeVoiceMessage(const std::string& message, const std::string& outputDirectory, const std::string& outputTrackName)
{
    std::unique_lock<std::mutex> lock(mutex_);
    boost::filesystem::path synthesizedVoiceMessageFilePath(outputDirectory + "/" + outputTrackName + ".mp3");
    if (boost::filesystem::exists(synthesizedVoiceMessageFilePath))
    {
        synthesizedVoiceAudioTracks_.insert(std::make_pair(outputTrackName, AudioTrack(synthesizedVoiceMessageFilePath.c_str(), Config::getInstance().volumeForAwsSynthesized, AudioTrack::Type::VOICE_MESSAGE)));
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Synthesizing a voice message: " << message << " (path: " << synthesizedVoiceMessageFilePath.c_str() << ").";

    Aws::Polly::PollyClient pollyClient;
    Aws::Polly::Model::SynthesizeSpeechRequest speechRequest;
    speechRequest.SetTextType(Aws::Polly::Model::TextType::ssml);
    speechRequest.SetVoiceId(Aws::Polly::Model::VoiceId::Ewa);
    speechRequest.SetOutputFormat(Aws::Polly::Model::OutputFormat::mp3);
    speechRequest.SetText(message.c_str());
    lock.unlock();
    auto result = pollyClient.SynthesizeSpeech(speechRequest);

    if (result.IsSuccess())
	{
        BOOST_LOG_TRIVIAL(info) << "Speech synthesis was successfull. Saving to file " << synthesizedVoiceMessageFilePath;

        Aws::String     filePath = synthesizedVoiceMessageFilePath.c_str();
        Aws::IOStream*  audioStream = &result.GetResult().GetAudioStream();
        Aws::OFStream   voiceFile;
        voiceFile.open(filePath.c_str(), std::ios::out | std::ios::binary);
        voiceFile.write(GetStreamBytes(audioStream), GetStreamSize(audioStream));
        voiceFile.close();
        lock.lock();
        synthesizedVoiceAudioTracks_.insert(std::make_pair(outputTrackName, AudioTrack(synthesizedVoiceMessageFilePath.c_str(), Config::getInstance().volumeForAwsSynthesized, AudioTrack::Type::VOICE_MESSAGE)));
        lock.unlock();
        BOOST_LOG_TRIVIAL(info) << "Saving to file done.";
    }
    else
    {
        BOOST_LOG_TRIVIAL(error) << "Speech synthesis failed. Error: " << result.GetError().GetMessage();
	}

}

int VoiceManager::GetStreamSize(Aws::IOStream* stream)
{
    // Ensure the stream is at the beginning
                stream->seekg (0, std::ios::beg);
    int begin = stream->tellg();
                stream->seekg (0, std::ios::end);
    int end =   stream->tellg();
                stream->seekg (0, std::ios::beg);

    return end-begin;
}

char* VoiceManager::GetStreamBytes(Aws::IOStream* stream)
{
    // Read the stream bytes into a memory block
    int size = GetStreamSize(stream);
    char* bytes = new char[size];
    stream->read(bytes, size);

    return bytes;
}


}