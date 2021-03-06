/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2012, Willow Garage, Inc.
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
*     disclaimer in the documentation and/or other materials provided
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

/* Author: Ioan Sucan */

#include <ros/ros.h>
#include <moveit/planning_scene/planning_scene.h>
#include <moveit/planning_models_loader/kinematic_model_loader.h>
#include <pluginlib/class_loader.h>
#include <moveit/planning_interface/planning_interface.h>
#include <moveit/trajectory_processing/trajectory_tools.h>

#include <moveit_msgs/ComputePlanningBenchmark.h>
#include <moveit_msgs/QueryPlannerInterfaces.h>
#include <boost/lexical_cast.hpp>
#include <boost/progress.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/math/constants/constants.hpp>

#include <unistd.h>
#include <fstream>

static const std::string ROBOT_DESCRIPTION="robot_description";      // name of the robot description (a param name, so it can be changed externally)
static const std::string BENCHMARK_SERVICE_NAME="benchmark_planning_problem"; // name of the advertised benchmarking service
static const std::string QUERY_SERVICE_NAME="query_known_planner_interfaces"; // name of the advertised query service

class BenchmarkService
{
public:
  
  BenchmarkService(void) : kml_(ROBOT_DESCRIPTION)
  {
    // initialize a planning scene
    
    if (kml_.getModel())
    {
      scene_.reset(new planning_scene::PlanningScene());
      scene_->configure(kml_.getURDF(), kml_.getSRDF() ? kml_.getSRDF() : boost::shared_ptr<srdf::Model>(new srdf::Model()), kml_.getModel());
      if (scene_->isConfigured())
      {
        cscene_ = scene_;		
        // load the planning plugins
        try
        {
          planner_plugin_loader_.reset(new pluginlib::ClassLoader<planning_interface::Planner>("planning_interface", "planning_interface::Planner"));
        }
        catch(pluginlib::PluginlibException& ex)
        {
          ROS_FATAL_STREAM("Exception while creating planning plugin loader " << ex.what());
        }
	
        const std::vector<std::string> &classes = planner_plugin_loader_->getDeclaredClasses();
        for (std::size_t i = 0 ; i < classes.size() ; ++i)
        {
          ROS_INFO("Attempting to load and configure %s", classes[i].c_str());
          try
          {
            boost::shared_ptr<planning_interface::Planner> p = planner_plugin_loader_->createInstance(classes[i]);
            p->init(scene_->getKinematicModel());
            planner_interfaces_[classes[i]] = p;
          }
          catch (pluginlib::PluginlibException& ex)
          {
            ROS_ERROR_STREAM("Exception while loading planner '" << classes[i] << "': " << ex.what());
          }
        }
	
        if (planner_interfaces_.empty())
          ROS_ERROR("No planning plugins have been loaded. Nothing to do for the benchmarking service.");
        else
        {
          std::stringstream ss;
          for (std::map<std::string, boost::shared_ptr<planning_interface::Planner> >::const_iterator it = planner_interfaces_.begin() ; 
               it != planner_interfaces_.end(); ++it)
            ss << it->first << " ";
          ROS_INFO("Available planner instances: %s", ss.str().c_str());
          benchmark_service_ = nh_.advertiseService(BENCHMARK_SERVICE_NAME, &BenchmarkService::computeBenchmark, this);
          query_service_ = nh_.advertiseService(QUERY_SERVICE_NAME, &BenchmarkService::queryInterfaces, this);
        }
      }
      else
        ROS_ERROR("Unable to configure planning scene");
    }
    else
      ROS_ERROR("Unable to construct planning model for parameter %s", ROBOT_DESCRIPTION.c_str());
  }
  
  bool queryInterfaces(moveit_msgs::QueryPlannerInterfaces::Request &req, moveit_msgs::QueryPlannerInterfaces::Response &res)
  {    
    for (std::map<std::string, boost::shared_ptr<planning_interface::Planner> >::const_iterator it = planner_interfaces_.begin() ; 
         it != planner_interfaces_.end(); ++it)
    {
      moveit_msgs::PlannerInterfaceDescription pi_desc;
      pi_desc.name = it->first;
      it->second->getPlanningAlgorithms(pi_desc.planner_ids);
      res.planner_interfaces.push_back(pi_desc);
    }
    return true;
  }
  
  void collectMetrics(std::map<std::string, std::string> &rundata, const moveit_msgs::MotionPlanDetailedResponse &mp_res, bool solved, double total_time)
  {
    rundata["total_time REAL"] = boost::lexical_cast<std::string>(total_time);
    rundata["solved BOOLEAN"] = boost::lexical_cast<std::string>(solved);
    double L = 0.0;
    double clearance = 0.0;
    double smoothness = 0.0;		
    bool correct = true;
    if (solved)
    {
      double process_time = total_time;
      for (std::size_t j = 0 ; j < mp_res.trajectory.size() ; ++j)
      {
        correct = true;
        L = 0.0;
        clearance = 0.0;
        smoothness = 0.0;
        std::vector<kinematic_state::KinematicStatePtr> p;
        trajectory_processing::convertToKinematicStates(p, mp_res.trajectory_start, mp_res.trajectory[j],
                                                        scene_->getCurrentState(), scene_->getTransforms());
        
        // compute path length
        for (std::size_t k = 1 ; k < p.size() ; ++k)
          L += p[k-1]->distance(*p[k]);

        // compute correctness and clearance
        collision_detection::CollisionRequest req;
        for (std::size_t k = 0 ; k < p.size() ; ++k)
        {
          collision_detection::CollisionResult res;
          scene_->checkCollisionUnpadded(req, res, *p[k]);
          if (res.collision)
            correct = false;
          if (!p[k]->satisfiesBounds())
            correct = false;
          double d = scene_->distanceToCollisionUnpadded(*p[k]);
          if (d > 0.0) // in case of collision, distance is negative
            clearance += d;
        }
        clearance /= (double)p.size();
        
        // compute smoothness
        if (p.size() > 2)
        {
          double a = p[0]->distance(*p[1]);
          for (std::size_t k = 2 ; k < p.size() ; ++k)
          {
            // view the path as a sequence of segments, and look at the triangles it forms:
            //          s1
            //          /\          s4
            //      a  /  \ b       |
            //        /    \        |
            //       /......\_______|
            //     s0    c   s2     s3
            //
            // use Pythagoras generalized theorem to find the cos of the angle between segments a and b
            double b = p[k-1]->distance(*p[k]);
            double cdist = p[k-2]->distance(*p[k]);
            double acosValue = (a*a + b*b - cdist*cdist) / (2.0*a*b);
            if (acosValue > -1.0 && acosValue < 1.0)
            {
              // the smoothness is actually the outside angle of the one we compute
              double angle = (boost::math::constants::pi<double>() - acos(acosValue));
              
              // and we normalize by the length of the segments
              double u = 2.0 * angle; /// (a + b);
              smoothness += u * u;
            }
            a = b;
          }
          smoothness /= (double)p.size();
        }
        rundata["path_" + mp_res.description[j] + "_correct BOOLEAN"] = boost::lexical_cast<std::string>(correct);
        rundata["path_" + mp_res.description[j] + "_length REAL"] = boost::lexical_cast<std::string>(L);
        rundata["path_" + mp_res.description[j] + "_clearance REAL"] = boost::lexical_cast<std::string>(clearance);
        rundata["path_" + mp_res.description[j] + "_smoothness REAL"] = boost::lexical_cast<std::string>(smoothness);
        rundata["path_" + mp_res.description[j] + "_time REAL"] = boost::lexical_cast<std::string>(mp_res.processing_time[j]);     
        process_time -= mp_res.processing_time[j].toSec();
      }
      if (process_time <= 0.0)
        process_time = 0.0;
      rundata["process_time REAL"] = boost::lexical_cast<std::string>(process_time);
      
    }
  }
  
  bool computeBenchmark(moveit_msgs::ComputePlanningBenchmark::Request &req, moveit_msgs::ComputePlanningBenchmark::Response &res)
  {      
    // figure out which planners to test
    if (!req.planner_interfaces.empty())
      for (std::size_t i = 0 ; i < req.planner_interfaces.size() ; ++i)
        if (planner_interfaces_.find(req.planner_interfaces[i].name) == planner_interfaces_.end())
          ROS_ERROR("Planning interface '%s' was not found", req.planner_interfaces[i].name.c_str());
    
    res.planner_interfaces.clear();
    std::vector<planning_interface::Planner*> planner_interfaces_to_benchmark;
    std::vector<planning_interface::PlannerCapabilities> planner_interfaces_to_benchmark_capabilities;
    std::vector<std::vector<std::string> > planner_ids_to_benchmark_per_planner_interface;
    std::vector<std::size_t> average_count_per_planner_interface;
    moveit_msgs::GetMotionPlan::Request mp_req;
    mp_req.motion_plan_request = req.motion_plan_request;
    
    for (std::map<std::string, boost::shared_ptr<planning_interface::Planner> >::const_iterator it = planner_interfaces_.begin() ; 
         it != planner_interfaces_.end(); ++it)
    {
      int found = -1;
      if (!req.planner_interfaces.empty())
      {
        for (std::size_t i = 0 ; i < req.planner_interfaces.size() ; ++i)
          if (req.planner_interfaces[i].name == it->first)
          {
            found = i;
            break;
          }
        if (found < 0)
          continue;
      }
      if (it->second->canServiceRequest(mp_req))
      {
        planning_interface::PlannerCapabilities capabilities = it->second->getPlannerCapabilities();
        res.planner_interfaces.resize(res.planner_interfaces.size() + 1);
        res.planner_interfaces.back().name = it->first;
        planner_interfaces_to_benchmark.push_back(it->second.get());
        planner_interfaces_to_benchmark_capabilities.push_back(capabilities);
        planner_ids_to_benchmark_per_planner_interface.resize(planner_ids_to_benchmark_per_planner_interface.size() + 1);
        average_count_per_planner_interface.resize(average_count_per_planner_interface.size() + 1, std::max<std::size_t>(1, req.default_average_count));
        std::vector<std::string> known;
        it->second->getPlanningAlgorithms(known);
        if (found < 0 || req.planner_interfaces[found].planner_ids.empty())
          planner_ids_to_benchmark_per_planner_interface.back() = known;
        else
        {
          if ((int)req.average_count.size() > found)
            average_count_per_planner_interface.back() = std::max<std::size_t>(1, req.average_count[found]);
          for (std::size_t k = 0 ; k < req.planner_interfaces[found].planner_ids.size() ; ++k)
          {
            bool fnd = false;
            for (std::size_t q = 0 ; q < known.size() ; ++q)
              if (known[q] == req.planner_interfaces[found].planner_ids[k] ||
                  mp_req.motion_plan_request.group_name + "[" + known[q] + "]" == req.planner_interfaces[found].planner_ids[k])
              {
                fnd = true;
                break;
              }
            if (fnd)
              planner_ids_to_benchmark_per_planner_interface.back().push_back(req.planner_interfaces[found].planner_ids[k]);
            else
              ROS_ERROR("The planner id '%s' is not known to the planning interface '%s'", req.planner_interfaces[found].planner_ids[k].c_str(), it->first.c_str());
          }          
        }
        
        if (planner_ids_to_benchmark_per_planner_interface.back().empty())
          ROS_ERROR("Planning interface '%s' has no planners defined", it->first.c_str());
      }
      else
        ROS_WARN_STREAM("Planning interface '" << it->second->getDescription() << "' is not able to solve the specified benchmark problem.");
    }
    
    if (planner_interfaces_to_benchmark.empty())
    {
      ROS_ERROR("There are no planning interfaces to benchmark");
      return false;	    
    }
    
    // output information about tested planners
    ROS_INFO("Benchmarking planning interfaces:");
    std::stringstream sst;
    for (std::size_t i = 0 ; i < planner_interfaces_to_benchmark.size() ; ++i)
    {
      if (planner_ids_to_benchmark_per_planner_interface[i].empty())
        continue;
      sst << "  * " << planner_interfaces_to_benchmark[i]->getDescription() << " executed " << average_count_per_planner_interface[i] << " times" << std::endl;
      for (std::size_t k = 0 ; k < planner_ids_to_benchmark_per_planner_interface[i].size() ; ++k)
        sst << "    - " << planner_ids_to_benchmark_per_planner_interface[i][k] << std::endl;
      sst << std::endl;
    }
    ROS_INFO("\n%s", sst.str().c_str());
    
    // configure planning context 
    scene_->setPlanningSceneMsg(req.scene);
    res.responses.resize(planner_interfaces_to_benchmark.size());

    std::size_t total_n_planners = 0;
    std::size_t total_n_runs = 0;
    for (std::size_t i = 0 ; i < planner_ids_to_benchmark_per_planner_interface.size() ; ++i)
    {
      total_n_planners += planner_ids_to_benchmark_per_planner_interface[i].size();
      total_n_runs += planner_ids_to_benchmark_per_planner_interface[i].size() * average_count_per_planner_interface[i];
    }
    
    // benchmark all the planners
    ros::WallTime startTime = ros::WallTime::now();
    boost::progress_display progress(total_n_runs, std::cout);
    typedef std::vector<std::map<std::string, std::string> > RunData;
    std::vector<RunData> data;
    std::vector<bool> first(planner_interfaces_to_benchmark.size(), true);
    for (std::size_t i = 0 ; i < planner_interfaces_to_benchmark.size() ; ++i)
    {
      for (std::size_t j = 0 ; j < planner_ids_to_benchmark_per_planner_interface[i].size() ; ++j)
      {
        mp_req.motion_plan_request.planner_id = planner_ids_to_benchmark_per_planner_interface[i][j];
        RunData runs(average_count_per_planner_interface[i]);
        for (unsigned int c = 0 ; c < average_count_per_planner_interface[i] ; ++c)
        {
          ++progress; 
          ROS_DEBUG("Calling %s:%s", planner_interfaces_to_benchmark[i]->getDescription().c_str(), mp_req.motion_plan_request.planner_id.c_str());
          moveit_msgs::MotionPlanDetailedResponse mp_res;
          ros::WallTime start = ros::WallTime::now();
          bool solved = planner_interfaces_to_benchmark[i]->solve(cscene_, mp_req, mp_res);
          double total_time = (ros::WallTime::now() - start).toSec();
          
          // collect data   
          start = ros::WallTime::now();
          collectMetrics(runs[c], mp_res, solved, total_time);
          double metrics_time = (ros::WallTime::now() - start).toSec();
          ROS_DEBUG("Spent %lf seconds collecting metrics", metrics_time);
          
          // record the first solution in the response
          if (solved && first[i])
          {
            first[i] = false;
            res.responses[i] = mp_res;
          }
        }
        data.push_back(runs);
      }
    }
    
    double duration = (ros::WallTime::now() - startTime).toSec();
    std::string host = getHostname();
    res.filename = req.filename.empty() ? ("moveit_benchmarks_" + host + "_" + boost::posix_time::to_iso_extended_string(startTime.toBoost()) + ".log") : req.filename;
    std::ofstream out(res.filename.c_str());
    out << "Experiment " << (cscene_->getName().empty() ? "NO_NAME" : cscene_->getName()) << std::endl;
    out << "Running on " << (host.empty() ? "UNKNOWN" : host) << std::endl;
    out << "Starting at " << boost::posix_time::to_iso_extended_string(startTime.toBoost()) << std::endl;
    out << "<<<|" << std::endl << "ROS" << std::endl << req.motion_plan_request << std::endl << "|>>>" << std::endl;
    out << req.motion_plan_request.allowed_planning_time.toSec() << " seconds per run" << std::endl;
    out << duration << " seconds spent to collect the data" << std::endl;
    out << total_n_planners << " planners" << std::endl;
    std::size_t ri = 0;
    for (std::size_t q = 0 ; q < planner_interfaces_to_benchmark.size() ; ++q)
      for (std::size_t p = 0 ; p < planner_ids_to_benchmark_per_planner_interface[q].size() ; ++p, ++ri)
      {
        out << planner_interfaces_to_benchmark[q]->getDescription() + "_" + planner_ids_to_benchmark_per_planner_interface[q][p] << std::endl;
        // in general, we could have properties specific for a planner;
        // right now, we do not include such properties
        out << "0 common properties" << std::endl;
        
        // construct the list of all possible properties for all runs
        std::set<std::string> propSeen;
        for (std::size_t j = 0 ; j < data[ri].size() ; ++j)
          for (std::map<std::string, std::string>::const_iterator mit = data[ri][j].begin() ; mit != data[ri][j].end() ; ++mit)
            propSeen.insert(mit->first);
        std::vector<std::string> properties;
        for (std::set<std::string>::iterator it = propSeen.begin() ; it != propSeen.end() ; ++it)
          properties.push_back(*it);
        out << properties.size() << " properties for each run" << std::endl;
        for (unsigned int j = 0 ; j < properties.size() ; ++j)
          out << properties[j] << std::endl;
        out << data[ri].size() << " runs" << std::endl;
        for (std::size_t j = 0 ; j < data[ri].size() ; ++j)
        {
          for (unsigned int k = 0 ; k < properties.size() ; ++k)
          {
            std::map<std::string, std::string>::const_iterator it = data[ri][j].find(properties[k]);
            if (it != data[ri][j].end())
              out << it->second;
            out << "; ";
          }
          out << std::endl;
        }
        out << '.' << std::endl;
      }
    out.close();
    ROS_INFO("Results saved to '%s'", res.filename.c_str());
    res.error_code.val = moveit_msgs::MoveItErrorCodes::SUCCESS;
    return true;
  }
  
  void status(void) const
  {
  }
  
private:
  
  std::string getHostname(void) const
  {
    static const int BUF_SIZE = 1024;
    char buffer[BUF_SIZE];
    int err = gethostname(buffer, sizeof(buffer));
    if (err != 0)
      return std::string();
    else
    {
      buffer[BUF_SIZE - 1] = '\0';
      return std::string(buffer);
    }
  }
  
  ros::NodeHandle nh_;
  planning_models_loader::KinematicModelLoader kml_;
  planning_scene::PlanningScenePtr scene_;
  planning_scene::PlanningSceneConstPtr cscene_;
  boost::shared_ptr<pluginlib::ClassLoader<planning_interface::Planner> > planner_plugin_loader_;
  std::map<std::string, boost::shared_ptr<planning_interface::Planner> > planner_interfaces_;
  ros::ServiceServer benchmark_service_;
  ros::ServiceServer query_service_;
};

int main(int argc, char **argv)
{
  ros::init(argc, argv, "planning_scene_benchmark");
  ros::AsyncSpinner spinner(1);
  spinner.start();
  
  BenchmarkService bs;
  bs.status();
  ros::waitForShutdown();
  
  return 0;
}
