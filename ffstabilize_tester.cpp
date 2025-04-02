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

#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <filesystem>

int test(const std::string& exe, const std::string& fin) {
	const std::string fout = "tmp.mp4";
	std::string cmd = exe + " " + fin + " " + fout;

	int ret = std::system(cmd.c_str());
	if(ret == 0) {
		auto s1 = std::filesystem::file_size(fin);
		auto s2 = std::filesystem::file_size(fout);

		if (s1 > 2 * s2 || s2 > 2 * s1) {
			ret = -1;
		}
	}

	if (std::remove(fout.c_str())) {
		ret = -1;
	}

	return ret;
}

int main(int argc, char* argv[]) {
	const std::string path = std::filesystem::path(argv[0]).parent_path().string();
	const std::string exe = path + "/ffstabilize";

	const std::vector<std::string> files {
		"h246_720p_60fps.mp4",
		"h264_4k_30fps.mp4",
		"h264_1080p_30fps_a.mp4",
		"hevc_4k_30fps_10bit.mp4",
		"hevc_4k_120fps_10bit.mp4",
		"hevc_8k_30fps_10bit.mp4",
		"hevc_8k_30fps_10bit_422.mp4",
		"hevc_8k_30fps_10bit_444.mp4",
		"hevc_720p_60fps_10bit.mp4",
		"hevc_720p_60fps_10bit_422.mp4",
		"hevc_720p_60fps_10bit_444.mp4",
		"hevc_1080p_30fps_10bit_444_a.mp4"
	};

	for (const auto& file : files) {
		if (test(exe, "../test_data/" + file)) {
			std::cerr << "Test failed for " << file << std::endl;
			return -1;
		}
	}

	return 0;
}
