#include <ctime>
#include <iomanip>
#include <sstream>
#include <bdifd/bdifd_util.h>
#include <bdifd/bdifd_analytic.h>
#include <bdifd/bdifd_rig.h>
#include "bdifd_data.h"
#include <algorithm>
#include <vsol/vsol_line_2d.h>
#include <vul/vul_file.h>
#include <boost/random/variate_generator.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_on_sphere.hpp>
#include <boost/random/normal_distribution.hpp>

void bdifd_data::
max_err_reproj_perturb(
    const std::vector<std::vector<bdifd_3rd_order_point_2d> > &crv2d_gt_,
    const std::vector<bdifd_camera> &cam_,
    const bdifd_rig &rig,
    double &err_pos,
    double &err_t,
    double &err_k,
    double &err_kdot,
    unsigned &i_pos, 
    unsigned &i_t, 
    unsigned &i_k, 
    unsigned &i_kdot,
    unsigned &nvalid
    )
{
  std::vector<double> err_pos_sq_v;
  std::vector<double> err_t_v;
  std::vector<double> err_k_v;
  std::vector<double> err_kdot_v;
  std::vector<unsigned> valid_idx;

  err_reproj_perturb(crv2d_gt_, cam_, rig, err_pos_sq_v, err_t_v, err_k_v, err_kdot_v, valid_idx);


  unsigned idx;

  err_pos = std::sqrt(bdifd_util::max(err_pos_sq_v,idx));
  i_pos   = valid_idx[idx];

  err_t = bdifd_util::max(err_t_v,idx);
  i_t = valid_idx[idx];

  err_k = bdifd_util::max(err_k_v,idx);
  i_k = valid_idx[idx];

  err_kdot = bdifd_util::max(err_kdot_v,idx);
  i_kdot = valid_idx[idx];

  nvalid = valid_idx.size();
}

//: err_* : vector of reprojection errors of the measure ??? for the valid
// (i.e. non-degenerate) points. Thus Number of valid points l== err_*.size()
// 
// valid_idx[i] returns the index l into crv2d_gt_ of the i-th valid point;
// For example, err_pos_sq[i] is the positional reprojection error of the
// correspondence crv2d_gt_[:][valid_idx[i]].
//
void bdifd_data::
err_reproj_perturb(
    const std::vector<std::vector<bdifd_3rd_order_point_2d> > &crv2d_gt_,
    const std::vector<bdifd_camera> &cam_,
    const bdifd_rig &rig,
    std::vector<double> &err_pos_sq,
    std::vector<double> &err_t,
    std::vector<double> &err_k,
    std::vector<double> &err_kdot,
    std::vector<unsigned> &valid_idx
    )
{
  assert(crv2d_gt_.size() >= 3 && crv2d_gt_[0].size() == crv2d_gt_[1].size() && crv2d_gt_[0].size() == crv2d_gt_[2].size());

  err_pos_sq.reserve(crv2d_gt_[0].size());
  err_t.reserve(crv2d_gt_[0].size());
  err_k.reserve(crv2d_gt_[0].size());
  err_kdot.reserve(crv2d_gt_[0].size());

  const double epipolar_angle_thresh = vnl_math::pi/6;

  for (unsigned i=0; i < crv2d_gt_[0].size(); ++i) {
    // get hold of p1, theta1, k1, kdot1
    const bdifd_3rd_order_point_2d &p1 = crv2d_gt_[0][i];

    // get hold of p2, theta2, k2, kdot2
    const bdifd_3rd_order_point_2d &p2 = crv2d_gt_[1][i];
    bdifd_3rd_order_point_2d p1_w, p2_w;
    bdifd_3rd_order_point_3d Prec;
    rig.cam[0].img_to_world(&p1,&p1_w);
    rig.cam[1].img_to_world(&p2,&p2_w);

    rig.reconstruct_3rd_order(p1_w, p2_w, &Prec);

    bool valid;
    bdifd_3rd_order_point_2d p_rec_reproj = cam_[2].project_to_image(Prec,&valid);

    double epipolar_angle = bdifd_rig::angle_with_epipolar_line(p1.t,p1.gama,rig.f12);
      
    if (valid && epipolar_angle > epipolar_angle_thresh) {
      valid_idx.push_back(i);

      bdifd_3rd_order_point_2d p3 = crv2d_gt_[2][i];

      double dx = p_rec_reproj.gama[0] - p3.gama[0];
      double dy = p_rec_reproj.gama[1] - p3.gama[1];
      err_pos_sq.push_back(dx*dx + dy*dy);

      double dtheta = std::acos(bdifd_util::clump_to_acos( p_rec_reproj.t[0]*p3.t[0] + p_rec_reproj.t[1]*p3.t[1] ));
      err_t.push_back(dtheta);
      
      double dk = std::fabs(p_rec_reproj.k - p3.k);
      err_k.push_back(dk);

      double dkdot = std::fabs(p_rec_reproj.kdot - p3.kdot);
      err_kdot.push_back(dkdot);
    }
  }
}

//: given a vector of 3rd order point 3d, it projects each point into the
//cameras and returns a vector containing a vector of points for each view
void bdifd_data::
project_into_cams(
    const std::vector<bdifd_3rd_order_point_3d> &crv3d, 
    const std::vector<bdifd_camera> &cam,
    std::vector<std::vector<vsol_point_2d_sptr> > &xi //:< image coordinates
    ) 
{
  unsigned nviews=cam.size();
  xi.resize(nviews);

  for (unsigned k=0; k<nviews; ++k) {
    xi[k].resize(crv3d.size());

    for (unsigned i=0; i<crv3d.size(); ++i) {
      // - get image coordinates
      bdifd_vector_2d p_aux;
      p_aux = cam[k].project_to_image(crv3d[i].Gama);
      xi[k][i] = new vsol_point_2d(p_aux[0], p_aux[1]);
    }
  }
}

//: Project a set of space curves into different cameras
void bdifd_data::
project_into_cams(
    const std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d,
    const std::vector<bdifd_camera> &cam,
    std::vector<std::vector<bdifd_3rd_order_point_2d> > &crv2d_gt)
{
  unsigned nviews=cam.size();
  unsigned npts=0;

  for (unsigned  k=0; k < crv3d.size(); ++k) { // ncurves
    npts += crv3d[k].size();
  }

  crv2d_gt.resize(nviews);
  for (unsigned i=0; i < nviews; ++i) { // nviews
    crv2d_gt[i].resize(npts);
    unsigned   nn=0;
    for (unsigned  k=0; k < crv3d.size(); ++k) { // ncurves
      for (unsigned  jj=0; jj < crv3d[k].size(); ++jj) {
        bool not_degenerate;
        crv2d_gt[i][nn++] = cam[i].project_to_image(crv3d[k][jj], &not_degenerate);
      }
    }
  }
}

//: Project a set of space curves into different cameras
void bdifd_data::
project_into_cams_without_epitangency(
    const std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d,
    const std::vector<bdifd_camera> &cam,
    std::vector<std::vector<bdifd_3rd_order_point_2d> > &crv2d,
    double epipolar_angle_thresh)
{
  std::vector<std::vector<bdifd_3rd_order_point_2d> > crv2d_gt_complete;

  project_into_cams(crv3d, cam, crv2d_gt_complete);

  crv2d.resize(crv2d_gt_complete.size());

  bdifd_rig rig(cam[0].Pr_, cam[1].Pr_);
  for (unsigned i=0; i < crv2d_gt_complete[0].size(); ++i) {
    const bdifd_3rd_order_point_2d &p1 = crv2d_gt_complete[0][i];

    double epipolar_angle = bdifd_rig::angle_with_epipolar_line(p1.t,p1.gama,rig.f12);

    if (epipolar_angle > epipolar_angle_thresh) {
      for (unsigned iv=0; iv < cam.size(); ++iv) {
        crv2d[iv].push_back(crv2d_gt_complete[iv][i]);
      }
    }
  }
}

void bdifd_data::
project_into_cams(
    const std::vector<bdifd_3rd_order_point_3d> &crv3d, 
    const std::vector<bdifd_camera> &cam,
    std::vector<std::vector<bdifd_3rd_order_point_2d> > &xi //:< image coordinates
    ) 
{
  unsigned nviews=cam.size();
  xi.resize(nviews);

  for (unsigned k=0; k<nviews; ++k) {
    xi[k].resize(crv3d.size());

    for (unsigned i=0; i<crv3d.size(); ++i) {
      bool not_degenerate;
      // - get image coordinates
      xi[k][i] = cam[k].project_to_image(crv3d[i], &not_degenerate);
    }
  }
}

void bdifd_data::
space_curves_ctspheres_old(
    std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d
    )
{
  static const unsigned number_of_curves=7;

  crv3d.resize(number_of_curves);

  std::vector<double> theta;

  bdifd_vector_3d translation(-11,-5,0);

  bdifd_analytic::circle_curve( 1, translation, crv3d[0], theta, -89, 1, 175);
  bdifd_analytic::circle_curve( 1, translation, crv3d[1], theta, 89, 1, 175);

  translation = bdifd_vector_3d(-8,-4,0);
  bdifd_analytic::circle_curve( 0.5, translation, crv3d[2], theta, 90, 1, 359);

//  bdifd_analytic::circle_curve( 5, translation, crv3d_2, theta,
//      120, 0.10, 120);

  translation = bdifd_vector_3d (-9,-3,0);
  bdifd_analytic:: helix_curve( 0.2, 4, translation,crv3d[3], theta, 0, 1, 360*5);

  translation = bdifd_vector_3d (-12,-2.5, 15);
  bdifd_analytic::circle_curve( 1.5, translation, crv3d[4], theta, 90, 1, 359);

  translation = bdifd_vector_3d (0,0,0);
  bdifd_vector_3d direction(1,1, 10);
  bdifd_analytic::line(translation, direction, crv3d[5], theta, 10, 0.01);

  translation = bdifd_vector_3d (-5.82,-5,-20);
  direction = bdifd_vector_3d (0,1, 3);
  bdifd_analytic::line(translation, direction, crv3d[6], theta, 30, 0.1);
}

void bdifd_data::
space_curves_ctspheres(
    std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d
    )
{
  std::vector<double> theta;
  bdifd_vector_3d translation;
  bdifd_vector_3d direction;
  std::vector<bdifd_3rd_order_point_3d > crv_tmp;
  bdifd_vector_3d axis;
  double angle;



  double l=10; //:< length of cube
  double un = l/20.; //(so cube goes from -10*un to 10*un)

  double stepsize_lines =un/5.;
  double stepsize_circle=2;
  double stepsize_ellipse=2;
  double stepsize_helix=5;
  double stepsize_curve1=0.5;

//  double stepsize_lines =un;
//  double stepsize_circle=5;
//  double stepsize_ellipse=5;
//  double stepsize_helix=5;
//  double stepsize_curve1=5;

  { // Basic shapes to define volume where curves are to be drawn
    translation = bdifd_vector_3d (0,0,0);
    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    bdifd_analytic::circle_curve( 1, translation, crv_tmp, theta, 0, stepsize_circle, 360);
    crv3d.push_back(crv_tmp); crv_tmp.clear();

    
    bdifd_vector_3d t_cube = bdifd_vector_3d(-l/2,-l/2,-l/2);
    translation = translation + t_cube;
    //: Cube
    direction = bdifd_vector_3d (1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,0,0)+t_cube;

    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,l,0) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,0,l) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,l,l) + t_cube;

    direction = bdifd_vector_3d (-1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    direction = bdifd_vector_3d (0,-1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    direction = bdifd_vector_3d (0,0, -1);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 
  }

  translation = bdifd_vector_3d (6,6,-2)*un;
  direction = bdifd_vector_3d(5,5, 9)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 10*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5.82,-5,-9)*un;
  direction = bdifd_vector_3d (0,1, 3)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 15*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d(-6,-2,0)*un;
  bdifd_analytic::circle_curve( 0.5*un, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();


  translation = bdifd_vector_3d (5,2.5, 9)*un;
  bdifd_analytic::circle_curve( 1.5*un, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  {
  translation = bdifd_vector_3d(8,-5,0)*un;

  bdifd_analytic::circle_curve( 1*un, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  bdifd_analytic::circle_curve( 1*un, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  }

  translation = bdifd_vector_3d(7,7,5)*un;
  bdifd_analytic::circle_curve( 2*un, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d(7,6.7,-5)*un;
  bdifd_analytic::circle_curve( 1.9*un, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::circle_curve( 3*un, translation, crv_tmp, theta, 60, stepsize_circle, 120);
  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-7,3)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Ellipses
  translation = bdifd_vector_3d (-6,-6,-7)*un;
  bdifd_analytic::ellipse(un, 4*un,translation, crv_tmp, theta, 60, stepsize_ellipse, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (9,0,-3)*un;
  bdifd_analytic::ellipse(un, 4*un,translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(3*un, un, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(3*un, un, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(un, 0.5*un, translation, crv_tmp, theta, 0, stepsize_ellipse, 280);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-8,6,+8)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(4*un, un, translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,-1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,8,+5)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Helices
  translation = bdifd_vector_3d (-9,-9,0)*un;
  bdifd_analytic:: helix_curve( 0.5*un, 1.8*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  
  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,0,0)*angle;
  translation = bdifd_vector_3d (5,10,5)*un;
  bdifd_analytic:: helix_curve( un, un/2, translation,crv_tmp, theta, 0, stepsize_helix, 360*10);
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,-1,-1);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(0.5*un, 3*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(5,5,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(un, 1.8*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*7);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-3,-7)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Space curve 1
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 2*un, translation, crv_tmp, theta, 0, stepsize_curve1, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

//  translation = bdifd_vector_3d (-3,5,-5)*un;
//  bdifd_analytic::space_curve1( 4*un, translation, crv_tmp, theta, 0, stepsize_curve1, 360);
//  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5,-5,12)*un;
  bdifd_analytic::space_curve1( 10*un, translation, crv_tmp, theta, 60, stepsize_curve1, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 5*un, translation, crv_tmp, theta, 0, stepsize_curve1, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,-1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

}

//: there are additions of small values to "translations"; these are to avoid
//degenerate cases where we output two exactly equal 3D points. 
void bdifd_data::
space_curves_olympus_turntable(
    std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d
    )
{
  std::vector<double> theta;
  bdifd_vector_3d translation;
  bdifd_vector_3d direction;
  std::vector<bdifd_3rd_order_point_3d > crv_tmp;
  bdifd_vector_3d axis;
  double angle;

  double l=80; //:< length of cube
  double un = l/20.; //(so cube goes from -10*un to 10*un)

  double stepsize_lines =un/5.;

//  double stepsize_circle_arclength=2;
//  double stepsize_ellipse_arclength=2;
  double stepsize_circle_arclength =stepsize_lines;
  double stepsize_ellipse_arclength=stepsize_circle_arclength;
  double radius;
  double ra, rb;
  double stepsize_ellipse=2;
  double stepsize_circle=10;

  double stepsize_helix=5;
  double stepsize_curve1=0.6;

  { // Basic shapes to define volume where curves are to be drawn
    translation = bdifd_vector_3d (0,0,0);
    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    radius = 1.0;
    stepsize_circle = stepsize_circle_arclength/radius;
    stepsize_circle *= 180.0/vnl_math::pi;
    translation = translation + bdifd_vector_3d(1e-5,5e-5,1e-5);
    bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 0, stepsize_circle, 360);
    crv3d.push_back(crv_tmp); crv_tmp.clear();

    
    bdifd_vector_3d t_cube = bdifd_vector_3d(-l/2,-l/2,-l/2);
    translation = translation + t_cube;
    //: Cube
    direction = bdifd_vector_3d (1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,0,0)+t_cube;

    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(1e-6,1e-5,1e-6);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,l,0) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(2e-5,7e-6,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(2e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,0,l) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(4e-6,1e-5,0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,2e-6,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,l,l) + t_cube;

    direction = bdifd_vector_3d (-1,0, 0);
    translation = translation + bdifd_vector_3d(4e-6,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,-1, 0);
    translation = translation + bdifd_vector_3d(4e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, -1);
    translation = translation - bdifd_vector_3d(5e-4,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 
  }

  translation = bdifd_vector_3d (6,6,-2)*un;
  direction = bdifd_vector_3d(5,5, 9)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 10*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5.82,-5,-9)*un;
  direction = bdifd_vector_3d (0,1, 3)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 15*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 0.5*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(-6,-2,0)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();


  radius = 1.5*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (5,2.5, 9)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  {
  translation = bdifd_vector_3d(8,-5,0)*un;

  radius = 1*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 1*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  }

  radius = 2*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(7,7,5)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 1.9*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(7,6.7,-5)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 3*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 60, stepsize_circle, 120);
  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-7,3)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Ellipses
  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (-6,-6,-7)*un;
  bdifd_analytic::ellipse(ra, rb,translation, crv_tmp, theta, 60, stepsize_ellipse, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (9,0,-3)*un;
  bdifd_analytic::ellipse(ra, rb,translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = 3*un;
  rb = un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = un;
  rb = 0.5*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 0, stepsize_ellipse, 280);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-8,6,+8)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = 4*un;
  rb = un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,-1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,8,+5)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Helices
  translation = bdifd_vector_3d (-9,-9,0)*un;
  bdifd_analytic:: helix_curve( 0.5*un, 2*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,0,0)*angle;
  translation = bdifd_vector_3d (5,10,5)*un;
  bdifd_analytic:: helix_curve( un, un/1.5, translation,crv_tmp, theta, 0, stepsize_helix, 360*10);
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,-1,-1);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(0.5*un, 6*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(5,5,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(un, 2.5*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*7);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-3,-7)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Space curve 1
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 2*un, translation, crv_tmp, theta, 0, 3*stepsize_curve1, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-3,5,-5)*un;
  translation = translation + bdifd_vector_3d(4e-6,1e-5,3e-5);
  bdifd_analytic::space_curve1( 4*un, translation, crv_tmp, theta, 0, 2*stepsize_curve1, 359);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5,-5,12)*un;
  bdifd_analytic::space_curve1( 10*un, translation, crv_tmp, theta, 60, stepsize_curve1, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 5*un, translation, crv_tmp, theta, 0, stepsize_curve1, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,-1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
}

//: there are additions of small values to "translations"; these are to try to avoid
//degenerate cases where we output two exactly equal 3D points. 
void bdifd_data::
space_curves_digicam_turntable_sandbox(
    std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d
    )
{
  std::vector<double> theta;
  bdifd_vector_3d translation;
  bdifd_vector_3d direction;
  std::vector<bdifd_3rd_order_point_3d > crv_tmp;
  bdifd_vector_3d axis;
  double angle;



  double l=80; //:< length of cube
  double un = l/20.; //(so cube goes from -10*un to 10*un)

  double stepsize_lines =un/5.;

//  double stepsize_circle_arclength=2;
  double stepsize_ellipse_arclength=2;
  double stepsize_circle_arclength =stepsize_lines;
//  double stepsize_ellipse_arclength=stepsize_circle_arclength;
  double radius;
  double ra, rb;
  double stepsize_ellipse=2;
  double stepsize_circle=10;


//  double stepsize_helix=5;
//  double stepsize_curve1=0.6;


  { // Basic shapes to define volume where curves are to be drawn
    translation = bdifd_vector_3d (0,0,0);
  /*
    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 


    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    radius = 1.0;
    stepsize_circle = stepsize_circle_arclength/radius;
    stepsize_circle *= 180.0/vnl_math::pi;
    translation = translation + bdifd_vector_3d(1e-5,5e-5,1e-5);
    bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 0, stepsize_circle, 360);
    crv3d.push_back(crv_tmp); crv_tmp.clear();

    
    */
    bdifd_vector_3d t_cube = bdifd_vector_3d(-l/2,-l/2,-l/2);
    /*
    translation = translation + t_cube;
    //: Cube
    direction = bdifd_vector_3d (1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,0,0)+t_cube;

    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(1e-6,1e-5,1e-6);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,l,0) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(2e-5,7e-6,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(2e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    */
    translation = bdifd_vector_3d (0,0,l) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(4e-6,1e-5,0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    /*
    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,2e-6,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,l,l) + t_cube;

    direction = bdifd_vector_3d (-1,0, 0);
    translation = translation + bdifd_vector_3d(4e-6,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,-1, 0);
    translation = translation + bdifd_vector_3d(4e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, -1);
    translation = translation - bdifd_vector_3d(5e-4,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 
    */
  }

  /*
  translation = bdifd_vector_3d (6,6,-2)*un;
  direction = bdifd_vector_3d(5,5, 9)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 10*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5.82,-5,-9)*un;
  direction = bdifd_vector_3d (0,1, 3)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 15*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

    /*
  radius = 0.5*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(-6,-2,0)*un;
//  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 90);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */


  /*
  radius = 1.5*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (5,2.5, 9)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  /*
  {
  translation = bdifd_vector_3d(8,-5,0)*un;

  radius = 1*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 1*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  }

  radius = 2*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(7,7,5)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 1.9*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(7,6.7,-5)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 3*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 60, stepsize_circle, 120);
  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-7,3)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  // Ellipses
  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (-6,-6,-7)*un;
  bdifd_analytic::ellipse(ra, rb,translation, crv_tmp, theta, 60, stepsize_ellipse, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  /*
  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (9,0,-3)*un;
  bdifd_analytic::ellipse(ra, rb,translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = 3*un;
  rb = un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = un;
  rb = 0.5*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 0, stepsize_ellipse, 280);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-8,6,+8)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  ra = 4*un;
  rb = un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,-1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,8,+5)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  /*

  // Helices
  translation = bdifd_vector_3d (-9,-9,0)*un;
  bdifd_analytic:: helix_curve( 0.5*un, 2*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,0,0)*angle;
  translation = bdifd_vector_3d (5,10,5)*un;
  bdifd_analytic:: helix_curve( un, un/1.5, translation,crv_tmp, theta, 0, stepsize_helix, 360*10);
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,-1,-1);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(0.5*un, 6*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(5,5,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(un, 2.5*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*7);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-3,-7)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  // Space curve 1
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 2*un, translation, crv_tmp, theta, 0, 3*stepsize_curve1, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-3,5,-5)*un;
  translation = translation + bdifd_vector_3d(4e-6,1e-5,3e-5);
  bdifd_analytic::space_curve1( 4*un, translation, crv_tmp, theta, 0, 2*stepsize_curve1, 359);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5,-5,12)*un;
  bdifd_analytic::space_curve1( 10*un, translation, crv_tmp, theta, 60, stepsize_curve1, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 5*un, translation, crv_tmp, theta, 0, stepsize_curve1, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,-1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */
}

//: there are additions of small values to "translations"; these are to avoid
//degenerate cases where we output two exactly equal 3D points. 
void bdifd_data::
space_curves_digicam_turntable_medium_sized(
    std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d
    )
{
  std::vector<double> theta;
  bdifd_vector_3d translation;
  bdifd_vector_3d direction;
  std::vector<bdifd_3rd_order_point_3d > crv_tmp;
  bdifd_vector_3d axis;
  double angle;



  double l=80; //:< length of cube
  double un = l/20.; //(so cube goes from -10*un to 10*un)

  double stepsize_lines =un/7.;

//  double stepsize_circle_arclength=2;
//  double stepsize_ellipse_arclength=2;
  double stepsize_circle_arclength =stepsize_lines;
  double stepsize_ellipse_arclength=stepsize_circle_arclength;
  double radius;
  double ra, rb;
  double stepsize_ellipse=2.5;
  double stepsize_circle=10;


  double stepsize_helix=5;
  double stepsize_curve1=1;


  /*
  { // Basic shapes to define volume where curves are to be drawn
    translation = bdifd_vector_3d (0,0,0);
    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, 1, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    radius = 1.0;
    stepsize_circle = stepsize_circle_arclength/radius;
    stepsize_circle *= 180.0/vnl_math::pi;
    translation = translation + bdifd_vector_3d(1e-5,5e-5,1e-5);
    bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 0, stepsize_circle, 360);
    crv3d.push_back(crv_tmp); crv_tmp.clear();

    
    bdifd_vector_3d t_cube = bdifd_vector_3d(-l/2,-l/2,-l/2);
    translation = translation + t_cube;
    //: Cube
    direction = bdifd_vector_3d (1,0, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation - bdifd_vector_3d(2e-5,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,0,0)+t_cube;

    direction = bdifd_vector_3d (0,1, 0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(1e-6,1e-5,1e-6);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,l,0) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(2e-5,7e-6,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, 1);
    translation = translation + bdifd_vector_3d(2e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (0,0,l) + t_cube;

    direction = bdifd_vector_3d (1,0, 0);
    translation = translation + bdifd_vector_3d(4e-6,1e-5,0);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,1, 0);
    translation = translation + bdifd_vector_3d(1e-5,2e-6,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    // ----
    translation = bdifd_vector_3d (l,l,l) + t_cube;

    direction = bdifd_vector_3d (-1,0, 0);
    translation = translation + bdifd_vector_3d(4e-6,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,-1, 0);
    translation = translation + bdifd_vector_3d(4e-5,1e-5,1e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 

    direction = bdifd_vector_3d (0,0, -1);
    translation = translation - bdifd_vector_3d(5e-4,2e-5,2e-5);
    bdifd_analytic::line(translation, direction, crv_tmp, theta, l, stepsize_lines);
    crv3d.push_back(crv_tmp); crv_tmp.clear(); 
  }

  translation = bdifd_vector_3d (6,6,-2)*un;
  direction = bdifd_vector_3d(5,5, 9)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 10*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  */
  translation = bdifd_vector_3d (-5.82,-5,-9)*un;
  direction = bdifd_vector_3d (0,1, 3)*un;
  bdifd_analytic::line(translation, direction, crv_tmp, theta, 15*un, stepsize_lines);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 0.5*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(-6,-2,0)*un;
//  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 90);
  crv3d.push_back(crv_tmp); crv_tmp.clear();


  /*
  radius = 1.5*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (5,2.5, 9)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 90, stepsize_circle, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  /*
  {
  translation = bdifd_vector_3d(8,-5,0)*un;

  radius = 1*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 1*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  }

  radius = 2*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(7,7,5)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, -89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 1.9*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d(7,6.7,-5)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 89, stepsize_circle, 175);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  radius = 3*un;
  stepsize_circle = stepsize_circle_arclength/radius;
  stepsize_circle *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::circle_curve( radius, translation, crv_tmp, theta, 60, stepsize_circle, 120);
  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-7,3)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  // Ellipses
  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (-6,-6,-7)*un;
  bdifd_analytic::ellipse(ra, rb,translation, crv_tmp, theta, 60, stepsize_ellipse, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  /*
  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (9,0,-3)*un;
  bdifd_analytic::ellipse(ra, rb,translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = un;
  rb = 4*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  ra = 3*un;
  rb = un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 30, stepsize_ellipse, 180);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(0,1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(7,-4,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  /*
  ra = un;
  rb = 0.5*un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 0, stepsize_ellipse, 280);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-8,6,+8)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  ra = 4*un;
  rb = un;
  stepsize_ellipse = stepsize_ellipse_arclength/std::max(ra,rb);
  stepsize_ellipse *= 180.0/vnl_math::pi;
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::ellipse(ra, rb, translation, crv_tmp, theta, 0, stepsize_ellipse, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,-1,0);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,8,+5)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  /*
  // Helices
  translation = bdifd_vector_3d (-9,-9,0)*un;
  bdifd_analytic:: helix_curve( 0.5*un, 2*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  /*
  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,0,0)*angle;
  translation = bdifd_vector_3d (5,10,5)*un;
  bdifd_analytic:: helix_curve( un, un/1.5, translation,crv_tmp, theta, 0, stepsize_helix, 360*10);
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  angle = vnl_math::pi/2;
  axis  = bdifd_vector_3d(1,-1,-1);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(0.5*un, 6*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*5);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(5,5,-10)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  /*
  angle = vnl_math::pi/4;
  axis  = bdifd_vector_3d(1,1,0);
  axis.normalize();
  axis = axis*angle;
  translation = bdifd_vector_3d(0,0,0)*un;
  bdifd_analytic::helix_curve(un, 2.5*un, translation,crv_tmp, theta, 0, stepsize_helix, 360*7);
  bdifd_analytic::rotate(crv_tmp,axis);
  translation = bdifd_vector_3d(-5,-3,-7)*un;
  bdifd_analytic::translate(crv_tmp,translation);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

  // Space curve 1
  /*
  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 2*un, translation, crv_tmp, theta, 0, 3*stepsize_curve1, 360);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
  */

//  translation = bdifd_vector_3d (-3,5,-5)*un;
//  translation = translation + bdifd_vector_3d(4e-6,1e-5,3e-5);
//  bdifd_analytic::space_curve1( 4*un, translation, crv_tmp, theta, 0, 2*stepsize_curve1, 359);
//  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (-5,-5,12)*un;
  bdifd_analytic::space_curve1( 10*un, translation, crv_tmp, theta, 60, stepsize_curve1, 120);
  crv3d.push_back(crv_tmp); crv_tmp.clear();

  translation = bdifd_vector_3d (0,0,0)*un;
  bdifd_analytic::space_curve1( 5*un, translation, crv_tmp, theta, 0, stepsize_curve1, 360);
  angle = vnl_math::pi/3;
  axis  = bdifd_vector_3d(1,1,-1);
  axis.normalize();
  axis = axis*angle;
  bdifd_analytic::rotate(crv_tmp,axis);
  crv3d.push_back(crv_tmp); crv_tmp.clear();
}



// To see how the system is modeled, check out my notes "Rewriting Geometry jan
// 11"
vpgl_perspective_camera<double> * bdifd_turntable::
camera_ctspheres(
    unsigned frm_index,
    const vpgl_calibration_matrix<double> &K)
{

  //--- Determining R and t (extrinsic params)
  //  double camera_to_object = camera_to_source - object_to_source;
  double const object_to_source = 121.00;
  double const r = object_to_source;
  double rotation_step = 0.5; // (deg) artifact 2
  //  double rotation_step = 0.440; // artifact 1

  rotation_step *= (vnl_math::pi/180.0);

  // -- Camera
  double const theta = frm_index*rotation_step;

  vnl_matrix_fixed< double, 3, 3 > R;
  R[0][0] = std::cos(theta);
  R[0][1] = 0;
  R[0][2] = -std::sin(theta);

  R[1][0] = 0;
  R[1][1] = 1;
  R[1][2] = 0;

  R[2][0] = std::sin(theta);
  R[2][1] = 0;
  R[2][2] = std::cos(theta);
  

  vnl_vector_fixed<double,3> dummy0(0,0,0.0);
  vgl_h_matrix_3d<double> Rhmg(R,dummy0);

  // World origin is at center of rotation or fixation point.
  vgl_homg_point_3d<double> transl(-r*std::sin(theta), 0, -r*std::cos(theta));


  return new vpgl_perspective_camera<double> (K, transl, vgl_rotation_3d<double>(Rhmg));
}


// \param[out] m : internal 3x3 calibration matrix
// \param[in] x_max_scaled: number of columns of image; currently aspect ratio
// is equal to the original CT Sphere data's aspect ratio (4000x2096)
void bdifd_turntable::
internal_calib_ctspheres(vnl_double_3x3 &m, double x_max_scaled)
{

  double const x_over_y_ratio=0.9830;
  double const camera_pixel_size = 0.01168 ; // in mm pixel size (diagonal??)

  double const camera_to_source = 161.00; 

  double const optical_axis_line = 980.0; /*artifact 2*/
  double const nx = 4000.0; /* number of cols (artifact 2)*/
//  double const ny = 2096.0; /* number of rows (artifact 2)*/

  // If we are working with scaled down images (same aspect ratio), 
  // the following factor scale = x_max_scaled/x_max_orig = y_max_scaled/y_max_orig
//  double const x_max_scaled = 600;
  double const scale = (x_max_scaled-1)/(nx-1); 
//  double const scale=1.0;

//  double const optical_axis_line = 490.0; /*artifact 1*/
//  double const nx = 1936; /* number of cols (artifact 1)*/
//  double const ny = 1048; /* number of rows (artifact 1)*/


  double ysize = camera_pixel_size/sqrt(x_over_y_ratio*x_over_y_ratio + 1);
  double xsize = x_over_y_ratio*ysize;

  // focal length and pixel unit conversion
  m[0][0]=scale*camera_to_source/xsize;
  m[1][1]=scale*camera_to_source/ysize;

  // principal points:
  m[0][2]=scale*nx/2.0;
  m[1][2]=scale*optical_axis_line;

  m[0][1]=0;   
  m[1][0]=0;   
  m[2][0]=0;   
  m[2][1]=0;   
  m[2][2]=1;
}


//: From david statue dataset 02-26-2006; Calib_Results.mat + base_extrinsics_rect.mat
void bdifd_turntable::
internal_calib_olympus(vnl_double_3x3 &m, double x_max_scaled, unsigned  crop_x, unsigned  crop_y)
{
//  double const nx = 4000.0; /* number of cols (artifact 2)*/
//  double const ny = 2096.0; /* number of rows (artifact 2)*/

  // If we are working with scaled down images (same aspect ratio), 
  // the following factor scale = x_max_scaled/x_max_orig = y_max_scaled/y_max_orig
//  double const x_max_scaled = 600;


//  double focal_length;
//  
//  double mm_per_pixel_x;
//  double mm_per_pixel_y;

  // KK(1:2,3) = KK(1:2,3) - crop_rect(:,1);
  // Principal point is (0.86019451514668e3, 1.41278081044646e3)
  // Obtained from calib toolbox and used here as a ref of real cam.
  //
  // This is approx (860,1412) so dimensions of image is:
  // is 2*[0.86019451514668e3, 1.41278081044646e3] = 1720  2826

  double principal_point_x = 0.86019451514668e3- crop_x;
  double principal_point_y = 1.41278081044646e3- crop_y;

  double scale; 
  if (x_max_scaled==0)
    scale = 1;
  else
    scale = (x_max_scaled-1)/(2*principal_point_x-1); 

  double skew=0;

  m[0][0] = scale*4.76264247209766e3;
  m[0][1] = scale*skew;
  m[0][2] = scale*principal_point_x;

  m[1][0] = 0;
  m[1][1] = scale*4.76238333112727e3;
  m[1][2] = scale*principal_point_y;

  m[2][0] = 0;
  m[2][1] = 0;
  m[2][2] = 1;

}

//: \param[in] theta : rotation angle in degrees
vpgl_perspective_camera<double> * bdifd_turntable::
camera_olympus(
    double theta,
    const vpgl_calibration_matrix<double> &K)
{

  double camera_to_object = 1.128036301860739e+03;
  bdifd_vector_3d Tckk = camera_to_object * bdifd_vector_3d(0, 0.12722239600987, 0.99187421680045);

  double roll = -3.961405930732378e-15;
  double pitch = -3.02282467212289;
  double yaw = -1.15081788134986;

  vnl_double_3x3 Rx, Ry, Rz;

  Rx[0][0] = 1          ;
  Rx[0][1] = 0          ;
  Rx[0][2] = 0          ;
               
  Rx[1][0] = 0          ;
  Rx[1][1] = std::cos(pitch) ;
  Rx[1][2] = -std::sin(pitch);
               
  Rx[2][0] = 0          ;
  Rx[2][1] = std::sin(pitch) ;
  Rx[2][2] = std::cos(pitch) ;

  Ry[0][0] = std::cos(yaw) ;
  Ry[0][1] = 0        ;
  Ry[0][2] = std::sin(yaw) ;
                        
  Ry[1][0] = 0        ;
  Ry[1][1] = 1        ;
  Ry[1][2] = 0        ;
                        
  Ry[2][0] = -std::sin(yaw); 
  Ry[2][1] = 0        ;
  Ry[2][2] = std::cos(yaw) ;

  Rz[0][0] =std::cos(roll) ;
  Rz[0][1] =-std::sin(roll);
  Rz[0][2] =0         ;
                       
  Rz[1][0] =std::sin(roll) ;
  Rz[1][1] =std::cos(roll) ;
  Rz[1][2] =0         ;
                       
  Rz[2][0] =0         ;
  Rz[2][1] =0         ;
  Rz[2][2] =1         ;


  //: Got the following from david-02-26-2006-crop2 Rckk normalized as in
  // Mundy's notes.
  vnl_double_3x3 Rckk;
  Rckk = Rx*Ry*Rz;
//  Rckk[0][0] =   4.077407717391018371E-01  ;
//  Rckk[0][1] =   2.058618136737014769E-15  ;
//  Rckk[0][2] =   - 9.130977291952939723E-01;
//                                           
//  Rckk[1][0] =   1.081919985807717616E-01  ;
//  Rckk[1][1] =   - 9.929553699976120251E-01;
//  Rckk[1][2] =   4.831277922046327278E-02  ;
//                                           
//  Rckk[2][0] = - 9.066652935370919097E-01  ;
//  Rckk[2][1] = - 1.184889581054157787E-01  ;
//  Rckk[2][2] = - 4.048683888653117346E-01  ;

  theta *= -vnl_math::pi / 180.0;

  // R_cam1_to_cam2 = dR^(i);

  vnl_double_3x3 Rot_theta;

  Rot_theta[0][0] = std::cos(theta);
  Rot_theta[0][1] = 0;
  Rot_theta[0][2] = -std::sin(theta);

  Rot_theta[1][0] = 0;
  Rot_theta[1][1] = 1;
  Rot_theta[1][2] = 0;

  Rot_theta[2][0] = std::sin(theta);
  Rot_theta[2][1] = 0;
  Rot_theta[2][2] = std::cos(theta);

  vnl_double_3x3 R_W0_to_Wtheta = Rot_theta.transpose();

  vnl_double_3x3 R_world_to_cam2 =  Rckk * R_W0_to_Wtheta;


  bdifd_vector_3d C2_in_world = - Rot_theta * Rckk.transpose() * Tckk;

  vgl_point_3d<double> C2_in_world_vgl(C2_in_world[0], C2_in_world[1], C2_in_world[2]);

  vgl_h_matrix_3d<double> Rhmg(R_world_to_cam2,bdifd_vector_3d(0,0,0));

  return new vpgl_perspective_camera<double>(K, C2_in_world_vgl, vgl_rotation_3d<double>(Rhmg));
}

void bdifd_turntable::
cameras_olympus_spherical(
  std::vector<vpgl_perspective_camera<double> > *pcams,
  const vpgl_calibration_matrix<double> &K,
  bool enforce_minimum_separation,
  bool perturb)
{
  typedef boost::random::mt19937 gen_type;
  std::vector<vpgl_perspective_camera<double> > &cams = *pcams;
  unsigned nviews=100;
  double minsep = (15./180.)*vnl_math::pi;
  assert(nviews != 0);

  // You can seed this your way as well, but this is my quick and dirty way.
  gen_type rand_gen;
  rand_gen.seed(static_cast<unsigned int>(std::time(0)));

  // Create the distribution object.
  boost::uniform_on_sphere<double> unif_sphere(3);
  boost::normal_distribution<double> nd(0.0, 1);

  // This is what will actually supply drawn values.
  boost::variate_generator<gen_type&, boost::uniform_on_sphere<double> > random_on_sphere(rand_gen, unif_sphere);
  boost::variate_generator<gen_type&, boost::uniform_on_sphere<double> > random_on_sphere_2(rand_gen, unif_sphere);
  boost::variate_generator<gen_type&, boost::normal_distribution<double> > random01(rand_gen, nd);
  
  
//  std::string dir("./out-tmp");
//  vul_file::make_directory(dir);
//  std::string fname_centers = dir + std::string("/") + "C.txt";
//  std::ofstream fp_centers;

//  fp_centers.open(fname_centers.c_str());
//  if (!fp_centers) {
//    std::cerr << "generate_synth_sequence: error, unable to open file name " << fname_centers << std::endl;
//    return;
//  }
//  fp_centers << std::setprecision(20);
//  fp_centers << r[0] << " " << r[1]  << " " << r[2] << std::endl;
  
  unsigned i=0;
  unsigned ntrials=0;
  do {
      // Now you can draw a vector of drawn coordinates as such:
      std::vector<double> r = random_on_sphere();
      bdifd_vector_3d z(-r[0],-r[1],-r[2]);
      
      if (perturb) {
        std::cout << "z before " << z << std::endl;
        z += bdifd_vector_3d(0.01*random01(),0.01*random01(),0.01*random01());
        z.normalize(); 
        std::cout << "z after " << z << std::endl;
      }
      
      if (enforce_minimum_separation) {
        ntrials++;
        bool need_resample = false;
        for (unsigned j=0; j < cams.size() && !need_resample; ++j) {
          vgl_point_3d<double> C_tmp = cams[j].get_camera_center();
          bdifd_vector_3d C_vector_tmp(C_tmp.x(),C_tmp.y(), C_tmp.z());
          
//          std::cout << "Angle: " << angle(-z,C_vector_tmp) << " Minsep: " << minsep << std::endl;
//          std::cout << "ntrials " << ntrials << std::endl;
          double angle_distance_along_unit_sphere = angle(-z,C_vector_tmp);
          assert(angle_distance_along_unit_sphere < vnl_math::pi);
          if (angle_distance_along_unit_sphere < minsep || vnl_math::pi - angle_distance_along_unit_sphere < minsep ) {
            if (ntrials > 100000) {
              std::cerr << "PROBLEM: Unable to reduce separation\n";
              break;
            } else {
//              std::cerr << "Trying again" << std::endl;
              need_resample = true;
            }
          }
        }
        if (need_resample)
          continue;
      }
      
//      std::cerr << "YES!" << std::endl;
      ntrials = 0;
      
      double camera_to_object = 1.128036301860739e+03;
      if (perturb)
        camera_to_object += random01()*10;

      std::cout << "Cam to object: " << camera_to_object << std::endl;
      
      vgl_point_3d<double> C(camera_to_object*r[0],camera_to_object*r[1],camera_to_object*r[2]);

      // x direction obtained by sampling 3D unit vector again, and orthogonalizing
      // if direction is to be kept, can do this with orthogonalizing usual x
      // dir
      r = random_on_sphere_2();
      bdifd_vector_3d x;
      x[0] = r[0];
      x[1] = r[1];
      x[2] = r[2];

      x = x - (dot_product(x,z))*z;
      x.normalize();

      bdifd_vector_3d y = vnl_cross_3d(z,x);
      vnl_double_3x3 R;
      
      R[0][0] = x[0];
      R[0][1] = x[1];
      R[0][2] = x[2];
      
      R[1][0] = y[0];
      R[1][1] = y[1];
      R[1][2] = y[2];
      
      R[2][0] = z[0];
      R[2][1] = z[1];
      R[2][2] = z[2];
      
      vgl_h_matrix_3d<double> Rhmg(R,bdifd_vector_3d(0,0,0));
      assert(Rhmg.is_euclidean());
      cams.push_back(vpgl_perspective_camera<double>(K, C, vgl_rotation_3d<double>(Rhmg)));
      ++i;
  } while (i < nviews);
}

//: convert from std::vector<bdifd_3rd_order_point_2d> 
// to std::vector<vsol_line_2d_sptr>  and perturb if wanted
void bdifd_data::
get_lines(
    std::vector<vsol_line_2d_sptr> &lines,
    const std::vector<bdifd_3rd_order_point_2d> &C_subpixel,
    bool do_perturb,
    double pert_pos,
    double pert_tan
    )
{
  lines.resize(C_subpixel.size());
  for (unsigned i=0; i<C_subpixel.size(); i++) {
    vgl_vector_2d<double> tan(C_subpixel[i].t[0], C_subpixel[i].t[1]);
    vsol_point_2d_sptr middle = new vsol_point_2d(C_subpixel[i].gama[0], C_subpixel[i].gama[1]);

    if (do_perturb) {
      middle->set_x(bdifd_analytic::perturb(middle->x(),pert_pos));
      middle->set_y(bdifd_analytic::perturb(middle->y(),pert_pos));
      bdifd_analytic::perturb(tan, pert_tan);
    }
    vsol_line_2d_sptr line = new vsol_line_2d(tan,middle);
    lines[i] = line;
  }
}

//: \param[in] dpos : how much to perturb position
//: \param[in] dtan : how much to perturb orientation (deg)
void bdifd_data::
get_circle_edgels(
    double radius, 
    std::vector<vsol_line_2d_sptr> &lines,
    std::vector<bdifd_3rd_order_point_2d> &C_subpixel,
    bool do_perturb,
    double pert_pos,
    double pert_tan
    )
{
  // transl. big enough so all coordinates are positive
  bdifd_vector_2d translation(radius,radius);

  double dtheta = (std::asin(std::sqrt(2.)/(2.*radius)) );

  dtheta *= (180.0/vnl_math::pi);

  if (do_perturb) {
    pert_tan *= (vnl_math::pi/180.0);
  }

  std::vector<double> theta;
  std::vector<bdifd_3rd_order_point_2d> C;
  bdifd_analytic::circle_curve( radius, translation, C, theta, 0, dtheta, 360);

  bdifd_analytic::limit_distance(C, C_subpixel);

  bdifd_data::get_lines(lines, C_subpixel, do_perturb, pert_pos, pert_tan);
}


//: \param[in] dpos : how much to perturb position
//: \param[in] dtan : how much to perturb orientation (deg)
void bdifd_data::
get_ellipse_edgels(
    double ra, 
    double rb, 
    std::vector<vsol_line_2d_sptr> &lines,
    std::vector<bdifd_3rd_order_point_2d> &C_subpixel,
    bool do_perturb,
    double pert_pos,
    double pert_tan
    )
{
  // transl. big enough so all coordinates are positive
  bdifd_vector_2d translation(ra,rb);

  double dtheta = (std::asin(std::sqrt(2.)/(2.*std::max(ra,rb))) );
  dtheta /= 10.0; // XXX

  dtheta *= (180.0/vnl_math::pi);

  if (do_perturb) {
    pert_tan *= (vnl_math::pi/180.0);
  }

  std::vector<double> theta;
  std::vector<bdifd_3rd_order_point_2d> C;
  bdifd_analytic::ellipse(ra, rb, translation, C, theta, 0, dtheta, 360);

  std::cout << "Before limit distance: " << C.size() << std::endl;
  bdifd_analytic::limit_distance(C, C_subpixel);
  std::cout << "After limit distance: " << C_subpixel.size() << std::endl;

  bdifd_data::get_lines(lines, C_subpixel, do_perturb, pert_pos, pert_tan);
}

vgl_point_3d<double> bdifd_data::
get_point_crv3d(const std::vector<std::vector<bdifd_3rd_order_point_3d> > &crv3d, unsigned i)
{

  unsigned idx=0;
  for (unsigned ic=0; ic < crv3d.size(); ++ic) {
    for (unsigned ip=0; ip < crv3d[ic].size(); ++ip) {
      if (idx == i)
        return vgl_point_3d<double>(crv3d[ic][ip].Gama[0],crv3d[ic][ip].Gama[1],crv3d[ic][ip].Gama[2]);
      ++idx;
    }
  }
  std::cerr << "Invalid index\n";
  return vgl_point_3d<double>();
}

// Get cameras and points in traditional format, i.e. with perspective projection cameras and no
// differential geometry.
//
// \param[out] pimage_pts :  pointer to a std::vector v where v[view_id][point_id]. Corresponding
// points have the same point_id, which is also the id of the corresponding 3D point into world_pts.
//
// \param[out] world_pts : std::vector of 3D points
//
// \param[out] cams : perspective cameras for each view v. These are turntable views at angle given
// by view_angles[i].
//
// \param[in] view_angles : the angle of each view, in degrees.
//
void bdifd_data::
get_digital_camera_point_dataset(
    std::vector<vpgl_perspective_camera<double> > *pcams, 
    std::vector<std::vector<vgl_point_2d<double> > > *pimage_pts, 
    std::vector<vgl_point_3d<double> > *pworld_pts, 
    const std::vector<double> &view_angles)
{
  // aliases
  std::vector<vpgl_perspective_camera<double> > &cams = *pcams;
  std::vector<std::vector<vgl_point_2d<double> > > &image_pts = *pimage_pts; 
  std::vector<vgl_point_3d<double> > &world_pts = *pworld_pts;


  unsigned  crop_origin_x = 450;
  unsigned  crop_origin_y = 1750;
  double x_max_scaled = 500;

  vnl_double_3x3 Kmatrix;
  bdifd_turntable::internal_calib_olympus(Kmatrix, x_max_scaled, crop_origin_x, crop_origin_y);
  vpgl_calibration_matrix<double> K(Kmatrix);

  for (unsigned v=0; v < view_angles.size(); ++v) {
    vpgl_perspective_camera<double> *P;
    P = bdifd_turntable::camera_olympus(view_angles[v], K);
    cams.push_back(*P);
    delete P;
  }

  // Extracts list of 3D point positions
  {
  std::vector<std::vector<bdifd_3rd_order_point_3d> > crv3d;
  bdifd_data::space_curves_olympus_turntable( crv3d );


  unsigned npts = 0;
  for (unsigned ic=0; ic < crv3d.size(); ++ic)
    npts += crv3d[ic].size();

  world_pts.reserve(npts);
  for (unsigned ic=0; ic < crv3d.size(); ++ic)
    for (unsigned ip=0; ip < crv3d[ic].size(); ++ip)
      world_pts.push_back(vgl_point_3d<double>(crv3d[ic][ip].Gama[0], 
          crv3d[ic][ip].Gama[1], crv3d[ic][ip].Gama[2]));
  }

  // Now project into each image
  image_pts.resize(cams.size());
  for (unsigned v=0; v < cams.size(); ++v) {
    image_pts[v].resize(world_pts.size());
    for (unsigned ip=0; ip < world_pts.size(); ++ip)
      image_pts[v][ip] = cams[v].project(world_pts[ip]);
  }
}

