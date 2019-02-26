
#ifdef	__cplusplus
extern "C"
{
#endif

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include <libavutil\avassert.h>

#ifdef __cplusplus
};
#endif

#include "ScreenRecordImpl.h"
#include <QDebug>
#include <QAudioDeviceInfo>
#include <thread>
#include <fstream>

#include <dshow.h>

using namespace std;

//g_collectFrameCnt等于g_encodeFrameCnt证明编解码帧数一致
int g_collectFrameCnt = 0;	//采集帧数
int g_encodeFrameCnt = 0;	//编码帧数


ScreenRecordImpl::ScreenRecordImpl(QObject * parent) :
	QObject(parent)
	, m_fps(25)
	, m_vIndex(-1)
	, m_vFmtCtx(nullptr),m_oFmtCtx(nullptr)
	, m_vDecodeCtx(nullptr)
	, m_vFifoBuf(nullptr)
	, m_swsCtx(nullptr)
	, m_stop(false)
	, m_vFrameIndex(0)
	, m_started(false)
{
}

void ScreenRecordImpl::Start()
{
	if (!m_started)
	{
		m_started = true;
		m_filePath = QStringLiteral("test.mp4");
		//m_width = 1920;
		//m_height = 1080;
		m_width = 1440;
		m_height = 900;
		m_fps = 30;
		std::thread encodeThread(&ScreenRecordImpl::EncodeThreadProc, this);
		encodeThread.detach();
	}
}

void ScreenRecordImpl::Finish()
{
	qDebug() << "stop record";
	m_stop = true;
}


int ScreenRecordImpl::OpenVideo()
{
	int ret = -1;
	AVInputFormat *ifmt = av_find_input_format("gdigrab");
	AVDictionary *options = nullptr;
	AVCodec *decoder = nullptr;
	av_dict_set(&options, "framerate", QString::number(m_fps).toStdString().c_str(), NULL);

	if (avformat_open_input(&m_vFmtCtx, "desktop", ifmt, &options) != 0)
	{
		qDebug() << "Cant not open video input stream";
		return -1;
	}
	if (avformat_find_stream_info(m_vFmtCtx, nullptr) < 0)
	{
		printf("Couldn't find stream information.（无法获取视频流信息）\n");
		return -1;
	}
	for (int i = 0; i < m_vFmtCtx->nb_streams; ++i)
	{
		AVStream *stream = m_vFmtCtx->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			decoder = avcodec_find_decoder(stream->codecpar->codec_id);
			if (decoder == nullptr)
			{
				printf("Codec not found.（没有找到解码器）\n");
				return -1;
			}
			//从视频流中拷贝参数到codecCtx
			m_vDecodeCtx = avcodec_alloc_context3(decoder);
			if ((ret = avcodec_parameters_to_context(m_vDecodeCtx, stream->codecpar)) < 0)
			{
				qDebug() << "Video avcodec_parameters_to_context failed,error code: " << ret;
				return -1;
			}
			m_vIndex = i;
			break;
		}
	}
	if (avcodec_open2(m_vDecodeCtx, decoder, nullptr) < 0)
	{
		printf("Could not open codec.（无法打开解码器）\n");
		return -1;
	}

	m_swsCtx = sws_getContext(m_vDecodeCtx->width, m_vDecodeCtx->height, m_vDecodeCtx->pix_fmt,
		m_width, m_height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
	return 0;
}

int ScreenRecordImpl::OpenOutput()
{
	int ret = -1;
	AVStream *vStream = nullptr;
	string outFilePath = m_filePath.toStdString();
	ret = avformat_alloc_output_context2(&m_oFmtCtx, nullptr, nullptr, outFilePath.c_str());
	if (ret < 0)
	{
		qDebug() << "avformat_alloc_output_context2 failed";
		return -1;
	}

	if (m_vFmtCtx->streams[m_vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		vStream = avformat_new_stream(m_oFmtCtx, nullptr);
		if (!vStream)
		{
			printf("can not new stream for output!\n");
			return -1;
		}
		//AVFormatContext第一个创建的流索引是0，第二个创建的流索引是1
		m_vOutIndex = vStream->index;
		vStream->time_base = AVRational{ 1, m_fps };

		m_vEncodeCtx = avcodec_alloc_context3(NULL);
		if (nullptr == m_vEncodeCtx)
		{
			qDebug() << "avcodec_alloc_context3 failed";
			return -1;
		}
		m_vEncodeCtx->width = m_width;
		m_vEncodeCtx->height = m_height;
		m_vEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
		m_vEncodeCtx->time_base.num = 1;
		m_vEncodeCtx->time_base.den = m_fps;
		m_vEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		m_vEncodeCtx->codec_id = AV_CODEC_ID_H264;
		m_vEncodeCtx->bit_rate = 800 * 1000;
		m_vEncodeCtx->rc_max_rate = 800 * 1000;
		m_vEncodeCtx->rc_buffer_size = 500 * 1000;
		//设置图像组层的大小, gop_size越大，文件越小 
		m_vEncodeCtx->gop_size = 30;
		m_vEncodeCtx->max_b_frames = 3;
		 //设置h264中相关的参数,不设置avcodec_open2会失败
		m_vEncodeCtx->qmin = 10;	//2
		m_vEncodeCtx->qmax = 31;	//31
		m_vEncodeCtx->max_qdiff = 4;
		m_vEncodeCtx->me_range = 16;	//0	
		m_vEncodeCtx->max_qdiff = 4;	//3	
		m_vEncodeCtx->qcompress = 0.6;	//0.5

		//查找视频编码器
		AVCodec *encoder;
		encoder = avcodec_find_encoder(m_vEncodeCtx->codec_id);
		if (!encoder)
		{
			qDebug() << "Can not find the encoder, id: " << m_vEncodeCtx->codec_id;
			return -1;
		}
		m_vEncodeCtx->codec_tag = 0;
		//正确设置sps/pps
		m_vEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		//打开视频编码器
		ret = avcodec_open2(m_vEncodeCtx, encoder, nullptr);
		if (ret < 0)
		{
			qDebug() << "Can not open encoder id: " << encoder->id << "error code: " << ret;
			return -1;
		}
		//将codecCtx中的参数传给输出流
		ret = avcodec_parameters_from_context(vStream->codecpar, m_vEncodeCtx);
		if (ret < 0)
		{
			qDebug() << "Output avcodec_parameters_from_context,error code:" << ret;
			return -1;
		}
	}

	//打开输出文件
	if (!(m_oFmtCtx->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_open(&m_oFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0)
		{
			printf("can not open output file handle!\n");
			return -1;
		}
	}
	//写文件头
	if (avformat_write_header(m_oFmtCtx, nullptr) < 0)
	{
		printf("can not write the header of the output file!\n");
		return -1;
	}
	return 0;
}

void ScreenRecordImpl::EncodeThreadProc()
{
	int ret = -1;
	//减小原子变量粒度
	bool done = false;
	int64_t vCurPts = 0;
	int vFrameIndex = 0;

	av_register_all();
	avdevice_register_all();
	avcodec_register_all();

	if (OpenVideo() < 0)
		return;
	if (OpenOutput() < 0)
		return;

	InitializeCriticalSection(&m_vSection);

	//设置视频帧
	m_vOutFrameSize = av_image_get_buffer_size(m_vEncodeCtx->pix_fmt, m_vEncodeCtx->width, m_vEncodeCtx->height, 1);
	m_vOutFrameBuf = (uint8_t *)av_malloc(m_vOutFrameSize);
	m_vOutFrame = av_frame_alloc();
	//先让AVFrame指针指向buf，后面再写入数据到buf
	av_image_fill_arrays(m_vOutFrame->data, m_vOutFrame->linesize, m_vOutFrameBuf, m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
	//申请30帧缓存
	m_vFifoBuf = av_fifo_alloc(30 * m_vOutFrameSize);

	//启动视频数据采集线程
	std::thread screenRecord(&ScreenRecordImpl::ScreenRecordThreadProc, this);
	screenRecord.detach();

	while (1)
	{
		if (m_stop && !done)
			done = true;
		//缓存数据写完就结束循环
		if (m_vFifoBuf && done && av_fifo_size(m_vFifoBuf) < m_vOutFrameSize)
			break;
		//if (done && av_fifo_size(m_vFifoBuf) < m_vOutFrameSize)
		//	vCurPts = 0x7fffffffffffffff;

		if (av_fifo_size(m_vFifoBuf) >= m_vOutFrameSize)
		{
			//从fifobuf读取一帧
			EnterCriticalSection(&m_vSection);
			av_fifo_generic_read(m_vFifoBuf, m_vOutFrameBuf, m_vOutFrameSize, NULL);
			LeaveCriticalSection(&m_vSection);

			//设置视频帧参数
			//m_vOutFrame->pts = vFrameIndex * ((m_oFmtCtx->streams[m_vOutIndex]->time_base.den / m_oFmtCtx->streams[m_vOutIndex]->time_base.num) / m_fps);
			m_vOutFrame->pts = vFrameIndex;
			++vFrameIndex;
			m_vOutFrame->format = m_vEncodeCtx->pix_fmt;
			m_vOutFrame->width = m_vEncodeCtx->width;
			m_vOutFrame->height = m_vEncodeCtx->height;

			AVPacket pkt = { 0 };
			av_init_packet(&pkt);

			ret = avcodec_send_frame(m_vEncodeCtx, m_vOutFrame);
			if (ret != 0)
			{
				qDebug() << "video avcodec_send_frame failed, ret: " << ret;
				av_packet_unref(&pkt);
				continue;
			}
			ret = avcodec_receive_packet(m_vEncodeCtx, &pkt);
			if (ret != 0)
			{
				av_packet_unref(&pkt);
				if (ret == AVERROR(EAGAIN))
				{
					qDebug() << "EAGAIN avcodec_receive_packet";
					continue;
				}
				qDebug() << "video avcodec_receive_packet failed, ret: " << ret;
				return;
			}

			/*ret = 1;
			while (ret)
			{
				ret = avcodec_send_frame(m_vEncodeCtx, m_vOutFrame);
				if (ret != 0)
				{
					qDebug() << "video avcodec_send_frame failed, ret: " << ret;
					av_packet_unref(&pkt);
					continue;
				}
				ret = avcodec_receive_packet(m_vEncodeCtx, &pkt);
				if (ret != 0)
				{
					av_packet_unref(&pkt);
					if (ret == AVERROR(EAGAIN))
					{
						Sleep(1);
						qDebug() << "EAGAIN avcodec_receive_packet";
						continue;
					}
					qDebug() << "video avcodec_receive_packet failed, ret: " << ret;
					return;
				}
			}*/


			//pkt.pts = av_rescale_q_rnd(pkt.pts, m_vFmtCtx->streams[m_vIndex]->time_base,
			//	m_oFmtCtx->streams[m_vOutIndex]->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
			//pkt.dts = av_rescale_q_rnd(pkt.dts, m_vFmtCtx->streams[m_vIndex]->time_base,
			//	m_oFmtCtx->streams[m_vOutIndex]->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
			//pkt.duration = ((m_oFmtCtx->streams[m_vOutIndex]->time_base.den / m_oFmtCtx->streams[m_vOutIndex]->time_base.num) / m_fps);
			//pkt.dts = pkt.pts;
			//pkt.duration = 1;
			//vCurPts = pkt.pts;
			pkt.stream_index = m_vOutIndex;
			av_packet_rescale_ts(&pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);

			ret = av_interleaved_write_frame(m_oFmtCtx, &pkt);
			if (ret == 0)
				qDebug() << "Write video packet id: " << ++g_encodeFrameCnt;
			else
				qDebug() << "video av_interleaved_write_frame failed, ret:" << ret;
			av_free_packet(&pkt);
		}
	}
	FlushEncoder();
	av_write_trailer(m_oFmtCtx);

	av_frame_free(&m_vOutFrame);
	av_free(m_vOutFrameBuf);
	avio_close(m_oFmtCtx->pb);
	avformat_free_context(m_oFmtCtx);
	if (m_vDecodeCtx)
	{
		avcodec_free_context(&m_vDecodeCtx);
		m_vDecodeCtx = nullptr;
	}
	if (m_vEncodeCtx)
	{
		avcodec_free_context(&m_vEncodeCtx);
		m_vEncodeCtx = nullptr;
	}
	av_fifo_freep(&m_vFifoBuf);
	if (m_vFmtCtx)
	{
		avformat_close_input(&m_vFmtCtx);
		m_vFmtCtx = nullptr;
	}
	qDebug() << "parent thread exit";
}

void ScreenRecordImpl::ScreenRecordThreadProc()
{
	int ret = -1;
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);
	int y_size = m_width * m_height;
	AVFrame	*oldFrame = av_frame_alloc();
	AVFrame *newFrame = av_frame_alloc();

	int oldFrameBufSize = av_image_get_buffer_size(m_vDecodeCtx->pix_fmt, m_vDecodeCtx->width, m_vDecodeCtx->height, 1);

	int newFrameBufSize = av_image_get_buffer_size(m_vEncodeCtx->pix_fmt, m_vEncodeCtx->width, m_vEncodeCtx->height, 1);
	uint8_t *newFrameBuf = (uint8_t*)av_malloc(newFrameBufSize);
	av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf,
		m_vEncodeCtx->pix_fmt, m_vEncodeCtx->width, m_vEncodeCtx->height, 1);

	while (!m_stop)
	{
		if (av_read_frame(m_vFmtCtx, &pkt) < 0)
		{
			qDebug() << "video av_read_frame < 0";
			continue;
		}
		if (pkt.stream_index == m_vIndex)
		{
			ret = avcodec_send_packet(m_vDecodeCtx, &pkt);
			if (ret != 0)
			{
				qDebug() << "avcodec_send_packet failed, ret:" << ret;
				av_packet_unref(&pkt);
				continue;
			}
			ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
			if (ret != 0)
			{
				qDebug() << "avcodec_receive_frame failed, ret:" << ret;
				av_packet_unref(&pkt);
				continue;
			}
			++g_collectFrameCnt;

			//srcSliceH到底是输入高度还是输出高度
			sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
				m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

			int ret = 1;
			while (ret)
			{
				if (av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize)
				{
					EnterCriticalSection(&m_vSection);
					av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
					av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
					av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
					LeaveCriticalSection(&m_vSection);
					ret = 0;
				}
				else
				{
					qDebug() << "video fifo buf overflow";
					//用条件信号替代
					Sleep(1);
				}
			}
		}
		else
			qDebug() << "not a video packet from video input";
		av_packet_unref(&pkt);
	}
	FlushDecoder();

	av_free(newFrameBuf);
	av_frame_free(&oldFrame);
	av_frame_free(&newFrame);
	qDebug() << "screen record thread exit";
}

void ScreenRecordImpl::FlushDecoder()
{
	int ret = -1;
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);
	int y_size = m_width * m_height;
	AVFrame	*oldFrame = av_frame_alloc();
	AVFrame *newFrame = av_frame_alloc();

	ret = avcodec_send_packet(m_vDecodeCtx, nullptr);
	qDebug() << "flush avcodec_send_packet, ret: " << ret;
	while (ret >= 0)
	{
		ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
		if (ret < 0)
		{
			av_packet_unref(&pkt);
			if (ret == AVERROR(EAGAIN))
			{
				qDebug() << "flush EAGAIN avcodec_receive_frame";
				continue;
			}
			else if (ret == AVERROR_EOF)
			{
				qDebug() << "flush video decoder finished";
				break;
			}
			qDebug() << "flush video avcodec_receive_frame error, ret: " << ret;
			return;
		}
		++g_collectFrameCnt;
		sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
			m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

		if (av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize)
		{
			EnterCriticalSection(&m_vSection);
			av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
			av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
			av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
			LeaveCriticalSection(&m_vSection);
		}
		else
			qDebug() << "flush video fifo buf overflow";
	}
	qDebug() << "collect frame count: " << g_collectFrameCnt;
}

void ScreenRecordImpl::FlushEncoder()
{
	int ret = -11;
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);
	ret = avcodec_send_frame(m_vEncodeCtx, nullptr);
	qDebug() << "avcodec_send_frame ret:" << ret;
	while (ret >= 0)
	{
		ret = avcodec_receive_packet(m_vEncodeCtx, &pkt);
		if (ret < 0)
		{
			av_packet_unref(&pkt);
			if (ret == AVERROR(EAGAIN))
			{
				qDebug() << "flush EAGAIN avcodec_receive_packet";
				continue;
			}
			else if (ret == AVERROR_EOF)
			{
				qDebug() << "flush video encoder finished";
				break;
			}
			qDebug() << "video avcodec_receive_packet failed, ret: " << ret;
			return;
		}
		pkt.stream_index = m_vOutIndex;
		av_packet_rescale_ts(&pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);

		ret = av_interleaved_write_frame(m_oFmtCtx, &pkt);
		if (ret == 0)
			qDebug() << "flush Write video packet id: " << ++g_encodeFrameCnt;
		else
			qDebug() << "video av_interleaved_write_frame failed, ret:" << ret;
		av_free_packet(&pkt);
	}
}
