#ifndef DUNE_PDELAB_PROBE_HH
#define DUNE_PDELAB_PROBE_HH

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <dune/common/tuples.hh>
#include <dune/pdelab/common/function.hh>

#include "fmt.hh"
#include "gnuplotgraph.hh"

namespace Dune {
  namespace PDELab {

    //
    // Interface
    //

    template<typename Imp>
    class ProbeInterface
    {
    public:
      template<typename GF>
      void measure(const GF &gf, double time = 0);
      //! function + exact solution
      template<typename GF, typename EGF>
      void measureExact(const GF& gf, const EGF& egf, double time = 0)
      { asImp().measure(gf, time); }

      template<typename GF>
      void measureFinal(const GF &gf, double time = 0);
      template<typename GF, typename EGF>
      void measureFinalExact(const GF &gf, const EGF &egf, double time = 0)
      { asImp().measureFinal(gf, time); }
    protected:
      Imp& asImp () {return static_cast<Imp &> (*this);}
      const Imp& asImp () const {return static_cast<const Imp &>(*this);}
    };

    template<template<typename GridView> class T>
    class LevelProbeFactoryInterface
    {
    public:
      template<typename GV>
      struct Traits : public T<GV> {};

      template<typename GV>
      SmartPointer<typename Traits<GV>::Probe>
      getProbe(const GV &gv, unsigned level);
    };

    template<template<typename Grid> class T>
    class GridProbeFactoryInterface
    {
    public:
      template<typename G>
      struct Traits : public T<G> {};

      template<typename G>
      SmartPointer<typename Traits<G>::LevelProbeFactory>
      levelProbeFactory(const G &grid, const std::string &tag);
    };

    //
    // ErrorProbe
    //

    // A probe which extracts some kind of error measure from the solution,
    // and provides this information afterwards

    template<typename Imp>
    class ErrorProbeInterface
      : public ProbeInterface<Imp>
    {
    public:
      double get_error() const;
    };

    //
    // DummyProbe
    //

    template<typename Imp>
    class DummyProbeDefault
      : public ProbeInterface<Imp>
    {
    public:
      template<typename GF>
      void measure(const GF &gf, double time = 0) { /* do nothing */ }

      template<typename GF>
      void measureFinal(const GF &gf, double time = 0) { /* do nothing */ }
    };

    class DummyProbe
      : public DummyProbeDefault<DummyProbe>
    {};

    //
    // Pair
    //

    template<typename P1, typename P2>
    class ProbePair
      : public ProbeInterface<ProbePair<P1, P2> >
    {
      SmartPointer<P1> p1;
      SmartPointer<P2> p2;

    public:
      ProbePair(SmartPointer<P1> p1_, SmartPointer<P2> p2_)
        : p1(p1_), p2(p2_)
      { }

      template<typename GF>
      void measure(const GF &gf, double time = 0) {
        p1->measure(gf, time);
        p2->measure(gf, time);
      }
      template<typename GF, typename EGF>
      void measureExact(const GF &gf, const EGF &egf, double time = 0) {
        p1->measureExact(gf, egf, time);
        p2->measureExact(gf, egf, time);
      }

      template<typename GF>
      void measureFinal(const GF &gf, double time = 0) {
        p1->measureFinal(gf, time);
        p2->measureFinal(gf, time);
      }
      template<typename GF, typename EGF>
      void measureFinalExact(const GF &gf, const EGF &egf, double time = 0) {
        p1->measureFinalExact(gf, egf, time);
        p2->measureFinalExact(gf, egf, time);
      }
    };

    template<typename LPF1, typename LPF2>
    class LevelProbeFactoryPair
    {
      SmartPointer<LPF1> lpf1;
      SmartPointer<LPF2> lpf2;

    public:
      template<typename GV>
      struct Traits {
        typedef ProbePair<typename LPF1::template Traits<GV>::Probe,
                          typename LPF2::template Traits<GV>::Probe> Probe;
      };

      LevelProbeFactoryPair(SmartPointer<LPF1> lpf1_, SmartPointer<LPF2> lpf2_)
        : lpf1(lpf1_), lpf2(lpf2_)
      { }

      template<typename GV>
      SmartPointer<typename Traits<GV>::Probe>
      getProbe(const GV &gv, unsigned level)
      {
        return new typename Traits<GV>::Probe(lpf1->getProbe(gv, level),
                                              lpf2->getProbe(gv, level));
      }
    };

    template<typename GPF1, typename GPF2>
    class GridProbeFactoryPair
    {
      SmartPointer<GPF1> gpf1;
      SmartPointer<GPF2> gpf2;

    public:
      template<typename G>
      struct Traits {
        typedef LevelProbeFactoryPair<
          typename GPF1::template Traits<G>::LevelProbeFactory,
          typename GPF2::template Traits<G>::LevelProbeFactory> LevelProbeFactory;
      };

      GridProbeFactoryPair(SmartPointer<GPF1> gpf1_, SmartPointer<GPF2> gpf2_)
        : gpf1(gpf1_), gpf2(gpf2_)
      { }

      template<typename G>
      SmartPointer<typename Traits<G>::LevelProbeFactory>
      levelProbeFactory(const G &grid, const std::string &tag)
      {
        return new typename Traits<G>::LevelProbeFactory(gpf1->levelProbeFactory(grid, tag),
                                                         gpf2->levelProbeFactory(grid, tag));
      }

    };
    
    template<typename GPF0, typename GPF1 = int, typename GPF2 = int, typename GPF3 = int,
             typename GPF4 = int, typename GPF5 = int, typename GPF6 = int,
             typename GPF7 = int, typename GPF8 = int, typename GPF9 = int>
    struct GridProbeFactoryListTraits;

    template<typename GPF0>
    struct GridProbeFactoryListTraits<GPF0> {
      typedef GPF0 GPF;
    };

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4, typename GPF5, typename GPF6,
             typename GPF7, typename GPF8, typename GPF9>
    struct GridProbeFactoryListTraits {
      typedef GridProbeFactoryPair<
        GPF0,
        typename GridProbeFactoryListTraits<
          GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7, GPF8, GPF9,
          int>::GPF> GPF;
    };

    template<typename GPF0>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0)
    {
      return gpf0;
    }

    template<typename GPF0, typename GPF1>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1
        >::GPF(gpf0, gpf1);
    }

    template<typename GPF0, typename GPF1, typename GPF2>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3, GPF4
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3,
                             SmartPointer<GPF4> gpf4)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3, GPF4
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3, gpf4));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4, typename GPF5>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3, GPF4, GPF5
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3,
                             SmartPointer<GPF4> gpf4,
                             SmartPointer<GPF5> gpf5)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3, GPF4, GPF5
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3, gpf4, gpf5));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4, typename GPF5, typename GPF6>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3,
                             SmartPointer<GPF4> gpf4,
                             SmartPointer<GPF5> gpf5,
                             SmartPointer<GPF6> gpf6)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3, gpf4, gpf5, gpf6));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4, typename GPF5, typename GPF6,
             typename GPF7>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3,
                             SmartPointer<GPF4> gpf4,
                             SmartPointer<GPF5> gpf5,
                             SmartPointer<GPF6> gpf6,
                             SmartPointer<GPF7> gpf7)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3, gpf4, gpf5, gpf6, gpf7));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4, typename GPF5, typename GPF6,
             typename GPF7, typename GPF8>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7, GPF8
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3,
                             SmartPointer<GPF4> gpf4,
                             SmartPointer<GPF5> gpf5,
                             SmartPointer<GPF6> gpf6,
                             SmartPointer<GPF7> gpf7,
                             SmartPointer<GPF8> gpf8)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7, GPF8
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3, gpf4, gpf5, gpf6, gpf7, gpf8));
    }

    template<typename GPF0, typename GPF1, typename GPF2, typename GPF3,
             typename GPF4, typename GPF5, typename GPF6,
             typename GPF7, typename GPF8, typename GPF9>
    SmartPointer<typename GridProbeFactoryListTraits<
                   GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7, GPF8, GPF9
                   >::GPF>
    makeGridProbeFactoryList(SmartPointer<GPF0> gpf0,
                             SmartPointer<GPF1> gpf1,
                             SmartPointer<GPF2> gpf2,
                             SmartPointer<GPF3> gpf3,
                             SmartPointer<GPF4> gpf4,
                             SmartPointer<GPF5> gpf5,
                             SmartPointer<GPF6> gpf6,
                             SmartPointer<GPF7> gpf7,
                             SmartPointer<GPF8> gpf8,
                             SmartPointer<GPF9> gpf9)
    {
      return new
        typename GridProbeFactoryListTraits<
          GPF0, GPF1, GPF2, GPF3, GPF4, GPF5, GPF6, GPF7, GPF8, GPF9
        >::GPF(gpf0,
               makeGridProbeFactoryList(gpf1, gpf2, gpf3, gpf4, gpf5, gpf6, gpf7, gpf8, gpf9));
    }

    //
    // Gnuplot
    //

    template<typename D>
    class GnuplotProbe
      : public DummyProbeDefault<GnuplotProbe<D> >
    {
      std::ostream &data;
      D x;
      unsigned &elems;
      bool &have_reference;

    public:
      GnuplotProbe(std::ostream &data_, const D &x_, unsigned &elems_,
                   bool& have_reference_)
        : data(data_), x(x_), elems(elems_), have_reference(have_reference_)
      {}

      template<typename GF>
      void measure(const GF &gf, double time = 0) {
        if(elems == 0) elems = GF::Traits::dimDomain;
        if(elems != GF::Traits::dimDomain)
          DUNE_THROW(Dune::Exception,
                     "Don't feed functions of different dimensions to the "
                     "same Gnuplot probe");

        typename GF::Traits::RangeType y;
        GridFunctionToFunctionAdapter<GF>(gf).evaluate(x, y);
        data << time;
        for(unsigned i = 0; i < GF::Traits::dimDomain; ++i)
          data << "\t" << y[i];
        data << std::endl;
      }

      template<typename GF, typename EGF>
      void measureExact(const GF &gf, const EGF &egf, double time = 0) {
        if(elems == 0) elems = GF::Traits::dimDomain;
        if(elems != GF::Traits::dimDomain)
          DUNE_THROW(Dune::Exception,
                     "Don't feed functions of different dimensions to the "
                     "same Gnuplot probe");
        have_reference = true;

        data << time;
        {
          typename GF::Traits::RangeType y;
          GridFunctionToFunctionAdapter<GF>(gf).evaluate(x, y);
          for(unsigned i = 0; i < GF::Traits::dimDomain; ++i)
            data << "\t" << y[i];
        }
        {
          typename EGF::Traits::RangeType y;
          GridFunctionToFunctionAdapter<EGF>(egf).evaluate(x, y);
          for(unsigned i = 0; i < EGF::Traits::dimDomain; ++i)
            data << "\t" << y[i];
        }
        data << std::endl;
      }

    };

    template<typename DF>
    class GnuplotLevelProbeFactory
    {
      struct PlotLine {
        PlotLine(unsigned index_ = 0, unsigned level_ = 0)
          : index(index_), level(level_), have_reference(false)
        {}

        unsigned index;
        unsigned level;
        bool have_reference;
      };

      GnuplotGraph &graph;
      std::string x;
      std::ostream::pos_type datapos;
      unsigned &index;
      PlotLine lastplot;
      std::vector<PlotLine> plots;
      unsigned elems;
      const std::string fileprefix;
      const std::string tag;

      void finishPlot() {
        if(datapos != graph.dat().tellp()) {
          graph.dat() << "\n\n";
          plots.push_back(lastplot);
          ++index;
        }
      }

      template<typename T>
      bool get(T& t, unsigned& pos) const {
        int nchars;
        double val;
        int extractions;
        while(pos != x.size()) {
          extractions = std::sscanf(x.c_str()+pos, "%lg%n", &val, &nchars);
          if(extractions == 1) { // success
            t = val;
            pos += nchars;
            return true;
          } else // didn't match
            ++pos;
        }
        return false;
      }

      void xAsVector(std::vector<DF>& xVector) const {
        xVector.clear();
        unsigned pos = 0;
        DF val;
        while(get(val, pos)) xVector.push_back(val);
      }

      template<int dim>
      void xAsFV(Dune::FieldVector<DF, dim>& xFV) const {
        xFV = 0;
        unsigned pos = 0;
        for(unsigned i = 0; i < dim; ++i)
          get(xFV[i], pos);
      }

    public:
      template<typename GV>
      struct Traits {
        typedef GnuplotProbe<Dune::FieldVector<DF, GV::dimension> > Probe;
      };

      GnuplotLevelProbeFactory(GnuplotGraph &graph_, const std::string &x_,
                               unsigned &index_,
                               const std::string &fileprefix_,
                               const std::string &tag_)
        : graph(graph_), x(x_), datapos(graph.dat().tellp()), index(index_)
        , lastplot(), plots(), elems(0), fileprefix(fileprefix_), tag(tag_)
      { }
      ~GnuplotLevelProbeFactory()
      {
        finishPlot();

        std::vector<DF> xVector;
        xAsVector(xVector);
        xVector.resize(elems, 0);

        for(unsigned comp = 0; comp < elems; ++comp) {
          std::ostringstream s;
          graph.addCommand("set title 'Probe at " + fmt(xVector) + "'");

          s << "set output '" << fileprefix << "-" << comp << ".eps'";
          graph.addCommand(s.str());
          s.str("");

          s << "set ylabel 'u_{"<< comp << "}'";
          graph.addCommand(s.str());
          s.str("");

          for(unsigned i = 0; i < plots.size(); ++i) {
            s << "'" << graph.datname() << "'"
              << " index " << plots[i].index
              << " using 1:" << comp+2
              << " title '" << tag << " level " << plots[i].level << "'"
              << " with lines";
            graph.addPlot(s.str());
            s.str("");
          }
          if(plots.size() > 0)
            if(plots[plots.size()-1].have_reference) {
              s << "'" << graph.datname() << "'"
                << " index " << plots[plots.size()-1].index
                << " using 1:" << comp+elems+2
                << " title '" << tag << " reference'"
                << " with lines ls -1";
              graph.addPlot(s.str());
              s.str("");
            }
        }
      }

      template<typename GV>
      SmartPointer<typename Traits<GV>::Probe>
      getProbe(const GV &gv, unsigned level)
      {
        Dune::FieldVector<DF, GV::dimension> xFV;
        xAsFV(xFV);
        finishPlot();
        graph.dat() << "# LEVEL" << level << std::endl;
        datapos = graph.dat().tellp();
        lastplot = PlotLine(index, level);
        return new typename Traits<GV>::Probe(graph.dat(),
                                              xFV,
                                              elems,
                                              lastplot.have_reference);
      }
    };

    template<typename DF>
    class GnuplotGridProbeFactory
    {
      const std::string x;
      std::string fileprefix;
      GnuplotGraph graph;
      unsigned index;

    public:
      template<typename G>
      struct Traits {
        typedef GnuplotLevelProbeFactory<DF> LevelProbeFactory;
      };

      GnuplotGridProbeFactory(const std::string &fileprefix_,
                              const std::string &x_)
        : x(x_), fileprefix(fileprefix_), graph(fileprefix_), index(0)
      {
        graph.addCommand("set terminal postscript eps color enhanced solid");
        graph.addCommand("");
        graph.addCommand("set key left top reverse Left");
        graph.addCommand("set xlabel 't'");
        graph.addCommand("");
      }

      template<typename G>
      SmartPointer<typename Traits<G>::LevelProbeFactory>
      levelProbeFactory(const G &grid, const std::string &tag)
      {
        return new typename Traits<G>::LevelProbeFactory(graph, x, index, fileprefix, tag);
      }
    };
    
  } // namespace PDELab
} // namespace Dune

#endif //DUNE_PDELAB_PROBE_HH
