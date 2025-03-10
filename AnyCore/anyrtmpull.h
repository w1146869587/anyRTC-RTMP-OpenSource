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
#ifndef __ANY_RTMP_PULL_H__
#define __ANY_RTMP_PULL_H__
#include "webrtc/base/thread.h"
#include "srs_librtmp/srs_kernel_codec.h"
#include "AnyRtmpSource.h"

enum RTMPLAYER_STATUS
{
	RS_PLY_Init,		// 初始化状态
	RS_PLY_Handshaked,	// 与服务器协商过程中
	RS_PLY_Connected,	// 与服务器连接成功
	RS_PLY_Played,		// 开始播放
	RS_PLY_Closed,		// 播放停止
	RS_PLY_Slowdown,	// 通知数据源减速数据供应
	RS_PLY_PAUSE,
	RS_PLY_FlvfileFinished // FLV文件读取完毕。
};

typedef struct DemuxData
{
	DemuxData(int size) : _data(NULL), _data_len(0), _data_size(size){
		_data = new char[_data_size];
	}
	virtual ~DemuxData(void){ delete[] _data; }
	void reset() {
		_data_len = 0;
	}
	int append(const char* pData, int len){
		if (_data_len + len > _data_size)
			return 0;
		memcpy(_data + _data_len, pData, len);
		_data_len += len;
		return len;
	}

	char*_data;
	int _data_len;
	int _data_size;
}DemuxData;

class AnyRtmpPullCallback
{
public:
	AnyRtmpPullCallback(void){};
	virtual ~AnyRtmpPullCallback(void){};

	virtual void OnRtmpullConnected() = 0;
	virtual void OnRtmpullFailed() = 0;
	virtual void OnRtmpullDisconnect() = 0;
	virtual void OnRtmpullH264Data(const uint8_t*pdata, int len, uint32_t ts) = 0;
	virtual void OnRtmpullAACData(const uint8_t*pdata, int len, uint32_t ts) = 0;
	virtual bool OnRtmpullSlowdown() = 0;
	virtual void OnRtmpullConnectionFailed(int) = 0;
};

class AnyRtmpPull : public rtc::Thread
{
public:
	AnyRtmpPull(AnyRtmpPullCallback&callback, const char* url, const char* type,const char* dir,int32_t,char);// AnyBaseSource*);
	virtual ~AnyRtmpPull(void);

	double GetTotalTime() { return mtotaltime; };
	uint32_t SeekTo(uint32_t pos,double);
protected:
	//* For Thread
	virtual void Run();

	void DoReadData();
	int GotVideoSample(u_int32_t timestamp, SrsCodecSample *sample);
	int GotAudioSample(u_int32_t timestamp, SrsCodecSample *sample);
    
    void RescanVideoframe(const char*pdata, int len, uint32_t timestamp);

	void CallConnect();
	void CallDisconnect(int code);

private:
	double	mtotaltime;
	uint32_t mseekpos;
	AnyRtmpPullCallback&	callback_;
	SrsAvcAacCodec*		srs_codec_;
	bool				running_;
    bool                connected_;
	int					retry_ct_;
	std::string			str_url_;
	std::string mdir;
    
    rtc::CriticalSection	cs_rtmp_;
	RTMPLAYER_STATUS	rtmp_status_;
	//void*				rtmp_;
	AnyBaseSource*		mrtmp;
	DemuxData*			audio_payload_;
	DemuxData*			video_payload_;
};
#endif	// __ANY_RTMP_PULL_H__