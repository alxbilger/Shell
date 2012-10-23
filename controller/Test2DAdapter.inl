#ifndef SOFA_COMPONENT_CONTROLLER_TEST2DADAPTER_INL
#define SOFA_COMPONENT_CONTROLLER_TEST2DADAPTER_INL

#include <sofa/core/objectmodel/KeypressedEvent.h>

#include "Test2DAdapter.h"

namespace sofa
{

namespace component
{

namespace controller
{

template<class DataTypes>
Test2DAdapter<DataTypes>::Test2DAdapter()
//: f_interval(initData(&f_interval, (unsigned int)10, "interval", "Switch triangles every number of steps"))
: stepCounter(0)
{
}

template<class DataTypes>
Test2DAdapter<DataTypes>::~Test2DAdapter()
{
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::init()
{
    m_state = dynamic_cast<sofa::core::behavior::MechanicalState<DataTypes>*> (this->getContext()->getMechanicalState());
    if (!m_state)
        serr << "Unable to find MechanicalState" << sendl;

    this->getContext()->get(m_container);
    if (m_container == NULL)
        serr << "Unable to find triangular topology" << sendl;

    reinit();
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::reinit()
{
    *this->f_listening.beginEdit() = true;
    this->f_listening.endEdit();
    
    detectBoundary();

    stepCounter = 0;
}


template<class DataTypes>
void Test2DAdapter<DataTypes>::onEndAnimationStep(const double /*dt*/)
{
    if ((m_container == NULL) || (m_state == NULL))
        return;

    stepCounter++;

    //if (stepCounter < f_interval.getValue())
    //    return; // Stay still for a few steps

    //stepCounter = 0;

    Data<VecVec3>* datax = m_state->write(sofa::core::VecCoordId::position());
    VecVec3& x = *datax->beginEdit();

    Index nTriangles = m_container->getNbTriangles();
    if (nTriangles == 0)
        return;
    
    m_metrics.resize(nTriangles);
    vector<Vec3> normals(nTriangles);

    // Compute initial metrics and normals
   for (Index i=0; i < nTriangles; i++) {
        Triangle t = m_container->getTriangle(i);
        computeTriangleNormal(t, x, normals[i]);
        m_metrics[i] = metricGeom(t, x, normals[i]);
    }
    //std::cout << "m: " << m_metrics << "\n";

    ngamma = 0;
    sumgamma = maxgamma = 0.0;
    mingamma = 1.0;

    Real maxdelta=0.0;
    unsigned int moved=0;
    for (Index i=0; i<x.size(); i++) {
        if (m_boundary[i]) {
            //std::cout << "skipping boundary " << i << "\n";
            continue;
        }

        Vec3 xold = x[i];
        if (!smoothLaplacian(i, x, m_metrics, normals)) {
        //if (!smoothOptimize(i, x, m_metrics, normals)) {
            x[i] = xold;
        } else {
            moved++;
            Real delta = (x[i] - xold).norm2();
            if (delta > maxdelta) {
                maxdelta = delta;
            }
        }
    }

    // Evaluate improvement
    Real sum=0.0, sum2=0.0, min = 1.0;
    for (Index i=0; i < nTriangles; i++) {
        if (m_metrics[i] < min) {
            min = m_metrics[i];
        }
        sum += m_metrics[i];
        sum2 += m_metrics[i] * m_metrics[i];
    }
    sum /= nTriangles;
    sum2 = helper::rsqrt(sum2/nTriangles);

    std::cout << stepCounter << "] moved " << moved << " points, max delta=" << helper::rsqrt(maxdelta)
        << " gamma min/avg/max: " << mingamma << "/" << sumgamma/ngamma
        << "/" << maxgamma

        << " Quality min/avg/RMS: " << min << "/" << sum << "/" << sum2

        << "\n";

    datax->endEdit();
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::onKeyPressedEvent(core::objectmodel::KeypressedEvent *key)
{
    if (key->getKey() == 'M') { // Ctrl+M
        std::cout << "Writing metrics to file /tmp/metrics.csv\n";
        // Write metrics to file
        std::ofstream of("/tmp/metrics.csv");
        of << "geom";
        for (Index i=0; i < m_metrics.size(); i++) {
            of << "," << m_metrics[i];
        }
        of << "\n";
        of.close();
    }
}

template<class DataTypes>
bool Test2DAdapter<DataTypes>::smoothLaplacian(Index v, VecVec3 &x, vector<Real>metrics, vector<Vec3> normals)
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
            Real newmetric = metricGeom(m_container->getTriangle(N1[it]), x,
                normals[v]);

            if (metrics[ N1[it] ] < oldworst) {
                oldworst = metrics[ N1[it] ];
            }

            if (newmetric < newworst) {
                newworst = newmetric;
            }
        }
        //std::cout << "cmp: " << newworst << " vs. " << oldworst << "\n";
        if (newworst < oldworst) {
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
            metrics[ N1[it] ] = metricGeom(m_container->getTriangle(N1[it]), x,
                normals[v]);
        }
    }

    return bAccepted;
}

template<class DataTypes>
bool Test2DAdapter<DataTypes>::smoothOptimize(Index v, VecVec3 &x, vector<Real>metrics, vector<Vec3> normals)
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
            Real m = metricGeom(m_container->getTriangle(N1[it]), x,
                normals[N1[it]]);
            grad[it][component] = (m - metrics[ N1[it] ])/delta;
        }
    }
    //std::cout << "grads: " << grad << "\n";

    // Find smallest metric with non-zero gradient
    Index imin = 0;
    Real mmin = 1.0;
    //std::cout << "metrics: ";
    for (Index it=0; it<N1.size(); it++) {
        if (metrics[ N1[it] ] < mmin && grad[it].norm2() > 1e-15) {
            imin = it;
            mmin = metrics[ N1[it] ];
        }
        //std::cout << metrics[ N1[it] ] << "(" << grad[it].norm() << "/"
        //    << grad[it].norm2()<< "), ";
    }
    //std::cout << "\n";

    Vec3 step = grad[imin];

    // Find out step size
    Real gamma = 0.5;

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
    for (Index it=0; it<N1.size(); it++) {
        if (dot(grad[it], step) > 0)
            continue;
        Real tmp = (metrics[ N1[imin] ] - metrics[ N1[it] ]) /
            dot(grad[it], step);
        assert(tmp > 0.0);
        //if (tmp < 0.0) {
        //    std::cout << "Eeeks! gamma=" << tmp << " partials:\n"
        //        << "grad[imin] = " << grad[imin] << "\n"
        //        << "grad[it]   = " << grad[it] << "\n"
        //        << "m =  " << metrics[ N1[it] ] << "\n"
        //        << "m' = " << metrics[ N1[imin] ] << "\n";
        //}
        if (tmp < gamma) {
            gamma = tmp;
        }
    }
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

        Real oldworst = 1.0, newworst = 1.0;
        for (Index it=0; it<N1.size(); it++) {
            Real newmetric = metricGeom(m_container->getTriangle(N1[it]), x,
                normals[v]);

            if (metrics[N1[it]] < oldworst) {
                oldworst = metrics[N1[it]];
            }

            if (newmetric < newworst) {
                newworst = newmetric;
            }
        }
        //std::cout << "cmp: " << newworst << " vs. " << oldworst << "\n";
        if (newworst <= oldworst+1e-5) {
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
            metrics[ N1[it] ] = metricGeom(m_container->getTriangle(N1[it]), x,
                normals[v]);
        }
    }

    return bAccepted;
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::detectBoundary()
{
    if ((m_container == NULL) || (m_container->getNbTriangles() == 0))
        return;

    // TODO: maybe use TriangleSetTopologyContainer::getPointsOnBorder()
    m_boundary.resize(m_container->getNbPoints());
    for (Index i=0; i<(Index)m_container->getNbEdges(); i++) {
        TrianglesAroundEdge tlist = m_container->getTrianglesAroundEdge(i);
        if (tlist.size() != 2) {
            Edge e = m_container->getEdge(i);
            m_boundary[e[0]] = true;
            m_boundary[e[1]] = true;
        }
    }
}

template<class DataTypes>
void Test2DAdapter<DataTypes>::computeTriangleNormal(const Triangle &t, const VecVec3 &x, Vec3 &normal)
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

} // namespace controller

} // namespace component

} // namespace sofa

#endif // SOFA_COMPONENT_CONTROLLER_TEST2DADAPTER_INL
