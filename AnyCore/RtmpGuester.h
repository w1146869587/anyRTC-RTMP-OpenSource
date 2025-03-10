/*
*  Copyright (c) 2016 The AnyRTC project authors. All Rights Reserved.
*
*  Please visit https://www.anyrtc.io for detail.
*
* The GNU General Public License is a free, copyleft license for
* software and other kinds of works.
*
* The licenses for most software and other practical works are designed
* to take away your freedom to share and change the works.  By contrast,
* the GNU General Public License is intended to guarantee your freedom to
* share and change all versions of a program--to make sure it remains free
* software for all its users.  We, the Free Software Foundation, use the
* GNU General Public License for most of our software; it applies also to
* any other work released this way by its authors.  You can apply it to
* your programs, too.
* See the GNU LICENSE file for more info.
*/
#ifndef __RTMP_GUESTER_H__
#define __RTMP_GUESTER_H__
#include <string>
#include "LIV_Export.h"
#include "RTMPCommon.h"
#include "AnyBaseSource.h"


class RTMPGuesterEvent{
public:
	RTMPGuesterEvent(void) {};
	virtual ~RTMPGuesterEvent(void) {};

	virtual void OnRtmplayerOK() = 0;
	virtual void OnRtmplayerStatus(int cacheTime, int curBitrate,uint32_t, double) = 0;
	virtual void OnRtmplayerCache(int time) = 0;
	virtual void OnRtmplayerClosed(int errcode/*0:OK */) = 0;
	virtual void OnGetPcmData(const void * p, const int len,const int rate, const int channels) = 0;
	virtual void OnRtmplayerPlayStart() = 0;
	virtual void OnRtmplayerPlayStop() = 0;
	virtual void OnRtmplayer1stVideo() = 0;
	virtual void OnRtmplayer1stAudio() = 0;
	virtual void OnRtmplayerConnectionFailed(int a) = 0;
};

class LIV_API RTMPGuester{
public:
	static RTMPGuester* Create(RTMPGuesterEvent&callback);
	static void Destory(RTMPGuester*guester);

	virtual void StartRtmpPlay(const char* url, void* render, const char* sourcetype, const char* datadir,int32_t,char, const char*, const short**, const int*) = 0;
	virtual uint32_t SeekTo(uint32_t, double) = 0;
	virtual void StopRtmpPlay() = 0;

protected:
	virtual void* GotSelfPtr() = 0;

protected:
	RTMPGuester() {};
	virtual ~RTMPGuester() {};
};

#endif	// __RTMP_GUESTER_H__