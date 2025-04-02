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
	const std::string input_filename;
	AVFormatContext* inputFormatContext = nullptr;
	AVCodecContext* inputCodecContext = nullptr;

	const std::string output_filename;
	const std::string output_codec;
	const int64_t output_bitrate;
	AVFormatContext* outputFormatContext = nullptr;
	AVCodecContext* outputCodecContext = nullptr;

	int videoStreamIndex = -1;
	std::vector<int> streamMapping;
	int frameNumber = 0;

public:

	class FrameProcessor {
	public:
		virtual void preprocess(AVFrame* src) = 0;
		virtual void process(AVFrame* src) = 0;
		virtual ~FrameProcessor() = default;
	};

	void init_input() {
		inputFormatContext = avformat_alloc_context();
		ASSERT_TRUE(inputFormatContext != nullptr);
		AV_CALL(avformat_open_input(&inputFormatContext, input_filename.c_str(), NULL, NULL));
		if (c4::Logger::getLogLevel() >= c4::LOG_DEBUG) {
			av_dump_format(inputFormatContext, 0, input_filename.c_str(), 0);
		}
		AV_CALL(avformat_find_stream_info(inputFormatContext, NULL));

		const AVCodec *inputVideoCodec = NULL;
		AVCodecParameters *inputVideoCodecParameters =  NULL;

		streamMapping = std::vector<int>(inputFormatContext->nb_streams, -1);

		int outStreamIndex = 0;
		videoStreamIndex = -1;

		for (int i = 0; i < inputFormatContext->nb_streams; i++) {
			AVCodecParameters *inCodecParameters = inputFormatContext->streams[i]->codecpar;

			if (inCodecParameters->codec_type != AVMEDIA_TYPE_AUDIO && inCodecParameters->codec_type != AVMEDIA_TYPE_VIDEO && inCodecParameters->codec_type != AVMEDIA_TYPE_SUBTITLE) {
				LOGW << "Skipping stream " << i << " of type " << av_get_media_type_string(inCodecParameters->codec_type);
				continue;
			}

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
	}

	void init_output() {
		avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, output_filename.c_str());
		ASSERT_TRUE(outputFormatContext != nullptr);

		for (int i = 0; i < streamMapping.size(); i++) {
			if (streamMapping[i] < 0) {
				continue;
			}
			AVStream* inStream = inputFormatContext->streams[i];
			AVStream* outStream = avformat_new_stream(outputFormatContext, NULL);
			ASSERT_TRUE(outStream != nullptr);
			AV_CALL(avcodec_parameters_copy(outStream->codecpar, inStream->codecpar));
		}

		if (c4::Logger::getLogLevel() >= c4::LOG_DEBUG) {
			av_dump_format(outputFormatContext, 0, output_filename.c_str(), 1);
		}

		AVRational input_framerate = av_guess_frame_rate(inputFormatContext, inputFormatContext->streams[videoStreamIndex], NULL);
		const AVCodec* outputVideoCodec = avcodec_find_encoder_by_name(output_codec.c_str());
		ASSERT_TRUE(outputVideoCodec != nullptr);

		outputCodecContext = avcodec_alloc_context3(outputVideoCodec);
		ASSERT_TRUE(outputCodecContext != nullptr);

		outputCodecContext->height = inputCodecContext->height;
		outputCodecContext->width = inputCodecContext->width;
		outputCodecContext->sample_aspect_ratio = inputCodecContext->sample_aspect_ratio;
		outputCodecContext->pix_fmt = inputCodecContext->pix_fmt;
		outputCodecContext->bit_rate = output_bitrate ? output_bitrate : inputCodecContext->bit_rate;
		outputCodecContext->time_base = av_inv_q(input_framerate);

		AV_CALL(avcodec_open2(outputCodecContext, outputVideoCodec, NULL));
		AV_CALL(avcodec_parameters_from_context(outputFormatContext->streams[videoStreamIndex]->codecpar, outputCodecContext));

		if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER){
			outputFormatContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}

		AV_CALL(avio_open(&outputFormatContext->pb, output_filename.c_str(), AVIO_FLAG_WRITE));

		AV_CALL(avformat_write_header(outputFormatContext, NULL));
	}

	FfmpegVideoProcessor(const std::string& input_filename, const std::string& output_filename, const int64_t output_bitrate, const std::string output_codec)
		: input_filename(input_filename), output_filename(output_filename), output_bitrate(output_bitrate), output_codec(output_codec) {
		init_input();
		init_output();
	}

	c4::matrix_dimensions get_frame_size() const {
		c4::matrix_dimensions ret;
		ret.height = inputCodecContext->height;
		ret.width = inputCodecContext->width;
		return ret;
	}

	void process(FrameProcessor& frame_processor, bool preprocess) {
		c4::progress_indicator progress(frameNumber, preprocess ? "Pre-processing frames" : "Processing frames");

		AVPacket packet;
		while (av_read_frame(inputFormatContext, &packet) >= 0) {
			if (streamMapping[packet.stream_index] < 0) {
				av_packet_unref(&packet);
				continue;
			}

			AVStream* inStream = inputFormatContext->streams[packet.stream_index];

			if (packet.stream_index == videoStreamIndex) {
				AV_CALL(avcodec_send_packet(inputCodecContext, &packet));

				AVFrame* frame = av_frame_alloc();
				while(avcodec_receive_frame(inputCodecContext, frame) >= 0) {
					if (preprocess) {
						frame_processor.preprocess(frame);
					} else {
						frame_processor.process(frame);
						frame->pict_type = AV_PICTURE_TYPE_NONE;
						encode_frame(inStream, outputFormatContext->streams[packet.stream_index], frame);
					}
					progress.did_some(1);
				}
				av_frame_unref(frame);
			} else if (!preprocess) {
				AVStream* outStream = outputFormatContext->streams[packet.stream_index];
				packet.pts = av_rescale_q_rnd(packet.pts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				packet.dts = av_rescale_q_rnd(packet.dts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
				packet.duration = av_rescale_q(packet.duration, inStream->time_base, outStream->time_base);
				packet.pos = -1;
				AV_CALL(av_interleaved_write_frame(outputFormatContext, &packet));
			}

			av_packet_unref(&packet);
		}

		progress.print_final();

		if (!preprocess) {
			encode_frame(inputFormatContext->streams[videoStreamIndex], outputFormatContext->streams[videoStreamIndex], nullptr);
			av_write_trailer(outputFormatContext);
			avio_closep(&outputFormatContext->pb);
			avformat_free_context(outputFormatContext);
		}

		avformat_close_input(&inputFormatContext);
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
};

class VidStabProcessor : public FfmpegVideoProcessor::FrameProcessor {
	c4::VideoStabilization stabilizer;
	const std::vector<c4::rectangle<int>> ignoreRects;
	const int downscale;
	const double prezoom;
	const int autozoom;
	const double zoomspeed;
	double zoom;
	SwsContext* sws_downscale_ctx = nullptr;
	std::deque<c4::MotionDetector::Motion> preprocessed;

	static std::vector<c4::rectangle<int>> downscale_rects(const std::vector<c4::rectangle<int>>& rects, int downscale) {
		std::vector<c4::rectangle<int>> scaled;
		for (const c4::rectangle<int>& r : rects) {
			scaled.emplace_back(r.x / downscale, r.y / downscale, r.w / downscale, r.h / downscale);
		}
		return scaled;
	}

	c4::MotionDetector::Motion detect(AVFrame* src, const AVPixFmtDescriptor *pixdesc) {
		static c4::time_printer tp("VidStabProcessor::detect()", c4::LOG_DEBUG);
        c4::scoped_timer timer(tp);

		c4::VideoStabilization::FramePtr frame = std::make_shared<c4::VideoStabilization::Frame>();

		frame->resize(src->height / downscale, src->width / downscale);
		if (sws_downscale_ctx == nullptr) {
			sws_downscale_ctx = sws_getContext(src->width, src->height, (AVPixelFormat)src->format, frame->width(), frame->height(), AV_PIX_FMT_GRAY8, SWS_AREA, 0, 0, 0);
			ASSERT_TRUE(sws_downscale_ctx != nullptr);
		}
		uint8_t* dst_data[1] = { frame->data() };
		int dst_stride[1] = { frame->stride() };
		int ret = sws_scale(sws_downscale_ctx, src->data, src->linesize, 0, src->height, dst_data, dst_stride);
		ASSERT_EQUAL(ret, frame->height());

		const std::vector<c4::rectangle<int>> scaledIgnoreRects = downscale_rects(ignoreRects, downscale);

		return stabilizer.process(frame, scaledIgnoreRects);
	}

public:
	VidStabProcessor(const c4::VideoStabilization::Params& params, const std::vector<c4::rectangle<int>> ignoreRects, int downscale, double prezoom, int autozoom, double zoomspeed)
		: stabilizer(params), downscale(downscale), ignoreRects(ignoreRects), prezoom(prezoom), autozoom(autozoom), zoomspeed(zoomspeed), zoom(prezoom) {
		ASSERT_GREATER_EQUAL(prezoom, 1.);
		ASSERT_GREATER_EQUAL(zoomspeed, 0.);
	}

	void preprocess(AVFrame* src) override {
		c4::MotionDetector::Motion motion = detect(src, av_pix_fmt_desc_get((AVPixelFormat)src->format));

		ASSERT_EQUAL(autozoom, 2); // Rignt now we only need two-pass decoding for autozoom mode 2
		zoom = std::max(zoom, motion.calc_fill_scale(src->height / downscale, src->width / downscale));

		preprocessed.push_back(motion);
	}

	void process(AVFrame* src) override {
        c4::scoped_timer timer("VidStabProcessor()");

		ASSERT_TRUE(src != nullptr);
		AV_CALL(av_frame_make_writable(src));

		const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get((AVPixelFormat)src->format);
		const int frameHeight = src->height / downscale;
		const int frameWidth = src->width / downscale;

		c4::MotionDetector::Motion motion;
		if (!preprocessed.empty()) {
			motion = preprocessed.front();
			preprocessed.pop_front();
		} else {
			motion = detect(src, pixdesc);

			if (autozoom) {
				ASSERT_EQUAL(autozoom, 1); // In one-pass decoding we only support dynamic zoom
				zoom -= zoomspeed;
				const double requiredZoom = motion.calc_fill_scale(frameHeight, frameWidth);
				zoom = std::max(zoom, requiredZoom);
				zoom = std::max(prezoom, zoom);
			}
		}

		motion.scale *= 1. / zoom;
		motion.shift *= 1. / zoom;

		const int planes = pixdesc->nb_components;
		ASSERT_EQUAL(planes, av_pix_fmt_count_planes((AVPixelFormat)src->format));

        c4::scoped_timer timer2("VidStabProcessor(): apply");

		for (int p = 0; p < planes; p++) {
			const int h = p ? AV_CEIL_RSHIFT(src->height, pixdesc->log2_chroma_h) : src->height;
			const int w = p ? AV_CEIL_RSHIFT(src->width, pixdesc->log2_chroma_w) : src->width;
			c4::MotionDetector::Motion planeSizeAdjustedMotion = motion;
			planeSizeAdjustedMotion.shift.y *= (double)h / frameHeight;
			planeSizeAdjustedMotion.shift.x *= (double)w / frameWidth;

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
		PRINT_DEBUG(zoom);
		sws_freeContext(sws_downscale_ctx);
	}
};

int64_t parse_bitrate(const std::string& bitrate) {
	if (bitrate.empty()) {
		return 0;
	}

	int64_t v = 0;
	try {
		v = std::stoi(bitrate.substr(0, bitrate.size() - 1));
	} catch (const std::exception& e) {
		v = 0;
	}

	if (v <= 0 || std::to_string(v) != bitrate.substr(0, bitrate.size() - 1)) {
		LOGW << "Invalid bitrate string: " << bitrate;
		return 0;
	}

	switch (bitrate.back()) {
	case 'k':
		return v * 1000;
	case 'M':
		return v * 1000000;
	case 'G':
		return v * 1000000000;
	default:
		LOGW << "Invalid bitrate suffix: " << bitrate;
		return 0;
	}

	return 0;
}

int main(int argc, char* argv[]) {
    try{
		c4::Logger::setLogLevel(c4::LOG_INFO);

		c4::scoped_timer timer("main", c4::LOG_DEBUG);

		c4::VideoStabilization::Params params;

		c4::cmd_opts opts;
		auto inputCmdOpt = opts.add_required_free_arg<std::string>("input.mp4");
		auto outputCmdOpt = opts.add_required_free_arg<std::string>("output.mp4");
		auto bitrateCmdOpt = opts.add_optional<std::string>("bitrate", "2", "Target bitrate.");
		auto codecCmdOpt = opts.add_optional<std::string>("codec", "libx265", "Output video codec. Default is libx265. You can use libx264, but you shouldn't. If you have nvidia drivers, you can try hevc_nvenc - it's faster, but has some pixel format limitations.");
		auto downscaleCmdOpt = opts.add_optional<int>("downscale", -1, "Downscale factor used for motion detection. Default value of -1 means automatic (based on resolution).");
		auto prezoomCmdOpt = opts.add_optional<double>("prezoom", 1.0, "Pre-zoom the source this much (used to reduce boarders or dynamic zoom effect).");
		auto autozoomCmdOpt = opts.add_optional<int>("autozoom", 0, "Automatic zooming to fill the resulting frame. 0 - disabled, 1 - dynamic zoom, 2 - static zoom, requires two-pass decoding.");
		auto zoomspeedCmdOpt = opts.add_optional<double>("zoomspeed", 0.0, "Every frame zoom is decreased by this amount if the frame stays filled.");

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
		const std::string outputFilename = outputCmdOpt;
		const int64_t bitrate = parse_bitrate(bitrateCmdOpt);

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

		FfmpegVideoProcessor videoProcessor(inputFilename, outputFilename, bitrate, codecCmdOpt);

		const int downscale = downscaleCmdOpt > 0 ? (int)downscaleCmdOpt : 1 + videoProcessor.get_frame_size().min() / 1000;

		PRINT_DEBUG(downscale);

		VidStabProcessor frameProcessor(params, ignoreRects, downscale, prezoomCmdOpt, autozoomCmdOpt, zoomspeedCmdOpt);

		if (autozoomCmdOpt == 2) {
			videoProcessor.process(frameProcessor, true);
			videoProcessor.init_input();
		}
		videoProcessor.process(frameProcessor, false);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
