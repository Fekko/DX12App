#pragma once

#include <vector>
#include <DirectXMath.h>

class Waves
{
public:
    Waves(int m, int n, float dx, float dt, float speed, float damping);
    Waves(const Waves& rhs) = delete;
    Waves& operator=(const Waves& rhs) = delete;
    ~Waves();

    int RowCount()const;
    int ColumnCount()const;
    int VertexCount()const;
    int TriangleCount()const;
    float Width()const;
    float Depth()const;

    const DirectX::XMFLOAT3& Position(int i)const { return _currentSolution[i]; }
    const DirectX::XMFLOAT3& Normal(int i)const { return _normals[i]; }
    const DirectX::XMFLOAT3& TangentX(int i)const { return _tangentX[i]; }

    void Update(float dt);
    void Disturb(int i, int j, float magnitude);

private:
    int _nrRows{};
    int _nrCols{};

    int _vertexCount{};
    int _triangleCount{};

    // Simulation constants we can precompute.
    float _simConstant1{};
    float _simConstant2{};
    float _simConstant3{};

    float _timeStep{};
    float _spatialStep{};

    std::vector<DirectX::XMFLOAT3> _prevSolution;
    std::vector<DirectX::XMFLOAT3> _currentSolution;
    std::vector<DirectX::XMFLOAT3> _normals;
    std::vector<DirectX::XMFLOAT3> _tangentX;
};

