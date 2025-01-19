#include "Waves.h"
#include <ppl.h>
#include <algorithm>
#include <vector>
#include <cassert>

using namespace DirectX;

Waves::Waves(int m, int n, float dx, float dt, float speed, float damping)
{
	_nrRows = m;
	_nrCols = n;

	_vertexCount = m * n;
	_triangleCount = (m - 1) * (n - 1) * 2;

	_timeStep = dt;
	_spatialStep = dx;

	float d = damping * dt + 2.0f;
	float e = (speed * speed) * (dt * dt) / (dx * dx);
	_simConstant1 = (damping * dt - 2.0f) / d;
	_simConstant2 = (4.0f - 8.0f * e) / d;
	_simConstant3 = (2.0f * e) / d;

	_prevSolution.resize(m * n);
	_currentSolution.resize(m * n);
	_normals.resize(m * n);
	_tangentX.resize(m * n);

	// Generate grid vertices in system memory.

	float halfWidth = (n - 1) * dx * 0.5f;
	float halfDepth = (m - 1) * dx * 0.5f;
	for (int i = 0; i < m; ++i)
	{
		float z = halfDepth - i * dx;
		for (int j = 0; j < n; ++j)
		{
			float x = -halfWidth + j * dx;

			_prevSolution[i * n + j] = XMFLOAT3(x, 0.0f, z);
			_currentSolution[i * n + j] = XMFLOAT3(x, 0.0f, z);
			_normals[i * n + j] = XMFLOAT3(0.0f, 1.0f, 0.0f);
			_tangentX[i * n + j] = XMFLOAT3(1.0f, 0.0f, 0.0f);
		}
	}
}

Waves::~Waves()
{}

int Waves::RowCount()const
{
	return _nrRows;
}

int Waves::ColumnCount()const
{
	return _nrCols;
}

int Waves::VertexCount()const
{
	return _vertexCount;
}

int Waves::TriangleCount()const
{
	return _triangleCount;
}

float Waves::Width()const
{
	return _nrCols * _spatialStep;
}

float Waves::Depth()const
{
	return _nrRows * _spatialStep;
}

void Waves::Update(float dt)
{
	static float t = 0;

	// Accumulate time.
	t += dt;

	// Only update the simulation at the specified time step.
	if (t >= _timeStep)
	{
		// Only update interior points; we use zero boundary conditions.
		concurrency::parallel_for(1, _nrRows - 1, [this] (int i)
			//for(int i = 1; i < mNumRows-1; ++i)
			{
				for (int j = 1; j < _nrCols - 1; ++j)
				{
					// After this update we will be discarding the old previous
					// buffer, so overwrite that buffer with the new update.
					// Note how we can do this inplace (read/write to same element) 
					// because we won't need prev_ij again and the assignment happens last.

					// Note j indexes x and i indexes z: h(x_j, z_i, t_k)
					// Moreover, our +z axis goes "down"; this is just to 
					// keep consistent with our row indices going down.

					_prevSolution[i * _nrCols + j].y =
						_simConstant1 * _prevSolution[i * _nrCols + j].y +
						_simConstant2 * _currentSolution[i * _nrCols + j].y +
						_simConstant3 * (_currentSolution[(i + 1) * _nrCols + j].y +
							_currentSolution[(i - 1) * _nrCols + j].y +
							_currentSolution[i * _nrCols + j + 1].y +
							_currentSolution[i * _nrCols + j - 1].y);
				}
			});

		// We just overwrote the previous buffer with the new data, so
		// this data needs to become the current solution and the old
		// current solution becomes the new previous solution.
		std::swap(_prevSolution, _currentSolution);

		t = 0.0f; // reset time

		//
		// Compute normals using finite difference scheme.
		//
		concurrency::parallel_for(1, _nrRows - 1, [this] (int i)
			//for(int i = 1; i < mNumRows - 1; ++i)
			{
				for (int j = 1; j < _nrCols - 1; ++j)
				{
					float l = _currentSolution[i * _nrCols + j - 1].y;
					float r = _currentSolution[i * _nrCols + j + 1].y;
					float t = _currentSolution[(i - 1) * _nrCols + j].y;
					float b = _currentSolution[(i + 1) * _nrCols + j].y;
					_normals[i * _nrCols + j].x = -r + l;
					_normals[i * _nrCols + j].y = 2.0f * _spatialStep;
					_normals[i * _nrCols + j].z = b - t;

					XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&_normals[i * _nrCols + j]));
					XMStoreFloat3(&_normals[i * _nrCols + j], n);

					_tangentX[i * _nrCols + j] = XMFLOAT3(2.0f * _spatialStep, r - l, 0.0f);
					XMVECTOR T = XMVector3Normalize(XMLoadFloat3(&_tangentX[i * _nrCols + j]));
					XMStoreFloat3(&_tangentX[i * _nrCols + j], T);
				}
			});
	}
}

void Waves::Disturb(int i, int j, float magnitude)
{
	// Don't disturb boundaries.
	assert(i > 1 && i < _nrRows - 2);
	assert(j > 1 && j < _nrCols - 2);

	float halfMag = 0.5f * magnitude;

	// Disturb the ijth vertex height and its neighbors.
	_currentSolution[i * _nrCols + j].y += magnitude;
	_currentSolution[i * _nrCols + j + 1].y += halfMag;
	_currentSolution[i * _nrCols + j - 1].y += halfMag;
	_currentSolution[(i + 1) * _nrCols + j].y += halfMag;
	_currentSolution[(i - 1) * _nrCols + j].y += halfMag;
}
