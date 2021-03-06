/*
 * Copyright (c) 2012, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <moveit_visualization_ros/proximity_visualization.h>
#include <collision_distance_field/collision_distance_field_types.h>
#include <collision_distance_field/hybrid_collision_robot.h>
#include <collision_distance_field/hybrid_collision_world.h>

namespace moveit_visualization_ros
{

ProximityVisualization::ProximityVisualization(const planning_scene::PlanningSceneConstPtr& planning_scene,
                                               ros::Publisher& marker_publisher)
  :
  planning_scene_(planning_scene), 
  publisher_(marker_publisher)
{
}

void ProximityVisualization::updatePlanningScene(const planning_scene::PlanningSceneConstPtr& planning_scene)
{
  planning_scene_ = planning_scene;
  // for(unsigned int i = 0; i < last_object_ids_.size(); i++) {
  //   if(!planning_scene_->getCollisionWorld()->hasObject(last_object_ids_[i])) {
  //     ROS_INFO_STREAM("Removing object");
  //     world_.removeObject(last_object_ids_[i]);
  //   }
  // }
  // last_object_ids_ = planning_scene_->getCollisionWorld()->getObjectIds();
  // for(unsigned int i = 0; i < last_object_ids_.size(); i++) {
  //   collision_detection::CollisionWorld::ObjectConstPtr other_obj = planning_scene_->getCollisionWorld()->getObject(last_object_ids_[i]);
  //   if(!world_.hasObject(last_object_ids_[i])) {
  //     world_.addToObject(last_object_ids_[i], other_obj->shapes_[0], other_obj->shape_poses_[0]);
  //   } else {
  //     collision_detection::CollisionWorld::ObjectConstPtr our_obj = world_.getObject(last_object_ids_[i]);
  //     if((our_obj->shape_poses_[0].translation()-other_obj->shape_poses_[0].translation()).norm() > .001) {
  //       world_.moveShapeInObject(our_obj->id_,
  //                                our_obj->shapes_[0],
  //                                other_obj->shape_poses_[0]);
  //     }
  //   }
  // }
  // groupChanged(current_group_);
}

void ProximityVisualization::groupChanged(const std::string& group) 
{
  current_group_ = group;
  stateChanged(group, planning_scene_->getCurrentState());
}

void ProximityVisualization::stateChanged(const std::string& group,
                                          const planning_models::KinematicState& state)
{
  if(group != current_group_) return;
  collision_detection::CollisionRequest req;
  collision_detection::CollisionResult res;
  req.group_name = current_group_;
  const collision_detection::CollisionWorldHybrid* hy_world 
    = dynamic_cast<const collision_detection::CollisionWorldHybrid*>(planning_scene_->getCollisionWorld().get());
  if(!hy_world) {
    ROS_WARN_STREAM("Can't cast to hybrid collision world");
    return;
  }
  const collision_detection::CollisionRobotHybrid* hy_robot
    = dynamic_cast<const collision_detection::CollisionRobotHybrid*>(planning_scene_->getCollisionRobot().get());
  if(!hy_robot) {
    ROS_WARN_STREAM("Can't cast to hybrid collision robot");
    return;
  }
  boost::shared_ptr<collision_detection::GroupStateRepresentation> world_grad_gsr;
  hy_world->getCollisionGradients(req, 
                                  res, 
                                  *hy_robot->getCollisionRobotDistanceField().get(),
                                  state,
                                  &planning_scene_->getAllowedCollisionMatrix(),
                                                                world_grad_gsr);
  req.contacts = true;
  req.max_contacts = 100000;
  req.max_contacts_per_pair = 1000;
  res = collision_detection::CollisionResult();
  boost::shared_ptr<collision_detection::GroupStateRepresentation> world_coll_gsr;
  hy_world->getAllCollisions(req, 
                             res, 
                             *hy_robot->getCollisionRobotDistanceField().get(),
                             state,
                             &planning_scene_->getAllowedCollisionMatrix(),
                             world_coll_gsr);

  visualization_msgs::MarkerArray arrow_markers;
  std_msgs::ColorRGBA col;
  col.b = 1.0;
  col.a = .8;
  collision_detection::getProximityGradientMarkers(planning_scene_->getPlanningFrame(),
                                                   "arrows",
                                                   ros::Duration(0.0),
                                                   world_grad_gsr->link_body_decompositions_,
                                                   world_grad_gsr->attached_body_decompositions_,
                                                   world_grad_gsr->gradients_,
                                                   arrow_markers);
  collision_detection::getCollisionMarkers(planning_scene_->getPlanningFrame(),
                                           "spheres",
                                           ros::Duration(0.0),
                                           world_coll_gsr->link_body_decompositions_,
                                           world_grad_gsr->attached_body_decompositions_,
                                           world_coll_gsr->gradients_,
                                           arrow_markers);
  
  publisher_.publish(arrow_markers);
}

}

