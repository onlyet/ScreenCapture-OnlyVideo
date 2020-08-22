#pragma once
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <QObject>
#include <QString>
#include <QMutex>
#include <QVariant>
#include <condition_variable>
#include <chrono>
#include <queue>

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
private:
	enum RecordState {
		NotStarted,
		Started,
		Paused,
		Stopped,
		Unknown,
	};
public:

	ScreenRecordImpl(QObject * parent = Q_NULLPTR);
	//��ʼ����Ƶ����
	void Init(const QVariantMap& map);

	private slots :
	void Start();
	void Pause();
	void Stop();

private:
	//��fifobuf��ȡ��Ƶ֡������д��������������ļ�
	void ScreenRecordThreadProc();
	//����Ƶ��������ȡ֡��д��fifobuf
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

	int m_vIndex;		//������Ƶ������
	int m_vOutIndex;	//�����Ƶ������
	AVFormatContext		*m_vFmtCtx;
	AVFormatContext		*m_oFmtCtx;
	AVCodecContext		*m_vDecodeCtx;
	AVCodecContext		*m_vEncodeCtx;
	AVDictionary		*m_dict;
	SwsContext			*m_swsCtx;
	AVFifoBuffer		*m_vFifoBuf;
	AVFrame				*m_vOutFrame;
	uint8_t				*m_vOutFrameBuf;
	int					m_vOutFrameSize;	// yuv:3/2*width*height
    int                 m_vOutFrameItemSize; // m_vOutFrameSize + sizeof(long long)
	RecordState			m_state;

	//�����ٶ�һ��Ȳɼ��ٶ��������Կ���ȥ��m_cvNotEmpty
	std::condition_variable m_cvNotFull;	//��fifoBuf���ˣ��ɼ��̹߳���
	std::condition_variable m_cvNotEmpty;	//��fifoBuf���ˣ������̹߳���
	std::mutex				m_mtx;			//m_cvNotFull��m_cvNotEmpty�������mutex
	std::condition_variable m_cvNotPause;	//�������ͣ��ʱ�򣬲ɼ��̹߳���
	std::mutex				m_mtxPause;

    std::chrono::steady_clock::time_point    m_firstTimePoint;
    long long                               m_timestamp;    // ���ʱ������ɼ��ĵ�һ֡Ϊ0

    std::queue<long long> m_tsList; // �ɼ�֡ʱ����б�

    long long m_previousTimestamp = 0; // ��һ֡��ʱ���
};