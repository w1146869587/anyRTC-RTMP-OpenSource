/*
 *  Copyright (c) 2012 The WebRTC@AnyRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/base/logging.h"
#include "webrtc/base/refcount.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/common_audio/signal_processing/include/signal_processing_library.h"
#include "webrtc/modules/audio_device/audio_device_config.h"
#include "webrtc/modules/audio_device/audio_device_impl.h"

#include <assert.h>
#include <string.h>

#if defined(_WIN32)
#include "audio_device_wave_win.h"
#if defined(WEBRTC_WINDOWS_CORE_AUDIO_BUILD)
#include "audio_device_core_win.h"
#endif
#elif defined(WEBRTC_ANDROID)
#include <stdlib.h>
#include "webrtc/modules/audio_device/android/audio_device_template.h"
#include "webrtc/modules/audio_device/android/audio_manager.h"
#include "webrtc/modules/audio_device/android/audio_record_jni.h"
#include "webrtc/modules/audio_device/android/audio_track_jni.h"
#include "webrtc/modules/audio_device/android/opensles_player.h"
#elif defined(WEBRTC_LINUX)
#if defined(LINUX_ALSA)
#include "audio_device_alsa_linux.h"
#endif
#if defined(LINUX_PULSE)
#include "audio_device_pulse_linux.h"
#endif
#elif defined(WEBRTC_IOS)
#include "audio_device_ios.h"
#elif defined(WEBRTC_MAC)
#include "audio_device_mac.h"
#endif

#if defined(WEBRTC_DUMMY_FILE_DEVICES)
#include "webrtc/modules/audio_device/dummy/file_audio_device_factory.h"
#endif

#include "webrtc/modules/audio_device/dummy/audio_device_dummy.h"
#include "webrtc/modules/audio_device/dummy/file_audio_device.h"
#include "webrtc/system_wrappers/include/critical_section_wrapper.h"

#define CHECK_INITIALIZED() \
  {                         \
    if (!_initialized) {    \
      return -1;            \
    };                      \
  }

#define CHECK_INITIALIZED_BOOL() \
  {                              \
    if (!_initialized) {         \
      return false;              \
    };                           \
  }

namespace webrtc {

// ============================================================================
//                                   Static methods
// ============================================================================

// ----------------------------------------------------------------------------
//  AudioDeviceModule::Create()
// ----------------------------------------------------------------------------

rtc::scoped_refptr<AudioDeviceModule> AudioDeviceModule::Create(
    const int32_t id,
    const AudioLayer audio_layer) {
  WCLOG(INFO) << __FUNCTION__;
  // Create the generic ref counted (platform independent) implementation.
  rtc::scoped_refptr<AudioDeviceModuleImpl> audioDevice(
      new rtc::RefCountedObject<AudioDeviceModuleImpl>(id, audio_layer));

  // Ensure that the current platform is supported.
  if (audioDevice->CheckPlatform() == -1) {
    return nullptr;
  }

  // Create the platform-dependent implementation.
  if (audioDevice->CreatePlatformSpecificObjects() == -1) {
    return nullptr;
  }

  // Ensure that the generic audio buffer can communicate with the
  // platform-specific parts.
  if (audioDevice->AttachAudioBuffer() == -1) {
    return nullptr;
  }

  WebRtcSpl_Init();

  return audioDevice;
}

// ============================================================================
//                            Construction & Destruction
// ============================================================================

// ----------------------------------------------------------------------------
//  AudioDeviceModuleImpl - ctor
// ----------------------------------------------------------------------------

AudioDeviceModuleImpl::AudioDeviceModuleImpl(const int32_t id,
                                             const AudioLayer audioLayer)
    : _critSect(*CriticalSectionWrapper::CreateCriticalSection()),
      _critSectEventCb(*CriticalSectionWrapper::CreateCriticalSection()),
      _critSectAudioCb(*CriticalSectionWrapper::CreateCriticalSection()),
      _ptrCbAudioDeviceObserver(NULL),
      _ptrAudioDevice(NULL),
      _id(id),
      _platformAudioLayer(audioLayer),
      _lastProcessTime(rtc::TimeMillis()),
      _platformType(kPlatformNotSupported),
      _initialized(false),
      _lastError(kAdmErrNone) {
  WCLOG(INFO) << __FUNCTION__;
}

// ----------------------------------------------------------------------------
//  CheckPlatform
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::CheckPlatform() {
  WCLOG(INFO) << __FUNCTION__;

  // Ensure that the current platform is supported
  //
  PlatformType platform(kPlatformNotSupported);

#if defined(_WIN32)
  platform = kPlatformWin32;
  WCLOG(INFO) << "current platform is Win32";
#elif defined(WEBRTC_ANDROID)
  platform = kPlatformAndroid;
  WCLOG(INFO) << "current platform is Android";
#elif defined(WEBRTC_LINUX)
  platform = kPlatformLinux;
  WCLOG(INFO) << "current platform is Linux";
#elif defined(WEBRTC_IOS)
  platform = kPlatformIOS;
  WCLOG(INFO) << "current platform is IOS";
#elif defined(WEBRTC_MAC)
  platform = kPlatformMac;
  WCLOG(INFO) << "current platform is Mac";
#endif

  if (platform == kPlatformNotSupported) {
    WCLOG(LERROR) << "current platform is not supported => this module will self "
                   "destruct!";
    return -1;
  }

  // Store valid output results
  //
  _platformType = platform;

  return 0;
}

// ----------------------------------------------------------------------------
//  CreatePlatformSpecificObjects
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::CreatePlatformSpecificObjects() {
  WCLOG(INFO) << __FUNCTION__;

  AudioDeviceGeneric* ptrAudioDevice(NULL);

#if defined(WEBRTC_DUMMY_AUDIO_BUILD)
  ptrAudioDevice = new AudioDeviceDummy(Id());
  WCLOG(INFO) << "Dummy Audio APIs will be utilized";
#elif defined(WEBRTC_DUMMY_FILE_DEVICES)
  ptrAudioDevice = FileAudioDeviceFactory::CreateFileAudioDevice(Id());
  WCLOG(INFO) << "Will use file-playing dummy device.";
#else
  AudioLayer audioLayer(PlatformAudioLayer());

// Create the *Windows* implementation of the Audio Device
//
#if defined(_WIN32)
  if ((audioLayer == kWindowsWaveAudio)
#if !defined(WEBRTC_WINDOWS_CORE_AUDIO_BUILD)
      // Wave audio is default if Core audio is not supported in this build
      || (audioLayer == kPlatformDefaultAudio)
#endif
          ) {
    // create *Windows Wave Audio* implementation
    ptrAudioDevice = new AudioDeviceWindowsWave(Id());
    WCLOG(INFO) << "Windows Wave APIs will be utilized";
  }
#if defined(WEBRTC_WINDOWS_CORE_AUDIO_BUILD)
  if ((audioLayer == kWindowsCoreAudio) ||
      (audioLayer == kPlatformDefaultAudio)) {
    WCLOG(INFO) << "attempting to use the Windows Core Audio APIs...";

    if (AudioDeviceWindowsCore::CoreAudioIsSupported()) {
      // create *Windows Core Audio* implementation
      ptrAudioDevice = new AudioDeviceWindowsCore(Id());
      WCLOG(INFO) << "Windows Core Audio APIs will be utilized";
    } else {
      // create *Windows Wave Audio* implementation
      ptrAudioDevice = new AudioDeviceWindowsWave(Id());
      if (ptrAudioDevice != NULL) {
        // Core Audio was not supported => revert to Windows Wave instead
        _platformAudioLayer =
            kWindowsWaveAudio;  // modify the state set at construction
        WCLOG(WARNING) << "Windows Core Audio is *not* supported => Wave APIs "
                        "will be utilized instead";
      }
    }
  }
#endif  // defined(WEBRTC_WINDOWS_CORE_AUDIO_BUILD)
#endif  // #if defined(_WIN32)

#if defined(WEBRTC_ANDROID)
  // Create an Android audio manager.
  _audioManagerAndroid.reset(new AudioManager());
  // Select best possible combination of audio layers.
  if (audioLayer == kPlatformDefaultAudio) {
    if (_audioManagerAndroid->IsLowLatencyPlayoutSupported()) {
      // Always use OpenSL ES for output on devices that supports the
      // low-latency output audio path.
      audioLayer = kAndroidJavaInputAndOpenSLESOutputAudio;
    } else {
      // Use Java-based audio in both directions when low-latency output
      // is not supported.
      audioLayer = kAndroidJavaAudio;
    }
  }
  AudioManager* audio_manager = _audioManagerAndroid.get();
  if (audioLayer == kAndroidJavaAudio) {
    // Java audio for both input and output audio.
    ptrAudioDevice = new AudioDeviceTemplate<AudioRecordJni, AudioTrackJni>(
        audioLayer, audio_manager);
  } else if (audioLayer == kAndroidJavaInputAndOpenSLESOutputAudio) {
    // Java audio for input and OpenSL ES for output audio (i.e. mixed APIs).
    // This combination provides low-latency output audio and at the same
    // time support for HW AEC using the AudioRecord Java API.
    ptrAudioDevice = new AudioDeviceTemplate<AudioRecordJni, OpenSLESPlayer>(
        audioLayer, audio_manager);
  } else {
    // Invalid audio layer.
    ptrAudioDevice = NULL;
  }
// END #if defined(WEBRTC_ANDROID)

// Create the *Linux* implementation of the Audio Device
//
#elif defined(WEBRTC_LINUX)
  if ((audioLayer == kLinuxPulseAudio) ||
      (audioLayer == kPlatformDefaultAudio)) {
#if defined(LINUX_PULSE)
    WCLOG(INFO) << "attempting to use the Linux PulseAudio APIs...";

    // create *Linux PulseAudio* implementation
    AudioDeviceLinuxPulse* pulseDevice = new AudioDeviceLinuxPulse(Id());
    if (pulseDevice->Init() != -1) {
      ptrAudioDevice = pulseDevice;
      WCLOG(INFO) << "Linux PulseAudio APIs will be utilized";
    } else {
      delete pulseDevice;
#endif
#if defined(LINUX_ALSA)
      // create *Linux ALSA Audio* implementation
      ptrAudioDevice = new AudioDeviceLinuxALSA(Id());
      if (ptrAudioDevice != NULL) {
        // Pulse Audio was not supported => revert to ALSA instead
        _platformAudioLayer =
            kLinuxAlsaAudio;  // modify the state set at construction
        WCLOG(WARNING) << "Linux PulseAudio is *not* supported => ALSA APIs will "
                        "be utilized instead";
      }
#endif
#if defined(LINUX_PULSE)
    }
#endif
  } else if (audioLayer == kLinuxAlsaAudio) {
#if defined(LINUX_ALSA)
    // create *Linux ALSA Audio* implementation
    ptrAudioDevice = new AudioDeviceLinuxALSA(Id());
    WCLOG(INFO) << "Linux ALSA APIs will be utilized";
#endif
  }
#endif  // #if defined(WEBRTC_LINUX)

// Create the *iPhone* implementation of the Audio Device
//
#if defined(WEBRTC_IOS)
  if (audioLayer == kPlatformDefaultAudio) {
    // Create iOS Audio Device implementation.
    ptrAudioDevice = new AudioDeviceIOS();
    WCLOG(INFO) << "iPhone Audio APIs will be utilized";
  }
// END #if defined(WEBRTC_IOS)

// Create the *Mac* implementation of the Audio Device
//
#elif defined(WEBRTC_MAC)
  if (audioLayer == kPlatformDefaultAudio) {
    // Create *Mac Audio* implementation
    ptrAudioDevice = new AudioDeviceMac(Id());
    WCLOG(INFO) << "Mac OS X Audio APIs will be utilized";
  }
#endif  // WEBRTC_MAC

  // Create the *Dummy* implementation of the Audio Device
  // Available for all platforms
  //
  if (audioLayer == kDummyAudio) {
    // Create *Dummy Audio* implementation
    assert(!ptrAudioDevice);
    ptrAudioDevice = new AudioDeviceDummy(Id());
    WCLOG(INFO) << "Dummy Audio APIs will be utilized";
  }
#endif  // if defined(WEBRTC_DUMMY_AUDIO_BUILD)

  if (ptrAudioDevice == NULL) {
    WCLOG(LERROR)
        << "unable to create the platform specific audio device implementation";
    return -1;
  }

  // Store valid output pointers
  //
  _ptrAudioDevice = ptrAudioDevice;

  return 0;
}

// ----------------------------------------------------------------------------
//  AttachAudioBuffer
//
//  Install "bridge" between the platform implemetation and the generic
//  implementation. The "child" shall set the native sampling rate and the
//  number of channels in this function call.
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::AttachAudioBuffer() {
  WCLOG(INFO) << __FUNCTION__;

  _audioDeviceBuffer.SetId(_id);
  _ptrAudioDevice->AttachAudioBuffer(&_audioDeviceBuffer);
  return 0;
}

// ----------------------------------------------------------------------------
//  ~AudioDeviceModuleImpl - dtor
// ----------------------------------------------------------------------------

AudioDeviceModuleImpl::~AudioDeviceModuleImpl() {
  WCLOG(INFO) << __FUNCTION__;

  if (_ptrAudioDevice) {
    delete _ptrAudioDevice;
    _ptrAudioDevice = NULL;
  }

  delete &_critSect;
  delete &_critSectEventCb;
  delete &_critSectAudioCb;
}

// ============================================================================
//                                  Module
// ============================================================================

// ----------------------------------------------------------------------------
//  Module::TimeUntilNextProcess
//
//  Returns the number of milliseconds until the module want a worker thread
//  to call Process().
// ----------------------------------------------------------------------------

int64_t AudioDeviceModuleImpl::TimeUntilNextProcess() {
  WCLOG(LS_VERBOSE) << __FUNCTION__;
  int64_t now = rtc::TimeMillis();
  int64_t deltaProcess = kAdmMaxIdleTimeProcess - (now - _lastProcessTime);
  return deltaProcess;
}

// ----------------------------------------------------------------------------
//  Module::Process
//
//  Check for posted error and warning reports. Generate callbacks if
//  new reports exists.
// ----------------------------------------------------------------------------

void AudioDeviceModuleImpl::Process() {
  WCLOG(LS_VERBOSE) << __FUNCTION__;
  _lastProcessTime = rtc::TimeMillis();

  // kPlayoutWarning
  if (_ptrAudioDevice->PlayoutWarning()) {
    CriticalSectionScoped lock(&_critSectEventCb);
    if (_ptrCbAudioDeviceObserver) {
      WCLOG(WARNING) << "=> OnWarningIsReported(kPlayoutWarning)";
      _ptrCbAudioDeviceObserver->OnWarningIsReported(
          AudioDeviceObserver::kPlayoutWarning);
    }
    _ptrAudioDevice->ClearPlayoutWarning();
  }

  // kPlayoutError
  if (_ptrAudioDevice->PlayoutError()) {
    CriticalSectionScoped lock(&_critSectEventCb);
    if (_ptrCbAudioDeviceObserver) {
      WCLOG(LERROR) << "=> OnErrorIsReported(kPlayoutError)";
      _ptrCbAudioDeviceObserver->OnErrorIsReported(
          AudioDeviceObserver::kPlayoutError);
    }
    _ptrAudioDevice->ClearPlayoutError();
  }

  // kRecordingWarning
  if (_ptrAudioDevice->RecordingWarning()) {
    CriticalSectionScoped lock(&_critSectEventCb);
    if (_ptrCbAudioDeviceObserver) {
      WCLOG(WARNING) << "=> OnWarningIsReported(kRecordingWarning)";
      _ptrCbAudioDeviceObserver->OnWarningIsReported(
          AudioDeviceObserver::kRecordingWarning);
    }
    _ptrAudioDevice->ClearRecordingWarning();
  }

  // kRecordingError
  if (_ptrAudioDevice->RecordingError()) {
    CriticalSectionScoped lock(&_critSectEventCb);
    if (_ptrCbAudioDeviceObserver) {
      WCLOG(LERROR) << "=> OnErrorIsReported(kRecordingError)";
      _ptrCbAudioDeviceObserver->OnErrorIsReported(
          AudioDeviceObserver::kRecordingError);
    }
    _ptrAudioDevice->ClearRecordingError();
  }
}

// ============================================================================
//                                    Public API
// ============================================================================

// ----------------------------------------------------------------------------
//  ActiveAudioLayer
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::ActiveAudioLayer(AudioLayer* audioLayer) const {
  WCLOG(INFO) << __FUNCTION__;
  AudioLayer activeAudio;
  if (_ptrAudioDevice->ActiveAudioLayer(activeAudio) == -1) {
    return -1;
  }
  *audioLayer = activeAudio;
  return 0;
}

// ----------------------------------------------------------------------------
//  LastError
// ----------------------------------------------------------------------------

AudioDeviceModule::ErrorCode AudioDeviceModuleImpl::LastError() const {
  WCLOG(INFO) << __FUNCTION__;
  return _lastError;
}

// ----------------------------------------------------------------------------
//  Init
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::Init() {
  WCLOG(INFO) << __FUNCTION__;
  if (_initialized)
    return 0;

  if (!_ptrAudioDevice)
    return -1;

  if (_ptrAudioDevice->Init() == -1) {
    return -1;
  }

  _initialized = true;
  return 0;
}

// ----------------------------------------------------------------------------
//  Terminate
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::Terminate() {
  WCLOG(INFO) << __FUNCTION__;
  if (!_initialized)
    return 0;

  if (_ptrAudioDevice->Terminate() == -1) {
    return -1;
  }

  _initialized = false;
  return 0;
}

// ----------------------------------------------------------------------------
//  Initialized
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::Initialized() const {
  WCLOG(INFO) << __FUNCTION__ << ": " << _initialized;
  return (_initialized);
}

// ----------------------------------------------------------------------------
//  InitSpeaker
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::InitSpeaker() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->InitSpeaker());
}

// ----------------------------------------------------------------------------
//  InitMicrophone
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::InitMicrophone() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->InitMicrophone());
}

// ----------------------------------------------------------------------------
//  SpeakerVolumeIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SpeakerVolumeIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->SpeakerVolumeIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetSpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetSpeakerVolume(uint32_t volume) {
  WCLOG(INFO) << __FUNCTION__ << "(" << volume << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetSpeakerVolume(volume));
}

// ----------------------------------------------------------------------------
//  SpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SpeakerVolume(uint32_t* volume) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint32_t level(0);

  if (_ptrAudioDevice->SpeakerVolume(level) == -1) {
    return -1;
  }

  *volume = level;

  WCLOG(INFO) << __FUNCTION__ << " output: " << *volume;
  return (0);
}

// ----------------------------------------------------------------------------
//  SetWaveOutVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetWaveOutVolume(uint16_t volumeLeft,
                                                uint16_t volumeRight) {
  WCLOG(INFO) << __FUNCTION__ << "(" << volumeLeft << ", " << volumeRight << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetWaveOutVolume(volumeLeft, volumeRight));
}

// ----------------------------------------------------------------------------
//  WaveOutVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::WaveOutVolume(uint16_t* volumeLeft,
                                             uint16_t* volumeRight) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t volLeft(0);
  uint16_t volRight(0);

  if (_ptrAudioDevice->WaveOutVolume(volLeft, volRight) == -1) {
    return -1;
  }

  *volumeLeft = volLeft;
  *volumeRight = volRight;

  WCLOG(INFO) << __FUNCTION__ << " output: volumeLeft = " << *volumeLeft
            << ", volumeRight = " << *volumeRight;

  return (0);
}

// ----------------------------------------------------------------------------
//  SpeakerIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::SpeakerIsInitialized() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();

  bool isInitialized = _ptrAudioDevice->SpeakerIsInitialized();

  if (isInitialized) {
    WCLOG(INFO) << __FUNCTION__ << " output: initialized";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not initialized";
  }
  return (isInitialized);
}

// ----------------------------------------------------------------------------
//  MicrophoneIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::MicrophoneIsInitialized() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();

  bool isInitialized = _ptrAudioDevice->MicrophoneIsInitialized();

  if (isInitialized) {
    WCLOG(INFO) << __FUNCTION__ << " output: initialized";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not initialized";
  }
  return (isInitialized);
}

// ----------------------------------------------------------------------------
//  MaxSpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MaxSpeakerVolume(uint32_t* maxVolume) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint32_t maxVol(0);

  if (_ptrAudioDevice->MaxSpeakerVolume(maxVol) == -1) {
    return -1;
  }

  *maxVolume = maxVol;

  WCLOG(INFO) << __FUNCTION__ << " output: maxVolume = " << *maxVolume;
  return (0);
}

// ----------------------------------------------------------------------------
//  MinSpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MinSpeakerVolume(uint32_t* minVolume) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint32_t minVol(0);

  if (_ptrAudioDevice->MinSpeakerVolume(minVol) == -1) {
    return -1;
  }

  *minVolume = minVol;

  WCLOG(INFO) << __FUNCTION__ << " output: " << *minVolume;
  return (0);
}

// ----------------------------------------------------------------------------
//  SpeakerVolumeStepSize
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SpeakerVolumeStepSize(uint16_t* stepSize) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t delta(0);

  if (_ptrAudioDevice->SpeakerVolumeStepSize(delta) == -1) {
    WCLOG(LERROR) << "failed to retrieve the speaker-volume step size";
    return -1;
  }

  *stepSize = delta;

  WCLOG(INFO) << __FUNCTION__ << " output: " << *stepSize;
  return (0);
}

// ----------------------------------------------------------------------------
//  SpeakerMuteIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SpeakerMuteIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->SpeakerMuteIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetSpeakerMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetSpeakerMute(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetSpeakerMute(enable));
}

// ----------------------------------------------------------------------------
//  SpeakerMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SpeakerMute(bool* enabled) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool muted(false);

  if (_ptrAudioDevice->SpeakerMute(muted) == -1) {
    return -1;
  }

  *enabled = muted;

  if (muted) {
    WCLOG(INFO) << __FUNCTION__ << " output: muted";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not muted";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  MicrophoneMuteIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneMuteIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->MicrophoneMuteIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetMicrophoneMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetMicrophoneMute(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetMicrophoneMute(enable));
}

// ----------------------------------------------------------------------------
//  MicrophoneMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneMute(bool* enabled) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool muted(false);

  if (_ptrAudioDevice->MicrophoneMute(muted) == -1) {
    return -1;
  }

  *enabled = muted;

  if (muted) {
    WCLOG(INFO) << __FUNCTION__ << " output: muted";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not muted";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  MicrophoneBoostIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneBoostIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->MicrophoneBoostIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetMicrophoneBoost
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetMicrophoneBoost(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetMicrophoneBoost(enable));
}

// ----------------------------------------------------------------------------
//  MicrophoneBoost
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneBoost(bool* enabled) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool onOff(false);

  if (_ptrAudioDevice->MicrophoneBoost(onOff) == -1) {
    return -1;
  }

  *enabled = onOff;

  if (onOff) {
    WCLOG(INFO) << __FUNCTION__ << " output: enabled";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not enabled";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  MicrophoneVolumeIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneVolumeIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->MicrophoneVolumeIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetMicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetMicrophoneVolume(uint32_t volume) {
  WCLOG(INFO) << __FUNCTION__ << "(" << volume << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetMicrophoneVolume(volume));
}

// ----------------------------------------------------------------------------
//  MicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneVolume(uint32_t* volume) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint32_t level(0);

  if (_ptrAudioDevice->MicrophoneVolume(level) == -1) {
    return -1;
  }

  *volume = level;

  WCLOG(INFO) << __FUNCTION__ << " output: volume = " << *volume;
  return (0);
}

// ----------------------------------------------------------------------------
//  StereoRecordingIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StereoRecordingIsAvailable(
    bool* available) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->StereoRecordingIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetStereoRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetStereoRecording(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->RecordingIsInitialized()) {
    WCLOG(WARNING) << "recording in stereo is not supported";
    return -1;
  }

  if (_ptrAudioDevice->SetStereoRecording(enable) == -1) {
    if (enable) {
      WCLOG(WARNING) << "failed to enable stereo recording";
    } else {
      WCLOG(WARNING) << "failed to disable stereo recording";
    }
    return -1;
  }

  int8_t nChannels(1);
  if (enable) {
    nChannels = 2;
  }
  _audioDeviceBuffer.SetRecordingChannels(nChannels);

  return 0;
}

// ----------------------------------------------------------------------------
//  StereoRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StereoRecording(bool* enabled) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool stereo(false);

  if (_ptrAudioDevice->StereoRecording(stereo) == -1) {
    return -1;
  }

  *enabled = stereo;

  if (stereo) {
    WCLOG(INFO) << __FUNCTION__ << " output: enabled";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not enabled";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetRecordingChannel
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetRecordingChannel(const ChannelType channel) {
  if (channel == kChannelBoth) {
    WCLOG(INFO) << __FUNCTION__ << "(both)";
  } else if (channel == kChannelLeft) {
    WCLOG(INFO) << __FUNCTION__ << "(left)";
  } else {
    WCLOG(INFO) << __FUNCTION__ << "(right)";
  }
  CHECK_INITIALIZED();

  bool stereo(false);

  if (_ptrAudioDevice->StereoRecording(stereo) == -1) {
    WCLOG(WARNING) << "recording in stereo is not supported";
    return -1;
  }

  return (_audioDeviceBuffer.SetRecordingChannel(channel));
}

// ----------------------------------------------------------------------------
//  RecordingChannel
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RecordingChannel(ChannelType* channel) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  ChannelType chType;

  if (_audioDeviceBuffer.RecordingChannel(chType) == -1) {
    return -1;
  }

  *channel = chType;

  if (*channel == kChannelBoth) {
    WCLOG(INFO) << __FUNCTION__ << " output: both";
  } else if (*channel == kChannelLeft) {
    WCLOG(INFO) << __FUNCTION__ << " output: left";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: right";
  }

  return (0);
}

// ----------------------------------------------------------------------------
//  StereoPlayoutIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StereoPlayoutIsAvailable(bool* available) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->StereoPlayoutIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetStereoPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetStereoPlayout(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->PlayoutIsInitialized()) {
    WCLOG(LERROR)
        << "unable to set stereo mode while playing side is initialized";
    return -1;
  }

  if (_ptrAudioDevice->SetStereoPlayout(enable)) {
    WCLOG(WARNING) << "stereo playout is not supported";
    return -1;
  }

  int8_t nChannels(1);
  if (enable) {
    nChannels = 2;
  }
  _audioDeviceBuffer.SetPlayoutChannels(nChannels);

  return 0;
}

// ----------------------------------------------------------------------------
//  StereoPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StereoPlayout(bool* enabled) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool stereo(false);

  if (_ptrAudioDevice->StereoPlayout(stereo) == -1) {
    return -1;
  }

  *enabled = stereo;

  if (stereo) {
    WCLOG(INFO) << __FUNCTION__ << " output: enabled";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not enabled";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  SetAGC
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetAGC(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetAGC(enable));
}

// ----------------------------------------------------------------------------
//  AGC
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::AGC() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  return (_ptrAudioDevice->AGC());
}

// ----------------------------------------------------------------------------
//  PlayoutIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::PlayoutIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->PlayoutIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  RecordingIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RecordingIsAvailable(bool* available) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  bool isAvailable(0);

  if (_ptrAudioDevice->RecordingIsAvailable(isAvailable) == -1) {
    return -1;
  }

  *available = isAvailable;

  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return (0);
}

// ----------------------------------------------------------------------------
//  MaxMicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MaxMicrophoneVolume(uint32_t* maxVolume) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint32_t maxVol(0);

  if (_ptrAudioDevice->MaxMicrophoneVolume(maxVol) == -1) {
    return -1;
  }

  *maxVolume = maxVol;

  WCLOG(INFO) << __FUNCTION__ << " output: = " << *maxVolume;
  return (0);
}

// ----------------------------------------------------------------------------
//  MinMicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MinMicrophoneVolume(uint32_t* minVolume) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint32_t minVol(0);

  if (_ptrAudioDevice->MinMicrophoneVolume(minVol) == -1) {
    return -1;
  }

  *minVolume = minVol;

  WCLOG(INFO) << __FUNCTION__ << " output: = " << *minVolume;
  return (0);
}

// ----------------------------------------------------------------------------
//  MicrophoneVolumeStepSize
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::MicrophoneVolumeStepSize(
    uint16_t* stepSize) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t delta(0);

  if (_ptrAudioDevice->MicrophoneVolumeStepSize(delta) == -1) {
    return -1;
  }

  *stepSize = delta;

  WCLOG(INFO) << __FUNCTION__ << " output: " << *stepSize;
  return (0);
}

// ----------------------------------------------------------------------------
//  PlayoutDevices
// ----------------------------------------------------------------------------

int16_t AudioDeviceModuleImpl::PlayoutDevices() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t nPlayoutDevices = _ptrAudioDevice->PlayoutDevices();

  WCLOG(INFO) << __FUNCTION__ << " output: " << nPlayoutDevices;
  return ((int16_t)(nPlayoutDevices));
}

// ----------------------------------------------------------------------------
//  SetPlayoutDevice I (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetPlayoutDevice(uint16_t index) {
  WCLOG(INFO) << __FUNCTION__ << "(" << index << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetPlayoutDevice(index));
}

// ----------------------------------------------------------------------------
//  SetPlayoutDevice II (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetPlayoutDevice(WindowsDeviceType device) {
  if (device == kDefaultDevice) {
  } else {
  }
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  return (_ptrAudioDevice->SetPlayoutDevice(device));
}

// ----------------------------------------------------------------------------
//  PlayoutDeviceName
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::PlayoutDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]) {
  WCLOG(INFO) << __FUNCTION__ << "(" << index << ", ...)";
  CHECK_INITIALIZED();

  if (name == NULL) {
    _lastError = kAdmErrArgument;
    return -1;
  }

  if (_ptrAudioDevice->PlayoutDeviceName(index, name, guid) == -1) {
    return -1;
  }

  if (name != NULL) {
    WCLOG(INFO) << __FUNCTION__ << " output: name = " << name;
  }
  if (guid != NULL) {
    WCLOG(INFO) << __FUNCTION__ << " output: guid = " << guid;
  }

  return (0);
}

// ----------------------------------------------------------------------------
//  RecordingDeviceName
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RecordingDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]) {
  WCLOG(INFO) << __FUNCTION__ << "(" << index << ", ...)";
  CHECK_INITIALIZED();

  if (name == NULL) {
    _lastError = kAdmErrArgument;
    return -1;
  }

  if (_ptrAudioDevice->RecordingDeviceName(index, name, guid) == -1) {
    return -1;
  }

  if (name != NULL) {
    WCLOG(INFO) << __FUNCTION__ << " output: name = " << name;
  }
  if (guid != NULL) {
    WCLOG(INFO) << __FUNCTION__ << " output: guid = " << guid;
  }

  return (0);
}

// ----------------------------------------------------------------------------
//  RecordingDevices
// ----------------------------------------------------------------------------

int16_t AudioDeviceModuleImpl::RecordingDevices() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t nRecordingDevices = _ptrAudioDevice->RecordingDevices();

  WCLOG(INFO) << __FUNCTION__ << " output: " << nRecordingDevices;
  return ((int16_t)nRecordingDevices);
}

// ----------------------------------------------------------------------------
//  SetRecordingDevice I (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetRecordingDevice(uint16_t index) {
  WCLOG(INFO) << __FUNCTION__ << "(" << index << ")";
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->SetRecordingDevice(index));
}

// ----------------------------------------------------------------------------
//  SetRecordingDevice II (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetRecordingDevice(WindowsDeviceType device) {
  if (device == kDefaultDevice) {
  } else {
  }
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  return (_ptrAudioDevice->SetRecordingDevice(device));
}

// ----------------------------------------------------------------------------
//  InitPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::InitPlayout() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  _audioDeviceBuffer.InitPlayout();
  return (_ptrAudioDevice->InitPlayout());
}

// ----------------------------------------------------------------------------
//  InitRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::InitRecording() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  _audioDeviceBuffer.InitRecording();
  return (_ptrAudioDevice->InitRecording());
}

// ----------------------------------------------------------------------------
//  PlayoutIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::PlayoutIsInitialized() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  return (_ptrAudioDevice->PlayoutIsInitialized());
}

// ----------------------------------------------------------------------------
//  RecordingIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::RecordingIsInitialized() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  return (_ptrAudioDevice->RecordingIsInitialized());
}

// ----------------------------------------------------------------------------
//  StartPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StartPlayout() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->StartPlayout());
}

// ----------------------------------------------------------------------------
//  StopPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StopPlayout() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->StopPlayout());
}

// ----------------------------------------------------------------------------
//  Playing
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::Playing() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  return (_ptrAudioDevice->Playing());
}

// ----------------------------------------------------------------------------
//  StartRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StartRecording() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->StartRecording());
}
// ----------------------------------------------------------------------------
//  StopRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StopRecording() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  return (_ptrAudioDevice->StopRecording());
}

// ----------------------------------------------------------------------------
//  Recording
// ----------------------------------------------------------------------------

bool AudioDeviceModuleImpl::Recording() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  return (_ptrAudioDevice->Recording());
}

// ----------------------------------------------------------------------------
//  RegisterEventObserver
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RegisterEventObserver(
    AudioDeviceObserver* eventCallback) {
  WCLOG(INFO) << __FUNCTION__;
  CriticalSectionScoped lock(&_critSectEventCb);
  _ptrCbAudioDeviceObserver = eventCallback;

  return 0;
}

// ----------------------------------------------------------------------------
//  RegisterAudioCallback
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RegisterAudioCallback(
    AudioTransport* audioCallback) {
  WCLOG(INFO) << __FUNCTION__;
  CriticalSectionScoped lock(&_critSectAudioCb);
  _audioDeviceBuffer.RegisterAudioCallback(audioCallback);

  return 0;
}

// ----------------------------------------------------------------------------
//  StartRawInputFileRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StartRawInputFileRecording(
    const char pcmFileNameUTF8[kAdmMaxFileNameSize]) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  if (NULL == pcmFileNameUTF8) {
    return -1;
  }

  return (_audioDeviceBuffer.StartInputFileRecording(pcmFileNameUTF8));
}

// ----------------------------------------------------------------------------
//  StopRawInputFileRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StopRawInputFileRecording() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  return (_audioDeviceBuffer.StopInputFileRecording());
}

// ----------------------------------------------------------------------------
//  StartRawOutputFileRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StartRawOutputFileRecording(
    const char pcmFileNameUTF8[kAdmMaxFileNameSize]) {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  if (NULL == pcmFileNameUTF8) {
    return -1;
  }

  return (_audioDeviceBuffer.StartOutputFileRecording(pcmFileNameUTF8));
}

// ----------------------------------------------------------------------------
//  StopRawOutputFileRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::StopRawOutputFileRecording() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  return (_audioDeviceBuffer.StopOutputFileRecording());
}

// ----------------------------------------------------------------------------
//  SetPlayoutBuffer
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetPlayoutBuffer(const BufferType type,
                                                uint16_t sizeMS) {
  if (type == kFixedBufferSize) {
    WCLOG(INFO) << __FUNCTION__ << "("
              << "fixed buffer, " << sizeMS << "ms"
              << ")";
  } else if (type == kAdaptiveBufferSize) {
    WCLOG(INFO) << __FUNCTION__ << "("
              << "adaptive buffer, " << sizeMS << "ms"
              << ")";
  } else {
    WCLOG(INFO) << __FUNCTION__ << "("
              << "?, " << sizeMS << "ms"
              << ")";
  }
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->PlayoutIsInitialized()) {
    WCLOG(LERROR) << "unable to modify the playout buffer while playing side is "
                   "initialized";
    return -1;
  }

  int32_t ret(0);

  if (kFixedBufferSize == type) {
    if (sizeMS < kAdmMinPlayoutBufferSizeMs ||
        sizeMS > kAdmMaxPlayoutBufferSizeMs) {
      WCLOG(LERROR) << "size parameter is out of range";
      return -1;
    }
  }

  if ((ret = _ptrAudioDevice->SetPlayoutBuffer(type, sizeMS)) == -1) {
    WCLOG(LERROR) << "failed to set the playout buffer (error: " << LastError()
                << ")";
  }

  return ret;
}

// ----------------------------------------------------------------------------
//  PlayoutBuffer
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::PlayoutBuffer(BufferType* type,
                                             uint16_t* sizeMS) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  BufferType bufType;
  uint16_t size(0);

  if (_ptrAudioDevice->PlayoutBuffer(bufType, size) == -1) {
    WCLOG(LERROR) << "failed to retrieve the buffer type and size";
    return -1;
  }

  *type = bufType;
  *sizeMS = size;

  WCLOG(INFO) << __FUNCTION__ << " output: type = " << *type
            << ", sizeMS = " << *sizeMS;
  return (0);
}

// ----------------------------------------------------------------------------
//  PlayoutDelay
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::PlayoutDelay(uint16_t* delayMS) const {
  WCLOG(LS_VERBOSE) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t delay(0);

  if (_ptrAudioDevice->PlayoutDelay(delay) == -1) {
    WCLOG(LERROR) << "failed to retrieve the playout delay";
    return -1;
  }

  *delayMS = delay;

  WCLOG(LS_VERBOSE) << __FUNCTION__ << " output: delayMS = " << *delayMS;
  return (0);
}

// ----------------------------------------------------------------------------
//  RecordingDelay
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RecordingDelay(uint16_t* delayMS) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t delay(0);

  if (_ptrAudioDevice->RecordingDelay(delay) == -1) {
    WCLOG(LERROR) << "failed to retrieve the recording delay";
    return -1;
  }

  *delayMS = delay;

  WCLOG(INFO) << __FUNCTION__ << " output: delayMS = " << *delayMS;
  return (0);
}

// ----------------------------------------------------------------------------
//  CPULoad
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::CPULoad(uint16_t* load) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  uint16_t cpuLoad(0);

  if (_ptrAudioDevice->CPULoad(cpuLoad) == -1) {
    WCLOG(LERROR) << "failed to retrieve the CPU load";
    return -1;
  }

  *load = cpuLoad;

  WCLOG(INFO) << __FUNCTION__ << " output: load = " << *load;
  return (0);
}

// ----------------------------------------------------------------------------
//  SetRecordingSampleRate
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetRecordingSampleRate(
    const uint32_t samplesPerSec) {
  WCLOG(INFO) << __FUNCTION__ << "(" << samplesPerSec << ")";
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->SetRecordingSampleRate(samplesPerSec) != 0) {
    return -1;
  }

  return (0);
}

// ----------------------------------------------------------------------------
//  RecordingSampleRate
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::RecordingSampleRate(
    uint32_t* samplesPerSec) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  int32_t sampleRate = _audioDeviceBuffer.RecordingSampleRate();

  if (sampleRate == -1) {
    WCLOG(LERROR) << "failed to retrieve the sample rate";
    return -1;
  }

  *samplesPerSec = sampleRate;

  WCLOG(INFO) << __FUNCTION__ << " output: samplesPerSec = " << *samplesPerSec;
  return (0);
}

// ----------------------------------------------------------------------------
//  SetPlayoutSampleRate
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetPlayoutSampleRate(
    const uint32_t samplesPerSec) {
  WCLOG(INFO) << __FUNCTION__ << "(" << samplesPerSec << ")";
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->SetPlayoutSampleRate(samplesPerSec) != 0) {
    return -1;
  }

  return (0);
}

// ----------------------------------------------------------------------------
//  PlayoutSampleRate
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::PlayoutSampleRate(
    uint32_t* samplesPerSec) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  int32_t sampleRate = _audioDeviceBuffer.PlayoutSampleRate();

  if (sampleRate == -1) {
    WCLOG(LERROR) << "failed to retrieve the sample rate";
    return -1;
  }

  *samplesPerSec = sampleRate;

  WCLOG(INFO) << __FUNCTION__ << " output: samplesPerSec = " << *samplesPerSec;
  return (0);
}

// ----------------------------------------------------------------------------
//  ResetAudioDevice
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::ResetAudioDevice() {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->ResetAudioDevice() == -1) {
    return -1;
  }

  return (0);
}

// ----------------------------------------------------------------------------
//  SetLoudspeakerStatus
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::SetLoudspeakerStatus(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();

  if (_ptrAudioDevice->SetLoudspeakerStatus(enable) != 0) {
    return -1;
  }

  return 0;
}

// ----------------------------------------------------------------------------
//  GetLoudspeakerStatus
// ----------------------------------------------------------------------------

int32_t AudioDeviceModuleImpl::GetLoudspeakerStatus(bool* enabled) const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED();
  int32_t ok = 0;
  if (_ptrAudioDevice->GetLoudspeakerStatus(*enabled) != 0) {
    ok = -1;
  }
  WCLOG(INFO) << __FUNCTION__ << " output: " << ok;
  return ok;
}

bool AudioDeviceModuleImpl::BuiltInAECIsEnabled() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  bool isEnabled = _ptrAudioDevice->BuiltInAECIsEnabled();
  if (isEnabled) {
    WCLOG(INFO) << __FUNCTION__ << " output: enabled";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not enabled";
  }
  return isEnabled;
}

bool AudioDeviceModuleImpl::BuiltInAECIsAvailable() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  bool isAvailable = _ptrAudioDevice->BuiltInAECIsAvailable();
  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return isAvailable;
}

int32_t AudioDeviceModuleImpl::EnableBuiltInAEC(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  int32_t ok = _ptrAudioDevice->EnableBuiltInAEC(enable);
  WCLOG(INFO) << __FUNCTION__ << " output: " << ok;
  return ok;
}

bool AudioDeviceModuleImpl::BuiltInAGCIsAvailable() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  bool isAvailable = _ptrAudioDevice->BuiltInAGCIsAvailable();
  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return isAvailable;
}

int32_t AudioDeviceModuleImpl::EnableBuiltInAGC(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  int32_t ok = _ptrAudioDevice->EnableBuiltInAGC(enable);
  WCLOG(INFO) << __FUNCTION__ << " output: " << ok;
  return ok;
}

bool AudioDeviceModuleImpl::BuiltInNSIsAvailable() const {
  WCLOG(INFO) << __FUNCTION__;
  CHECK_INITIALIZED_BOOL();
  bool isAvailable = _ptrAudioDevice->BuiltInNSIsAvailable();
  if (isAvailable) {
    WCLOG(INFO) << __FUNCTION__ << " output: available";
  } else {
    WCLOG(INFO) << __FUNCTION__ << " output: not available";
  }
  return isAvailable;
}

int32_t AudioDeviceModuleImpl::EnableBuiltInNS(bool enable) {
  WCLOG(INFO) << __FUNCTION__ << "(" << enable << ")";
  CHECK_INITIALIZED();
  int32_t ok = _ptrAudioDevice->EnableBuiltInNS(enable);
  WCLOG(INFO) << __FUNCTION__ << " output: " << ok;
  return ok;
}

int AudioDeviceModuleImpl::GetPlayoutAudioParameters(
    AudioParameters* params) const {
  WCLOG(INFO) << __FUNCTION__;
  int r = _ptrAudioDevice->GetPlayoutAudioParameters(params);
  WCLOG(INFO) << __FUNCTION__ << " output: " << r;
  return r;
}

int AudioDeviceModuleImpl::GetRecordAudioParameters(
    AudioParameters* params) const {
  WCLOG(INFO) << __FUNCTION__;
  int r = _ptrAudioDevice->GetRecordAudioParameters(params);
  WCLOG(INFO) << __FUNCTION__ << " output: " << r;
  return r;
}

// ============================================================================
//                                 Private Methods
// ============================================================================

// ----------------------------------------------------------------------------
//  Platform
// ----------------------------------------------------------------------------

AudioDeviceModuleImpl::PlatformType AudioDeviceModuleImpl::Platform() const {
  WCLOG(INFO) << __FUNCTION__;
  return _platformType;
}

// ----------------------------------------------------------------------------
//  PlatformAudioLayer
// ----------------------------------------------------------------------------

AudioDeviceModule::AudioLayer AudioDeviceModuleImpl::PlatformAudioLayer()
    const {
  WCLOG(INFO) << __FUNCTION__;
  return _platformAudioLayer;
}

}  // namespace webrtc
