#include "AudiobookPlayer.h"
#include <boost/log/trivial.hpp>
#include <future>
#include <memory>
#include <iterator>
#include <sstream>
#include <fstream>
#include <boost/filesystem.hpp>

namespace filesystem = boost::filesystem;

namespace Pdb
{

AudiobookPlayer::AudiobookPlayer(AudioManager& audioManager, VoiceManager& voiceManager) 
    : audioManager_(audioManager), voiceManager_(voiceManager), fastForwardingSpeed_(0), currentAudioTask_(nullptr), pausedAudioTask_(nullptr),
    trackInfoPattern_(std::string("^(.+)([[:space:]])([0-9]|[1-9][0-9]*)$"))
{
    this->loadTracks();
    this->loadTracksInfo();
    this->synchronizeTracksInfo();
    currentTrackIndex_ = 0;
    currentState_ = State::CHOOSING;

    InputManager::Button playButton, pauseButton, rewindButton, fastForwardButton, increaseVolumeButton, decreaseVolumeButton,
        exitButton, switchToNextButton, switchToPreviousButton;

    if (Config::getInstance().inputMode == "debug")
    {
        playButton = InputManager::Button::BUTTON_S;
        pauseButton = InputManager::Button::BUTTON_S;
        rewindButton = InputManager::Button::BUTTON_A;
        fastForwardButton = InputManager::Button::BUTTON_D;
        increaseVolumeButton = InputManager::Button::BUTTON_UP;
        decreaseVolumeButton = InputManager::Button::BUTTON_DOWN;
        exitButton = InputManager::Button::BUTTON_F;
        switchToNextButton = InputManager::Button::BUTTON_D;
        switchToPreviousButton = InputManager::Button::BUTTON_A;
    }
    else if (Config::getInstance().inputMode == "prod")
    {
        playButton = InputManager::Button::KeyKpBegin;
        pauseButton = InputManager::Button::KeyKpBegin;
        rewindButton = InputManager::Button::KeyKpLeft;
        fastForwardButton = InputManager::Button::KeyKpRight;
        increaseVolumeButton = InputManager::Button::KeyKpAdd;
        decreaseVolumeButton = InputManager::Button::KeyKpSubtract;
        exitButton = InputManager::Button::KeyKpInsert;
        switchToNextButton = InputManager::Button::KeyKpRight;
        switchToPreviousButton = InputManager::Button::KeyKpLeft;
    }

    // CHOOSING STATE
    std::vector< std::pair<InputManager::Button, std::function<void()> > > choosingStateActions;
    choosingStateActions.push_back(std::make_pair(switchToPreviousButton, std::bind(&AudiobookPlayer::switchToPreviousAudiobook, this)));
    choosingStateActions.push_back(std::make_pair(playButton, std::bind(&AudiobookPlayer::playChosenAudiobook, this)));
    choosingStateActions.push_back(std::make_pair(switchToNextButton, std::bind(&AudiobookPlayer::switchToNextAudiobook, this)));
    choosingStateActions.push_back(std::make_pair(increaseVolumeButton, std::bind(&AudioManager::increaseMasterVolume, &audioManager_)));
    choosingStateActions.push_back(std::make_pair(decreaseVolumeButton, std::bind(&AudioManager::decreaseMasterVolume, &audioManager_)));
    availableActions_.insert(std::make_pair(State::CHOOSING, std::move(choosingStateActions)));

    // PLAYING STATE
    std::vector< std::pair<InputManager::Button, std::function<void()> > > playingStateActions;
    playingStateActions.push_back(std::make_pair(rewindButton, std::bind(&AudiobookPlayer::rewind, this)));
    playingStateActions.push_back(std::make_pair(fastForwardButton, std::bind(&AudiobookPlayer::fastForward, this)));
    playingStateActions.push_back(std::make_pair(pauseButton, std::bind(&AudiobookPlayer::pauseToggle, this)));
    playingStateActions.push_back(std::make_pair(increaseVolumeButton, std::bind(&AudioManager::increaseMasterVolume, &audioManager_)));
    playingStateActions.push_back(std::make_pair(decreaseVolumeButton, std::bind(&AudioManager::decreaseMasterVolume, &audioManager_)));
    playingStateActions.push_back(std::make_pair(exitButton, std::bind(&AudiobookPlayer::stopAudiobook, this)));
    availableActions_.insert(std::make_pair(State::PLAYING, std::move(playingStateActions)));

    // REWINDING STATE
    std::vector< std::pair<InputManager::Button, std::function<void()> > > rewindingStateActions;
    rewindingStateActions.push_back(std::make_pair(rewindButton, std::bind(&AudiobookPlayer::rewind, this)));
    rewindingStateActions.push_back(std::make_pair(fastForwardButton, std::bind(&AudiobookPlayer::fastForward, this)));
    rewindingStateActions.push_back(std::make_pair(pauseButton, std::bind(&AudiobookPlayer::pauseToggle, this)));
    rewindingStateActions.push_back(std::make_pair(increaseVolumeButton, std::bind(&AudioManager::increaseMasterVolume, &audioManager_)));
    rewindingStateActions.push_back(std::make_pair(decreaseVolumeButton, std::bind(&AudioManager::decreaseMasterVolume, &audioManager_)));
    availableActions_.insert(std::make_pair(State::REWINDING, std::move(rewindingStateActions)));

    // FAST_FORWARDING STATE
    std::vector< std::pair<InputManager::Button, std::function<void()> > > fastForwardingStateActions;
    fastForwardingStateActions.push_back(std::make_pair(rewindButton, std::bind(&AudiobookPlayer::rewind, this)));
    fastForwardingStateActions.push_back(std::make_pair(fastForwardButton, std::bind(&AudiobookPlayer::fastForward, this)));
    fastForwardingStateActions.push_back(std::make_pair(pauseButton, std::bind(&AudiobookPlayer::pauseToggle, this)));
    fastForwardingStateActions.push_back(std::make_pair(increaseVolumeButton, std::bind(&AudioManager::increaseMasterVolume, &audioManager_)));
    fastForwardingStateActions.push_back(std::make_pair(decreaseVolumeButton, std::bind(&AudioManager::decreaseMasterVolume, &audioManager_)));
    availableActions_.insert(std::make_pair(State::FAST_FORWARDING, std::move(fastForwardingStateActions)));

    // PAUSED STATE
    std::vector< std::pair<InputManager::Button, std::function<void()> > > pausedStateActions;
    pausedStateActions.push_back(std::make_pair(rewindButton, std::bind(&AudiobookPlayer::rewind, this)));
    pausedStateActions.push_back(std::make_pair(fastForwardButton, std::bind(&AudiobookPlayer::fastForward, this)));
    pausedStateActions.push_back(std::make_pair(playButton, std::bind(&AudiobookPlayer::pauseToggle, this)));
    pausedStateActions.push_back(std::make_pair(increaseVolumeButton, std::bind(&AudioManager::increaseMasterVolume, &audioManager_)));
    pausedStateActions.push_back(std::make_pair(decreaseVolumeButton, std::bind(&AudioManager::decreaseMasterVolume, &audioManager_)));
    pausedStateActions.push_back(std::make_pair(exitButton, std::bind(&AudiobookPlayer::stopAudiobook, this)));
    availableActions_.insert(std::make_pair(State::PAUSED, std::move(pausedStateActions)));
}

void AudiobookPlayer::play(std::list<AudioTask::Element> audioTaskElements, std::function<void()> callbackFunction)
{
    currentAudioTask_ = audioManager_.play(audioTaskElements, callbackFunction);
}

void AudiobookPlayer::playChosenAudiobook()
{
    std::lock_guard<std::mutex> lock(mutex_);
    changeStateTo(State::PLAYING);
    AudioTrack& currentAudioTrack = audioTracks_[currentTrackIndex_];

    BOOST_LOG_TRIVIAL(info) << "Playing audiotrack: " << currentAudioTrack.getTrackName() << " (" << currentAudioTrack.getFilePath() << ")";

    if (currentAudioTask_) currentAudioTask_->stop();
    
    auto audiobookFinishCallback = [this]()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (currentState_ == State::PLAYING)
        {
            audioTracks_[currentTrackIndex_].setLastPlayedMillisecond(0);
            saveTracksInfo();
            changeStateTo(State::CHOOSING);
            BOOST_LOG_TRIVIAL(info) << "Finished playing audiotrack.";
            play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("stopping_audiobook") });
        }
    };
    play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("playing_audiobook"), currentAudioTrack }, audiobookFinishCallback);
}

void AudiobookPlayer::fastForwardingTimerFunction()
{
    fastForwardedSeconds_ = 0;
    //TODO: TO WCZYTUJE INFO z "REWINDING" a nie z AUDIO TRACK, wiec ZMIEN TOOOO
    //JAK JEST REWINDING I INNE GOWNA TO MUSI SIE NADPISYWAC AUDIO_TRACK INFO KONIECZNIE!
    // Z TEGO TEZ BIERZEMY TO INFO I DAJEMY TU NA DOLE (lastPlayedSecond)
    int lastPlayedSecond = audioTracks_[currentTrackIndex_].getLastPlayedMillisecond() / 1000;
    // TO BYLO WCZESNIEJ: int lastPlayedSecond = currentAudioTask_->getCurrentTaskElementMilliseconds() / 1000;


    while (fastForwardingSpeed_ != 0)
    {
        //TODO: Czy tu moze byc problem? zobacz funkcje np. rewind
        auto sleepToTime = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        fastForwardedSeconds_ += fastForwardingSpeed_;
        std::this_thread::sleep_until(sleepToTime);
                
        std::unique_lock<std::mutex> lock(mutex_);
        BOOST_LOG_TRIVIAL(info) << "Fast-forwarded seconds: " << fastForwardedSeconds_ 
            << ", last played second: " << lastPlayedSecond << ", difference " << (lastPlayedSecond + fastForwardedSeconds_);
        //TODO: Skrajny przypadek gdy przewiniemy z 2xd o 0x ale tez bedzie koniec -> wtedy 2x razy sie wykona np. pauseToggle()
        // if ((lastPlayedSecond + fastForwardedSeconds_) < 0)
        // {
        //     fastForwardingSpeed_ = 0;
        //     changeStateTo(State::PLAYING);
        //     currentAudioTask_->pauseToggle();
        //     currentAudioTask_->seek(fastForwardedSeconds_ * 1000);
        //     break;
        // }
    }
}

void AudiobookPlayer::pauseToggle()
{
    if (currentState_ == State::PAUSED || currentState_ == State::FAST_FORWARDING || currentState_ == State::REWINDING)
    {
        if (!pausedAudioTask_)
        {
            BOOST_LOG_TRIVIAL(info) << "No paused audio task!";
            return;
        }
        if (!pausedAudioTask_->isPausable())
        {
            BOOST_LOG_TRIVIAL(info) << "Audio task not pausable! ";
            return;
        }
        BOOST_LOG_TRIVIAL(info) << "Toggling audiotrack pause: " << audioTracks_[currentTrackIndex_].getTrackName();
        fastForwardingSpeed_ = 0;
        if (fastForwardingTimerThread_.joinable()) fastForwardingTimerThread_.join();
        changeStateTo(State::PLAYING);
        pausedAudioTask_->seek(fastForwardedSeconds_ * 1000);
        updateCurrentTrackInfo(pausedAudioTask_);
        fastForwardedSeconds_ = 0;
        play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("unpausing_audiobook") });
        currentAudioTask_->waitForEnd();
        if (pausedAudioTask_)
        {
            pausedAudioTask_->pauseToggle();
            currentAudioTask_ = pausedAudioTask_;
        }
        pausedAudioTask_ = nullptr;
    }
    else
    {
        if (!currentAudioTask_) return;
        if (!currentAudioTask_->isPausable()) return;
        BOOST_LOG_TRIVIAL(info) << "Toggling audiotrack pause: " << audioTracks_[currentTrackIndex_].getTrackName();
        audioTracks_[currentTrackIndex_].setLastPlayedMillisecond(currentAudioTask_->getCurrentTaskElementMilliseconds());
        saveTracksInfo();
        fastForwardingSpeed_ = 0;
        if (fastForwardingTimerThread_.joinable()) fastForwardingTimerThread_.join();
        changeStateTo(State::PAUSED);
        currentAudioTask_->seek(fastForwardedSeconds_ * 1000);
        updateCurrentTrackInfo(currentAudioTask_);
        fastForwardedSeconds_ = 0;
        if (currentAudioTask_) currentAudioTask_->pauseToggle();
        pausedAudioTask_ = currentAudioTask_;
        play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("pausing_audiobook") });
    }
}

void AudiobookPlayer::loadTracks()
{
    filesystem::path path(std::string("../data/audiobooks/"));
    std::string fileExtension;

    if (!filesystem::exists(path))
    {
        BOOST_LOG_TRIVIAL(error) << "Directory with audiobooks not found.";
        return;
    }

    if (filesystem::is_directory(path))
    {
        filesystem::directory_iterator endIter;
        for (filesystem::directory_iterator dirItr(path); dirItr != endIter; ++dirItr)
        {
            if (filesystem::is_regular_file(dirItr->status()))
            {
                std::string trackName(dirItr->path().filename().c_str());
                fileExtension = (trackName.length() > 4) ? trackName.substr(trackName.length() - 3, 3) : "";

                if (fileExtension == "mp3" || fileExtension == "wav")
                {
                    AudioTrack audioTrack("../data/audiobooks/" + trackName, Config::getInstance().volumeForAudiobooks, AudioTrack::Type::STANDARD);
                    audioTracks_.push_back(audioTrack);
                    BOOST_LOG_TRIVIAL(info) << trackName << " loaded.";
                }
            }
        }
    }
    BOOST_LOG_TRIVIAL(info) << audioTracks_.size() << " audio tracks successfully loaded.";
}

void AudiobookPlayer::loadTracksInfo()
{
    std::ifstream inputFile("../data/audiobook_data.txt");
    if (!inputFile.is_open())
    {
        BOOST_LOG_TRIVIAL(info) << "File audiobook_data.txt could not be opened.";
        exit(0);
    }
    else
    {
        for (std::string line; std::getline(inputFile, line);)
        {
            BOOST_LOG_TRIVIAL(info) << line;
            if (std::regex_match(line, trackInfoPattern_))
            {
                std::istringstream iss(line);
                std::vector<std::string> tokens {std::istream_iterator<std::string> {iss}, std::istream_iterator<std::string> {}};
                audioTracksInfo_.push_back(AudioTrack(tokens[0], std::stoi(tokens[1])));
                BOOST_LOG_TRIVIAL(info) << "Added audio track info: " << tokens[0] << ", " << tokens[1];
            }
        }

        inputFile.close();
    }
}

void AudiobookPlayer::saveTracksInfo()
{
    std::ofstream outputFile("../data/audiobook_data.txt", std::ofstream::out | std::ofstream::trunc);
    if (!outputFile.is_open())
    {
        BOOST_LOG_TRIVIAL(info) << "File audiobook_data.txt could not be opened.";
    }
    else
    {
        for (AudioTrack audioTrack : audioTracks_)
        {
            outputFile << audioTrack.getTrackName() + " " + std::to_string(audioTrack.getLastPlayedMillisecond()) + "\n";
        }

        outputFile.close();
    }
}

void AudiobookPlayer::synchronizeTracksInfo()
{
    for (AudioTrack & audioTrack : audioTracks_)
    {
        for (AudioTrack audioTrackInfo : audioTracksInfo_)
        {
            if (audioTrack.getTrackName() == audioTrackInfo.getTrackName())
            {
                audioTrack.setLastPlayedMillisecond(audioTrackInfo.getLastPlayedMillisecond());
            }
        }
    }

    saveTracksInfo();
}

void AudiobookPlayer::updateCurrentTrackInfo(AudioTask* audioTask)
{
    audioTracks_[currentTrackIndex_].setLastPlayedMillisecond(audioTask->getCurrentTaskElementMilliseconds());
    saveTracksInfo();
}

void AudiobookPlayer::switchToNextAudiobook()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTrackIndex_ == audioTracks_.size() - 1)
        currentTrackIndex_ = 0;
    else
        ++currentTrackIndex_;

    if (currentAudioTask_) currentAudioTask_->stop();
    play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("chosen_next"), 
        voiceManager_.getSynthesizedVoiceAudioTracks().at(getCurrentTrack().getTrackName()) 
        });
}

void AudiobookPlayer::switchToPreviousAudiobook()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentTrackIndex_ == 0)
        currentTrackIndex_ = audioTracks_.size() - 1;  
    else
        --currentTrackIndex_;

    if (currentAudioTask_) currentAudioTask_->stop();
    play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("chosen_previous"), 
        voiceManager_.getSynthesizedVoiceAudioTracks().at(getCurrentTrack().getTrackName()) 
        });
}

std::vector< std::pair<InputManager::Button, std::function<void()> > > & AudiobookPlayer::getAvailableActions()
{
    return availableActions_.at(currentState_);
}

void AudiobookPlayer::rewind()
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (fastForwardingSpeed_ == 0)
    {
        AudioTask* checkedAudioTask = nullptr;
        if (currentState_ == State::PLAYING) checkedAudioTask = currentAudioTask_;
        else if (currentState_ == State::PAUSED) checkedAudioTask = pausedAudioTask_;

        if (!checkedAudioTask) return;
        if (!checkedAudioTask->isPausable()) return;
        if (!checkedAudioTask->isPaused())
        {
            pausedAudioTask_ = currentAudioTask_;
            checkedAudioTask->pauseToggle();
            updateCurrentTrackInfo(checkedAudioTask);
        }
        fastForwardingSpeed_ = -2;
        changeStateTo(State::REWINDING);
        lock.unlock();
        if (fastForwardingTimerThread_.joinable())
            fastForwardingTimerThread_.join();
        lock.lock();
        fastForwardingTimerThread_ = std::thread(&AudiobookPlayer::fastForwardingTimerFunction, this);
    }
    else if (fastForwardingSpeed_ == 2)
    {
        fastForwardingSpeed_ = 0;
        lock.unlock();
        if (fastForwardingTimerThread_.joinable())
            fastForwardingTimerThread_.join();
        lock.lock();
        currentAudioTask_ = pausedAudioTask_;
        currentAudioTask_->seek(fastForwardedSeconds_ * 1000);
        currentAudioTask_->pauseToggle();
        updateCurrentTrackInfo(currentAudioTask_);
        changeStateTo(State::PLAYING);
    }
    else if (fastForwardingSpeed_ < 0) fastForwardingSpeed_ *= 2;
    else if (fastForwardingSpeed_ > 0) fastForwardingSpeed_ *= 0.5f;

    /* lower value */
    if (fastForwardingSpeed_ < -128) fastForwardingSpeed_ = -128;

    /* Playing appropriate voice messages */
    if (fastForwardingSpeed_ == -2)
    {
        play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("rewinding"),
            voiceManager_.getSynthesizedVoiceAudioTracks().at("2x") });
    }
    else if (fastForwardingSpeed_ != 0)
    {
        if (currentAudioTask_) currentAudioTask_->stop();
        play({ voiceManager_.getSynthesizedVoiceAudioTracks().at(std::to_string(std::abs(fastForwardingSpeed_)) + "x") });
    }

    BOOST_LOG_TRIVIAL(info) << "Set fast-forwarding speed to " << fastForwardingSpeed_;
}

void AudiobookPlayer::fastForward()
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (fastForwardingSpeed_ == 0)
    {
        AudioTask* checkedAudioTask = nullptr;
        if (currentState_ == State::PLAYING) checkedAudioTask = currentAudioTask_;
        else if (currentState_ == State::PAUSED) checkedAudioTask = pausedAudioTask_;

        if (!checkedAudioTask) return;
        if (!checkedAudioTask->isPausable()) return;
        if (!checkedAudioTask->isPaused())
        {
            pausedAudioTask_ = currentAudioTask_;
            checkedAudioTask->pauseToggle();
            updateCurrentTrackInfo(checkedAudioTask);
        }
        fastForwardingSpeed_ = 2;
        changeStateTo(State::FAST_FORWARDING);
        lock.unlock();
        if (fastForwardingTimerThread_.joinable())
            fastForwardingTimerThread_.join();
        lock.lock();
        fastForwardingTimerThread_ = std::thread(&AudiobookPlayer::fastForwardingTimerFunction, this);
    }
    else if (fastForwardingSpeed_ == -2)
    {
        fastForwardingSpeed_ = 0;
        lock.unlock();
        if (fastForwardingTimerThread_.joinable())
            fastForwardingTimerThread_.join();
        lock.lock();
        currentAudioTask_ = pausedAudioTask_;
        pausedAudioTask_->seek(fastForwardedSeconds_ * 1000);
        currentAudioTask_->pauseToggle();
        updateCurrentTrackInfo(currentAudioTask_);
        changeStateTo(State::PLAYING);
    }
    else if (fastForwardingSpeed_ < 0) fastForwardingSpeed_ *= 0.5f;
    else if (fastForwardingSpeed_ > 0) fastForwardingSpeed_ *= 2;

    /* Upper value */
    if (fastForwardingSpeed_ > 128) fastForwardingSpeed_ = 128;

    /* Playing appropriate voice messages */
    if (fastForwardingSpeed_ == 2)
    {
        play({ voiceManager_.getSynthesizedVoiceAudioTracks().at("fast_forwarding"),
            voiceManager_.getSynthesizedVoiceAudioTracks().at("2x") });
    }
    else if (fastForwardingSpeed_ != 0)
    {
        if (currentAudioTask_) currentAudioTask_->stop();
        play({ voiceManager_.getSynthesizedVoiceAudioTracks().at(std::to_string(std::abs(fastForwardingSpeed_)) + "x") });
    }

    BOOST_LOG_TRIVIAL(info) << "Set fast-forwarding speed to " << fastForwardingSpeed_;
}

void AudiobookPlayer::stopAudiobook()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (currentAudioTask_)
    {
        updateCurrentTrackInfo(currentAudioTask_);
        currentAudioTask_->stop();
    }
    play( {voiceManager_.getSynthesizedVoiceAudioTracks().at("stopping_audiobook")} );
    BOOST_LOG_TRIVIAL(info) << "Audiobook stopped.";
    currentState_ = State::CHOOSING;
}

void AudiobookPlayer::printState()
{
    BOOST_LOG_TRIVIAL(info) << "State: " << stateNames_[static_cast<std::underlying_type<State>::type>(currentState_)];
}

}