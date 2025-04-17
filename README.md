# ffstabilize
Very fast command line video stabilization tool, based on ffmpeg libav for video decoding and encoding.  
[Demo video on YouTube](https://youtu.be/pfKWAo2nPJU)

# Install
## Windows
<b>Option 1</b>. Download and install ffstabilize-\*-win64.msi from the latest release. This will put ffstabilize folder to the system PATH, so you can run ffstabilize from any directory.\
<b>Option 2</b>. Download ffstabilize-\*-win64.zip from the latest release. Then you can extract it wherever you want and not have it in the system PATH. Run ffstabilize.exe from it.\
<b>Option 3</b>. Build from sources. You will need CMake and MSVC. Should be pretty straightforward from there.

## Linux
<b>Option 1</b>. Download ffstabilize-\*-Linux.zip from the latest release. Extract the archive and run ./ffstabilize from the root folder. It's a bash script that sets proper LD_LIBRARY_PATH.\
<b>Option 2</b>. Build from sources. You will need CMake and g++. Should be pretty straightforward from there.

# Usage
The app has --help so you should be able to use it ;) I will explain optional parameters below.

## Basic options
You can run the app by just providing input and output. This will give okay results in many cases. The options bellow might be useful.

<dt><b>--autozoom</b></dt>
Enables automatic zooming (automatic cropping) so that you don't see artifacts on the output video.

<dt><b>--x_smooth</b></dt>
How many frames should be used for smoothing horizontal motion. Bigger this value - smoother motion you will see. Default value is 25. Usually a value equal to the input video fps is a good start.
<dt><b>--y_smooth</b></dt>
Same as x_smooth, but vertical.

<dt><b>--codec</b></dt>
Set the encoder to use for output. libx265 is used by default. You can try hevc_nvenc, if it works - good, it's faster. If not, just stay with the default. You can also use libx264 if you're having troubles opening the resulting video (but it's a better idea to update your software that can't open h265, wth?)

## Advanced options
<dt><b>--autozoom</b></dt>
Enables automatic zooming (automatic cropping) so that you don't see artifacts on the output video.

<dt><b>--x_smooth</b></dt>
How many frames should be used for smoothing horizontal motion. Bigger this value - smoother motion you will see. Default value is 25. Usually a value equal to the input video fps is a good start.
<dt><b>--y_smooth</b></dt>
Same as x_smooth, but vertical.
<dt><b>--alpha_smooth</b></dt>
How many frames should be used for rotation smoothing.
<dt><b>--scale_smooth</b></dt>
How many frames should be used for scale smoothing. The default value is 25. Which is good for most cases. If you can see camera forward-backward movement in the resulting video, you can try increasing it. Decreasing can help reduce cropping if there's a fast forward motion.

<dt><b>--autozoom</b></dt>
Automatic zooming to fill the resulting frame. Two-pass decoding is enabled if autozoom is on.
<dt><b>--zoom_speed</b></dt>
The ratio of zooms of two consequtive frames will not be greater than this value. The value of 1.0 means static zoom. The deafault value of 1.0002 gives smooth, almost invisible zooming.
<dt><b>--prezoom</b></dt>
Pre-zoom the source this much.

<dt><b>--codec</b></dt>
Output video codec. Default is libx265. You can use libx264, but you shouldn't. If you have nvidia drivers, you can try hevc_nvenc - it's faster, but has some pixel format limitations.
<dt><b>--bitrate</b></dt>
Target bitrate.

<dt><b>--debug</b></dt>
Enable debug output.
<dt><b>--verbose</b></dt>
Enable verbose output. Can only benefit you if you're looking at the source code.
<dt><b>--debug_imprint</b></dt>
Enable motion info imprint on the output video.

<dt><b>--downscale</b></dt>
Downscale factor used for motion detection. Default value of -1 means automatic (based on resolution). Can only be integer values. In most cases you can leave it on automatic.

<dt><b>--ignore</b></dt>
Add rectangle where motion should be ignored. It can be very useful when there's a moving subject in the frame and it's movemnt should not impact stabilization. Format: "x, y, w, h". You can pass multiple of these e.g. --ignore "0, 0, 100, 100" --ignore "1820, 0, 100, 100" - this will exclude two top corners on a FullHD video.

<dt><b>--block_size</b></dt>
Block size in pixels (after downscale). This is used for local motion detection. Possible values are 16, 32 (default), 48, 64. As a rule of thumb, bigger values are better at detecting shifts, while smaller values are better at detecting rotation and scaling. Also smaller values are faster.
<dt><b>--max_shift</b></dt>
Max shift in pixels (after downscale), should be <= block_size / 2. Smaller values are faster, larger values alow detection of faster movements. The default value is 16.

<dt><b>--max_alpha</b></dt>
Max rotation angle of consecutive frames, in radians. The default value is 0.003. In most cases you should either not touch it or set it to zero, if you know that there's no accidental camera rotations.
<dt><b>--max_scale</b></dt>
Max scale ratio of consecutive frames (1 / max_scale if we scale down). The default value is 1.05. If you know there's no zooming in and out to compensate, you can set it to 1. If the forward movement is very fast, you can try increasing it.

<dt><b>--scene_cut_threshold</b></dt>
Motion detection confidence threshold for scene cut detection. The default value is 0.1. If algorithm is missing some cuts - decrease it, if it cutsa where it shouldn't - increase. Information about detected cuts can be found using --debug option.

# Examples
### Original (source) video
You can download the source video here (Google Drive):  [wwimf_1st_scene_src_4k.mp4](https://drive.google.com/file/d/1urXm6aUY-B69dK8MhdI7AmU_VYO4-iv_/view?usp=drive_link)  
### ffstabilize
ffstabilize default params
```
ffstabilize wwimf_1st_scene_src_4k.mp4 wwimf_1st_scene_ffstab-0.1.1.mp4 --codec hevc_nvenc
```
Download here:  [wwimf_1st_scene_ffstab-0.1.1.mp4](https://drive.google.com/file/d/17McaWfDbe05WwfXA8GJhSZCX0ci7vLGL/view?usp=drive_link)  
<br/>
<br/>
ffstabilize with autozoom
```
ffstabilize wwimf_1st_scene_src_4k.mp4 wwimf_1st_scene_ffstab-0.1.1-az.mp4 --codec hevc_nvenc --autozoom
```
Download here:  [wwimf_1st_scene_ffstab-0.1.1-az.mp4](https://drive.google.com/file/d/1yqO4IZ1cMrdzbYWqR0RoT5ottHHA1gYH/view?usp=drive_link)  
<br/>
<br/>
ffstabilize with autozoom and bigger smooth params
```
ffstabilize wwimf_1st_scene_src_4k.mp4 wwimf_1st_scene_ffstab-0.1.1-az-sm50.mp4 --codec hevc_nvenc --autozoom --x_smooth 50 --y_smooth 50
```
Download here:  [wwimf_1st_scene_ffstab-0.1.1-az-sm50.mp4](https://drive.google.com/file/d/1i59Zw2kqfWqnDjGSYIxvDfG_GUu3owwn/view?usp=drive_link)  
<br/>
### Adobe Premiere Pro
Method: Position, Scale, Rotation: [wwimf_1st_scene_app_psr.mp4](https://drive.google.com/file/d/10EU7Ox8h4rjsbUkqz4LBE58NuWFW42-k/view?usp=drive_link)  
Method: Subspace Warp: [wwimf_1st_scene_app_sw.mp4](https://drive.google.com/file/d/1-SLFFxw1Roj88uPZC-KBtByYpkFreoCq/view?usp=drive_link)  
<br/>
### Processing times
ffstabilize processing time was 28s without autozoom, and 36s with autozoom.  
Adobe Premiere Pro analyzing time is 1:21, and encoding time is 0:19, obviously UI manipulation time is not included.  
<br/>
### Compare on YouTube
Original + ffstabilize (--autozoom --x_smooth 50 --y_smooth 50) + adobe position scale rotation + adobe subspace warp: [YouTube Video](https://youtu.be/UwWBu9h01XQ)
