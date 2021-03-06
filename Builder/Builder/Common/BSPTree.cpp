#include "stdafx.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "BSPTree.h"

#if COMMON_COMPILER == COMPILER_MSVC
// disable portability warnings generated by 
// pointer arithmetic in code
#pragma warning( disable : 4311 4312 )
#endif 

inline int compare_float_lt(const void *arg1, const void *arg2)
{
	return (int)(*((float *)arg1) - *((float *)arg2));
}

inline int compare_float_gt(const void *arg1, const void *arg2)
{
	return (int)(*((float *)arg2) - *((float *)arg1));
}


void FastBSPTree::buildTree(int subdivisionMode)
{			

	timeBuildStart.set();

	// start building the BSPTree by subdividing along the larges axis first */
	root = new BSPTreeNode;
	root->splitcoord = 0;
	root->children = 0;
	numTris = (int)trianglelist->size();
	numNodes = 1;

	// Build list with all indices and list of intervals:	
	leftlist[0] = new TriangleIndexList(numTris);
	leftlist[1] = new TriangleIndexList(numTris);

	for (int i = 0; i < MAXBSPSIZE; i++)
		rightlist[i] = new TriangleIndexList();

	// subdivision mode == balanced tree ? then prepare min/max lists
	// for the triangle coordinates..
	if (subdivisionMode == BSP_SUBDIVISIONMODE_BALANCED) {
		minvals = new Vector3[trianglelist->size()];
		maxvals = new Vector3[trianglelist->size()];

		for (int i = 0; i < numTris; i++) {
			Triangle &tri = trianglelist->at(i);			
			(*leftlist[0])[i] = i;

			for (int j = 0; j < 3; j++) {
				minvals[i].e[j] = min(tri.p[0]->e[j], min(tri.p[1]->e[j], tri.p[2]->e[j]));
				maxvals[i].e[j] = max(tri.p[0]->e[j], max(tri.p[1]->e[j], tri.p[2]->e[j]));			
			}
		}
	}
	else { // all other subdivision modes:
		for (int i = 0; i < numTris; i++) {	
			(*leftlist[0])[i] = i;
		}
	}
	
	// Start subdividing, if we have got triangles
	if (trianglelist->size() > 0 && maxDepth > 0) {	
		int idx = 0;
		int useAxis = 0;

		// use largest axis for first subdivision
		Vector3 dim = max - min;

		if (subdivisionMode == BSP_SUBDIVISIONMODE_SIMPLE)
			SubdivideSimple(root, leftlist[0], 0, dim.indexOfMaxComponent(), min, max);
		else if (subdivisionMode == BSP_SUBDIVISIONMODE_NORMAL)
			Subdivide(root, leftlist[0], 0, dim.indexOfMaxComponent(), min, max);
		else
			SubdivideBalanced(root, leftlist[0], 0, dim.indexOfMaxComponent(), min, max);
	}
	else {
		root->children = NULL;
		root->splitcoord = 0.0;
		numLeafs = 1;
	}

	delete leftlist[0];
	delete leftlist[1];

	for (int i = 0; i < MAXBSPSIZE; i++)
		delete rightlist[i];

	// if subdivision = balanced tree, delete the arrays we
	// created above, they are not needed once the tree is built
	if (subdivisionMode == BSP_SUBDIVISIONMODE_BALANCED) {
		delete minvals;
		delete maxvals;
		minvals = NULL;
		maxvals = NULL;
	}

	timeBuildEnd.set();
	timeBuild = timeBuildEnd - timeBuildStart;
}


void FastBSPTree::SubdivideBalanced(BSPTreeNodePtr node, TriangleIndexList *trilist, int depth, int axis, Vector3 &min, Vector3 &max)
{
	int i,j;
	unsigned int triCount, newCount;
	BSPTreeNodePtr child[2];
	int axisCount[3];
	float axisSplit[3];
	TriangleIndexList *newlists[2];
	Vector3 newmin = min, 
			newmax = max;
	Vector3 dim;

	// Subdivide:

	// allocate memory for child nodes, aligned to 32 bit
	node->children = _aligned_malloc(sizeof(BSPTreeNode)*2, 4);
	child[0] = (BSPTreeNodePtr)node->children;
	child[1] = child[0] + 1;

	numNodes += 2;

	newlists[0] = leftlist[(depth+1)%2];
	newlists[1] = rightlist[depth];

	// assert 4 byte alignment, or else we mess up our axis marking
	assert(((unsigned int)newlists[0] & 3) == 0);
	assert(((unsigned int)newlists[1] & 3) == 0);

	// go through list, sort triangles into children
	triCount = (unsigned int)trilist->size();

	float *mins = new float[triCount];
	float *maxs = new float[triCount];
	
	for (j = 0; j < 3; j++, axis = (axis + 1)%3) {

		// Build sorted list of min and max vals for this axis:
		//
		i = 0;
		for (TriangleIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {
			mins[i] = minvals[*j].e[axis];
			maxs[i] = maxvals[*j].e[axis];
			i++;
		}

		qsort(mins, triCount, sizeof(float), compare_float_lt);
		qsort(maxs, triCount, sizeof(float), compare_float_gt);

		// Find split coordinate:
		node->splitcoord = (min.e[axis] + max.e[axis]) / 2.0f;
		for (i = 0; i < (int)triCount; i++) {
			if (mins[i] >= maxs[i]) {
				node->splitcoord = (max(maxs[i], mins[i-1]) + min(mins[i], maxs[i-1])) / 2.0f;
				break;
			}
		}
		
		axisCount[axis] = 0;
		for (TriangleIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {
			Triangle &t = trianglelist->at(*j);

			for (int k = 0; k < 3; k++) {
				if (t.p[k]->e[axis] <= axisSplit[axis]) {
					axisCount[axis]++;
					break;
				}				
			}
		}	
	}

	delete mins;
	delete maxs;

	// find axis with least number of triangles by subdivision
	if (axisCount[0] <= axisCount[1] && axisCount[0] <= axisCount[2])
		axis = 0;
	else if (axisCount[1] <= axisCount[2])
		axis = 1;
	else
		axis = 2;

	for (TriangleIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {
		Triangle &t = trianglelist->at(*j);

		for (int k = 0; k < 3; k++) {
			if (t.p[k]->e[axis] <= axisSplit[axis]) {
				newlists[0]->push_back(*j);
				break;
			}				
		}

		for (int k = 0; k < 3; k++) {
			if (t.p[k]->e[axis] >= axisSplit[axis]) {
				newlists[1]->push_back(*j);
				break;
			}
		}
	}

	newCount = (unsigned int)newlists[0]->size();

	// set split plane lower bits of children pointer
	node->children = (void *)((char *)node->children + axis + 1);	
	depth++;

	for (i = 0; i < 2; i++) { 

		// should we subdivide child further ?
		if ((newCount > (unsigned int)maxListLength) && (depth < maxDepth) && (newCount != triCount)) {
			// build new min/max bounding box:
			newmin.e[axis] = min.e[axis] + 0.5f * i * (max.e[axis] - min.e[axis]);
			newmax.e[axis] = min.e[axis] + 0.5f * (i+1) * (max.e[axis] - min.e[axis]);
			dim = newmax - newmin;
	
			// recursively subdivide further along largest axis
			SubdivideBalanced(child[i], newlists[i], depth, dim.indexOfMaxComponent(), newmin, newmax);

			// free list (not used anymore, since copy exists at leaf)
			delete newlists[i];
		}
		else { // make this child a leaf			
			unsigned int count = (unsigned int)newlists[i]->size();
			child[i]->children = new int[count];
			child[i]->splitcoord = (float)count;

			// copy vector into leaf
			for (unsigned int j=0; j<count; j++)
				((int *)child[i]->children)[j] = (*(newlists[i]))[j];
			//newlists[i]->swap(*((TriangleIndexList *)child[i]->children));

			// statistical information:
			numLeafs++;
			sumDepth += depth;
			sumTris  += count;
			if (depth > maxLeafDepth)
				maxLeafDepth = depth;
		}
	}
}


void FastBSPTree::Subdivide(BSPTreeNodePtr node, TriangleIndexList *trilist, int depth, int axis, Vector3 &min, Vector3 &max, int subdivideFailCount)
{
	int i;
	unsigned int triCount, newCount[2];
	BSPTreeNodePtr child[2];
	TriangleIndexList *newlists[2];
	Vector3 newmin = min, 
		    newmax = max;
	Vector3 dim;

	// Subdivide:

	// allocate memory for child nodes, aligned to 32 bit
	node->children = _aligned_malloc(sizeof(BSPTreeNode)*2, 4);
	child[0] = (BSPTreeNodePtr)node->children;
	child[1] = child[0] + 1;

	numNodes += 2;

	newlists[0] = leftlist[(depth+1)%2];
	newlists[1] = rightlist[depth];

	// assert 4 byte alignment, or else we mess up our axis marking
	assert(((unsigned int)newlists[0] & 3) == 0);
	assert(((unsigned int)newlists[1] & 3) == 0);
	
	// go through list, sort triangles into children
	triCount = (unsigned int)trilist->size();

	// split coordinate is in middle of previous span
	node->splitcoord = (min.e[axis] + max.e[axis]) / 2.0f;

	// subdivide along the axis until we have found
	// a subdivision that reduces the number of tris
	// in at least one branch
	i = 0;
	do {
		if (i > 0) {
			newlists[0]->clear();
			newlists[1]->clear();	
			
			if (newCount[0] > newCount[1])
				node->splitcoord = (min.e[axis] + node->splitcoord) / 2.0f;
			else
				node->splitcoord = (node->splitcoord + max.e[axis]) / 2.0f;
		}
		
		for (TriangleIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {
			Triangle &t = trianglelist->at(*j);

			for (int k = 0; k < 3; k++) {
				if (t.p[k]->e[axis] <= node->splitcoord) {
					newlists[0]->push_back(*j);
					break;
				}				
			}

			for (int k = 0; k < 3; k++) {
				if (t.p[k]->e[axis] >= node->splitcoord) {
					newlists[1]->push_back(*j);
					break;
				}
			}
		}		
	
		newCount[0] = (unsigned int)newlists[0]->size();
		newCount[1] = (unsigned int)newlists[1]->size();
		
		i++;
	} 
	while (i < 3 && (abs((int)(newCount[0]-newCount[1])) >= triCount/2) );	

	// set split plane lower bits of children pointer
	node->children = (void *)((char *)node->children + axis + 1);	
	depth++;

	float newbounds[3];
	newbounds[0] = min.e[axis];
	newbounds[1] = node->splitcoord;
	newbounds[2] = max.e[axis];

	for (i = 0; i < 2; i++) { 

		// We count the number of times a subdivision has not reduced
		// the number of triangles in this branch. If the count is too
		// high, no further subdivision will be attempted in order to
		// prevent an loop which will unnecessarily increase the depth
		int newFailCount;
		if (newCount[i] == triCount)
			newFailCount = subdivideFailCount+1;
		else
			newFailCount = 0;

		// should we subdivide child further ?
		if ((newCount[i] > (unsigned int)maxListLength) && (depth < maxDepth) && (newFailCount < 3)) {
			// build new min/max bounding box:
			newmin.e[axis] = newbounds[i];
			newmax.e[axis] = newbounds[i+1];
			dim = newmax - newmin;

			// recursively subdivide further along largest axis
			Subdivide(child[i], newlists[i], depth, dim.indexOfMaxComponent(), newmin, newmax, newFailCount);

			// free list (not used anymore, since copy exists at leaf)
			delete newlists[i];
		}
		else { // make this child a leaf			
			unsigned int count = (unsigned int)newlists[i]->size();
			child[i]->children = new int[count];
			child[i]->splitcoord = (float)count;

			// copy vector into leaf
			for (unsigned int j=0; j<count; j++)
				((int *)child[i]->children)[j] = (*(newlists[i]))[j];
			//newlists[i]->swap(*((TriangleIndexList *)child[i]->children));

			// statistical information:
			numLeafs++;
			sumDepth += depth;
			sumTris  += count;
			if (depth > maxLeafDepth)
				maxLeafDepth = depth;
		}
	}
}

void FastBSPTree::SubdivideSimple(BSPTreeNodePtr node, TriangleIndexList *trilist, int depth, int axis, Vector3 &min, Vector3 &max)
{
	int i;
	unsigned int triCount, newCount[2];
	BSPTreeNodePtr child[2];
	TriangleIndexList *newlists[2];
	Vector3 newmin = min, 
			newmax = max;
	Vector3 dim;

	// Subdivide:

	// allocate memory for child nodes, aligned to 32 bit
	assert(sizeof(BSPTreeNode) == 8);
	node->children = _aligned_malloc(sizeof(BSPTreeNode)*2, 4);
	child[0] = (BSPTreeNodePtr)node->children;
	child[1] = child[0] + 1;

	numNodes += 2;

	// create new triangle lists
	newlists[0] = leftlist[(depth+1)%2];
	newlists[1] = rightlist[depth];

	newlists[0]->clear();
	newlists[1]->clear();

	// assert 4 byte alignment, or else we mess up our axis marking
	assert(((unsigned int)newlists[0] & 3) == 0);
	assert(((unsigned int)newlists[1] & 3) == 0);

	// go through list, sort triangles into children
	triCount = (unsigned int)trilist->size();

	// split coordinate is in middle of previous span
	node->splitcoord = (min.e[axis] + max.e[axis]) / 2.0f;

	for (TriangleIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {
		Triangle &t = trianglelist->at(*j);

		for (int k = 0; k < 3; k++) {
			if (t.p[k]->e[axis] <= node->splitcoord) {
				newlists[0]->push_back(*j);
				break;
			}
		}

		for (int k = 0; k < 3; k++) {
			if (t.p[k]->e[axis] >= node->splitcoord) {
				newlists[1]->push_back(*j);
				break;
			}
		}
	}

	newCount[0] = (unsigned int)newlists[0]->size();
	newCount[1] = (unsigned int)newlists[1]->size();

	// set split plane lower bits of children pointer
	node->children = (void *)((char *)node->children + axis + 1);	
	depth++;

	for (i = 0; i < 2; i++) { 

		// should we subdivide child further ?
		if ((newCount[i] > (unsigned int)maxListLength) && (depth < maxDepth) && (newCount[0] + newCount[1] < 2*triCount)) {
			// build new min/max bounding box:
			newmin.e[axis] = min.e[axis] + 0.5f * i * (max.e[axis] - min.e[axis]);
			newmax.e[axis] = min.e[axis] + 0.5f * (i+1) * (max.e[axis] - min.e[axis]);
			dim = newmax - newmin;

			// recursively subdivide further along largest axis
			SubdivideSimple(child[i], newlists[i], depth, dim.indexOfMaxComponent(), newmin, newmax);		
		}
		else { // make this child a leaf	
			unsigned int count = (unsigned int)newlists[i]->size();
			child[i]->children = new int[count];
			child[i]->splitcoord = (float)count;

			// copy vector into leaf
			for (unsigned int j=0; j<count; j++)
				((int *)child[i]->children)[j] = (*(newlists[i]))[j];

			// statistical information:
			numLeafs++;
			sumDepth += depth;
			sumTris  += count;
			if (depth > maxLeafDepth)
				maxLeafDepth = depth;
		}
	}
}


bool FastBSPTree::RayTreeIntersect(const Ray &ray, HitPointPtr hit, float sign)
{		
	BSPTreeNodePtr currentNode, nearChild, farChild;
	int currentAxis;
	float dist, min, max;	
	const Vector3 &origin = ray.data[0], 
		          &direction = ray.data[1];

	_debug_TreeIntersectCount++;

	// Test if the whole BSP tree is missed by the input ray and 
	// min and max (t values for the ray for the scene's bounding box)
	if (!RayBoxIntersect(ray, this->min, this->max, &min, &max)) {	
		return false;
	}

	initStack();
	currentNode = root;

	// traverse BSP tree:
	while (currentNode != NULL) {
		currentAxis = ((unsigned int)currentNode->children) & 3;

		// while we are not a leaf..
		while ( currentAxis > 0 ) {
			currentAxis--;			
			
			// calculate distance to splitting plane
			dist = (currentNode->splitcoord - origin[currentAxis]) / direction[currentAxis];

			if ( currentNode->splitcoord >= origin[currentAxis] ) {
				nearChild = (BSPTreeNodePtr)((unsigned int)currentNode->children & ~3);
				farChild = nearChild + 1;
			} else {
				farChild = (BSPTreeNodePtr)((unsigned int)currentNode->children & ~3);
				nearChild = farChild + 1;
			}

			if ( (dist > max) || (dist < 0) ) {				
				currentNode = nearChild;
			} else if (dist < min)  {
				currentNode = farChild;
			} else {
				push(farChild, dist, max);
				currentNode = nearChild;
				max = dist;
			}

			currentAxis = ((unsigned int)currentNode->children) & 3;
		}

		// intersect with current node's members
		assert(currentAxis <= 0);
		if ( RayObjIntersect(ray, currentNode, hit, max) ) {
				return true; // was hit
		}

		pop(&currentNode, &min, &max);
	} 

	// ray did not intersect with tree
	return false;
}

bool FastBSPTree::isVisible(const Vector3 &origin, const Vector3 &target)
{	
	BSPTreeNodePtr currentNode, nearChild, farChild;
	int currentAxis;	
	float dist, min, max, target_t;

	Vector3 dir = target - origin;
	dir.makeUnitVector();
	Ray ray(origin, dir);

	// test if the whole BSP tree is missed by the input ray
	if (!RayBoxIntersect(ray, this->min, this->max, &min, &max)) {			
		return true;
	}

	// calculate t value to reach the ray's target
	int idx = dir.indexOfMaxComponent();
	target_t = (target.e[idx] - origin.e[idx]) / dir.e[idx];

	initStack();
	currentNode = root;

	// traverse BSP tree:
	while (currentNode != NULL) {
		currentAxis = ((unsigned int)currentNode->children) & 3;

		// while we are not a leaf..
		while ( currentAxis > 0 ) {
			currentAxis--;			

			// calculate distance to splitting plane
			dist = (currentNode->splitcoord - origin[currentAxis]) / dir[currentAxis];

			if ( currentNode->splitcoord >= origin[currentAxis] ) {
				nearChild = (BSPTreeNodePtr)((unsigned int)currentNode->children & ~3);
				farChild = nearChild + 1;
			} else {
				farChild = (BSPTreeNodePtr)((unsigned int)currentNode->children & ~3);
				nearChild = farChild + 1;
			}

			if ( (dist > max) || (dist < 0) ) {				
				currentNode = nearChild;
			} else if (dist < min)  {
				currentNode = farChild;
			} else {
				push(farChild, dist, max);
				currentNode = nearChild;
				max = dist;
			}

			currentAxis = ((unsigned int)currentNode->children) & 3;
		}

		// intersect with current node's members
		assert(currentAxis <= 0);
		if ( RayObjIntersectTarget(ray, currentNode, target_t) ) 
			return false; // was hit			

		pop(&currentNode, &min, &max);
	} 

	return true;
}

inline bool FastBSPTree::RayBoxIntersect(const Ray& r, Vector3 &min, Vector3 &max, float *returnMin, float *returnMax)  {

	float interval_min = -9999999.0f;
	float interval_max = 9999999.0f;
	Vector3 pp[2];
	pp[0] = min;
	pp[1] = max;

	float t0 = (pp[r.posneg[0]].e[0] - r.data[0].e[0]) * r.data[2].e[0];
	float t1 = (pp[r.posneg[1]].e[0] - r.data[0].e[0]) * r.data[2].e[0];
	if (t0 > interval_min) interval_min = t0;
	if (t1 < interval_max) interval_max = t1;
	if (interval_min > interval_max) return false;

	t0 = (pp[r.posneg[2]].e[1] - r.data[0].e[1]) * r.data[2].e[1];
	t1 = (pp[r.posneg[3]].e[1] - r.data[0].e[1]) * r.data[2].e[1];
	if (t0 > interval_min) interval_min = t0;
	if (t1 < interval_max) interval_max = t1;
	if (interval_min > interval_max) return false;

	t0 = (pp[r.posneg[4]].e[2] - r.data[0].e[2]) * r.data[2].e[2];
	t1 = (pp[r.posneg[5]].e[2] - r.data[0].e[2]) * r.data[2].e[2];
	if (t0 > interval_min) interval_min = t0;
	if (t1 < interval_max) interval_max = t1;

	*returnMin = interval_min;
	*returnMax = interval_max;
	return (interval_min <= interval_max);
}

inline bool FastBSPTree::RayObjIntersect(const Ray &ray, BSPTreeNodePtr objList, HitPointPtr obj, float tmax, float sign)   
{
	float point[2];
	float vdot, vdot2;
	float alpha, beta;
	float t, u0, v0;
	int foundtri = -1;
	int count = (int)objList->splitcoord;
	int *idxList = (int *)objList->children;

	_debug_ObjIntersectCount++;

	for (int i=0; i<count; i++,idxList++) {		
		IntersectionTriangle &tri = (*intersectlist)[*idxList];
		_debug_ObjTriIntersectCount++;
	
		// is ray parallel to plane or a backface ?
		vdot = dot(ray.direction(), tri.n);
		//if (fabs(vdot) < EPSILON)
		if (sign*vdot > EPSILON)
			continue;

		// find parameter t of ray -> intersection point
		vdot2 = dot(ray.origin(),tri.n);
		t = (tri.d - vdot2) / vdot;

		// if either too near or further away than a previous hit, we stop
		if (t < 0.001f || t > tmax)
			continue;

		// intersection point with plane
		point[0] = ray.data[0].e[tri.i1] + ray.data[1].e[tri.i1] * t;
		point[1] = ray.data[0].e[tri.i2] + ray.data[1].e[tri.i2] * t;

		// begin barycentric intersection algorithm
		u0 = point[0] - tri.p[0];
		v0 = point[1] - tri.p[1];

		// calculate and compare barycentric coordinates
		if (tri.u1inv == 0.0) {	// uncommon case 
			beta = u0 * tri.precalc3;
			if (beta < 0 || beta > 1)
				continue;
			alpha = (v0 - beta * tri.precalc2) * tri.precalc1;
		}
		else {	// common case, used for this analysis 
			beta = v0 * tri.precalc1 - u0 * tri.precalc2;
			if (beta < 0 || beta > 1)
				continue;
			alpha = (u0 - beta*tri.precalc3) * tri.u1inv;
		}

		// not in triangle ?
		if (alpha < 0 || (alpha + beta) > 1.0f)
			continue;

		// we have a hit:
		tmax = t;			 // new t value
		foundtri = *idxList; // save index
		obj->alpha = alpha;  // .. and barycentric coords
		obj->beta  = beta;
	}

	// A triangle was found during intersection :
	if (foundtri >= 0) {
		Triangle &pTri = trianglelist->at(foundtri);
		IntersectionTriangle &pTriIntersect = intersectlist->at(foundtri);

		// Fill hitpoint structure:
		//
		obj->m = pTri.material;
		obj->t = tmax;
		obj->triIdx = foundtri;

		#ifdef _USE_VERTEX_NORMALS
		// interpolate vertex normals..
		obj->n = pTri.normals[0] + obj->alpha * pTri.normals[1] + obj->beta * pTri.normals[2];
		#endif
		
		#ifdef _USE_TEXTURING
		// interpolate tex coords..
		obj->uv = pTri.uv[0] + obj->alpha * pTri.uv[1] + obj->beta * pTri.uv[2];
		#endif

		// hitpoint:
		obj->x = ray.pointAtParameter(tmax);					

		rayNumCounter[1]++;
		return true;
	}
	rayNumCounter[0]++;
	return false;	
}

inline bool FastBSPTree::RayObjIntersectTarget(const Ray &ray, BSPTreeNodePtr objList, float target_t)   
{
	float point[2];
	float vdot, vdot2;
	float alpha, beta;
	float t, u0, v0;
	int foundtri = -1;	
	int count = (int)objList->splitcoord;
	int *idxList = (int *)objList->children;

	for (int i=0; i<count; i++,idxList++) {		
		IntersectionTriangle &tri = (*intersectlist)[*idxList];

		// is ray parallel to plane or a backface ?
		vdot = dot(ray.direction(), tri.n);
		if (fabs(vdot) < EPSILON)
			continue;

		// find parameter t of ray -> intersection point
		vdot2 = dot(ray.origin(),tri.n);
		t = (tri.d - vdot2) / vdot;

		// if either too near or further away than a previous hit, we stop
		if (t < 0.001f || t > target_t)
			continue;

		// intersection point with plane
		point[0] = ray.data[0].e[tri.i1] + ray.data[1].e[tri.i1] * t;
		point[1] = ray.data[0].e[tri.i2] + ray.data[1].e[tri.i2] * t;

		// begin barycentric intersection algorithm
		u0 = point[0] - tri.p[0];
		v0 = point[1] - tri.p[1];

		// calculate and compare barycentric coordinates
		if (tri.u1inv == 0.0) {	// uncommon case 
			beta = u0 * tri.precalc3;
			if (beta < 0 || beta > 1)
				continue;
			alpha = (v0 - beta * tri.precalc2) * tri.precalc1;
		}
		else {	// common case, used for this analysis 
			beta = v0 * tri.precalc1 - u0 * tri.precalc2;
			if (beta < 0 || beta > 1)
				continue;
			alpha = (u0 - beta*tri.precalc3) * tri.u1inv;
		}

		// not in triangle ?
		if (alpha < 0 || (alpha + beta) > 1.0f)
			continue;

		// we have a hit:
		return true;	
	}
	
	return false;	
}

int FastBSPTree::getNumTris() {
	return numTris;
}

void FastBSPTree::printTree(bool dumpTree, const char *LoggerName) {
	LogManager *log = LogManager::getSingletonPtr();
	char outputBuffer[2000];
	log->logMessage("-------------------------------------------", LoggerName);
	log->logMessage("BSP Tree Statistics", LoggerName);
	log->logMessage("-------------------------------------------", LoggerName);
	sprintf(outputBuffer, "Time to build:\t%d seconds, %d milliseconds", (int)timeBuild, (int)((timeBuild - floor(timeBuild)) * 1000));
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Triangles:\t%d", numTris);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Nodes:\t\t%d", numNodes);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Leafs:\t\t%d", numLeafs);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Max. leaf depth:\t%d (of %d)", maxLeafDepth, maxDepth);
	log->logMessage(outputBuffer, LoggerName);
	if (numLeafs > 0) {
		sprintf(outputBuffer, "Avg. leaf depth:\t%.2f", (float)sumDepth / (float)numLeafs);
		log->logMessage(outputBuffer, LoggerName);
	
		sprintf(outputBuffer, "Avg. tris/leaf:\t%.2f", (float)sumTris / (float)numLeafs);
		log->logMessage(outputBuffer, LoggerName);
	}
	sprintf(outputBuffer, "Used memory:\t%d KB", (numNodes*sizeof(BSPTreeNode) + (sumTris * sizeof(int))) / 1024);
	log->logMessage(outputBuffer, LoggerName);
	
	if (dumpTree) {
		log->logMessage("-------------------------------------------", LoggerName);
		log->logMessage("BSP Tree structure", LoggerName);
		log->logMessage("-------------------------------------------", LoggerName);
		printNode(LoggerName, root, 0);
		log->logMessage("-------------------------------------------", LoggerName);
	}
}

void FastBSPTree::printNode(const char *LoggerName, BSPTreeNodePtr current, int depth) {
	
	LogManager *log = LogManager::getSingletonPtr();
	char outStr[500];
	char indent[100];
	char *axis = "LXYZ";
	int axisNr;
	int numTris = 0;
	BSPTreeNodePtr children;

	indent[0] = 0;
	if (depth > 0) {
		int i;
		for (i = 0; i < (depth-1)*2; i++) {
			indent[i]   = ' ';			
		}
		indent[i]   = '|';
		indent[i+1] = '-';		
		indent[depth*2] = 0;
	}

	if (depth == 0) {
		if (this->root == NULL)
			return;
		current = this->root;		
	}

	// find axis (encoded in the lower two bits)	
	axisNr = ((unsigned int)current->children) & 3;	
	children = (BSPTreeNodePtr)((unsigned int)current->children & (~3));	

	if (axisNr == 0) { // leaf		
		numTris = (unsigned int)current->splitcoord;
		sprintf(outStr, "%sLeaf %d Tris", indent, numTris);
	}
	else
		sprintf(outStr, "%sNode %c (%.2f)", indent, axis[axisNr], current->splitcoord);
	
	
	log->logMessage(outStr, LoggerName);

	if (axisNr > 0) {
		printNode(LoggerName, children, depth + 1);		
		printNode(LoggerName, children+1, depth + 1);
	}
}

/**
* Destroys the tree, frees memory. Called by dtor.
*/	
void FastBSPTree::destroyNode(BSPTreeNodePtr current) {
	// Find axis (encoded in the lower two bits)	
	int axisNr = ((unsigned int)current->children) & 3;	
	BSPTreeNodePtr children = (BSPTreeNodePtr)((unsigned int)current->children & (~3));	

	// Inner node:
	if (axisNr > 0) {
		destroyNode(children);
		destroyNode(children+1);

		// free children
		_aligned_free(children);
	}
	else { // Leaf node:
		// free triangle list
		if (current != root)
			delete[] children;
	}
}