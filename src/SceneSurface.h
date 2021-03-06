#ifndef SCENE_SURFACE_H
#define SCENE_SURFACE_H

namespace Node{

//additional layer of abstraction - the openvdb compile times are ridiculous
class BaseSurfaceNode1 : public virtual BaseSurfaceNode{
public:
	BaseSurfaceNode1(uint, NodeTree *);
	~BaseSurfaceNode1();
	virtual void Clear();
	openvdb::FloatGrid::Ptr ComputeLevelSet(openvdb::math::Transform::Ptr, float, float) const;
	openvdb::FloatGrid::Ptr pbgrid; //billowing grid
	std::vector<openvdb::Vec3s> vl;
	std::vector<openvdb::Vec3I> tl;
	std::vector<openvdb::Vec4I> ql;
};

class SurfaceInput : public BaseSurfaceNode1, public ISurfaceInput{
public:
	SurfaceInput(uint, NodeTree *);
	~SurfaceInput();
	void Evaluate(const void *);
};

class SolidInput : public BaseSurfaceNode1, public ISolidInput{
public:
	SolidInput(uint, NodeTree *, char);
	~SolidInput();
	void Evaluate(const void *);
	char geomch;
};

class Displacement : public BaseSurfaceNode1, public IDisplacement{
public:
	Displacement(uint _level, NodeTree *pnt, float);
	~Displacement();
	void Evaluate(const void *);
	float resf;
};

class Transform : public BaseSurfaceNode1, public ITransform{
public:
	Transform(uint _level, NodeTree *pnt);
	~Transform();
	void Evaluate(const void *);
};

class CSG : public BaseSurfaceNode1, public ICSG{
public:
	CSG(uint _level, NodeTree *pnt, float);
	~CSG();
	void Evaluate(const void *);
	char opch;
};

}

#endif
