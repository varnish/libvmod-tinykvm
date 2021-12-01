#include <stdio.h>
#include <opencv2/opencv.hpp>
#include "facedetectcnn.h"
#include "../tenant.hpp"
#include "../machine_instance.hpp"
#include "../varnish.hpp"
#define DETECT_BUFFER_SIZE 0x20000
using namespace cv;

namespace kvm {

void initialize_facedetect(VRT_CTX, VCL_PRIV task)
{
	(void) ctx;

	TenantConfig::set_dynamic_call(task, "facedetect.image",
	[=] (MachineInstance& inst)
	{
		auto regs = inst.machine().registers();
		const auto bufreg  = regs.rdi;
		const auto bufsize = regs.rsi;
		std::vector<uint8_t> bufdata(bufsize);
		inst.machine().copy_from_guest(bufdata.data(), bufreg, bufsize);

		Mat image = imdecode(bufdata, cv::IMREAD_COLOR);
		if (image.empty())
		{
			fprintf(stderr, "Facedetect could not load image\n");
			regs.rax = -1;
			inst.machine().set_registers(regs);
			return;
		}

		auto pBuffer = std::make_unique<uint8_t[]> (DETECT_BUFFER_SIZE);

		///////////////////////////////////////////
		// CNN face detection
		// Best detection rate
		//////////////////////////////////////////
		TickMeter cvtm;
		cvtm.start();

		int* pResults = facedetect_cnn(pBuffer.get(),
			(uint8_t*)(image.ptr(0)), image.cols, image.rows, (int)image.step);

		cvtm.stop();
		printf("time = %gms\n", cvtm.getTimeMilli());

		printf("%d faces detected.\n", (pResults ? *pResults : 0));
		Mat& result_image = image;

		for (int i = 0; i < (pResults ? *pResults : 0); i++)
		{
			const short * p = ((short*)(pResults+1))+142*i;
			const int confidence = p[0];
			const int x = p[1];
			const int y = p[2];
			const int w = p[3];
			const int h = p[4];
			// skip shit-ass confidences
			if (confidence < 50) continue;

			char sScore[64];
			snprintf(sScore, sizeof(sScore), "%d", confidence);
			cv::putText(result_image, sScore, cv::Point(x, y-3), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
			// draw face rectangle
			rectangle(result_image, Rect(x, y, w, h), Scalar(0, 255, 0), 2);
			// draw five face landmarks in different colors
			cv::circle(result_image, cv::Point(p[5], p[5 + 1]), 1, cv::Scalar(255, 0, 0), 2);
			cv::circle(result_image, cv::Point(p[5 + 2], p[5 + 3]), 1, cv::Scalar(0, 0, 255), 2);
			cv::circle(result_image, cv::Point(p[5 + 4], p[5 + 5]), 1, cv::Scalar(0, 255, 0), 2);
			cv::circle(result_image, cv::Point(p[5 + 6], p[5 + 7]), 1, cv::Scalar(255, 0, 255), 2);
			cv::circle(result_image, cv::Point(p[5 + 8], p[5 + 9]), 1, cv::Scalar(0, 255, 255), 2);

			printf("face %d: confidence=%d, [%d, %d, %d, %d] (%d,%d) (%d,%d) (%d,%d) (%d,%d) (%d,%d)\n",
					i, confidence, x, y, w, h,
					p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13],p[14]);
		}

		vector<uint8_t> output;
		imencode(".jpg", result_image, output);

		struct {
			uint64_t gdata;
			size_t   glen;
		} gres;
		gres.gdata = inst.machine().mmap_allocate(output.size());
		gres.glen  = output.size();
		inst.machine().copy_to_guest(gres.gdata, output.data(), gres.glen);
		inst.machine().copy_to_guest(regs.rdx, &gres, sizeof(gres));
		regs.rax = 0;
		inst.machine().set_registers(regs);
	});
}

}
