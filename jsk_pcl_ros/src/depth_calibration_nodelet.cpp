// -*- mode: C++ -*-
/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, JSK Lab
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/o2r other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#include "jsk_pcl_ros/depth_calibration.h"
#include <jsk_topic_tools/rosparam_utils.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

namespace jsk_pcl_ros
{
  void DepthCalibration::onInit()
  {
    DiagnosticNodelet::onInit();
    if (pnh_->hasParam("coefficients2")) {
      jsk_topic_tools::readVectorParameter(
        *pnh_, "coefficients2", coefficients2_);
    }
    else {
      coefficients2_.assign(3, 0);
    }
    if (pnh_->hasParam("coefficients1")) {
      jsk_topic_tools::readVectorParameter(
        *pnh_, "coefficients1", coefficients1_);
    }
    else {
      coefficients1_.assign(3, 0);
    }
    if (pnh_->hasParam("coefficients0")) {
      jsk_topic_tools::readVectorParameter(
        *pnh_, "coefficients0", coefficients0_);
    }
    else {
      coefficients0_.assign(3, 0);
    }
    pnh_->param("use_abs", use_abs_, false);
    
    ROS_INFO("C2(u, v) = %fu + %fv + %f",
             coefficients2_[0], coefficients2_[1], coefficients2_[2]);
    ROS_INFO("C1(u, v) = %fu + %fv + %f",
             coefficients1_[0], coefficients1_[1], coefficients1_[2]);
    ROS_INFO("C0(u, v) = %fu + %fv + %f",
             coefficients0_[0], coefficients0_[1], coefficients0_[2]);
    set_calibration_parameter_srv_ = pnh_->advertiseService(
      "set_calibration_parameter",
      &DepthCalibration::setCalibrationParameter,
      this);
    pub_ = advertise<sensor_msgs::Image>(*pnh_, "output", 1);
  }
  
  bool DepthCalibration::setCalibrationParameter(
    DepthCalibrationParameter::Request& req,
    DepthCalibrationParameter::Response& res)
  {
    boost::mutex::scoped_lock lock(mutex_);
    coefficients2_[0] = req.c22;
    coefficients2_[1] = req.c21;
    coefficients2_[2] = req.c20;
    coefficients1_[0] = req.c12;
    coefficients1_[1] = req.c11;
    coefficients1_[2] = req.c10;
    coefficients0_[0] = req.c02;
    coefficients0_[1] = req.c01;
    coefficients0_[2] = req.c00;
    use_abs_ = req.use_abs;
    return true;
  }

  void DepthCalibration::subscribe()
  {
      sub_input_.subscribe(*pnh_, "input", 1);
      sub_camera_info_.subscribe(*pnh_, "camera_info", 1);
      sync_ = boost::make_shared<message_filters::Synchronizer<SyncPolicy> >(100);
      sync_->connectInput(sub_input_, sub_camera_info_);
      sync_->registerCallback(boost::bind(&DepthCalibration::calibrate, this, _1, _2));
  }
  
  void DepthCalibration::unsubscribe()
  {
      sub_input_.unsubscribe();
      sub_camera_info_.unsubscribe();
  }

  void DepthCalibration::calibrate(
      const sensor_msgs::Image::ConstPtr& msg,
      const sensor_msgs::CameraInfo::ConstPtr& camera_info)
  {
    boost::mutex::scoped_lock lock(mutex_);
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
      cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::TYPE_32FC1);
    }
    catch (cv_bridge::Exception& e)
    {
      NODELET_ERROR("cv_bridge exception: %s", e.what());
      return;
    }
    cv::Mat image = cv_ptr->image;
    cv::Mat output_image = image.clone();
    double cu = camera_info->P[2];
    double cv = camera_info->P[6];
    for(int v = 0; v < image.rows; v++) {
      for(int u = 0; u < image.cols; u++) {
        float z = image.at<float>(v, u);
        if (isnan(z)) {
          output_image.at<float>(v, u) = z;
        }
        else {
          output_image.at<float>(v, u) = applyModel(z, u, v, cu, cv);
        }
        //NODELET_INFO("z: %f", z);
      }
    }
    sensor_msgs::Image::Ptr ros_image = cv_bridge::CvImage(msg->header, "32FC1", output_image).toImageMsg();
    pub_.publish(ros_image);
  }

  void DepthCalibration::updateDiagnostic(
      diagnostic_updater::DiagnosticStatusWrapper &stat)
  {
  }

}
#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS (jsk_pcl_ros::DepthCalibration,
                          nodelet::Nodelet);
