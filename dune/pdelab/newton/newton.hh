// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_PDELAB_NEWTON_HH
#define DUNE_PDELAB_NEWTON_HH

#include <iostream>
#include <iomanip>
#include <cmath>
#include <memory>

#include <math.h>

#include <dune/common/exceptions.hh>
#include <dune/common/ios_state.hh>
#include <dune/common/timer.hh>
#include <dune/common/parametertree.hh>

#include <dune/pdelab/backend/solver.hh>

namespace Dune
{
  namespace PDELab
  {
    // Exception classes used in NewtonSolver
    class NewtonError : public Exception {};
    class NewtonDefectError : public NewtonError {};
    class NewtonLinearSolverError : public NewtonError {};
    class NewtonLineSearchError : public NewtonError {};
    class NewtonNotConverged : public NewtonError {};

    // Status information of Newton's method
    template<class RFType>
    struct NewtonResult : LinearSolverResult<RFType>
    {
      RFType first_defect;       // the first defect
      RFType defect;             // the final defect
      double assembler_time;     // Cumulative time for matrix assembly
      double linear_solver_time; // Cumulative time for linear sovler
      int linear_solver_iterations; // Total number of linear iterations

      NewtonResult() :
        first_defect(0.0), defect(0.0), assembler_time(0.0), linear_solver_time(0.0),
        linear_solver_iterations(0) {}
    };

    template<class GOS, class TrlV, class TstV>
    class NewtonBase
    {
      typedef GOS GridOperator;
      typedef TrlV TrialVector;
      typedef TstV TestVector;

      typedef typename TestVector::ElementType RFType;
      typedef typename GOS::Traits::Jacobian Matrix;


    public:
      // export result type
      typedef NewtonResult<RFType> Result;

      void setVerbosityLevel(unsigned int verbosity_level_)
      {
        if (gridoperator.trialGridFunctionSpace().gridView().comm().rank()>0)
          verbosity_level = 0;
        else
          verbosity_level = verbosity_level_;
      }

      //! Set whether the jacobian matrix should be kept across calls to apply().
      void setKeepMatrix(bool b)
      {
        keep_matrix = b;
      }

      //! Return whether the jacobian matrix is kept across calls to apply().
      bool keepMatrix() const
      {
        return keep_matrix;
      }

      //! Discard the stored Jacobian matrix.
      void discardMatrix()
      {
        if(A)
          A.reset();
      }

    protected:
      const GridOperator& gridoperator;
      TrialVector *u;
      std::shared_ptr<TrialVector> z;
      std::shared_ptr<TestVector> r;
      std::shared_ptr<Matrix> A;
      Result res;
      unsigned int verbosity_level;
      RFType prev_defect;
      RFType linear_reduction;
      bool reassembled;
      RFType reduction;
      RFType abs_limit;
      bool keep_matrix;

      NewtonBase(const GridOperator& go, TrialVector& u_)
        : gridoperator(go)
        , u(&u_)
        , verbosity_level(1)
        , keep_matrix(true)
      {
        if (gridoperator.trialGridFunctionSpace().gridView().comm().rank()>0)
          verbosity_level = 0;
      }

      NewtonBase(const GridOperator& go)
        : gridoperator(go)
        , u(0)
        , verbosity_level(1)
        , keep_matrix(true)
      {
        if (gridoperator.trialGridFunctionSpace().gridView().comm().rank()>0)
          verbosity_level = 0;
      }

      virtual ~NewtonBase() { }

      virtual bool terminate() = 0;
      virtual void prepare_step(Matrix& A, TestVector& r) = 0;
      virtual void line_search(TrialVector& z, TestVector& r) = 0;
      virtual void defect(TestVector& r) = 0;
    }; // end class NewtonBase

    template<class GOS, class S, class TrlV, class TstV>
    class NewtonSolver : public virtual NewtonBase<GOS,TrlV,TstV>
    {
      typedef S Solver;
      typedef GOS GridOperator;
      typedef TrlV TrialVector;
      typedef TstV TestVector;

      typedef typename TestVector::ElementType RFType;
      typedef typename GOS::Traits::Jacobian Matrix;

    public:
      typedef NewtonResult<RFType> Result;

      NewtonSolver(const GridOperator& go, TrialVector& u_, Solver& solver_)
        : NewtonBase<GOS,TrlV,TstV>(go,u_)
        , solver(solver_)
        , result_valid(false)
      {}

      NewtonSolver(const GridOperator& go, Solver& solver_)
        : NewtonBase<GOS,TrlV,TstV>(go)
        , solver(solver_)
        , result_valid(false)
      {}

      void apply();

      void apply(TrialVector& u_);

      const Result& result() const
      {
        if (!result_valid)
          DUNE_THROW(NewtonError,
                     "NewtonSolver::result() called before NewtonSolver::solve()");
        return this->res;
      }

    protected:
      virtual void defect(TestVector& r)
      {
        r = 0.0;                                        // TODO: vector interface
        this->gridoperator.residual(*this->u, r);
        this->res.defect = this->solver.norm(r);                    // TODO: solver interface
        if (!std::isfinite(this->res.defect))
          DUNE_THROW(NewtonDefectError,
                     "NewtonSolver::defect(): Non-linear defect is NaN or Inf");
      }


    private:
      void linearSolve(Matrix& A, TrialVector& z, TestVector& r) const
      {
        if (this->verbosity_level >= 4)
          std::cout << "      Solving linear system..." << std::endl;
        z = 0.0;                                        // TODO: vector interface
        this->solver.apply(A, z, r, this->linear_reduction);        // TODO: solver interface

        ios_base_all_saver restorer(std::cout); // store old ios flags

        if (!this->solver.result().converged)                 // TODO: solver interface
          DUNE_THROW(NewtonLinearSolverError,
                     "NewtonSolver::linearSolve(): Linear solver did not converge "
                     "in " << this->solver.result().iterations << " iterations");
        if (this->verbosity_level >= 4)
          std::cout << "          linear solver iterations:     "
                    << std::setw(12) << solver.result().iterations << std::endl
                    << "          linear defect reduction:      "
                    << std::setw(12) << std::setprecision(4) << std::scientific
                    << solver.result().reduction << std::endl;
      }

      Solver& solver;
      bool result_valid;
    }; // end class NewtonSolver

    template<class GOS, class S, class TrlV, class TstV>
    void NewtonSolver<GOS,S,TrlV,TstV>::apply(TrialVector& u_)
    {
      this->u = &u_;
      apply();
    }

    template<class GOS, class S, class TrlV, class TstV>
    void NewtonSolver<GOS,S,TrlV,TstV>::apply()
    {
      this->res.iterations = 0;
      this->res.converged = false;
      this->res.reduction = 1.0;
      this->res.conv_rate = 1.0;
      this->res.elapsed = 0.0;
      this->res.assembler_time = 0.0;
      this->res.linear_solver_time = 0.0;
      this->res.linear_solver_iterations = 0;
      result_valid = true;
      Timer timer;

      try
        {
          if(!this->r) {
            // std::cout << "=== Setting up residual vector ..." << std::endl;
            this->r = std::make_shared<TestVector>(this->gridoperator.testGridFunctionSpace());
          }
          // residual calculation in member function "defect":
          //--------------------------------------------------
          // - set residual vector to zero
          // - calculate new residual
          // - store norm of residual in "this->res.defect"
          this->defect(*this->r);
          this->res.first_defect = this->res.defect;
          this->prev_defect = this->res.defect;

          if (this->verbosity_level >= 2)
            {
              // store old ios flags
              ios_base_all_saver restorer(std::cout);
              std::cout << "  Initial defect: "
                        << std::setw(12) << std::setprecision(4) << std::scientific
                        << this->res.defect << std::endl;
            }

          if(!this->A) {
            // std::cout << "==== Setting up jacobian matrix ... " << std::endl;
            this->A = std::make_shared<Matrix>(this->gridoperator);
          }
          if(!this->z) {
            // std::cout << "==== Setting up correction vector ... " << std::endl;
            this->z = std::make_shared<TrialVector>(this->gridoperator.trialGridFunctionSpace());
          }

          while (!this->terminate())
            {
              if (this->verbosity_level >= 3)
                std::cout << "  Newton iteration " << this->res.iterations
                          << " --------------------------------" << std::endl;

              Timer assembler_timer;
              try
                {
                  // jacobian calculation in member function "prepare_step"
                  //-------------------------------------------------------
                  // - if above reassemble threshold
                  //   - set jacobian to zero
                  //   - calculate new jacobian
                  // - set linear reduction
                  this->prepare_step(*this->A,*this->r);
                }
              catch (...)
                {
                  this->res.assembler_time += assembler_timer.elapsed();
                  throw;
                }
              double assembler_time = assembler_timer.elapsed();
              this->res.assembler_time += assembler_time;
              if (this->verbosity_level >= 3)
                std::cout << "      matrix assembly time:             "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << assembler_time << std::endl;

              Timer linear_solver_timer;
              try
                {
                  // solution of linear system in member function "linearSolve"
                  //-----------------------------------------------------------
                  // - set initial guess for correction z to zero
                  // - call linear solver
                  this->linearSolve(*this->A, *this->z, *this->r);
                }
              catch (...)
                {
                  this->res.linear_solver_time += linear_solver_timer.elapsed();
                  this->res.linear_solver_iterations += this->solver.result().iterations;
                  throw;
                }
              double linear_solver_time = linear_solver_timer.elapsed();
              this->res.linear_solver_time += linear_solver_time;
              this->res.linear_solver_iterations += this->solver.result().iterations;

              try
                {
                  // line search with correction z
                  // the undamped version is also integrated in here
                  this->line_search(*this->z, *this->r);
                }
              catch (NewtonLineSearchError)
                {
                  if (this->reassembled)
                    throw;
                  if (this->verbosity_level >= 3)
                    std::cout << "      line search failed - trying again with reassembled matrix" << std::endl;
                  continue;
                }

              this->res.reduction = this->res.defect/this->res.first_defect;
              this->res.iterations++;
              this->res.conv_rate = std::pow(this->res.reduction, 1.0/this->res.iterations);

              // store old ios flags
              ios_base_all_saver restorer(std::cout);

              if (this->verbosity_level >= 3)
                std::cout << "      linear solver time:               "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << linear_solver_time << std::endl
                          << "      defect reduction (this iteration):"
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << this->res.defect/this->prev_defect << std::endl
                          << "      defect reduction (total):         "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << this->res.reduction << std::endl
                          << "      new defect:                       "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << this->res.defect << std::endl;
              if (this->verbosity_level == 2)
                std::cout << "  Newton iteration " << std::setw(2) << this->res.iterations
                          << ".  New defect: "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << this->res.defect
                          << ".  Reduction (this): "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << this->res.defect/this->prev_defect
                          << ".  Reduction (total): "
                          << std::setw(12) << std::setprecision(4) << std::scientific
                          << this->res.reduction << std::endl;
            } // end while
        } // end try
      catch(...)
        {
          this->res.elapsed = timer.elapsed();
          throw;
        }
      this->res.elapsed = timer.elapsed();

      ios_base_all_saver restorer(std::cout); // store old ios flags

      if (this->verbosity_level == 1)
        std::cout << "  Newton converged after " << std::setw(2) << this->res.iterations
                  << " iterations.  Reduction: "
                  << std::setw(12) << std::setprecision(4) << std::scientific
                  << this->res.reduction
                  << "   (" << std::setprecision(4) << this->res.elapsed << "s)"
                  << std::endl;

      if(!this->keep_matrix)
        this->A.reset();
    } // end apply

    template<class GOS, class TrlV, class TstV>
    class NewtonTerminate : public virtual NewtonBase<GOS,TrlV,TstV>
    {
      typedef GOS GridOperator;
      typedef TrlV TrialVector;

      typedef typename TstV::ElementType RFType;

    public:
      NewtonTerminate(const GridOperator& go, TrialVector& u_)
        : NewtonBase<GOS,TrlV,TstV>(go,u_)
        , maxit(40)
        , force_iteration(false)
      {
        this->reduction = 1e-8;
        this->abs_limit = 1e-12;
      }

      NewtonTerminate(const GridOperator& go)
        : NewtonBase<GOS,TrlV,TstV>(go)
        , maxit(40)
        , force_iteration(false)
      {
        this->reduction = 1e-8;
        this->abs_limit = 1e-12;
      }

      void setReduction(RFType reduction_)
      {
        this->reduction = reduction_;
      }

      void setMaxIterations(unsigned int maxit_)
      {
        maxit = maxit_;
      }

      void setForceIteration(bool force_iteration_)
      {
        force_iteration = force_iteration_;
      }

      void setAbsoluteLimit(RFType abs_limit_)
      {
        this->abs_limit = abs_limit_;
      }

      virtual bool terminate()
      {
        if (force_iteration && this->res.iterations == 0)
          return false;
        this->res.converged = this->res.defect < this->abs_limit
          || this->res.defect < this->res.first_defect * this->reduction;
        if (this->res.iterations >= maxit && !this->res.converged)
          DUNE_THROW(NewtonNotConverged,
                     "NewtonTerminate::terminate(): Maximum iteration count reached");
        return this->res.converged;
      }

    private:
      unsigned int maxit;
      bool force_iteration;
    }; // end class NewtonTerminate

    template<class GOS, class TrlV, class TstV>
    class NewtonPrepareStep : public virtual NewtonBase<GOS,TrlV,TstV>
    {
      typedef GOS GridOperator;
      typedef TrlV TrialVector;

      typedef typename TstV::ElementType RFType;
      typedef typename GOS::Traits::Jacobian Matrix;

    public:
      NewtonPrepareStep(const GridOperator& go, TrialVector& u_)
        : NewtonBase<GOS,TrlV,TstV>(go,u_)
        , min_linear_reduction(1e-3)
        , fixed_linear_reduction(0.0)
        , reassemble_threshold(0.0)
      {}

      NewtonPrepareStep(const GridOperator& go)
        : NewtonBase<GOS,TrlV,TstV>(go)
        , min_linear_reduction(1e-3)
        , fixed_linear_reduction(0.0)
        , reassemble_threshold(0.0)
      {}

      /* with min_linear_reduction > 0, the linear reduction will be
         determined as mininum of the min_linear_reduction and the
         linear_reduction needed to achieve second order
         Newton convergence. */
      void setMinLinearReduction(RFType min_linear_reduction_)
      {
        min_linear_reduction = min_linear_reduction_;
      }

      /* with fixed_linear_reduction > 0, the linear reduction
         rate will always be fixed to min_linear_reduction. */
      void setFixedLinearReduction(bool fixed_linear_reduction_)
      {
        fixed_linear_reduction = fixed_linear_reduction_;
      }

      void setReassembleThreshold(RFType reassemble_threshold_)
      {
        reassemble_threshold = reassemble_threshold_;
      }

      virtual void prepare_step(Matrix& A, TstV& )
      {
        this->reassembled = false;
        if (this->res.defect/this->prev_defect > reassemble_threshold)
          {
            if (this->verbosity_level >= 3)
              std::cout << "      Reassembling matrix..." << std::endl;
            A = 0.0;                                    // TODO: Matrix interface
            this->gridoperator.jacobian(*this->u, A);
            this->reassembled = true;
          }

        if (fixed_linear_reduction == true)
          this->linear_reduction = min_linear_reduction;
        else {
          // determine maximum defect, where Newton is converged.
          RFType stop_defect =
            std::max(this->res.first_defect * this->reduction,
                     this->abs_limit);

          /*
            To achieve second order convergence of newton
            we need a linear reduction of at least
            current_defect^2/prev_defect^2.
            For the last newton step a linear reduction of
            1/10*end_defect/current_defect
            is sufficient for convergence.
          */
          if ( stop_defect/(10*this->res.defect) >
               this->res.defect*this->res.defect/(this->prev_defect*this->prev_defect) )
            this->linear_reduction =
              stop_defect/(10*this->res.defect);
          else
            this->linear_reduction =
              std::min(min_linear_reduction,this->res.defect*this->res.defect/(this->prev_defect*this->prev_defect));
        }

        this->prev_defect = this->res.defect;

        ios_base_all_saver restorer(std::cout); // store old ios flags

        if (this->verbosity_level >= 3)
          std::cout << "      requested linear reduction:       "
                    << std::setw(12) << std::setprecision(4) << std::scientific
                    << this->linear_reduction << std::endl;
      }

    private:
      RFType min_linear_reduction;
      bool fixed_linear_reduction;
      RFType reassemble_threshold;
    }; // end class NewtonPrepareStep

    template<class GOS, class TrlV, class TstV>
    class NewtonLineSearch : public virtual NewtonBase<GOS,TrlV,TstV>
    {
      typedef GOS GridOperator;
      typedef TrlV TrialVector;
      typedef TstV TestVector;

      typedef typename TestVector::ElementType RFType;

    public:
      enum Strategy { noLineSearch,
                      hackbuschReusken,
                      hackbuschReuskenAcceptBest };

      NewtonLineSearch(const GridOperator& go, TrialVector& u_)
        : NewtonBase<GOS,TrlV,TstV>(go,u_)
        , strategy(hackbuschReusken)
        , maxit(10)
        , damping_factor(0.5)
      {}

      NewtonLineSearch(const GridOperator& go)
        : NewtonBase<GOS,TrlV,TstV>(go)
        , strategy(hackbuschReusken)
        , maxit(10)
        , damping_factor(0.5)
      {}

      void setLineSearchStrategy(Strategy strategy_)
      {
        strategy = strategy_;
      }

      void setLineSearchStrategy(std::string strategy_)
      {
        strategy = strategyFromName(strategy_);
      }

      void setLineSearchMaxIterations(unsigned int maxit_)
      {
        maxit = maxit_;
      }

      void setLineSearchDampingFactor(RFType damping_factor_)
      {
        damping_factor = damping_factor_;
      }

      virtual void line_search(TrialVector& z, TestVector& r)
      {
        if (strategy == noLineSearch)
          {
            this->u->axpy(-1.0, z);                     // TODO: vector interface
            this->defect(r);
            return;
          }

        if (this->verbosity_level >= 4)
          std::cout << "      Performing line search..." << std::endl;
        RFType lambda = 1.0;
        RFType best_lambda = 0.0;
        RFType best_defect = this->res.defect;
        TrialVector prev_u(*this->u);  // TODO: vector interface
        unsigned int i = 0;
        ios_base_all_saver restorer(std::cout); // store old ios flags

        while (1)
          {
            if (this->verbosity_level >= 4)
              std::cout << "          trying line search damping factor:   "
                        << std::setw(12) << std::setprecision(4) << std::scientific
                        << lambda
                        << std::endl;

            this->u->axpy(-lambda, z);                  // TODO: vector interface
            try {
              this->defect(r);
            }
            catch (NewtonDefectError)
              {
                if (this->verbosity_level >= 4)
                  std::cout << "          Nans detected" << std::endl;
              }       // ignore NaNs and try again with lower lambda

            if (this->res.defect <= (1.0 - lambda/4) * this->prev_defect)
              {
                if (this->verbosity_level >= 4)
                  std::cout << "          line search converged" << std::endl;
                break;
              }

            if (this->res.defect < best_defect)
              {
                best_defect = this->res.defect;
                best_lambda = lambda;
              }

            if (++i >= maxit)
              {
                if (this->verbosity_level >= 4)
                  std::cout << "          max line search iterations exceeded" << std::endl;
                switch (strategy)
                  {
                  case hackbuschReusken:
                    *this->u = prev_u;
                    this->defect(r);
                    DUNE_THROW(NewtonLineSearchError,
                               "NewtonLineSearch::line_search(): line search failed, "
                               "max iteration count reached, "
                               "defect did not improve enough");
                  case hackbuschReuskenAcceptBest:
                    if (best_lambda == 0.0)
                      {
                        *this->u = prev_u;
                        this->defect(r);
                        DUNE_THROW(NewtonLineSearchError,
                                   "NewtonLineSearch::line_search(): line search failed, "
                                   "max iteration count reached, "
                                   "defect did not improve in any of the iterations");
                      }
                    if (best_lambda != lambda)
                      {
                        *this->u = prev_u;
                        this->u->axpy(-best_lambda, z);
                        this->defect(r);
                      }
                    break;
                  case noLineSearch:
                    break;
                  }
                break;
              }

            lambda *= damping_factor;
            *this->u = prev_u;                          // TODO: vector interface
          }
        if (this->verbosity_level >= 4)
          std::cout << "          line search damping factor:   "
                    << std::setw(12) << std::setprecision(4) << std::scientific
                    << lambda << std::endl;
      } // end line_search

    protected:
      /** helper function to get the different strategies from their name */
      Strategy strategyFromName(const std::string & s) {
        if (s == "noLineSearch")
          return noLineSearch;
        if (s == "hackbuschReusken")
          return hackbuschReusken;
        if (s == "hackbuschReuskenAcceptBest")
          return hackbuschReuskenAcceptBest;
      }

    private:
      Strategy strategy;
      unsigned int maxit;
      RFType damping_factor;
    }; // end class NewtonLineSearch

    template<class GOS, class S, class TrlV, class TstV = TrlV>
    class Newton : public NewtonSolver<GOS,S,TrlV,TstV>
                 , public NewtonTerminate<GOS,TrlV,TstV>
                 , public NewtonLineSearch<GOS,TrlV,TstV>
                 , public NewtonPrepareStep<GOS,TrlV,TstV>
    {
      typedef GOS GridOperator;
      typedef S Solver;
      typedef TrlV TrialVector;

    public:
      Newton(const GridOperator& go, TrialVector& u_, Solver& solver_)
        : NewtonBase<GOS,TrlV,TstV>(go,u_)
        , NewtonSolver<GOS,S,TrlV,TstV>(go,u_,solver_)
        , NewtonTerminate<GOS,TrlV,TstV>(go,u_)
        , NewtonLineSearch<GOS,TrlV,TstV>(go,u_)
        , NewtonPrepareStep<GOS,TrlV,TstV>(go,u_)
      {}
      Newton(const GridOperator& go, Solver& solver_)
        : NewtonBase<GOS,TrlV,TstV>(go)
        , NewtonSolver<GOS,S,TrlV,TstV>(go,solver_)
        , NewtonTerminate<GOS,TrlV,TstV>(go)
        , NewtonLineSearch<GOS,TrlV,TstV>(go)
        , NewtonPrepareStep<GOS,TrlV,TstV>(go)
      {}
      //! interpret a parameter tree as a set of options for the newton solver
      /**

         example configuration:

         \code
         [NewtonParameters]

         ReassembleThreshold = 0.1
         LineSearchMaxIterations = 10
         MaxIterations = 7
         AbsoluteLimit = 1e-6
         Reduction = 1e-4
         LinearReduction = 1e-3
         LineSearchDamping  = 0.9
         \endcode

         and invocation in the code:
         \code
         newton.setParameters(param.sub("NewtonParameters"));
         \endcode
      */
      void setParameters(Dune::ParameterTree & param)
      {
        typedef typename TstV::ElementType RFType;
        if (param.hasKey("VerbosityLevel"))
          this->setVerbosityLevel(
            param.get<unsigned int>("VerbosityLevel"));
        if (param.hasKey("Reduction"))
          this->setReduction(
            param.get<RFType>("Reduction"));
        if (param.hasKey("MaxIterations"))
          this->setMaxIterations(
            param.get<unsigned int>("MaxIterations"));
        if (param.hasKey("ForceIteration"))
          this->setForceIteration(
            param.get<bool>("ForceIteration"));
        if (param.hasKey("AbsoluteLimit"))
          this->setAbsoluteLimit(
            param.get<RFType>("AbsoluteLimit"));
        if (param.hasKey("MinLinearReduction"))
          this->setMinLinearReduction(
            param.get<RFType>("MinLinearReduction"));
        if (param.hasKey("FixedLinearReduction"))
          this->setFixedLinearReduction(
            param.get<bool>("FixedLinearReduction"));
        if (param.hasKey("ReassembleThreshold"))
          this->setReassembleThreshold(
            param.get<RFType>("ReassembleThreshold"));
        if (param.hasKey("LineSearchStrategy"))
          this->setLineSearchStrategy(
            param.get<std::string>("LineSearchStrategy"));
        if (param.hasKey("LineSearchMaxIterations"))
          this->setLineSearchMaxIterations(
            param.get<unsigned int>("LineSearchMaxIterations"));
        if (param.hasKey("LineSearchDampingFactor"))
          this->setLineSearchDampingFactor(
            param.get<RFType>("LineSearchDampingFactor"));
        if (param.hasKey("KeepMatrix"))
          this->setKeepMatrix(
            param.get<bool>("KeepMatrix"));
      }
    }; // end class Newton
  } // end namespace PDELab
} // end namespace Dune

#endif // DUNE_PDELAB_NEWTON_HH
