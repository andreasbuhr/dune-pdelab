set(constraintsdir  ${CMAKE_INSTALL_INCLUDEDIR}/dune/pdelab/constraints)
set(constraints_HEADERS
    conforming.hh
    constraints.hh
    constraintsparameters.hh
    constraintstransformation.hh
    hangingnode.hh
    hangingnodemanager.hh
    interiornode.hh
    interiornodeconstraints.hh
    mfd.hh
    noconstraints.hh
    p0.hh
    p0ghost.hh
    raviartthomas0.hh)

set(SUBDIRS common)

foreach(i ${SUBDIRS})
  if(${i} STREQUAL "test")
    set(opt EXCLUDE_FROM_ALL)
  endif(${i} STREQUAL "test")
  add_subdirectory(${i} ${opt})
  unset(opt)
endforeach(i ${SUBDIRS})

install(FILES ${constraints_HEADERS} DESTINATION ${constraintsdir})
