//MIT License
//
//Copyright(c) 2025 Alex Kasitskyi
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files(the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions :
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#include <memory>

#include <c4/jpeg.hpp>
#include <c4/drawing.hpp>
#include <c4/string.hpp>
#include <c4/serialize.hpp>
#include <c4/image_dumper.hpp>
#include <c4/video_stabilization.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class FfmpegFrameProcessor {
	AVFormatContext* inputFormatContext = nullptr;
	AVFormatContext* outputFormatContext = nullptr;
	//AVCodecContext* codecContext = nullptr;
	//AVFrame* frame = nullptr;

public:
	FfmpegFrameProcessor(const std::string& input_filename, const std::string& output_filename) {
		inputFormatContext = avformat_alloc_context();
		ASSERT_TRUE(inputFormatContext != nullptr);
		ASSERT_EQUAL(avformat_open_input(&inputFormatContext, input_filename.c_str(), NULL, NULL), 0);
		av_dump_format(inputFormatContext, 0, input_filename.c_str(), 0);
		ASSERT_EQUAL(avformat_find_stream_info(inputFormatContext, NULL), 0);

		avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, output_filename.c_str());
		ASSERT_TRUE(outputFormatContext != nullptr);

		const AVCodec *inputVideoCodec = NULL;
		AVCodecParameters *inputVideoCodecParameters =  NULL;

		int videoStreamIndex = -1;

		for (int i = 0; i < inputFormatContext->nb_streams; i++) {
			AVCodecParameters *inCodecParameters = inputFormatContext->streams[i]->codecpar;

			AVStream* outStream = avformat_new_stream(outputFormatContext, NULL);
			ASSERT_TRUE(outStream != nullptr);
			ASSERT_TRUE(avcodec_parameters_copy(outStream->codecpar, inCodecParameters) >= 0);

			const AVCodec* codec = avcodec_find_decoder(inCodecParameters->codec_id);
			if (!codec) {
				continue;
			}
			PRINT_DEBUG(codec->name);
			PRINT_DEBUG(inCodecParameters->bit_rate);
			if (inCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
				videoStreamIndex = i;
				inputVideoCodec = codec;
				inputVideoCodecParameters = inCodecParameters;
				PRINT_DEBUG(inCodecParameters->width);
				PRINT_DEBUG(inCodecParameters->height);
			}
		}

		AVCodecContext* codecContext = avcodec_alloc_context3(inputVideoCodec);
		ASSERT_TRUE(codecContext != nullptr);
		ASSERT_TRUE(avcodec_parameters_to_context(codecContext, inputVideoCodecParameters) >= 0);
		ASSERT_TRUE(avcodec_open2(codecContext, inputVideoCodec, NULL) >= 0);

		av_dump_format(outputFormatContext, 0, output_filename.c_str(), 1);

		ASSERT_TRUE(avio_open(&outputFormatContext->pb, output_filename.c_str(), AVIO_FLAG_WRITE) >= 0);

		ASSERT_TRUE(avformat_write_header(outputFormatContext, NULL) >= 0);

		AVFrame* frame = av_frame_alloc();

		AVPacket packet;

		while (av_read_frame(inputFormatContext, &packet) >= 0) {
			if (packet.stream_index == videoStreamIndex) {
				decode_packet(&packet, codecContext, frame);
			}

			AVStream* inStream = inputFormatContext->streams[packet.stream_index];
			AVStream* outStream = outputFormatContext->streams[packet.stream_index];
			packet.pts = av_rescale_q_rnd(packet.pts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
			packet.dts = av_rescale_q_rnd(packet.dts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
			packet.duration = av_rescale_q(packet.duration, inStream->time_base, outStream->time_base);
			packet.pos = -1;
			ASSERT_TRUE(av_interleaved_write_frame(outputFormatContext, &packet) >= 0);

			av_packet_unref(&packet);
		}
		av_write_trailer(outputFormatContext);
	}

	//AVFrame* read_frame() {
	//	int how_many_packets_to_process = 8;

	//	if (av_read_frame(inputFormatContext, packet) >= 0 && how_many_packets_to_process-- > 0) {
	//		if (packet->stream_index == 0) {
	//			int response = decode_packet(packet, codecContext, frame);
	//			if (response < 0) {
	//				return nullptr;
	//			}
	//		}
	//		av_packet_unref(packet);
	//	} else {
	//		return nullptr;
	//	}
	//	return frame;
	//}
	
	~FfmpegFrameProcessor() {
		avformat_close_input(&inputFormatContext);
		
		avio_closep(&outputFormatContext->pb);
		avformat_free_context(outputFormatContext);
		
		//av_frame_free(&frame);
		//avcodec_free_context(&codecContext);
	}

private:
	static void decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame) {
		int response = avcodec_send_packet(pCodecContext, pPacket);

		if (response < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE]{};
			THROW_EXCEPTION(av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, response));
		}

		while (response >= 0) {
			response = avcodec_receive_frame(pCodecContext, pFrame);
			if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
				break;
			} 
			if (response < 0) {
				break;
			}

			ASSERT_EQUAL (pFrame->format, AV_PIX_FMT_YUV420P);

			c4::matrix_ref img(pFrame->height, pFrame->width, pFrame->linesize[0], pFrame->data[0]);

			//c4::dump_image(img, "in");
		}
	}
};

int main(int argc, char* argv[]) {
    try{
        c4::scoped_timer timer("main");

		c4::image_dumper::getInstance().init("", true);

		FfmpegFrameProcessor frameProcessor("in.mp4", "out.mp4");

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
