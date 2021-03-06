#include <ros/ros.h>
// PCL specific includes
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/passthrough.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <iostream>
#include <pcl/io/pcd_io.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/filters/conditional_removal.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/surface/on_nurbs/fitting_surface_tdm.h>
#include <pcl/surface/on_nurbs/fitting_curve_2d_asdm.h>
#include <pcl/surface/on_nurbs/triangulation.h>
#include <vector>
#include <fstream>
#include <Eigen/Dense>

/*下面是共享内存需要用到的头文件*/
#include <stdlib.h>
#include <stdio.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <errno.h>

/*ROS自定义的消息类型*/
#include "point_msgs/Point.h"

using namespace pcl;
using namespace std;
using namespace Eigen;
unsigned order (3);

typedef pcl::PointXYZ Point;
const int BUF_SIZE=4096;

ros::Publisher pub;
point_msgs::Point output;
ros::Subscriber sub2;

/*初始化一个nurbs来作为全局变量*/
ON_NurbsSurface nurbs (3, false, order, order, order, order);
// nurbs.MakeClampedUniformKnotVector (0, 1.0);
// nurbs.MakeClampedUniformKnotVector (1, 1.0);

void
PointCloud2Vector3d (pcl::PointCloud<Point>::Ptr cloud, pcl::on_nurbs::vector_vec3d &data);

void
visualizeCurve (ON_NurbsCurve &curve,
                ON_NurbsSurface &surface,
                pcl::visualization::PCLVisualizer &viewer);


void pca_init(const sensor_msgs::PointCloud2ConstPtr& cloud_msg)
{
  pcl::PointCloud<PointXYZ>::Ptr cloud (new pcl::PointCloud<PointXYZ>);
  pcl::PointCloud<PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<PointXYZ>);

  //pcl::fromPCLPointCloud2 (blob, *cloud);
  pcl::fromROSMsg (*cloud_msg, *cloud);

  pcl::on_nurbs::NurbsDataSurface data;

  PointCloud2Vector3d (cloud, data.interior);

  unsigned order (3);

  //这是原始的使用了boxing 的手段的;
  nurbs = pcl::on_nurbs::FittingSurface::initNurbsPCABoundingBox (order, &data);

  sub2.shutdown();

  std::cout<<*(nurbs.PointAt(0,0))<<std::endl;
  // nurbs = nurbs_tmp;

}

void
cloud_cb (const sensor_msgs::PointCloud2ConstPtr& cloud_msg)
{

  unsigned order (3);
  unsigned refinement (2);
  unsigned iterations (20);
  // unsigned mesh_resolution (256);

  pcl::on_nurbs::FittingSurface::Parameter params;
  params.interior_smoothness = 0.3;
  params.interior_weight = 0.4;
  params.boundary_smoothness = 0.01;
  params.boundary_weight = 10;


//接受处理的点云
     // pcl::PCLPointCloud2 blob;
  pcl::PointCloud<PointXYZ>::Ptr cloud (new pcl::PointCloud<PointXYZ>);
  pcl::PointCloud<PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<PointXYZ>);

  //pcl::fromPCLPointCloud2 (blob, *cloud);
  pcl::fromROSMsg (*cloud_msg, *cloud);

  pcl::on_nurbs::NurbsDataSurface data;

  PointCloud2Vector3d (cloud, data.interior);

  printf ("  %lu points in data set\n", cloud->size ());


  printf ("  surface fitting ...\n");

  std::cout<<*(nurbs.PointAt(0,0))<<std::endl;

    // ros::topic::waitForMessage("/voxel/grid",);

  // nurbs = pcl::on_nurbs::FittingSurface::initNurbsPCA (order, &data);
  pcl::on_nurbs::FittingSurface fit (&data, nurbs);


/*Because the refinemrnt is too slow so I annotate this refinemnt section*/
  // for (unsigned i = 0; i < refinement; i++)
  // {
  //   fit.refine (0);
  //   fit.refine (1);
  //   fit.assemble (params);
  //   fit.solve ();
  // //   //pcl::on_nurbs::Triangulation::convertSurface2Vertices (fit.m_nurbs, mesh_cloud, mesh_vertices, mesh_resolution);
  // //   //viewer.updatePolygonMesh<pcl::PointXYZ> (mesh_cloud, mesh_vertices, mesh_id);
  // //   //viewer.spinOnce ();
  // }

  // surface fitting with final refinement level

  for (unsigned i = 0; i < iterations; i++)
  {
    fit.assemble (params);
    fit.solve ();
  }

  double u=0.0;
  double v=0.0;
  int NUB_U =6;//那一共就有7*9=63个点在网格上
  int NUB_V =8;
  float array[600];
  ON_3dPoint mypoint;
  int i =0 , j=0, k=0;

  Matrix4f car2base; //这是把手眼标定的矩阵拿过来用了
  car2base<<-0.0213  , -0.8264 ,  -0.5626   , 1.1210,
   -0.9997  ,  0.0264 ,  -0.0009 ,  -0.1194,
    0.0156   , 0.5624  , -0.8267   , 1.0983,
   -0.0000 ,  -0.0000 ,   0.0000  ,  1.0000;
  Matrix4f parend2car;
  Matrix4f parend2base;

  for(i=0;i<=NUB_U;i++)//添加等于号是为了包含边界
  {
    for(j=0;j<=NUB_V;j++)
    {
      mypoint = fit.m_nurbs.PointAt(u,v);
      array[k] = mypoint.x;
      array[k+1] =mypoint.y;
      array[k+2] =mypoint.z;
      k += 3;
      v += (double)1/NUB_V;
      // std::cout<<v<<std::endl;
    }
    u += (double)1/NUB_U;
    v=0.0;
    // std::cout<<u<<std::endl;
  }
  // std::cout<<array[0]<<std::endl;
  // std::cout<<array[1]<<std::endl;
  // std::cout<<array[2]<<std::endl;

  std::vector<float> array1(array,array+600);
  output.data = array1;

  ON_3dPoint par_end_car = fit.m_nurbs.PointAt(0.0,0.5);
  parend2car<<1,0,0,par_end_car.x,
              0,1,0,par_end_car.y,
              0,0,1,par_end_car.z,
              0,0,0,1;
  parend2base=car2base*parend2car;
  // std::cout<<parend2base(0,3)<<std::endl;
  // std::cout<<parend2base(1,3)<<std::endl;
  // std::cout<<parend2base(2,3)<<std::endl;

  pub.publish (output);
}

int
main (int argc, char** argv)
{
  // Initialize ROS
  ros::init (argc, argv, "visual_bsfitting");
  ros::NodeHandle nh;
  ros::NodeHandle nh2;

  // Create a ROS subscriber for the input point cloud
  ros::Subscriber sub = nh.subscribe ("input", 1, cloud_cb);
  sub2 = nh2.subscribe ("input", 1, pca_init);

  // Create a ROS publisher for the output model coefficients
  pub = nh.advertise<point_msgs::Point> ("visual/bsfitting", 1);

  // Spin
  ros::spin ();
}

void
PointCloud2Vector3d (pcl::PointCloud<Point>::Ptr cloud, pcl::on_nurbs::vector_vec3d &data)
{
  for (unsigned i = 0; i < cloud->size (); i++)
  {
    Point &p = cloud->at (i);
    if (!pcl_isnan (p.x) && !pcl_isnan (p.y) && !pcl_isnan (p.z))
      data.push_back (Eigen::Vector3d (p.x, p.y, p.z));
  }
}
