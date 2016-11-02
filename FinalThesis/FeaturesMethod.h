﻿#pragma once
#include "Method.h"
#include "MyFeature2DFactory.h"

using namespace std;

/**
	Base class for algorithms using feature tracking approach
*/
class FeaturesMethod : public Method
{
protected:
	const int MIN_NUM_OF_FEATURES = 9;
	Mat mPrevFrame; /**< Previous frame */
	Ptr<MyFeature2D> mDetector; /**< Pointer to detector/descriptor object */
	vector<KeyPoint> mPrevKeypoints; /**< Previously detected keypoints */
	bool mRANSAC;

	struct
	{
		
	};

	/**
	Find rigid transformation matrix for the next frame
	@param frame		next frame
	@return				transformation matrix
	*/
	virtual Mat getTransform(const Mat& frame) = 0;

	/**
	Calculate matrix of rigid transformation (translation, rotation and scale)
	@param before			points from descriptor
	@param after			resulting points from tracker
	@param RTMatrix			output container for transformation matrix
	*/
	static void getRTMatrix(const vector<Point2f>& before, const vector<Point2f>& after, Mat& RTMatrix)
	{
		CV_Assert(RTMatrix.isContinuous());
		auto N = before.size();
		CV_Assert(N > 1 && N == after.size());

		double pA[4][4] = { { 0. } }, pB[4] = { 0. }, pMM[4] = { 0. };
		Mat A(4, 4, CV_64F, pA), B(4, 1, CV_64F, pB);
		Mat MM(4, 1, CV_64F, pMM);

		//least squares 
		for (auto i = 0; i < N; ++i)
		{
			pA[0][0] += before[i].ddot(before[i]);
			pA[0][2] += before[i].x;
			pA[0][3] += before[i].y;

			pB[0] += before[i].ddot(after[i]);
			pB[1] += before[i].cross(after[i]);
			pB[2] += after[i].x;
			pB[3] += after[i].y;
		}

		pA[1][1] = pA[0][0];
		pA[2][1] = pA[1][2] = -pA[0][3];
		pA[3][1] = pA[1][3] = pA[2][0] = pA[0][2];
		pA[2][2] = pA[3][3] = N;
		pA[3][0] = pA[0][3];

		solve(A, B, MM, DECOMP_EIG);

		double* om = RTMatrix.ptr<double>();
		om[0] = om[4] = pMM[0];
		om[1] = -pMM[1];
		om[3] = pMM[1];
		om[2] = pMM[2];
		om[5] = pMM[3];
	}

	/**
	Random sample consensus algorithm to discard wrongly tracked points
	@param pA			points from descriptor
	@param pB			resulting points from tracker
	@param good_ratio	minimum ratio of good points in set to accept result
	@return				true - if success / false - if fail
	*/
	static bool RANSAC(vector<Point2f>& pA, vector<Point2f>& pB, double good_ratio)
	{
		const int RANSAC_MAX_ITERS = 300;
		const int RANSAC_SIZE0 = 3;

		RNG rng(static_cast<uint64>(-1));

		int i, j, k, k1;
		int good_count = 0;
		int count = pA.size();
		vector<int> good_idx(count);

		//don't filter if number of points too low
		if (count < RANSAC_SIZE0)
			return false;

		//bounding rectangle for all tracked points
		Rect brect = boundingRect(pB);

		// RANSAC stuff:
		for (k = 0; k < RANSAC_MAX_ITERS; k++)
		{
			int idx[RANSAC_SIZE0];
			vector<Point2f> a(RANSAC_SIZE0), b(RANSAC_SIZE0);
			Mat M(2, 3, CV_64F);

			// choose random 3 non-complanar points from A & B
			for (i = 0; i < RANSAC_SIZE0; i++)
			{
				for (k1 = 0; k1 < RANSAC_MAX_ITERS; k1++)
				{
					//draw index from feature set
					idx[i] = rng.uniform(0, count);

					//repeat if the same index has been drawn or points behind indexes are too close
					for (j = 0; j < i; j++)
					{
						if (idx[j] == idx[i])
							break;
						if (fabs(pA[idx[i]].x - pA[idx[j]].x) +
							fabs(pA[idx[i]].y - pA[idx[j]].y) < 2 * FLT_EPSILON)
							break;
						if (fabs(pB[idx[i]].x - pB[idx[j]].x) +
							fabs(pB[idx[i]].y - pB[idx[j]].y) < 2 * FLT_EPSILON)
							break;
					}
					if (j < i) continue;

					//finish if enough number of points drawn
					if (i + 1 == RANSAC_SIZE0)
					{
						// additional check for non-coplanar vectors
						a[0] = pA[idx[0]];
						a[1] = pA[idx[1]];
						a[2] = pA[idx[2]];

						b[0] = pB[idx[0]];
						b[1] = pB[idx[1]];
						b[2] = pB[idx[2]];

						double dax1 = a[1].x - a[0].x, day1 = a[1].y - a[0].y;
						double dax2 = a[2].x - a[0].x, day2 = a[2].y - a[0].y;
						double dbx1 = b[1].x - b[0].x, dby1 = b[1].y - b[0].y;
						double dbx2 = b[2].x - b[0].x, dby2 = b[2].y - b[0].y;
						const double eps = 0.01;

						if (fabs(dax1*day2 - day1*dax2) < eps*sqrt(dax1*dax1 + day1*day1)*sqrt(dax2*dax2 + day2*day2) ||
							fabs(dbx1*dby2 - dby1*dbx2) < eps*sqrt(dbx1*dbx1 + dby1*dby1)*sqrt(dbx2*dbx2 + dby2*dby2))
							continue;
					}
					break;
				}

				//if cannot find 3 non-complanar points in iterations limit
				if (k1 >= RANSAC_MAX_ITERS)
					break;
			}

			//if not enough points have met conditions
			if (i < RANSAC_SIZE0)
				continue;

			// estimate the rigid transformation using drawn points
			getRTMatrix(a, b, M);

			//calculate how many points accurately follow transformation 
			const double* m = M.ptr<double>();
			for (i = 0, good_count = 0; i < count; i++)
			{
				if (abs(m[0] * pA[i].x + m[1] * pA[i].y + m[2] - pB[i].x) +
					abs(m[3] * pA[i].x + m[4] * pA[i].y + m[5] - pB[i].y) < max(brect.width, brect.height)*0.05)
					good_idx[good_count++] = i;
			}

			if (good_count >= count*good_ratio)
				break;
		}

		//if too many iterations
		if (k >= RANSAC_MAX_ITERS)
			return false;

		//leave only good points in vectors
		if (good_count < count)
		{
			for (i = 0; i < good_count; i++)
			{
				j = good_idx[i];
				pA[i] = pA[j];
				pB[i] = pB[j];
			}
		}
		pA.resize(good_count);
		pB.resize(good_count);

		return true;
	}

public:

	FeaturesMethod(const String& name, const Mat& first, int detector, int maxFeatures, bool RANSAC) : Method(name), mPrevFrame(first), mRANSAC(RANSAC)
	{
		Mat mask = MyFeature2D::createMask(first, 0.1f);
		mDetector = MyFeature2DFactory::create(detector, maxFeatures, first, mask);

		addToName(mDetector->getName());
		if (RANSAC) addToName("RANSAC");
	}
};
