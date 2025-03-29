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

#include <c4/drawing.hpp>
#include <c4/cmd_opts.hpp>
#include <c4/image_dumper.hpp>
#include <c4/video_stabilization.hpp>
#include <c4/progress_indicator.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
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

class FfmpegVideoProcessor {
	static const AVCodec* find_best_encoder(const std::vector<std::string>& names, const AVPixelFormat px_fmt){
		for (const std::string& name : names) {
			const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
			if (!codec)
				continue;
			const enum AVPixelFormat *pix_fmts = NULL;
			int out_num_configs = 0;
			avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&pix_fmts, &out_num_configs);
			for (int i = 0; i < out_num_configs; i++) {
				if (pix_fmts[i] == px_fmt)
					return codec;
			}
		}

		return nullptr;
	}

	AVFormatContext* inputFormatContext = nullptr;
	AVFormatContext* outputFormatContext = nullptr;
	AVCodecContext* inputCodecContext = nullptr;
	AVCodecContext* outputCodecContext = nullptr;

	int videoStreamIndex = -1;
	std::vector<int> streamMapping;
	int frameNumber = 0;

public:

	class FrameProcessor {
	public:
		virtual void operator()(AVFrame* src) = 0;
		virtual ~FrameProcessor() = default;
	};

	FfmpegVideoProcessor(const std::string& input_filename, const std::string& output_filename) {
		inputFormatContext = avformat_alloc_context();
		ASSERT_TRUE(inputFormatContext != nullptr);
		AV_CALL(avformat_open_input(&inputFormatContext, input_filename.c_str(), NULL, NULL));
		if (c4::Logger::getLogLevel() >= c4::LOG_DEBUG) {
			av_dump_format(inputFormatContext, 0, input_filename.c_str(), 0);
		}
		AV_CALL(avformat_find_stream_info(inputFormatContext, NULL));

		avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, output_filename.c_str());
		ASSERT_TRUE(outputFormatContext != nullptr);

		const AVCodec *inputVideoCodec = NULL;
		AVCodecParameters *inputVideoCodecParameters =  NULL;

		streamMapping.resize(inputFormatContext->nb_streams, -1);

		int outStreamIndex = 0;

		for (int i = 0; i < inputFormatContext->nb_streams; i++) {
			AVCodecParameters *inCodecParameters = inputFormatContext->streams[i]->codecpar;

			if (inCodecParameters->codec_type != AVMEDIA_TYPE_AUDIO && inCodecParameters->codec_type != AVMEDIA_TYPE_VIDEO && inCodecParameters->codec_type != AVMEDIA_TYPE_SUBTITLE) {
				LOGW << "Skipping stream " << i << " of type " << av_get_media_type_string(inCodecParameters->codec_type);
				continue;
			}

			AVStream* outStream = avformat_new_stream(outputFormatContext, NULL);
			ASSERT_TRUE(outStream != nullptr);
			AV_CALL(avcodec_parameters_copy(outStream->codecpar, inCodecParameters));
			streamMapping[i] = outStreamIndex++;

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
				frameNumber = inputFormatContext->streams[i]->nb_frames;
				PRINT_DEBUG(frameNumber);
				PRINT_DEBUG(inCodecParameters->width);
				PRINT_DEBUG(inCodecParameters->height);
			}
		}

		ASSERT_TRUE(videoStreamIndex >= 0);

		inputCodecContext = avcodec_alloc_context3(inputVideoCodec);
		ASSERT_TRUE(inputCodecContext != nullptr);
		ASSERT_TRUE(avcodec_parameters_to_context(inputCodecContext, inputVideoCodecParameters) >= 0);
		ASSERT_TRUE(avcodec_open2(inputCodecContext, inputVideoCodec, NULL) >= 0);

		if (c4::Logger::getLogLevel() >= c4::LOG_DEBUG) {
			av_dump_format(outputFormatContext, 0, output_filename.c_str(), 1);
		}

		AVRational input_framerate = av_guess_frame_rate(inputFormatContext, inputFormatContext->streams[videoStreamIndex], NULL);
		const AVCodec* outputVideoCodec = find_best_encoder({"hevc_nvenc", "libx265", "libx264"}, inputCodecContext->pix_fmt);
		ASSERT_TRUE(outputVideoCodec != nullptr);

		outputCodecContext = avcodec_alloc_context3(outputVideoCodec);
		ASSERT_TRUE(outputCodecContext != nullptr);

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

		AV_CALL(avformat_write_header(outputFormatContext, NULL));
	}

	void process(FrameProcessor& frame_processor) {
		c4::progress_indicator progress(frameNumber, "Processing frames");

		AVPacket packet;
		while (av_read_frame(inputFormatContext, &packet) >= 0) {
			if (streamMapping[packet.stream_index] < 0) {
				av_packet_unref(&packet);
				continue;
			}

			AVStream* inStream = inputFormatContext->streams[packet.stream_index];
			AVStream* outStream = outputFormatContext->streams[packet.stream_index];

			if (packet.stream_index == videoStreamIndex) {
				AV_CALL(avcodec_send_packet(inputCodecContext, &packet));

				AVFrame* frame = av_frame_alloc();
				while(avcodec_receive_frame(inputCodecContext, frame) >= 0) {
					frame_processor(frame);
					frame->pict_type = AV_PICTURE_TYPE_NONE;
					encode_frame(inStream, outStream, frame);
					progress.did_some(1);
				}
				av_frame_unref(frame);
			} else {
				packet.pts = av_rescale_q_rnd(packet.pts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				packet.dts = av_rescale_q_rnd(packet.dts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				packet.duration = av_rescale_q(packet.duration, inStream->time_base, outStream->time_base);
				packet.pos = -1;
				AV_CALL(av_interleaved_write_frame(outputFormatContext, &packet));
			}

			av_packet_unref(&packet);
		}

		progress.print_final();

		encode_frame(inputFormatContext->streams[videoStreamIndex], outputFormatContext->streams[videoStreamIndex], nullptr);
		av_write_trailer(outputFormatContext);
	}

	void encode_frame(AVStream* inStream, AVStream* outStream, AVFrame* frame){
		AV_CALL(avcodec_send_frame(outputCodecContext, frame));

		AVPacket *output_packet = av_packet_alloc();
		while (avcodec_receive_packet(outputCodecContext, output_packet) >= 0) {
			output_packet->stream_index = videoStreamIndex;
			av_packet_rescale_ts(output_packet, inStream->time_base, outStream->time_base);
			ASSERT_TRUE(av_interleaved_write_frame(outputFormatContext, output_packet) >= 0);
		}
		av_packet_unref(output_packet);
		av_packet_free(&output_packet);
	}

	~FfmpegVideoProcessor() {
		avformat_close_input(&inputFormatContext);
		
		avio_closep(&outputFormatContext->pb);
		avformat_free_context(outputFormatContext);
	}

private:
};


class VidStabProcessor : public FfmpegVideoProcessor::FrameProcessor {
	c4::VideoStabilization stabilizer;
	const int downscale;
	SwsContext* sws_downscale_ctx = nullptr;

	static std::vector<c4::rectangle<int>> downscale_rects(const std::vector<c4::rectangle<int>>& rects, int downscale) {
		std::vector<c4::rectangle<int>> scaled;
		for (const c4::rectangle<int>& r : rects) {
			scaled.emplace_back(r.x / downscale, r.y / downscale, r.w / downscale, r.h / downscale);
		}
		return scaled;
	}
public:
	VidStabProcessor(const c4::VideoStabilization::Params& params, const std::vector<c4::rectangle<int>> ignore, int downscale) : stabilizer(params, downscale_rects(ignore, downscale)), downscale(downscale) {
	}
	
	void operator()(AVFrame* src) override {
        c4::scoped_timer timer("VidStabProcessor()");

		ASSERT_TRUE(src != nullptr);
		//PRINT_DEBUG(av_frame_is_writable(src));
		AV_CALL(av_frame_make_writable(src));

		const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get((AVPixelFormat)src->format);

		c4::VideoStabilization::FramePtr frame = std::make_shared<c4::VideoStabilization::Frame>();

		if (pixdesc->comp[0].depth == 8) {
			ASSERT_EQUAL(pixdesc->comp[0].step, 1);
			c4::matrix_ref<uint8_t> m(src->height, src->width, src->linesize[0],  src->data[0] + pixdesc->comp[0].offset);
			c4::downscale_bilinear_nx(m, *frame, downscale);
		} else {
			frame->resize(src->height / downscale, src->width / downscale);
			if (sws_downscale_ctx == nullptr) {
				sws_downscale_ctx = sws_getContext(src->width, src->height, (AVPixelFormat)src->format, frame->width(), frame->height(), AV_PIX_FMT_GRAY8, SWS_AREA, 0, 0, 0);
				ASSERT_TRUE(sws_downscale_ctx != nullptr);
			}
			uint8_t* dst_data[1] = { frame->data() };
			int dst_stride[1] = { frame->stride() };
			int ret = sws_scale(sws_downscale_ctx, src->data, src->linesize, 0, src->height, dst_data, dst_stride);
			ASSERT_EQUAL(ret, frame->height());
		}

		c4::MotionDetector::Motion motion = stabilizer.process(frame);

		const int planes = pixdesc->nb_components;
		ASSERT_EQUAL(planes, av_pix_fmt_count_planes((AVPixelFormat)src->format));

        c4::scoped_timer timer2("VidStabProcessor(): apply");

		for (int p = 0; p < planes; p++) {
			const int h = p ? AV_CEIL_RSHIFT(src->height, pixdesc->log2_chroma_h) : src->height;
			const int w = p ? AV_CEIL_RSHIFT(src->width, pixdesc->log2_chroma_w) : src->width;
			c4::MotionDetector::Motion planeSizeAdjustedMotion = motion;
			planeSizeAdjustedMotion.shift.y *= (double)h / frame->height();
			planeSizeAdjustedMotion.shift.x *= (double)w / frame->width();

			if (pixdesc->comp[p].depth == 8) {
				ASSERT_EQUAL(pixdesc->comp[p].step, 1);

				c4::matrix_ref<uint8_t> planeRef(h, w, src->linesize[p], src->data[p] + pixdesc->comp[p].offset);
				c4::matrix<uint8_t> srcPlaneCopy = planeRef;
				planeSizeAdjustedMotion.apply(srcPlaneCopy, planeRef);
			}else{
				ASSERT_TRUE(pixdesc->comp[p].depth > 8 && pixdesc->comp[p].depth <= 16);
				ASSERT_EQUAL(pixdesc->comp[p].step, 2);

				c4::matrix_ref<uint16_t> planeRef(h, w, src->linesize[p] / 2, (uint16_t*)(src->data[p] + pixdesc->comp[p].offset));
				c4::matrix<uint16_t> srcPlaneCopy = planeRef;
				planeSizeAdjustedMotion.apply(srcPlaneCopy, planeRef);
			}
		}
	}

	~VidStabProcessor() override {
		sws_freeContext(sws_downscale_ctx);
	}
};

int main(int argc, char* argv[]) {
    try{
		c4::Logger::setLogLevel(c4::LOG_INFO);

		c4::scoped_timer timer("main", c4::LOG_DEBUG);

		c4::VideoStabilization::Params params;

		c4::cmd_opts opts;
		auto inputCmdOpt = opts.add_required_free_arg<std::string>("input.mp4");
		auto outputCmdOpt = opts.add_required_free_arg<std::string>("output.mp4");
		auto downscaleCmdOpt = opts.add_optional<int>("downscale", 2, "Downscale factor used for motion detection.");

		auto xSmoothCmdOpt = opts.add_optional<int>("x_smooth", params.x_smooth, "How many frames should be used for horizontal motion smoothing.");
		auto ySmoothCmdOpt = opts.add_optional<int>("y_smooth", params.y_smooth, "How many frames should be used for vertical motion smoothing.");
		auto scaleSmoothCmdOpt = opts.add_optional<int>("scale_smooth", params.scale_smooth, "How many frames should be used for scale smoothing.");
		auto alphaSmoothCmdOpt = opts.add_optional<int>("alpha_smooth", params.alpha_smooth, "How many frames should be used for rotation smoothing.");
		auto blocksizeCmdOpt = opts.add_optional<int>("block_size", params.blockSize, "Block size in pixels (after downscale).");
		auto maxShiftCmdOpt = opts.add_optional<int>("max_shift", params.maxShift, "Max shift in pixels (after downscale), should be <= block_size / 2.");
		auto maxAlphaCmdOpt = opts.add_optional<double>("max_alpha", params.maxAlpha, "Max rotation angle of consecutive frames, in radians.");
		auto maxScaleCmdOpt = opts.add_optional<double>("max_scale", params.maxScale, "Max scale ratio of consecutive frames (1 / max_scale if we scale down).");

		auto ignoreCmdOpt = opts.add_multiple("ignore", "Add rectangle where motion should be ignored. Format: \"x, y, w, h\".");

		auto debugCmdOpt = opts.add_flag("debug", "Enable debug output.");
		auto verboseCmdOpt = opts.add_flag("verbose", "Enable verbose output.");

		opts.parse(argc, argv);

		if (debugCmdOpt) {
			c4::Logger::setLogLevel(c4::LOG_DEBUG);
		}

		if (verboseCmdOpt) {
			c4::Logger::setLogLevel(c4::LOG_VERBOSE);
		}

		const std::string inputFilename = inputCmdOpt;
		LOGI << "Input file: " << inputFilename;
		const std::string outputFilename = outputCmdOpt;

		const int downscale = downscaleCmdOpt;

		params.x_smooth = xSmoothCmdOpt;
		params.y_smooth = ySmoothCmdOpt;
		params.scale_smooth = scaleSmoothCmdOpt;
		params.alpha_smooth = alphaSmoothCmdOpt;

		params.blockSize = blocksizeCmdOpt;
		params.maxShift = maxShiftCmdOpt;
		params.maxAlpha = maxAlphaCmdOpt;
		params.maxScale = maxScaleCmdOpt;

		std::vector<std::string> ignore = ignoreCmdOpt;

		std::vector<c4::rectangle<int>> ignoreRects;
		for (const std::string& s : ignore) {
			std::vector<std::string> parts = c4::split(s, ", ");
			if (parts.size() != 4) {
				THROW_EXCEPTION("Invalid ignore rectangle: " + s);
			}
			c4::rectangle<int> r(std::stoi(parts[0]), std::stoi(parts[1]), std::stoi(parts[2]), std::stoi(parts[3]));
			ignoreRects.push_back(r);

			LOGD << "Ignore rect: " << r.x << " " << r.y << " " << r.w << " " << r.h;
		}

		c4::image_dumper::getInstance().init("", false);

		VidStabProcessor frameProcessor(params, ignoreRects, downscale);

		FfmpegVideoProcessor videoProcessor(inputFilename, outputFilename);
		videoProcessor.process(frameProcessor);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
