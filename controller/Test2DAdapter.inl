//
// Component for dynamic remeshing.
//
// Few notes for the all brave adventurers.
// * There is a thin line between operations performed in rest shape and in
//   deformed shape. Specificaly the element geometry is analysed in rest
//   shape, but point tracking for cutting support is (mostly) done in deformed
//   shape.

#ifndef SOFA_COMPONENT_CONTROLLER_TEST2DADAPTER_INL
#define SOFA_COMPONENT_CONTROLLER_TEST2DADAPTER_INL

#include "../initPluginShells.h"

#include <sofa/core/objectmodel/KeypressedEvent.h>
#include <sofa/core/visual/VisualParams.h>  
#include <sofa/helper/rmath.h>
#include <sofa/helper/system/thread/debug.h>
#include <sofa/component/topology/TopologyData.inl>

//#include <sofa/component/collision/ComponentMouseInteraction.h>
//#include <sofa/component/collision/PointModel.h>
//#include <sofa/component/collision/LineModel.h>
#include <sofa/component/collision/TriangleModel.h>

#include <sofa/gui/GUIManager.h>
#include <sofa/gui/BaseGUI.h>
#include <sofa/gui/BaseViewer.h>

#include "Test2DAdapter.h"


namespace sofa
{

namespace component
{

namespace controller
{


template<class DataTypes>
void Test2DAdapter<DataTypes>::PointInfoHandler::applyDestroyFunction(unsigned int pointIndex, PointInformation& /*pInfo*/)
{
    //std::cout << "pt " << __FUNCTION__ << pointIndex << std::endl;
    adapter->m_toUpdate.erase(pointIndex);
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::PointInfoHandler::swap(unsigned int i1,
    unsigned int i2)
{
    //std::cout << "pt " << __FUNCTION__ << " " << i1 << " " << i2 << std::endl;
    Inherited::swap(i1, i2);

    // m_pointId may change
    if (adapter->m_pointId == i1) {
        adapter->m_pointId = i2;
    } else if (adapter->m_pointId == i2) {
        adapter->m_pointId = i1;
    }

    // Update indices in m_toUpdate
    if (adapter->m_toUpdate[i1] && !adapter->m_toUpdate[i2]) {
        adapter->m_toUpdate.erase(i1);
        adapter->m_toUpdate[i2] = true;
    } else if (adapter->m_toUpdate[i2] && !adapter->m_toUpdate[i1]) {
        adapter->m_toUpdate.erase(i2);
        adapter->m_toUpdate[i1] = true;
    }
}




template<class DataTypes>
void Test2DAdapter<DataTypes>::TriangleInfoHandler::applyCreateFunction(
    unsigned int triangleIndex, TriangleInformation &tInfo,
    const topology::Triangle& elem,
    const sofa::helper::vector< unsigned int > &/*ancestors*/,
    const sofa::helper::vector< double > &/*coeffs*/)
{
    //std::cout << "tri " << __FUNCTION__ << triangleIndex << " (" << elem << ") [" << adapter->m_container->getTriangle(triangleIndex) << "]" << std::endl;

    // Check if vertices are on the boundary
    // NOTE: We cannot do any complicated checking here because the topology
    //       may be inconsistent.
    const Triangle& t = adapter->m_container->getTriangle(triangleIndex);
    adapter->m_toUpdate[t[0]] = true;
    adapter->m_toUpdate[t[1]] = true;
    adapter->m_toUpdate[t[2]] = true;

    // Compute initial normal
    adapter->computeTriangleNormal(elem, adapter->m_state->read(sofa::core::VecCoordId::restPosition())->getValue(), tInfo.normal);
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::TriangleInfoHandler::applyDestroyFunction(unsigned int triangleIndex, TriangleInformation &/*tInfo*/)
{
    //std::cout << "tri " << __FUNCTION__ << triangleIndex << std::endl;
    // Check if vertices are on the boundary
    // NOTE: We cannot do any complicated checking here because the topology
    //       may be inconsistent.
    const Triangle& t = adapter->m_container->getTriangle(triangleIndex);
    adapter->m_toUpdate[t[0]] = true;
    adapter->m_toUpdate[t[1]] = true;
    adapter->m_toUpdate[t[2]] = true;
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::TriangleInfoHandler::swap(unsigned int i1,
    unsigned int i2)
{
    //std::cout << "tri " << __FUNCTION__ << " " << i1 << " " << i2 << std::endl;
    Inherited::swap(i1, i2);

    if (adapter->m_pointTriId == i1) {
        adapter->m_pointTriId = i2;
    } else if (adapter->m_pointTriId == i2) {
        adapter->m_pointTriId = i1;
    }
}

#if 0
template<class DataTypes>
void Test2DAdapter<DataTypes>::TriangleInfoHandler::addOnMovedPosition(const sofa::helper::vector<unsigned int> &indexList,
    const sofa::helper::vector< topology::Triangle > & elems)
{
    std::cout << "tri " << __FUNCTION__ << " " << indexList << std::endl;
    Inherited::addOnMovedPosition(indexList, elems);
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::TriangleInfoHandler::removeOnMovedPosition(const sofa::helper::vector<unsigned int> &indices)
{
    std::cout << "tri " << __FUNCTION__ << " " << indices << std::endl;
    Inherited::removeOnMovedPosition(indices);
}


#endif








template<class DataTypes>
Test2DAdapter<DataTypes>::Test2DAdapter()
: m_sigma(initData(&m_sigma, (Real)0.01, "sigma", "Minimal increase in functional to accept the change"))
, m_functionals(initData(&m_functionals, "functionals", "Current values of the functional for each triangle"))
, m_affinity(initData(&m_affinity, (Real)0.7, "affinity", "Threshold for point attachment (value betwen 0 and 1)."))
, stepCounter(0)
, m_precision(1e-8)
, m_pointId(InvalidID)
, m_gracePeriod(0)
, m_cutEdge(InvalidID)
, m_cutPoints(0)
, pointInfo(initData(&pointInfo, "pointInfo", "Internal point data"))
, triInfo(initData(&triInfo, "triInfo", "Internal triangle data"))
{
    pointHandler = new PointInfoHandler(this, &pointInfo);
    triHandler = new TriangleInfoHandler(this, &triInfo);
}

template<class DataTypes>
Test2DAdapter<DataTypes>::~Test2DAdapter()
{
    if(pointHandler) delete pointHandler;
    if(triHandler) delete triHandler;
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::init()
{
    m_state = dynamic_cast<sofa::core::behavior::MechanicalState<DataTypes>*> (this->getContext()->getMechanicalState());
    if (!m_state) {
        serr << "Unable to find MechanicalState" << sendl;
        return;
    }

    this->getContext()->get(m_container);
    if (m_container == NULL) {
        serr << "Unable to find triangular topology" << sendl;
        return;
    }

    this->getContext()->get(m_modifier);
    if (m_modifier == NULL) {
        serr << "Unable to find TriangleSetTopologyModifier" << sendl;
        return;
    }

    this->getContext()->get(m_algoGeom);
    if (m_algoGeom == NULL) {
        serr << "Unable to find TriangleSetGeometryAlgorithms" << sendl;
        return;
    }

    this->getContext()->get(m_algoTopo);
    if (m_algoTopo == NULL) {
        serr << "Unable to find TriangleSetTopologyAlgorithms" << sendl;
        return;
    }

    pointInfo.createTopologicalEngine(m_container, pointHandler);
    pointInfo.registerTopologicalData();

    triInfo.createTopologicalEngine(m_container, triHandler);
    triInfo.registerTopologicalData();


    reinit();
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::reinit()
{
    *this->f_listening.beginEdit() = true;
    this->f_listening.endEdit();

    if ((m_sigma.getValue() < (Real)0.0) || (m_sigma.getValue() > (Real)1.0)) {
        serr << "The value of sigma must be between 0 and 1." << sendl;
        *m_sigma.beginEdit() = 0.01;
        m_sigma.endEdit();
    }
    
    m_functionals.beginEdit()->resize(m_container->getNbTriangles(), Real(0.0));
    m_functionals.endEdit();

    if (m_affinity.getValue() < 0.0 || m_affinity.getValue() > 1.0) {
        *m_affinity.beginEdit() = 0.7;
        m_affinity.endEdit();
    }

    helper::vector<PointInformation>& pts = *pointInfo.beginEdit();
    pts.resize(m_container->getNbPoints());
    //for (int i=0; i<m_container->getNbPoints(); i++)
    //{
    //    pointHandler->applyCreateFunction(
    //        i,
    //        pts[i],
    //        m_container->getPoint(i),
    //        (const sofa::helper::vector< unsigned int > )0,
    //        (const sofa::helper::vector< double >)0);
    //}
    pointInfo.endEdit();


    helper::vector<TriangleInformation>& tris = *triInfo.beginEdit();
    tris.resize(m_container->getNbTriangles());
    for (int i=0; i<m_container->getNbTriangles(); i++)
    {
        triHandler->applyCreateFunction(
            i,
            tris[i],
            m_container->getTriangle(i),
            (const sofa::helper::vector< unsigned int > )0,
            (const sofa::helper::vector< double >)0);
    }
    triInfo.endEdit();

    recheckBoundary();

    stepCounter = 0;
}


template<class DataTypes>
void Test2DAdapter<DataTypes>::onEndAnimationStep(const double /*dt*/)
{
    //std::cout << "CPU step\n";

    if ((m_container == NULL) || (m_state == NULL))
        return;

    stepCounter++;
    if (m_gracePeriod > 0) m_gracePeriod--;

    // Perform the (delayed) cutting
    if ((m_cutList.size() > 0) && (m_cutPoints > 2) ) {
        VecIndex newList, endList;
        bool bReachedBorder;
        m_algoTopo->InciseAlongEdgeList(m_cutList, newList, endList,
            bReachedBorder);
        //m_algoTopo->InciseAlongEdge(m_cutList[0], NULL);
        m_cutList.clear();
    }

    // Update boundary vertices
    recheckBoundary();

    //if (stepCounter < f_interval.getValue())
    //    return; // Stay still for a few steps

    //stepCounter = 0;

    Data<VecCoord>* datax = m_state->write(sofa::core::VecCoordId::restPosition());
    //VecCoord& x = *datax->beginEdit();
    // WARNING: Notice that we're working on a copy that is NOT updated by
    //          external changes!
    VecCoord x = datax->getValue();
    //
    const VecCoord& xrest = datax->getValue();

    Index nTriangles = m_container->getNbTriangles();
    if (nTriangles == 0)
        return;

    // Update projection of tracked point in rest shape
    if (m_pointId != InvalidID) {
        Triangle tri = m_container->getTriangle(m_pointTriId);
        helper::vector< double > bary = m_algoGeom->compute3PointsBarycoefs(
            m_point, tri[0], tri[1], tri[2], false);
        m_pointRest =
            xrest[ tri[0] ] * bary[0] +
            xrest[ tri[1] ] * bary[1] +
            xrest[ tri[2] ] * bary[2];
    }


    //sofa::helper::system::thread::ctime_t start, stop;
    //sofa::helper::system::thread::CTime timer;
    //start = timer.getTime();

    vector<Real> &functionals = *m_functionals.beginEdit();

    functionals.resize(nTriangles);

    // Compute initial metrics
    for (Index i=0; i < nTriangles; i++) {
        Triangle t = m_container->getTriangle(i);
        functionals[i] = funcTriangle(t, x, triInfo.getValue()[i].normal);
    }
    //std::cout << "m: " << functionals << "\n";

    ngamma = 0;
    sumgamma = maxgamma = 0.0;
    mingamma = 1.0;

    Real maxdelta=0.0;
    unsigned int moved=0;
    for (Index i=0; i<x.size(); i++) {
        if (pointInfo.getValue()[i].isFixed()) {
            //std::cout << "skipping fixed node " << i << "\n";
            continue;
        }

        Vec3 xold = x[i];
        //if (!smoothLaplacian(i, x, functionals, normals))
        //if (!smoothOptimizeMin(i, x, functionals, normals))
        if (!smoothOptimizeMax(i, x, functionals))
        //if (!smoothPain2D(i, x, functionals, normals))
        {
            x[i] = xold;
        } else {
            // Move the point
            relocatePoint(i, x[i]);

            // Update boundary vertices
            recheckBoundary();

            moved++;
            Real delta = (x[i] - xold).norm2();
            if (delta > maxdelta) {
                maxdelta = delta;
            }

            // Update projection of tracked point in rest shape
            if (m_pointId == i) {
                Triangle tri = m_container->getTriangle(m_pointTriId);
                helper::vector< double > bary = m_algoGeom->compute3PointsBarycoefs(
                    m_point, tri[0], tri[1], tri[2], false);
                m_pointRest =
                    xrest[ tri[0] ] * bary[0] +
                    xrest[ tri[1] ] * bary[1] +
                    xrest[ tri[2] ] * bary[2];
            }


        }
    }

    //stop = timer.getTime();
    //std::cout << "---------- CPU time = " << stop-start << "\n";

    // Evaluate improvement
    Real sum=0.0, sum2=0.0, min = 1.0;
    for (Index i=0; i < nTriangles; i++) {
        if (functionals[i] < min) {
            min = functionals[i];
        }
        sum += functionals[i];
        sum2 += functionals[i] * functionals[i];
    }
    sum /= nTriangles;
    sum2 = helper::rsqrt(sum2/nTriangles);

    // Check the values around attached point and reattach if necessary
    if ((m_pointId != InvalidID) && !m_gracePeriod && !cutting()) {
        TrianglesAroundVertex N1 =
            m_container->getTrianglesAroundVertex(m_pointId);
        Real min = 1.0;
        for (unsigned int it=0; it<N1.size(); it++) {
            Real f = metricGeom(m_container->getTriangle(N1[it]), x,
                triInfo.getValue()[ N1[it] ].normal);
            if (f < min) min = f;
        }
        if (min < m_affinity.getValue()) {
            // Value too low, try reattaching the node
            m_gracePeriod = 20;
            Triangle t = m_container->getTriangle(m_pointTriId);
            VecIndex pts;
            for (int i=0; i<3; i++) {
                if (t[i] != m_pointId) pts.push_back(t[i]);
            }
            if ((x[m_pointId] - x[ pts[0] ]).norm2() <
                (x[m_pointId] - x[ pts[1] ]).norm2()) {
                m_pointId = pts[0];
            } else {
                m_pointId = pts[1];
            }
        }
    }

    //std::cout << stepCounter << "] moved " << moved << " points, max delta=" << helper::rsqrt(maxdelta)
    //    << " gamma min/avg/max: " << mingamma << "/" << sumgamma/ngamma
    //    << "/" << maxgamma

    //    << " Quality min/avg/RMS: " << min << "/" << sum << "/" << sum2

    //    << "\n";

    datax->endEdit();

    m_functionals.endEdit();

    //// Write metrics to file
    //std::ofstream of("/tmp/metrics.csv", std::ios::app);
    //of << "geom," << stepCounter;
    //for (Index i=0; i < m_functionals.getValue().size(); i++) {
    //    of << "," << m_functionals.getValue()[i];
    //}
    //of << "\n";
    //of.close();
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::onKeyPressedEvent(core::objectmodel::KeypressedEvent *key)
{
    if (key->getKey() == 'M') { // Ctrl+M
        std::cout << "Writing metrics to file /tmp/metrics.csv\n";
        // Write metrics to file
        std::ofstream of("/tmp/metrics.csv", std::ios::app);
        of << "geom," << stepCounter;
        for (Index i=0; i < m_functionals.getValue().size(); i++) {
            of << "," << m_functionals.getValue()[i];
        }
        of << "\n";
        of.close();
    }
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::setTrackedPoint(const collision::BodyPicked &picked)
{
    using namespace sofa::component::collision;

    const VecCoord& x = m_state->read(
        sofa::core::ConstVecCoordId::restPosition())->getValue();

    // Support only trianglular model! The others don't give any added value.
    /*if(dynamic_cast<PointModel*>(picked.body)) {
    // Point
    m_pointId = picked.indexCollisionElement;
    } else if(dynamic_cast<LineModel*>(picked.body)) {
    // Edge
    Edge e = m_container->getEdge(picked.indexCollisionElement);
    Real d1 = (x[ e[0] ] - m_point).norm2();
    Real d2 = (x[ e[1] ] - m_point).norm2();
    m_pointId = (d1 < d2 ? e[0] : e[1]);
    } else*/ if(dynamic_cast<TriangleModel*>(picked.body)) {

        // TODO: can we tell directly from bary coords?
        Triangle t = m_container->getTriangle(
            picked.indexCollisionElement);
        Real d1 = (x[ t[0] ] - picked.point).norm2();
        Real d2 = (x[ t[1] ] - picked.point).norm2();
        Real d3 = (x[ t[2] ] - picked.point).norm2();
        Index newId = (d1 < d2) ?
            (d1 < d3 ? t[0] : t[2]) :
            (d2 < d3 ? t[1] : t[2]);
        // TODO: Durign cutting we should allow change only to point directly
        // connected with last cut point.
        if (!m_gracePeriod && !cutting() && (newId != m_pointId)) {
            m_pointId = newId;
            m_gracePeriod = 20;
        }
        if (newId == m_pointId) {
            m_point = picked.point;
            m_pointTriId = picked.indexCollisionElement;
        }
    } else {
        m_pointId = InvalidID;
    }
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::addCuttingPoint()
{
    if (!m_algoTopo || !m_algoGeom) return;

    if (m_pointId == InvalidID) {
        serr << "BUG! Attempted cutting with no point tracked." << sendl;
        return;
    }

    const VecCoord& x = m_state->read(
        sofa::core::ConstVecCoordId::position())->getValue();
    //const VecCoord& xrest = m_state->read(
    //    sofa::core::ConstVecCoordId::restPosition())->getValue();
    Coord oldpos = x[m_pointId];

    bool bFirst = !cutting();

    // Make sure the tracked point is at the target location.
    if (pointInfo[m_pointId].type == PointInformation::NORMAL) {
        relocatePoint(m_pointId, m_point, m_pointTriId, false);
    } else {
        // TODO: We need a parameter controling how close one has to be to the
        // boundary/fixed node to attach to it. For boundary nodes we have to
        // project the target position onto the boundary nodes. Fixed points,
        // of course, have to be kept intact. Or alternatively we may prevent
        // attachment to fixed nodes completely.
        serr << "Handling of boundary/fixed nodes is not implemented!" << serr;
    }


    // Chose another point in the direction of movement.
    // TODO: the following is not good
    Vec3 dir = m_point - oldpos;
    dir.normalize();
    // END_TODO

    // Get another point in that direction.
    Index tId = m_algoGeom->getTriangleInDirection(m_pointId, dir);
    if (tId == InvalidID) {
        serr << "BUG! Nothing in cutting direction!" << sendl;
        return;
    }

    EdgesInTriangle elist = m_container->getEdgesInTriangle(tId);
    Real alpha[2];
    Index otherPt[2], otherEdge[2];
    for (int i=0,j=0; i<3; i++) {
        Edge e = m_container->getEdge(elist[i]);
        if ((e[0] != m_pointId) && (e[1] != m_pointId)) continue;
        Vec3 edgeDir;
        if (e[0] == m_pointId) {
            otherPt[j] = e[1];
            edgeDir = x[ e[1] ] - x[ e[0] ];
        } else {
            otherPt[j] = e[0];
            edgeDir = x[ e[0] ] - x[ e[1] ];
        }
        otherEdge[j] = elist[i];
        edgeDir.normalize();
        alpha[j] = dir * edgeDir;
        j++;
    }

    Index newPt = (alpha[0] < alpha[1]) ? otherPt[1] : otherPt[0];
    Index oldPt = m_pointId;
    Index edge = (alpha[0] < alpha[1]) ? otherEdge[1] : otherEdge[0];

    m_cutPoints++;

    // Attach the new point and move it to be near the cursor.
    m_pointId = newPt;
    m_pointTriId = tId;
    m_gracePeriod = 20;
    relocatePoint(newPt, x[oldPt] + (dir*m_precision/2.0), tId, false);

    // Split the edge. If this is first cut point and the point is not on the
    // border just remeber it and do the operation when another cut point is
    // inserted.
    //if (bFirst &&
    //    (pointInfo[oldPt].type == PointInformation::NORMAL)) {
    //    m_cutEdge = edge;
    //    // For now just fix the point.
    //    (*pointInfo.beginEdit())[oldPt].type = PointInformation::FIXED;
    //    pointInfo.endEdit();
    //} else {
    //    if (m_cutEdge != InvalidID) {
    //        m_cutList.push_back(m_cutEdge);
    //        m_cutEdge = InvalidID;
    //    }
    //    m_cutList.push_back(edge);
    //    // TODO: remove this
    //    (*pointInfo.beginEdit())[oldPt].type = PointInformation::FIXED;
    //    pointInfo.endEdit();
    //}
    m_cutLastPoint = oldPt;
    if (bFirst) {
        m_cutEdge = edge;
        // For now just fix the point.
        (*pointInfo.beginEdit())[oldPt].forceFixed = true;
        pointInfo.endEdit();
    } else {
        if (m_cutEdge != InvalidID) {
            m_cutList.push_back(m_cutEdge);
        }
        m_cutEdge = edge;
        // Fix the point so nothing happens to it before the topological change
        // occurs.
        (*pointInfo.beginEdit())[oldPt].forceFixed = true;
        pointInfo.endEdit();
    }
}

template<class DataTypes>
bool Test2DAdapter<DataTypes>::smoothLaplacian(Index v, VecCoord &x, vector<Real> &metrics, vector<Vec3> normals)
{
    Vec3 xold = x[v];

    // Compute new position
    EdgesAroundVertex N1e = m_container->getEdgesAroundVertex(v);

    // Compute centroid of polygon from 1-ring around the vertex
    Vec3 xnew(0,0,0);
    for (Index ie=0; ie<N1e.size(); ie++) {
        Edge e = m_container->getEdge(N1e[ie]); 
        for (int n=0; n<2; n++) {
            if (e[n] != v) {
                xnew += x[ e[n] ];
            }
        }
    }
    x[v] = xnew / N1e.size();

    TrianglesAroundVertex N1 = m_container->getTrianglesAroundVertex(v);

    // Check if this improves the mesh
    //
    // Note: To track element inversion we either need a normal computed
    // from vertex normals, or assume the triangle was originaly not
    // inverted. Now we do the latter.
    //
    // We accept any change that doesn't decreas worst metric for the
    // triangle set.

    bool bAccepted = false;
    for (int iter=10; iter>0 && !bAccepted; iter--) {

        if ((xold - x[v]).norm2() < 1e-8) {
            // No change in position
            //std::cout << "No change in position for " << v << "\n";
            break;
        }

        Real oldworst = 1.0, newworst = 1.0;
        for (Index it=0; it<N1.size(); it++) {
            Real newmetric = funcTriangle(m_container->getTriangle(N1[it]), x,
                normals[ N1[it] ]);

            if (metrics[ N1[it] ] < oldworst) {
                oldworst = metrics[ N1[it] ];
            }

            if (newmetric < newworst) {
                newworst = newmetric;
            }
        }
        //std::cout << "cmp: " << newworst << " vs. " << oldworst << "\n";
        if (newworst < (oldworst + m_sigma.getValue())) {
            //std::cout << "   --rejected " << xold << " -> " << x[v] << "\n";
            // The correct step size is best found empiricaly
            //x[v] = (x[v] + xold)/2.0;
            x[v] = (x[v] + xold)*2.0/3.0;
        } else {
            //std::cout << "   --accepted: " << xold << " -> " << x[v] << "\n";
            bAccepted = true;
        }
    }

    if (bAccepted) {
        // Update metrics
        for (Index it=0; it<N1.size(); it++) {
            metrics[ N1[it] ] = funcTriangle(m_container->getTriangle(N1[it]), x,
                normals[ N1[it] ]);
        }
    }
    // NOTE: Old position restore by caller (if needed).

    return bAccepted;
}

template<class DataTypes>
bool Test2DAdapter<DataTypes>::smoothOptimizeMax(Index v, VecCoord &x, vector<Real> &metrics)
{
    Vec3 xold = x[v];

#if 1
    // Compute gradients
    TrianglesAroundVertex N1 = m_container->getTrianglesAroundVertex(v);
    helper::vector<Vec3> grad(N1.size());
    Real delta = m_precision/10.0;
    // NOTE: Constrained to 2D!
    for (int component=0; component<2; component++) {
        x[v] = xold;
        x[v][component] += delta;
        for (Index it=0; it<N1.size(); it++) {
            Real m = funcTriangle(m_container->getTriangle(N1[it]), x,
                triInfo.getValue()[ N1[it] ].normal);
            grad[it][component] = (m - metrics[ N1[it] ])/delta;
        }
    }

    // Find smallest metric with non-zero gradient
    Index imin = InvalidID;
    Real mmin = 1.0;
    //std::cout << v << " metrics: ";
    for (Index it=0; it<N1.size(); it++) {
        if (metrics[ N1[it] ] < mmin && grad[it].norm2() > 1e-15) {
            imin = it;
            mmin = metrics[ N1[it] ];
        }
        //std::cout << metrics[ N1[it] ] << "(" << grad[it].norm() << "/"
        //    << grad[it].norm2()<< "), ";
    }
    if (imin == InvalidID) {
        //std::cout << "   doing nothing" << "\n";
        return false;
    //} else {
    //    std::cout << "   using " << imin << "\n";
    }

    Vec3 step = grad[imin];
#else
    //
    // Minimaze the mean, i.e.: F = 1/n Σ_i f_i
    //
    // TODO

    Vec3 grad(0.0, 0.0, 0.0); // F = 1/n Σ_i f_i

    // Compute gradients
    TrianglesAroundVertex N1 = m_container->getTrianglesAroundVertex(v);
    helper::vector<Vec3> grad(N1.size());
    Real delta = m_precision/10.0;
    // NOTE: Constrained to 2D!
    for (int component=0; component<2; component++) {
        x[v] = xold;
        x[v][component] += delta;
        for (Index it=0; it<N1.size(); it++) {
            Real m = funcTriangle(m_container->getTriangle(N1[it]), x,
                triInfo.getValue()[ N1[it] ].normal);
            grad[it][component] = (m - metrics[ N1[it] ])/delta;
        }
    }
    Vec3 step = ...;
#endif

    // Find out step size
    Real gamma = 0.01; // ≈ m_precision * 2^10

    //gamma *= step.norm();
    step.normalize();

    // Note: The following method from [CTS98] underestimates the value and
    //       leads to slow convergence. It is ok to start with large value, we
    //       verify the benefit later anyway.
    //for (Index it=0; it<N1.size(); it++) {
    //    if (dot(grad[it], step) > 0)
    //        continue;
    //    Real tmp = (metrics[ N1[it] ] - metrics[ N1[imin] ]) / (
    //        1.0 - dot(grad[it], step)); // dot(step, step) == 1 for unit step vector
    //    assert(tmp > 0.0);
    //    //if (tmp < 0.0) {
    //    //    std::cout << "Eeeks! gamma=" << tmp << " partials:\n"
    //    //        << "grad[imin] = " << grad[imin] << "\n"
    //    //        << "grad[it]   = " << grad[it] << "\n"
    //    //        << "m =  " << metrics[ N1[it] ] << "\n"
    //    //        << "m' = " << metrics[ N1[imin] ] << "\n";
    //    //}
    //    if (tmp < gamma) {
    //        gamma = tmp;
    //    }
    //}
    // Fixed the previous
    //for (Index it=0; it<N1.size(); it++) {
    //    if (dot(grad[it], step) > 0)
    //        continue;
    //    Real tmp = (metrics[ N1[imin] ] - metrics[ N1[it] ]) /
    //        dot(grad[it], step);
    //    assert(tmp > 0.0);
    //    //if (tmp < 0.0) {
    //    //    std::cout << "Eeeks! gamma=" << tmp << " partials:\n"
    //    //        << "grad[imin] = " << grad[imin] << "\n"
    //    //        << "grad[it]   = " << grad[it] << "\n"
    //    //        << "m =  " << metrics[ N1[it] ] << "\n"
    //    //        << "m' = " << metrics[ N1[imin] ] << "\n";
    //    //}
    //    if (tmp < gamma) {
    //        gamma = tmp;
    //    }
    //}
    //std::cout << "gamma=" << gamma << " grad=" << step << "\n";

    // If it's boundary node project it onto the boundary line
    const PointInformation &pt = pointInfo.getValue()[v];
    if (pt.isBoundary()) { // && !pt.isFixed()
        step = pt.boundary * (pt.boundary*step);
    }

    x[v] = xold + gamma*step;

    // Check if this improves the mesh
    //
    // Note: To track element inversion we either need a normal computed
    // from vertex normals, or assume the triangle was originaly not
    // inverted. Now we do the latter.
    //
    // We accept any change that doesn't decreas worst metric for the
    // triangle set.

    bool bAccepted = false;
    for (int iter=10; iter>0 && !bAccepted; iter--) {

        if ((xold - x[v]).norm2() < m_precision) {
            // No change in position
            //std::cout << "No change in position for " << v << "\n";
            break;
        }

        Real oldworst = 1.0, newworst = 1.0;
        for (Index it=0; it<N1.size(); it++) {
            Real newmetric = funcTriangle(m_container->getTriangle(N1[it]), x,
                triInfo.getValue()[ N1[it] ].normal);

            if (metrics[N1[it]] < oldworst) {
                oldworst = metrics[N1[it]];
            }

            if (newmetric < newworst) {
                newworst = newmetric;
            }
        }
        //std::cout << "cmp: " << newworst << " vs. " << oldworst << "\n";
        if (newworst < (oldworst + m_sigma.getValue())) {
            //std::cout << "   --rejected " << xold << " -> " << x[v]
            //    << " worst: " << oldworst << " -> " << newworst
            //    << " (" << (newworst-oldworst) << ")\n";
            //x[v] = (x[v] + xold)/2.0;
            gamma *= 2.0/3.0;
            //gamma /= 2.0;
            x[v] = xold + gamma*step;
        } else {
            //std::cout << "   --accepted: " << xold << " -> " << x[v]
            //    << " gamma=" << gamma << "\n";
            //    << " worst: " << oldworst << " -> " << newworst
            //    << " (" << (newworst-oldworst) << ")\n";
            sumgamma += gamma; ngamma++;
            if (gamma < mingamma) mingamma = gamma;
            if (gamma > maxgamma) maxgamma = gamma;
            bAccepted = true;
        }
    }

    if (bAccepted) {
        // Update metrics
        for (Index it=0; it<N1.size(); it++) {
            metrics[ N1[it] ] = funcTriangle(m_container->getTriangle(N1[it]), x,
                triInfo.getValue()[ N1[it] ].normal);
        }
    }
    // NOTE: Old position restored by caller (if needed).

    return bAccepted;
}

template<class DataTypes>
bool Test2DAdapter<DataTypes>::smoothOptimizeMin(Index v, VecCoord &x, vector<Real> &metrics, vector<Vec3> normals)
{
    Vec3 xold = x[v];

    // Compute gradients
    TrianglesAroundVertex N1 = m_container->getTrianglesAroundVertex(v);
    helper::vector<Vec3> grad(N1.size());
    Real delta = 1e-5;
    // NOTE: Constrained to 2D!
    for (int component=0; component<2; component++) {
        x[v] = xold;
        x[v][component] += delta;
        for (Index it=0; it<N1.size(); it++) {
            Real m = funcTriangle(m_container->getTriangle(N1[it]), x,
                normals[N1[it]]);
            grad[it][component] = (m - metrics[ N1[it] ])/delta;
        }
    }
    //std::cout << "grads: " << grad << "\n";

    // Find largest metric with non-zero gradient
    Index imax = 0;
    Real mmax = 0.0;
    //std::cout << "metrics: ";
    for (Index it=0; it<N1.size(); it++) {
        if (metrics[ N1[it] ] > mmax && grad[it].norm2() > 1e-15) {
            imax = it;
            mmax = metrics[ N1[it] ];
        }
        //std::cout << metrics[ N1[it] ] << "(" << grad[it].norm() << "/"
        //    << grad[it].norm2()<< "), ";
    }
    //std::cout << "\n";

    Vec3 step = grad[imax];

    // Find out step size
    Real gamma = -0.05;

    //gamma *= step.norm();
    step.normalize();

    // Note: The following method from [CTS98] underestimates the value and
    //       leads to slow convergence. It is ok to start with large value, we
    //       verify the benefit later anyway.
    //for (Index it=0; it<N1.size(); it++) {
    //    if (dot(grad[it], step) > 0)
    //        continue;
    //    Real tmp = (metrics[ N1[it] ] - metrics[ N1[imax] ]) / (
    //        1.0 - dot(grad[it], step)); // dot(step, step) == 1 for unit step vector
    //    assert(tmp > 0.0);
    //    //if (tmp < 0.0) {
    //    //    std::cout << "Eeeks! gamma=" << tmp << " partials:\n"
    //    //        << "grad[imax] = " << grad[imax] << "\n"
    //    //        << "grad[it]   = " << grad[it] << "\n"
    //    //        << "m =  " << metrics[ N1[it] ] << "\n"
    //    //        << "m' = " << metrics[ N1[imax] ] << "\n";
    //    //}
    //    if (tmp < gamma) {
    //        gamma = tmp;
    //    }
    //}
    // Fixed the previous
    //for (Index it=0; it<N1.size(); it++) {
    //    if (dot(grad[it], step) > 0)
    //        continue;
    //    Real tmp = (metrics[ N1[imax] ] - metrics[ N1[it] ]) /
    //        dot(grad[it], step);
    //    assert(tmp > 0.0);
    //    //if (tmp < 0.0) {
    //    //    std::cout << "Eeeks! gamma=" << tmp << " partials:\n"
    //    //        << "grad[imax] = " << grad[imax] << "\n"
    //    //        << "grad[it]   = " << grad[it] << "\n"
    //    //        << "m =  " << metrics[ N1[it] ] << "\n"
    //    //        << "m' = " << metrics[ N1[imax] ] << "\n";
    //    //}
    //    if (tmp < gamma) {
    //        gamma = tmp;
    //    }
    //}
    //std::cout << "gamma=" << gamma << " grad=" << step << "\n";
    x[v] = xold + gamma*step;

    // Check if this improves the mesh
    //
    // Note: To track element inversion we either need a normal computed
    // from vertex normals, or assume the triangle was originaly not
    // inverted. Now we do the latter.
    //
    // We accept any change that doesn't decreas worst metric for the
    // triangle set.

    bool bAccepted = false;
    for (int iter=10; iter>0 && !bAccepted; iter--) {

        if ((xold - x[v]).norm2() < 1e-8) {
            // No change in position
            //std::cout << "No change in position for " << v << "\n";
            break;
        }

        //Real oldworst = 1.0, newworst = 1.0;
        Real oldworst = 0.0, newworst = 0.0;
        for (Index it=0; it<N1.size(); it++) {
            Real newmetric = funcTriangle(m_container->getTriangle(N1[it]), x,
                normals[ N1[it] ]);

            if (metrics[N1[it]] > oldworst) {
                oldworst = metrics[N1[it]];
            }

            if (newmetric > newworst) {
                newworst = newmetric;
            }
        }
        //std::cout << "cmp: " << newworst << " vs. " << oldworst << "\n";
        if (newworst > (oldworst - m_sigma.getValue())) {
            //std::cout << "   --rejected " << xold << " -> " << x[v]
            //    << " worst: " << oldworst << " -> " << newworst
            //    << " (" << (newworst-oldworst) << ")\n";
            //x[v] = (x[v] + xold)/2.0;
            gamma *= 2.0/3.0;
            //gamma /= 2.0;
            x[v] = xold + gamma*step;
        } else {
            //std::cout << "   --accepted: " << xold << " -> " << x[v]
            //    << " gamma=" << gamma << "\n"
            //    << " worst: " << oldworst << " -> " << newworst
            //    << " (" << (newworst-oldworst) << ")\n";
            sumgamma += gamma; ngamma++;
            if (gamma < mingamma) mingamma = gamma;
            if (gamma > maxgamma) maxgamma = gamma;
            bAccepted = true;
        }
    }

    if (bAccepted) {
        // Update metrics
        for (Index it=0; it<N1.size(); it++) {
            metrics[ N1[it] ] = funcTriangle(m_container->getTriangle(N1[it]), x,
                normals[ N1[it] ]);
        }
    }
    // NOTE: Old position restore by caller (if needed).

    return bAccepted;
}

template<class DataTypes>
bool Test2DAdapter<DataTypes>::smoothPain2D(Index v, VecCoord &x, vector<Real> &metrics, vector<Vec3> normals)
{
    Real w = 0.5, sigma = 0.01;

    Vec3 xold = x[v];
    Vec2 old2 = Vec2(xold[0], xold[1]);

    Mat22 A;
    Vec2 q;

    EdgesAroundVertex N1e = m_container->getEdgesAroundVertex(v);
    for (Index ie=0; ie<N1e.size(); ie++) {
        Edge e = m_container->getEdge(N1e[ie]); 

        Mat22 M(Vec2(1.0,0.0), Vec2(0.0,1.0));
        //Mat22 M = Mlist[ N1e[ie] ];

        Vec3 other;
        for (int n=0; n<2; n++) {
            if (e[n] != v) {
                other = x[ e[n] ];
            }
        }

        A += M;
        q += M * Vec2(other[0], other[1]);
    }

    Mat22 D;
    for (int n=0; n<2; n++) {
        // D_jj = max { A_jj , (1+s) Σ_m!=j |A_jm| }
        Real offdiag = (1.0+sigma) * helper::rabs(A[n][(n+1)%2]);
        if (A[n][n] < offdiag) {
            D[n][n] = offdiag; 
        } else {
            D[n][n] = A[n][n];
        }
    }

    // Solve: (D + A)(xnew - old2) = w(q - A old2)
    // ==> new = (D + A)^{-1} w (q - A old2) + old2
    Mat22 DAi;
    DAi.invert(D+A);
    Vec2 xnew = (q - A * old2) * w;
    xnew = DAi * xnew;
    xnew += old2;

    x[v] = Vec3(xnew[0], xnew[1], 0.0);

    // Check if this improves the mesh
    //
    // Note: To track element inversion we either need a normal computed
    // from vertex normals, or assume the triangle was originaly not
    // inverted. Now we do the latter.
    //
    // We accept any change that doesn't decreas worst metric for the
    // triangle set.

    TrianglesAroundVertex N1 = m_container->getTrianglesAroundVertex(v);

    bool bAccepted = false;
    for (int iter=1; iter>0 && !bAccepted; iter--) {

        if ((xold - x[v]).norm2() < 1e-8) {
            // No change in position
            //std::cout << "No change in position for " << v << "\n";
            break;
        }

        Real oldworst = 1.0, newworst = 1.0;
        for (Index it=0; it<N1.size(); it++) {
            Real newmetric = funcTriangle(m_container->getTriangle(N1[it]), x,
                normals[ N1[it] ]);

            if (metrics[N1[it]] < oldworst) {
                oldworst = metrics[N1[it]];
            }

            if (newmetric < newworst) {
                newworst = newmetric;
            }
        }
        //std::cout << "cmp: " << newworst << " vs. " << oldworst << "\n";
        if (newworst < (oldworst + m_sigma.getValue())) {
            //std::cout << "   --rejected " << xold << " -> " << x[v]
            //    << " worst: " << oldworst << " -> " << newworst
            //    << " (" << (newworst-oldworst) << ")\n";
            x[v] = (x[v] + xold)/2.0;
        } else {
            //std::cout << "   --accepted: " << xold << " -> " << x[v]
            //    << " gamma=" << gamma << "\n";
            //    << " worst: " << oldworst << " -> " << newworst
            //    << " (" << (newworst-oldworst) << ")\n";
            bAccepted = true;
        }
    }

    if (bAccepted) {
        // Update metrics
        for (Index it=0; it<N1.size(); it++) {
            metrics[ N1[it] ] = funcTriangle(m_container->getTriangle(N1[it]), x,
                triInfo.getValue()[ N1[it] ].normal);
        }
    }
    // NOTE: Old position restore by caller (if needed).

    return bAccepted;
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::computeTriangleNormal(const Triangle &t, const VecCoord &x, Vec3 &normal) const
{
    Vec3 A, B;
    A = x[ t[1] ] - x[ t[0] ];
    B = x[ t[2] ] - x[ t[0] ];

    Real An = A.norm(), Bn = B.norm();
    if (An < 1e-20 || Bn < 1e-20) {
        serr << "Found degenerated triangle: "
            << x[ t[0] ] << " / " << x[ t[1] ] << " / " << x[ t[2] ]
            << " :: " << An << ", " << Bn << sendl;

        normal = Vec3(0,0,0);
        return;
    }

    A /= An;
    B /= Bn;
    normal = cross(A, B);
    normal.normalize();
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::recheckBoundary()
{
    helper::vector<PointInformation>& pts = *pointInfo.beginEdit();
    for (std::map<Index,bool>::const_iterator i=m_toUpdate.begin();
        i != m_toUpdate.end(); i++) {
        pts[i->first].type = detectNodeType(i->first, pts[i->first].boundary);
    }
    pointInfo.endEdit();
    m_toUpdate.clear();
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::relocatePoint(Index pt, Coord target,
    Index hint, bool bInRest)
{
    if (m_modifier == NULL ||
        m_algoGeom == NULL ||
        m_container == NULL ||
        m_state == NULL)
        return;

    Index tId = hint;

    const VecCoord& x = m_state->read(
        bInRest
        ? sofa::core::ConstVecCoordId::restPosition()
        : sofa::core::ConstVecCoordId::position()
        )->getValue();

    if ((x[pt] - target).norm() < m_precision) return; // Nothing to do

    if (tId == InvalidID) {
        tId = m_algoGeom->getTriangleInDirection(pt, target - x[pt]);
    }

    if (tId == InvalidID) {
        // Try once more and more carefully
        TrianglesAroundVertex N1 = m_container->getTrianglesAroundVertex(pt);
        for (Index it=0; it<N1.size(); it++) {
            Index t;
            if (m_algoGeom->isPointInsideTriangle(N1[it], false, target, t, bInRest)) {
                Triangle tri = m_container->getTriangle(N1[it]);
                helper::vector<double> bary =
                    m_algoGeom->compute3PointsBarycoefs(
                        target, tri[0], tri[1], tri[2], bInRest);
                bool bGood = true;
                for (int bc=0; bc<3; bc++) {
                    if (bary[bc] < -1e-15) {
                        bGood = false;
                        break;
                    }
                }

                if (!bGood) continue;

                tId = N1[it];
                break;
            }
        }
    }

    if (tId == InvalidID) {
        // We screwed up something. Probably a triangle got inverted accidentaly.
        serr << "Unexpected triangle Id -1! Cannot move point "
            << pt << ", marking point as fixed." << sendl;

        (*pointInfo.beginEdit())[pt].forceFixed = true;
        pointInfo.endEdit();

        //EdgesAroundVertex N1e = m_container->getEdgesAroundVertex(pt);
        //for (Index ip=0; ip<N1e.size(); ip++) {
        //    Edge e = m_container->getEdge(N1e[ip]);
        //    std::cout << m_container->getPointDataArray().getValue()[ e[0] ] << " " << m_container->getPointDataArray().getValue()[ e[1] ] << "\n";
        //}

        return;
    }

    Triangle tri = m_container->getTriangle(tId);

    helper::vector <unsigned int> move_ids;
    helper::vector< helper::vector< unsigned int > > move_ancestors;
    helper::vector< helper::vector< double > > move_coefs;

    move_ids.push_back(pt);

    move_ancestors.resize(1);
    move_ancestors.back().push_back(tri[0]);
    move_ancestors.back().push_back(tri[1]);
    move_ancestors.back().push_back(tri[2]);

    move_coefs.push_back(
        m_algoGeom->compute3PointsBarycoefs(target,
            tri[0], tri[1], tri[2], bInRest));

    if (hint != InvalidID) {
    std::cout << "hinted move: " << x[pt] << " -> " << target <<
        " :: " << move_coefs.back() << "\n";
    }

    // Do the real work
    m_modifier->movePointsProcess(move_ids, move_ancestors, move_coefs);
    m_modifier->notifyEndingEvent();
    m_modifier->propagateTopologicalChanges();

    // Check
    //const VecCoord& xnew = m_state->read(
    //    sofa::core::ConstVecCoordId::restPosition())->getValue();
    //std::cout << "requested " << target << "\tgot " << xnew[pt] << "\tdelta"
    //    << (target - xnew[pt]).norm() << "\n";
}

template<class DataTypes>
typename Test2DAdapter<DataTypes>::PointInformation::NodeType Test2DAdapter<DataTypes>::detectNodeType(Index pt, Vec3 &boundaryDirection)
{
    if (m_container == NULL)
        return PointInformation::NORMAL;

    const VecCoord& x = m_state->read(sofa::core::ConstVecCoordId::restPosition())->getValue();

    VecVec3 dirlist; // Directions of boundary edges

    //std::cout << "checking " << pt << "\n";
    EdgesAroundVertex N1e = m_container->getEdgesAroundVertex(pt);
    for (Index ie=0; ie<N1e.size(); ie++) {
        TrianglesAroundEdge tlist = m_container->getTrianglesAroundEdge(N1e[ie]);
        unsigned int count = tlist.size();
        //std::cout << "  " << count << " (" << tlist << ")\n";
        if (count < 1 || count > 2) {
            // What a strange topology! Better not touch ...
            return PointInformation::FIXED;
        } else if (count == 1) {
            Edge e = m_container->getEdge(N1e[ie]);
            Vec3 dir;
            if (e[0] == pt) {
                dir = x[e[1]] - x[e[0]];
            } else {
                dir = x[e[0]] - x[e[1]];
            }
            dir.normalize();
            dirlist.push_back(dir);
        }
    }

    if (dirlist.size() == 0) {
        // No boundary edges
        return PointInformation::NORMAL;
    } else if (dirlist.size() != 2) {
        // What a strange topology! Better not touch ...
        return PointInformation::FIXED;
    }

    if ((1.0 + dirlist[0] * dirlist[1]) > 1e-15) {
        // Not on a line
        return PointInformation::FIXED;
    }

    boundaryDirection = dirlist[0];

    return PointInformation::BOUNDARY;
}


template<class DataTypes>
void Test2DAdapter<DataTypes>::draw(const core::visual::VisualParams* vparams)
{
    if ((!vparams->displayFlags().getShowBehaviorModels()))
        return;

    if (!m_state) return;
    const VecCoord& x = m_state->read(sofa::core::ConstVecCoordId::position())->getValue();

    helper::vector<defaulttype::Vector3> boundary;
    helper::vector<defaulttype::Vector3> fixed;
    const helper::vector<PointInformation> &pts = pointInfo.getValue();
    if (pts.size() != x.size()) return;
    for (Index i=0; i < x.size(); i++) {
        if (pts[i].isFixed()) {
            fixed.push_back(x[i]);
        } else if (pts[i].isBoundary()) {
            boundary.push_back(x[i]);
        }
    }
    vparams->drawTool()->drawPoints(boundary, 10,
        sofa::defaulttype::Vec<4,float>(0.5, 0.5, 1.0, 1.0));
    vparams->drawTool()->drawPoints(fixed, 10,
        sofa::defaulttype::Vec<4,float>(0.8, 0.0, 0.8, 1.0));

    if (m_pointId != InvalidID) {
        // Draw tracked position
        helper::vector<defaulttype::Vector3> vv(1,
            defaulttype::Vector3(m_point[0], m_point[1], m_point[2]));
        vparams->drawTool()->drawPoints(vv, 4,
            sofa::defaulttype::Vec<4,float>(1.0, 1.0, 1.0, 1.0));
        // Draw tracked position (projected in rest shape)
        vv[0] = m_pointRest;
        vparams->drawTool()->drawPoints(vv, 4,
            sofa::defaulttype::Vec<4,float>(1.0, 1.0, 0.0, 1.0));
        // Draw attached point
        vv[0] = x[m_pointId];
        vparams->drawTool()->drawPoints(vv, 6,
            cutting()
            ? sofa::defaulttype::Vec<4,float>(1.0, 1.0, 0.0, 1.0)
            : sofa::defaulttype::Vec<4,float>(1.0, 1.0, 1.0, 1.0));
    }

    if (m_cutList.size() > 0) {
        helper::vector<defaulttype::Vector3> points;
        for (VecIndex::const_iterator i=m_cutList.begin();
            i != m_cutList.end(); i++) {
            const Edge &e = m_container->getEdge(*i);
            points.push_back(x[ e[0] ]);
            points.push_back(x[ e[1] ]);
        }
        vparams->drawTool()->drawLines(points, 4,
            sofa::defaulttype::Vec<4,float>(1.0, 0.0, 0.0, 1.0));
    }
    if (m_cutEdge != InvalidID) {
        const Edge &e = m_container->getEdge(m_cutEdge);
        helper::vector<defaulttype::Vector3> points;
        points.push_back(x[ e[0] ]);
        points.push_back(x[ e[1] ]);
        vparams->drawTool()->drawLines(points, 4,
            sofa::defaulttype::Vec<4,float>(1.0, 0.0, 0.0, 1.0));
    }
}

} // namespace controller

} // namespace component

} // namespace sofa

#endif // SOFA_COMPONENT_CONTROLLER_TEST2DADAPTER_INL
