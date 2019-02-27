#pragma once


#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <QObject>
#include <QString>
#include <QMutex>
#include <QVariant>

#ifdef	__cplusplus
extern "C"
{
#endif
struct AVFormatContext;
struct AVCodecContext;
struct AVCodec;
struct AVFifoBuffer;
struct AVAudioFifo;
struct AVFrame;
struct SwsContext;
struct SwrContext;
struct AVDictionary;
#ifdef __cplusplus
};
#endif

class ScreenRecordImpl : public QObject
{
	Q_OBJECT
public:
	enum RecordState {
		NotStarted,
		Started,
		Paused,
		Stopped,
		Unknown,
	};

	ScreenRecordImpl(QObject * parent = Q_NULLPTR);
	//初始化视频参数
	void Init(const QVariantMap& map);

	private slots :
	void Start();
	void Pause();
	void Stop();

private:
	//从fifobuf读取视频帧，编码写入输出流，生成文件
	void ScreenRecordThreadProc();
	//从视频输入流读取帧，写入fifobuf
	void ScreenAcquireThreadProc();
	int OpenVideo();
	int OpenOutput();
	void SetEncoderParm();
	void FlushDecoder();
	void FlushEncoder();
	void InitBuffer();
	void Release();

private:
	QString				m_filePath;
	int					m_width;
	int					m_height;
	int					m_fps;

	int m_vIndex;		//输入视频流索引
	int m_vOutIndex;	//输出视频流索引
	AVFormatContext		*m_vFmtCtx;
	AVFormatContext		*m_oFmtCtx;
	AVCodecContext		*m_vDecodeCtx;
	AVCodecContext		*m_vEncodeCtx;
	AVDictionary		*m_dict;
	SwsContext			*m_swsCtx;
	AVFifoBuffer		*m_vFifoBuf;
	AVFrame				*m_vOutFrame;
	uint8_t				*m_vOutFrameBuf;
	int					m_vOutFrameSize;	//一个输出帧的字节
	RecordState			m_state;

	//编码速度一般比采集速度慢，所以可以去掉m_cvNotEmpty
	std::condition_variable m_cvNotFull;
	std::condition_variable m_cvNotEmpty;
	std::mutex				m_mtx;
	std::condition_variable m_cvNotPause;
	std::mutex				m_mtxPause;
};