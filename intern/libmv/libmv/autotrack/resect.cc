// Copyright (c) 2011 libmv authors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include "libmv/autotrack/resect.h"

#include <cstdio>

#include "libmv/base/vector.h"
#include "libmv/logging/logging.h"
#include "libmv/multiview/euclidean_resection.h"
#include "libmv/multiview/resection.h"
#include "libmv/multiview/projection.h"
#include "libmv/numeric/numeric.h"
#include "libmv/numeric/levenberg_marquardt.h"
#include "libmv/autotrack/reconstruction.h"
#include "libmv/autotrack/tracks.h"

using libmv::Mat2X;
using libmv::Mat3X;
using libmv::Mat4X;
using libmv::Mat34;
using libmv::Vec;
using libmv::Vec6;

namespace mv {
namespace {

Mat2X PointMatrixFromMarkers(const vector<Marker> &markers) {
  Mat2X points(2, markers.size());
  for (int i = 0; i < markers.size(); ++i) {
    points(0, i) = markers[i].center[0];
    points(1, i) = markers[i].center[1];
  }
  return points;
}

// Uses an incremental rotation:
//
//   x = R' * R * X + t;
//
// to avoid issues with the rotation representation. R' is derived from a
// euler vector encoding the rotation in 3 parameters; the direction is the
// axis to rotate around and the magnitude is the amount of the rotation.
struct EuclideanResectCostFunction {
 public:
  typedef Vec  FMatrixType;
  typedef Vec6 XMatrixType;

  EuclideanResectCostFunction(const vector<Marker> &markers,
                              const Reconstruction &reconstruction,
                              const Mat3 &initial_R)
    : markers(markers),
      reconstruction(reconstruction),
      initial_R(initial_R) {}

  // dRt has dR (delta R) encoded as a euler vector in the first 3 parameters,
  // followed by t in the next 3 parameters.
  Vec operator()(const Vec6 &dRt) const {
    // Unpack R, t from dRt.
    Mat3 R = libmv::RotationFromEulerVector(dRt.head<3>()) * initial_R;
    Vec3 t = dRt.tail<3>();

    // Compute the reprojection error for each coordinate.
    Vec residuals(2 * markers.size());
    residuals.setZero();
    for (int i = 0; i < markers.size(); ++i) {
      const Point &point =
          *reconstruction.PointForTrack(markers[i].track);
      Vec3 projected = R * point.X + t;
      projected /= projected(2);
      residuals[2*i + 0] = projected(0) - markers[i].center[0];
      residuals[2*i + 1] = projected(1) - markers[i].center[1];
    }
    return residuals;
  }

  const vector<Marker> &markers;
  const Reconstruction &reconstruction;
  const Mat3 &initial_R;
};

}  // namespace

bool EuclideanResect(const vector<Marker> &markers,
                     Reconstruction *reconstruction,
                     bool final_pass,
                     int intrinsics) {
  if (markers.size() < 5) {		// five-point algorithm
    return false;
  }
  Mat2X points_2d = PointMatrixFromMarkers(markers);
  Mat3X points_3d(3, markers.size());
  for (int i = 0; i < markers.size(); i++) {
    points_3d.col(i) = reconstruction->PointForTrack(markers[i].track)->X;
  }
  LG << "Number of points for resect: " << points_2d.cols() << "\n";

  Mat3 R;
  Vec3 t;

  if (0 || !libmv::euclidean_resection::EuclideanResection(
                points_2d, points_3d, &R, &t,
                libmv::euclidean_resection::RESECTION_EPNP)) {
    LG << "[EuclideanResect] Euclidean resection failed\n";
    return false;

    if (!final_pass) return false;
    // Euclidean resection failed. Fall back to projective resection, which is
    // less reliable but better conditioned when there are many points.
    Mat34 P;
    Mat4X points_3d_homogeneous(4, markers.size());
    for (int i = 0; i < markers.size(); i++) {
      points_3d_homogeneous.col(i).head<3>() = points_3d.col(i);
      points_3d_homogeneous(3, i) = 1.0;
    }
    libmv::resection::Resection(points_2d, points_3d_homogeneous, &P);
    if ((P * points_3d_homogeneous.col(0))(2) < 0) {
      LG << "Point behind camera; switch sign.";
      P = -P;
    }

    Mat3 ignored;
    libmv::KRt_From_P(P, &ignored, &R, &t);

    // The R matrix should be a rotation, but don't rely on it.
    Eigen::JacobiSVD<Mat3> svd(R, Eigen::ComputeFullU | Eigen::ComputeFullV);

    LG << "Resection rotation is: " << svd.singularValues().transpose();
    LG << "Determinant is: " << R.determinant();

    // Correct to make R a rotation.
    R = svd.matrixU() * svd.matrixV().transpose();

    Vec3 xx = R * points_3d.col(0) + t;
    if (xx(2) < 0.0) {
      LG << "Final point is still behind camera...";
    }
    // XXX Need to check if error is horrible and fail here too in that case.
  }

  // Refine the result.
  typedef libmv::LevenbergMarquardt<EuclideanResectCostFunction> Solver;

  // Give the cost our initial guess for R.
  EuclideanResectCostFunction resect_cost(markers, *reconstruction, R);

  // Encode the initial parameters: start with zero delta rotation, and the
  // guess for t obtained from resection.
  Vec6 dRt = Vec6::Zero();
  dRt.tail<3>() = t;

  Solver solver(resect_cost);

  Solver::SolverParameters params;
  /* Solver::Results results = */ solver.minimize(params, &dRt);
  VLOG(1) << "LM found incremental rotation: " << dRt.head<3>().transpose();
  // TODO(keir): Check results to ensure clean termination.

  // Unpack the rotation and translation.
  R = libmv::RotationFromEulerVector(dRt.head<3>()) * R;
  t = dRt.tail<3>();

  VLOG(1) << "Resection for frame " << markers[0].clip << " " << markers[0].frame
     << " got:\n" << "R:\n" << R << "\nt:\n" << t << "\n";
  CameraPose pose(markers[0].clip, markers[0].frame, intrinsics, R, t);
  reconstruction->AddCameraPose(pose);
  return true;
}

}  // namespace libmv