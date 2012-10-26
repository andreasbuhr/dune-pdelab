// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_PDELAB_GENERICDATAHANDLE_HH
#define DUNE_PDELAB_GENERICDATAHANDLE_HH

#include <vector>
#include <set>

#include<dune/common/exceptions.hh>
#include <dune/common/mpihelper.hh>
#include <dune/common/static_assert.hh>

#include <dune/grid/common/datahandleif.hh>
#include <dune/grid/common/gridenums.hh>

#include <dune/pdelab/gridfunctionspace/entitycontainerindexcache.hh>

namespace Dune {
  namespace PDELab {

    //! Communication descriptor for sending one item of type E per DOF.
    template<typename E>
    struct DOFDataCommunicationDescriptor
    {

      typedef E DataType;

      template<typename GFS>
      bool contains(const GFS& gfs, int dim, int codim) const
      {
        return gfs.dataHandleContains(dim,codim);
      }

      template<typename GFS>
      bool fixedSize(const GFS& gfs, int dim, int codim) const
      {
        return gfs.dataHandleFixedSize(dim,codim);
      }

      template<typename GFS, typename Entity>
      std::size_t size(const GFS& gfs, const Entity& e) const
      {
        return gfs.dataHandleSize(e);
      }

    };

    //! Communication descriptor for sending count items of type E per entity with attached DOFs.
    template<typename E>
    struct EntityDataCommunicationDescriptor
    {

      typedef E DataType;

      template<typename GFS>
      bool contains(const GFS& gfs, int dim, int codim) const
      {
        return gfs.dataHandleContains(dim,codim);
      }

      template<typename GFS>
      bool fixedSize(const GFS& gfs, int dim, int codim) const
      {
        return true;
      }

      template<typename GFS, typename Entity>
      std::size_t size(const GFS& gfs, const Entity& e) const
      {
        return gfs.dataHandleContains(Entity::dimension,Entity::codimension) ? _count : 0;
      }

      explicit EntityDataCommunicationDescriptor(std::size_t count = 1)
        : _count(count)
      {}

    private:

      const std::size_t _count;

    };

    //! Implement a data handle with a grid function space.
    /**
     * \tparam GFS                      a grid function space
     * \tparam V                        a vector container associated with the GFS
     * \tparam GatherScatter            gather/scatter methods with argumemts buffer, and data
     * \tparam CommunicationDescriptor  A descriptor for the communication structure
     */
    template<typename GFS, typename V, typename GatherScatter, typename CommunicationDescriptor = DOFDataCommunicationDescriptor<typename V::ElementType> >
    class GFSDataHandle
      : public Dune::CommDataHandleIF<GFSDataHandle<GFS,V,GatherScatter,CommunicationDescriptor>,typename CommunicationDescriptor::DataType>
    {

    public:

      typedef typename CommunicationDescriptor::DataType DataType;
      typedef typename GFS::Traits::SizeType size_type;

      GFSDataHandle(const GFS& gfs, V& v, GatherScatter gather_scatter = GatherScatter(), CommunicationDescriptor communication_descriptor = CommunicationDescriptor())
        : _gfs(gfs)
        , _index_cache(gfs)
        , _local_view(v)
        , _gather_scatter(gather_scatter)
        , _communication_descriptor(communication_descriptor)
      {}

      //! returns true if data for this codim should be communicated
      bool contains(int dim, int codim) const
      {
        return _communication_descriptor.contains(_gfs,dim,codim);
      }

      //!  \brief returns true if size per entity of given dim and codim is a constant
      bool fixedsize(int dim, int codim) const
      {
        return _communication_descriptor.fixedSize(_gfs,dim,codim);
      }

      /*!  \brief how many objects of type DataType have to be sent for a given entity

        Note: Only the sender side needs to know this size.
      */
      template<typename Entity>
      size_type size(const Entity& e) const
      {
        return _communication_descriptor.size(_gfs,e);
      }

      //! \brief pack data from user to message buffer
      template<typename MessageBuffer, typename Entity>
      void gather(MessageBuffer& buff, const Entity& e) const
      {
        _index_cache.update(e);
        _local_view.bind(_index_cache);
        if (_gather_scatter.gather(buff,e,_local_view))
          _local_view.commit();
        _local_view.unbind();
      }

      /*! \brief unpack data from message buffer to user

        n is the number of objects sent by the sender
      */
      template<typename MessageBuffer, typename Entity>
      void scatter(MessageBuffer& buff, const Entity& e, size_type n)
      {
        _index_cache.update(e);
        _local_view.bind(_index_cache);
        if (_gather_scatter.scatter(buff,n,e,_local_view))
          _local_view.commit();
        _local_view.unbind();
      }

    private:

      typedef EntityContainerIndexCache<GFS> IndexCache;
      typedef typename V::template LocalView<IndexCache> LocalView;

      const GFS& _gfs;
      mutable IndexCache _index_cache;
      mutable LocalView _local_view;
      mutable GatherScatter _gather_scatter;
      CommunicationDescriptor _communication_descriptor;

    };


    template<typename GatherScatter>
    class DataGatherScatter
    {

    public:

      typedef std::size_t size_type;

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool gather(MessageBuffer& buff, const Entity& e, const LocalView& local_view) const
      {
        for (std::size_t i = 0; i < local_view.size(); ++i)
          _gather_scatter.gather(buff,local_view[i]);
        return false;
      }

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool scatter(MessageBuffer& buff, size_type n, const Entity& e, LocalView& local_view) const
      {
        if (local_view.cache().gridFunctionSpace().containsPartition(e.partitionType()))
          {
            if (local_view.size() != n)
              DUNE_THROW(Exception,"size mismatch in GridFunctionSpace data handle, have " << local_view.size() << "DOFs, but received " << n);

            for (std::size_t i = 0; i < local_view.size(); ++i)
              _gather_scatter.scatter(buff,local_view[i]);
            return true;
          }
        else
          {
            if (local_view.size() != 0)
              DUNE_THROW(Exception,"expected no DOFs in partition '" << e.partitionType() << "', but have " << local_view.size());

            for (std::size_t i = 0; i < local_view.size(); ++i)
              {
                typename LocalView::ElementType dummy;
                buff.read(dummy);
              }
            return false;
          }
      }

      DataGatherScatter(GatherScatter gather_scatter = GatherScatter())
        : _gather_scatter(gather_scatter)
      {}

    private:

      GatherScatter _gather_scatter;

    };


    template<typename GatherScatter>
    class DataEntityGatherScatter
    {

    public:

      typedef std::size_t size_type;

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool gather(MessageBuffer& buff, const Entity& e, const LocalView& local_view) const
      {
        for (std::size_t i = 0; i < local_view.size(); ++i)
          _gather_scatter.gather(buff,e,local_view[i]);
        return false;
      }

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool scatter(MessageBuffer& buff, size_type n, const Entity& e, LocalView& local_view) const
      {
        if (local_view.cache().gridFunctionSpace().containsPartition(e.partitionType()))
          {
            if (local_view.size() != n)
              DUNE_THROW(Exception,"size mismatch in GridFunctionSpace data handle, have " << local_view.size() << "DOFs, but received " << n);

            for (std::size_t i = 0; i < local_view.size(); ++i)
              _gather_scatter.scatter(buff,e,local_view[i]);
            return true;
          }
        else
          {
            if (local_view.size() != 0)
              DUNE_THROW(Exception,"expected no DOFs in partition '" << e.partitionType() << "', but have " << local_view.size());

            for (std::size_t i = 0; i < local_view.size(); ++i)
              {
                typename LocalView::ElementType dummy;
                buff.read(dummy);
              }
            return false;
          }
      }

      DataEntityGatherScatter(GatherScatter gather_scatter = GatherScatter())
        : _gather_scatter(gather_scatter)
      {}

    private:

      GatherScatter _gather_scatter;

    };


    template<typename GatherScatter>
    class DataContainerIndexGatherScatter
    {

    public:

      typedef std::size_t size_type;

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool gather(MessageBuffer& buff, const Entity& e, const LocalView& local_view) const
      {
        for (std::size_t i = 0; i < local_view.size(); ++i)
          _gather_scatter.gather(buff,local_view.cache().container_index(i),local_view[i]);
        return false;
      }

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool scatter(MessageBuffer& buff, size_type n, const Entity& e, LocalView& local_view) const
      {
        if (local_view.cache().gridFunctionSpace().containsPartition(e.partitionType()))
          {
            if (local_view.size() != n)
              DUNE_THROW(Exception,"size mismatch in GridFunctionSpace data handle, have " << local_view.size() << "DOFs, but received " << n);

            for (std::size_t i = 0; i < local_view.size(); ++i)
              _gather_scatter.scatter(buff,local_view.cache().container_index(i),local_view[i]);

            return true;
          }
        else
          {
            if (local_view.size() != 0)
              DUNE_THROW(Exception,"expected no DOFs in partition '" << e.partitionType() << "', but have " << local_view.size());

            for (std::size_t i = 0; i < local_view.size(); ++i)
              {
                typename LocalView::ElementType dummy;
                buff.read(dummy);
              }
            return false;
          }
      }

      DataContainerIndexGatherScatter(GatherScatter gather_scatter = GatherScatter())
        : _gather_scatter(gather_scatter)
      {}

    private:

      GatherScatter _gather_scatter;

    };


    class AddGatherScatter
    {
    public:
      template<class MessageBuffer, class DataType>
      void gather (MessageBuffer& buff, DataType& data) const
      {
        buff.write(data);
      }

      template<class MessageBuffer, class DataType>
      void scatter (MessageBuffer& buff, DataType& data) const
      {
        DataType x;
        buff.read(x);
        data += x;
      }
    };

    template<class GFS, class V>
    class AddDataHandle
      : public GFSDataHandle<GFS,V,DataGatherScatter<AddGatherScatter> >
    {
      typedef GFSDataHandle<GFS,V,DataGatherScatter<AddGatherScatter> > BaseT;

    public:

      AddDataHandle (const GFS& gfs_, V& v_)
        : BaseT(gfs_,v_)
      {}
    };

    class AddClearGatherScatter
    {
    public:
      template<class MessageBuffer, class DataType>
      void gather (MessageBuffer& buff, DataType& data) const
      {
        buff.write(data);
        data = (DataType) 0;
      }

      template<class MessageBuffer, class DataType>
      void scatter (MessageBuffer& buff, DataType& data) const
      {
        DataType x;
        buff.read(x);
        data += x;
      }
    };

    template<class GFS, class V>
    class AddClearDataHandle
      : public GFSDataHandle<GFS,V,DataGatherScatter<AddClearGatherScatter> >
    {
      typedef GFSDataHandle<GFS,V,DataGatherScatter<AddClearGatherScatter> > BaseT;

    public:

      AddClearDataHandle (const GFS& gfs_, V& v_)
        : BaseT(gfs_,v_)
      {}
    };

    class CopyGatherScatter
    {
    public:
      template<class MessageBuffer, class DataType>
      void gather (MessageBuffer& buff, DataType& data) const
      {
        buff.write(data);
      }

      template<class MessageBuffer, class DataType>
      void scatter (MessageBuffer& buff, DataType& data) const
      {
        DataType x;
        buff.read(x);
        data = x;
      }
    };

    template<class GFS, class V>
    class CopyDataHandle
      : public GFSDataHandle<GFS,V,DataGatherScatter<CopyGatherScatter> >
    {
      typedef GFSDataHandle<GFS,V,DataGatherScatter<CopyGatherScatter >> BaseT;

    public:

      CopyDataHandle (const GFS& gfs_, V& v_)
        : BaseT(gfs_,v_)
      {}
    };

    class MinGatherScatter
    {
    public:
      template<class MessageBuffer, class DataType>
      void gather (MessageBuffer& buff, DataType& data) const
      {
        buff.write(data);
      }

      template<class MessageBuffer, class DataType>
      void scatter (MessageBuffer& buff, DataType& data) const
      {
        DataType x;
        buff.read(x);
        data = std::min(data,x);
      }
    };

    template<class GFS, class V>
    class MinDataHandle
      : public GFSDataHandle<GFS,V,DataGatherScatter<MinGatherScatter> >
    {
      typedef GFSDataHandle<GFS,V,DataGatherScatter<MinGatherScatter> > BaseT;

    public:

      MinDataHandle (const GFS& gfs_, V& v_)
        : BaseT(gfs_,v_)
      {}
    };

    class MaxGatherScatter
    {
    public:
      template<class MessageBuffer, class DataType>
      void gather (MessageBuffer& buff, DataType& data) const
      {
        buff.write(data);
      }

      template<class MessageBuffer, class DataType>
      void scatter (MessageBuffer& buff, DataType& data) const
      {
        DataType x;
        buff.read(x);
        data = std::max(data,x);
      }
    };

    template<class GFS, class V>
    class MaxDataHandle
      : public GFSDataHandle<GFS,V,DataGatherScatter<MaxGatherScatter> >
    {
      typedef GFSDataHandle<GFS,V,DataGatherScatter<MaxGatherScatter> > BaseT;

    public:

      MaxDataHandle (const GFS& gfs_, V& v_)
        : BaseT(gfs_,v_)
      {}
    };


    //! GatherScatter functor for marking ghost DOFs.
    /**
     * This data handle will mark all ghost DOFs (more precisely, all DOFs associated
     * with entities not part of either the interior or the border partition).
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::InteriorBorder_All_Interface.
     */
    class GhostGatherScatter
    {
    public:

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool gather(MessageBuffer& buff, const Entity& e, LocalView& local_view) const
      {
        // Figure out where we are...
        const bool ghost = e.partitionType()!=Dune::InteriorEntity && e.partitionType()!=Dune::BorderEntity;

        // ... and send something (doesn't really matter what, we'll throw it away on the receiving side).
        buff.write(ghost);

        return false;
      }

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool scatter(MessageBuffer& buff, std::size_t n, const Entity& e, LocalView& local_view) const
      {
        // Figure out where we are - we have to do this again on the receiving side due to the asymmetric
        // communication interface!
        const bool ghost = e.partitionType()!=Dune::InteriorEntity && e.partitionType()!=Dune::BorderEntity;

        // drain buffer
        bool dummy;
        buff.read(dummy);

        for (std::size_t i = 0; i < local_view.size(); ++i)
            local_view[i] = ghost;

        return true;
      }

    };

    //! Data handle for marking ghost DOFs.
    /**
     * This data handle will mark all ghost DOFs (more precisely, all DOFs associated
     * with entities not part of either the interior or the border partition).
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::InteriorBorder_All_Interface.
     */
    template<class GFS, class V>
    class GhostDataHandle
      : public Dune::PDELab::GFSDataHandle<GFS,
                                           V,
                                           GhostGatherScatter,
                                           EntityDataCommunicationDescriptor<bool> >
    {
      typedef Dune::PDELab::GFSDataHandle<
        GFS,
        V,
        GhostGatherScatter,
        EntityDataCommunicationDescriptor<bool>
        > BaseT;

      dune_static_assert((is_same<typename V::ElementType,bool>::value),
                         "GhostDataHandle expects a vector of bool values");

    public:

      //! Creates a new GhostDataHandle.
      /**
       * Creates a new GhostDataHandle and by default initializes the result vector
       * with the correct value of false. If you have already done that externally,
       * you can skip the initialization.
       *
       * \param gfs_         The GridFunctionSpace to operate on.
       * \param v_           The result vector.
       * \param init_vector  Flag to control whether the result vector will be initialized.
       */
      GhostDataHandle(const GFS& gfs_, V& v_, bool init_vector = true)
        : BaseT(gfs_,v_)
      {
        if (init_vector)
          v_ = false;
      }
    };


    //! GatherScatter functor for creating a disjoint DOF partitioning.
    /**
     * This functor will associate each DOF with a unique rank, creating a nonoverlapping partitioning
     * of the unknowns. The rank for a DOF is chosen by finding the lowest rank on which the associated
     * grid entity belongs to either the interior or the border partition.
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::InteriorBorder_All_Interface and the result vector must be initialized with the MPI rank value.
     */
    template<typename RankIndex>
    class DisjointPartitioningGatherScatter
    {

    public:

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool gather(MessageBuffer& buff, const Entity& e, LocalView& local_view) const
      {
        for (std::size_t i = 0; i < local_view.size(); ++i)
          {
            // We only gather from interior and border entities, so we can throw in our ownership
            // claim without any further checks.
            buff.write(_rank);
          }
        return true;
      }

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool scatter(MessageBuffer& buff, std::size_t n, const Entity& e, LocalView& local_view) const
      {
        // Value used for DOFs with currently unknown rank.
        const RankIndex unknown_rank = std::numeric_limits<RankIndex>::max();

        // We can only own this DOF if it is either on the interior or border partition.
        const bool is_interior_or_border = (e.partitionType()==Dune::InteriorEntity || e.partitionType()==Dune::BorderEntity);

        for (std::size_t i = 0; i < local_view.size(); ++i)
          {
            // Get the currently stored owner rank for this DOF.
            RankIndex current_rank = local_view[i];

            // We only gather from interior and border entities, so we need to make sure
            // we relinquish any ownership claims on overlap and ghost entities on the
            // receiving side. We also need to make sure not to overwrite any data already
            // received, so we only blank the rank value if the currently stored value is
            // equal to our own rank.
            if (!is_interior_or_border && current_rank == _rank)
              current_rank = unknown_rank;

            // Receive data.
            RankIndex received_rank;
            buff.read(received_rank);

            // Assign DOFs to minimum rank value.
            local_view[i] = std::min(current_rank,received_rank);
          }
        return true;
      }

      //! Create a DisjointPartitioningGatherScatter object.
      /**
       * \param rank  The MPI rank of the current process.
       */
      DisjointPartitioningGatherScatter(RankIndex rank)
        : _rank(rank)
      {}

    private:

      const RankIndex _rank;

    };

    //! GatherScatter data handle for creating a disjoint DOF partitioning.
    /**
     * This data handle will associate each DOF with a unique rank, creating a nonoverlapping partitioning
     * of the unknowns. The rank for a DOF is chosen by finding the lowest rank on which the associated
     * grid entity belongs to either the interior or the border partition.
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::InteriorBorder_All_Interface and the result vector must be initialized with the MPI rank value.
     */
    template<class GFS, class V>
    class DisjointPartitioningDataHandle
      : public Dune::PDELab::GFSDataHandle<GFS,
                                           V,
                                           DisjointPartitioningGatherScatter<
                                             typename V::ElementType
                                             >,
                                           EntityDataCommunicationDescriptor<
                                             typename V::ElementType
                                             >
                                           >
    {
      typedef Dune::PDELab::GFSDataHandle<
        GFS,
        V,
        DisjointPartitioningGatherScatter<
          typename V::ElementType
          >,
        EntityDataCommunicationDescriptor<
          typename V::ElementType
          >
        > BaseT;

    public:

      //! Creates a new DisjointPartitioningDataHandle.
      /**
       * Creates a new DisjointPartitioningDataHandle and by default initializes the
       * result vector with the current MPI rank. If you have already done that
       * externally, you can skip the initialization.
       *
       * \param gfs_         The GridFunctionSpace to operate on.
       * \param v_           The result vector.
       * \param init_vector  Flag to control whether the result vector will be initialized.
       */
      DisjointPartitioningDataHandle(const GFS& gfs_, V& v_, bool init_vector = true)
        : BaseT(gfs_,v_,DisjointPartitioningGatherScatter<typename V::ElementType>(gfs_.gridView().comm().rank()))
      {
        if (init_vector)
          v_ = gfs_.gridView().comm().rank();
      }
    };


    //! GatherScatter functor for marking shared DOFs.
    /**
     * This functor will mark all DOFs that exist on multiple processes.
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::All_All_Interface and the result vector must be initialized with false.
     */
    struct SharedDOFGatherScatter
    {

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool gather(MessageBuffer& buff, const Entity& e, LocalView& local_view) const
      {
        buff.write(local_view.size() > 0);
        return false;
      }

      template<typename MessageBuffer, typename Entity, typename LocalView>
      bool scatter(MessageBuffer& buff, std::size_t n, const Entity& e, LocalView& local_view) const
      {
        bool remote_entity_has_dofs;
        buff.read(remote_entity_has_dofs);

        for (std::size_t i = 0; i < local_view.size(); ++i)
          {
            local_view[i] |= remote_entity_has_dofs;
          }
        return true;
      }

    };


    //! Data handle for marking shared DOFs.
    /**
     * This data handle will mark all DOFs that exist on multiple processes.
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::All_All_Interface and the result vector must be initialized with false.
     */
    template<class GFS, class V>
    class SharedDOFDataHandle
      : public Dune::PDELab::GFSDataHandle<GFS,
                                           V,
                                           SharedDOFGatherScatter,
                                           EntityDataCommunicationDescriptor<bool> >
    {
      typedef Dune::PDELab::GFSDataHandle<
        GFS,
        V,
        SharedDOFGatherScatter,
        EntityDataCommunicationDescriptor<bool>
        > BaseT;

      dune_static_assert((is_same<typename V::ElementType,bool>::value),
                         "SharedDOFDataHandle expects a vector of bool values");

    public:

      //! Creates a new SharedDOFDataHandle.
      /**
       * Creates a new SharedDOFDataHandle and by default initializes the result vector
       * with the correct value of false. If you have already done that externally,
       * you can skip the initialization.
       *
       * \param gfs_         The GridFunctionSpace to operate on.
       * \param v_           The result vector.
       * \param init_vector  Flag to control whether the result vector will be initialized.
       */
      SharedDOFDataHandle(const GFS& gfs_, V& v_, bool init_vector = true)
        : BaseT(gfs_,v_)
      {
        if (init_vector)
          v_ = false;
      }
    };


    //! Data handle for collecting set of neighboring MPI ranks.
    /**
     * This data handle collects the MPI ranks of all processes that share grid entities
     * with attached DOFs.
     *
     * \note In order to work correctly, the data handle must be communicated on the
     * Dune::All_All_Interface.
     */
    template<typename GFS, typename RankIndex>
    class GFSNeighborDataHandle
      : public Dune::CommDataHandleIF<GFSNeighborDataHandle<GFS,RankIndex>,RankIndex>
    {

      // We deliberately avoid using the GFSDataHandle here, as we don't want to incur the
      // overhead of invoking the whole GFS infrastructure.

    public:

      typedef RankIndex DataType;
      typedef typename GFS::Traits::SizeType size_type;

      GFSNeighborDataHandle(const GFS& gfs, RankIndex rank, std::set<RankIndex>& neighbors)
        : _gfs(gfs)
        , _rank(rank)
        , _neighbors(neighbors)
      {}

      bool contains(int dim, int codim) const
      {
        // Only create neighbor relations for codims used by the GFS.
        return _gfs.dataHandleContains(dim,codim);
      }

      bool fixedsize(int dim, int codim) const
      {
        // We always send a single value, the MPI rank.
        return true;
      }

      template<typename Entity>
      size_type size(Entity& e) const
      {
        return 1;
      }

      template<typename MessageBuffer, typename Entity>
      void gather(MessageBuffer& buff, const Entity& e) const
      {
        buff.write(_rank);
      }

      template<typename MessageBuffer, typename Entity>
      void scatter(MessageBuffer& buff, const Entity& e, size_type n)
      {
        RankIndex rank;
        buff.read(rank);
        _neighbors.insert(rank);
      }

    private:

      const GFS& _gfs;
      const RankIndex _rank;
      std::set<RankIndex>& _neighbors;

    };


  } // namespace PDELab
} // namespace Dune

#endif // DUNE_PDELAB_GENERICDATAHANDLE_HH
