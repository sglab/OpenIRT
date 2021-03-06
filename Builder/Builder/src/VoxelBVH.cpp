#include <windows.h>
#include <gl/gl.h>
#include <gl/glu.h>
//#include <gl/glut.h>

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

#include "VoxelBVH.h"
#include "Defines.h"


#if HIERARCHY_TYPE == TYPE_BVH 
#if COMMON_COMPILER == COMPILER_MSVC
// disable portability warnings generated by 
// pointer arithmetic in code
#pragma warning( disable : 4311 4312 4102 )
#endif 

// surface area of a voxel:
__inline float surfaceArea(float dim1, float dim2, float dim3) {
	return 2.0f * ((dim1 * dim2) + (dim2 * dim3) + (dim1 * dim3));
}

void VoxelBVH::buildTree()
{				
	BSPArrayTreeNode root;

	timeBuildStart.set();

	subdivisionMode = BSP_SUBDIVISIONMODE_NORMAL;

	// Build list with all indices and list of intervals:	
	leftlist[0] = new VoxelIndexList(treeStats.numTris);
	leftlist[1] = new VoxelIndexList(treeStats.numTris);

	for (int i = 0; i < MAXBSPSIZE; i++)
		rightlist[i] = new VoxelIndexList();

	// open temporary file for the indices
	FILE *indexFP = fopen("tempindex.tmp", "wb+");
	// .. and tree nodes
	FILE *nodeFP = fopen("tempnodes.tmp", "wb+");

	if (!indexFP) {
		LogManager *log = LogManager::getSingletonPtr();
		log->logMessage(LOG_ERROR, "Unable to write index file tempindex.tmp!");
		return;
	}

	if (!nodeFP) {
		LogManager *log = LogManager::getSingletonPtr();
		log->logMessage(LOG_ERROR, "Unable to write node file tempnodes.tmp!");
		return;
	}

	treeStats.numNodes = 1;	

	// subdivision mode == SAH ? then prepare min/max lists
	// for the triangle coordinates..
	minvals = new Vector3[treeStats.numTris];
	maxvals = new Vector3[treeStats.numTris];

	for (int j = 0; j < treeStats.numTris; j++) {
		const Voxel &voxel = voxellist[j];
		
		(*leftlist[0])[j] = j;

		minvals[j] = voxel.min;			
		maxvals[j] = voxel.max;
	}
	
	// Start subdividing, if we have got triangles
	if (treeStats.numTris > 1 && treeStats.maxDepth > 0) {	
		int idx = 0;
		int useAxis = 0;

		// use largest axis for first subdivision
		Vector3 dim = max - min;

		fseek(nodeFP, sizeof(BSPArrayTreeNode), SEEK_SET);

		Subdivide(0, leftlist[0], 0, indexFP, nodeFP);		
	
	}
	else { // no triangles or depth limit == 0: make root
		treeStats.numLeafs = 1;

		unsigned int count = (unsigned int)leftlist[0]->size();
		#ifdef _USE_ONE_TRI_PER_LEAF
		root.triIndex = 3;
		#else
		root.indexCount = MAKECHILDCOUNT(count);
		root.indexOffset = 0;
		#endif
		
		fwrite(&root, sizeof(BSPArrayTreeNode), 1, nodeFP);

		// write vector to file:
		fwrite(& (*(leftlist[0]))[0], sizeof(int), count, indexFP);
	}
	if(treeStats.numTris == 1) treeStats.sumTris = 1;

	delete leftlist[0];
	delete leftlist[1];

	for (int k = 0; k < MAXBSPSIZE; k++)
		delete rightlist[k];

	delete[] minvals;
	delete[] maxvals;
	minvals = 0;
	maxvals = 0;

	// read in temporary files
	makeFlatRepresentation(indexFP, nodeFP);

	// remove temporary files (may be huge)
	remove("tempindex.tmp");
	remove("tempnodes.tmp");

	timeBuildEnd.set();
	treeStats.timeBuild += timeBuildEnd - timeBuildStart;
}

void VoxelBVH::makeFlatRepresentation(FILE *indexFP, FILE *nodeFP) {
	LogManager *log = LogManager::getSingletonPtr();
	log->logMessage(LOG_DEBUG, "Reading in array tree representation from file...");	

	// temporary node file:
	log->logMessage(LOG_DEBUG, " - read in node list.");	
	tree = new BSPArrayTreeNode[treeStats.numNodes];
	fseek(nodeFP, 0, SEEK_SET);
	fread((void *)tree, sizeof(BSPArrayTreeNode), treeStats.numNodes, nodeFP);
	fclose(nodeFP);

	// temporary index list file:
	log->logMessage(LOG_DEBUG, " - read in index list.");
	indexlists = new int[treeStats.sumTris];
	fseek(indexFP, 0, SEEK_SET);
	fread((void *)indexlists, sizeof(int), treeStats.sumTris, indexFP);
	fclose(indexFP);
}

bool VoxelBVH::Subdivide(long myOffset, VoxelIndexList *trilist, int depth, FILE *indexFP, FILE *nodeFP)
{
	if(trilist->size() <= 1) return false;
	static unsigned int curIndex = 0;	

	BSPArrayTreeNode node;
	unsigned int triCount = (unsigned int)trilist->size(), newCount[2];	
	VoxelIndexList *newlists[2];
	int i, k;

	long curOffset;

	// mark current file offset
	curOffset = ftell(nodeFP);


	// taejoon
	node.min.e[0] = FLT_MAX;
	node.min.e[1] = FLT_MAX;
	node.min.e[2] = FLT_MAX;
	node.max.e[0] = -FLT_MAX;
	node.max.e[1] = -FLT_MAX;
	node.max.e[2] = -FLT_MAX;

	for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) 
	{
		updateMinBB(node.min, voxelMins[*j]);
		updateMaxBB(node.max, voxelMaxs[*j]);
	}

	/*
	node.min.e[0] -= 5;
	node.min.e[1] -= 5;
	node.min.e[2] -= 5;
	node.max.e[0] += 5;
	node.max.e[1] += 5;
	node.max.e[2] += 5;
	*/

	// find biggest axis
	Vector3 diff = node.max - node.min;
	int bestAxis = diff.indexOfMaxComponent();;

	// find center of centers.
	float center = 0;
	for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) 
	{
		center += voxelMins[*j].e[bestAxis];
		center += voxelMaxs[*j].e[bestAxis];
	}
	center *= 0.5;
	center /= trilist->size();

	float bestSplitCoord = .5 * diff[bestAxis] +min[bestAxis];
//	float bestSplitCoord = center;
	// set split coordinate

	#ifdef BVHNODE_16BYTES
		
		#ifdef FOUR_BYTE_FOR_BV_NODE
		// set pointer to children and split plane in lower bits 
		node.children = (curOffset >> 4);
		node.children2 = bestAxis;
		node.lodIndex = 0;
		#else
		// set pointer to children and split plane in lower bits 
		node.children = (curOffset >> 3) | bestAxis;
		#ifndef _USE_CONTI_NODE
		node.children2 = (curOffset + sizeof(BSPArrayTreeNode)) >> 3;
		#else
		node.children2 = 0;
		#endif
		#endif

	#else
	// set pointer to children and split plane in lower bits 
	node.children = (curOffset >> 1) | bestAxis;
	#endif

	// create new triangle lists
	newlists[0] = leftlist[(depth+1)%2];
	newlists[1] = rightlist[depth];
	newlists[0]->clear();
	newlists[1]->clear();

	// 
	// do the real assignment of triangles to child nodes:
	// 
	float avgloc;
	for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {			
		avgloc = voxelMins[*j].e[bestAxis];
		avgloc += voxelMaxs[*j].e[bestAxis];
		avgloc *=.5;

		if(avgloc <= bestSplitCoord)
			newlists[0]->push_back(*j);
		else
			newlists[1]->push_back(*j);
	}
	// special case: subdivision did not work out, just go half/half
	if(newlists[0]->size() == 0 || newlists[1]->size() == 0)
	{
		int mid = trilist->size()/2;
		newlists[0]->clear();
		newlists[1]->clear();
		int i = 0;
		for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j, i++) {
			if(i < mid)
				newlists[0]->push_back(*j);
			else
				newlists[1]->push_back(*j);
		}
	}

	newCount[0] = (unsigned int)newlists[0]->size();
	newCount[1] = (unsigned int)newlists[1]->size();	

	// jump back to own offset
	fseek(nodeFP, myOffset, SEEK_SET);

	// write real data to file
	fwrite(&node, sizeof(BSPArrayTreeNode), 1, nodeFP);

	// jump forward to previous address + size of 2 children -> new current position
	fseek(nodeFP, curOffset + 2*sizeof(BSPArrayTreeNode), SEEK_SET);

	// we officially have 2 more tree nodes!
	treeStats.numNodes += 2;


	depth++;

	for (i = 0; i < 2; i++) { 
		bool was_subdivided = false;
		long thisChildFileOffset = curOffset + i*sizeof(BSPArrayTreeNode);

		if(newCount[i] > 1) Subdivide(thisChildFileOffset, newlists[i], depth, indexFP, nodeFP);
		if(newCount[i] == 1) { // make this child a leaf
			BSPArrayTreeNode newLeaf;

			unsigned int count = (unsigned int)newlists[i]->size();
			#ifdef _USE_ONE_TRI_PER_LEAF
			newLeaf.triIndex = (curIndex << 2) | 3;
			#else
			newLeaf.indexCount = MAKECHILDCOUNT(count);
			newLeaf.indexOffset = curIndex;
			#endif
			curIndex += count;

	#if HIERARCHY_TYPE == TYPE_BVH 
			if(count > 0)
			{
				// taejoon
				newLeaf.min.e[0] = FLT_MAX;
				newLeaf.min.e[1] = FLT_MAX;
				newLeaf.min.e[2] = FLT_MAX;
				newLeaf.max.e[0] = -FLT_MAX;
				newLeaf.max.e[1] = -FLT_MAX;
				newLeaf.max.e[2] = -FLT_MAX;
				updateMinBB(newLeaf.min, voxelMins[(*(newlists[i]))[0]]);
				updateMaxBB(newLeaf.max, voxelMaxs[(*(newlists[i]))[0]]);
			}
	#endif

			// write final node information to file:
			long tempPos = ftell(nodeFP);
			fseek(nodeFP, thisChildFileOffset, SEEK_SET);
			fwrite(&newLeaf, sizeof(BSPArrayTreeNode), 1, nodeFP);
			fseek(nodeFP, tempPos, SEEK_SET);

			// taejoon
			if(count > 0)
			{
			// write index vector to file:
			fwrite(& (*(newlists[i]))[0], sizeof(int), count, indexFP);
			}

			// statistical information:
			treeStats.numLeafs++;
			treeStats.sumDepth += depth;
			treeStats.sumTris  += count;
			if (depth > treeStats.maxLeafDepth)
				treeStats.maxLeafDepth = depth;
			if (count > treeStats.maxTriCountPerLeaf)
				treeStats.maxTriCountPerLeaf = count;
		}
	}

	return true;
}

#if 0
bool VoxelBVH::SubdivideSAH(long myOffset, VoxelIndexList *trilist, int depth,  Vector3 &min, Vector3 &max, FILE *indexFP, FILE *nodeFP)
{
	static float *mins, *maxs, *collinear;
	static unsigned int curIndex = 0;	

	BSPArrayTreeNode node;
	unsigned int triCount = (unsigned int)trilist->size(), newCount[2];	
	VoxelIndexList *newlists[2];
	Vector3 newmin = min, newmax = max;
	Vector3 dim;
	int i, k;
	int bestAxis = -1;
	float bestCost = FLT_MAX, bestSplitCoord, currentSplitCoord;
	unsigned int numLeft, numRight;
	unsigned int curMin = 0, curMax = 0, curCol = 0;
	float bestAreaLeft = -1.0f, bestAreaRight = -1.0f, wholeArea = -1.0f;	
	int bestNumLeft = triCount, bestNumRight = triCount;
	long curOffset;

	bool debug1 = false; // && depth > 30; 
	bool debug2 = false;  //&& triCount == 23;	

	// allocate space for interval values:	
	mins = new float[triCount+1];
	maxs = new float[triCount+1];	
	collinear = new float[triCount+1];

	// for each axis:
	for (int curAxis = 0; curAxis < 3; curAxis++) {		

		// early termination: bb of axis has dimension 0
		if ((max[curAxis] - min[curAxis]) < EPSILON)
			continue;

		// Build sorted list of min and max vals for this axis:
		//
		i = k = 0;
		for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {
			if (minvals[*j].e[curAxis] == maxvals[*j].e[curAxis]) {
				collinear[k++] = minvals[*j].e[curAxis];
				continue;
			}

			mins[i] = minvals[*j].e[curAxis];
			maxs[i] = maxvals[*j].e[curAxis];

			i++;
		}

		// put guard values at end of array, needed later so we don't go
		// beyond end of array...
		mins[i] = FLT_MAX;
		maxs[i] = FLT_MAX;
		collinear[k] = FLT_MAX;

		// sort arrays:
		std::sort(mins, mins+i);
		std::sort(maxs, maxs+i);
		std::sort(collinear, collinear+k);

		int numMinMaxs = i + 1;
		int numCols = k + 1;				

		unsigned int subtractRight = 0, addLeft = 0;
		unsigned int curMin = 0, curMax = 0, curCol = 0;
		wholeArea = -1.0f;
		currentSplitCoord = -FLT_MAX;

		numLeft = 0;
		numRight = triCount;

		// test for subdivide to create an empty cell:
		float emptySpanBegin = min(mins[0],collinear[0]) - min[curAxis];
		float emptySpanEnd = max[curAxis] - max(maxs[i-1], collinear[k-1]);
		float threshold = treeStats.emptySubdivideRatio * (max[curAxis] - min[curAxis]);

		// empty area to the left?
		if (emptySpanBegin > threshold) {
			bestSplitCoord = currentSplitCoord = min(mins[0],collinear[0]) - 0.01*emptySpanBegin;
			bestCost = 0;
			bestNumLeft = numLeft = 0;
			bestNumRight = numRight = triCount;
			bestAxis = curAxis;

			if (debug1) {		
				cout << "Found free space left: " << emptySpanBegin << " (" << (emptySpanBegin / (max[curAxis] - min[curAxis])) << ")  at Coord " << currentSplitCoord << endl;
				cout << min << " - " << max << " (" << min(mins[0],collinear[0]) << endl;
			}

			break;
		}
		else if (emptySpanEnd > threshold) { // empty area to the right?
			bestSplitCoord = currentSplitCoord =  max(maxs[i-1], collinear[k-1]) + 0.01*emptySpanEnd;
			bestCost = 0;
			bestNumLeft = numLeft = triCount;
			bestNumRight = numRight = 0;
			bestAxis = curAxis;

			if (debug1) {
				cout << "Found free space right: " << emptySpanEnd << " (" << (emptySpanEnd / (max[curAxis] - min[curAxis])) << ")  at Coord " << currentSplitCoord << endl;
				cout << min << " - " << max << " (" << max(maxs[i-1], collinear[k-1]) << endl;
			}

			break;
		}
		else {

			//
			// test all possible split planes according to surface area heuristic:
			//

			wholeArea = surfaceArea(max.e[0] - min.e[0], max.e[1] - min.e[1], max.e[2] - min.e[2]);

			if (debug1) {				
				cout << "Find Split curAxis=" << curAxis << " " << min << " - " << max << endl;
				cout << " nTris=" << triCount << " Startcoord = " << currentSplitCoord << endl;
			}

			if (debug2) {
				int l;			
				cout << "Mins: ";
				for (l = 0; l < numMinMaxs; l++)
					cout << mins[l] << " ";
				cout << endl;

				cout << "Maxs: ";
				for (l = 0; l < numMinMaxs; l++)
					cout << maxs[l] << " ";
				cout << endl;

				cout << "Cols: ";
				for (l = 0; l < numCols; l++)
					cout << collinear[l] << " ";
				cout << endl;
			}

			while (mins[curMin] != FLT_MAX || maxs[curMax] != FLT_MAX || collinear[curCol] != FLT_MAX) {
				float newCoord;

				numRight -= subtractRight;
				numLeft  += addLeft;
				addLeft = 0;
				subtractRight = 0;

				do {
					if (collinear[curCol] <= mins[curMin] && collinear[curCol] <= maxs[curMax]) {
						newCoord = collinear[curCol++];

						if (newCoord <= (min.e[curAxis] + max.e[curAxis]) / 2.0f) {
							numLeft++;
							numRight--;
						}
						else {
							addLeft++;
							subtractRight++;
						}
					}
					// find next split coord, either from min or max interval values
					else if (mins[curMin] <= maxs[curMax]) { // take from mins:
						newCoord = mins[curMin++];

						// since this is a minimal value of some triangle, we now have one more
						// triangle on the left side:
						addLeft++;
					}
					else { // take from maxs:
						newCoord = maxs[curMax++];

						// since this is a maximal value of some triangle, we have one less
						// triangle on the right side at the next interval:
						numRight--;
					}
				} while (mins[curMin] == newCoord || maxs[curMax] == newCoord || collinear[curCol] == newCoord);

				if (debug2)
					cout << " [" << i << "] : " << newCoord << endl;

				// don't test if the new split coord is the same as the old one, waste of time..
				if (newCoord == currentSplitCoord || numLeft == 0 || numRight == 0) 
					continue;

				// set new split coord to test
				currentSplitCoord = newCoord;				

				// calculate area on each side of split plane:
				float areaLeft = surfaceArea(currentSplitCoord - min.e[curAxis],  max.e[(curAxis+1)%3] - min.e[(curAxis+1)%3], max.e[(curAxis+2)%3] - min.e[(curAxis+2)%3]) / wholeArea;
				float areaRight = surfaceArea(max.e[curAxis] - currentSplitCoord,  max.e[(curAxis+1)%3] - min.e[(curAxis+1)%3], max.e[(curAxis+2)%3] - min.e[(curAxis+2)%3]) / wholeArea;

				//
				// calculate cost for this split according to SAH:
				//
				float currentCost = BSP_COST_TRAVERSAL + BSP_COST_INTERSECTION * (areaLeft * numLeft + areaRight * numRight);

				if (debug2)
					cout << "  - accepted! cost=" << currentCost << " L:" << numLeft << " R:" << numRight << endl;

				// better than previous minimum?
				if (currentCost < bestCost && numLeft != triCount && numRight != triCount) {
					bestCost = currentCost;
					bestSplitCoord = currentSplitCoord;
					bestNumLeft = numLeft;
					bestNumRight = numRight;
					bestAreaLeft = areaLeft;
					bestAreaRight = areaRight;
					bestAxis = curAxis;
				}
			}
		}

		if (debug1)
			cout << " axis=" << curAxis << " best: cost=" << bestCost << " Split:" << bestSplitCoord << " (L:" << bestNumLeft << " R:" << bestNumRight << ")" << endl;
	}

	// free memory for interval lists
	delete mins;
	delete maxs;
	delete collinear;

	if (debug1)
		cout << "Best: axis=" << bestAxis << " cost=" << bestCost << " Split:" << bestSplitCoord << " (L:" << bestNumLeft << " R:" << bestNumRight << ")" << endl;

	// this subdivision didn't work out... all triangles are still in one of the sides:													
	if ((bestNumLeft == triCount && bestNumRight > 0) || (bestNumLeft > 0 && bestNumRight == triCount)) {

		//if (debug1)
		//cout << " ----> could not find split coord for any axis! (N:" << triCount << " Axis:" << axis << " L:" << bestNumLeft << " R:" << bestNumRight << ")\n";
		//getchar();

		//renderSplit(bestAxis, bestSplitCoord, min, max, newlists);

		return false;
	}


	// determine if cost for splitting would be greater than just keeping this:
	//if (depth != 0 && bestCost > (BSP_COST_INTERSECTION * triCount)) {	
	//	return false;
	//}

	// mark current file offset
	curOffset = ftell(nodeFP);

	// jump back to own offset
	fseek(nodeFP, myOffset, SEEK_SET);

	//cout << "Writing BSP tree node to offset " << myOffset << ", Children = " << curOffset << endl;	

	// taejoon
	node.min.e[0] = FLT_MAX;
	node.min.e[1] = FLT_MAX;
	node.min.e[2] = FLT_MAX;
	node.max.e[0] = -FLT_MAX;
	node.max.e[1] = -FLT_MAX;
	node.max.e[2] = -FLT_MAX;
	for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) 
	{
		Vector3 tMin = voxelMins[*j];
		Vector3 tMax = voxelMaxs[*j];
		updateMinBB(node.min, voxelMins[*j]);
		updateMaxBB(node.max, voxelMaxs[*j]);
	}

//	node.min = min;
//	node.max = max;

	fprintf(fp, "%f %f %f %f %f %f\n", min.e[0], min.e[1], min.e[2], max.e[0], max.e[1], max.e[2]);

	#ifdef BVHNODE_16BYTES
		
		#ifdef FOUR_BYTE_FOR_BV_NODE
		// set pointer to children and split plane in lower bits 
		node.children = (curOffset >> 4);
		node.children2 = bestAxis;
		node.lodIndex = 0;
		#else
		// set pointer to children and split plane in lower bits 
		node.children = (curOffset >> 1) | bestAxis;
		node.children2 = (curOffset + sizeof(BSPArrayTreeNode)) >> 1;
		#endif

	#else
	// set pointer to children and split plane in lower bits 
	node.children = (curOffset >> 1) | bestAxis;
	#endif

	// write real data to file
	fwrite(&node, sizeof(BSPArrayTreeNode), 1, nodeFP);

	// jump forward to previous address + size of 2 children -> new current position
	fseek(nodeFP, curOffset + 2*sizeof(BSPArrayTreeNode), SEEK_SET);

	// we officially have 2 more tree nodes!
	treeStats.numNodes += 2;

	// create new triangle lists
	newlists[0] = leftlist[(depth+1)%2];
	newlists[1] = rightlist[depth];
	newlists[0]->clear();
	newlists[1]->clear();

	// 
	// do the real assignment of triangles to child nodes:
	// 

	int smallerSide = (bestAreaLeft <= bestAreaRight)?0:1;
	for (VoxelIndexListIterator j = trilist->begin(); j != trilist->end(); ++j) {			

		// special case: triangle is coplanar to split plane.
		//  - if split plane is also a side of the AABB, put the tri into respective side
		//  - otherwise: put to smaller side (in terms of area)
		if (minvals[*j].e[bestAxis] == maxvals[*j].e[bestAxis]) {
			float v = minvals[*j].e[bestAxis];
			if (v == min.e[bestAxis]) {
				newlists[0]->push_back(*j);			
				continue;
			} 
			else if (v == max.e[bestAxis]) {
				newlists[1]->push_back(*j);
				continue;
			}
			else if (v == node.splitcoord) {
				newlists[smallerSide]->push_back(*j);
				continue;
			}
		}

		// non-collinear triangle, put in respective child or
		// both if overlapping
		if (minvals[*j].e[bestAxis] >= node.splitcoord) 
			newlists[1]->push_back(*j);
		else if (maxvals[*j].e[bestAxis] <= node.splitcoord) 
			newlists[0]->push_back(*j);
		else {
			newlists[0]->push_back(*j);
			newlists[1]->push_back(*j);
		}

	}

	newCount[0] = (unsigned int)newlists[0]->size();
	newCount[1] = (unsigned int)newlists[1]->size();	

	//if (newCount[0] != bestNumLeft || newCount[1] != bestNumRight) {
	/*if (debug2) {
	cout << "Counts inequal: Split:" << bestSplitCoord << " (N:" << triCount << " L:" << newCount[0] << " R:" << newCount[1] << ")" << endl;
	cout << "  should be : (L:" << bestNumLeft << " R:" << bestNumRight << ")" << endl;		


	renderSplit(axis, bestSplitCoord, min, max, newlists);
	}*/


	depth++;

	for (i = 0; i < 2; i++) { 
		bool was_subdivided = false;
		long thisChildFileOffset = curOffset + i*sizeof(BSPArrayTreeNode);

		// should we subdivide child further ?
		if (newCount[i] > (unsigned int)treeStats.maxListLength && depth < treeStats.maxDepth && (newCount[0] + newCount[1] < 2*triCount)) {
			if (debug1)
				cout << " ----> subdivide child " << i << endl;
			// build new min/max bounding box (if i=0, then [min...splitcoord], otherwise [splitcoord...max]:
			newmin.e[bestAxis] = (i==0)?min.e[bestAxis]:node.splitcoord;
			newmax.e[bestAxis] = (i==0)?node.splitcoord:max.e[bestAxis];
			dim = newmax - newmin;

			// recursively subdivide further along largest axis
			if (SubdivideSAH(thisChildFileOffset, newlists[i], depth, newmin, newmax, indexFP, nodeFP))
				was_subdivided = true;
		}

		if (!was_subdivided) { // make this child a leaf
			BSPArrayTreeNode newLeaf;

			unsigned int count = (unsigned int)newlists[i]->size();
			#ifdef _USE_ONE_TRI_PER_LEAF
			newLeaf.triIndex = (curIndex << 2) | 3;
			#else
			newLeaf.indexCount = MAKECHILDCOUNT(count);
			newLeaf.indexOffset = curIndex;
			#endif
			curIndex += count;
			// taejoon
	#if HIERARCHY_TYPE == TYPE_BVH 
	newLeaf.min.e[0] = FLT_MAX;
	newLeaf.min.e[1] = FLT_MAX;
	newLeaf.min.e[2] = FLT_MAX;
	newLeaf.max.e[0] = -FLT_MAX;
	newLeaf.max.e[1] = -FLT_MAX;
	newLeaf.max.e[2] = -FLT_MAX;
	updateMinBB(newLeaf.min, voxelMins[curIndex]);
	updateMaxBB(newLeaf.max, voxelMaxs[curIndex]);
	#endif
			if (debug1)
				cout << " ----> make leaf " << i << " (" << count << " tris)" << endl;

			// write final node information to file:
			long tempPos = ftell(nodeFP);
			fseek(nodeFP, thisChildFileOffset, SEEK_SET);
			fwrite(&newLeaf, sizeof(BSPArrayTreeNode), 1, nodeFP);
			fseek(nodeFP, tempPos, SEEK_SET);

			// taejoon
			if(count > 0)
			{
			// write index vector to file:
			fwrite(& (*(newlists[i]))[0], sizeof(int), count, indexFP);
			}

			// statistical information:
			treeStats.numLeafs++;
			treeStats.sumDepth += depth;
			treeStats.sumTris  += count;
			if (depth > treeStats.maxLeafDepth)
				treeStats.maxLeafDepth = depth;
			if (count > treeStats.maxTriCountPerLeaf)
				treeStats.maxTriCountPerLeaf = count;
		}
	}

	return true;
}
#endif



bool VoxelBVH::saveToFile(const char* filename) {	
	LogManager *log = LogManager::getSingletonPtr();	
	char output[255], idxName[MAX_PATH], nodeName[MAX_PATH];	

	sprintf(output, "Saving BSP tree to file '%s'...", filename);
	log->logMessage(LOG_INFO, output);
	
	// open files:
	sprintf(idxName, "%s.idx", filename);
	sprintf(nodeName, "%s.node", filename);	

	HANDLE fp     = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE fpIdx  = CreateFile(idxName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE fpNode = CreateFile(nodeName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	/*
	FILE *fp = fopen(filename, "wb");			// information/header file
	FILE *fpIdx = fopen(idxName, "wb+");		// triangle index file
	FILE *fpNode = fopen(nodeName, "wb+");		// node file
	*/
	
	if (fp == NULL) {
		sprintf(output, "Could not open BSP tree file '%s'!", filename);
		log->logMessage(LOG_ERROR, output);
		return false;
	}

	if (fpIdx == NULL) {
		sprintf(output, "Could not open BSP tree file '%s'!", idxName);
		log->logMessage(LOG_ERROR, output);
		return false;
	}

	if (fpNode == NULL) {
		sprintf(output, "Could not open BSP tree file '%s'!", nodeName);
		log->logMessage(LOG_ERROR, output);
		return false;
	}

	//
	// init tree stats:
	// (will be updated while inserting voxel trees)
	//

	LARGE_INTEGER startNodeOffset,startIdxOffset,temp;

	startNodeOffset.QuadPart = treeStats.numNodes * sizeof(BSPArrayTreeNode);

	startIdxOffset.QuadPart  = 0;
	temp.QuadPart = 0;

	// backup some values that will be overwritten:
	float maxDepthHL = treeStats.maxLeafDepth;
	
	treeStats.numNodes -= treeStats.numLeafs;
	treeStats.sumTris = 0;
	treeStats.numTris = 0;
	treeStats.timeBuild = 0.0f;
	treeStats.numLeafs = 0;	
	treeStats.maxTriCountPerLeaf = 0;
	treeStats.maxListLength = 0;	
	treeStats.maxLeafDepth = 0;
	
	// traverse tree and insert subtrees:
	saveNodeInArray(temp, &tree[0], fpNode, fpIdx, &treeStats, startNodeOffset, startIdxOffset, filename);

	CloseHandle(fpIdx);
	CloseHandle(fpNode);
	
	// total max depth of tree is max depth of voxel trees + max depth HL tree
	treeStats.maxLeafDepth += maxDepthHL;	
	
	DWORD written;
	//fwrite(BSP_FILEIDSTRING, 1, BSP_FILEIDSTRINGLEN, fp);
	WriteFile(fp, BSP_FILEIDSTRING, BSP_FILEIDSTRINGLEN, &written, NULL);	

	// write header and version:
	char fileVersion = BSP_FILEVERSION;
	//fputc(BSP_FILEVERSION, fp);
	WriteFile(fp, &fileVersion, 1, &written, NULL);	
	
	// write stats:
	//fwrite(&treeStats, sizeof(BSPTreeInfo), 1, fp);
	WriteFile(fp, &treeStats, sizeof(BSPTreeInfo), &written, NULL);	

	sprintf(output, "  done!");
	log->logMessage(LOG_INFO, output);
	CloseHandle(fp);

	return true;
}

void VoxelBVH::saveNodeInArray(LARGE_INTEGER myOffset, BSPArrayTreeNodePtr current, HANDLE nodePtr, HANDLE idxPtr, BSPTreeInfo *treeStats, 
								  LARGE_INTEGER &nodeOffset, LARGE_INTEGER &idxOffset, const char* filename) {
	DWORD written;
	char outStr[200];
	LogManager *log = LogManager::getSingletonPtr();
	int axisNr;
	int numTris = 0;
	BSPArrayTreeNodePtr child_left, child_right;
	
	// find axis (encoded in the lower two bits)	
	axisNr = AXIS(current);
	child_left = GETNODE(GETLEFTCHILD(current));	
	child_right = GETNODE(GETRIGHTCHILD(current));	

	if (ISLEAF(current)) { // leaf: read in voxel BVH
		unsigned int leafVoxelCount = GETCHILDCOUNT(current);

		if (leafVoxelCount > 1) {
			cerr << "ERROR: more than one voxel in this leaf!" << endl;			
		}
		else if (leafVoxelCount == 1) {
			Voxel &voxel = voxellist[*MAKEIDX_PTR(GETIDXOFFSET(current))];
			//sprintf(outStr, "MyOffset %d  Leaf, Voxel %d, %d Tris, nodeOffset:%d", myOffset, voxel.index, voxel.numTris, nodeOffset);
			//log->logMessage(outStr);
			
			// jump to next free position in file:
			SetFilePointer(nodePtr, nodeOffset.LowPart, &nodeOffset.HighPart, FILE_BEGIN);
			
			BSPArrayTreeNode newRoot = writeVoxelBVH(voxel, nodePtr, idxPtr, nodeOffset, idxOffset, filename);
			
			// jump back to own offset
			SetFilePointer(nodePtr, myOffset.LowPart, &myOffset.HighPart, FILE_BEGIN);

			// write:
			WriteFile(nodePtr, &newRoot, sizeof(BSPArrayTreeNode), &written, NULL);	
		}
		else { // empty leaf node:

			BSPArrayTreeNode newEmptyLeaf;
			#ifdef _USE_ONE_TRI_PER_LEAF
			newEmptyLeaf.triIndex = 3;
			#else
			newEmptyLeaf.indexCount = MAKECHILDCOUNT(0);
			newEmptyLeaf.indexOffset = 0;
			#endif

			treeStats->numNodes += 1;		
			treeStats->numLeafs += 1;

			// jump back to own offset
			SetFilePointer(nodePtr, myOffset.LowPart, &myOffset.HighPart, FILE_BEGIN);

			// write:
			WriteFile(nodePtr, &newEmptyLeaf, sizeof(BSPArrayTreeNode), &written, NULL);	
		}
	}
	else { // inner node: write node to file, then recurse				
		// jump to own offset
		SetFilePointer(nodePtr, myOffset.LowPart, &myOffset.HighPart, FILE_BEGIN);

		// write:		
		WriteFile(nodePtr, current, sizeof(BSPArrayTreeNode), &written, NULL);	
				
		// recurse:
		LARGE_INTEGER childOffset;

		#ifdef FOUR_BYTE_FOR_BV_NODE
		childOffset.QuadPart = GETLEFTCHILD(current);
		childOffset.QuadPart *= 16;
		saveNodeInArray(childOffset, child_left, nodePtr, idxPtr, treeStats, nodeOffset, idxOffset, filename);		
		childOffset.QuadPart = GETRIGHTCHILD(current);
		childOffset.QuadPart *= 16;;
		saveNodeInArray(childOffset, child_right, nodePtr, idxPtr, treeStats, nodeOffset, idxOffset, filename);
		#else
		childOffset.QuadPart = GETLEFTCHILD(current);
		saveNodeInArray(childOffset, child_left, nodePtr, idxPtr, treeStats, nodeOffset, idxOffset, filename);		
		childOffset.QuadPart = GETRIGHTCHILD(current);
		saveNodeInArray(childOffset, child_right, nodePtr, idxPtr, treeStats, nodeOffset, idxOffset, filename);
		#endif
	}

}

BSPArrayTreeNode VoxelBVH::writeVoxelBVH(Voxel &voxel, HANDLE nodePtr, HANDLE idxPtr, LARGE_INTEGER &nodeOffset, LARGE_INTEGER &idxOffset, const char* filename) {	
	DWORD written;
	BSPTreeInfo subTreeStats;
	BSPArrayTreeNode subRoot;
	LogManager *log = LogManager::getSingletonPtr();
	size_t ret;
	char output[400];
	char header[200];
	char bspfilestring[50];
	std::string treeFileName = std::string(filename) + "_" + toString(voxel.index,5,'0') + ".ooc";

	#ifdef FOUR_BYTE_FOR_BV_NODE
	subRoot.children2 = 3;
	#else
	#ifndef _USE_ONE_TRI_PER_LEAF
	subRoot.children = 3;
	#endif
	#endif

	#ifdef BVHNODE_16BYTES
		#ifdef FOUR_BYTE_FOR_BV_NODE
		//subRoot.children2 = 0;
		#else
		subRoot.children2 = 0;
		#endif

	#endif

	cout << " > including voxel tree " << treeFileName << endl;

	FILE *fp = fopen(treeFileName.c_str(), "rb");

	if (fp == NULL) {
		sprintf(output, "Could not open BSP tree file '%s'!", treeFileName.c_str());
		log->logMessage(LOG_WARNING, output);
		return subRoot;
	}

	//sprintf(output, "Loading BSP tree from file '%s'...", treeFileName.c_str());
	//log->logMessage(LOG_INFO, output);

	ret = fread(header, 1, BSP_FILEIDSTRINGLEN + 1, fp);
	if (ret != (BSP_FILEIDSTRINGLEN + 1)) {
		sprintf(output, "Could not read header from BSP tree file '%s', aborting. (empty file?)", treeFileName.c_str());
		log->logMessage(LOG_ERROR, output);
		return subRoot;
	}

	// test header format:
	strcpy(bspfilestring, BSP_FILEIDSTRING);
	for (unsigned int i = 0; i < BSP_FILEIDSTRINGLEN; i++) {
		if (header[i] != bspfilestring[i]) {
			sprintf(output, "Invalid BSP tree header, aborting. (expected:'%c', found:'%c')", bspfilestring[i], header[i]);
			log->logMessage(LOG_ERROR, output);
			return subRoot;		
		}
	}

	// test file version:
	if (header[BSP_FILEIDSTRINGLEN] != BSP_FILEVERSION) {
		sprintf(output, "Wrong BSP tree file version (expected:%d, found:%d)", BSP_FILEVERSION, header[BSP_FILEIDSTRINGLEN]);
		log->logMessage(LOG_ERROR, output);
		return subRoot;		
	}

	// format correct, read in full BSP tree info structure:

	// read count of nodes and tri indices:
	ret = fread(&subTreeStats, sizeof(BSPTreeInfo), 1, fp);
	if (ret != 1) {
		sprintf(output, "Could not read tree info header!");
		log->logMessage(LOG_ERROR, output);
		return subRoot;
	}

	sprintf(output, "Allocating memory...");
	//log->logMessage(LOG_INFO, output);

	BSPArrayTreeNode *subTree = new BSPArrayTreeNode[subTreeStats.numNodes];	
	unsigned int *subIndices = new unsigned int[subTreeStats.sumTris];

	// read tree node array:
	sprintf(output, "  ... reading %d tree nodes ...", subTreeStats.numNodes);
	//log->logMessage(LOG_INFO, output);
	ret = fread(subTree, sizeof(BSPArrayTreeNode), subTreeStats.numNodes, fp);

	if (ret != subTreeStats.numNodes) {
		sprintf(output, "Could only read %u nodes, expecting %u!", ret, subTreeStats.numNodes);
		log->logMessage(LOG_ERROR, output);
		return subRoot;
	}

	// read tri index array
	sprintf(output, "  ... reading %d tri indices ...", subTreeStats.sumTris);
	//log->logMessage(LOG_INFO, output);
	ret = fread(subIndices, sizeof(unsigned int), subTreeStats.sumTris, fp);

	if (ret != subTreeStats.sumTris) {
		sprintf(output, "Could only read %u indices, expecting %u!", ret, subTreeStats.sumTris);
		log->logMessage(LOG_ERROR, output);
		return subRoot;
	}

	fclose(fp);
	// reading done.

	
	// apply offsets to nodes:	
	for (unsigned int j = 0; j < subTreeStats.numNodes; j++) {	
		if (ISLEAF(&subTree[j])) {
			// !@#$
			#ifndef _USE_ONE_TRI_PER_LEAF
			subTree[j].indexOffset += idxOffset.QuadPart;
			#else
			subTree[j].triIndex += (idxOffset.QuadPart << 2);
			#endif
			//unsigned int count = MAKECHILDCOUNT(subTree[j].indexCount);
			//if (count == 0)
			//	subTree[j].children = 3;
			//else
			//    subTree[j].children += idxOffset;
			//cout << "DEBUG: > node " << j << " is leaf, " << (unsigned int)subTree[j].splitcoord << ", idx " << subTree[j].children << endl; 
		}
		else {
			#ifdef FOUR_BYTE_FOR_BV_NODE
				subTree[j].children += (nodeOffset.QuadPart - sizeof(BSPArrayTreeNode)) >> 4;
			#else
				subTree[j].children += (nodeOffset.QuadPart - sizeof(BSPArrayTreeNode)) >> 3;

				#ifdef BVHNODE_16BYTES
				#ifndef _USE_CONTI_NODE
				subTree[j].children2 += (nodeOffset.QuadPart - sizeof(BSPArrayTreeNode)) >> 3;
				#else
				subTree[j].children2 = 0;
				#endif
				#endif

			#endif
			//cout << "DEBUG: > node " << j << " is inner, at " << subTree[j].splitcoord << ", off " << subTree[j].children << endl;
		}


	}

	// increment offsets (nodeOffset - 1 because the root node is already
	// in the high-level BVH and will just be replaced):
	idxOffset.QuadPart  += subTreeStats.sumTris; 
	nodeOffset.QuadPart += (subTreeStats.numNodes -1) * sizeof(BSPArrayTreeNode);	
	
	// write tree to files (excluding first node!)
	WriteFile(nodePtr, &subTree[1], sizeof(BSPArrayTreeNode)*(subTreeStats.numNodes-1), &written, NULL);		
	WriteFile(idxPtr, subIndices, sizeof(unsigned int)*subTreeStats.sumTris, &written, NULL);	
	
	// save root node of tree
	subRoot = subTree[0];

	// then free memory
	delete subTree;
	delete subIndices;	

	// update tree stats:
	treeStats.numNodes += subTreeStats.numNodes;
	treeStats.sumTris += subTreeStats.sumTris;
	treeStats.numTris += subTreeStats.numTris;
	treeStats.timeBuild += subTreeStats.timeBuild;
	treeStats.numLeafs += subTreeStats.numLeafs;
	treeStats.sumDepth += subTreeStats.sumDepth;

	treeStats.maxLeafDepth = max(subTreeStats.maxLeafDepth, treeStats.maxLeafDepth);
	treeStats.maxListLength = max(subTreeStats.maxListLength, treeStats.maxListLength);
	treeStats.maxTriCountPerLeaf = max(subTreeStats.maxTriCountPerLeaf, treeStats.maxTriCountPerLeaf);

	return subRoot;
}

void VoxelBVH::printNodeInArray(const char *LoggerName, BSPArrayTreeNodePtr current, int depth) {

	LogManager *log = LogManager::getSingletonPtr();
	char outStr[500];
	char indent[100];
	char *axis = "XYZL";
	int axisNr;
	int numTris = 0;
	BSPArrayTreeNodePtr child_left, child_right;

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
	else if (depth == 0) {
		if (tree == NULL)
			return;
		current = &tree[0];		
	}

	// find axis (encoded in the lower two bits)	
	axisNr = AXIS(current);
	child_left = GETNODE(GETLEFTCHILD(current));
	child_right = GETNODE(GETRIGHTCHILD(current));

	if (ISLEAF(current)) { // leaf		
		numTris = GETCHILDCOUNT(current);
		sprintf(outStr, "%sLeaf %d Tris", indent, numTris);
		log->logMessage(outStr, LoggerName);		
	}
	else {
		sprintf(outStr, "%sNode %c Child-Offset %u/%u", indent, axis[axisNr], GETLEFTCHILD(current), GETRIGHTCHILD(current));
		log->logMessage(outStr, LoggerName);
	}	

	if (ISNOLEAF(current)) {
		printNodeInArray(LoggerName, child_left, depth + 1);		
		printNodeInArray(LoggerName, child_right, depth + 1);
	}

}

void VoxelBVH::printTree(bool dumpTree, const char *LoggerName) {
	LogManager *log = LogManager::getSingletonPtr();
	char outputBuffer[2000];
	log->logMessage("-------------------------------------------", LoggerName);
	log->logMessage("BSP Tree Statistics", LoggerName);
	log->logMessage("-------------------------------------------", LoggerName);
	sprintf(outputBuffer, "Time to build:\t%d seconds, %d milliseconds", (int)treeStats.timeBuild, (int)((treeStats.timeBuild - floor(treeStats.timeBuild)) * 1000));
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Triangles:\t%d", treeStats.numTris);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Nodes:\t\t%d", treeStats.numNodes);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Leafs:\t\t%d", treeStats.numLeafs);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Max. leaf depth:\t%d (of %d)", treeStats.maxLeafDepth, treeStats.maxDepth);
	log->logMessage(outputBuffer, LoggerName);
	sprintf(outputBuffer, "Max. tri count/leaf:\t%d", treeStats.maxTriCountPerLeaf);
	log->logMessage(outputBuffer, LoggerName);
	if (treeStats.numLeafs > 0) {
		sprintf(outputBuffer, "Avg. leaf depth:\t%.2f", (float)treeStats.sumDepth / (float)treeStats.numLeafs);
		log->logMessage(outputBuffer, LoggerName);

		sprintf(outputBuffer, "Avg. tris/leaf:\t%.2f", (float)treeStats.sumTris / (float)treeStats.numLeafs);
		log->logMessage(outputBuffer, LoggerName);

		sprintf(outputBuffer, "Tri refs total:\t\t%d", treeStats.sumTris);
		log->logMessage(outputBuffer, LoggerName);

	}
	sprintf(outputBuffer, "Used memory:\t%d KB", (treeStats.numNodes*sizeof(BSPArrayTreeNode) + (treeStats.sumTris * sizeof(int))) / 1024);
	log->logMessage(outputBuffer, LoggerName);

	if (dumpTree) {
		log->logMessage("-------------------------------------------", LoggerName);
		log->logMessage("BSP Tree structure", LoggerName);
		log->logMessage("-------------------------------------------", LoggerName);		
		printNodeInArray(LoggerName, NULL, 0);
		log->logMessage("-------------------------------------------", LoggerName);		
	}
}
int VoxelBVH::getNumTris() {
	return treeStats.numTris;
}

#endif