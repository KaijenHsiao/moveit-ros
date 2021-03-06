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
 *     * Neither the name of the <ORGANIZATION> nor the names of its
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

// Author: E. Gil Jones

#include <moveit_visualization_ros/moveit_proximity_visualizer.h>

namespace moveit_visualization_ros 
{

MoveItProximityVisualizer::MoveItProximityVisualizer() :
  MoveItVisualizer()
{
  proximity_visualization_.reset(new ProximityVisualizationQtWrapper(planning_scene_monitor_->getPlanningScene(),
                                                                     vis_marker_array_publisher_));
  proximity_visualization_->groupChanged(pv_->getCurrentGroup());
  QObject::connect(planning_group_selection_menu_, 
                   SIGNAL(groupSelected(const QString&)),
                   proximity_visualization_.get(),
                   SLOT(newGroupSelected(const QString&)));

  pv_->addStateChangedCallback(boost::bind(&ProximityVisualization::stateChanged, proximity_visualization_, _1, _2));
}

void MoveItProximityVisualizer::updatePlanningScene(planning_scene::PlanningSceneConstPtr planning_scene)
{
  MoveItVisualizer::updatePlanningScene(planning_scene);
  proximity_visualization_->updatePlanningScene(planning_scene);
}

}
