/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "CropLayer.h"
#include "paddle/utils/Stat.h"

namespace paddle {

REGISTER_LAYER(crop, CropLayer);

bool CropLayer::init(const LayerMap& layerMap,
                     const ParameterMap& parameterMap) {
  /* Initialize the basic parent class */
  Layer::init(layerMap, parameterMap);

  auto& crop_conf = config_.inputs(0).crop_conf();
  crop_axis_ = crop_conf.axis();
  for (int i = 0; i < crop_conf.offset_size(); i++) {
    crop_offsets_[i] = crop_conf.offset(i);
  }

  // 1. get input_0 shape
  auto& input0_img_conf = config_.inputs(0).image_conf();
  inDims_ = TensorShape({0,
                         input0_img_conf.channels(),
                         input0_img_conf.has_img_size_y()
                             ? input0_img_conf.img_size_y()
                             : input0_img_conf.img_size(),
                         input0_img_conf.img_size()});

  // 2. get output shape from input_1 or crop shap conf
  if (config_.inputs_size() == 2) {
    auto& input1_img_conf = config_.inputs(1).image_conf();
    targetDims_ = TensorShape({0,
                               input1_img_conf.channels(),
                               input1_img_conf.has_img_size_y()
                                   ? input1_img_conf.img_size_y()
                                   : input1_img_conf.img_size(),
                               input1_img_conf.img_size()});
  } else {
    targetDims_ = TensorShape({crop_conf.shape(0),
                               crop_conf.shape(1),
                               crop_conf.shape(2),
                               crop_conf.shape(3)});
  }

  // 3. get final crop shape
  int dimSize = 4;
  for (int i = 0; i < dimSize; i++) {
    if (i >= crop_axis_) {
      crop_shape_[i] = targetDims_[i];
    } else {
      crop_shape_[i] = inDims_[i];
    }
  }

  // 4. get final crop corner
  crop_corner_ = {0, 0, 0, 0};
  for (int i = 0; i < dimSize; i++) {
    if (i >= crop_axis_) {
      if (crop_offsets_.size() > 1) {
        crop_corner_[i] = crop_offsets_[i - crop_axis_];
      } else {
        crop_corner_[i] = crop_offsets_[0];
      }
    }
  }

  outDims_ = TensorShape(4);
  setOutDims(0);

  createFunction(forward_,
                 "Crop",
                 FuncConfig()
                     .set("crop_corner", crop_corner_)
                     .set("crop_shape", crop_shape_));
  createFunction(backward_,
                 "CropGrad",
                 FuncConfig()
                     .set("crop_corner", crop_corner_)
                     .set("crop_shape", crop_shape_));

  return true;
}

void CropLayer::setOutDims(const size_t batchSize) {
  outDims_.reshape({batchSize, crop_shape_[1], crop_shape_[2], crop_shape_[3]});
}

void CropLayer::setTensorDim(const size_t batchSize) {
  CHECK_EQ(static_cast<int>(inputLayers_.size()), 1);
  inDims_.setDim(0, batchSize);
  int h = inputLayers_[0]->getOutput().getFrameHeight();
  if (h != 0) inDims_.setDim(2, h);
  int w = inputLayers_[0]->getOutput().getFrameWidth();
  if (w != 0) inDims_.setDim(3, w);
  setOutDims(batchSize);
}

void CropLayer::forward(PassType passType) {
  Layer::forward(passType);
  MatrixPtr input = inputLayers_[0]->getOutputValue();
  size_t batchSize = input->getHeight();
  setTensorDim(batchSize);
  int size = outDims_[1] * outDims_[2] * outDims_[3];
  resetOutput(batchSize, size);
  MatrixPtr outV = getOutputValue();
  REGISTER_TIMER_INFO("CropForward", getName().c_str());

  BufferArgs inputs;
  BufferArgs outputs;
  inputs.addArg(*getInputValue(0), inDims_);
  outputs.addArg(*getOutputValue(), outDims_, ASSIGN_TO);
  forward_[0]->calc(inputs, outputs);
}

void CropLayer::backward(const UpdateCallback& callback) {
  (void)callback;
  REGISTER_TIMER_INFO("CropBackward", getName().c_str());

  BufferArgs inputs;
  BufferArgs outputs;
  inputs.addArg(*getOutputGrad(), outDims_);
  outputs.addArg(*getInputGrad(0), inDims_, ADD_TO);
  backward_[0]->calc(inputs, outputs);
}
}  // namespace paddle
