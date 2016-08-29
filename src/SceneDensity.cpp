#include "main.h"
#include "node.h"
#include "noise.h"

#include <openvdb/openvdb.h>
#include <openvdb/tools/Interpolation.h>
#include <openvdb/tools/ParticlesToLevelSet.h>
#include <openvdb/tools/LevelSetUtil.h> //sdfToFogVolume
#include <openvdb/tools/GridTransformer.h> //resampleToMatch
#include <openvdb/tools/Composite.h>

#include "scene.h"
#include "SceneDensity.h"

namespace SceneData{

PostFog::PostFog(Node::NodeTree *_pnt, openvdb::FloatGrid::Ptr _pdgrid) : BaseObject(_pnt), pdgrid(_pdgrid){
	//
}

PostFog::~PostFog(){
	//
}

}

namespace Node{

BaseFogNode1::BaseFogNode1(uint _level, NodeTree *pnt) : BaseFogNode(_level,pnt){
    pdgrid = openvdb::FloatGrid::create();
    pdgrid->setGridClass(openvdb::GRID_FOG_VOLUME);
}

BaseFogNode1::~BaseFogNode1(){
    //
}

BaseFogNode * BaseFogNode::Create(uint level, NodeTree *pnt){
    return new BaseFogNode1(level,pnt);
}

BaseVectorFieldNode1::BaseVectorFieldNode1(uint _level, NodeTree *pnt) : BaseVectorFieldNode(_level,pnt){
	pvgrid = openvdb::Vec3SGrid::create();
	pvgrid->setGridClass(openvdb::GRID_FOG_VOLUME); //this probably doesn't matter here
}

BaseVectorFieldNode1::~BaseVectorFieldNode1(){
	//
}

BaseVectorFieldNode * BaseVectorFieldNode::Create(uint level, NodeTree *pnt){
	return new BaseVectorFieldNode1(level,pnt);
}

class ParticleInputList{
public:
	typedef openvdb::Vec3R PosType;
	ParticleInputList(SceneData::ParticleSystem *_pps, float _rscale) : pps(_pps), rscale(_rscale){}//, rscale(1.0f), vscale(1.0f){}
	//ParticleInputList(openvdb::Real rscale1 = 1.0f, openvdb::Real vscale1 = 1.0f) : rscale(rscale1), vscale(vscale1){}

	size_t size() const{
		return pps->vl.size();
	}

	void getPos(size_t x, PosType &p) const{
		dfloat3 &t = pps->vl[x];
		p = openvdb::Vec3R(t.x,t.y,t.z);
	}

	void getPosRad(size_t x, PosType &p, openvdb::Real &r) const{
		dfloat3 &t = pps->vl[x];
		p = openvdb::Vec3R(t.x,t.y,t.z);
		r = rscale;
	}

protected:
	SceneData::ParticleSystem *pps;
	float rscale;
};

ParticleInput::ParticleInput(uint _level, NodeTree *pnt) : BaseFogNode(_level,pnt), BaseFogNode1(_level,pnt), IParticleInput(_level,pnt){
    //
    //DebugPrintf(">> ParticleInput()\n");
}

ParticleInput::~ParticleInput(){
    //
}

void ParticleInput::Evaluate(const void *pp){
	InputNodeParams *pd = (InputNodeParams*)pp;
	SceneData::ParticleSystem *pps = dynamic_cast<SceneData::ParticleSystem*>(std::get<INP_OBJECT>(*pd));
	if(!pps)
		return;

	BaseValueNode<float> *psizen = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_SIZE]); //should lifetime determine the size?
	BaseValueNode<float> *pcoffn = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_CUTOFF]);

	dfloat3 zr(0.0f);
	const FloatGridBoxSampler *psamplers[] = {std::get<INP_SDFSAMPLER>(*pd),std::get<INP_FOGSAMPLER>(*pd)};
	ValueNodeParams np(&zr,&zr,0.0f,0.0f,psamplers);
	pntree->EvaluateNodes0(&np,level+1,emask);

	float size = psizen->locr(indices[INPUT_SIZE]);
	float coff = pcoffn->locr(indices[INPUT_CUTOFF]);

	openvdb::math::Transform::Ptr pgridtr = std::get<INP_TRANSFORM>(*pd);
	//pdgrid->setTransform(pgridtr);

	pdgrid = openvdb::FloatGrid::create(coff);
	pdgrid->setTransform(pgridtr);
	pdgrid->setGridClass(openvdb::GRID_LEVEL_SET);
	//pdgrid = openvdb::createLevelSet<openvdb::FloatGrid>(pgridtr->voxelSize().x(),coff/pgridtr->voxelSize().x());

	ParticleInputList pl(pps,size);

	DebugPrintf("> Rasterizing particles...\n");
	openvdb::tools::ParticlesToLevelSet<openvdb::FloatGrid> lsf(*pdgrid);
	lsf.setGrainSize(1);
	lsf.rasterizeSpheres(pl);
	lsf.finalize();

	DebugPrintf("> Converting fog volume...\n");
	openvdb::tools::sdfToFogVolume(*pdgrid);

	pdgrid->tree().voxelizeActiveTiles(true); //voxelize the interior so that futher processing works

#if 0
	BaseValueNode<float> *prasres = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_RASTERIZATIONRES]);
	BaseValueNode<float> *pweight = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_WEIGHT]);

	dfloat3 zr(0.0f);
	ValueNodeParams np(&zr,&zr,0.0f,0.0f);
	pntree->EvaluateNodes0(&np,level+1,emask);

	openvdb::math::Transform::Ptr pgridtr = std::get<INP_TRANSFORM>(*pd);
	pdgrid->setTransform(pgridtr);

	openvdb::FloatGrid::Ptr ptgrid;
	if(pgridtr->voxelSize().x() < prasres->locr(indices[INPUT_RASTERIZATIONRES])){
		openvdb::math::Transform::Ptr pgridtr1 = openvdb::math::Transform::createLinearTransform(prasres->locr(indices[INPUT_RASTERIZATIONRES]));
		ptgrid = openvdb::FloatGrid::create();
        ptgrid->setGridClass(openvdb::GRID_FOG_VOLUME);
		ptgrid->setTransform(pgridtr1);
	}else ptgrid = pdgrid;

	DebugPrintf("> Rasterizing particles...\n");

	openvdb::FloatGrid::Accessor grida = ptgrid->getAccessor();
	for(uint i = 0; i < pps->vl.size(); ++i){
		//pntree->EvaluateNodes0(0,level+1,emask);
		openvdb::Vec3s posw(pps->vl[i].x,pps->vl[i].y,pps->vl[i].z);
		openvdb::Vec3s c = ptgrid->transform().worldToIndex(posw); //assume cell-centered indices
		openvdb::Vec3f f = openvdb::Vec3f(floorf(c.x()-0.5f),floorf(c.y()-0.5f),floorf(c.z()-0.5f));
		openvdb::Vec3f b = c-f;
		openvdb::Vec3f B = openvdb::Vec3f(1.0f)-b;

		ValueNodeParams np1((dfloat3*)posw.asPointer(),&zr,0.0f,0.0f);
		pntree->EvaluateNodes0(&np1,level+1,emask);

		openvdb::Coord q((int)f.x(),(int)f.y(),(int)f.z());
		grida.modifyValue(q.offsetBy(0,0,0),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*B.x()*B.y()*B.z();});
		grida.modifyValue(q.offsetBy(1,0,0),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*b.x()*B.y()*B.z();});
		grida.modifyValue(q.offsetBy(0,1,0),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*B.x()*b.y()*B.z();});
		grida.modifyValue(q.offsetBy(1,1,0),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*b.x()*b.y()*B.z();});
		grida.modifyValue(q.offsetBy(0,0,1),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*B.x()*B.y()*b.z();});
		grida.modifyValue(q.offsetBy(1,0,1),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*b.x()*B.y()*b.z();});
		grida.modifyValue(q.offsetBy(0,1,1),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*B.x()*b.y()*b.z();});
		grida.modifyValue(q.offsetBy(1,1,1),[&](float &v){v += pweight->locr(indices[INPUT_WEIGHT])*b.x()*b.y()*b.z();});
	}

	if(pgridtr->voxelSize().x() < prasres->locr(indices[IParticleInput::INPUT_RASTERIZATIONRES])){
		DebugPrintf("> Upsampling particle fog...\n");
		openvdb::tools::resampleToMatch<openvdb::tools::BoxSampler>(*ptgrid,*pdgrid);
	}else DebugPrintf("Used native grid resolution for particle rasterization.\n");

	//advection: trace voxel if density < threshold

	pdgrid->tree().prune();
#endif
}

IParticleInput * IParticleInput::Create(uint level, NodeTree *pnt){
	return new ParticleInput(level,pnt);
}

SmokeCache::SmokeCache(uint _level, NodeTree *pnt) : BaseFogNode(_level,pnt), BaseFogNode1(_level,pnt), ISmokeCache(_level,pnt){
    //
    //DebugPrintf(">> ParticleInput()\n");
}

SmokeCache::~SmokeCache(){
    //
}

void SmokeCache::Evaluate(const void *pp){
	InputNodeParams *pd = (InputNodeParams*)pp;
	SceneData::SmokeCache *psmc = dynamic_cast<SceneData::SmokeCache*>(std::get<INP_OBJECT>(*pd));
	if(!psmc)
		return;

	//openvdb::io::File vdbc = openvdb::io::File("̛/tmp/fog_000142_00.vdb");
	//openvdb::io::File vdbc("/home/jasper/Asiakirjat/3dcgi/clouds/blendcache_droplet_fluid_sim01/fog_000142_00.vdb");
	openvdb::io::File vdbc("/home/jasper/Asiakirjat/3dcgi/clouds/blendcache_droplet_random1/fog_000070_00.vdb");
	try{
		vdbc.open(false);
		openvdb::FloatGrid::Ptr ptgrid = openvdb::gridPtrCast<openvdb::FloatGrid>(vdbc.readGrid("density"));
		vdbc.close();

		DebugPrintf("Read OpenVDB smoke cache: %s\n",ptgrid->getName().c_str());

		DebugPrintf("> Upsampling smoke cache...\n");
		openvdb::math::Transform::Ptr pgridtr = std::get<INP_TRANSFORM>(*pd);
		pdgrid->setTransform(pgridtr);

		openvdb::tools::resampleToMatch<openvdb::tools::BoxSampler>(*ptgrid,*pdgrid);
		pdgrid->tree().prune();

	}catch(const openvdb::IoError &e){
		DebugPrintf("OpenVDB: %s\n",e.what());
	}
}

ISmokeCache * ISmokeCache::Create(uint level, NodeTree *pnt){
	return new SmokeCache(level,pnt);
}

FogPostInput::FogPostInput(uint _level, NodeTree *pnt) : BaseFogNode(_level,pnt), BaseFogNode1(_level,pnt), IFogPostInput(_level,pnt){
    //
}

FogPostInput::~FogPostInput(){
    //
}

void FogPostInput::Evaluate(const void *pp){
	InputNodeParams *pd = (InputNodeParams*)pp;
	SceneData::PostFog *ppf = dynamic_cast<SceneData::PostFog*>(std::get<INP_OBJECT>(*pd));
	if(!ppf)
		return;
	pdgrid = ppf->pdgrid;
}

IFogPostInput * IFogPostInput::Create(uint level, NodeTree *pnt){
	return new FogPostInput(level,pnt);
}

Composite::Composite(uint _level, NodeTree *pnt) : BaseFogNode(_level,pnt), BaseFogNode1(_level,pnt), IComposite(_level,pnt){
	//
}

Composite::~Composite(){
	//
}

void Composite::Evaluate(const void *pp){
	InputNodeParams *pd = (InputNodeParams*)pp;

	BaseValueNode<float> *pvalue = dynamic_cast<BaseValueNode<float>*>(pnodes[IComposite::INPUT_VALUE]);
	BaseFogNode1 *pnode = dynamic_cast<BaseFogNode1*>(pnodes[IComposite::INPUT_FOG]);

	dfloat3 zr(0.0f);
	const FloatGridBoxSampler *psamplers[] = {std::get<INP_SDFSAMPLER>(*pd),std::get<INP_FOGSAMPLER>(*pd)};
	//ValueNodeParams np(&zr,&zr,0.0f,0.0f,psamplers);
	//pntree->EvaluateNodes0(&np,level+1,emask);

	openvdb::math::Transform::Ptr pgridtr = std::get<INP_TRANSFORM>(*pd);
	pdgrid->setTransform(pgridtr);

	DebugPrintf("> Compositing fog volume...\n");

	typedef std::tuple<openvdb::FloatGrid::Ptr, openvdb::FloatGrid::Accessor> FloatGridT;
    tbb::enumerable_thread_specific<FloatGridT> tgrida([&]()->FloatGridT{
        openvdb::FloatGrid::Ptr ptgrid = openvdb::FloatGrid::create();
        ptgrid->setTransform(pgridtr);
        ptgrid->setGridClass(openvdb::GRID_FOG_VOLUME);
        return FloatGridT(ptgrid,ptgrid->getAccessor());
    });
    tbb::parallel_for(openvdb::tree::IteratorRange<openvdb::FloatGrid::ValueOnIter>(pnode->pdgrid->beginValueOn()),[&](openvdb::tree::IteratorRange<openvdb::FloatGrid::ValueOnIter> &r){
        FloatGridT &fgt = tgrida.local();
        for(; r; ++r){
            const openvdb::FloatGrid::ValueOnIter &m = r.iterator();

			openvdb::Coord c = m.getCoord();
			openvdb::math::Vec3s posw = pnode->pdgrid->transform().indexToWorld(c);

			ValueNodeParams np1((dfloat3*)posw.asPointer(),&zr,0.0f,m.getValue(),psamplers);
			pntree->EvaluateNodes0(&np1,level+1,emask);

			float f = pvalue->locr(indices[IComposite::INPUT_VALUE]);
			std::get<1>(fgt).setValue(c,f);
        }
    });

	//TODO: try compSum
	openvdb::FloatGrid::Accessor dgrida = pdgrid->getAccessor();
    for(tbb::enumerable_thread_specific<FloatGridT>::const_iterator q = tgrida.begin(); q != tgrida.end(); ++q){
        //
        for(openvdb::FloatGrid::ValueOnIter m = std::get<0>(*q)->beginValueOn(); m.test(); ++m){
			openvdb::Coord c = m.getCoord();
            float f = m.getValue();
            dgrida.setValue(c,f);
        }
    }
}

IComposite * IComposite::Create(uint level, NodeTree *pnt){
	return new Composite(level,pnt);
}

Advection::Advection(uint _level, NodeTree *pnt, uint _flags) : BaseFogNode(_level,pnt), BaseFogNode1(_level,pnt), IAdvection(_level,pnt), flags(_flags){
	//
}

Advection::~Advection(){
	//
}

void Advection::Evaluate(const void *pp){
	InputNodeParams *pd = (InputNodeParams*)pp;

	BaseValueNode<float> *pthrs = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_THRESHOLD]);
	BaseValueNode<float> *pdist = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_DISTANCE]);
	BaseValueNode<int> *piters = dynamic_cast<BaseValueNode<int>*>(pnodes[INPUT_ITERATIONS]);
	BaseValueNode<float> *pdn = dynamic_cast<BaseValueNode<float>*>(pnodes[INPUT_DENSITY]);
	BaseValueNode<dfloat3> *pvn = dynamic_cast<BaseValueNode<dfloat3>*>(pnodes[INPUT_VELOCITY]);
	BaseFogNode1 *pnode = dynamic_cast<BaseFogNode1*>(pnodes[INPUT_FOG]);

	dfloat3 zr(0.0f);
	const FloatGridBoxSampler *psamplers[] = {std::get<INP_SDFSAMPLER>(*pd),std::get<INP_FOGSAMPLER>(*pd)};
	//ValueNodeParams np(&zr,&zr,0.0f,0.0f,psamplers);
	//pntree->EvaluateNodes0(&np,level+1,emask);

	openvdb::math::Transform::Ptr pgridtr = std::get<INP_TRANSFORM>(*pd);
	pdgrid->setTransform(pgridtr);

	DebugPrintf("> Advecting fog volume...\n");

	typedef std::tuple<openvdb::FloatGrid::Ptr, openvdb::FloatGrid::Accessor> FloatGridT;
    tbb::enumerable_thread_specific<FloatGridT> tgrida([&]()->FloatGridT{
        openvdb::FloatGrid::Ptr ptgrid = openvdb::FloatGrid::create();
        ptgrid->setTransform(pgridtr);
        ptgrid->setGridClass(openvdb::GRID_FOG_VOLUME);
        return FloatGridT(ptgrid,ptgrid->getAccessor());
    });
    tbb::parallel_for(openvdb::tree::IteratorRange<openvdb::FloatGrid::ValueOnIter>(pnode->pdgrid->beginValueOn()),[&](openvdb::tree::IteratorRange<openvdb::FloatGrid::ValueOnIter> &r){
        FloatGridT &fgt = tgrida.local();
        for(; r; ++r){
            const openvdb::FloatGrid::ValueOnIter &m = r.iterator();

			openvdb::Coord c = m.getCoord();
			openvdb::math::Vec3s posw = pnode->pdgrid->transform().indexToWorld(c);

			float f = m.getValue();
			float4 rc = float4::load(posw.asPointer());

			ValueNodeParams np1((dfloat3*)posw.asPointer(),&zr,0.0f,f,psamplers);
			pntree->EvaluateNodes0(&np1,level+1,emask);

			float th = pthrs->locr(indices[INPUT_THRESHOLD]);
			if(f > th)
				continue;

			float s = pdist->locr(indices[INPUT_DISTANCE])/(float)piters->locr(indices[INPUT_ITERATIONS]); //step size
			float p; //density

			uint ic = piters->locr(indices[INPUT_ITERATIONS]);
			for(uint i = 0; i < ic; ++i){
				p = pdn->locr(indices[INPUT_DENSITY]);
				//TODO: do actual integration instead of sampling the last value?
				if(p > th && flags & 1<<BOOL_BREAK_ITERATION)
					break;
				dfloat3 v = pvn->locr(indices[INPUT_VELOCITY]);
				rc += s*float4::load(&v);
				//
				//all the other inputs except the vector is evaluated only per-voxel
				//TODO: advection data to info node (current iteration, distance etc)?
				//TODO: local data sampling (by the sample_local option)
				float4::store((dfloat3*)posw.asPointer(),rc);
				new(&np1) ValueNodeParams((dfloat3*)posw.asPointer(),&zr,0.0f,f,psamplers);
				pntree->EvaluateNodes0(&np1,level+1,emask);
			}

			//if(p > th) //??
			std::get<1>(fgt).setValue(c,p);
        }
    });

	//TODO: try compSum
	openvdb::FloatGrid::Accessor dgrida = pdgrid->getAccessor();
    for(tbb::enumerable_thread_specific<FloatGridT>::const_iterator q = tgrida.begin(); q != tgrida.end(); ++q){
        //
        for(openvdb::FloatGrid::ValueOnIter m = std::get<0>(*q)->beginValueOn(); m.test(); ++m){
			openvdb::Coord c = m.getCoord();
            float f = m.getValue();
            dgrida.setValue(c,f);
        }
    }
}

IAdvection * IAdvection::Create(uint level, NodeTree *pnt, uint flags){
	return new Advection(level,pnt,flags);
}

VectorFieldSampler::VectorFieldSampler(uint level, NodeTree *pnt) : BaseValueNode<dfloat3>(level,pnt), BaseNode(level,pnt), IVectorFieldSampler(level,pnt){
	//
}

VectorFieldSampler::~VectorFieldSampler(){
	//
}

void VectorFieldSampler::Evaluate(const void *pp){
	BaseVectorFieldNode1 *pfieldn = dynamic_cast<BaseVectorFieldNode1*>(pnodes[INPUT_FIELD]);
	BaseValueNode<dfloat3> *pnode = dynamic_cast<BaseValueNode<dfloat3>*>(pnodes[INPUT_POSITION]);

	openvdb::tools::GridSampler<openvdb::Vec3SGrid, openvdb::tools::BoxSampler> sampler1(*pfieldn->pvgrid);
	dfloat3 dposw = pnode->locr(indices[INPUT_POSITION]);

	openvdb::Vec3s v = sampler1.wsSample(*(openvdb::Vec3s*)&dposw);
	this->BaseValueNode<dfloat3>::result.local().value[0] = *(dfloat3*)&v;
}

IVectorFieldSampler * IVectorFieldSampler::Create(uint level, NodeTree *pnt){
	return new VectorFieldSampler(level,pnt);
}

}
