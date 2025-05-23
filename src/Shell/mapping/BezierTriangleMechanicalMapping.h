/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 beta 4      *
*                (c) 2006-2009 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Modules                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#ifndef SOFA_COMPONENT_MAPPING_BEZIERTRIANGLEMAPPING_INL
#define SOFA_COMPONENT_MAPPING_BEZIERTRIANGLEMAPPING_INL


#include <sofa/core/Mapping.h>

#include <sofa/core/behavior/MechanicalState.h>
#include <sofa/type/vector.h>

#include <sofa/gl/GLSLShader.h>

#include <sofa/linearalgebra/CompressedRowSparseMatrix.h>
#include <sofa/component/topology/container/dynamic/TriangleSetTopologyContainer.h>
#include <sofa/simulation/AnimateBeginEvent.h>

#include <sofa/defaulttype/VecTypes.h>

#include <sofa/helper/system/thread/CTime.h>

#include <Shell/forcefield/BezierTriangularBendingFEMForceField.h>

// Use quaternions for rotations
//#define ROTQ
// We have own code to check the getJ() because checkJacobian sucks (at this
// point in time).
//#define CHECK_J

namespace sofa
{

namespace component
{

namespace mapping
{

using namespace sofa::type;
using namespace sofa::core::topology;
using namespace sofa::helper::system::thread;
using namespace core::topology;
using namespace sofa::core::behavior;

template <class TIn, class TOut>
class BezierTriangleMechanicalMapping : public core::Mapping<TIn, TOut>
{
public:
    SOFA_CLASS(SOFA_TEMPLATE2(BezierTriangleMechanicalMapping,TIn,TOut), SOFA_TEMPLATE2(core::Mapping,TIn,TOut));
    typedef core::Mapping<TIn, TOut> Inherit;
    typedef TIn In;
    typedef TOut Out;

    typedef typename In::VecCoord               InVecCoord;
    typedef typename In::VecDeriv               InVecDeriv;
    typedef typename In::Coord                  InCoord;
    typedef typename In::Deriv                  InDeriv;
    typedef typename In::MatrixDeriv            InMatrixDeriv;
    typedef typename In::Real                   InReal;

    typedef typename Out::VecCoord              OutVecCoord;
    typedef typename Out::VecDeriv              OutVecDeriv;
    typedef typename Out::Coord                 OutCoord;
    typedef typename Out::Deriv                 OutDeriv;
    typedef typename Out::MatrixDeriv           OutMatrixDeriv;
    typedef typename Out::Real                  OutReal;

    typedef typename sofa::component::forcefield::BezierTriangularBendingFEMForceField<In>    BezierFF;

    typedef InReal Real;

    typedef Vec<3, Real> Vec3;
    typedef Mat<3, 3, Real> Mat33;

    typedef sofa::type::Quat<Real> Quat;


    typedef sofa::Index Index;
    //typedef BaseMeshTopology::Edge              Edge;
    typedef BaseMeshTopology::SeqEdges          SeqEdges;
    typedef BaseMeshTopology::Triangle          Triangle;
    typedef BaseMeshTopology::SeqTriangles      SeqTriangles;

    enum { NIn = sofa::defaulttype::DataTypeInfo<InDeriv>::Size };
    enum { NOut = sofa::defaulttype::DataTypeInfo<OutDeriv>::Size };
    typedef type::Mat<NOut, NIn, Real> MBloc;
    typedef sofa::linearalgebra::CompressedRowSparseMatrix<MBloc> MatrixType;

    BezierTriangleMechanicalMapping(core::State<In>* from, core::State<Out>* to)
    : Inherit(from, to)
    , inputTopo(NULL)
    , outputTopo(NULL)
    , bezierForcefield(NULL)
    , normals(initData(&normals, "normals","Node normals at the rest shape"))
    , measureError(initData(&measureError, false, "measureError","Error with high resolution mesh"))
    , targetTopology(initLink("targetTopology","Targeted high resolution topology"))
    , verticesTarget(OutVecCoord()) // dummy initialization
    , trianglesTarget(SeqTriangles()) // dummy initialization
    , matrixJ()
    , updateJ(false)
    {
    }

    virtual ~BezierTriangleMechanicalMapping()
    {
    }

    void init() override;
    void reinit() override;
    void draw(const core::visual::VisualParams* vparams) override;


    void apply(const core::MechanicalParams *mparams, Data<OutVecCoord>& out, const Data<InVecCoord>& in) override;
    const sofa::linearalgebra::BaseMatrix* getJ(const core::MechanicalParams * mparams) override;
    void applyJ(const core::MechanicalParams *mparams, Data<OutVecDeriv>& out, const Data<InVecDeriv>& in) override;
    void applyJT(const core::MechanicalParams *mparams, Data<InVecDeriv>& out, const Data<OutVecDeriv>& in) override;
    void applyJT(const core::ConstraintParams *cparams, Data<InMatrixDeriv>& out, const Data<OutMatrixDeriv>& in) override;


#if 0
    /// For checkJacobian and to hide some deprecation warnings
    const sofa::linearalgebra::BaseMatrix* getJ() { return getJ(NULL); }
    void applyJ(Data<OutVecDeriv>& out, const Data<InVecDeriv>& in)
    { applyJ(NULL, out, in); }
    void applyJ( OutVecDeriv& out, const InVecDeriv& in)
    {
        Data<OutVecDeriv> dout;
        Data<InVecDeriv> din;
        *din.beginEdit() = in;
        din.endEdit();
        dout.beginEdit()->resize(out.size());
        dout.endEdit();
        applyJ(NULL, dout, din);
        out = dout.getValue();
    }
    void applyJT(InVecDeriv& out, const OutVecDeriv& in)
    {
        Data<OutVecDeriv> din;
        Data<InVecDeriv> dout;
        *din.beginEdit() = in;
        din.endEdit();
        dout.beginEdit()->resize(out.size());
        dout.endEdit();
        applyJT(NULL, dout, din);
        out = dout.getValue();
    }
    ///
#endif


    void handleEvent(sofa::core::objectmodel::Event *event) override {
        if (dynamic_cast<simulation::AnimateBeginEvent*>(event))
        {
            //std::cout << "begin\n";
            // We have to update the matrix at every step
            updateJ = true;
        }
    }

protected:

    BezierTriangleMechanicalMapping()
    : Inherit()
    , inputTopo(NULL)
    , outputTopo(NULL)
    , bezierForcefield(NULL)
    , normals(initData(&normals, "normals","Node normals at the rest shape"))
    , measureError(initData(&measureError, false, "measureError","Error with high resolution mesh"))
    , targetTopology(initLink("targetTopology","Targeted high resolution topology"))
    , verticesTarget(OutVecCoord()) // dummy initialization
    , trianglesTarget(SeqTriangles()) // dummy initialization
    , matrixJ()
    , updateJ(false)
    {
    }

    typedef struct {

        // Nodes of the Bézier triangle
        sofa::type::fixed_array<Vec3, 10> bezierNodes;
        sofa::type::fixed_array<Vec3, 10> bezierNodesV;

        // Segments in the reference frame
        Vec3 P0_P1;
        Vec3 P0_P2;
        Vec3 P1_P2;
        Vec3 P1_P0;
        Vec3 P2_P0;
        Vec3 P2_P1;

        // Contains the list of poinst connected to this triangle
        sofa::type::vector<int> attachedPoints;

    } TriangleInformation;

    gl::GLSLShader shader;

    BaseMeshTopology* inputTopo;
    BaseMeshTopology* outputTopo;

    // Pointer to the forcefield associated with the input topology
    BezierFF* bezierForcefield;

    Data< type::vector<Vec3> > normals;
    Data<bool> measureError;
    SingleLink<BezierTriangleMechanicalMapping<TIn, TOut>,
    sofa::core::topology::BaseMeshTopology,
    BaseLink::FLAG_STOREPATH|BaseLink::FLAG_STRONGLINK> targetTopology;

    topology::container::dynamic::TriangleSetTopologyContainer* topologyTarget;
    const OutVecCoord verticesTarget;
    const SeqTriangles trianglesTarget;

    type::vector<Vec3> colourMapping;
    type::vector<Vec3> coloursPerVertex;
    type::vector<Real> vectorErrorCoarse;
    type::vector<Real> vectorErrorTarget;

    type::vector<TriangleInformation> triangleInfo;

    std::unique_ptr<MatrixType> matrixJ;
    bool updateJ;

    // Pointer on the topological mapping to retrieve the list of edges
    // XXX: The edges are no longer there!!!
    //TriangleSubdivisionTopologicalMapping* triangleSubdivisionTopologicalMapping;

    void HSL2RGB(Vec3 &rgb, Real h, Real sl, Real l);
    void MeasureError();
    Real DistanceHausdorff(BaseMeshTopology *topo1, BaseMeshTopology *topo2, type::vector<Real> &vectorError);
    void ComputeNormals(type::vector<Vec3> &normals);
    void FindTriangleInNormalDirection(const InVecCoord& highResVertices, const SeqTriangles highRestriangles, const type::vector<Vec3> &normals);

    // Computes the barycentric coordinates of a vertex within a triangle
    void computeBaryCoefs(Vec3 &baryCoefs, const Vec3 &p, const Vec3 &a, const Vec3 &b, const Vec3 &c, bool bConstraint = true);

    // NOTE: The following funcitons return *square* of the distance!
    Real FindClosestPoint(unsigned int& closestVerticex, const Vec3& point, const OutVecCoord &inVertices);
    Real FindClosestEdge(unsigned int& closestEdge, const Vec3& point, const OutVecCoord &inVertices, const SeqEdges &inEdges);
    Real FindClosestTriangle(unsigned int& closestEdge, const Vec3& point, const OutVecCoord &inVertices, const SeqTriangles &inTriangles);

    // Contains the barycentric coordinates of the point within a triangle
    sofa::type::vector<Vec3> barycentricCoordinates;
};

} // namespace mapping

} // namespace component

} // namespace sofa

#endif
