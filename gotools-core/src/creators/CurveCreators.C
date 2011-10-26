//===========================================================================
//                                                                           
// File: CurveCreators.C                                                   
//                                                                           
// Created: Mon Aug  6 14:34:56 2001                                         
//                                                                           
// Author: Sverre Briseid <Sverre.Briseid@math.sintef.no>
//                                                                           
// Revision: $Id: CurveCreators.C,v 1.6 2009-05-13 07:30:32 vsk Exp $
//                                                                           
// Description: Various SplineCurve creators.
//                                                                           
//===========================================================================


#include "GoTools/creators/CurveCreators.h"

#include "GoTools/geometry/SplineInterpolator.h"
#include "GoTools/geometry/SplineCurve.h"
#include "GoTools/geometry/SISLconversion.h"
#include "GoTools/geometry/SplineUtils.h"
#include "GoTools/geometry/Utils.h"
#include "GoTools/geometry/GoIntersections.h"
#include "GoTools/creators/ApproxCurve.h"
#include "GoTools/creators/ProjectCurve.h"
#include "GoTools/creators/HermiteAppC.h"
#include "GoTools/creators/HermiteAppS.h"
#include "GoTools/creators/LiftCurve.h"
#include "GoTools/creators/TrimCurve.h"
#include "GoTools/geometry/GeometryTools.h"
#include "GoTools/geometry/BoundedUtils.h"

#include <vector>
#include <algorithm>
#include <fstream>
#include <iterator>

#ifdef __BORLANDC__
#include <iterator>
#endif

#include "GoTools/geometry/Plane.h"

using namespace Go;
using std::shared_ptr;
using std::dynamic_pointer_cast;
using std::vector;
using std::max;
using std::min;


//===========================================================================
SplineCurve*
CurveCreators::multCurveWithFunction(const SplineCurve& alpha,
				       const SplineCurve& f)
//===========================================================================
{
    ALWAYS_ERROR_IF(alpha.dimension() != 1,
		    "Dimension of function is different from 1.");

    // Testing parameter interval.
    double start_par = alpha.startparam();
    double end_par = alpha.endparam();
    ALWAYS_ERROR_IF(f.startparam() != start_par || f.endparam() != end_par,
		    "Parameter intervals of the curves do not coincide.");


    // We start by constructing the needed knot vector.
    vector<double> new_alpha_knots, new_f_knots, knots;
    int i, j;
    std::vector<double>::const_iterator iter;
    for (iter = alpha.basis().begin(); iter < alpha.basis().end(); ++iter) {
	new_alpha_knots.push_back(iter[0]);
	// We must insert multiple knots for cont. reasons.
	if ((iter[0] < iter[1]) || (alpha.basis().end() - iter == 1))
	    for (i = 0; i < f.order() - 1; ++i)
		new_alpha_knots.push_back(iter[0]);
    }
    for (iter = f.basis().begin(); iter < f.basis().end(); ++iter) {
	new_f_knots.push_back(iter[0]);
	// We must insert multiple knots for cont. reasons.
	if ((iter[0] < iter[1]) || (f.basis().end() - iter == 1))
	    for (i = 0; i < alpha.order() - 1; ++i)
		new_f_knots.push_back(iter[0]);
    }
    std::set_union(new_alpha_knots.begin(), new_alpha_knots.end(),
		   new_f_knots.begin(), new_f_knots.end(),
		   std::back_inserter(knots));

    int order = alpha.order() + f.order() - 1;
    int num_coefs = (int)knots.size() - order;
    vector<double> coefs_par; // Parameter values for the coefs (Greville).
    vector<double> coefs;

    for (i = 0; i < num_coefs; ++i) {
	double tpar = 0.0;
	for (j = 1; j < order; ++j)
	    tpar += knots[i + j];
	tpar /= (order - 1);
	coefs_par.push_back(tpar);

	// We now must evaluate the product of the curves in tpar.
	Point alpha_pt, f_pt;
	alpha.point(alpha_pt, tpar);
	f.point(f_pt, tpar);
	for (j = 0; j < f.dimension(); ++j)
	    coefs.push_back(alpha_pt[0] * f_pt[j]);
    }

    // We interpolate the points. As we use up all our degrees of freedom
    // we're assured to end up with the wanted spline product.
    vector<double> new_coefs;
    vector<double> dummy_tangents;
    vector<int> dummy_index;
    BsplineBasis basis(num_coefs, order, knots.begin());
    SplineInterpolator interpolator;
    interpolator.setBasis(basis);
    interpolator.interpolate(coefs_par, coefs, dummy_index,
			     dummy_tangents, new_coefs);

    //    num_coefs = (new_coefs.size()/f.dimension());
    SplineCurve* return_curve =
	new SplineCurve(num_coefs, order,
			  interpolator.basis().begin(),
			  new_coefs.begin(), f.dimension());

    return return_curve;
}


//===========================================================================
SplineCurve* CurveCreators::blend(const SplineCurve& alpha_1,
				    const SplineCurve& f_1,
				    const SplineCurve& alpha_2,
				    const SplineCurve& f_2)
//===========================================================================
{
    // Testing dimensions.
    ALWAYS_ERROR_IF(alpha_1.dimension() != 1 || alpha_2.dimension() != 1,
		    "Dimension of (at least one) blend function differs from 1.");

    ALWAYS_ERROR_IF(f_1.dimension() != f_2.dimension(),
		    "Dimension mismatch between the spline curves to be blended.");

    // Testing parameter interval.
    double start_par = alpha_1.startparam();
    double end_par = alpha_1.endparam();
    ALWAYS_ERROR_IF(alpha_2.startparam() != start_par || alpha_2.endparam() != end_par
		|| f_1.startparam() != start_par || f_1.endparam() != end_par ||
		f_2.startparam() != start_par || f_2.endparam() != end_par,
		    "Parameter intervals of the curves do not coincide.");


    SplineCurve* first_curve = multCurveWithFunction(alpha_1, f_1);
    SplineCurve* second_curve = multCurveWithFunction(alpha_2, f_2);

    // Make sure the two curves have the same order.
    int i;
    int order_1 = first_curve->order();
    int order_2 = second_curve->order();
    for (i = 0; i < abs(order_1 - order_2); ++i)
	if (order_1 < order_2) first_curve->raiseOrder();
	else second_curve->raiseOrder();

    // We next put the two curves on the same knot vector.
    vector<double> all_knots, new_knots_first, new_knots_second;
    std::set_union(first_curve->basis().begin(), first_curve->basis().end(),
		   second_curve->basis().begin(), second_curve->basis().end(),
		   std::back_inserter(all_knots));
    std::set_difference(all_knots.begin(), all_knots.end(),
			first_curve->basis().begin(), first_curve->basis().end(),
			std::back_inserter(new_knots_first));
    std::set_difference(all_knots.begin(), all_knots.end(),
			second_curve->basis().begin(), second_curve->basis().end(),
			std::back_inserter(new_knots_second));
    first_curve->insertKnot(new_knots_first);
    second_curve->insertKnot(new_knots_second);

    // We add the spline coefficients of second_curve to first_curve.
    for (i = 0; i < first_curve->numCoefs() * first_curve->dimension(); ++i)
	first_curve->coefs_begin()[i] += second_curve->coefs_begin()[i];

    delete second_curve;

    return first_curve;
}


//===========================================================================
SplineCurve* CurveCreators::approxCurves(shared_ptr<SplineCurve>* first_crv,
					   shared_ptr<SplineCurve>* last_crv,
					   const vector<Point>& start_pt,
					   const vector<Point>& end_pt, 
					   double approxtol,
					   double& maxdist, 
					   int max_iter)
//---------------------------------------------------------------------------
//
// Purpose: Replace the current boundary pieces by an approximation.
//          Continuity towards neighbours should be C1.
//
//===========================================================================
{
    vector<Point> start_pt_cpy = start_pt;
    vector<Point> end_pt_cpy = end_pt;
    int nmbcrvs = (int)(last_crv - first_crv);

  int dim = -1;
  double len;
  Point pt1, pt2;
  double t1, t2, tint, tpar;
  int nmbsample;
  vector<double> points;
  vector<double> params;
  for (int ki=0; ki<nmbcrvs; ki++)
    {
      dim = first_crv[ki]->dimension();
      // First make an estimate of the length of the curve piece to replace.
      len = first_crv[ki]->estimatedCurveLength();

      // Compute number of sample points.
#if ((_MSC_VER > 0) && (_MSC_VER < 1300))
     nmbsample = max(5, min(1000, (int)(len/approxtol)));
#else
    nmbsample = std::max(5, std::min(1000, (int)(len/approxtol)));
#endif

      t1 = first_crv[ki]->startparam(); 
      t2 = first_crv[ki]->endparam();

      // Evaluate the curve in the sample points and make a chord
      // length parameterization of the points. Remember the index
      // of the last point.
      if (ki == 0)
	{
	  first_crv[ki]->point(pt1, t1);
	  points.insert(points.end(), pt1.begin(), pt1.end());
	  params.push_back(0.0);
	}
	  
      tint = (t2 - t1)/(double)(nmbsample-1);
      int kj;
      for (kj=1, tpar=t1+tint; kj<nmbsample; kj++, tpar+=tint)
	{
	  first_crv[ki]->point(pt2, tpar);
	  points.insert(points.end(), pt2.begin(), pt2.end());
	  params.push_back(params[params.size()-1] + pt1.dist(pt2));

	  pt1 = pt2;
	}
    }

  // If start_pt or end_pt is missing the point, method will approximate while
  // keeping start/end point of input curve fixed.
  // As approximation method uses cord length parametrization, we make sure input
  // tangents are of length 1.
  int nmb_derivatives = 0;
  if (start_pt_cpy.size() > 1) {
    start_pt_cpy[1].normalize();
    ++nmb_derivatives;
  }
  if (end_pt_cpy.size() > 1) {
    end_pt_cpy[1].normalize();
    ++nmb_derivatives;
  }
  // Create a curve approximating the points.
  // If max_iter is too large, we risk ending up with spline curve with dense inner knot spacing.
  double avdist;
  ApproxCurve approx_curve(points, params, dim, approxtol,
			     4 + nmb_derivatives, 4);
  approx_curve.setEndPoints(start_pt_cpy, end_pt_cpy);
  shared_ptr<SplineCurve> crv = approx_curve.getApproxCurve(maxdist, avdist,
							      max_iter);

  if (maxdist > approxtol) {
    // Either the number of iterations was too small or we didn't make any progress.
      MESSAGE("Failed iterating towards curve pieces! User must decide if close enough.");
  }

  return dynamic_cast<SplineCurve*>(crv->clone());
}


//===========================================================================
void
CurveCreators::projectCurve(shared_ptr<ParamCurve>& space_cv,
			    shared_ptr<ParamSurface>& surf,
			    double epsge,
			    shared_ptr<SplineCurve>& proj_cv,
			    shared_ptr<SplineCurve>& par_cv)
//===========================================================================
{
    ASSERT(space_cv->dimension() == 3);


    // Make surface curve
    shared_ptr<CurveOnSurface> sf_cv = 
      shared_ptr<CurveOnSurface>(new CurveOnSurface(surf, space_cv, false));

    // We must first construct a EvalCurveSet for use in GoHermitAppS.
    shared_ptr<TrimCurve> proj_crv(new TrimCurve(sf_cv.get()));

    // Approximate
    vector<double> initpars;
    if (space_cv->instanceType() == Class_SplineCurve) {
	shared_ptr<SplineCurve> spline_cv =
	    dynamic_pointer_cast<SplineCurve>(space_cv);
	int order = spline_cv->order();
	int nb_coef = spline_cv->numCoefs();
	vector<double>::const_iterator knots = spline_cv->basis().begin();
	initpars.push_back(knots[order-1]);
	for (int kj = order; kj <= nb_coef; ++kj)
	    if (knots[kj] > initpars[initpars.size()-1])
		initpars.push_back(knots[kj]);
    } else {
	initpars.push_back(space_cv->startparam());
	initpars.push_back(space_cv->endparam());
    }
    double tol2 = epsge;
    vector<int> dims(2);
    dims[0] = 3;
    dims[1] = 2;
    HermiteAppS approximator(proj_crv.get(),
			     &initpars[0], (int)initpars.size(),
			     epsge, tol2, dims);
    approximator.refineApproximation();
    vector<shared_ptr<SplineCurve> > proj_crvs = approximator.getCurves();

    proj_cv = proj_crvs[0];
    par_cv = proj_crvs[1];
}


// //===========================================================================
// SplineCurve*
// CurveCreators::projectSpaceCurve(shared_ptr<ParamCurve>& space_cv,
// 				 shared_ptr<ParamSurface>& surf,
// 				 shared_ptr<Point>& start_par_pt,
// 				 shared_ptr<Point>& end_par_pt,
// 				 double epsge,
// 				 const RectDomain* domain_of_interest)
// //===========================================================================
// {
//   if (surf->instanceType() == Class_SplineSurface) {
// //     shared_ptr<SplineCurve> spline_cv =
// //       dynamic_pointer_cast<SplineCurve, ParamCurve>(space_cv);
//       shared_ptr<SplineSurface> spline_sf =
// 	  dynamic_pointer_cast<SplineSurface, ParamSurface>(surf);
//       return projectSpaceCurve(space_cv, spline_sf,
// 			       start_par_pt, end_par_pt,
// 			       epsge, domain_of_interest);
//   } else if (surf->instanceType() == Class_Plane) {
//       // As a quick fix we use method for spline surface.
//       shared_ptr<Plane> plane = dynamic_pointer_cast<Plane>(surf);
//       shared_ptr<SplineSurface> bd_plane;
//       if (!plane->isBounded()) {
// 	  MESSAGE("Plane is unbounded, we must restrict it using "
// 		  "the space cv.");
// 	  vector<shared_ptr<ParamCurve> > space_cv_vec;
// 	  space_cv_vec.push_back(space_cv);
// 	  bd_plane = BoundedUtils::makeTrimmedPlane(plane, space_cv_vec);
//       } else {
// 	  bd_plane = shared_ptr<SplineSurface>(plane->geometrySurface());
//       }

//       return projectSpaceCurve(space_cv, bd_plane,
// 			       start_par_pt, end_par_pt,
// 			       epsge, domain_of_interest);      
//   } else {
//       MESSAGE("Not yet supported!");
//       return NULL;
//   }
// }


//===========================================================================
SplineCurve*
CurveCreators::projectSpaceCurve(shared_ptr<ParamCurve>& space_cv,
				 shared_ptr<ParamSurface>& surf,
				 shared_ptr<Point>& start_par_pt,
				 shared_ptr<Point>& end_par_pt,
				 double epsge,
				 const RectDomain* domain_of_interest)
//===========================================================================
{
    ASSERT(space_cv->dimension() == 3);

    shared_ptr<ParamSurface> surf2 = shared_ptr<ParamSurface>(surf->clone());
      
    shared_ptr<SplineSurface> tmp_srf = 
      dynamic_pointer_cast<SplineSurface,ParamSurface>(surf2);

    if (tmp_srf.get())
      {
	// We make sure the surf is k-regular.
	surf2 = shared_ptr<ParamSurface>
	  (tmp_srf->subSurface(tmp_srf->startparam_u(), tmp_srf->startparam_v(),
			    tmp_srf->endparam_u(), tmp_srf->endparam_v()));
      }

    // We must first construct a EvalCurve for use in GoHermitAppC.
//     shared_ptr<ParamCurve> cv = space_cv;
    shared_ptr<ProjectCurve> proj_crv(new ProjectCurve(space_cv, surf,
						       start_par_pt, end_par_pt,
						       epsge,
// 						       epsge,
						       domain_of_interest));

    // Check the evaluator base curve in the endpoints. If they don't satisfy the
    // accuracy requirement, then the approximation will never succeed
    Point proj1, proj2, space1, space2, sf_pt1, sf_pt2;
    proj1 = proj_crv->eval(proj_crv->start());
    proj2 = proj_crv->eval(proj_crv->end());
    space_cv->point(space1, space_cv->startparam());
    space_cv->point(space2, space_cv->endparam());
    surf2->point(sf_pt1, proj1[0], proj1[1]);
    surf2->point(sf_pt2, proj2[0], proj2[1]);
    double dist1 = sf_pt1.dist(space1);
    double dist2 = sf_pt2.dist(space2);
    // Even though end distance is larger than tolerance, the routine
    // actually compares distance from projection to approximation
    // ... Which makes sense. Hence no need to demand curve to be
    // within tolerance in end points.
    if (std::max(dist1, dist2) > epsge) {
      std::ofstream out("project.g2");
      surf->writeStandardHeader(out);
      surf->write(out);
      space_cv->writeStandardHeader(out);
      space_cv->write(out);

	MESSAGE("Inconsistent input to curve approximation: max_dist = "
		<< std::max(dist1, dist2) << ", epsge = " << epsge);
// 	return NULL;
    }

    // Approximate
    vector<double> initpars;
    if (space_cv->instanceType() == Class_SplineCurve) {
	shared_ptr<SplineCurve> spline_cv =
	    dynamic_pointer_cast<SplineCurve>(space_cv);
	int order = spline_cv->order();
	int nb_coef = spline_cv->numCoefs();
	vector<double>::const_iterator knots = spline_cv->basis().begin();
	initpars.push_back(knots[order-1]);
	for (int kj = order; kj <= nb_coef; ++kj)
	    if (knots[kj] > initpars[initpars.size()-1])
		initpars.push_back(knots[kj]);
    } else {
	initpars.push_back(space_cv->startparam());
	initpars.push_back(space_cv->endparam());
    }
    double kink_tol = 1e-02; // We only want it to be reasonably smooth.
    HermiteAppC approximator(proj_crv.get(),
			     &initpars[0], (int)initpars.size(),
			       epsge, kink_tol);
    approximator.refineApproximation();
    shared_ptr<SplineCurve> projection_cv = approximator.getCurve();

#if _MSC_VER > 0 && _MSC_VER < 1300
    return dynamic_cast<SplineCurve*>(projection_cv->clone());
#else
    return (projection_cv.get() != NULL) ? projection_cv->clone() : NULL;
#endif
}


//===========================================================================
SplineCurve*
CurveCreators::liftParameterCurve(shared_ptr<ParamCurve>& parameter_cv,
				  shared_ptr<ParamSurface>& surf,
				  double epsge)
//===========================================================================
{
    ASSERT(parameter_cv->dimension() == 2);

    // We must first construct a EvalCurve for use in GoHermitAppC.
    shared_ptr<LiftCurve> lift_crv(new LiftCurve(parameter_cv, surf, epsge));

    // Approximate
    vector<double> initpars;
    shared_ptr<SplineCurve> tmp_cv =
      dynamic_pointer_cast<SplineCurve, ParamCurve>(parameter_cv);
    if (tmp_cv.get())
      {
	int order = tmp_cv->order();
	int nb_coef = tmp_cv->numCoefs();
	vector<double>::const_iterator knots = tmp_cv->basis().begin();
	initpars.push_back(knots[order-1]);
	for (int kj = order; kj <= nb_coef; ++kj)
	  if (knots[kj] > initpars[initpars.size()-1])
	    initpars.push_back(knots[kj]);
      }
    else
      {
	initpars.push_back(parameter_cv->startparam());
	initpars.push_back(parameter_cv->endparam());
      }
    HermiteAppC approximator(lift_crv.get(),
			     &initpars[0], (int)initpars.size(),
			       epsge, epsge); // Using iput epsge for both geom and kink tol.
    approximator.refineApproximation();
    shared_ptr<SplineCurve> lifted_cv = approximator.getCurve();

#if _MSC_VER > 0 && _MSC_VER < 1300
    return dynamic_cast<SplineCurve*>(lifted_cv->clone());
#else
    return lifted_cv->clone();
#endif
}


//===========================================================================
SplineCurve* CurveCreators::createCircle(Point center, 
					   Point axis, 
					   Point normal, 
					   double radius)
//===========================================================================
{
    ALWAYS_ERROR_IF(center.dimension() != 3,
		"Input pts must be of dimension 3!"); // We may accept 2 also, but not now...

    // Just to be on the safe side...
    axis.normalize();
    axis *= radius;
    normal.normalize();
    int ki;
    int dim = center.dimension();
    int num_coefs_circ = 9;
    std::vector<double> circle_coefs((dim + 1)*num_coefs_circ); // Rational cv.
    // We're starting in the yz-plane. Circle is defined as unit circle in input plane.
    double weight = 1/sqrt(2.0);
    Point cross = normal % axis;
    for (ki = 0; ki < 3; ++ki) {
	circle_coefs[ki] = center[ki] + axis[ki];
	circle_coefs[ki+4] = weight*(center[ki] + axis[ki] + cross[ki]);
	circle_coefs[ki+8] = center[ki] + cross[ki];
	circle_coefs[ki+12] = weight*(center[ki] - axis[ki] + cross[ki]);
	circle_coefs[ki+16] = center[ki] - axis[ki];
	circle_coefs[ki+20] = weight*(center[ki] - axis[ki] - cross[ki]);
	circle_coefs[ki+24] = center[ki] - cross[ki];
	circle_coefs[ki+28] = weight*(center[ki] + axis[ki] - cross[ki]);
	circle_coefs[ki+32] = center[ki] + axis[ki];
    }
    for (ki = 0; ki < 9; ++ki) {
	circle_coefs[ki*4+3] = (ki % 2 == 0) ? 1.0 : weight;
    }

    // We try to make a rational spline curve from coefs.
    int order_circ = 3; // Quadratic.
    std::vector<double> knots(num_coefs_circ+order_circ);
    int kk;
    for (kk = 0; kk < order_circ; ++kk) {
	knots[kk] = 0.0;
    }
    int val = 1;
    for (; kk < num_coefs_circ; kk+= 2, ++val) {
	knots[kk] = knots[kk+1] = (double)(val);
    }
    for (; kk < num_coefs_circ + order_circ; ++kk) {
	knots[kk] = (double)(val);
    }

    SplineCurve* circle_cv = new SplineCurve(num_coefs_circ, order_circ, knots.begin(),
					     circle_coefs.begin(), dim, true);

    return circle_cv;
}



//===========================================================================
shared_ptr<SplineCurve> CurveCreators::insertParamDomain(const SplineCurve& cv_1d,
							 double knot_tol)
//===========================================================================
{
    if (cv_1d.rational()) {
	MESSAGE("Rational case not supported yet! Using non-rational coefs.");
    }

    // The returned object should be linear in the first direction.
    // We create an additional 1d-cv describing the linear param space.
    vector<double> lin_knots(4, cv_1d.startparam());
    lin_knots[2] = lin_knots[3] = cv_1d.endparam();
    vector<double> lin_coefs(2);
    lin_coefs[0] = lin_knots[0];
    lin_coefs[1] = lin_knots[2];
    shared_ptr<SplineCurve> lin_cv(new SplineCurve(2, 2, lin_knots.begin(), lin_coefs.begin(), 1));

    // We then make sure the domains are equal (i.e. we may need to insert knots into basis of lin_cv).
    vector<shared_ptr<SplineCurve> > cvs;
    cvs.push_back(lin_cv);
    cvs.push_back(shared_ptr<SplineCurve>(cv_1d.clone()));
    unifyCurveSplineSpace(cvs, knot_tol);

    // We then create our param cv (i.e. living a 2-dimensional domain).
    vector<double> all_coefs;
    int nmb_ctl_pts = cvs[0]->numCoefs();
    for (int ki = 0; ki < nmb_ctl_pts; ++ki) {
	all_coefs.push_back(cvs[0]->coefs_begin()[ki]);
	all_coefs.push_back(cvs[1]->coefs_begin()[ki]);
    }
    shared_ptr<SplineCurve> return_cv(new SplineCurve(cvs[0]->numCoefs(), cvs[0]->order(),
						      cvs[0]->basis().begin(), all_coefs.begin(), 2));

    return return_cv;
}

//===========================================================================
SplineCurve* CurveCreators::offsetCurve(const SplineCurve& base_cv, Point offset_val)
//===========================================================================
{

    SplineCurve* offset_cv(new SplineCurve(base_cv));

    // We then add offset_val to all coefs (should handle rational cvs as well).
    std::vector<double>::iterator coefs_iter = offset_cv->coefs_begin();
    int dim = offset_cv->dimension();
    ASSERT(dim == offset_val.dimension());
    while (coefs_iter < offset_cv->coefs_end())
    {
	for (int ki = 0; ki < dim; ++ki)
	    coefs_iter[ki] += offset_val[ki];

	coefs_iter += dim;
    }

    return offset_cv;
}