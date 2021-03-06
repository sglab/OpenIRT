#ifndef VOXEL_H
#define VOXEL_H

#include "Vector3.h"
#include "BV.h"

typedef struct OctreeHeader_t
{
	int dim;
	int maxDepth;
	Vector3 min;
	Vector3 max;
} OctreeHeader;

#ifndef min
#define min(a, b)  (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)  (((a) > (b)) ? (a) : (b))
#endif

class Voxel
{
public:
	class VoxelMat
	{
	public:
		unsigned int mat_Kd;
		unsigned int mat_Ks;
		unsigned short mat_d;
		unsigned short mat_Ns;

		VoxelMat() {clear();}
		VoxelMat(const Vector3 &kd, const Vector3 &ks, const float d, const float Ns)
		{
			setKd(kd); setKs(ks); setD(d); setNs(Ns);
		}

		void setKd(const Vector3 &kd)
		{
			unsigned char r = (unsigned char)min(255, (int)(kd.e[0] * 255.0f));
			unsigned char g = (unsigned char)min(255, (int)(kd.e[1] * 255.0f));
			unsigned char b = (unsigned char)min(255, (int)(kd.e[2] * 255.0f));
			mat_Kd = (r | (((unsigned int)g) << 8)) | (((unsigned int)b) << 16);
		}
		void setKs(const Vector3 &ks)
		{
			unsigned char r = (unsigned char)min(255, (int)(ks.e[0] * 255.0f));
			unsigned char g = (unsigned char)min(255, (int)(ks.e[1] * 255.0f));
			unsigned char b = (unsigned char)min(255, (int)(ks.e[2] * 255.0f));
			mat_Ks = (r | (((unsigned int)g) << 8)) | (((unsigned int)b) << 16);
		}
		void setD(const float d)
		{
			mat_d = (unsigned short)min(65535, (int)(d * 65535.0f));
		}
		void setNs(const float Ns)
		{
			mat_Ns = (unsigned short)Ns;
		}
		void clear()
		{
		}
	};

public:
	VoxelMat mat;
	/*
	int childIndex;			// 0..29 : childIndex, 30 : unused, 31 : is leaf
	int parentIndex;
	unsigned char theta, phi;
	unsigned char geomBitmap[8];
	unsigned short d;
	*/
	int childIndex;			// 0..29 : childIndex, 30 : unused, 31 : is leaf
	unsigned char theta, phi;
	unsigned char geomBitmap[8];
	unsigned short m;
	float d;

	static float s_minD, s_unitD;
public:
	//Vector3 corners[4];

	bool hasChild() const {return (childIndex >> 2) != 0;}
	bool isLeaf() const {return (childIndex & 0x1) == 0x1;}
	bool isEmpty() const {return !hasChild() && !isLeaf();}

	int getChildIndex() const {return childIndex >> 2;}
	//void setChildIndex(int index) {childIndex = (index << 2) | (childIndex & 0x3);}
	//void setLeaf() {childIndex |= 0x1;}
	void setChildIndex(int index) {childIndex = index << 2;}
	void setLeaf() {childIndex = 0x1;}

	Vector3 getNorm() const 
	{
		float fTheta = theta / 256.0f * PI, fPhi = phi / 256.0f * PI;

		Vector3 n;
		n.e[0] = sin(fTheta)*cosf(2.0f*fPhi);
		n.e[1] = sin(fTheta)*sinf(2.0f*fPhi);
		n.e[2] = cosf(fTheta);

		return n;
	}
	
	float getD() const 
	{
		return d;
		//return s_minD + (d * s_unitD);
	}
	
	void setNorm(const Vector3 &n)
	{
		theta = (unsigned char)(acosf(n.e[2]) * 256.0f / PI);
		float temp = atan2f(n.e[1], n.e[0]);
		// change range: (-pi, pi] -> [0, 2pi)
		temp = temp < 0 ? -temp : temp;
		phi = (unsigned char)(temp * 256.0f / (2*PI));
	}
	
	void setD(const float d) 
	{
		this->d = d;
		//this->d = (unsigned short)((d - s_minD) / s_unitD);
	}

	void setMat(const VoxelMat &mat) {this->mat = mat;}
	void setMat(const Vector3 &kd, const Vector3 &ks, const float d, const float Ns) {mat = VoxelMat(kd, ks, d, Ns);}

	void clear() {mat.clear(); childIndex = 0;}

	static void initD(const AABB &bb)
	{
		float len = (bb.max - bb.min).length();
		s_minD = (0.5f*(bb.min + bb.max)).length() - (len * 0.5f);
		s_unitD = len / 0xFFFF;
	}
};

#endif // VOXEL_H