#ifndef DUNE_PDELAB_ONESTEP_OPERATOR_HH
#define DUNE_PDELAB_ONESTEP_OPERATOR_HH

#include <dune/pdelab/gridoperator/onestep/localassembler.hh>
#include <dune/pdelab/gridoperator/common/gridoperatorutilities.hh>

namespace Dune{
  namespace PDELab{

    /**
       \brief A standard grid operator implementation suitable for the
       combination with a one step time stepping method.

       
       \tparam GFSU GridFunctionSpace for ansatz functions
       \tparam GFSV GridFunctionSpace for test functions
       \tparam MB The matrix backend to be used for representation of the jacobian
       \tparam DF The domain field type of the operator
       \tparam RF The range field type of the operator
       \tparam ST The type of the sub triangulation
       \tparam nonoverlapping_mode Switch for nonoverlapping grids
       \tparam CU   Constraints maps for the individual dofs (trial space)
       \tparam CV   Constraints maps for the individual dofs (test space)

    */
    template<typename GO0, typename GO1,
             typename CU=Dune::PDELab::EmptyTransformation,
             typename CV=Dune::PDELab::EmptyTransformation>
    class OneStepGridOperator
    {
    public:

      //! The sparsity pattern container for the jacobian matrix
      typedef typename GO0::Pattern Pattern;

      //! The global UDG assembler type
      typedef typename GO0::Assembler Assembler;

      //! The local assembler types of the subordinate grid operators
      //! @{
      typedef typename GO0::LocalAssembler LocalAssemblerDT0;
      typedef typename GO1::LocalAssembler LocalAssemblerDT1;
      //! @}

      //! The local UDG assembler type
      typedef OneStepLocalAssembler<LocalAssemblerDT0,LocalAssemblerDT1,CU,CV> LocalAssembler;

      //! The grid operator traits 
      typedef Dune::PDELab::GridOperatorTraits
      <typename GO0::Traits::TrialGridFunctionSpace,
       typename GO0::Traits::TestGridFunctionSpace,
       typename GO0::Traits::MatrixBackend, 
       typename GO0::Traits::DomainField,
       typename GO0::Traits::RangeField, 
       typename GO0::Traits::JacobianField,
       typename GO0::Traits::TrialGridFunctionSpaceConstraints,
       typename GO0::Traits::TestGridFunctionSpaceConstraints,
       Assembler, 
       LocalAssembler> Traits;

      //! The io types of the operator
      //! @{
      typedef typename Traits::Domain Domain;
      typedef typename Traits::Range Range;
      typedef typename Traits::Jacobian Jacobian;
      //! @}

      template <typename MFT>
      struct MatrixContainer{
        typedef Jacobian Type;
      };

      //! The type for real number e.g. time
      typedef typename LocalAssembler::Real Real;

      //! The type of the one step method parameters
      typedef typename LocalAssembler::OneStepParameters OneStepParameters;

      //! Constructor for non trivial constraints
      OneStepGridOperator(const GO0 & go0_, const GO1 & go1_)
        : global_assembler(go0_.assembler()), 
          go0(go0_), go1(go1_),
          la0(go0_.localAssembler()), la1(go1_.localAssembler()),
          const_residual(go0_.testGridFunctionSpace()),
          local_assembler(la0,la1, const_residual)
      {}

      //! Constructor for non trivial constraints
      OneStepGridOperator(const GO0 & go0_, const GO1 & go1_, const CU & cu_, const CV & cv_)
        : global_assembler(go0_.assembler()), 
          go0(go0_), go1(go1_),
          la0(go0_.localAssembler()), la1(go1_.localAssembler()),
          const_residual(go0_.testGridFunctionSpace()),
          local_assembler(la0,la1, const_residual, cu_, cv_)
      {}

      //! Get the trial grid function space
      const typename Traits::TrialGridFunctionSpace& trialGridFunctionSpace() const
      {
        return global_assembler.trialGridFunctionSpace();
      }

      //! Get the test grid function space
      const typename Traits::TestGridFunctionSpace& testGridFunctionSpace() const
      {
        return global_assembler.testGridFunctionSpace();
      }

      //! Get dimension of space u
      typename Traits::TrialGridFunctionSpace::Traits::SizeType globalSizeU () const
      {
        return trialGridFunctionSpace().globalSize();
      }

      //! Get dimension of space v
      typename Traits::TestGridFunctionSpace::Traits::SizeType globalSizeV () const
      {
        return testGridFunctionSpace().globalSize();
      }

      Assembler & assembler(){ return global_assembler; }

      LocalAssembler & localAssembler(){ return local_assembler; }

      //! Fill pattern of jacobian matrix
      void fill_pattern(Pattern & p) const {
        typedef typename LocalAssembler::LocalPatternAssemblerEngine PatternEngine;
        PatternEngine & pattern_engine = local_assembler.localPatternAssemblerEngine(p);
        global_assembler.assemble(pattern_engine);
      }

      //! Assemble constant part of residual
      void preStage(unsigned int stage, const std::vector<Domain*> & x){
        typedef typename LocalAssembler::LocalPreStageAssemblerEngine PreStageEngine;
        local_assembler.setStage(stage);
        PreStageEngine & prestage_engine = local_assembler.localPreStageAssemblerEngine(x);
        global_assembler.assemble(prestage_engine);
        //Dune::printvector(std::cout,const_residual.base(),"const residual","row",4,9,1);
      }

      //! Assemble residual
      void residual(const Domain & x, Range & r) const {
        typedef typename LocalAssembler::LocalResidualAssemblerEngine ResidualEngine;
        ResidualEngine & residual_engine = local_assembler.localResidualAssemblerEngine(r,x);
        global_assembler.assemble(residual_engine);
        //Dune::printvector(std::cout,r.base(),"residual","row",4,9,1);
      }

      //! Assemble jacobian
      void jacobian(const Domain & x, Jacobian & a) const {
        typedef typename LocalAssembler::LocalJacobianAssemblerEngine JacobianEngine;
        JacobianEngine & jacobian_engine = local_assembler.localJacobianAssemblerEngine(a,x);
        global_assembler.assemble(jacobian_engine);
        //printmatrix(std::cout,a.base(),"global stiffness matrix","row",9,1);
      }

      //! Interpolate constrained values from given function f
      template<typename F, typename X> 
      void interpolate (unsigned stage, const X& xold, F& f, X& x) const
      {
        // Set time in boundary value function
        f.setTime(local_assembler.timeAtStage(stage));

        // Interpolate 
        go0.interpolate(xold,f,x);

        // Copy non-constrained dofs from old time step
        Dune::PDELab::copy_nonconstrained_dofs(local_assembler.trialConstraints(),xold,x);
      }
      
      //! parametrize assembler with a time-stepping method
      void preStep (const TimeSteppingParameterInterface<Real>& method_, Real time_, Real dt_)
      {
        local_assembler.setMethod(method_);
        local_assembler.preStep(time_,dt_,method_.s());
      }

      //! to be called after step is completed
      void postStep ()
      {
        la0.postStep();
        la1.postStep();
      }

      //! to be called after stage is completed
      void postStage ()
      {
        la0.postStage();
        la1.postStage();
      }

      //! to be called once before each stage
      Real suggestTimestep (Real dt) const
      {
        Real suggested_dt = std::min(la0.suggestTimestep(dt),la1.suggestTimestep(dt));
        if (trialGridFunctionSpace().gridview().comm().size()>1)
          suggested_dt =  trialGridFunctionSpace().gridview().comm().min(suggested_dt);
        return suggested_dt;
      }

    private:
      Assembler global_assembler;
      const GO0 & go0;
      const GO1 & go1;
      LocalAssemblerDT0 & la0;
      LocalAssemblerDT1 & la1;
      Range const_residual;
      mutable LocalAssembler local_assembler;
    };

  };
};
#endif
