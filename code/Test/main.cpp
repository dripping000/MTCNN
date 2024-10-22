#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "mtcnn.h"
#include "browse.h"
#define USE_SHELL_OPEN
#ifndef  nullptr
#define nullptr 0
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h> 
#else
#include <unistd.h>
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
//ref:https://github.com/nothings/stb/blob/master/stb_image.h
#define TJE_IMPLEMENTATION

#include "tiny_jpeg.h"
//ref:https://github.com/serge-rgb/TinyJPEG/blob/master/tiny_jpeg.h

#include <stdint.h>
#include "timing.h"

#ifndef _MAX_DRIVE
#define _MAX_DRIVE 3
#endif
#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif
#ifndef _MAX_EXT
#define _MAX_EXT 256
#endif
#ifndef _MAX_DIR
#define _MAX_DIR 256
#endif

char saveFile[1024];

unsigned char *loadImage(const char *filename, int *Width, int *Height, int *Channels) {
	return stbi_load(filename, Width, Height, Channels, 0);
}

void saveImage(const char *filename, int Width, int Height, int Channels, unsigned char *Output) {
	memcpy(saveFile + strlen(saveFile), filename, strlen(filename));
	*(saveFile + strlen(saveFile) + 1) = 0;
	//保存为jpg
	if (!tje_encode_to_file(saveFile, Width, Height, Channels, true, Output)) {
		fprintf(stderr, "save JPEG fail.\n");
		return;
	}

#ifdef USE_SHELL_OPEN
	browse(saveFile);
#endif
}

void splitpath(const char *path, char *drv, char *dir, char *name, char *ext) {
	const char *end;
	const char *p;
	const char *s;
	if (path[0] && path[1] == ':') {
		if (drv) {
			*drv++ = *path++;
			*drv++ = *path++;
			*drv = '\0';
		}
	}
	else if (drv)
		*drv = '\0';
	for (end = path; *end && *end != ':';)
		end++;
	for (p = end; p > path && *--p != '\\' && *p != '/';)
		if (*p == '.') {
			end = p;
			break;
		}
	if (ext)
		for (s = end; (*ext = *s++);)
			ext++;
	for (p = end; p > path;)
		if (*--p == '\\' || *p == '/') {
			p++;
			break;
		}
	if (name) {
		for (s = p; s < end;)
			*name++ = *s++;
		*name = '\0';
	}
	if (dir) {
		for (s = path; s < p;)
			*dir++ = *s++;
		*dir = '\0';
	}
}

void getCurrentFilePath(const char *filePath, char *saveFile) {
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];
	splitpath(filePath, drive, dir, fname, ext);
	size_t n = strlen(filePath);
	memcpy(saveFile, filePath, n);
	char *cur_saveFile = saveFile + (n - strlen(ext));
	cur_saveFile[0] = '_';
	cur_saveFile[1] = 0;
}

void drawPoint(unsigned char *bits, int width, int depth, int x, int y, const uint8_t *color) {
	for (int i = 0; i < min(depth, 3); ++i) {
		bits[(y * width + x) * depth + i] = color[i];
	}
}

void drawLine(unsigned char *bits, int width, int depth, int startX, int startY, int endX, int endY,
	const uint8_t *col) {
	if (endX == startX) {
		if (startY > endY) {
			int a = startY;
			startY = endY;
			endY = a;
		}
		for (int y = startY; y <= endY; y++) {
			drawPoint(bits, width, depth, startX, y, col);
		}
	}
	else {
		float m = 1.0f * (endY - startY) / (endX - startX);
		int y = 0;
		if (startX > endX) {
			int a = startX;
			startX = endX;
			endX = a;
		}
		for (int x = startX; x <= endX; x++) {
			y = (int)(m * (x - startX) + startY);
			drawPoint(bits, width, depth, x, y, col);
		}
	}
}

void drawRectangle(unsigned char *bits, int width, int depth, int x1, int y1, int x2, int y2, const uint8_t *col) {
	drawLine(bits, width, depth, x1, y1, x2, y1, col);
	drawLine(bits, width, depth, x2, y1, x2, y2, col);
	drawLine(bits, width, depth, x2, y2, x1, y2, col);
	drawLine(bits, width, depth, x1, y2, x1, y1, col);
}

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a): (b))
#endif
#ifndef MIN
#define MIN(a, b) (((a) > (b)) ? (b): (a))
#endif

unsigned char ClampToByte(int Value) {
    return ((Value | ((signed int) (255 - Value) >> 31)) & ~((signed int) Value >> 31));
}

int Clamp(int Value, int Min, int Max) {
    if (Value < Min)
        return Min;
    else if (Value > Max)
        return Max;
    else
        return Value;
}

void RemoveRedEyes(unsigned char *input, unsigned char *output, int width, int height, int depth, int CenterX, int CenterY,
              int Radius) {
    if (depth < 3) return;
    if ((input == nullptr) || (output == nullptr)) return;
    if ((width <= 0) || (height <= 0)) return;

    int Left = Clamp(CenterX - Radius, 0, width);
    int Top = Clamp(CenterY - Radius, 0, height);
    int Right = Clamp(CenterX + Radius, 0, width);
    int Bottom = Clamp(CenterY + Radius, 0, height);
    int PowRadius = Radius * Radius;

    for (int Y = Top; Y < Bottom; Y++) {
        unsigned char *in_scanline = input + Y * width * depth + Left * depth;
        unsigned char *out_scanline = output + Y * width * depth + Left * depth;
        int OffsetY = Y - CenterY;
        for (int X = Left; X < Right; X++) {
            int OffsetX = X - CenterX;
            int dis = OffsetX * OffsetX + OffsetY * OffsetY;
            if (dis <= PowRadius) {
                float bluf = 0;
                int Red = in_scanline[0];
                int Green = in_scanline[1];
                int Blue = in_scanline[2];
                int nrv = Blue + Green;
                if (nrv < 1) nrv = 1;
                if (Green > 1)
					bluf = (float) Blue / Green;
                else
                    bluf = (float) Blue;
                bluf = MAX(0.5f, MIN(1.5f, sqrt(bluf)));
                float redq = (float) Red / nrv * bluf;
                if (redq > 0.7f) {
                    float powr = 1.775f - (redq * 0.75f +
                                           0.25f);
                    if (powr < 0) powr = 0;
                    powr = powr * powr;
                    float powb = 0.5f + powr * 0.5f;
                    float powg = 0.75f + powr * 0.25f;
                    out_scanline[0] = ClampToByte(powr * Red + 0.5f);
                    out_scanline[1] = ClampToByte(powg * Green + 0.5f);
                    out_scanline[2] = ClampToByte(powb * Blue + 0.5f);
                }
            }
            in_scanline += depth;
            out_scanline += depth;
        }
    }
}

void RotateBilinear(unsigned char *sourceData, int width, int height, int Channels, int RowBytes,
                    unsigned char *destinationData, int newWidth, int newHeight, float angle, bool keepSize = true,
                    int fillColorR = 255, int fillColorG = 255, int fillColorB = 255) {
    if (sourceData == NULL || destinationData == NULL) return;

    float oldXradius = (float) (width - 1) / 2;
    float oldYradius = (float) (height - 1) / 2;

    float newXradius = (float) (newWidth - 1) / 2;
    float newYradius = (float) (newHeight - 1) / 2;

    double MPI = 3.14159265358979323846;
    double angleRad = -angle * MPI / 180.0;
    float angleCos = (float) cos(angleRad);
    float angleSin = (float) sin(angleRad);

    int srcStride = RowBytes;
    int dstOffset = newWidth * Channels - ((Channels == 1) ? newWidth : newWidth * Channels);

    unsigned char fillR = fillColorR;
    unsigned char fillG = fillColorG;
    unsigned char fillB = fillColorB;

    unsigned char *src = (unsigned char *) sourceData;
    unsigned char *dst = (unsigned char *) destinationData;

    int ymax = height - 1;
    int xmax = width - 1;
    if (Channels == 1) {
        float cy = -newYradius;
        for (int y = 0; y < newHeight; y++) {
            float tx = angleSin * cy + oldXradius;
            float ty = angleCos * cy + oldYradius;

            float cx = -newXradius;
            for (int x = 0; x < newWidth; x++, dst++) {
                float ox = tx + angleCos * cx;
                float oy = ty - angleSin * cx;

                int ox1 = (int) ox;
                int oy1 = (int) oy;

                if ((ox1 < 0) || (oy1 < 0) || (ox1 >= width) || (oy1 >= height)) {
                    *dst = fillG;
                } else {
                    int ox2 = (ox1 == xmax) ? ox1 : ox1 + 1;
                    int oy2 = (oy1 == ymax) ? oy1 : oy1 + 1;
                    float dx1 = 0;
                    if ((dx1 = ox - (float) ox1) < 0)
                        dx1 = 0;
                    float dx2 = 1.0f - dx1;
                    float dy1 = 0;
                    if ((dy1 = oy - (float) oy1) < 0)
                        dy1 = 0;
                    float dy2 = 1.0f - dy1;

                    unsigned char *p1 = src + oy1 * srcStride;
                    unsigned char *p2 = src + oy2 * srcStride;

                    *dst = (unsigned char) (dy2 * (dx2 * p1[ox1] + dx1 * p1[ox2]) +
                                            dy1 * (dx2 * p2[ox1] + dx1 * p2[ox2]));
                }
                cx++;
            }
            cy++;
            dst += dstOffset;
        }
    } else if (Channels == 3) {
        float cy = -newYradius;
        for (int y = 0; y < newHeight; y++) {
            float tx = angleSin * cy + oldXradius;
            float ty = angleCos * cy + oldYradius;

            float cx = -newXradius;
            for (int x = 0; x < newWidth; x++, dst += Channels) {
                float ox = tx + angleCos * cx;
                float oy = ty - angleSin * cx;

                int ox1 = (int) ox;
                int oy1 = (int) oy;

                if ((ox1 < 0) || (oy1 < 0) || (ox1 >= width) || (oy1 >= height)) {
                    dst[0] = fillR;
                    dst[1] = fillG;
                    dst[2] = fillB;
                } else {
                    int ox2 = (ox1 == xmax) ? ox1 : ox1 + 1;
                    int oy2 = (oy1 == ymax) ? oy1 : oy1 + 1;

                    float dx1 = 0;
                    if ((dx1 = ox - (float) ox1) < 0)
                        dx1 = 0;
                    float dx2 = 1.0f - dx1;
                    float dy1 = 0;
                    if ((dy1 = oy - (float) oy1) < 0)
                        dy1 = 0;
                    float dy2 = 1.0f - dy1;

                    unsigned char *p1 = src + oy1 * srcStride;
                    unsigned char *p2 = p1;
                    p1 += ox1 * Channels;
                    p2 += ox2 * Channels;

                    unsigned char *p3 = src + oy2 * srcStride;
                    unsigned char *p4 = p3;
                    p3 += ox1 * Channels;
                    p4 += ox2 * Channels;

                    dst[0] = (unsigned char) (
                            dy2 * (dx2 * p1[0] + dx1 * p2[0]) +
                            dy1 * (dx2 * p3[0] + dx1 * p4[0]));

                    dst[1] = (unsigned char) (
                            dy2 * (dx2 * p1[1] + dx1 * p2[1]) +
                            dy1 * (dx2 * p3[1] + dx1 * p4[1]));

                    dst[2] = (unsigned char) (
                            dy2 * (dx2 * p1[2] + dx1 * p2[2]) +
                            dy1 * (dx2 * p3[2] + dx1 * p4[2]));
                }
                cx++;
            }
            cy++;
            dst += dstOffset;
        }
    } else if (Channels == 4) {
        float cy = -newYradius;
        for (int y = 0; y < newHeight; y++) {
            float tx = angleSin * cy + oldXradius;
            float ty = angleCos * cy + oldYradius;

            float cx = -newXradius;
            for (int x = 0; x < newWidth; x++, dst += Channels) {
                float ox = tx + angleCos * cx;
                float oy = ty - angleSin * cx;

                int ox1 = (int) ox;
                int oy1 = (int) oy;

                if ((ox1 < 0) || (oy1 < 0) || (ox1 >= width) || (oy1 >= height)) {
                    dst[0] = fillR;
                    dst[1] = fillG;
                    dst[2] = fillB;
                    dst[3] = 255;
                } else {
                    int ox2 = (ox1 == xmax) ? ox1 : ox1 + 1;
                    int oy2 = (oy1 == ymax) ? oy1 : oy1 + 1;

                    float dx1 = 0;
                    if ((dx1 = ox - (float) ox1) < 0)
                        dx1 = 0;
                    float dx2 = 1.0f - dx1;
                    float dy1 = 0;
                    if ((dy1 = oy - (float) oy1) < 0)
                        dy1 = 0;
                    float dy2 = 1.0f - dy1;

                    unsigned char *p1 = src + oy1 * srcStride;
                    unsigned char *p2 = p1;
                    p1 += ox1 * Channels;
                    p2 += ox2 * Channels;

                    unsigned char *p3 = src + oy2 * srcStride;
                    unsigned char *p4 = p3;
                    p3 += ox1 * Channels;
                    p4 += ox2 * Channels;

                    dst[0] = (unsigned char) (
                            dy2 * (dx2 * p1[0] + dx1 * p2[0]) +
                            dy1 * (dx2 * p3[0] + dx1 * p4[0]));

                    dst[1] = (unsigned char) (
                            dy2 * (dx2 * p1[1] + dx1 * p2[1]) +
                            dy1 * (dx2 * p3[1] + dx1 * p4[1]));

                    dst[2] = (unsigned char) (
                            dy2 * (dx2 * p1[2] + dx1 * p2[2]) +
                            dy1 * (dx2 * p3[2] + dx1 * p4[2]));
                    dst[3] = 255;
                }
                cx++;
            }
            cy++;
            dst += dstOffset;
        }
    }
}

void facialPoseCorrection(unsigned char *inputImage, int Width, int Height, int Channels, int left_eye_x,
                          int left_eye_y,
                          int right_eye_x, int right_eye_y) {
    float diffEyeX = right_eye_x - left_eye_x;
    float diffEyeY = right_eye_y - left_eye_y;

    float fAngle;
    float pi = 3.1415926535897932384626433832795f;
    if (fabs(diffEyeX) < 0.0000001f)
        fAngle = 0.f;
    else
        fAngle = atanf(diffEyeY / diffEyeX) * 180.0f / pi;
    size_t numberOfPixels = Width * Height * Channels * sizeof(unsigned char);
    unsigned char *outputImage = (unsigned char *) malloc(numberOfPixels);
    if (outputImage != nullptr) {
        RotateBilinear(inputImage, Width, Height, Channels, Width * Channels, outputImage, Width, Height, fAngle);
        memcpy(inputImage, outputImage, numberOfPixels);
        free(outputImage);
    }
}

int main(int argc, char **argv) {
	printf("mtcnn face detection\n");
	printf("blog:http://cpuimage.cnblogs.com/\n");

	//if (argc < 2) {
	//	printf("usage: %s  model_path image_file \n ", argv[0]);
	//	printf("eg: %s  ../models ../sample.jpg \n ", argv[0]);
	//	printf("press any key to exit. \n");
	//	getchar();
	//	return 0;
	//}
	//const char *model_path = argv[1];
	//char *szfile = argv[2];
    const char *model_path = "./models";
    char *szfile = "./Resource/sample.jpg";

	getCurrentFilePath(szfile, saveFile);

	int Width = 0;
	int Height = 0;
	int Channels = 0;
	unsigned char *inputImage = loadImage(szfile, &Width, &Height, &Channels);
	if (inputImage == nullptr || Channels != 3) return -1;
	ncnn::Mat ncnn_img = ncnn::Mat::from_pixels(inputImage, ncnn::Mat::PIXEL_RGB, Width, Height);

	std::vector<Bbox> finalBbox;
	MTCNN mtcnn(model_path);
	double startTime = now();
	mtcnn.detect(ncnn_img, finalBbox);
	double nDetectTime = calcElapsed(startTime, now());
	printf("time: %d ms.\n ", (int)(nDetectTime * 1000));

	int num_box = finalBbox.size();
	printf("face num: %u \n", num_box);
    bool draw_face_feat = true;
    int left_eye_x = 0;
    int left_eye_y = 0;
    int right_eye_x = 0;
    int right_eye_y = 0;
    for (int i = 0; i < num_box; i++) {
        if (draw_face_feat) {
            const uint8_t red[3] = {255, 0, 0};
            drawRectangle(inputImage, Width, Channels, finalBbox[i].x1, finalBbox[i].y1,
                          finalBbox[i].x2,
                          finalBbox[i].y2, red);

            const uint8_t blue[3] = {0, 0, 255};
            for (int num = 0; num < 5; num++) {
                drawPoint(inputImage, Width, Channels, (int) (finalBbox[i].ppoint[num] + 0.5f),
                          (int) (finalBbox[i].ppoint[num + 5] + 0.5f), blue);
            }
        }
        left_eye_x = (int) (finalBbox[i].ppoint[0] + 0.5f);
        left_eye_y = (int) (finalBbox[i].ppoint[5] + 0.5f);
        right_eye_x = (int) (finalBbox[i].ppoint[1] + 0.5f);
        right_eye_y = (int) (finalBbox[i].ppoint[6] + 0.5f);
        int dis_eye = (int) sqrtf((right_eye_x - left_eye_x) * (right_eye_x - left_eye_x) +
                                  (right_eye_y - left_eye_y) * (right_eye_y - left_eye_y));
        int radius = MAX(1, dis_eye / 9);
        RemoveRedEyes(inputImage, inputImage, Width, Height, Channels, left_eye_x, left_eye_y, radius);
        RemoveRedEyes(inputImage, inputImage, Width, Height, Channels, right_eye_x, right_eye_y, radius);
    }
    //facialPoseCorrection(inputImage, Width, Height, Channels, left_eye_x, left_eye_y, right_eye_x, right_eye_y);
    saveImage("_done.jpg", Width, Height, Channels, inputImage);
    free(inputImage);
    printf("press any key to exit. \n");
    getchar();


#if 1
    cv::VideoCapture capture(1);
    capture.set(CV_CAP_PROP_FRAME_WIDTH, 640);
    capture.set(CV_CAP_PROP_FRAME_HEIGHT, 480);
    if (!capture.isOpened()) return -1;
    cv::Mat frame;

    Width = 640;
    Height = 480;

    while (capture.read(frame)) {     
        int q = cv::waitKey(10);
        if (q == 27) break;

        double t1 = (double)cv::getTickCount();
        ncnn_img = ncnn::Mat::from_pixels(frame.data, ncnn::Mat::PIXEL_BGR2RGB, frame.cols, frame.rows);
        std::vector<Bbox> finalBbox_;
        mtcnn.detect(ncnn_img, finalBbox_);
        printf("total %gms\n", ((double)cv::getTickCount() - t1) * 1000 / cv::getTickFrequency());
        printf("------------------\n");

        size_t num_box = finalBbox_.size();
        printf("face num: %d \n", (int)num_box);

#if 1
        for (int i = 0; i < num_box; i++) {
            if (draw_face_feat) {
                const uint8_t red[3] = { 255, 0, 0 };
                drawRectangle(frame.data, Width, Channels, finalBbox_[i].x1, finalBbox_[i].y1,
                    finalBbox_[i].x2,
                    finalBbox_[i].y2, red);

                const uint8_t blue[3] = { 0, 0, 255 };
                for (int num = 0; num < 5; num++) {
                    drawPoint(frame.data, Width, Channels, lround(finalBbox_[i].ppoint[num]),
                        lround(finalBbox_[i].ppoint[num + 5]), blue);
                }
            }
            left_eye_x = lround(finalBbox_[i].ppoint[0]);
            left_eye_y = lround(finalBbox_[i].ppoint[5]);
            right_eye_x = lround(finalBbox_[i].ppoint[1]);
            right_eye_y = lround(finalBbox_[i].ppoint[6]);
            int dis_eye = (int)sqrtf((right_eye_x - left_eye_x) * (right_eye_x - left_eye_x) +
                (right_eye_y - left_eye_y) * (right_eye_y - left_eye_y));
            int radius = MAX(1, dis_eye / 9);
            RemoveRedEyes(frame.data, frame.data, Width, Height, Channels, left_eye_x, left_eye_y, radius);
            RemoveRedEyes(frame.data, frame.data, Width, Height, Channels, right_eye_x, right_eye_y, radius);
        }
        facialPoseCorrection(frame.data, Width, Height, Channels, left_eye_x, left_eye_y, right_eye_x, right_eye_y);
#endif

        imshow("frame", frame);
    }
    capture.release();
    cv::destroyAllWindows();
#endif

    return 0;
}