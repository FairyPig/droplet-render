#include "main.h"
#include "node.h"
#include "scene.h"
#include "noise.h"

#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h> //sdf rebuilding
#include <openvdb/tools/Interpolation.h>

#include "SceneSurface.h"

namespace Node{

template<class T>
using InputNodeParams = std::tuple<T *, openvdb::math::Transform::Ptr>;
enum INP{
	INP_OBJECT,
	INP_TRANSFORM
};

BaseSurfaceNode1::BaseSurfaceNode1(uint _level, NodeTree *pnt) : BaseSurfaceNode(_level,pnt){
    //
    pdgrid = openvdb::FloatGrid::create();
    pdgrid->setGridClass(openvdb::GRID_FOG_VOLUME);
    //transform shouldn't matter here, only index coordinates are used
}

BaseSurfaceNode1::~BaseSurfaceNode1(){
    //
}

openvdb::FloatGrid::Ptr BaseSurfaceNode1::ComputeLevelSet(openvdb::math::Transform::Ptr pgridtr, float lff) const{
	openvdb::FloatGrid::Ptr ptgrid = openvdb::tools::meshToSignedDistanceField<openvdb::FloatGrid>(*pgridtr,vl,tl,ql,lff,lff);
	return ptgrid;
}

BaseSurfaceNode * BaseSurfaceNode::Create(uint level, NodeTree *pnt){
    return new BaseSurfaceNode1(level,pnt);
}

SurfaceInput::SurfaceInput(uint _level, NodeTree *pnt) : BaseSurfaceNode(_level,pnt), BaseSurfaceNode1(_level,pnt), ISurfaceInput(_level,pnt){
    //
    //DebugPrintf(">> SurfaceInput()\n");
}

SurfaceInput::~SurfaceInput(){
    //
}

void SurfaceInput::Evaluate(const void *pp){
    //DebugPrintf("---Surface::Evaluate()\n");
    InputNodeParams<SceneObject> *pd = (InputNodeParams<SceneObject>*)pp;
    SceneObject *pobj = std::get<INP_OBJECT>(*pd);

    //clear possible data from previous evaluation
    vl.clear();
    tl.clear();
    ql.clear();

    vl.reserve(pobj->vl.size());
    for(uint i = 0; i < pobj->vl.size(); ++i){
        openvdb::Vec3s t(pobj->vl[i].x,pobj->vl[i].y,pobj->vl[i].z);
        vl.push_back(t);
    }
    tl.reserve(pobj->tl.size());
    for(uint i = 0; i < pobj->tl.size(); i += 3){
        openvdb::Vec3I t(pobj->tl[i+0],pobj->tl[i+1],pobj->tl[i+2]);
        tl.push_back(t);
    }
}

Node::ISurfaceInput * ISurfaceInput::Create(uint level, NodeTree *pnt){
    return new SurfaceInput(level,pnt);
}

Displacement::Displacement(uint _level, NodeTree *pnt) : BaseSurfaceNode(_level,pnt), BaseSurfaceNode1(_level,pnt), IDisplacement(_level,pnt){
    //
    //DebugPrintf(">> Displacement()\n");
}

Displacement::~Displacement(){
    //
}

void Displacement::Evaluate(const void *pp){
    //
    //DebugPrintf("---Displacement::Evaluate()\n");
}

Node::IDisplacement * IDisplacement::Create(uint level, NodeTree *pnt){
    return new Displacement(level,pnt);
}

fBmPerlinNoise::fBmPerlinNoise(uint _level, NodeTree *pnt) : BaseSurfaceNode(_level,pnt), BaseSurfaceNode1(_level,pnt), IfBmPerlinNoise(_level,pnt){
    //
    //DebugPrintf(">> fBmPerlinNoise()\n");
}

fBmPerlinNoise::~fBmPerlinNoise(){
    //
}

void fBmPerlinNoise::Evaluate(const void *pp){
    InputNodeParams<SceneObject> *pd = (InputNodeParams<SceneObject>*)pp;
    //DebugPrintf("---PerlinNoise::Evaluate()\n");

    const float lff = 4.0f;

    BaseValueNode<int> *poctn = dynamic_cast<BaseValueNode<int>*>(pnodes[IfBmPerlinNoise::INPUT_OCTAVES]);
    BaseValueNode<float> *pfreqn = dynamic_cast<BaseValueNode<float>*>(pnodes[IfBmPerlinNoise::INPUT_FREQ]);
    BaseValueNode<float> *pampn = dynamic_cast<BaseValueNode<float>*>(pnodes[IfBmPerlinNoise::INPUT_AMP]);
    BaseValueNode<float> *pfjumpn = dynamic_cast<BaseValueNode<float>*>(pnodes[IfBmPerlinNoise::INPUT_FJUMP]);
    BaseValueNode<float> *pgainn = dynamic_cast<BaseValueNode<float>*>(pnodes[IfBmPerlinNoise::INPUT_GAIN]);
    BaseValueNode<float> *pbilln = dynamic_cast<BaseValueNode<float>*>(pnodes[IfBmPerlinNoise::INPUT_BILLOW]);
    BaseSurfaceNode1 *pnode = dynamic_cast<BaseSurfaceNode1*>(pnodes[IfBmPerlinNoise::INPUT_SURFACE]);
    //BaseSurfaceNode1 *pgridn = dynamic_cast<BaseSurfaceNode1*>(pnodes[IfBmPerlinNoise::INPUT_GRID]); //could be just using input_surface

    pntree->EvaluateNodes0(0,level+1,emask);
    float amp = fBm::GetAmplitudeMax(poctn->result.local(),pampn->result.local(),pgainn->result.local());

    openvdb::math::Transform::Ptr pgridtr = std::get<INP_TRANSFORM>(*pd);//openvdb::math::Transform::createLinearTransform(s);
    openvdb::FloatGrid::Ptr psgrid = openvdb::tools::meshToSignedDistanceField<openvdb::FloatGrid>(*pgridtr,pnode->vl,pnode->tl,pnode->ql,ceilf(amp/pgridtr->voxelSize().x()+lff),lff);

    DebugPrintf("Allocated disp. exterior narrow band for amp = %f+%f (%u voxels)\n",amp,pgridtr->voxelSize().x()*lff,(uint)ceilf(amp/pgridtr->voxelSize().x()+lff));
    DebugPrintf("> Displacing SDF...\n");

    /*openvdb::FloatGrid::Ptr pdgrid = openvdb::FloatGrid::create();
    pdgrid->setTransform(pgridtr);
    pdgrid->setGridClass(openvdb::GRID_FOG_VOLUME);*/

    /*openvdb::FloatGrid::Accessor grida = psgrid->getAccessor();
    openvdb::FloatGrid::Accessor gridd = pdgrid->getAccessor();
    openvdb::FloatGrid::ConstAccessor gridac = psgrid->getConstAccessor();
    openvdb::FloatGrid::ConstAccessor griddc = pdgrid->getConstAccessor();

    openvdb::math::CPT_RANGE<openvdb::math::UniformScaleMap,openvdb::math::CD_2ND> cptr;
    for(openvdb::FloatGrid::ValueOnIter m = psgrid->beginValueOn(); m.test(); ++m){
        //BaseValueNode<float>::EvaluateAll(0,level+1);
        EvaluateValueGroup(level+1);

        openvdb::Coord c = m.getCoord();
        openvdb::math::Vec3s posw = cptr.result(*pgridtr->map<openvdb::math::UniformScaleMap>(),gridac,c);
        sfloat4 sposw = sfloat1(posw.x(),posw.y(),posw.z(),0.0f); //TODO: utilize vectorization

        float f = fabsf(fBm::noise(sposw,poctn->result,pfreqn->result,pampn->result,pfjumpn->result,pgainn->result).get<0>());
        //if(i > 0)
            //f *= powf(std::min(gridd.getValue(c)/(*pdls)[i].qscale,1.0f),(*pdls)[i].billow);
        gridd.setValue(c,f);
    }

    for(openvdb::FloatGrid::ValueOnIter m = psgrid->beginValueOn(); m.test(); ++m){
        openvdb::Coord c = m.getCoord();
        float d = m.getValue();
        float f = griddc.getValue(c);

        //grida.setValue(c,d-f);
        m.setValue(d-f);
    }*/

    typedef std::tuple<openvdb::FloatGrid::Ptr, openvdb::FloatGrid::Accessor, openvdb::FloatGrid::ConstAccessor> FloatGridT;
    tbb::enumerable_thread_specific<FloatGridT> tgrida([&]()->FloatGridT{
        openvdb::FloatGrid::Ptr ptgrid = openvdb::FloatGrid::create();
        ptgrid->setTransform(pgridtr);
        ptgrid->setGridClass(openvdb::GRID_FOG_VOLUME);
        return FloatGridT(ptgrid,ptgrid->getAccessor(),psgrid->getConstAccessor());
    });
    openvdb::math::CPT_RANGE<openvdb::math::UniformScaleMap,openvdb::math::CD_2ND> cptr;
    tbb::parallel_for(openvdb::tree::IteratorRange<openvdb::FloatGrid::ValueOnIter>(psgrid->beginValueOn()),[&](openvdb::tree::IteratorRange<openvdb::FloatGrid::ValueOnIter> &r){
        FloatGridT &fgt = tgrida.local();
        for(; r; ++r){ //++fi
            pntree->EvaluateNodes0(0,level+1,emask);

            const openvdb::FloatGrid::ValueOnIter &m = r.iterator();

            openvdb::Coord c = m.getCoord(); //, m.getValue()
            openvdb::math::Vec3s posw = cptr.result(*pgridtr->map<openvdb::math::UniformScaleMap>(),std::get<2>(fgt),c);
            sfloat4 sposw = sfloat1(posw.x(),posw.y(),posw.z(),0.0f); //TODO: utilize vectorization

            float f = fabsf(fBm::noise(sposw,poctn->result.local(),pfreqn->result.local(),pampn->result.local(),pfjumpn->result.local(),pgainn->result.local()).get<0>());
			/*if(fi == 0)
				fn = dfloatN(fBm::noise(sposw+offset,poctn->result,pfreqn->result,pampn->result,pfjumpn->result,pgainn->result)); //dfloatN fn
			float f = fn.v[fi];*/
            std::get<1>(fgt).setValue(c,f); //set only the displacement, so that billowing can be done
        }
    });

    //This could probably be faster with parallel reduction
    //for(tbb::enumerable_thread_specific<FloatGridT>::const_iterator m = tgrida.begin(); m != tgrida.end(); ++m)
    //openvdb::tools::compSum(*psgrid,*std::get<0>(*m));
    openvdb::FloatGrid::Accessor sgrida = psgrid->getAccessor();
    openvdb::FloatGrid::Accessor dgrida = pdgrid->getAccessor();
    openvdb::FloatGrid::ConstAccessor dgrida0 = pnode->pdgrid->getConstAccessor();
    for(tbb::enumerable_thread_specific<FloatGridT>::const_iterator q = tgrida.begin(); q != tgrida.end(); ++q){
        //
        for(openvdb::FloatGrid::ValueOnIter m = std::get<0>(*q)->beginValueOn(); m.test(); ++m){
            openvdb::Coord c = m.getCoord();

            float d = sgrida.getValue(c);
            float f = m.getValue();

            dgrida.setValue(c,f/amp);
            f *= powf(std::min(dgrida0.getValue(c),1.0f),pbilln->result.local()); //this is here because of the normalization

            sgrida.setValue(c,d-f);

            //if(omask & (1<<OUTPUT_GRID))
                //dgrida.setValue(c,d/amp);
        }
    }

    DebugPrintf("> Rebuilding...\n");

    openvdb::tools::volumeToMesh<openvdb::FloatGrid>(*psgrid,vl,tl,ql,0.0);
}

IfBmPerlinNoise * IfBmPerlinNoise::Create(uint level, NodeTree *pnt){
    return new fBmPerlinNoise(level,pnt);
}

}