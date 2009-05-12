// -*- tab-width: 4; indent-tabs-mode: nil -*-
#ifndef DUNE_PDELAB_P12DFEM_HH
#define DUNE_PDELAB_P12DFEM_HH

#include<dune/finiteelements/p12d.hh>
#include"../common/geometrywrapper.hh"
#include"finiteelementmap.hh"

namespace Dune {
  namespace PDELab {

	//! wrap up element from local functions
    //! \ingroup FiniteElementMap
	template<class D, class R>
	class P12DLocalFiniteElementMap
	  : public SimpleLocalFiniteElementMap< Dune::P12DLocalFiniteElement<D,R> >
	{};


    //! Constraints construction for P1 elements on triangles
    class P12DConstraints
    {
    public:
      enum { doBoundary = true };
      enum { doProcessor = false }; // added ParallelStuff
      enum { doSkeleton = false };
      enum { doVolume = false };

      // boundary constraints
      // F : grid function returning boundary condition type
      // IG : intersection geometry
      // LFS : local function space
      // T : TransformationType
      template<typename F, typename I, typename LFS, typename T>
      void boundary (const F& f, const IntersectionGeometry<I>& ig, 
                     const LFS& lfs, T& trafo) const
      {
        // 2D here, get midpoint of edge
        typename F::Traits::DomainType ip(0.5);

        // determine type of boundary condition
        typename F::Traits::RangeType bctype;
        f.evaluate(ig,ip,bctype);

        // if dirichlet boundary, the two end nodes of the edge are constrained
        if (bctype>0)
          {
            // determine the constrained nodes (requires knowledge about reference element)
            typename T::RowType empty;
            int edge = ig.numberInSelf();
            trafo[(edge+1)%3] = empty; // first node
            trafo[(edge+2)%3] = empty; // second node
          }
      }
    };
  }
}

#endif
