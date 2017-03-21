// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef CSIFT_IMAGE_DESCRIBER_HPP
#define CSIFT_IMAGE_DESCRIBER_HPP

#include <tuple>
#include <vector>
#include "openMVG/image/image.hpp"
#include "openMVG/features/image_describer.hpp"
#include "openMVG/features/regions_factory.hpp"
#include "openMVG/features/feature.hpp"
#include "third_party/cudasift/cudaImage.h"
#include "third_party/cudasift/cudaSift.h"
#include "openMVG/features/cudasift/csift_keypoint.hpp"
#include <iostream>
#include <numeric>
#include <cereal/cereal.hpp>
#include <Eigen/Core>

namespace openMVG {
namespace features {

class CSIFT_Image_describer : public Image_describer
{
public:
    struct Params
    {
        Params(
        float initBlur = 1.0f,
        float thresh = 3.5f,
		int numOctaves = 5
        ):
        initBlur_(initBlur),
        thresh_(thresh) {}

        template<class Archive>
        void serialize(Archive & ar)
        {
            ar(
                cereal::make_nvp("initBlur", initBlur_),
                cereal::make_nvp("thresh", thresh_));

        }
        float initBlur_;
        float thresh_;
		int numOctaves_;
    };

    CSIFT_Image_describer
    (
        const Params params = Params()
    )
    :Image_describer(), params_(params)
    {}

    bool Set_configuration_preset(EDESCRIBER_PRESET preset) override
    {
        switch(preset)
        {
          case NORMAL_PRESET:
            params_.thresh_ = 2.5f;
          break;
          case HIGH_PRESET:
            params_.thresh_ = 3.5f;
          break;
          case ULTRA_PRESET:
            params_.thresh_ = 4.00f;
          break;
          default:
            return false;
        }
        return true;
    }


    bool Describe(const image::Image<unsigned char>& image,
    std::unique_ptr<Regions> &regions,
    const image::Image<unsigned char> * mask = nullptr) override
    {
        // Initialize GPU, allocate memory for image
        InitCuda(0);
        CudaImage img1;
        const int w = image.Width(), h = image.Height();

        // Convert to float 
        const image::Image<float> If(image.GetMat().cast<float>());
        img1.Allocate(w, h, iAlignUp(w, 128), false, NULL, (float*)If.data());
        img1.Download();
        Allocate(regions);
        SIFT_Regions * regionsCasted = dynamic_cast<SIFT_Regions*>(regions.get());
        {
            SiftData siftData1;
            InitSiftData(siftData1, 32768, true, true);
			ExtractSift(siftData1, img1, params_.numOctaves_, params_.initBlur_, params_.thresh_, 0.0f, false);
            int numPts1 = siftData1.numPts;
			std::cout << "Number of features detected: " << numPts1 << std::endl;

            SiftPoint *sift1 = siftData1.h_data;
            std::vector< csift::Keypoint > keys;
            Descriptor<unsigned char, 128> descriptor;
			// Load keypoint and descriptor data from result, convert descriptor array into Eigen matrix
            for (int i = 0; i < numPts1 ; i++)
            {
                csift::Keypoint k;
                k.x = sift1[i].xpos;
                k.y = sift1[i].ypos;
                k.sigma = sift1[i].scale;
                k.theta = sift1[i].orientation;
                k.descr = Eigen::Map<Eigen::Matrix<float,128,1>> (sift1[i].data);
                keys.push_back(k);
            }
            std::vector< csift::Keypoint > keypoints;
            std::move(keys.begin(), keys.end(), std::back_inserter(keypoints));

            for (const auto & k : keypoints)
            {
                Descriptor<unsigned char, 128> descriptor;
                descriptor << (k.descr.cast<unsigned char>());
                {
					// Convert SIFT descriptor to OpenMVG format
					for (unsigned int ctr = 0 ; ctr < 128 ; ++ctr)
					{
	                    descriptor[ctr] = static_cast<unsigned char>(512.f*k.descr[ctr]);
					}
					regionsCasted->Descriptors().emplace_back(descriptor);
					regionsCasted->Features().emplace_back(k.x, k.y, k.sigma, k.theta);
				}
			}
        }
        return true;
    }

    void Allocate(std::unique_ptr<Regions> &regions) const override
    {
        regions.reset( new SIFT_Regions );
    }

    template<class Archive>
    void serialize( Archive & ar )
    {
        ar(cereal::make_nvp("params", params_));
    }

private:
    Params params_;
};

}
}

#include <cereal/types/polymorphic.hpp>
#include <cereal/archives/json.hpp>
CEREAL_REGISTER_TYPE_WITH_NAME(openMVG::features::CSIFT_Image_describer, "CSIFT_Image_describer");
CEREAL_REGISTER_POLYMORPHIC_RELATION(openMVG::features::Image_describer, openMVG::features::CSIFT_Image_describer)
#endif // OPENMVG_FEATURES_SIFT_SIFT_ANATOMY_IMAGE_DESCRIBER_HPP
