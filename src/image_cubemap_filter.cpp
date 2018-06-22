/*
 * Copyright 2011-2018 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bimg#license-bsd-2-clause
 */

#include "bimg_p.h"

namespace bimg
{
	/*
	 * Copyright 2014-2015 Dario Manesku. All rights reserved.
	 * License: http://www.opensource.org/licenses/BSD-2-Clause
	 */

	//                  +----------+
	//                  |-z       2|
	//                  | ^  +y    |
	//                  | |        |
	//                  | +---->+x |
	//       +----------+----------+----------+----------+
	//       |+y       1|+y       4|+y       0|+y       5|
	//       | ^  -x    | ^  +z    | ^  +x    | ^  -z    |
	//       | |        | |        | |        | |        |
	//       | +---->+z | +---->+x | +---->-z | +---->-x |
	//       +----------+----------+----------+----------+
	//                  |+z       3|
	//                  | ^  -y    |
	//                  | |        |
	//                  | +---->+x |
	//                  +----------+
	//
	struct CubeMapFace
	{
		enum Enum
		{
			PositiveX,
			NegativeX,
			PositiveY,
			NegativeY,
			PositiveZ,
			NegativeZ,

			Count
		};

		struct Edge
		{
			enum Enum
			{
				Left,
				Right,
				Top,
				Bottom,

				Count
			};
		};

		//    --> U    _____
		//   |        |     |
		//   v        | +Y  |
		//   V   _____|_____|_____ _____
		//      |     |     |     |     |
		//      | -X  | +Z  | +X  | -Z  |
		//      |_____|_____|_____|_____|
		//            |     |
		//            | -Y  |
		//            |_____|
		//
		// Neighbour faces in order: left, right, top, bottom.
		// FaceEdge is the edge that belongs to the neighbour face.
		struct Neighbour
		{
			uint8_t m_faceIdx;
			uint8_t m_faceEdge;
		};

		float uv[3][3];
	};

	static const CubeMapFace s_cubeMapFace[] =
	{
		{{ // +x face
			{  0.0f,  0.0f, -1.0f }, // u -> -z
			{  0.0f, -1.0f,  0.0f }, // v -> -y
			{  1.0f,  0.0f,  0.0f }, // +x face
		}},
		{{ // -x face
			{  0.0f,  0.0f,  1.0f }, // u -> +z
			{  0.0f, -1.0f,  0.0f }, // v -> -y
			{ -1.0f,  0.0f,  0.0f }, // -x face
		}},
		{{ // +y face
			{  1.0f,  0.0f,  0.0f }, // u -> +x
			{  0.0f,  0.0f,  1.0f }, // v -> +z
			{  0.0f,  1.0f,  0.0f }, // +y face
		}},
		{{ // -y face
			{  1.0f,  0.0f,  0.0f }, // u -> +x
			{  0.0f,  0.0f, -1.0f }, // v -> -z
			{  0.0f, -1.0f,  0.0f }, // -y face
		}},
		{{ // +z face
			{  1.0f,  0.0f,  0.0f }, // u -> +x
			{  0.0f, -1.0f,  0.0f }, // v -> -y
			{  0.0f,  0.0f,  1.0f }, // +z face
		}},
		{{ // -z face
			{ -1.0f,  0.0f,  0.0f }, // u -> -x
			{  0.0f, -1.0f,  0.0f }, // v -> -y
			{  0.0f,  0.0f, -1.0f }, // -z face
		}},
	};

	static const CubeMapFace::Neighbour s_cubeMapFaceNeighbours[6][4] =
	{
		{ // +X
			{ CubeMapFace::PositiveZ, CubeMapFace::Edge::Right  },
			{ CubeMapFace::NegativeZ, CubeMapFace::Edge::Left   },
			{ CubeMapFace::PositiveY, CubeMapFace::Edge::Right  },
			{ CubeMapFace::NegativeY, CubeMapFace::Edge::Right  },
		},
		{ // -X
			{ CubeMapFace::NegativeZ, CubeMapFace::Edge::Right  },
			{ CubeMapFace::PositiveZ, CubeMapFace::Edge::Left   },
			{ CubeMapFace::PositiveY, CubeMapFace::Edge::Left   },
			{ CubeMapFace::NegativeY, CubeMapFace::Edge::Left   },
		},
		{ // +Y
			{ CubeMapFace::NegativeX, CubeMapFace::Edge::Top    },
			{ CubeMapFace::PositiveX, CubeMapFace::Edge::Top    },
			{ CubeMapFace::NegativeZ, CubeMapFace::Edge::Top    },
			{ CubeMapFace::PositiveZ, CubeMapFace::Edge::Top    },
		},
		{ // -Y
			{ CubeMapFace::NegativeX, CubeMapFace::Edge::Bottom },
			{ CubeMapFace::PositiveX, CubeMapFace::Edge::Bottom },
			{ CubeMapFace::PositiveZ, CubeMapFace::Edge::Bottom },
			{ CubeMapFace::NegativeZ, CubeMapFace::Edge::Bottom },
		},
		{ // +Z
			{ CubeMapFace::NegativeX, CubeMapFace::Edge::Right  },
			{ CubeMapFace::PositiveX, CubeMapFace::Edge::Left   },
			{ CubeMapFace::PositiveY, CubeMapFace::Edge::Bottom },
			{ CubeMapFace::NegativeY, CubeMapFace::Edge::Top    },
		},
		{ // -Z
			{ CubeMapFace::PositiveX, CubeMapFace::Edge::Right  },
			{ CubeMapFace::NegativeX, CubeMapFace::Edge::Left   },
			{ CubeMapFace::PositiveY, CubeMapFace::Edge::Top    },
			{ CubeMapFace::NegativeY, CubeMapFace::Edge::Bottom },
		},
	};

	/// _u and _v should be center addressing and in [-1.0+invSize..1.0-invSize] range.
	void texelUvToDir(float* _outDir, uint8_t _side, float _u, float _v)
	{
		const CubeMapFace& face = s_cubeMapFace[_side];

		float tmp[3];
		tmp[0] = face.uv[0][0] * _u + face.uv[1][0] * _v + face.uv[2][0];
		tmp[1] = face.uv[0][1] * _u + face.uv[1][1] * _v + face.uv[2][1];
		tmp[2] = face.uv[0][2] * _u + face.uv[1][2] * _v + face.uv[2][2];
		bx::vec3Norm(_outDir, tmp);
	}

	void dirToTexelUv(float& _outU, float& _outV, uint8_t& _outSide, const float* _dir)
	{
		float absVec[3];
		bx::vec3Abs(absVec, _dir);

		const float max = bx::max(absVec[0], absVec[1], absVec[2]);

		if (max == absVec[0])
		{
			_outSide = (_dir[0] >= 0.0f) ? uint8_t(CubeMapFace::PositiveX) : uint8_t(CubeMapFace::NegativeX);
		}
		else if (max == absVec[1])
		{
			_outSide = (_dir[1] >= 0.0f) ? uint8_t(CubeMapFace::PositiveY) : uint8_t(CubeMapFace::NegativeY);
		}
		else
		{
			_outSide = (_dir[2] >= 0.0f) ? uint8_t(CubeMapFace::PositiveZ) : uint8_t(CubeMapFace::NegativeZ);
		}

		float faceVec[3];
		bx::vec3Mul(faceVec, _dir, 1.0f/max);

		_outU = (bx::vec3Dot(s_cubeMapFace[_outSide].uv[0], faceVec) + 1.0f) * 0.5f;
		_outV = (bx::vec3Dot(s_cubeMapFace[_outSide].uv[1], faceVec) + 1.0f) * 0.5f;
	}

	ImageContainer* imageCubemapFromLatLongRgba32F(bx::AllocatorI* _allocator, const ImageContainer& _input, bool _useBilinearInterpolation, bx::Error* _err)
	{
		BX_ERROR_SCOPE(_err);

		if (_input.m_depth     != 1
		&&  _input.m_numLayers != 1
		&&  _input.m_format    != TextureFormat::RGBA32F
		&&  _input.m_width/2   != _input.m_height)
		{
			BX_ERROR_SET(_err, BIMG_ERROR, "Input image format is not equirectangular projection.");
			return NULL;
		}

		const uint32_t srcWidthMinusOne  = _input.m_width-1;
		const uint32_t srcHeightMinusOne = _input.m_height-1;
		const uint32_t srcPitch = _input.m_width*16;
		const uint32_t dstWidth = _input.m_height/2;
		const uint32_t dstPitch = dstWidth*16;
		const float invDstWidth = 1.0f / float(dstWidth);

		ImageContainer* output = imageAlloc(_allocator
			, _input.m_format
			, uint16_t(dstWidth)
			, uint16_t(dstWidth)
			, uint16_t(1)
			, 1
			, true
			, false
			);

		const uint8_t* srcData = (const uint8_t*)_input.m_data;

		for (uint8_t side = 0; side < 6 && _err->isOk(); ++side)
		{
			ImageMip mip;
			imageGetRawData(*output, side, 0, output->m_data, output->m_size, mip);

			for (uint32_t yy = 0; yy < dstWidth; ++yy)
			{
				for (uint32_t xx = 0; xx < dstWidth; ++xx)
				{
					float* dstData = (float*)&mip.m_data[yy*dstPitch+xx*16];

					const float uu = 2.0f*xx*invDstWidth - 1.0f;
					const float vv = 2.0f*yy*invDstWidth - 1.0f;

					float dir[3];
					texelUvToDir(dir, side, uu, vv);

					float srcU, srcV;
					bx::vec3ToLatLong(&srcU, &srcV, dir);

					srcU *= srcWidthMinusOne;
					srcV *= srcHeightMinusOne;

					if (_useBilinearInterpolation)
					{
						const uint32_t x0 = uint32_t(srcU);
						const uint32_t y0 = uint32_t(srcV);
						const uint32_t x1 = bx::min(x0 + 1, srcWidthMinusOne);
						const uint32_t y1 = bx::min(y0 + 1, srcHeightMinusOne);

						const float* src0 = (const float*)&srcData[y0*srcPitch + x0*16];
						const float* src1 = (const float*)&srcData[y0*srcPitch + x1*16];
						const float* src2 = (const float*)&srcData[y1*srcPitch + x0*16];
						const float* src3 = (const float*)&srcData[y1*srcPitch + x1*16];

						const float tx   = srcU - float(int32_t(x0) );
						const float ty   = srcV - float(int32_t(y0) );
						const float omtx = 1.0f - tx;
						const float omty = 1.0f - ty;

						float p0[4];
						bx::vec4Mul(p0, src0, omtx*omty);

						float p1[4];
						bx::vec4Mul(p1, src1, tx*omty);

						float p2[4];
						bx::vec4Mul(p2, src2, omtx*ty);

						float p3[4];
						bx::vec4Mul(p3, src3, tx*ty);

						const float rr = p0[0] + p1[0] + p2[0] + p3[0];
						const float gg = p0[1] + p1[1] + p2[1] + p3[1];
						const float bb = p0[2] + p1[2] + p2[2] + p3[2];
						const float aa = p0[3] + p1[3] + p2[3] + p3[3];

						dstData[0] = rr;
						dstData[1] = gg;
						dstData[2] = bb;
						dstData[3] = aa;
					}
					else
					{
						const uint32_t x0 = uint32_t(srcU);
						const uint32_t y0 = uint32_t(srcV);
						const float* src0 = (const float*)&srcData[y0*srcPitch + x0*16];

						dstData[0] = src0[0];
						dstData[1] = src0[1];
						dstData[2] = src0[2];
						dstData[3] = src0[3];
					}

				}
			}
		}

		return output;
	}

	inline float areaElement(float _x, float _y)
	{
		return bx::atan2(_x*_y, bx::sqrt(_x*_x + _y*_y + 1.0f));
	}

	float texelSolidAngle(float _u, float _v, float _invFaceSize)
	{
		/// Reference:
		///  - https://web.archive.org/web/20180614195754/http://www.mpia.de/~mathar/public/mathar20051002.pdf
		///  - https://web.archive.org/web/20180614195725/http://www.rorydriscoll.com/2012/01/15/cubemap-texel-solid-angle/

		const float x0 = _u - _invFaceSize;
		const float x1 = _u + _invFaceSize;
		const float y0 = _v - _invFaceSize;
		const float y1 = _v + _invFaceSize;

		return
			+ areaElement(x1, y1)
			- areaElement(x0, y1)
			- areaElement(x1, y0)
			+ areaElement(x0, y0)
			;
	}

	ImageContainer* imageCubemapNormalSolidAngle(bx::AllocatorI* _allocator, uint32_t _size)
	{
		const uint32_t dstWidth = _size;
		const uint32_t dstPitch = dstWidth*16;
		const float invDstWidth = 1.0f / float(dstWidth);

		ImageContainer* output = imageAlloc(_allocator, TextureFormat::RGBA32F, uint16_t(dstWidth), uint16_t(dstWidth), 1, 1, true, false);

		for (uint8_t side = 0; side < 6; ++side)
		{
			ImageMip mip;
			imageGetRawData(*output, side, 0, output->m_data, output->m_size, mip);

			for (uint32_t yy = 0; yy < dstWidth; ++yy)
			{
				for (uint32_t xx = 0; xx < dstWidth; ++xx)
				{
					float* dstData = (float*)&mip.m_data[yy*dstPitch+xx*16];

					const float uu = float(xx)*invDstWidth*2.0f - 1.0f;
					const float vv = float(yy)*invDstWidth*2.0f - 1.0f;

					texelUvToDir(dstData, side, uu, vv);
					dstData[3] = texelSolidAngle(uu, vv, invDstWidth);
				}
			}
		}

		return output;
	}

	struct Aabb
	{
		Aabb()
		{
			m_min[0] =  bx::kFloatMax;
			m_min[1] =  bx::kFloatMax;
			m_max[0] = -bx::kFloatMax;
			m_max[1] = -bx::kFloatMax;
		}

		void add(float _x, float _y)
		{
			m_min[0] = bx::min(m_min[0], _x);
			m_min[1] = bx::min(m_min[1], _y);
			m_max[0] = bx::max(m_max[0], _x);
			m_max[1] = bx::max(m_max[1], _y);
		}

		void clamp(float _min, float _max)
		{
			m_min[0] = bx::clamp(m_min[0], _min, _max);
			m_min[1] = bx::clamp(m_min[1], _min, _max);
			m_max[0] = bx::clamp(m_max[0], _min, _max);
			m_max[1] = bx::clamp(m_max[1], _min, _max);
		}

		bool isEmpty() const
		{
			// Has to have at least two points added so that no value is equal to initial state.
			return ( (m_min[0] ==  bx::kFloatMax)
				||   (m_min[1] ==  bx::kFloatMax)
				||   (m_max[0] == -bx::kFloatMax)
				||   (m_max[1] == -bx::kFloatMax)
				);
		}

		float m_min[2];
		float m_max[2];
	};

	void calcFilterArea(Aabb* _outFilterArea, const float* _dir, float _filterSize)
	{
		///   ______
		///  |      |
		///  |      |
		///  |    x |
		///  |______|
		///
		// Get face and hit coordinates.
		float uu, vv;
		uint8_t hitFaceIdx;
		dirToTexelUv(uu, vv, hitFaceIdx, _dir);

		///  ........
		///  .      .
		///  .   ___.
		///  .  | x |
		///  ...|___|
		///
		// Calculate hit face filter bounds.
		Aabb hitFaceFilterBounds;
		hitFaceFilterBounds.add(uu-_filterSize, vv-_filterSize);
		hitFaceFilterBounds.add(uu+_filterSize, vv+_filterSize);
		hitFaceFilterBounds.clamp(0.0f, 1.0f);

		// Output result for hit face.
		bx::memCopy(&_outFilterArea[hitFaceIdx], &hitFaceFilterBounds, sizeof(Aabb));

		/// Filter area might extend on neighbour faces.
		/// Case when extending over the right edge:
		///
		///  --> U
		/// |        ......
		/// v       .      .
		/// V       .      .
		///         .      .
		///  ....... ...... .......
		///  .      .      .      .
		///  .      .  .....__min .
		///  .      .  .   .  |  -> amount
		///  ....... .....x.__|....
		///         .  .   .  max
		///         .  ........
		///         .      .
		///          ......
		///         .      .
		///         .      .
		///         .      .
		///          ......
		///

		struct NeighourFaceBleed
		{
			float m_amount;
			float m_bbMin;
			float m_bbMax;
		};

		const NeighourFaceBleed bleed[CubeMapFace::Edge::Count] =
		{
			{ // Left
				_filterSize - uu,
				hitFaceFilterBounds.m_min[1],
				hitFaceFilterBounds.m_max[1],
			},
			{ // Right
				uu + _filterSize - 1.0f,
				hitFaceFilterBounds.m_min[1],
				hitFaceFilterBounds.m_max[1],
			},
			{ // Top
				_filterSize - vv,
				hitFaceFilterBounds.m_min[0],
				hitFaceFilterBounds.m_max[0],
			},
			{ // Bottom
				vv + _filterSize - 1.0f,
				hitFaceFilterBounds.m_min[0],
				hitFaceFilterBounds.m_max[0],
			},
		};

		// Determine bleeding for each side.
		for (uint8_t side = 0; side < 4; ++side)
		{
			uint8_t currentFaceIdx = hitFaceIdx;

			for (float bleedAmount = bleed[side].m_amount; bleedAmount > 0.0f; bleedAmount -= 1.0f)
			{
				uint8_t neighbourFaceIdx  = s_cubeMapFaceNeighbours[currentFaceIdx][side].m_faceIdx;
				uint8_t neighbourFaceEdge = s_cubeMapFaceNeighbours[currentFaceIdx][side].m_faceEdge;
				currentFaceIdx = neighbourFaceIdx;

				/// https://code.google.com/p/cubemapgen/source/browse/trunk/CCubeMapProcessor.cpp#773
				///
				/// Handle situations when bbMin and bbMax should be flipped.
				///
				///    L - Left           ....................T-T
				///    R - Right          v                     .
				///    T - Top        __________                .
				///    B - Bottom    .          |               .
				///                  .          |               .
				///                  .          |<...R-T        .
				///                  .          |    v          v
				///        .......... ..........|__________ __________
				///       .          .          .          .          .
				///       .          .          .          .          .
				///       .          .          .          .          .
				///       .          .          .          .          .
				///        __________ .......... .......... __________
				///            ^     |          .               ^
				///            .     |          .               .
				///            B-L..>|          .               .
				///                  |          .               .
				///                  |__________.               .
				///                       ^                     .
				///                       ....................B-B
				///
				/// Those are:
				///     B-L, B-B
				///     T-R, T-T
				///     (and in reverse order, R-T and L-B)
				///
				/// If we add, R-R and L-L (which never occur), we get:
				///     B-L, B-B
				///     T-R, T-T
				///     R-T, R-R
				///     L-B, L-L
				///
				/// And if L = 0, R = 1, T = 2, B = 3 as in NeighbourSides enumeration,
				/// a general rule can be derived for when to flip bbMin and bbMax:
				///     if ((a+b) == 3 || (a == b))
				///     {
				///        ..flip bbMin and bbMax
				///     }
				///
				float bbMin = bleed[side].m_bbMin;
				float bbMax = bleed[side].m_bbMax;
				if ( (side == neighbourFaceEdge)
				||   (3    == (side + neighbourFaceEdge) ) )
				{
					// Flip.
					bbMin = 1.0f - bbMin;
					bbMax = 1.0f - bbMax;
				}

				switch (neighbourFaceEdge)
				{
				case CubeMapFace::Edge::Left:
					{
						///  --> U
						/// |  .............
						/// v  .           .
						/// V  x___        .
						///    |   |       .
						///    |   |       .
						///    |___x       .
						///    .           .
						///    .............
						///
						_outFilterArea[neighbourFaceIdx].add(0.0f, bbMin);
						_outFilterArea[neighbourFaceIdx].add(bleedAmount, bbMax);
					}
					break;

				case CubeMapFace::Edge::Right:
					{
						///  --> U
						/// |  .............
						/// v  .           .
						/// V  .       x___.
						///    .       |   |
						///    .       |   |
						///    .       |___x
						///    .           .
						///    .............
						///
						_outFilterArea[neighbourFaceIdx].add(1.0f - bleedAmount, bbMin);
						_outFilterArea[neighbourFaceIdx].add(1.0f, bbMax);
					}
					break;

				case CubeMapFace::Edge::Top:
					{
						///  --> U
						/// |  ...x____ ...
						/// v  .  |    |  .
						/// V  .  |____x  .
						///    .          .
						///    .          .
						///    .          .
						///    ............
						///
						_outFilterArea[neighbourFaceIdx].add(bbMin, 0.0f);
						_outFilterArea[neighbourFaceIdx].add(bbMax, bleedAmount);
					}
					break;

				case CubeMapFace::Edge::Bottom:
					{
						///  --> U
						/// |  ............
						/// v  .          .
						/// V  .          .
						///    .          .
						///    .  x____   .
						///    .  |    |  .
						///    ...|____x...
						///
						_outFilterArea[neighbourFaceIdx].add(bbMin, 1.0f - bleedAmount);
						_outFilterArea[neighbourFaceIdx].add(bbMax, 1.0f);
					}
					break;
				}

				// Clamp bounding box to face size.
				_outFilterArea[neighbourFaceIdx].clamp(0.0f, 1.0f);
			}
		}
	}

	void processFilterArea(
		  float* _result
		, const ImageContainer& _image
		, uint8_t _lod
		, const Aabb* _aabb
		, const float* _dir
		, float _specularPower
		, float _specularAngle
		)
	{
		float color[3] = { 0.0f, 0.0f, 0.0f };
		float totalWeight = 0.0f;

		const uint32_t bpp   = getBitsPerPixel(_image.m_format);

		UnpackFn unpack = getUnpack(_image.m_format);

		for (uint8_t side = 0; side < 6; ++side)
		{
			if (_aabb[side].isEmpty() )
			{
				continue;
			}

			ImageMip mip;
			if (imageGetRawData(_image, side, _lod, _image.m_data, _image.m_size, mip) )
			{
				const uint32_t pitch      = mip.m_width*bpp/8;
				const float widthMinusOne = float(mip.m_width-1);
				const float invWidth      = 1.0f/float(mip.m_width);

				const uint32_t minX = uint32_t(_aabb[side].m_min[0] * widthMinusOne);
				const uint32_t maxX = uint32_t(_aabb[side].m_max[0] * widthMinusOne);
				const uint32_t minY = uint32_t(_aabb[side].m_min[1] * widthMinusOne);
				const uint32_t maxY = uint32_t(_aabb[side].m_max[1] * widthMinusOne);

				for (uint32_t yy = minY; yy <= maxY; ++yy)
				{
					const uint8_t* row = mip.m_data + yy*pitch;

					for (uint32_t xx = minX; xx <= maxX; ++xx)
					{
						const float uu = float(xx)*invWidth*2.0f - 1.0f;
						const float vv = float(yy)*invWidth*2.0f - 1.0f;

						float normal[4];
						texelUvToDir(normal, side, uu, vv);
						const float solidAngle = texelSolidAngle(uu, vv, invWidth);

						const float ndotl = bx::clamp(bx::vec3Dot(normal, _dir), 0.0f, 1.0f);

						if (ndotl >= _specularAngle)
						{
							const float weight = solidAngle * bx::pow(ndotl, _specularPower);

							float rgba[4];
							unpack(rgba, row + xx*bpp/8);

							color[0] += rgba[0] * weight;
							color[1] += rgba[1] * weight;
							color[2] += rgba[2] * weight;
							totalWeight += weight;
						}
					}
				}

				if (0.0f < totalWeight)
				{
					const float invWeight = 1.0f/totalWeight;
					_result[0] = color[0] * invWeight;
					_result[1] = color[1] * invWeight;
					_result[2] = color[2] * invWeight;
				}
				else
				{
					float uu, vv;
					uint8_t face;
					dirToTexelUv(uu, vv, face, _dir);

					imageGetRawData(_image, face, 0, _image.m_data, _image.m_size, mip);

					const uint32_t xx = uint32_t(uu*widthMinusOne);
					const uint32_t yy = uint32_t(vv*widthMinusOne);

					float rgba[4];
					unpack(rgba, mip.m_data + yy*pitch + xx*bpp/8);

					_result[0] = rgba[0];
					_result[1] = rgba[1];
					_result[2] = rgba[2];
				}
			}
		}
	}

	ImageContainer* imageGenerateMips(bx::AllocatorI* _allocator, const ImageContainer& _image)
	{
		if (_image.m_format != TextureFormat::RGBA8
		&&  _image.m_format != TextureFormat::RGBA32F)
		{
			return NULL;
		}

		ImageContainer* output = imageAlloc(_allocator, _image.m_format, uint16_t(_image.m_width), uint16_t(_image.m_height), uint16_t(_image.m_depth), _image.m_numLayers, _image.m_cubeMap, true);

		const uint32_t numMips   = output->m_numMips;
		const uint32_t numLayers = output->m_numLayers;
		const uint32_t numSides  = output->m_cubeMap ? 6 : 1;

		for (uint32_t layer = 0; layer < numLayers; ++layer)
		{
			for (uint8_t side = 0; side < numSides; ++side)
			{
				ImageMip mip;
				if (imageGetRawData(_image, uint16_t(layer*numSides + side), 0, _image.m_data, _image.m_size, mip) )
				{
					for (uint8_t lod = 0; lod < numMips; ++lod)
					{
						ImageMip srcMip;
						imageGetRawData(*output, uint16_t(layer*numSides + side), lod == 0 ? 0 : lod-1, output->m_data, output->m_size, srcMip);

						ImageMip dstMip;
						imageGetRawData(*output, uint16_t(layer*numSides + side), lod, output->m_data, output->m_size, dstMip);

						uint8_t* dstData = const_cast<uint8_t*>(dstMip.m_data);

						if (0 == lod)
						{
							bx::memCopy(dstData, mip.m_data, mip.m_size);
						}
						else if (output->m_format == TextureFormat::RGBA8)
						{
							imageRgba8Downsample2x2(
								  dstData
								, srcMip.m_width
								, srcMip.m_height
								, srcMip.m_depth
								, srcMip.m_width*4
								, dstMip.m_width*4
								, srcMip.m_data
								);
						}
						else if (output->m_format == TextureFormat::RGBA32F)
						{
							imageRgba32fDownsample2x2(
								  dstData
								, srcMip.m_width
								, srcMip.m_height
								, srcMip.m_depth
								, srcMip.m_width*16
								, srcMip.m_data
								);
						}
					}
				}
			}
		}

		return output;
	}

	ImageContainer* imageCubemapRadianceFilter(bx::AllocatorI* _allocator, const ImageContainer& _image, float _filterSize)
	{
		ImageContainer* output = imageConvert(_allocator, TextureFormat::RGBA32F, _image, true);

		if (1 >= output->m_numMips)
		{
			ImageContainer* temp = imageGenerateMips(_allocator, *output);
			imageFree(output);
			output = temp;
		}

		const uint32_t numMips = output->m_numMips;

		for (uint8_t side = 0; side < 6; ++side)
		{
			for (uint8_t lod = 0; lod < numMips; ++lod)
			{
				ImageMip mip;
				imageGetRawData(*output, side, lod, output->m_data, output->m_size, mip);

				const uint32_t dstWidth = mip.m_width;
				const uint32_t dstPitch = dstWidth*16;
				const float invDstWidth = 1.0f / float(dstWidth);

				for (uint32_t yy = 0; yy < dstWidth; ++yy)
				{
					for (uint32_t xx = 0; xx < dstWidth; ++xx)
					{
						float* dstData = (float*)&mip.m_data[yy*dstPitch+xx*16];

						const float uu = float(xx)*invDstWidth*2.0f - 1.0f;
						const float vv = float(yy)*invDstWidth*2.0f - 1.0f;

						float dir[3];
						texelUvToDir(dir, side, uu, vv);

						Aabb aabb[6];
						calcFilterArea(aabb, dir, _filterSize);

						processFilterArea(dstData, *output, lod, aabb, dir, 10.0f, 0.2f);
					}
				}
			}
		}

		return output;
	}

} // namespace bimg
