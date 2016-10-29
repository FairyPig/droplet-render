#ifndef SCENE_OCCLUSION_H
#define SCENE_OCCLUSION_H

class SceneOcclusion{
public:
	SceneOcclusion();
	~SceneOcclusion();
	void Initialize();
	void Intersect(const sfloat4 &, const sfloat4 &, const sfloat1 &, dintN *, dfloatN *) const;
	void Destroy();
#ifdef USE_EMBREE
	struct __RTCDevice *pdev;
	struct __RTCScene *pscene;
#endif
};

#endif