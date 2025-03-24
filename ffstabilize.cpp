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

int av_check_err(int err, std::string filename, int line) {
	if (err < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE]{};
		av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, err);
		throw c4::exception(errbuf, filename, line);
	}
	return err;
}

#define AV_CALL(x) av_check_err(x, __FILE__, __LINE__)

class FfmpegFrameProcessor {
	AVFormatContext* inputFormatContext = nullptr;
	AVFormatContext* outputFormatContext = nullptr;
	//AVCodecContext* codecContext = nullptr;
	//AVFrame* frame = nullptr;

	static const AVCodec* find_best_encoder(const std::vector<std::string>& names){
		for (const std::string& name : names) {
			const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
			if (codec) {
				return codec;
			}
		}

		return nullptr;
	}

public:
	FfmpegFrameProcessor(const std::string& input_filename, const std::string& output_filename) {
		inputFormatContext = avformat_alloc_context();
		ASSERT_TRUE(inputFormatContext != nullptr);
		ASSERT_EQUAL(avformat_open_input(&inputFormatContext, input_filename.c_str(), NULL, NULL), 0);
		av_dump_format(inputFormatContext, 0, input_filename.c_str(), 0);
		AV_CALL(avformat_find_stream_info(inputFormatContext, NULL));

		avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, output_filename.c_str());
		ASSERT_TRUE(outputFormatContext != nullptr);

		const AVCodec *inputVideoCodec = NULL;
		AVCodecParameters *inputVideoCodecParameters =  NULL;

		int videoStreamIndex = -1;

		for (int i = 0; i < inputFormatContext->nb_streams; i++) {
			AVCodecParameters *inCodecParameters = inputFormatContext->streams[i]->codecpar;

			AVStream* outStream = avformat_new_stream(outputFormatContext, NULL);
			ASSERT_TRUE(outStream != nullptr);
			AV_CALL(avcodec_parameters_copy(outStream->codecpar, inCodecParameters));

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

		AVCodecContext* inputCodecContext = avcodec_alloc_context3(inputVideoCodec);
		ASSERT_TRUE(inputCodecContext != nullptr);
		ASSERT_TRUE(avcodec_parameters_to_context(inputCodecContext, inputVideoCodecParameters) >= 0);
		ASSERT_TRUE(avcodec_open2(inputCodecContext, inputVideoCodec, NULL) >= 0);

		av_dump_format(outputFormatContext, 0, output_filename.c_str(), 1);

		AVRational input_framerate = av_guess_frame_rate(inputFormatContext, inputFormatContext->streams[videoStreamIndex], NULL);
		const AVCodec* outputVideoCodec = find_best_encoder({"hevc_nvenc", "libx265", "libx264"});
		ASSERT_TRUE(outputVideoCodec != nullptr);

		AVCodecContext* outputCodecContext = avcodec_alloc_context3(outputVideoCodec);
		outputCodecContext->height = inputCodecContext->height;
		outputCodecContext->width = inputCodecContext->width;
		outputCodecContext->sample_aspect_ratio = inputCodecContext->sample_aspect_ratio;
		outputCodecContext->pix_fmt = inputCodecContext->pix_fmt;
		outputCodecContext->bit_rate = inputCodecContext->bit_rate;
		outputCodecContext->time_base = av_inv_q(input_framerate);

		AV_CALL(avcodec_open2(outputCodecContext, outputVideoCodec, NULL));
		AV_CALL(avcodec_parameters_from_context(outputFormatContext->streams[videoStreamIndex]->codecpar, outputCodecContext));

		if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER){
			outputFormatContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}

		AV_CALL(avio_open(&outputFormatContext->pb, output_filename.c_str(), AVIO_FLAG_WRITE));

		ASSERT_TRUE(avformat_write_header(outputFormatContext, NULL) >= 0);

		AVPacket packet;

		while (av_read_frame(inputFormatContext, &packet) >= 0) {
			AVStream* inStream = inputFormatContext->streams[packet.stream_index];
			AVStream* outStream = outputFormatContext->streams[packet.stream_index];

			if (packet.stream_index == videoStreamIndex) {
				AV_CALL(avcodec_send_packet(inputCodecContext, &packet));

				AVFrame* frame = av_frame_alloc();
				while(avcodec_receive_frame(inputCodecContext, frame) >= 0) {
					frame->pict_type = AV_PICTURE_TYPE_NONE;
					encode_frame(outputCodecContext, videoStreamIndex, inStream, outStream, frame);
				}
				av_frame_unref(frame);
			} else {
				packet.pts = av_rescale_q_rnd(packet.pts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				packet.dts = av_rescale_q_rnd(packet.dts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				packet.duration = av_rescale_q(packet.duration, inStream->time_base, outStream->time_base);
				packet.pos = -1;
				ASSERT_TRUE(av_interleaved_write_frame(outputFormatContext, &packet) >= 0);
			}

			av_packet_unref(&packet);
		}

		encode_frame(outputCodecContext, videoStreamIndex, inputFormatContext->streams[videoStreamIndex], outputFormatContext->streams[videoStreamIndex], nullptr);
		av_write_trailer(outputFormatContext);
	}

	void encode_frame(AVCodecContext* outputCodecContext, int videoStreamIndex, AVStream* inStream, AVStream* outStream, AVFrame* frame){
		avcodec_send_frame(outputCodecContext, frame);

		AVPacket *output_packet = av_packet_alloc();
		while (avcodec_receive_packet(outputCodecContext, output_packet) >= 0) {
			output_packet->stream_index = videoStreamIndex;
			av_packet_rescale_ts(output_packet, inStream->time_base, outStream->time_base);
			ASSERT_TRUE(av_interleaved_write_frame(outputFormatContext, output_packet) >= 0);
		}
		av_packet_unref(output_packet);
		av_packet_free(&output_packet);
	}

	~FfmpegFrameProcessor() {
		avformat_close_input(&inputFormatContext);
		
		avio_closep(&outputFormatContext->pb);
		avformat_free_context(outputFormatContext);
		
		//av_frame_free(&frame);
		//avcodec_free_context(&codecContext);
	}

private:
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
