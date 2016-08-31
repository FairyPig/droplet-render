#include "main.h"
#include "node.h"
#include "scene.h"
#include "kernel.h"
#include <python3.5m/Python.h>

#include <unordered_map>
#include <functional> //std::function

static RenderKernel *gpkernel = 0;
static Scene *gpscene = 0;

#ifndef __unix__
#define strcasecmp stricmp
#endif

static inline float PyGetFloat(PyObject *pb, const char *pn){
    PyObject *pa = PyObject_GetAttrString(pb,pn);
    float r = (float)PyFloat_AsDouble(pa);
    Py_DECREF(pa);
    return r;
}

static PyObject * DRE_BeginRender(PyObject *pself, PyObject *pargs){
    PyObject *pscene, *pdata;
	uint rx, ry, w, h;
    if(gpscene || !PyArg_ParseTuple(pargs,"OOIIII",&pscene,&pdata,&rx,&ry,&w,&h)){
		DebugPrintf("Invalid arguments\n");
		return 0;
	}
	PyObject *pcam = PyObject_GetAttrString(pscene,"camera");
	PyObject *ploc = PyObject_GetAttrString(pcam,"location");
	PyObject *prot = PyObject_GetAttrString(pcam,"rotation_euler");
	//PyObject *prot = PyObject_GetAttrString(pcam,"rotation_quaternion");
	PyObject *pcdt = PyObject_GetAttrString(pcam,"data");

    float4 u = float4(0,0,-1,0);

    float fov = (float)h/(float)w*PyGetFloat(pcdt,"angle"); //blender seems to be using y-fov
    float zmin = PyGetFloat(pcdt,"clip_start");
    float zmax = PyGetFloat(pcdt,"clip_end");

    float4 p = float4(
        PyGetFloat(ploc,"x"),
        PyGetFloat(ploc,"y"),
        PyGetFloat(ploc,"z"),1.0f);

	PyObject *pquat = PyObject_CallMethod(prot,"to_quaternion","");
    float4 q = float4(
        PyGetFloat(pquat,"x"),
        PyGetFloat(pquat,"y"),
        PyGetFloat(pquat,"z"),
        PyGetFloat(pquat,"w"));
    float4 d = XMVector3Rotate(u.v,q.v);

	Py_DECREF(pcam);
	Py_DECREF(ploc);
	Py_DECREF(prot);
	Py_DECREF(pquat);
	Py_DECREF(pcdt);

    /*dfloat3 sp;
    float4::store(&sp,p);
    dfloat4 sq;
    float4::store(&sq,q);
    dfloat3 sd;
    float4::store(&sd,d);
    DebugPrintf("p: %f, %f, %f\nq: %f, %f, %f, %f\nd: %f, %f, %f\nfov: %f",sp.x,sp.y,sp.z,sq.x,sq.y,sq.z,sq.w,sd.x,sd.y,sd.z,fov);*/

    XMMATRIX view = XMMatrixLookToRH(p.v,d.v,u.v);
    XMMATRIX proj = XMMatrixPerspectiveFovRH(fov,(float)w/(float)h,zmin,zmax);

    dmatrix44 sviewi, sproji;
    matrix44::store(&sviewi,matrix44(XMMatrixInverse(0,view).r));
    matrix44::store(&sproji,matrix44(XMMatrixInverse(0,proj).r));

    std::unordered_map<Py_hash_t, Node::NodeTree *> ntm;

    PyObject *pngn = PyObject_GetAttrString(pdata,"node_groups");
    PyObject *pntl = PyObject_CallMethod(pngn,"values",""); //TODO: iterator
    uint ntc = PyList_Size(pntl);
    for(uint i = 0; i < ntc; ++i){
        PyObject *pnt1 = PyList_GetItem(pntl,i);
        PyObject *pns1 = PyObject_GetAttrString(pnt1,"nodes");

        std::unordered_map<Py_hash_t, Node::BaseNode *> nodem; //keep list of local duplicates
        std::function<Node::BaseNode * (PyObject *, uint, Node::NodeTree *)> ntree = [&](PyObject *proot, uint l, Node::NodeTree *pnt)->Node::BaseNode *{
            //
            PyObject *pidn = PyObject_GetAttrString(proot,"bl_idname"); //TODO: give enum id
            const char *pn = PyUnicode_AsUTF8(pidn);
            DebugPrintf("%s\n",pn);

            Node::BaseNode *pbn = Node::CreateNodeByType(pn,proot,l,pnt);//Node::CreateNode(pn,l);
            if(!pbn){
                DebugPrintf("Error: unknown node %s\n",pn);
                return 0;
            }

            Py_DECREF(pidn);

            PyObject *pnouts = PyObject_GetAttrString(proot,"outputs");
            PyObject *pnoutv = PyObject_GetIter(pnouts);
            //
            uint nx = 0;
            for(PyObject *pnout1 = PyIter_Next(pnoutv); pnout1; Py_DecRef(pnout1), pnout1 = PyIter_Next(pnoutv), ++nx){
                //
                PyObject *plnks = PyObject_GetAttrString(pnout1,"links");
                if(PyTuple_Size(plnks) > 0)
                    pbn->omask |= 1<<nx;
                Py_DECREF(plnks);
            }

            Py_DECREF(pnoutv);
            Py_DECREF(pnouts);

            Py_hash_t h = PyObject_Hash(proot);
            nodem.insert(std::pair<Py_hash_t, Node::BaseNode *>(h,pbn));

            PyObject *pnins = PyObject_GetAttrString(proot,"inputs");
            PyObject *pninv = PyObject_GetIter(pnins);
            //
            nx = 0;
            for(PyObject *pnin1 = PyIter_Next(pninv); pnin1; Py_DecRef(pnin1), pnin1 = PyIter_Next(pninv), ++nx){
                PyObject *plnks = PyObject_GetAttrString(pnin1,"links");
                PyObject *plnkv = PyObject_GetIter(plnks);
                PyObject *plnk1 = PyIter_Next(plnkv);
                Py_DECREF(plnks);
                Py_DECREF(plnkv);

				pidn = PyObject_GetAttrString(pnin1,"bl_idname");
				pn = PyUnicode_AsUTF8(pidn);

                if(!plnk1){
                    PyObject *pvalue = PyObject_GetAttrString(pnin1,"value"); //note: every socket class should have this property, even if not used
                    pbn->pnodes[nx] = Node::CreateNodeBySocket(pn,pvalue,l+1,pnt);
					pbn->indices[nx] = 0;

                    Py_DECREF(pvalue);
                    Py_DECREF(pidn);
                    continue;
                }else pbn->imask |= 1<<nx;

                PyObject *pnode = PyObject_GetAttrString(plnk1,"from_node");
                Py_hash_t h = PyObject_Hash(pnode);

				PyObject *psock = PyObject_GetAttrString(plnk1,"from_socket");
				Py_hash_t t = PyObject_Hash(psock);

				//get the child node output index
				PyObject *pcouts = PyObject_GetAttrString(pnode,"outputs");
				PyObject *pcoutv = PyObject_GetIter(pcouts);
	            uint sx = 0;
	            for(PyObject *pcout1 = PyIter_Next(pcoutv); pcout1; Py_DecRef(pcout1), pcout1 = PyIter_Next(pcoutv)){
					Py_hash_t q = PyObject_Hash(pcout1);
	                if(t == q)
						break;
					PyObject *pidn1 = PyObject_GetAttrString(pcout1,"bl_idname");
                    const char *pn1 = PyUnicode_AsUTF8(pidn1);
					if(strcmp(pn,pn1) == 0)
						++sx;
				}
				pbn->indices[nx] = sx;

	            Py_DECREF(pcoutv);
				Py_DECREF(pcouts);
				//
				Py_DECREF(psock);

                std::unordered_map<Py_hash_t, Node::BaseNode *>::const_iterator m = nodem.find(h);
                if(m != nodem.end()){
                    if(m->second->level < l+1)
                        m->second->level = l+1;
                    pbn->pnodes[nx] = m->second;
                }else pbn->pnodes[nx] = ntree(pnode,l+1,pnt);

                Py_DECREF(plnk1);
                Py_DECREF(pnode);
            }

            Py_DECREF(pninv);
            Py_DECREF(pnins);

            return pbn;
        };

        PyObject *proot = PyObject_CallMethod(pns1,"get","(s)","Surface Output");
        /*PyObject *prins = PyObject_CallMethod(PyObject_GetAttrString(pdata,"inputs"),"values","");
        PyList_GetItem(prins,Node::OutputNode::INPUT_SURFACE);*/

        Node::NodeTree *pntree = new Node::NodeTree();
        ntree(proot,0,pntree);

        pntree->SortNodes();
        pntree->ApplyBranchMask();

        //coordinate node:
        //outputs: vector
        //-node0-group
        //-internal node1 loop coordinate is given as a parameter to level-0 evaluation

        Py_hash_t h = PyObject_Hash(pnt1);
        ntm.insert(std::pair<Py_hash_t, Node::NodeTree *>(h,pntree));

        Py_DECREF(pns1);
        //Py_DECREF(pnt1); //borrowed ref
    }

    Py_DECREF(pngn);

	std::vector<Light> lights;

	PyObject *pbmeshn = PyUnicode_FromString("bmesh");
	PyObject *pbmesh = PyImport_Import(pbmeshn);

	PyObject *pbmops = PyObject_GetAttrString(pbmesh,"ops");

    PyObject *poat = PyObject_GetAttrString(pscene,"objects");
    PyObject *pobl = PyObject_CallMethod(poat,"values","");
	uint objc = PyList_Size(pobl);
	for(uint i = 0; i < objc; ++i){
		PyObject *pobj = PyList_GetItem(pobl,i);

		//get smoke domain modifiers
		PyObject *pmfs = PyObject_GetAttrString(pobj,"modifiers");
		PyObject *pmfl = PyObject_CallMethod(pmfs,"values","");
		uint mfc = PyList_Size(pmfl);
		for(uint j = 0; j < mfc; ++j){
			PyObject *pmf = PyList_GetItem(pmfl,j), *pmto;
			const char *pmts;
			//
			pmto = PyObject_GetAttrString(pmf,"type");
			pmts = PyUnicode_AsUTF8(pmto);
			if(strcasecmp(pmts,"SMOKE") != 0){
				Py_DECREF(pmto);
				continue;
			}
			Py_DECREF(pmto);

			pmto = PyObject_GetAttrString(pmf,"smoke_type");
			pmts = PyUnicode_AsUTF8(pmto);
			if(strcasecmp(pmts,"DOMAIN") != 0){
				Py_DECREF(pmto);
				continue;
			}
			Py_DECREF(pmto);

			std::unordered_map<Py_hash_t, Node::NodeTree *>::const_iterator m = ntm.begin();
            SceneData::SmokeCache *pprs = new SceneData::SmokeCache(m->second);
			//TODO: get the params somehow
			//
		}
		Py_DECREF(pmfs);

        //get particle systems
        PyObject *ppro = PyObject_GetAttrString(pobj,"particle_systems");
        PyObject *ppsl = PyObject_CallMethod(ppro,"values","");
        uint prc = PyList_Size(ppsl);
        for(uint j = 0; j < prc; ++j){
			std::unordered_map<Py_hash_t, Node::NodeTree *>::const_iterator m = ntm.begin();
            SceneData::ParticleSystem *pprs = new SceneData::ParticleSystem(m->second); //TODO: reserve() the vector size

            PyObject *pps = PyList_GetItem(ppsl,j);
			//TODO: Get visibility status. There's some weird modifier class for this.
            PyObject *ppr = PyObject_GetAttrString(pps,"particles");
            PyObject *ppi = PyObject_GetIter(ppr);
            for(PyObject *pni = PyIter_Next(ppi); pni; Py_DecRef(pni), pni = PyIter_Next(ppi)){
				PyObject *past = PyObject_GetAttrString(pni,"alive_state");
				const char *past1 = PyUnicode_AsUTF8(past);
				if(strcasecmp(past1,"DEAD") == 0){
					Py_DECREF(past);
					continue;
				}
				Py_DECREF(past);

                PyObject *pvco = PyObject_GetAttrString(pni,"location");
                float4 co = float4(
                    PyGetFloat(pvco,"x"),
                    PyGetFloat(pvco,"y"),
                    PyGetFloat(pvco,"z"),1.0f);
                pprs->pl.push_back(dfloat3(co)); //already in world-space
				Py_DECREF(pvco);

				PyObject *pvvl = PyObject_GetAttrString(pni,"velocity");
                float4 vl = float4(
                    PyGetFloat(pvvl,"x"),
                    PyGetFloat(pvvl,"y"),
                    PyGetFloat(pvvl,"z"),1.0f);
                pprs->vl.push_back(dfloat3(vl));
				Py_DECREF(pvvl);
            }
            Py_DECREF(ppi);
            Py_DECREF(ppr);
        }
        Py_DECREF(ppro);

        PyObject *phdn = PyObject_GetAttrString(pobj,"hide_render");
		if(PyLong_AsLong(phdn) != 0){
			Py_DECREF(phdn);
			continue;
		}
        Py_DECREF(phdn);

		PyObject *ptype = PyObject_GetAttrString(pobj,"type");
		const char *ptype1 = PyUnicode_AsUTF8(ptype);

        if(strcasecmp(ptype1,"LAMP") == 0){
			prot = PyObject_GetAttrString(pobj,"rotation_euler");
			pquat = PyObject_CallMethod(prot,"to_quaternion","");

            q = float4(
                PyGetFloat(pquat,"x"),
                PyGetFloat(pquat,"y"),
                PyGetFloat(pquat,"z"),
                PyGetFloat(pquat,"w"));
            d = float4(XMVector3Rotate(u.v,q.v));

            Py_DECREF(prot);
            Py_DECREF(pquat);

            PyObject *pdata = PyObject_GetAttrString(pobj,"data");
            PyObject *pdrop = PyObject_GetAttrString(pdata,"droplet");
            PyObject *pcolor = PyObject_GetAttrString(pdrop,"color");
            float intensity = PyGetFloat(pdrop,"intensity");

			Light light;
            light.direction = dfloat3(-d);
            light.color = intensity*dfloat3(
                PyGetFloat(pcolor,"r"),
                PyGetFloat(pcolor,"g"),
                PyGetFloat(pcolor,"b"));
            light.angle = PyGetFloat(pdrop,"angle");//0.7f; //0.98

            Py_DECREF(pcolor);
            Py_DECREF(pdrop);
            Py_DECREF(pdata);

			lights.push_back(light);
		}

        else if(strcasecmp(ptype1,"MESH") == 0){
	        std::unordered_map<Py_hash_t, Node::NodeTree *>::const_iterator m = ntm.begin();
	        SceneData::Surface *psobj = new SceneData::Surface(m->second);

	        XMMATRIX wm;
			PyObject *pwm = PyObject_GetAttrString(pobj,"matrix_world");
			PyObject *pmr = PyObject_GetAttrString(pwm,"row");
			PyObject *pmi = PyObject_GetIter(pmr);
			uint mr = 0;
			for(PyObject *pni = PyIter_Next(pmi); pni; Py_DecRef(pni), pni = PyIter_Next(pmi), ++mr){
	            wm.r[mr] = float4(
	                PyGetFloat(pni,"x"),
	                PyGetFloat(pni,"y"),
	                PyGetFloat(pni,"z"),
	                PyGetFloat(pni,"w")).v;
			}
			Py_DECREF(pmi);
			Py_DECREF(pmr);
			Py_DECREF(pwm);
			wm = XMMatrixTranspose(wm);

			PyObject *pbm = PyObject_CallMethod(pbmesh,"new","");
			PyObject_CallMethod(pbm,"from_object","OO",pobj,pscene);

			/*PyObject *pfs = PyObject_GetAttrString(pbm,"faces");
			PyObject_CallMethod(pbmops,"triangulate","O{s,O}",pbm,"faces",pfs); //doesn't work*/

			PyObject *pfa = PyObject_GetAttrString(pbm,"faces");
			PyObject *pag = Py_BuildValue("(O)",pbm);
			PyObject *pkw = PyDict_New();
			PyObject *ptm = PyObject_GetAttrString(pbmops,"triangulate");
			PyDict_SetItemString(pkw,"faces",pfa);
			PyObject_Call(ptm,pag,pkw);
	        //Py_DECREF(ptm); //crash
			Py_DECREF(pfa);
			Py_DECREF(ptm);
			Py_DECREF(pkw);

	        //uint vca = verts.size();

			PyObject *pvs = PyObject_GetAttrString(pbm,"verts");
			PyObject *pvi = PyObject_GetIter(pvs);
			for(PyObject *pni = PyIter_Next(pvi); pni; Py_DecRef(pni), pni = PyIter_Next(pvi)){
				PyObject *pvco = PyObject_GetAttrString(pni,"co");
	            float4 co = float4(
	                PyGetFloat(pvco,"x"),
	                PyGetFloat(pvco,"y"),
	                PyGetFloat(pvco,"z"),1.0f);
				dfloat3 sco = dfloat3(XMVector3TransformCoord(co.v,wm));
	            psobj->vl.push_back(sco);

	            Py_DECREF(pvco);
			}

			Py_DECREF(pvi);
			Py_DECREF(pvs);

			PyObject *pfs = PyObject_GetAttrString(pbm,"faces");
			//uint tl = PySequence_Size(pfs);
			PyObject *pfi = PyObject_GetIter(pfs);
			for(PyObject *pni = PyIter_Next(pfi); pni; Py_DecRef(pni), pni = PyIter_Next(pfi)){
				PyObject *pfvs = PyObject_GetAttrString(pni,"verts");
				PyObject *pfvi = PyObject_GetIter(pfvs);
				//uint fvc = PySequence_Size(pfvs);
				for(PyObject *pji = PyIter_Next(pfvi); pji; Py_DecRef(pji), pji = PyIter_Next(pfvi)){
	                PyObject *pxo = PyObject_GetAttrString(pji,"index");
	                uint index = PyLong_AsLong(pxo);
	                psobj->tl.push_back(index);
	                //tris.push_back(index+vca);

	                Py_DECREF(pxo);
				}
				Py_DECREF(pfvi);
			}

			Py_DECREF(pfi);
			Py_DECREF(pfs);

			PyObject_CallMethod(pbm,"free","");
			Py_DECREF(pbm);

	        //Py_DECREF(pobj); //borrowed ref
		}

		Py_DECREF(ptype);
	}

    Py_DECREF(poat);

    PyObject *prender = PyObject_GetAttrString(pscene,"blcloudrender");
    PyObject *ptransparent = PyObject_GetAttrString(prender,"transparent");
    uint flags = PyObject_IsTrue(ptransparent) & RENDER_TRANSPARENT;
    Py_DECREF(ptransparent);
    Py_DECREF(prender);

    PyObject *psampling = PyObject_GetAttrString(pscene,"blcloudsampling");
    PyObject *pscattevs = PyObject_GetAttrString(psampling,"scatterevs");
    uint scattevs = (uint)PyLong_AsLong(pscattevs);
	float msigmas = PyGetFloat(psampling,"msigmas");
	float msigmaa = PyGetFloat(psampling,"msigmaa");
    //
    Py_DECREF(pscattevs);
    Py_DECREF(psampling);

    PyObject *pgrid = PyObject_GetAttrString(pscene,"blcloudgrid");
    PyObject *pdsize = PyObject_GetAttrString(pgrid,"detailsize");
    float dsize = (float)PyFloat_AsDouble(pdsize);

    Py_DECREF(pdsize);
    Py_DECREF(pgrid);

    PyObject *pperf = PyObject_GetAttrString(pscene,"blcloudperf");
    PyObject *pcache = PyObject_GetAttrString(pperf,"cache");
    const char *pcms = PyUnicode_AsUTF8(pcache);
    SCENE_CACHE_MODE cm = (pcms[0] == 'R'?SCENE_CACHE_READ:pcms[0] == 'W'?SCENE_CACHE_WRITE:SCENE_CACHE_DISABLED);

    Py_DECREF(pcache);
    Py_DECREF(pperf);

    //TODO: cache postfix from bpy.path.basename(bpy.data.filepath)

    gpscene = new Scene(); //TODO: interface for blender status reporting
    gpscene->Initialize(dsize,cm);
	//gpscene->BuildScene();

	gpkernel = new RenderKernel();
    gpkernel->Initialize(gpscene,&sviewi,&sproji,&lights,scattevs,msigmas,msigmaa,rx,ry,w,h,flags);

	return Py_None;
}

static PyObject * DRE_Render(PyObject *pself, PyObject *pargs){
	//
    uint x0, y0, samples;
    if(!gpkernel || !PyArg_ParseTuple(pargs,"III",&x0,&y0,&samples))
		return 0;

    gpkernel->Render(x0,y0,samples);

    //TODO: write directly to the blender's render result?
	uint l = gpkernel->rx*gpkernel->ry;
    PyObject *prt = PyList_New(l);
	for(uint i = 0; i < l; ++i){
		PyObject *pc = Py_BuildValue("[f,f,f,f]",gpkernel->phb[i].x,gpkernel->phb[i].y,gpkernel->phb[i].z,gpkernel->phb[i].w);
		PyList_SET_ITEM(prt,i,pc);
	}

	return prt;
}

static PyObject * DRE_EndRender(PyObject *pself, PyObject *pargs){
	SceneData::SmokeCache::DeleteAll();
    SceneData::ParticleSystem::DeleteAll();
    SceneData::Surface::DeleteAll();
    Node::NodeTree::DeleteAll();

    gpkernel->Destroy();
	delete gpkernel;
	gpkernel = 0;

	gpscene->Destroy();
	delete gpscene;
    gpscene = 0;

	return Py_None;
}

static PyMethodDef g_blmethods[] = {
    //{"test",DRE_Test,METH_NOARGS,"test() doc string"},
    {"BeginRender",DRE_BeginRender,METH_VARARGS,"BeginRender() doc string"}, //CreateDevice
    //{"AddObject",DRE_AddObject,METH_VARARGS,"AddObject() doc string"},
    {"Render",DRE_Render,METH_VARARGS,"Render() doc string"},
    {"EndRender",DRE_EndRender,METH_NOARGS,"EndRender() doc string"},
    {0,0,0,0}
};

static struct PyModuleDef g_bldre = {
	PyModuleDef_HEAD_INIT,
    "droplet", //blcloud
    "Droplet Render Engine",
	-1,
	g_blmethods,
    0,0,0,0 //freefunc m_free
};

#define BLCLOUD_MODINITFUNC PyInit_libdroplet //PyInit_libblcloud

PyMODINIT_FUNC BLCLOUD_MODINITFUNC(){
    return PyModule_Create(&g_bldre);
}
